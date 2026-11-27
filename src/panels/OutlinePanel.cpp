#include "panels/OutlinePanel.h"

#include "editor/LivePreviewHighlighter.h" // toView
#include "editor/MarkdownEditor.h"
#include "markdown/NoteScan.h"

#include <QHeaderView>
#include <QLabel>
#include <QTextBlock>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
constexpr int lineRole = Qt::UserRole;
}

OutlinePanel::OutlinePanel(QWidget *parent)
    : QWidget(parent)
    , m_tree(new QTreeWidget(this))
    , m_empty(new QLabel(tr("No headings"), this))
    , m_timer(new QTimer(this))
{
    m_tree->setHeaderHidden(true);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setIndentation(14);
    m_tree->setExpandsOnDoubleClick(false);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setForegroundRole(QPalette::PlaceholderText);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tree);
    layout->addWidget(m_empty);
    layout->addStretch();
    m_timer->setSingleShot(true);
    m_timer->setInterval(300);
    connect(m_timer, &QTimer::timeout, this, &OutlinePanel::rebuild);

    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        if (!m_editor)
            return;
        const QTextBlock block = m_editor->document()->findBlockByNumber(item->data(0, lineRole).toInt());
        if (!block.isValid())
            return;
        QTextCursor cursor(block);
        m_editor->setTextCursor(cursor);
        m_editor->centerCursor();
        m_editor->setFocus();
    });
    rebuild();
}

void OutlinePanel::setEditor(MarkdownEditor *editor)
{
    for (const auto &connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    m_editor = editor;
    if (editor) {
        m_connections << connect(editor->document(), &QTextDocument::contentsChanged, m_timer,
                                 qOverload<>(&QTimer::start));
        m_connections << connect(editor, &QPlainTextEdit::cursorPositionChanged, this, &OutlinePanel::markCurrent);
        m_connections << connect(editor, &MarkdownEditor::markdownModeChanged, this, &OutlinePanel::rebuild);
    }
    rebuild();
}

void OutlinePanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    rebuild(); // not kept up to date while hidden
}

void OutlinePanel::rebuild()
{
    m_tree->clear();
    if (m_editor && m_editor->markdownMode() && isVisible()) {
        const QString text = m_editor->markdown();
        const md::NoteScan scan = md::scanNote(toView(text));
        // Each heading goes under the nearest previous one with a lower level.
        QList<std::pair<int, QTreeWidgetItem *>> stack;
        for (const md::Heading &heading : scan.headings) {
            while (!stack.isEmpty() && stack.last().first >= heading.level)
                stack.removeLast();
            const QString title = QString::fromStdU16String(heading.text);
            auto *item = stack.isEmpty() ? new QTreeWidgetItem(m_tree) : new QTreeWidgetItem(stack.last().second);
            item->setText(0, title.isEmpty() ? tr("(untitled)") : title);
            item->setToolTip(0, item->text(0));
            item->setData(0, lineRole, heading.line);
            stack.append({heading.level, item});
        }
        m_tree->expandAll();
    }
    const bool empty = m_tree->topLevelItemCount() == 0;
    m_tree->setVisible(!empty);
    m_empty->setVisible(empty);
    m_empty->setText(m_editor && !m_editor->markdownMode() ? tr("Outlines are for Markdown notes")
                                                            : tr("No headings"));
    markCurrent();
}

// The heading the cursor is under: the last one at or above its line.
void OutlinePanel::markCurrent()
{
    if (!m_editor || !isVisible())
        return;
    const int line = m_editor->textCursor().blockNumber();
    QTreeWidgetItem *current = nullptr;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        if ((*it)->data(0, lineRole).toInt() > line)
            break;
        current = *it;
    }
    const QSignalBlocker blocker(m_tree);
    m_tree->setCurrentItem(current);
    if (current)
        m_tree->scrollToItem(current);
}
