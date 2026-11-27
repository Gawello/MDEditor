#include "app/EditorTab.h"

#include "app/AppSettings.h"
#include "app/Background.h"
#include "app/ExternalChangeBar.h"
#include "app/FindBar.h"
#include "document/Encodings.h"
#include "editor/MarkdownEditor.h"
#include "plugins/PluginManager.h"

#include <QFileInfo>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

EditorTab::EditorTab(QWidget *parent)
    : QWidget(parent)
    , m_editor(new MarkdownEditor(this))
    , m_findBar(new FindBar(m_editor, this))
    , m_changeBar(new ExternalChangeBar(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_changeBar);
    layout->addWidget(m_findBar);
    layout->addWidget(m_editor);
    setFocusProxy(m_editor);

    connect(m_editor->document(), &QTextDocument::modificationChanged, this, &EditorTab::stateChanged);
    connect(m_editor->document(), &QTextDocument::contentsChange, this, &EditorTab::mirror);
    connect(&m_diskCheck, &QFutureWatcher<DiskCheck>::finished, this, &EditorTab::handleDiskCheck);
    connect(m_changeBar, &ExternalChangeBar::reloadRequested, this, [this] {
        QString error;
        if (!reload(error))
            emit statusMessage(tr("Cannot reload %1: %2").arg(displayName(), error));
    });
    connect(m_changeBar, &ExternalChangeBar::keepRequested, this, &EditorTab::keepMyVersion);
}

bool EditorTab::isModified() const
{
    return m_editor->document()->isModified();
}

bool EditorTab::isPristine() const
{
    return isUntitled() && !isModified() && m_editor->document()->isEmpty();
}

QString EditorTab::waitLabel() const
{
    return tr("Waiting for %1…").arg(displayName());
}

bool EditorTab::load(const QString &path, QString &error, bool *binary)
{
    const QString fallback = AppSettings::instance().fallbackEncoding();
    const auto file = runAndWait([path, fallback] { return Document::read(path, fallback); },
                                 tr("Waiting for %1…").arg(QFileInfo(path).fileName()), this);
    if (!file) {
        error = tr("Stopped waiting for the drive.");
        return false;
    }
    if (!file->ok) {
        error = file->error;
        if (binary)
            *binary = file->binary;
        return false;
    }

    m_document.adopt(*file);
    // Path and mode first, so relative image paths resolve during the first highlight, and a big
    // non-Markdown file is never parsed as Markdown.
    m_editor->setDocumentPath(m_document.filePath());
    m_editor->setMarkdownMode(MarkdownEditor::isMarkdownFile(m_document.filePath()));
    m_editor->setMarkdown(file->text);
    m_editor->document()->setModified(false);
    m_changeBar->hide();
    emit stateChanged();
    if (file->lossy)
        emit statusMessage(tr("%1 has bytes that aren't valid %2: saving will replace them")
                               .arg(displayName(), Encodings::label(file->encoding, false)));
    PluginManager::instance().notifyOpen(m_document.filePath());
    return true;
}

bool EditorTab::reload(QString &error, const QString &encoding)
{
    const QString path = filePath();
    const QString forced = encoding; // otherwise detected again: the file may have been rewritten as UTF-8
    const QString fallback = AppSettings::instance().fallbackEncoding();
    const auto file = runAndWait([path, fallback, forced] { return Document::read(path, fallback, forced); },
                                 waitLabel(), this);
    if (!file) {
        error = tr("Stopped waiting for the drive.");
        return false;
    }
    if (!file->ok) {
        error = file->error;
        return false;
    }

    m_document.adopt(*file);
    m_editor->replaceAll(file->text); // through the undo stack: the discarded text is one Ctrl+Z away
    m_editor->document()->setModified(false);
    m_changeBar->hide();
    emit stateChanged();
    shareFileState();
    return true;
}

EditorTab::SaveResult EditorTab::save(const QString &path, QString &error, bool overwrite)
{
    // Save As targets a different file, and the file dialog already asked about replacing it.
    if (!overwrite && !isUntitled() && QFileInfo(path).absoluteFilePath() == filePath()) {
        const FileStamp known = m_document.stamp();
        const auto check = runAndWait([path, known] { return Document::check(path, known); }, waitLabel(), this);
        if (!check) {
            error = tr("Stopped waiting for the drive. Nothing was saved.");
            return SaveResult::Failed;
        }
        if (check->state == DiskState::Changed || check->state == DiskState::Missing) {
            m_conflictState = check->state;
            return SaveResult::Conflict;
        }
    }

    if (AppSettings::instance().formatOnSave())
        m_editor->formatDocument(true);
    // Plugins may adjust the text before it's written (e.g. update a "modified" date).
    m_editor->replaceAll(PluginManager::instance().notifyBeforeSave(path, m_editor->markdown()));

    bool encodable = true;
    const QByteArray data = m_document.encode(m_editor->markdown(), &encodable);
    if (!encodable) {
        error = tr("Some characters can't be written in %1.").arg(Encodings::label(m_document.encoding(), false));
        return SaveResult::Unencodable;
    }
    const auto file = runAndWait([path, data] { return Document::write(path, data); }, waitLabel(), this);
    if (!file) {
        // The write can't be cancelled, so the tab stays modified and the next save checks the disk again.
        error = tr("Stopped waiting for the drive. The file may still be written later: check it before relying on it.");
        return SaveResult::Failed;
    }
    if (!file->ok) {
        error = file->error;
        return SaveResult::Failed;
    }

    const bool renamed = m_document.filePath() != file->path;
    m_document.adopt(*file);
    m_editor->document()->setModified(false);
    m_editor->setDocumentPath(m_document.filePath());
    if (renamed) // Save As may have changed the type: notes.md -> script.py
        m_editor->setMarkdownMode(MarkdownEditor::isMarkdownFile(m_document.filePath()));
    m_changeBar->hide();
    emit stateChanged();
    shareFileState();
    PluginManager::instance().notifySave(m_document.filePath());
    return SaveResult::Saved;
}

