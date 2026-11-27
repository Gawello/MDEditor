#include "notes/NoteIndex.h"

#include "app/Background.h"
#include "editor/LivePreviewHighlighter.h" // toView
#include "editor/MarkdownEditor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QUrl>

#include <algorithm>

NoteIndex::NoteIndex(QObject *parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<Result>::finished, this, &NoteIndex::finished);
}

NoteIndex::~NoteIndex() = default; // a running scan owns copies only, and may finish after this

void NoteIndex::setRoot(const QString &root)
{
    const QString clean = root.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    if (clean == m_root)
        return;
    m_root = clean;
    m_files.clear();
    m_notes.clear();
    m_byName.clear();
    emit changed();
    refresh();
}

void NoteIndex::refresh()
{
    if (m_root.isEmpty())
        return;
    if (m_watcher.isRunning()) {
        m_again = true;
        return;
    }
    const QString root = m_root;
    const Notes previous = m_notes;
    m_watcher.setFuture(runInBackground([root, previous] { return scan(root, previous); }));
}

NoteIndex::Result NoteIndex::scan(const QString &root, const Notes &previous)
{
    Result result;
    result.root = root;
    // Hidden folders (.git, .obsidian, .trash) and the usual dependency folders are skipped.
    QStringList folders{root};
    while (!folders.isEmpty() && result.files.size() < maxFiles) {
        const QString folder = folders.takeFirst();
        const QFileInfoList entries =
            QDir(folder).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &entry : entries) {
            if (entry.fileName().startsWith(u'.'))
                continue;
            if (entry.isDir()) {
                if (!entry.isSymLink() && entry.fileName() != u"node_modules")
                    folders << entry.absoluteFilePath();
                continue;
            }
            const QString path = entry.absoluteFilePath();
            result.files << path;
            if (!MarkdownEditor::isMarkdownFile(path) || entry.size() > maxNoteSize)
                continue;
            const auto old = previous.constFind(path);
            if (old != previous.constEnd() && (*old)->modified == entry.lastModified() && (*old)->size == entry.size()) {
                result.notes.insert(path, *old);
                continue;
            }
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                continue;
            auto note = std::make_shared<Note>();
            note->path = path;
            note->modified = entry.lastModified();
            note->size = entry.size();
            const QString text = QString::fromUtf8(file.readAll());
            note->scan = md::scanNote(toView(text));
            result.notes.insert(path, note);
        }
    }
    result.files.sort(Qt::CaseInsensitive);
    return result;
}

void NoteIndex::finished()
{
    Result result = m_watcher.result();
    if (result.root == m_root) {
        m_files = std::move(result.files);
        m_notes = std::move(result.notes);
        m_byName.clear();
        for (const QString &path : std::as_const(m_files)) {
            const QFileInfo info(path);
            const QString name = MarkdownEditor::isMarkdownFile(path) ? info.completeBaseName() : info.fileName();
            m_byName[name.toLower()] << path;
        }
        emit changed();
    }
    if (m_again) {
        m_again = false;
        refresh();
    }
}

void NoteIndex::update(const QString &path, const QString &text)
{
    const QString clean = QDir::cleanPath(path);
    if (m_root.isEmpty() || !clean.startsWith(m_root + u'/'))
        return;
    auto note = std::make_shared<Note>();
    note->path = clean;
    // No date: the next scan reads the file again.
    note->scan = md::scanNote(toView(text));
    m_notes.insert(clean, note);
    if (!m_files.contains(clean)) {
        m_files << clean;
        m_files.sort(Qt::CaseInsensitive);
        m_byName[QFileInfo(clean).completeBaseName().toLower()] << clean;
    }
    emit changed();
}

QStringList NoteIndex::noteNames() const
{
    QStringList names;
    for (auto it = m_notes.constBegin(); it != m_notes.constEnd(); ++it)
        names << QFileInfo(it.key()).completeBaseName();
    names.sort(Qt::CaseInsensitive);
    names.removeDuplicates();
    return names;
}

QString NoteIndex::resolve(const QString &target, bool wiki, const QString &from) const
{
    QString path = QUrl::fromPercentEncoding(target.trimmed().toUtf8());
    if (path.isEmpty())
        return {};
    const QDir here(QFileInfo(from).absolutePath());
    const auto existing = [](const QString &candidate) -> QString {
        const QFileInfo info(candidate);
        if (info.isFile())
            return QDir::cleanPath(info.absoluteFilePath());
        if (info.suffix().isEmpty() && QFileInfo::exists(candidate + QStringLiteral(".md")))
            return QDir::cleanPath(QFileInfo(candidate + QStringLiteral(".md")).absoluteFilePath());
        return {};
    };

    if (!wiki || path.contains(u'/')) {
        if (QString found = existing(QDir::isAbsolutePath(path) ? path : here.filePath(path)); !found.isEmpty())
            return found;
        if (wiki && !m_root.isEmpty()) {
            if (QString found = existing(QDir(m_root).filePath(path)); !found.isEmpty())
                return found;
        }
        if (!wiki)
            return {};
    }

    // [[Name]]: any note of that name; the one closest to `from` wins (same folder, then shortest path).
    QString name = QFileInfo(path).fileName();
    if (name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
        name.chop(3);
    QStringList candidates = m_byName.value(name.toLower());
    if (candidates.isEmpty()) {
        if (QString found = existing(here.filePath(path)); !found.isEmpty())
            return found; // outside the folder, or no folder open
        candidates = m_byName.value(QFileInfo(path).fileName().toLower()); // pic.png
        if (candidates.isEmpty())
            return {};
    }
    const QString folder = QDir::cleanPath(here.absolutePath());
    const auto distance = [&folder](const QString &candidate) {
        const QString dir = QFileInfo(candidate).absolutePath();
        return (dir == folder ? 0 : 1000) + int(candidate.size());
    };
    return *std::min_element(candidates.cbegin(), candidates.cend(),
                             [&](const QString &a, const QString &b) { return distance(a) < distance(b); });
}

QHash<QString, int> NoteIndex::tagCounts() const
{
    QHash<QString, int> counts;
    for (const auto &note : m_notes) {
        QSet<QString> seen; // a note counts once per tag, also when it has "a/b" and "a/c"
        for (const std::u16string &tag : note->scan.tags) {
            const QString name = QString::fromStdU16String(tag);
            const QStringList parts = name.split(u'/', Qt::SkipEmptyParts);
            for (int i = 1; i <= parts.size(); ++i)
                seen.insert(parts.mid(0, i).join(u'/'));
        }
        for (const QString &tag : std::as_const(seen))
            ++counts[tag];
    }
    return counts;
}
