#include "panels/LinksPanel.h"

#include "app/Background.h"
#include "editor/LivePreviewHighlighter.h" // toView
#include "editor/MarkdownEditor.h"
#include "markdown/InlineParser.h"
#include "markdown/NoteScan.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int pathRole = Qt::UserRole;
constexpr int lineRole = Qt::UserRole + 1;
constexpr int columnRole = Qt::UserRole + 2;
constexpr int lengthRole = Qt::UserRole + 3;
constexpr int targetRole = Qt::UserRole + 4; // outgoing: the link as written
constexpr qint64 maxFileSize = 4 * 1024 * 1024;

QStringList readLines(const QString &path)
{
    QFile file(path);
    if (file.size() > maxFileSize || !file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = file.readAll();
    if (bytes.left(65536).contains('\0'))
        return {};
    QStringList lines = QString::fromUtf8(bytes).split(u'\n');
    for (QString &line : lines) {
        if (line.endsWith(u'\r'))
            line.chop(1);
    }
    return lines;
}

QTreeWidget *makeTree(QWidget *parent)
{
    auto *tree = new QTreeWidget(parent);
    tree->setHeaderHidden(true);
    tree->setFrameShape(QFrame::NoFrame);
    tree->setIndentation(14);
    tree->setColumnCount(2);
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree->setWordWrap(false);
    tree->setTextElideMode(Qt::ElideRight);
    return tree;
}

QLabel *makeEmpty(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setAlignment(Qt::AlignCenter);
    label->setForegroundRole(QPalette::PlaceholderText);
    label->setWordWrap(true);
    return label;
}

} // namespace

// --- Backlinks ----------------------------------------------------------------------------------

BacklinksPanel::BacklinksPanel(NoteIndex *index, QWidget *parent)
    : QWidget(parent)
    , m_index(index)
    , m_tree(makeTree(this))
    , m_empty(makeEmpty(tr("No note open"), this))
    , m_timer(new QTimer(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tree);
    layout->addWidget(m_empty);
    layout->addStretch();
    m_timer->setSingleShot(true);
    m_timer->setInterval(300);
    connect(m_timer, &QTimer::timeout, this, &BacklinksPanel::start);
    connect(m_index, &NoteIndex::changed, this, &BacklinksPanel::refresh);
    connect(&m_watcher, &QFutureWatcher<Result>::finished, this, [this] {
        const Result result = m_watcher.result();
        if (result.target == m_note)
            show(result);
    });
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        const QString path = item->data(0, pathRole).toString();
        if (!path.isEmpty())
            emit openRequested(path, item->data(0, lineRole).toInt(), item->data(0, columnRole).toInt(),
                               item->data(0, lengthRole).toInt());
    });
    m_tree->hide();
}

BacklinksPanel::~BacklinksPanel()
{
    if (m_cancel)
        *m_cancel = true;
}

void BacklinksPanel::setNote(const QString &path)
{
    const QString clean = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (clean == m_note)
        return;
    m_note = clean;
    m_tree->clear();
    start();
}

void BacklinksPanel::refresh()
{
    m_timer->start();
}

bool BacklinksPanel::busy() const
{
    return m_watcher.isRunning() || m_timer->isActive();
}

void BacklinksPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    start(); // not kept up to date while hidden
}

void BacklinksPanel::start()
{
    m_timer->stop();
    if (m_cancel)
        *m_cancel = true;
    if (m_note.isEmpty() || !MarkdownEditor::isMarkdownFile(m_note)) {
        m_tree->clear();
        m_tree->hide();
        m_empty->setText(tr("No note open"));
        m_empty->show();
        return;
    }
    if (!isVisible())
        return;
    // Whole words of the note's name and aliases, other notes only.
    QStringList names{QFileInfo(m_note).completeBaseName()};
    names += m_index->aliases(m_note);
    QStringList notes;
    for (const QString &path : m_index->files()) {
        if (MarkdownEditor::isMarkdownFile(path))
            notes << path;
    }
    const QList<NoteIndex::Backlink> links = m_index->backlinks(m_note);
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    const auto cancel = m_cancel;
    const QString target = m_note;
    m_watcher.setFuture(runInBackground([target, links, notes, names, cancel] {
        return collect(target, links, notes, names, cancel.get());
    }));
}