void EditorTab::setFilePath(const QString &path)
{
    const bool typeChanged =
        MarkdownEditor::isMarkdownFile(path) != MarkdownEditor::isMarkdownFile(m_document.filePath());
    m_document.setFilePath(path);
    m_editor->setDocumentPath(path);
    if (typeChanged)
        m_editor->setMarkdownMode(MarkdownEditor::isMarkdownFile(path));
    emit stateChanged();
}

void EditorTab::setEncoding(const QString &encoding, bool bom)
{
    if (encoding == m_document.encoding() && bom == m_document.hasBom())
        return;
    m_document.setEncoding(encoding, bom);
    m_editor->document()->setModified(true);
    emit stateChanged();
    shareFileState();
}

void EditorTab::setCrlf(bool crlf)
{
    if (crlf == m_document.usesCrlf())
        return;
    m_document.setCrlf(crlf);
    m_editor->document()->setModified(true);
    emit stateChanged();
    shareFileState();
}

void EditorTab::openTwin(EditorTab *twin)
{
    unlinkTwin();
    twin->unlinkTwin();
    twin->m_document = m_document;
    twin->m_editor->setDocumentPath(filePath());
    twin->m_editor->setMarkdownMode(m_editor->markdownMode());
    twin->m_editor->setMarkdown(m_editor->markdown());
    twin->m_editor->document()->setModified(isModified());
    m_twin = twin;
    twin->m_twin = this;
    emit twin->stateChanged();
}

void EditorTab::unlinkTwin()
{
    if (m_twin)
        m_twin->m_twin = nullptr;
    m_twin = nullptr;
}

// Applies an edit of this tab's text to the twin's. Highlighters change formats only, which emits the same
// signal with unchanged text: those are skipped by comparing.
void EditorTab::mirror(int position, int removed, int added)
{
    if (!m_twin || m_mirroring || m_twin->m_mirroring)
        return;
    QTextDocument *from = m_editor->document();
    QTextDocument *to = m_twin->m_editor->document();
    // At the end of the document Qt counts the final paragraph separator too: clamp to the real text.
    const int fromEnd = from->characterCount() - 1;
    const int toEnd = to->characterCount() - 1;
    QTextCursor source(from);
    source.setPosition(std::min(position, fromEnd));
    source.setPosition(std::min(position + added, fromEnd), QTextCursor::KeepAnchor);
    QString text = source.selectedText();
    text.replace(QChar::ParagraphSeparator, u'\n');

    QTextCursor target(to);
    target.setPosition(std::min(position, toEnd));
    target.setPosition(std::min(position + removed, toEnd), QTextCursor::KeepAnchor);
    QString current = target.selectedText();
    current.replace(QChar::ParagraphSeparator, u'\n');
    if (current == text)
        return;
    m_twin->m_mirroring = true;
    target.insertText(text);
    m_twin->m_mirroring = false;
    // Edits can be anywhere: if both documents ever differ in length, start the twin over from this text.
    if (to->characterCount() != from->characterCount()) {
        m_twin->m_mirroring = true;
        m_twin->m_editor->replaceAll(m_editor->markdown());
        m_twin->m_mirroring = false;
    }
}

void EditorTab::shareFileState()
{
    if (!m_twin)
        return;
    m_twin->m_document = m_document;
    m_twin->m_editor->setDocumentPath(filePath());
    m_twin->m_editor->document()->setModified(isModified());
    m_twin->m_changeBar->hide();
    emit m_twin->stateChanged();
}

void EditorTab::checkDisk()
{
    if (isUntitled() || m_diskCheck.isRunning())
        return; // one at a time: on a hung drive this would pile up a thread per check
    const QString path = filePath();
    const FileStamp known = m_document.stamp();
    m_checkedPath = path;
    m_checkedHash = known.hash;
    m_diskCheck.setFuture(runInBackground([path, known] { return Document::check(path, known); }));
}

void EditorTab::handleDiskCheck()
{
    const DiskCheck check = m_diskCheck.result();
    // Saved or reloaded meanwhile: the result is about an older version.
    if (m_checkedPath != filePath() || m_checkedHash != m_document.stamp().hash) {
        emit diskCheckFinished();
        return;
    }

    switch (check.state) {
    case DiskState::Unchanged:
        m_document.setStamp(check.stamp); // same content with a new date: don't read it again next time
        m_changeBar->hide();
        break;
    case DiskState::Changed:
        if (isModified()) {
            m_pendingStamp = check.stamp;
            m_changeBar->showChanged(displayName());
        } else if (QString error; reload(error)) {
            emit statusMessage(tr("Reloaded %1: it changed on disk").arg(displayName()));
        } else {
            emit statusMessage(tr("Cannot reload %1: %2").arg(displayName(), error));
        }
        break;
    case DiskState::Missing:
        m_pendingStamp = FileStamp();
        m_changeBar->showMissing(displayName());
        break;
    case DiskState::Unknown:
        break; // a temporary problem: the next check or a save will tell
    }
    emit diskCheckFinished();
}

void EditorTab::keepMyVersion()
{
    m_document.setStamp(m_pendingStamp); // the user has seen this version, so saving replaces it unasked
    m_changeBar->hide();
}