BacklinksPanel::Result BacklinksPanel::collect(const QString &target, const QList<NoteIndex::Backlink> &links,
                                               const QStringList &notes, const QStringList &names,
                                               const std::atomic<bool> *cancel)
{
    Result result;
    result.target = target;

    // Linked: the lines with the links (one entry per line).
    QString current;
    QStringList lines;
    int lastLine = -1;
    for (const NoteIndex::Backlink &link : links) {
        if (cancel && *cancel)
            return result;
        if (link.source != current) {
            current = link.source;
            lines = readLines(current);
            lastLine = -1;
        }
        if (link.line == lastLine)
            continue;
        lastLine = link.line;
        result.linked.append({link.source, link.line, 0, 0, lines.value(link.line).trimmed()});
    }

    // Unlinked: the names in plain text, outside of code, links, tags and the front matter.
    QStringList wanted;
    for (const QString &name : names) {
        if (!name.trimmed().isEmpty())
            wanted << name.trimmed();
    }
    std::sort(wanted.begin(), wanted.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
    for (const QString &path : notes) {
        if ((cancel && *cancel) || result.unlinked.size() >= maxMentions)
            break;
        if (path == target)
            continue;
        const QStringList text = readLines(path);
        std::vector<std::u16string_view> views;
        views.reserve(size_t(text.size()));
        for (const QString &line : text)
            views.push_back(toView(line));
        const std::vector<md::BlockInfo> blocks = md::parseBlocks(views);
        for (int i = 0; i < text.size() && result.unlinked.size() < maxMentions; ++i) {
            const md::BlockType type = blocks[size_t(i)].type;
            if (type == md::BlockType::Blank || type == md::BlockType::CodeLine || type == md::BlockType::FenceOpen
                || type == md::BlockType::FenceClose || md::isFrontmatterType(type) || md::isMathType(type)
                || type == md::BlockType::CommentLine)
                continue;
            const QString &line = text[i];
            const int contentStart = std::min<int>(blocks[size_t(i)].contentStart, int(line.size()));
            md::InlineResult parsed;
            bool parsedYet = false;
            QList<std::pair<int, int>> taken; // ranges already reported on this line
            for (const QString &name : std::as_const(wanted)) {
                for (qsizetype at = line.indexOf(name, contentStart, Qt::CaseInsensitive); at >= 0;
                     at = line.indexOf(name, at + name.size(), Qt::CaseInsensitive)) {
                    const qsizetype end = at + name.size();
                    if ((at > 0 && line[at - 1].isLetterOrNumber()) || (end < line.size() && line[end].isLetterOrNumber()))
                        continue; // part of a longer word
                    if (std::any_of(taken.cbegin(), taken.cend(), [&](const auto &r) { return at < r.second && end > r.first; }))
                        continue;
                    if (!parsedYet) {
                        parsed = md::parseInline(toView(line).substr(size_t(contentStart)));
                        parsedYet = true;
                    }
                    bool plain = true;
                    for (qsizetype c = at - contentStart; c < end - contentStart && plain; ++c) {
                        if (c >= 0 && size_t(c) < parsed.styles.size()
                            && (parsed.styles[size_t(c)] & (md::Link | md::LinkUrl | md::Code | md::Tag | md::Math | md::Marker)))
                            plain = false;
                    }
                    if (!plain)
                        continue;
                    taken.append({int(at), int(end)});
                    result.unlinked.append({path, i, int(at), int(name.size()), line});
                }
            }
        }
    }
    return result;
}

void BacklinksPanel::show(const Result &result)
{
    m_tree->clear();
    addGroup(tr("Linked mentions"), result.linked, false);
    addGroup(tr("Unlinked mentions"), result.unlinked, true);
    m_tree->show();
    m_empty->hide();
}

void BacklinksPanel::addGroup(const QString &title, const QList<Mention> &mentions, bool linkButtons)
{
    QSet<QString> sources;
    for (const Mention &mention : mentions)
        sources.insert(mention.source);
    auto *group = new QTreeWidgetItem(m_tree, {title, QString::number(sources.size())});
    QFont bold = group->font(0);
    bold.setBold(true);
    group->setFont(0, bold);
    group->setForeground(1, palette().brush(QPalette::PlaceholderText));
    group->setFirstColumnSpanned(false);

    const QDir root(m_index->root());
    QTreeWidgetItem *file = nullptr;
    for (const Mention &mention : mentions) {
        if (!file || file->data(0, pathRole).toString() != mention.source) {
            file = new QTreeWidgetItem(group, {QFileInfo(mention.source).completeBaseName()});
            file->setData(0, pathRole, mention.source);
            file->setData(0, lineRole, mention.line);
            const QString folder = root.relativeFilePath(QFileInfo(mention.source).absolutePath());
            file->setToolTip(0, folder == u"." ? QFileInfo(mention.source).fileName()
                                               : folder + u'/' + QFileInfo(mention.source).fileName());
        }
        auto *line = new QTreeWidgetItem(file, {mention.text.trimmed().isEmpty() ? tr("(line %1)").arg(mention.line + 1)
                                                                                  : mention.text.trimmed()});
        line->setData(0, pathRole, mention.source);
        line->setData(0, lineRole, mention.line);
        line->setData(0, columnRole, mention.column);
        line->setData(0, lengthRole, mention.length);
        line->setToolTip(0, mention.text);
        if (linkButtons) {
            auto *button = new QToolButton(m_tree);
            button->setText(tr("Link"));
            button->setToolTip(tr("Turn this mention into a link to %1").arg(QFileInfo(m_note).completeBaseName()));
            button->setAutoRaise(true);
            const QString target = m_note;
            connect(button, &QToolButton::clicked, this, [this, mention, target] {
                // Queued: the panel is rebuilt (and this button deleted) as a result.
                QMetaObject::invokeMethod(this, [this, mention, target] { emit linkMentionRequested(mention, target); },
                                          Qt::QueuedConnection);
            });
            m_tree->setItemWidget(line, 1, button);
        }
    }
    group->setExpanded(true);
    for (int i = 0; i < group->childCount(); ++i)
        group->child(i)->setExpanded(true);
}

// --- Outgoing links -----------------------------------------------------------------------------

OutgoingLinksPanel::OutgoingLinksPanel(NoteIndex *index, QWidget *parent)
    : QWidget(parent)
    , m_index(index)
    , m_tree(makeTree(this))
    , m_empty(makeEmpty(tr("No links"), this))
    , m_timer(new QTimer(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tree);
    layout->addWidget(m_empty);
    layout->addStretch();
    m_timer->setSingleShot(true);
    m_timer->setInterval(300);
    connect(m_timer, &QTimer::timeout, this, &OutgoingLinksPanel::rebuild);
    connect(m_index, &NoteIndex::changed, m_timer, qOverload<>(&QTimer::start));
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        const QString target = item->data(0, targetRole).toString();
        if (m_editor && !target.isEmpty())
            m_editor->openLink(target);
    });
}

void OutgoingLinksPanel::setEditor(MarkdownEditor *editor)
{
    for (const auto &connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    m_editor = editor;
    if (editor) {
        m_connections << connect(editor->document(), &QTextDocument::contentsChanged, m_timer,
                                 qOverload<>(&QTimer::start));
        m_connections << connect(editor, &MarkdownEditor::markdownModeChanged, this, &OutgoingLinksPanel::rebuild);
    }
    rebuild();
}

void OutgoingLinksPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    rebuild();
}

void OutgoingLinksPanel::rebuild()
{
    m_timer->stop();
    m_tree->clear();
    if (!m_editor || !m_editor->markdownMode() || !isVisible()) {
        m_tree->hide();
        m_empty->show();
        return;
    }
    const QString from = m_editor->documentPath();
    const md::NoteScan scan = md::scanNote(toView(m_editor->markdown()));
    QTreeWidgetItem *existing = nullptr;
    QTreeWidgetItem *missing = nullptr;
    QSet<QString> seen;
    for (const md::NoteLink &link : scan.links) {
        const QString target = QString::fromStdU16String(link.target);
        const QString anchor = QString::fromStdU16String(link.anchor);
        const QString written = anchor.isEmpty() ? target : target + u'#' + anchor;
        if (seen.contains(written))
            continue;
        seen.insert(written);
        const QString path = from.isEmpty() ? QString() : m_index->resolveIndexed(target, link.wiki, from);
        QTreeWidgetItem *&group = path.isEmpty() ? missing : existing;
        if (!group) {
            group = new QTreeWidgetItem(m_tree, {path.isEmpty() ? tr("Not created yet") : tr("Links")});
            QFont bold = group->font(0);
            bold.setBold(true);
            group->setFont(0, bold);
            group->setExpanded(true);
        }
        QString label = path.isEmpty() ? QFileInfo(target).completeBaseName() : QFileInfo(path).completeBaseName();
        if (path.isEmpty() && label.isEmpty())
            label = target;
        if (!anchor.isEmpty())
            label += QStringLiteral(" › ") + anchor;
        auto *item = new QTreeWidgetItem(group, {label});
        item->setData(0, targetRole, written);
        item->setToolTip(0, path.isEmpty() ? written : QDir::toNativeSeparators(path));
        if (path.isEmpty())
            item->setForeground(0, palette().brush(QPalette::PlaceholderText));
    }
    // Existing ones first.
    if (existing && missing)
        m_tree->insertTopLevelItem(0, m_tree->takeTopLevelItem(m_tree->indexOfTopLevelItem(existing)));
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        m_tree->topLevelItem(i)->setText(1, QString::number(m_tree->topLevelItem(i)->childCount()));
    const bool any = m_tree->topLevelItemCount() > 0;
    m_tree->setVisible(any);
    m_empty->setVisible(!any);
}
