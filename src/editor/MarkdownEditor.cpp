#include "editor/MarkdownEditor.h"

#include "app/AppSettings.h"
#include "editor/CodeHighlighter.h"
#include "spell/SpellChecker.h"
#include "editor/LivePreviewHighlighter.h"
#include "format/CodeFormatters.h"
#include "format/FormatterRegistry.h"
#include "markdown/AutoPair.h"
#include "markdown/InlineParser.h"
#include "markdown/NoteScan.h"
#include "markdown/TableEdit.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontInfo>
#include <QCursor>
#include <QTextBrowser>
#include <QGuiApplication>
#include <QImageReader>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QScopeGuard>
#include <QScrollBar>
#include <QTextBlock>
#include <QAbstractTextDocumentLayout>
#include <QTextDocument>
#include <QTextLayout>
#include <QToolTip>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <limits>
#include <memory>
#include <numeric>

using md::BlockType;

namespace {

QStringList localFiles(const QMimeData *mime)
{
    QStringList files;
    if (!mime->hasUrls())
        return files;
    for (const QUrl &url : mime->urls()) {
        if (!url.isLocalFile())
            return {};
        files << url.toLocalFile();
    }
    return files;
}

bool isCode(BlockType type)
{
    return type == BlockType::FenceOpen || type == BlockType::FenceClose || type == BlockType::CodeLine;
}

bool isImageFile(const QString &path)
{
    static const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    return formats.contains(QFileInfo(path).suffix().toLower().toLatin1());
}

MarkdownEditor::LinkResolver &linkResolver()
{
    static MarkdownEditor::LinkResolver resolver;
    return resolver;
}

constexpr qreal embedMaxHeight = 420;

} // namespace

void MarkdownEditor::setLinkResolver(LinkResolver resolver)
{
    linkResolver() = std::move(resolver);
}

namespace {

MarkdownEditor::CompletionProvider &completionProvider()
{
    static MarkdownEditor::CompletionProvider provider;
    return provider;
}

} // namespace

void MarkdownEditor::setCompletionProvider(CompletionProvider provider)
{
    completionProvider() = std::move(provider);
}

MarkdownEditor::MarkdownEditor(QWidget *parent)
    : QPlainTextEdit(parent)
{
    applyFontSettings();

    setFrameShape(QFrame::NoFrame);
    // Fill the side margins (outside of the viewport) with the text background.
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    viewport()->setMouseTracking(true);
    document()->setDocumentMargin(16);

    m_theme = EditorTheme::fromPalette(palette(), font());
    m_highlighter = new LivePreviewHighlighter(document(), &m_theme);
    m_codeHighlighter = new CodeHighlighter(this, &m_theme);
    m_highlighter->setCaretProvider([this] { return textCursor().position(); });
    m_codeHighlighter->setCaretProvider([this] { return textCursor().position(); });
    // New dictionaries or words: mark the text again.
    connect(&SpellChecker::instance(), &SpellChecker::changed, this, [this] {
        if (m_highlighter->isAttached())
            m_highlighter->rehighlight();
        if (m_codeHighlighter->document())
            m_codeHighlighter->rehighlight();
    });
    m_highlighter->setMathSizeProvider([this](const QString &tex, bool display) {
        const MathRenderer::Formula &rendered = formula(tex, display);
        return MathMetrics{rendered.size(), rendered.baseline};
    });
    m_highlighter->setImageSizeProvider(
        [this](const QString &target, int requestedWidth) { return imageDisplaySize(target, requestedWidth); });

    // Image sizes depend on the text column width: re-layout once resizing settles.
    m_imageRelayout = new QTimer(this);
    m_imageRelayout->setSingleShot(true);
    m_imageRelayout->setInterval(120);
    connect(m_imageRelayout, &QTimer::timeout, m_highlighter, [this] {
        if (m_highlighter->isAttached())
            m_highlighter->rehighlight();
    });

    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &MarkdownEditor::refreshActiveBlock);
    // The list follows the cursor while it's open (Backspace, arrows) and goes once it leaves the link or tag.
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        if (m_completion && m_completion->isVisible())
            updateCompletion(false);
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &MarkdownEditor::hideCompletion);

    // Folds follow edits (a heading typed above a folded one changes its section) once the edit is done.
    m_foldTimer = new QTimer(this);
    m_foldTimer->setSingleShot(true);
    m_foldTimer->setInterval(0);
    connect(m_foldTimer, &QTimer::timeout, this, &MarkdownEditor::applyFolds);
    connect(document(), &QTextDocument::contentsChanged, m_foldTimer, qOverload<>(&QTimer::start));
    // The cursor can't stay in hidden text (search, undo, Ctrl+End): unfold around it.
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        const QTextBlock block = textCursor().block();
        // The visible line above a hidden run is the fold hiding it; nested folds take another round.
        for (int round = 0; !block.isVisible() && round < 64; ++round) {
            QTextBlock header = block.previous();
            while (header.isValid() && !header.isVisible())
                header = header.previous();
            auto *data = header.isValid() ? static_cast<BlockData *>(header.userData()) : nullptr;
            if (!data || !data->folded)
                break;
            data->folded = false;
            applyFolds();
        }
    });

    applyTheme();
    document()->setModified(false); // setDocumentMargin() counts as a modification

    connect(&AppSettings::instance(), &AppSettings::changed, this, [this](const QString &key) {
        if (key == QLatin1String(AppSettings::fontFamilyKey) || key == QLatin1String(AppSettings::fontSizeKey))
            applyFontSettings();
        else if (key == QLatin1String(AppSettings::readableWidthKey) || key == QLatin1String(AppSettings::indentSizeKey))
            applyTheme();
    });
}

bool MarkdownEditor::isMarkdownFile(const QString &path)
{
    if (path.isEmpty())
        return true; // a new note
    static const QStringList suffixes{"md", "markdown", "mdown", "mkd", "mkdn", "mdwn", "mdtxt", "mdtext"};
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}

void MarkdownEditor::setMarkdownMode(bool on)
{
    if (on == m_markdown)
        return;
    m_markdown = on;
    const bool modified = document()->isModified();
    m_highlighter->setAttached(on);
    updateCodeHighlighter();
    document()->setModified(modified);
    applyFontSettings(); // monospace for plain text
    applyTheme();
    refreshActiveBlock();
    emit markdownModeChanged(on);
}

void MarkdownEditor::applyFontSettings()
{
    QFont base = QApplication::font();
    const QString family = AppSettings::instance().fontFamily();
    if (!m_markdown)
        base = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    else if (!family.isEmpty())
        base.setFamily(family);
    base.setFamilies(EditorTheme::withEmoji(base));
    base.setPointSizeF(AppSettings::instance().fontSize());
    setFont(base); // FontChange re-applies the theme
}

QString MarkdownEditor::markdown() const
{
    // toPlainText() would also turn non-breaking spaces into plain spaces.
    QString text = document()->toRawText();
    text.replace(QChar::ParagraphSeparator, u'\n');
    text.replace(QChar::LineSeparator, u'\n');
    return text;
}

void MarkdownEditor::setMarkdown(const QString &text)
{
    setPlainText(text);
    refreshActiveBlock();
}

void MarkdownEditor::replaceAll(const QString &text)
{
    if (text == markdown())
        return;
    const int position = textCursor().position();
    QTextCursor cursor(document());
    cursor.select(QTextCursor::Document);
    cursor.insertText(text);
    cursor.setPosition(std::min(position, document()->characterCount() - 1));
    setTextCursor(cursor);
}

namespace {

// The lines the cursor or the selection touches (a selection ending at the start of a line leaves that one out).
std::pair<QTextBlock, QTextBlock> selectedLines(const QTextCursor &cursor, QTextDocument *document)
{
    const QTextBlock first = document->findBlock(cursor.selectionStart());
    QTextBlock last = document->findBlock(cursor.selectionEnd());
    if (cursor.hasSelection() && last != first && cursor.selectionEnd() == last.position())
        last = last.previous();
    return {first, last};
}

} // namespace

bool MarkdownEditor::moveLines(int direction)
{
    QTextCursor cursor = textCursor();
    auto [first, last] = selectedLines(cursor, document());
    // The neighbour that changes places: one visible line and whatever is folded under it.
    QTextBlock from;
    QTextBlock to;
    if (direction < 0) {
        to = first.previous();
        from = to;
        while (from.isValid() && !from.isVisible())
            from = from.previous();
        if (!from.isValid())
            return false;
    } else {
        from = last.next();
        if (!from.isValid())
            return false;
        to = from;
        while (to.next().isValid() && !to.next().isVisible())
            to = to.next();
    }
    const int anchorOffset = cursor.anchor() - first.position();
    const int positionOffset = cursor.position() - first.position();
    const auto textOf = [](QTextBlock a, const QTextBlock &b) {
        QStringList lines;
        for (; a.isValid(); a = a.next()) {
            lines << a.text();
            if (a == b)
                break;
        }
        return lines;
    };
    const QStringList moving = textOf(first, last);
    const QStringList neighbour = textOf(from, to);
    const QTextBlock start = direction < 0 ? from : first;
    const QTextBlock end = direction < 0 ? last : to;
    const QStringList result = direction < 0 ? moving + neighbour : neighbour + moving;
    const int startPosition = start.position();
    QTextCursor edit(document());
    edit.beginEditBlock();
    edit.setPosition(startPosition);
    edit.setPosition(end.position() + end.length() - 1, QTextCursor::KeepAnchor);
    edit.insertText(result.join(u'\n'));
    edit.endEditBlock();
    // The selection moves with the lines.
    int movedStart = startPosition;
    if (direction > 0) {
        for (const QString &line : neighbour)
            movedStart += int(line.size()) + 1;
    }
    QTextCursor moved(document());
    moved.setPosition(std::min(movedStart + anchorOffset, document()->characterCount() - 1));
    moved.setPosition(std::min(movedStart + positionOffset, document()->characterCount() - 1), QTextCursor::KeepAnchor);
    setTextCursor(moved);
    ensureCursorVisible();
    return true;
}

void MarkdownEditor::deleteLines()
{
    const auto [first, last] = selectedLines(textCursor(), document());
    QTextCursor edit(document());
    edit.beginEditBlock();
    if (last.next().isValid()) { // the lines and their line break
        edit.setPosition(first.position());
        edit.setPosition(last.next().position(), QTextCursor::KeepAnchor);
    } else if (first.previous().isValid()) { // the last lines: the break before them
        edit.setPosition(first.previous().position() + first.previous().length() - 1);
        edit.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
    } else {
        edit.setPosition(first.position());
        edit.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
    }
    edit.removeSelectedText();
    edit.endEditBlock();
    setTextCursor(edit);
}

void MarkdownEditor::transformLines(const std::function<std::u16string(std::u16string_view)> &change)
{
    QTextCursor cursor = textCursor();
    const auto [first, last] = selectedLines(cursor, document());
    const bool single = first == last && !cursor.hasSelection();
    const int column = cursor.positionInBlock();
    const int oldLength = first.length();
    QTextCursor edit(document());
    edit.beginEditBlock();
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        const QString line = block.text();
        const QString changed = QString::fromStdU16String(change(toView(line)));
        if (changed != line) {
            edit.setPosition(block.position());
            edit.setPosition(block.position() + int(line.size()), QTextCursor::KeepAnchor);
            edit.insertText(changed);
        }
        if (block == last)
            break;
    }
    edit.endEditBlock();
    if (single) { // the cursor keeps its place in the text after what was added or removed in front
        QTextCursor at(first);
        at.setPosition(first.position() + std::clamp(column + (first.length() - oldLength), 0, first.length() - 1));
        setTextCursor(at);
    }
}

void MarkdownEditor::applyText(const QString &text)
{
    const QString old = markdown();
    if (text == old)
        return;
    const qsizetype shorter = std::min(old.size(), text.size());
    qsizetype prefix = 0;
    while (prefix < shorter && old[prefix] == text[prefix])
        ++prefix;
    qsizetype suffix = 0;
    while (suffix < shorter - prefix && old[old.size() - 1 - suffix] == text[text.size() - 1 - suffix])
        ++suffix;
    QTextCursor cursor(document());
    cursor.setPosition(int(prefix));
    cursor.setPosition(int(old.size() - suffix), QTextCursor::KeepAnchor);
    cursor.insertText(text.mid(prefix, text.size() - prefix - suffix));
}

void MarkdownEditor::refreshActiveBlock()
{
    if (m_highlighter->setActiveBlock(textCursor().blockNumber()))
        viewport()->update();
}

void MarkdownEditor::toggleWrap(const QString &marker)
{
    const int m = int(marker.size());
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();

    if (!cursor.hasSelection()) {
        const int pos = cursor.position();
        cursor.insertText(marker + marker);
        cursor.setPosition(pos + m);
    } else {
        const int start = cursor.selectionStart();
        const int end = cursor.selectionEnd();
        auto textAt = [this](int from, int length) {
            QTextCursor c(document());
            c.setPosition(from);
            c.setPosition(from + length, QTextCursor::KeepAnchor);
            return c.selectedText();
        };
        QTextCursor edit(document());
        if (start >= m && end + m <= document()->characterCount() - 1 && textAt(start - m, m) == marker
            && textAt(end, m) == marker) {
            edit.setPosition(end);
            edit.setPosition(end + m, QTextCursor::KeepAnchor);
            edit.removeSelectedText();
            edit.setPosition(start - m);
            edit.setPosition(start, QTextCursor::KeepAnchor);
            edit.removeSelectedText();
            cursor.setPosition(start - m);
            cursor.setPosition(end - m, QTextCursor::KeepAnchor);
        } else {
            edit.setPosition(end);
            edit.insertText(marker);
            edit.setPosition(start);
            edit.insertText(marker);
            cursor.setPosition(start + m);
            cursor.setPosition(end + m, QTextCursor::KeepAnchor);
        }
    }

    cursor.endEditBlock();
    setTextCursor(cursor);
}

bool MarkdownEditor::event(QEvent *event)
{
    // Wheel events over the side margins don't reach the viewport; scroll anyway.
    if (event->type() == QEvent::Wheel) {
        wheelEvent(static_cast<QWheelEvent *>(event));
        return true;
    }
    // Leave Ctrl+Tab / Ctrl+Shift+Tab to the window's tab switching shortcuts.
    if (event->type() == QEvent::ShortcutOverride) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if ((key->modifiers() & Qt::ControlModifier) && (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab)) {
            event->ignore();
            return false;
        }
        // On X11 Ctrl+K deletes to the end of the line; in Markdown it's Add External Link (window menu).
        if (m_markdown && key->keyCombination() == QKeyCombination(Qt::CTRL | Qt::Key_K)) {
            event->ignore();
            return false;
        }
    }
    // Footnote references show their text when hovered.
    if (event->type() == QEvent::ToolTip && m_markdown) {
        const auto *help = static_cast<QHelpEvent *>(event);
        const QPoint pos = viewport()->mapFrom(this, help->pos());
        if (const auto footnote = footnoteAt(pos)) {
            QToolTip::showText(help->globalPos(), footnote->first, this);
            return true;
        }
        QToolTip::hideText();
        event->ignore();
        return true;
    }
    return QPlainTextEdit::event(event);
}

void MarkdownEditor::keyPressEvent(QKeyEvent *event)
{
    const auto modifiers = event->modifiers() & ~Qt::KeypadModifier;
    const int key = event->key();

    if (m_completion && m_completion->isVisible() && (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier)) {
        switch (key) {
        case Qt::Key_Down:
            m_completion->move(1);
            return;
        case Qt::Key_Up:
            m_completion->move(-1);
            return;
        case Qt::Key_PageDown:
            m_completion->move(10);
            return;
        case Qt::Key_PageUp:
            m_completion->move(-10);
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Tab:
            acceptCompletion(m_completion->currentRow());
            return;
        case Qt::Key_Escape:
            m_completionDismissed = m_completionStart;
            hideCompletion();
            return;
        default:
            break;
        }
    }
    if (autoPair(event))
        return updateCompletion(true); // "[" makes "[]": the second one may start a [[link]]
    if (key == Qt::Key_Escape && m_preview && m_preview->isVisible()) {
        m_preview->hide();
        return;
    }
    // Pressing Ctrl over a link shows its preview, as moving the mouse with Ctrl held does.
    if (key == Qt::Key_Control && m_markdown && viewport()->underMouse())
        startPreview(viewport()->mapFromGlobal(QCursor::pos()));
    // Typing (or deleting) may start, narrow or end a list of suggestions.
    const bool typed = !event->text().isEmpty() || key == Qt::Key_Backspace || key == Qt::Key_Delete;
    const auto suggest = qScopeGuard([this, typed] {
        if (typed)
            updateCompletion(true);
    });

    if (m_markdown) {
        if ((key == Qt::Key_Return || key == Qt::Key_Enter) && modifiers == Qt::NoModifier
            && (tableEnter() || continueList()))
            return;
        // Obsidian's "Follow link under cursor" (Alt+Enter; Ctrl+Enter too, as a new tab is where links open
        // anyway) and "Open link to the right" (Ctrl+Alt+Enter).
        if ((key == Qt::Key_Return || key == Qt::Key_Enter)
            && (modifiers == Qt::AltModifier || modifiers == Qt::ControlModifier
                || modifiers == (Qt::ControlModifier | Qt::AltModifier))) {
            if (const auto target = linkTargetAt(textCursor())) {
                openLink(*target, modifiers == (Qt::ControlModifier | Qt::AltModifier));
                return;
            }
        }
        if (key == Qt::Key_Tab && modifiers == Qt::NoModifier && (moveToTableCell(false) || indentListItem(false)))
            return;
        if (key == Qt::Key_Backtab && (moveToTableCell(true) || indentListItem(true)))
            return;
    }

    QPlainTextEdit::keyPressEvent(event);
}

bool MarkdownEditor::autoPair(QKeyEvent *event)
{
    if (isReadOnly())
        return false;
    const auto modifiers = event->modifiers() & ~(Qt::KeypadModifier | Qt::ShiftModifier);
    // AltGr (Ctrl+Alt on Windows) types brackets on many keyboards; plain Ctrl or Alt are shortcuts.
    const bool typing = modifiers == Qt::NoModifier || modifiers == (Qt::ControlModifier | Qt::AltModifier)
                        || modifiers == Qt::GroupSwitchModifier;
    md::PairOptions options{AppSettings::instance().autoPairBrackets(), m_markdown && AppSettings::instance().autoPairMarkdown()};
    if (!options.brackets && !options.markdown)
        return false;
    QTextCursor cursor = textCursor();
    const QTextBlock block = cursor.block();
    const QString line = block.text();

    if (event->key() == Qt::Key_Backspace && modifiers == Qt::NoModifier && !cursor.hasSelection()) {
        const int at = cursor.positionInBlock();
        if (!md::deletesPair(toView(line).substr(0, size_t(at)), toView(line).substr(size_t(at)), options))
            return false;
        cursor.beginEditBlock();
        cursor.movePosition(QTextCursor::Left);
        cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
        cursor.removeSelectedText();
        cursor.endEditBlock();
        setTextCursor(cursor);
        return true;
    }
    if (!typing || event->text().size() != 1)
        return false;
    const char16_t typed = event->text().at(0).unicode();
    const bool selection = cursor.hasSelection();
    if (selection && document()->findBlock(cursor.selectionEnd()) != document()->findBlock(cursor.selectionStart()))
        return false; // across lines: typing replaces it as usual
    const int start = cursor.selectionStart() - block.position();
    const int end = cursor.selectionEnd() - block.position();
    const md::PairDecision decision = md::pairFor(typed, toView(line).substr(0, size_t(start)),
                                                  toView(line).substr(size_t(end)), selection, options);
    switch (decision.action) {
    case md::PairAction::None:
        return false;
    case md::PairAction::Skip:
        cursor.movePosition(QTextCursor::Right);
        setTextCursor(cursor);
        return true;
    case md::PairAction::Insert:
        cursor.beginEditBlock();
        cursor.insertText(QString(QChar(decision.open)) + QChar(decision.close));
        cursor.endEditBlock();
        cursor.movePosition(QTextCursor::Left);
        setTextCursor(cursor);
        return true;
    case md::PairAction::Wrap: {
        const int from = cursor.selectionStart();
        const int to = cursor.selectionEnd();
        cursor.beginEditBlock();
        cursor.setPosition(to);
        cursor.insertText(QString(QChar(decision.close)));
        cursor.setPosition(from);
        cursor.insertText(QString(QChar(decision.open)));
        cursor.endEditBlock();
        // The text stays selected, so "*" again makes it bold.
        cursor.setPosition(from + 1);
        cursor.setPosition(to + 1, QTextCursor::KeepAnchor);
        setTextCursor(cursor);
        return true;
    }
    }
    return false;
}

void MarkdownEditor::focusOutEvent(QFocusEvent *event)
{
    hideCompletion();
    if (m_preview)
        m_preview->hide();
    QPlainTextEdit::focusOutEvent(event);
}

void MarkdownEditor::hideCompletion()
{
    if (m_completion)
        m_completion->hide();
}

void MarkdownEditor::updateCompletion(bool mayOpen)
{
    const CompletionProvider &provider = completionProvider();
    const QTextCursor cursor = textCursor();
    const QTextBlock block = cursor.block();
    const BlockType type = blockType(block);
    if (!m_markdown || !provider || isReadOnly() || cursor.hasSelection() || isCode(type)
        || md::isFrontmatterType(type) || md::isMathType(type)) {
        hideCompletion();
        return;
    }
    const md::CompletionContext context = md::completionAt(toView(block.text()), cursor.positionInBlock());
    const int start = block.position() + context.start;
    if (context.kind == md::CompletionKind::None || start == m_completionDismissed) {
        hideCompletion();
        return;
    }
    if (!mayOpen && !(m_completion && m_completion->isVisible()))
        return;
    m_completionDismissed = -1;
    const bool ownText = (context.kind == md::CompletionKind::Heading || context.kind == md::CompletionKind::Block)
                         && context.note.empty();
    const QList<CompletionItem> items = provider(context, m_documentPath, ownText ? document()->toPlainText() : QString());
    if (items.isEmpty()) {
        hideCompletion();
        return;
    }
    if (!m_completion) {
        m_completion = new CompletionPopup(this);
        connect(m_completion, &CompletionPopup::activated, this, &MarkdownEditor::acceptCompletion);
    }
    m_completionContext = context;
    m_completionStart = start;
    m_completion->setItems(items);
    QTextCursor at(document());
    at.setPosition(start);
    const QRect rect = cursorRect(at);
    m_completion->showAt(viewport()->mapToGlobal(rect.bottomLeft() + QPoint(0, 2)),
                         viewport()->mapToGlobal(rect.topLeft()).y() - 2);
}

void MarkdownEditor::acceptCompletion(int row)
{
    if (!m_completion || row < 0 || row >= m_completion->items().size()) {
        hideCompletion();
        return;
    }
    const CompletionItem item = m_completion->items().at(row);
    hideCompletion();
    QTextCursor cursor = textCursor();
    const int end = cursor.position();
    if (m_completionStart < 0 || m_completionStart > end || cursor.block() != document()->findBlock(m_completionStart))
        return;
    cursor.beginEditBlock();
    cursor.setPosition(m_completionStart);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.insertText(item.insert);
    if (m_completionContext.kind != md::CompletionKind::Tag) {
        // Past the closing brackets, adding them if they aren't there yet.
        if (m_completionContext.closed)
            cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 2);
        else
            cursor.insertText(QStringLiteral("]]"));
    }
    cursor.endEditBlock();
    setTextCursor(cursor);
    m_completionStart = -1;
}

void MarkdownEditor::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_markdown) {
        const QPoint pos = event->position().toPoint();
        if (const QTextBlock block = foldableBlockAt(pos); block.isValid()) {
            toggleFold(block);
            return;
        }
        if (event->modifiers() & Qt::ControlModifier) {
            if (const auto footnote = footnoteAt(pos); footnote && footnote->second.isValid()) {
                setTextCursor(QTextCursor(footnote->second));
                centerCursor();
                return;
            }
            if (const auto target = linkTargetAt(pos)) {
                openLink(*target, event->modifiers().testFlag(Qt::AltModifier)); // Ctrl+Alt+click: to the right
                return;
            }
        } else if (toggleTaskAt(pos) || focusTableCellAt(pos)) {
            return;
        }
    }
    QPlainTextEdit::mousePressEvent(event);
}

QKeySequence MarkdownEditor::prettifyShortcut()
{
    return QKeySequence(Qt::SHIFT | Qt::ALT | Qt::Key_F);
}

QKeySequence MarkdownEditor::prettifyDocumentShortcut()
{
    return QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::ALT | Qt::Key_F);
}

void MarkdownEditor::contextMenuEvent(QContextMenuEvent *event)
{
    const QPoint pos = event->pos();
    // The menu acts on the cursor, so a right click must put it on what was clicked. A click on the
    // selection keeps it, and the menu key (no position of its own) leaves the cursor alone.
    if (event->reason() == QContextMenuEvent::Mouse) {
        const QTextCursor current = textCursor();
        const int hit = cursorForPosition(pos).position();
        const bool onSelection = current.hasSelection() && hit >= current.selectionStart() && hit < current.selectionEnd();
        if (!onSelection && !(m_markdown && focusTableCellAt(pos)))
            setTextCursor(cursorForPosition(pos));
    }
    const std::unique_ptr<QMenu> menu = createContextMenu(event->reason() == QContextMenuEvent::Mouse
                                                              ? pos
                                                              : cursorRect().center());
    menu->exec(event->globalPos());
}

void MarkdownEditor::mouseMoveEvent(QMouseEvent *event)
{
    QPlainTextEdit::mouseMoveEvent(event);
    if (m_markdown) {
        const int hover = cursorForPosition(event->position().toPoint()).blockNumber();
        if (hover != m_hoverBlock) {
            m_hoverBlock = hover;
            viewport()->update();
        }
        if (foldableBlockAt(event->position().toPoint()).isValid()) {
            viewport()->setCursor(Qt::PointingHandCursor);
            return;
        }
    }
    const bool overLink = m_markdown && (event->modifiers() & Qt::ControlModifier)
        && linkTargetAt(event->position().toPoint()).has_value();
    viewport()->setCursor(overLink ? Qt::PointingHandCursor : Qt::IBeamCursor);
    if (overLink)
        startPreview(event->position().toPoint());
    else if (m_previewTimer)
        m_previewTimer->stop();
}

void MarkdownEditor::startPreview(const QPoint &viewportPos)
{
    const auto target = linkTargetAt(viewportPos);
    if (!target)
        return;
    if (m_preview && m_preview->isVisible() && *target == m_previewTarget)
        return; // already shown
    m_previewTarget = *target;
    m_previewPos = viewportPos;
    if (!m_previewTimer) {
        m_previewTimer = new QTimer(this);
        m_previewTimer->setSingleShot(true);
        m_previewTimer->setInterval(350);
        connect(m_previewTimer, &QTimer::timeout, this, [this] {
            if (!(QGuiApplication::queryKeyboardModifiers() & Qt::ControlModifier))
                return;
            const QTextCursor at = cursorForPosition(m_previewPos);
            QRect line = cursorRect(at);
            line.setLeft(0);
            line.setRight(viewport()->width());
            previewLink(m_previewTarget, viewport()->mapToGlobal(QPoint(m_previewPos.x(), line.bottom() + 6)),
                        QRect(viewport()->mapToGlobal(line.topLeft()), line.size()));
        });
    }
    m_previewTimer->start();
}

bool MarkdownEditor::previewLink(const QString &target, const QPoint &globalPos, const QRect &lineRect)
{
    const QString path = linkFile(target);
    const QFileInfo info(path);
    if (path.isEmpty() || !info.isFile() || info.size() > 2 * 1024 * 1024
        || !(isMarkdownFile(path) || info.suffix().compare(u"txt", Qt::CaseInsensitive) == 0))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QString text = QString::fromUtf8(file.readAll());
    text.remove(u'\r');
    const QString anchor = target.section(u'#', 1);
    if (!m_preview)
        m_preview = new NotePreview(this);
    m_preview->browser()->document()->setDefaultFont(font()); // the note's own size, not the UI's
    const QString title = anchor.isEmpty() ? info.completeBaseName() : info.completeBaseName() + QStringLiteral(" › ") + anchor;
    m_preview->showNote(title, isMarkdownFile(path) ? excerpt(text, anchor) : QStringLiteral("```\n") + text + QStringLiteral("\n```"),
                        info.absolutePath(), globalPos, lineRect);
    return true;
}

QString MarkdownEditor::excerpt(const QString &text, const QString &anchor)
{
    const md::NoteScan scan = md::scanNote(toView(text));
    QStringList lines = text.split(u'\n');
    int first = scan.frontmatterEnd + 1;
    int last = int(lines.size()) - 1;
    const QString wanted = QUrl::fromPercentEncoding(anchor.toUtf8()).trimmed();
    if (wanted.startsWith(u'^')) { // a block: that line
        for (const md::BlockId &block : scan.blocks) {
            if (QString::fromStdU16String(block.id) == wanted.mid(1)) {
                first = last = block.line;
                break;
            }
        }
    } else if (!wanted.isEmpty()) { // a heading: its section
        const QString heading = wanted.section(u'#', -1).trimmed();
        for (size_t i = 0; i < scan.headings.size(); ++i) {
            const md::Heading &h = scan.headings[i];
            if (QString::fromStdU16String(h.text).compare(heading, Qt::CaseInsensitive) != 0)
                continue;
            first = h.line;
            for (size_t j = i + 1; j < scan.headings.size(); ++j) {
                if (scan.headings[j].level <= h.level) {
                    last = scan.headings[j].line - 1;
                    break;
                }
            }
            break;
        }
    }
    QString content = lines.mid(first, last - first + 1).join(u'\n');
    // [[target#heading|alias]] -> alias, or "target › heading"; ![[pic.png]] -> an image.
    static const QRegularExpression wiki(QStringLiteral(R"((!?)\[\[([^\]|#]*)(?:#([^\]|]*))?(?:\|([^\]]*))?\]\])"));
    QString result;
    qsizetype done = 0;
    for (auto it = wiki.globalMatch(content); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        result += QStringView(content).mid(done, match.capturedStart() - done);
        const QString target = match.captured(2).trimmed();
        const QString section = match.captured(3).trimmed();
        if (!match.captured(1).isEmpty() && !isImageFile(target)) {
            result += QFileInfo(target).completeBaseName(); // an embedded note: its name
        } else if (!match.captured(1).isEmpty()) {
            result += QStringLiteral("![](%1)").arg(QString::fromUtf8(QUrl::toPercentEncoding(target, "/")));
        } else if (!match.captured(4).isEmpty()) {
            result += match.captured(4);
        } else {
            QString name = QFileInfo(target).completeBaseName();
            if (name.isEmpty())
                name = target;
            result += section.isEmpty() ? name : (name.isEmpty() ? section : name + QStringLiteral(" › ") + section);
        }
        done = match.capturedEnd();
    }
    result += QStringView(content).mid(done);
    return result;
}

void MarkdownEditor::paintEvent(QPaintEvent *event)
{
    if (!m_markdown) {
        QPlainTextEdit::paintEvent(event);
        return;
    }
    const QRect area = event->rect();
    const QPointF offset = contentOffset();
    const qreal margin = document()->documentMargin();
    const int activeBlock = textCursor().blockNumber();

    // Backgrounds go below the text.
    {
        QPainter painter(viewport());
        for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next()) {
            const QRectF r = blockBoundingGeometry(block).translated(offset);
            if (r.top() > area.bottom())
                break;
            const auto *data = static_cast<BlockData *>(block.userData());
            if (data && block.isVisible() && (isCode(data->info.type) || md::isFrontmatterType(data->info.type))) {
                painter.fillRect(QRectF(offset.x() + margin - 6, r.top(), viewport()->width() - 2 * margin + 12,
                                        r.height()),
                                 m_theme.codeBackground);
            }
        }
        paintBoxes(painter, area);
    }

    QPlainTextEdit::paintEvent(event);

    // Decorations replacing hidden markers go on top.
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);
    const QTextBlock cursorBlock = textCursor().block();
    const int cursorTable = isTableBlock(cursorBlock) ? tableHeader(cursorBlock).blockNumber() : -1;
    int skipUntil = -1; // last block of a table that has already been handled
    for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next()) {
        const QRectF r = blockBoundingGeometry(block).translated(offset);
        if (r.top() > area.bottom())
            break;
        const auto *data = static_cast<BlockData *>(block.userData());
        if (!data || !block.isVisible() || block.blockNumber() <= skipUntil)
            continue;
        if (md::isTableType(data->info.type)) {
            const QTextBlock header = tableHeader(block);
            if (header.blockNumber() != cursorTable)
                paintTable(painter, tableGeometry(header));
            skipUntil = tableEnd(block).blockNumber();
            continue;
        }
        if (block.blockNumber() == activeBlock) {
            paintFolds(painter, block, r);
            continue;
        }
        const md::BlockInfo &info = data->info;
        paintFolds(painter, block, r);
        paintEntities(painter, block, *data);
        paintMath(painter, block, *data);

        switch (info.type) {
        case BlockType::Quote: {
            if (calloutStart(block).isValid())
                break; // the callout's box has its own bar
            const QRectF c = characterRect(block, info.markerStart);
            painter.fillRect(QRectF(c.left(), r.top(), 3, r.height()), m_theme.quoteBar);
            break;
        }
        case BlockType::BulletItem: {
            const QRectF c = characterRect(block, info.markerStart);
            const qreal radius = std::max<qreal>(2.0, c.height() * 0.11);
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_theme.faint);
            painter.drawEllipse(c.center(), radius, radius);
            break;
        }
        case BlockType::TaskItem: {
            const QRectF first = characterRect(block, info.checkboxStart);
            const QRectF last = characterRect(block, info.checkboxStart + 2);
            const qreal side = std::min(first.height() * 0.7, last.right() - first.left());
            QRectF box(0, 0, side, side);
            box.moveCenter(QPointF((first.left() + last.right()) / 2, first.center().y()));

            const QColor color = info.checked ? m_theme.accent : m_theme.faint;
            painter.setPen(QPen(color, 1.5));
            painter.setBrush(info.checked ? QBrush(color) : QBrush(Qt::NoBrush));
            painter.drawRoundedRect(box, 3, 3);
            if (info.checked) {
                QPainterPath tick;
                tick.moveTo(box.left() + side * 0.25, box.top() + side * 0.52);
                tick.lineTo(box.left() + side * 0.43, box.top() + side * 0.70);
                tick.lineTo(box.left() + side * 0.76, box.top() + side * 0.32);
                painter.setPen(QPen(palette().color(QPalette::Base), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(tick);
            }
            break;
        }
        case BlockType::Paragraph:
            if (data->imageShown)
                paintImage(painter, block, data->image, data->imageWidth);
            break;
        case BlockType::HorizontalRule: {
            const qreal y = r.center().y();
            painter.setPen(QPen(m_theme.rule, 1));
            painter.drawLine(QPointF(offset.x() + margin, y), QPointF(offset.x() + viewport()->width() - margin, y));
            break;
        }
        default:
            break;
        }
    }
}

void MarkdownEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    updateMargins();
    if (m_hasImages && m_imageRelayout && viewport()->width() != m_layoutWidth) {
        m_layoutWidth = viewport()->width();
        m_imageRelayout->start();
    }
}

void MarkdownEditor::setDocumentPath(const QString &filePath)
{
    m_documentPath = filePath;
    setBaseDirectory(QFileInfo(filePath).absolutePath());
    updateCodeHighlighter(); // a rename may change the language
}

void MarkdownEditor::updateCodeHighlighter()
{
    if (!m_codeHighlighter)
        return;
    const int language = syntax::languageForFileName(toView(QFileInfo(m_documentPath).fileName()));
    const bool modified = document()->isModified();
    // Plain-text mode: code gets syntax colors, prose (language 0) spell checking.
    m_codeHighlighter->setLanguage(m_markdown ? nullptr : document(), language);
    document()->setModified(modified);
}

void MarkdownEditor::setBaseDirectory(const QString &directory)
{
    if (directory == m_baseDirectory)
        return;
    m_baseDirectory = directory;
    if (!document()->isEmpty() && m_highlighter->isAttached()) // relative image paths now resolve differently
        m_highlighter->rehighlight();
}

void MarkdownEditor::changeEvent(QEvent *event)
{
    QPlainTextEdit::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::FontChange)
        applyTheme();
}

void MarkdownEditor::dragEnterEvent(QDragEnterEvent *event)
{
    if (!localFiles(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    QPlainTextEdit::dragEnterEvent(event);
}

void MarkdownEditor::dragMoveEvent(QDragMoveEvent *event)
{
    if (!localFiles(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    QPlainTextEdit::dragMoveEvent(event);
}

// Dropped images are linked into the text; other files are opened.
void MarkdownEditor::dropEvent(QDropEvent *event)
{
    const QStringList files = localFiles(event->mimeData());
    if (!files.isEmpty()) {
        event->acceptProposedAction();
        if (!m_markdown) {
            emit filesDropped(files);
            return;
        }
        QStringList images;
        QStringList documents;
        for (const QString &file : files)
            (isImageFile(file) ? images : documents) << file;
        if (!images.isEmpty())
            insertImageLinks(images, cursorForPosition(event->position().toPoint()));
        if (!documents.isEmpty())
            emit filesDropped(documents);
        return;
    }
    QPlainTextEdit::dropEvent(event);
}

bool MarkdownEditor::canInsertFromMimeData(const QMimeData *source) const
{
    return (m_markdown && source->hasImage()) || QPlainTextEdit::canInsertFromMimeData(source);
}

// Pasting copied image files links them; pasting image data (a screenshot, "Copy image")
// stores it next to the note. Text wins when the clipboard has both (e.g. copied from Word).
void MarkdownEditor::insertFromMimeData(const QMimeData *source)
{
    if (!m_markdown) {
        QPlainTextEdit::insertFromMimeData(source);
        return;
    }
    const QStringList files = localFiles(source);
    if (!files.isEmpty() && std::all_of(files.begin(), files.end(), isImageFile)) {
        insertImageLinks(files, textCursor());
        return;
    }
    if (source->hasImage() && !source->hasText()) {
        pasteImage(qvariant_cast<QImage>(source->imageData()));
        return;
    }
    QPlainTextEdit::insertFromMimeData(source);
}

void MarkdownEditor::pasteImage(const QImage &image)
{
    if (image.isNull())
        return;
    const QString attachmentsFolder = AppSettings::instance().attachmentsFolder();
    if (m_documentPath.isEmpty()) {
        QMessageBox::information(this, tr("Paste Image"),
                                 tr("Save the note first: pasted images are stored in an \"%1\" folder next to it.")
                                     .arg(attachmentsFolder));
        return;
    }

    QDir directory(QFileInfo(m_documentPath).absolutePath());
    if (!directory.mkpath(attachmentsFolder) || !directory.cd(attachmentsFolder)) {
        QMessageBox::warning(this, tr("Paste Image"),
                             tr("Cannot create the folder %1.")
                                 .arg(QDir::toNativeSeparators(directory.filePath(attachmentsFolder))));
        return;
    }

    const QString stem = QStringLiteral("pasted-image-%1")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    QString name = stem + QStringLiteral(".png");
    for (int n = 1; directory.exists(name); ++n)
        name = QStringLiteral("%1-%2.png").arg(stem).arg(n);

    const QString path = directory.filePath(name);
    if (!image.save(path, "PNG")) {
        QMessageBox::warning(this, tr("Paste Image"),
                             tr("Cannot save the image to %1.").arg(QDir::toNativeSeparators(path)));
        return;
    }
    insertImageLinks({path}, textCursor());
}

// Inserts one image link per line after the cursor's line.
void MarkdownEditor::insertImageLinks(const QStringList &files, QTextCursor cursor)
{
    cursor.clearSelection();
    cursor.beginEditBlock();
    cursor.movePosition(QTextCursor::EndOfBlock);
    for (const QString &file : files) {
        QString path = m_baseDirectory.isEmpty() ? file : QDir(m_baseDirectory).relativeFilePath(file);
        path.replace(u' ', QStringLiteral("%20"));
        if (!cursor.block().text().trimmed().isEmpty())
            cursor.insertBlock();
        cursor.insertText(QStringLiteral("![](%1)").arg(path));
    }
    cursor.insertBlock(); // move on, so the image renders right away
    cursor.endEditBlock();
    setTextCursor(cursor);
    setFocus();
}

QString MarkdownEditor::resolvePath(const QString &target) const
{
    const QString path = QUrl::fromPercentEncoding(target.section(u'#', 0, 0).toUtf8());
    if (path.isEmpty())
        return {};
    const QDir base(m_baseDirectory.isEmpty() ? QDir::currentPath() : m_baseDirectory);
    return QFileInfo(base.filePath(path)).absoluteFilePath();
}

// Loads (and caches) a local image; reloads it when the file changes. Remote images aren't fetched.
MarkdownEditor::CachedImage *MarkdownEditor::cachedImage(const QString &target) const
{
    if (QUrl(target).scheme().size() > 1)
        return nullptr;
    const QString path = resolvePath(target);
    const QFileInfo info(path);
    if (path.isEmpty() || !info.isFile()) {
        m_images.remove(path);
        return nullptr;
    }

    auto it = m_images.find(path);
    if (it == m_images.end() || it->modified != info.lastModified()) {
        QImageReader reader(path);
        reader.setAutoTransform(true); // honor EXIF rotation
        QImage image = reader.read();
        if (image.isNull()) {
            m_images.remove(path);
            return nullptr;
        }
        if (image.width() > 2400) // nobody needs full-resolution photos in a text column
            image = image.scaledToWidth(2400, Qt::SmoothTransformation);
        it = m_images.insert(path, {image, info.lastModified(), {}});
    }
    m_hasImages = true;
    return &it.value();
}

QSizeF MarkdownEditor::imageDisplaySize(const QString &target, int requestedWidth) const
{
    const CachedImage *cached = cachedImage(target);
    if (!cached) {
        if (const Embed *note = embed(target))
            return {note->width, note->height};
        return {};
    }
    const QSizeF natural = cached->image.size();
    const qreal available = viewport()->width() - 2 * document()->documentMargin();
    const qreal width = std::min<qreal>(requestedWidth > 0 ? requestedWidth : natural.width(), available);
    if (width <= 0 || natural.width() <= 0)
        return {};
    return {width, width * natural.height() / natural.width()};
}

void MarkdownEditor::paintImage(QPainter &painter, const QTextBlock &block, const QString &target,
                                int requestedWidth) const
{
    CachedImage *cached = cachedImage(target);
    if (!cached) {
        if (const Embed *note = embed(target))
            paintEmbed(painter, block, *note);
        return;
    }
    const QSizeF size = imageDisplaySize(target, requestedWidth);
    if (size.isEmpty())
        return;

    const QRectF r = blockBoundingGeometry(block).translated(contentOffset());
    const QRectF area(contentOffset().x() + document()->documentMargin(), r.top() + imageVerticalPadding,
                      size.width(), size.height());
    const qreal dpr = viewport()->devicePixelRatioF();
    const QSize pixels = (size * dpr).toSize();
    if (cached->scaled.size() != pixels) {
        cached->scaled = QPixmap::fromImage(
            cached->image.scaled(pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        cached->scaled.setDevicePixelRatio(dpr);
    }

    painter.save();
    QPainterPath clip;
    clip.addRoundedRect(area, 4, 4);
    painter.setClipPath(clip);
    painter.drawPixmap(area.topLeft(), cached->scaled);
    painter.restore();
}

void MarkdownEditor::applyTheme()
{
    if (m_applyingTheme || !m_highlighter) // setFont() in the constructor gets here first
        return;
    const QScopedValueRollback guard(m_applyingTheme, true);

    m_theme = EditorTheme::fromPalette(palette(), font());
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * AppSettings::instance().indentSize());
    if (m_highlighter->isAttached())
        m_highlighter->rehighlight();
    if (m_codeHighlighter && m_codeHighlighter->document())
        m_codeHighlighter->rehighlight();
    updateMargins();
    viewport()->update();
}

// Keeps lines at a comfortable reading width by centering the text column.
void MarkdownEditor::updateMargins()
{
    // Code and other plain text use the whole width: long lines are common and meaningful there.
    const int characters = m_markdown ? AppSettings::instance().readableWidth() : 0;
    const int side = characters > 0 ? std::max(0, (width() - fontMetrics().averageCharWidth() * characters) / 2) : 0;
    if (viewportMargins().left() != side)
        setViewportMargins(side, 0, side, 0);
}

md::BlockInfo MarkdownEditor::blockInfo(const QTextBlock &block) const
{
    const auto state = md::BlockState::decode(block.previous().isValid() ? block.previous().userState() : -1);
    const QString nextText = block.next().isValid() ? block.next().text() : QString();
    const auto nextLine = block.next().isValid() ? std::optional(toView(nextText)) : std::nullopt;
    return md::parseLine(toView(block.text()), state, nextLine, block.blockNumber() == 0).info;
}

void MarkdownEditor::paintEntities(QPainter &painter, const QTextBlock &block, const BlockData &data) const
{
    const QTextLayout *layout = block.layout();
    if (data.entities.empty() || !layout)
        return;
    const QPointF origin = blockBoundingGeometry(block).translated(contentOffset()).topLeft() + layout->position();
    painter.save();
    for (const EntityGlyph &glyph : data.entities) {
        const QTextLine line = layout->lineForTextPosition(glyph.position);
        if (!line.isValid())
            continue;
        painter.setFont(glyph.font);
        painter.setPen(glyph.color.isValid() ? glyph.color : m_theme.text);
        painter.drawText(QPointF(origin.x() + line.cursorToX(glyph.position), origin.y() + line.y() + line.ascent()),
                         glyph.text);
    }
    painter.restore();
}

const MathRenderer::Formula &MarkdownEditor::formula(const QString &tex, bool display) const
{
    // A little larger than the text, as TeX's Computer Modern looks small next to UI fonts.
    const qreal size = QFontInfo(font()).pixelSize() * 1.15;
    return MathRenderer::instance().render(tex, size, m_theme.text, display, devicePixelRatioF());
}

void MarkdownEditor::paintMath(QPainter &painter, const QTextBlock &block, const BlockData &data) const
{
    const QTextLayout *layout = block.layout();
    if (!layout || (data.math.empty() && !data.mathShown))
        return;
    const QRectF rect = blockBoundingGeometry(block).translated(contentOffset());
    const QPointF origin = rect.topLeft() + layout->position();

    for (const MathGlyph &glyph : data.math) {
        const QTextLine line = layout->lineForTextPosition(glyph.position);
        if (!line.isValid())
            continue;
        const MathRenderer::Formula &rendered = formula(glyph.tex, glyph.display);
        const QPointF baseline(origin.x() + line.cursorToX(glyph.position), origin.y() + line.y() + line.ascent());
        painter.drawPixmap(QPointF(baseline.x(), baseline.y() - rendered.baseline), rendered.pixmap);
    }

    if (data.mathShown) { // a display block, centered in the text column
        const MathRenderer::Formula &rendered = formula(data.mathBlock, true);
        const qreal margin = document()->documentMargin();
        const qreal column = viewport()->width() - 2 * margin;
        const QSizeF size = rendered.size();
        const qreal x = contentOffset().x() + margin + std::max<qreal>(0, (column - size.width()) / 2);
        const QTextLine first = layout->lineAt(0);
        const qreal height = first.isValid() ? first.height() : rect.height();
        painter.drawPixmap(QPointF(x, rect.top() + (height - size.height()) / 2), rendered.pixmap);
    }
}

QRectF MarkdownEditor::characterRect(const QTextBlock &block, int position) const
{
    const QTextLayout *layout = block.layout();
    if (!layout)
        return {};
    const QTextLine line = layout->lineForTextPosition(position);
    if (!line.isValid())
        return {};
    const QPointF origin = blockBoundingGeometry(block).translated(contentOffset()).topLeft() + layout->position();
    const qreal x0 = line.cursorToX(position);
    const qreal x1 = line.cursorToX(position + 1);
    return QRectF(origin.x() + x0, origin.y() + line.y(), x1 - x0, line.height());
}

// Enter on a list item or quote starts the next item; Enter on an empty item ends the list.
bool MarkdownEditor::continueList()
{
    QTextCursor cursor = textCursor();
    if (cursor.hasSelection())
        return false;

    const QTextBlock block = cursor.block();
    const md::BlockInfo info = blockInfo(block);
    if (!info.isList() && info.type != BlockType::Quote)
        return false;
    if (cursor.positionInBlock() < info.contentStart)
        return false;

    const QString text = block.text();
    cursor.beginEditBlock();
    if (text.mid(info.contentStart).trimmed().isEmpty()) {
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    } else {
        QString prefix = text.left(info.indent);
        switch (info.type) {
        case BlockType::BulletItem:
            prefix += QChar(info.listMarker) + QStringLiteral(" ");
            break;
        case BlockType::TaskItem:
            prefix += QChar(info.listMarker) + QStringLiteral(" [ ] ");
            break;
        case BlockType::OrderedItem:
            prefix += QString::number(info.orderedNumber + 1) + QChar(info.listMarker) + QStringLiteral(" ");
            break;
        default:
            prefix = text.left(info.contentStart);
            break;
        }
        cursor.insertBlock();
        cursor.insertText(prefix);
    }
    cursor.endEditBlock();
    setTextCursor(cursor);
    ensureCursorVisible();
    return true;
}

bool MarkdownEditor::indentListItem(bool outdent)
{
    const QTextCursor cursor = textCursor();
    if (document()->findBlock(cursor.selectionStart()) != document()->findBlock(cursor.selectionEnd()))
        return false;

    const QTextBlock block = cursor.block();
    if (!blockInfo(block).isList())
        return false;

    QTextCursor edit(block);
    if (!outdent) {
        edit.insertText(QString(AppSettings::instance().indentSize(), u' '));
        return true;
    }

    const QString text = block.text();
    int remove = 0;
    if (text.startsWith(u'\t'))
        remove = 1;
    else
        while (remove < 4 && remove < text.size() && text[remove] == u' ')
            ++remove;
    if (remove > 0) {
        edit.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, remove);
        edit.removeSelectedText();
    }
    return true;
}

std::optional<QString> MarkdownEditor::linkTargetAt(const QPoint &pos) const
{
    return linkTargetAt(cursorForPosition(pos));
}

std::optional<QString> MarkdownEditor::linkTargetAt(const QTextCursor &hit) const
{
    const QTextBlock block = hit.block();
    const md::BlockInfo info = blockInfo(block);
    if (isCode(info.type) || info.type == BlockType::Blank || info.type == BlockType::HorizontalRule)
        return std::nullopt;

    const int local = hit.positionInBlock() - info.contentStart;
    if (local < 0)
        return std::nullopt;

    const QString content = block.text().mid(info.contentStart);
    const md::InlineResult parsed = md::parseInline(toView(content));
    const md::LinkRange *link = md::linkAt(parsed, local);
    if (!link && local > 0)
        link = md::linkAt(parsed, local - 1);
    if (!link)
        return std::nullopt;
    return content.mid(link->urlStart, link->urlEnd - link->urlStart);
}

QString MarkdownEditor::linkFile(const QString &target) const
{
    const QUrl url(target);
    if (url.isValid() && url.scheme().size() > 1) // one-letter schemes are Windows drive letters
        return {};
    const QString base = target.section(u'#', 0, 0);
    if (base.trimmed().isEmpty())
        return {};
    QString path = resolveLink(base, true);
    if (path.isEmpty())
        path = resolvePath(base);
    if (path.isEmpty())
        return {};
    QFileInfo file(path);
    if (!file.exists() && file.suffix().isEmpty())
        file.setFile(file.filePath() + QStringLiteral(".md"));
    return QDir::cleanPath(file.absoluteFilePath());
}

void MarkdownEditor::openLink(const QString &target, bool toTheRight)
{
    const QUrl url(target);
    if (url.isValid() && url.scheme().size() > 1) {
        QDesktopServices::openUrl(url);
        return;
    }
    const QString anchor = target.section(u'#', 1);
    if (target.section(u'#', 0, 0).trimmed().isEmpty()) {
        if (!anchor.isEmpty())
            emit linkRequested(QString(), anchor, toTheRight); // [[#Heading]]: this note
        return;
    }
    const QString path = linkFile(target);
    if (path.isEmpty())
        return;

    // Notes and other text files open in a tab (a missing note is created, as in Obsidian); images, PDFs
    // and the like in their own application.
    const QFileInfo file(path);
    const QString suffix = file.suffix().toLower();
    const bool text = isMarkdownFile(path) || suffix == u"txt"
                      || (!isImageFile(path) && !FormatterRegistry::instance().languageForSuffix(suffix).isEmpty());
    if (!file.exists() && !isMarkdownFile(path)) {
        emit statusMessage(tr("%1 doesn't exist").arg(QDir::toNativeSeparators(path)));
        return;
    }
    if (text)
        emit linkRequested(path, anchor, toTheRight);
    else
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

// Clicking a rendered checkbox (outside of the line being edited) toggles it.
bool MarkdownEditor::toggleTaskAt(const QPoint &pos)
{
    const QTextBlock block = cursorForPosition(pos).block();
    if (block.blockNumber() == textCursor().blockNumber())
        return false;

    const md::BlockInfo info = blockInfo(block);
    if (info.type != BlockType::TaskItem)
        return false;

    const QRectF box = characterRect(block, info.checkboxStart).united(characterRect(block, info.checkboxStart + 2));
    if (!box.adjusted(-2, -2, 2, 2).contains(pos))
        return false;

    QTextCursor edit(document());
    edit.setPosition(block.position() + info.checkboxStart + 1);
    edit.setPosition(block.position() + info.checkboxStart + 2, QTextCursor::KeepAnchor);
    edit.insertText(info.checked ? QStringLiteral(" ") : QStringLiteral("x"));
    return true;
}

namespace {

constexpr qreal tableCellPadding = 10;
constexpr qreal tableCornerRadius = 6;

QString emptyTableRow(int columns)
{
    QString row = QStringLiteral("|");
    for (int c = 0; c < columns; ++c)
        row += QStringLiteral("     |");
    return row;
}

} // namespace

void MarkdownEditor::insertTable(int rows, int columns)
{
    rows = std::max(rows, 1);
    columns = std::max(columns, 1);

    QString delimiter = QStringLiteral("|");
    for (int c = 0; c < columns; ++c)
        delimiter += QStringLiteral(" --- |");
    QStringList lines{emptyTableRow(columns), delimiter};
    for (int r = 1; r < rows; ++r)
        lines << emptyTableRow(columns);

    insertBlockText(lines.join(u'\n'), 2); // first header cell
    setFocus();
}

// Inserts a block of Markdown as its own paragraph: on the current line if it's blank, otherwise
// below it, separated from the text around it by blank lines. `caret` is where the cursor goes.
void MarkdownEditor::insertBlockText(const QString &text, int caret)
{
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    cursor.clearSelection();
    if (!cursor.block().text().trimmed().isEmpty()) {
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.insertBlock();
        cursor.insertBlock();
    } else {
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        if (cursor.block().previous().isValid() && !cursor.block().previous().text().trimmed().isEmpty())
            cursor.insertBlock();
    }
    const int start = cursor.position();
    cursor.insertText(text);
    const QTextBlock next = cursor.block().next();
    if (next.isValid() && !next.text().trimmed().isEmpty())
        cursor.insertBlock();
    cursor.endEditBlock();

    cursor.setPosition(start + caret);
    setTextCursor(cursor);
    ensureCursorVisible();
}

MarkdownEditor::TableGeometry MarkdownEditor::tableGeometry(const QTextBlock &header) const
{
    TableGeometry table;
    const QTextBlock delimiter = header.next();
    if (blockType(delimiter) != BlockType::TableDelimiter)
        return table;
    table.aligns = md::parseDelimiterRow(toView(delimiter.text())).value_or(std::vector<md::Align>{});
    const int columns = int(table.aligns.size());
    if (columns == 0)
        return table;

    table.rows.push_back(header);
    for (QTextBlock b = delimiter.next(); b.isValid() && blockType(b) == BlockType::TableRow; b = b.next())
        table.rows.push_back(b);

    std::vector<qreal> widths(columns, 3 * tableCellPadding);
    for (size_t r = 0; r < table.rows.size(); ++r) {
        const QString text = table.rows[r].text();
        const md::TableRow row = md::splitTableRow(toView(text));
        for (int c = 0; c < columns && c < int(row.cells.size()); ++c) {
            const md::TableCell &cell = row.cells[c];
            qreal width = 0;
            for (const TextRun &run : cellRuns(text.mid(cell.start, cell.end - cell.start), r == 0))
                width += QFontMetricsF(run.font).horizontalAdvance(run.text);
            widths[c] = std::max(widths[c], width + 2 * tableCellPadding);
        }
    }

    // Shrink proportionally when the table is wider than the text column.
    const qreal margin = document()->documentMargin();
    const qreal available = viewport()->width() - 2 * margin;
    const qreal total = std::accumulate(widths.begin(), widths.end(), qreal(0));
    if (total > available && available > 0) {
        for (qreal &width : widths)
            width *= available / total;
    }

    table.edges.push_back(contentOffset().x() + margin);
    for (const qreal width : widths)
        table.edges.push_back(table.edges.back() + width);
    return table;
}

std::vector<MarkdownEditor::TextRun> MarkdownEditor::cellRuns(const QString &text, bool header) const
{
    std::vector<TextRun> runs;
    const md::InlineResult parsed = md::parseInline(toView(text));
    const int n = int(parsed.styles.size());
    int i = 0;
    while (i < n) {
        const std::uint16_t style = parsed.styles[i];
        int j = i + 1;
        while (j < n && parsed.styles[j] == style)
            ++j;
        if (!(style & md::Marker)) {
            QString shown = text.mid(i, j - i);
            if (style & md::Entity) { // the run may hold several, with nothing between them
                shown.clear();
                for (const md::EntityRange &entity : parsed.entities) {
                    if (entity.start >= i && entity.end <= j)
                        shown += QString::fromStdU16String(entity.text);
                }
            }
            TextRun run{shown, font(), m_theme.text, QColor()};
            if (header || (style & md::Bold))
                run.font.setBold(true);
            if (style & md::Italic)
                run.font.setItalic(true);
            if (style & md::Strike)
                run.font.setStrikeOut(true);
            if (style & md::Code) {
                run.font.setFamilies(m_theme.monoFamilies);
                run.color = m_theme.codeText;
                run.background = m_theme.codeBackground;
            }
            if (style & md::Link) {
                run.color = m_theme.accent;
                run.font.setUnderline(true);
            }
            if (style & md::Highlight)
                run.background = m_theme.highlightBackground;
            runs.push_back(run);
        }
        i = j;
    }
    return runs;
}

void MarkdownEditor::paintTable(QPainter &painter, const TableGeometry &table) const
{
    if (table.rows.empty() || table.edges.size() < 2)
        return;

    const QPointF offset = contentOffset();
    auto rectOf = [&](const QTextBlock &block) { return blockBoundingGeometry(block).translated(offset); };
    const qreal left = table.edges.front();
    const qreal right = table.edges.back();
    const QRectF headerRect = rectOf(table.rows.front());
    const qreal top = headerRect.top();
    const qreal bottom = rectOf(table.rows.back()).bottom();
    const QRectF outline(left, top, right - left, bottom - top);
    const QPen gridPen(m_theme.rule, 1);

    painter.save();
    QPainterPath clip;
    clip.addRoundedRect(outline, tableCornerRadius, tableCornerRadius);
    painter.setClipPath(clip);
    painter.fillRect(QRectF(left, top, right - left, headerRect.height()),
                     EditorTheme::mix(palette().color(QPalette::Base), m_theme.text, 0.06));

    painter.setPen(gridPen);
    for (size_t r = 1; r < table.rows.size(); ++r) {
        const qreal y = rectOf(table.rows[r]).top();
        painter.drawLine(QPointF(left, y), QPointF(right, y));
    }
    for (size_t c = 1; c + 1 < table.edges.size(); ++c)
        painter.drawLine(QPointF(table.edges[c], top), QPointF(table.edges[c], bottom));

    const int columns = int(table.edges.size()) - 1;
    for (size_t r = 0; r < table.rows.size(); ++r) {
        const QTextBlock block = table.rows[r];
        const QRectF rowRect = rectOf(block);
        const QString text = block.text();
        const md::TableRow row = md::splitTableRow(toView(text));
        const qreal centerY = rowRect.center().y();

        for (int c = 0; c < columns && c < int(row.cells.size()); ++c) {
            const md::TableCell &cell = row.cells[c];
            const std::vector<TextRun> runs = cellRuns(text.mid(cell.start, cell.end - cell.start), r == 0);
            qreal width = 0;
            for (const TextRun &run : runs)
                width += QFontMetricsF(run.font).horizontalAdvance(run.text);

            const qreal cellLeft = table.edges[c];
            const qreal cellRight = table.edges[c + 1];
            qreal x = cellLeft + tableCellPadding;
            const md::Align align = c < int(table.aligns.size()) ? table.aligns[c] : md::Align::None;
            if (align == md::Align::Center)
                x = (cellLeft + cellRight - width) / 2;
            else if (align == md::Align::Right)
                x = cellRight - tableCellPadding - width;

            painter.save();
            painter.setClipRect(QRectF(cellLeft, rowRect.top(), cellRight - cellLeft, rowRect.height()),
                                Qt::IntersectClip);
            for (const TextRun &run : runs) {
                const QFontMetricsF metrics(run.font);
                const qreal runWidth = metrics.horizontalAdvance(run.text);
                if (run.background.isValid())
                    painter.fillRect(QRectF(x, centerY - metrics.height() / 2, runWidth, metrics.height()),
                                     run.background);
                painter.setFont(run.font);
                painter.setPen(run.color);
                painter.drawText(QPointF(x, centerY + (metrics.ascent() - metrics.descent()) / 2), run.text);
                x += runWidth;
            }
            painter.restore();
        }
    }
    painter.restore();

    painter.setPen(gridPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(outline.adjusted(0.5, 0.5, -0.5, -0.5), tableCornerRadius, tableCornerRadius);
}

// Clicking a rendered table puts the cursor into the clicked cell (which reveals the Markdown).
bool MarkdownEditor::focusTableCellAt(const QPoint &pos)
{
    QTextBlock block = cursorForPosition(pos).block();
    if (!isTableBlock(block))
        return false;
    const QTextBlock header = tableHeader(block);
    if (isTableBlock(textCursor().block()) && tableHeader(textCursor().block()) == header)
        return false; // already editing this table: plain click

    const TableGeometry table = tableGeometry(header);
    if (table.edges.size() < 2 || pos.x() < table.edges.front() || pos.x() > table.edges.back())
        return false;
    if (blockType(block) == BlockType::TableDelimiter)
        block = header;

    int column = 0;
    while (column + 2 < int(table.edges.size()) && pos.x() >= table.edges[column + 1])
        ++column;
    placeInTableCell(block, column, false);
    return true;
}

void MarkdownEditor::placeInTableCell(const QTextBlock &block, int cell, bool select)
{
    const md::TableRow row = md::splitTableRow(toView(block.text()));
    QTextCursor cursor(document());
    if (row.cells.empty()) {
        cursor.setPosition(block.position() + block.length() - 1);
    } else {
        const md::TableCell &target = row.cells[std::clamp(cell, 0, int(row.cells.size()) - 1)];
        cursor.setPosition(block.position() + (select ? target.start : target.end));
        if (select)
            cursor.setPosition(block.position() + target.end, QTextCursor::KeepAnchor);
    }
    setTextCursor(cursor);
    ensureCursorVisible();
}

// Tab / Shift+Tab move between cells (selecting their content); Tab in the last cell adds a row.
bool MarkdownEditor::moveToTableCell(bool backwards)
{
    const QTextCursor cursor = textCursor();
    const QTextBlock block = cursor.block();
    const BlockType type = blockType(block);
    if (type != BlockType::TableHeader && type != BlockType::TableRow)
        return false;

    const md::TableRow row = md::splitTableRow(toView(block.text()));
    if (row.cells.empty())
        return false;
    const int pos = cursor.positionInBlock();
    int current = int(row.cells.size()) - 1;
    for (int i = 0; i < int(row.cells.size()); ++i) {
        if (pos <= row.cells[i].segmentEnd) {
            current = i;
            break;
        }
    }

    if (!backwards) {
        if (current + 1 < int(row.cells.size())) {
            placeInTableCell(block, current + 1, true);
            return true;
        }
        QTextBlock next = block.next();
        if (blockType(next) == BlockType::TableDelimiter)
            next = next.next();
        if (next.isValid() && blockType(next) == BlockType::TableRow)
            placeInTableCell(next, 0, true);
        else
            insertTableRowAfter(block);
        return true;
    }

    if (current > 0) {
        placeInTableCell(block, current - 1, true);
        return true;
    }
    QTextBlock previous = block.previous();
    if (blockType(previous) == BlockType::TableDelimiter)
        previous = previous.previous();
    if (isTableBlock(previous))
        placeInTableCell(previous, std::numeric_limits<int>::max(), true);
    return true;
}

// Enter adds a row below; Enter on an empty row leaves the table.
bool MarkdownEditor::tableEnter()
{
    QTextCursor cursor = textCursor();
    const QTextBlock block = cursor.block();
    const BlockType type = blockType(block);
    if (cursor.hasSelection() || (type != BlockType::TableHeader && type != BlockType::TableRow))
        return false;

    if (type == BlockType::TableRow) {
        const md::TableRow row = md::splitTableRow(toView(block.text()));
        const bool empty = std::all_of(row.cells.begin(), row.cells.end(),
                                       [](const md::TableCell &cell) { return cell.start == cell.end; });
        if (empty) {
            // Leave a blank line after the table: in GFM a following line without pipes would
            // otherwise still count as a table row.
            cursor.beginEditBlock();
            cursor.movePosition(QTextCursor::StartOfBlock);
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            cursor.insertBlock();
            cursor.endEditBlock();
            setTextCursor(cursor);
            return true;
        }
    }
    insertTableRowAfter(block);
    return true;
}

void MarkdownEditor::insertTableRowAfter(const QTextBlock &block)
{
    const QTextBlock header = tableHeader(block);
    const int columns = std::max<int>(1, int(md::splitTableRow(toView(header.text())).cells.size()));
    // A new row never goes between the header and the delimiter row.
    const QTextBlock after = blockType(block) == BlockType::TableHeader ? block.next() : block;

    QTextCursor cursor(after);
    cursor.movePosition(QTextCursor::EndOfBlock);
    cursor.beginEditBlock();
    cursor.insertBlock();
    cursor.insertText(emptyTableRow(columns));
    cursor.endEditBlock();
    placeInTableCell(after.next(), 0, false);
}

std::optional<MarkdownEditor::CodeBlock> MarkdownEditor::codeBlockAt(const QTextBlock &block) const
{
    if (!isCode(blockType(block)))
        return std::nullopt;
    QTextBlock open = block;
    if (blockType(open) == BlockType::FenceClose)
        open = open.previous();
    while (open.isValid() && blockType(open) != BlockType::FenceOpen)
        open = open.previous();
    if (!open.isValid())
        return std::nullopt;

    QTextBlock last = open;
    while (last.next().isValid() && blockType(last.next()) == BlockType::CodeLine)
        last = last.next();
    if (last == open)
        return std::nullopt; // empty block

    QString info = open.text().trimmed();
    while (info.startsWith(u'`') || info.startsWith(u'~'))
        info.remove(0, 1);
    CodeBlock code;
    code.start = open.next().position();
    code.end = last.position() + last.length() - 1;
    code.language = info.trimmed().section(u' ', 0, 0).toLower();
    return code;
}

QString MarkdownEditor::documentLanguage() const
{
    const QString suffix = QFileInfo(m_documentPath).suffix().toLower();
    if (suffix.isEmpty() || suffix == u"md" || suffix == u"markdown" || suffix == u"txt")
        return {};
    return FormatterRegistry::instance().languageForSuffix(suffix);
}

namespace {

// Where a caret goes when `before` is reformatted into `after`. Formatters mostly change whitespace,
// so the caret is located by how many other characters precede it.
int mapAcrossWhitespace(const QString &before, const QString &after, int offset)
{
    offset = std::clamp(offset, 0, int(before.size()));
    int preceding = 0;
    for (int i = 0; i < offset; ++i)
        preceding += before[i].isSpace() ? 0 : 1;

    // In front of text, or in a line's indentation, the caret belongs to what follows it.
    int next = offset;
    while (next < before.size() && before[next].isSpace() && before[next] != u'\n')
        ++next;
    int lineStart = offset;
    while (lineStart > 0 && before[lineStart - 1].isSpace() && before[lineStart - 1] != u'\n')
        --lineStart;
    const bool atLineStart = lineStart == 0 || before[lineStart - 1] == u'\n';
    const bool followsNext = next < before.size() && before[next] != u'\n' && (next == offset || atLineStart);

    int seen = 0;
    for (int i = 0; i < after.size(); ++i) {
        if (after[i].isSpace())
            continue;
        if (followsNext && seen == preceding)
            return i;
        ++seen;
        if (!followsNext && seen == preceding)
            return i + 1;
    }
    return preceding == 0 ? 0 : int(after.size());
}

// Where a caret at `column` of `before` goes in `after`, the same table row reformatted: the same
// cell, at the same place in its content.
int mapAcrossTableRow(const QString &before, const QString &after, int column)
{
    const md::TableRow oldRow = md::splitTableRow(toView(before));
    const md::TableRow newRow = md::splitTableRow(toView(after));
    if (oldRow.cells.empty() || newRow.cells.empty())
        return std::min(column, int(after.size()));

    int index = 0;
    while (index + 1 < int(oldRow.cells.size()) && column > oldRow.cells[index].segmentEnd)
        ++index;
    const md::TableCell &oldCell = oldRow.cells[index];
    const md::TableCell &newCell = newRow.cells[std::min(index, int(newRow.cells.size()) - 1)];
    const int inContent = std::clamp(column - oldCell.start, 0, oldCell.end - oldCell.start);
    return newCell.start + std::min(inContent, newCell.end - newCell.start);
}

} // namespace

// Replacing text collapses a cursor inside it to the end of the new text: put it back where it was.
void MarkdownEditor::restoreCursor(QTextCursor cursor, int anchor, int position)
{
    cursor.setPosition(anchor);
    cursor.setPosition(position, QTextCursor::KeepAnchor);
    if (m_batchCursor)
        *m_batchCursor = cursor; // the layout is out of date until the edit block ends
    else
        setTextCursor(cursor);
}

bool MarkdownEditor::formatRange(int start, int end, const QString &language, QString *message)
{
    QTextCursor cursor(document());
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    QString text = cursor.selectedText();
    text.replace(QChar::ParagraphSeparator, u'\n');

    const FormatterRegistry::Outcome outcome = FormatterRegistry::instance().format(language, text);
    if (!outcome.ok) {
        *message = tr("Cannot prettify %1: %2").arg(language, outcome.error);
        return false;
    }
    if (outcome.text != text) {
        QTextCursor caret = m_batchCursor ? *m_batchCursor : textCursor();
        const int anchor = caret.anchor();
        const int position = caret.position();
        const bool inside = anchor >= start && anchor <= end && position >= start && position <= end;
        cursor.insertText(outcome.text);
        if (inside) {
            restoreCursor(caret, start + mapAcrossWhitespace(text, outcome.text, anchor - start),
                          start + mapAcrossWhitespace(text, outcome.text, position - start));
        }
    }
    *message = tr("Prettified %1 (%2)").arg(language, outcome.formatter);
    return true;
}

bool MarkdownEditor::formatTable(const QTextBlock &header, QString *message)
{
    const QTextBlock last = tableEnd(header);
    std::vector<std::u16string> lines;
    for (QTextBlock b = header; b.isValid(); b = b.next()) {
        lines.push_back(b.text().toStdU16String());
        if (b == last)
            break;
    }
    QStringList formatted;
    for (const std::u16string &line : fmt::formatTable(lines))
        formatted << QString::fromStdU16String(line);

    const int start = header.position();
    const int end = last.position() + last.length() - 1;
    const int firstLine = header.blockNumber(); // `header` doesn't survive the replacement
    QTextCursor cursor(document());
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    const QString replacement = formatted.join(u'\n');
    if (cursor.selectedText().replace(QChar::ParagraphSeparator, u'\n') != replacement) {
        QTextCursor caret = m_batchCursor ? *m_batchCursor : textCursor();
        const bool inside = caret.anchor() >= start && caret.anchor() <= end && caret.position() >= start
            && caret.position() <= end;

        // Rows keep their order and number, so a caret is a (line, column) that maps row by row.
        const auto locate = [&](int position) {
            const QTextBlock block = document()->findBlock(position);
            return std::pair(block.blockNumber() - firstLine, position - block.position());
        };
        const auto [anchorLine, anchorColumn] = inside ? locate(caret.anchor()) : std::pair(0, 0);
        const auto [positionLine, positionColumn] = inside ? locate(caret.position()) : std::pair(0, 0);

        cursor.insertText(replacement);

        if (inside) {
            const auto mapped = [&](int line, int column) {
                const QTextBlock block = document()->findBlockByNumber(firstLine + line);
                return block.position()
                    + mapAcrossTableRow(QString::fromStdU16String(lines[line]), block.text(), column);
            };
            restoreCursor(caret, mapped(anchorLine, anchorColumn), mapped(positionLine, positionColumn));
        }
    }
    *message = tr("Table aligned");
    return true;
}

void MarkdownEditor::formatAtCursor()
{
    const QTextCursor cursor = textCursor();
    QString message;

    if (cursor.hasSelection()) {
        QString language;
        if (const auto code = codeBlockAt(document()->findBlock(cursor.selectionStart())))
            language = code->language;
        if (language.isEmpty())
            language = documentLanguage();
        if (language.isEmpty()) {
            const QString text = cursor.selectedText().trimmed();
            if (text.startsWith(u'{') || text.startsWith(u'['))
                language = QStringLiteral("json");
            else if (text.startsWith(u'<'))
                language = QStringLiteral("xml");
        }
        if (language.isEmpty())
            message = tr("Can't tell the language of the selection: put it in a fenced code block like ```json");
        else
            formatRange(cursor.selectionStart(), cursor.selectionEnd(), language, &message);
    } else if (const auto code = codeBlockAt(cursor.block())) {
        if (code->language.isEmpty())
            message = tr("The code block has no language: add one after the fence, e.g. ```json");
        else
            formatRange(code->start, code->end, code->language, &message);
    } else if (isTableBlock(cursor.block())) {
        formatTable(tableHeader(cursor.block()), &message);
    } else if (!documentLanguage().isEmpty() || !m_markdown) {
        formatDocument();
        return;
    } else {
        message = tr("Nothing to prettify here: select code, or put the cursor in a code block or table");
    }
    emit statusMessage(message);
}

int MarkdownEditor::formatDocument(bool quiet)
{
    QString message;
    const QString language = documentLanguage();
    if (!m_markdown && language.isEmpty()) {
        if (!quiet)
            emit statusMessage(tr("There is no formatter for this type of file"));
        return 0;
    }
    if (!language.isEmpty()) {
        const bool ok = formatRange(0, document()->characterCount() - 1, language, &message);
        if (!quiet)
            emit statusMessage(message);
        return ok ? 1 : 0;
    }

    // Collect code blocks and tables first, then format bottom-up so positions stay valid.
    struct Target {
        int block;
        bool table;
    };
    std::vector<Target> targets;
    for (QTextBlock b = document()->firstBlock(); b.isValid(); b = b.next()) {
        const BlockType type = blockType(b);
        if (type == BlockType::FenceOpen) {
            targets.push_back({b.blockNumber(), false});
        } else if (type == BlockType::TableHeader) {
            targets.push_back({b.blockNumber(), true});
            b = tableEnd(b);
        }
    }

    int formatted = 0;
    QString firstError;
    QTextCursor group(document());
    m_batchCursor = textCursor(); // tracks edits, and each target may move it
    group.beginEditBlock(); // one undo step for the whole document
    for (auto it = targets.rbegin(); it != targets.rend(); ++it) {
        const QTextBlock block = document()->findBlockByNumber(it->block);
        QString result;
        if (it->table) {
            formatted += formatTable(block, &result);
        } else if (const auto code = codeBlockAt(block.next()); code && !code->language.isEmpty()) {
            if (FormatterRegistry::instance().canonical(code->language).isEmpty())
                continue; // unknown languages are left alone
            if (formatRange(code->start, code->end, code->language, &result))
                ++formatted;
            else if (firstError.isEmpty())
                firstError = result;
        }
    }
    group.endEditBlock();
    const QTextCursor caret = *m_batchCursor;
    m_batchCursor.reset();
    if (caret.anchor() != textCursor().anchor() || caret.position() != textCursor().position())
        setTextCursor(caret);
    refreshActiveBlock(); // lines above the cursor may have changed its block number

    if (!quiet) {
        message = firstError.isEmpty() ? tr("Prettified %n block(s)", nullptr, formatted)
                                       : tr("Prettified %n block(s); %1", nullptr, formatted).arg(firstError);
        emit statusMessage(message);
    }
    return formatted;
}

// --- Editing commands for the menus ------------------------------------------------------------

namespace {

// A menu entry that shows a shortcut without owning it (the main window's action does).
QString withShortcut(const QString &text, const QKeySequence &shortcut)
{
    return shortcut.isEmpty() ? text : text + u'\t' + shortcut.toString(QKeySequence::NativeText);
}

QString fromStd(const std::u16string &text)
{
    return QString::fromStdU16String(text);
}

bool looksLikeUrl(const QString &text)
{
    static const QRegularExpression url(QStringLiteral(R"(^[a-zA-Z][a-zA-Z0-9+.-]+://\S+$|^www\.\S+$)"));
    return url.match(text.trimmed()).hasMatch();
}

} // namespace

void MarkdownEditor::setLineStyle(md::LineStyle style, int level)
{
    QTextCursor cursor = textCursor();
    const bool selection = cursor.hasSelection();
    const QTextBlock first = document()->findBlock(cursor.selectionStart());
    QTextBlock last = document()->findBlock(cursor.selectionEnd());
    if (selection && last != first && cursor.selectionEnd() == last.position())
        last = last.previous(); // a selection of whole lines ends at the start of the next one

    std::vector<std::u16string> lines;
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        lines.push_back(b.text().toStdU16String());
        if (b == last)
            break;
    }
    const std::vector<std::u16string> styled = md::toggleLineStyle(lines, style, level);
    if (styled == lines)
        return;

    QStringList text;
    for (const std::u16string &line : styled)
        text << fromStd(line);
    const QString replacement = text.join(u'\n');
    const int start = first.position();
    const int fromEnd = first.length() - 1 - cursor.positionInBlock(); // the caret stays in the same text

    QTextCursor edit(document());
    edit.setPosition(start);
    edit.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
    edit.insertText(replacement);

    if (selection) {
        edit.setPosition(start);
        edit.setPosition(start + int(replacement.size()), QTextCursor::KeepAnchor);
    } else {
        edit.setPosition(std::max(start, start + int(replacement.size()) - fromEnd));
    }
    setTextCursor(edit);
}

void MarkdownEditor::clearFormatting()
{
    QTextCursor cursor = textCursor();
    const bool selection = cursor.hasSelection();
    if (!selection) {
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    }
    const int start = cursor.selectionStart();
    QString text = cursor.selectedText();
    text.replace(QChar::ParagraphSeparator, u'\n');

    QStringList lines = text.split(u'\n'); // inline formatting never spans lines
    for (QString &line : lines)
        line = fromStd(md::stripInlineFormatting(toView(line)));
    const QString result = lines.join(u'\n');
    if (result == text) {
        emit statusMessage(tr("No formatting to clear"));
        return;
    }
    cursor.insertText(result);
    if (selection) {
        cursor.setPosition(start);
        cursor.setPosition(start + int(result.size()), QTextCursor::KeepAnchor);
    }
    setTextCursor(cursor);
}

void MarkdownEditor::insertLink(bool wiki)
{
    QTextCursor cursor = textCursor();
    QString selected = cursor.selectedText();
    selected.replace(QChar::ParagraphSeparator, u' ');
    const int start = cursor.selectionStart();

    QString text;
    int caret = 0;
    if (wiki) {
        text = QStringLiteral("[[%1]]").arg(selected);
        caret = selected.isEmpty() ? 2 : int(text.size());
    } else if (selected.isEmpty()) {
        text = QStringLiteral("[]()");
        caret = 1;
    } else if (looksLikeUrl(selected)) {
        text = QStringLiteral("[](%1)").arg(selected.trimmed());
        caret = 1; // the link text comes next
    } else {
        text = QStringLiteral("[%1]()").arg(selected);
        caret = int(text.size()) - 1; // the address comes next
    }
    cursor.insertText(text);
    cursor.setPosition(start + caret);
    setTextCursor(cursor);
}

void MarkdownEditor::insertSnippet(Snippet snippet)
{
    QTextCursor cursor = textCursor();
    const bool selection = cursor.hasSelection();

    // Code blocks and callouts wrap the selected lines.
    if (selection && (snippet == Snippet::CodeBlock || snippet == Snippet::MathBlock || snippet == Snippet::Callout)) {
        const QTextBlock first = document()->findBlock(cursor.selectionStart());
        QTextBlock last = document()->findBlock(cursor.selectionEnd());
        if (last != first && cursor.selectionEnd() == last.position())
            last = last.previous();
        QStringList lines;
        for (QTextBlock b = first; b.isValid(); b = b.next()) {
            lines << b.text();
            if (b == last)
                break;
        }
        QString text;
        int caret = 3;
        if (snippet == Snippet::CodeBlock) {
            text = QStringLiteral("```\n%1\n```").arg(lines.join(u'\n'));
        } else if (snippet == Snippet::MathBlock) {
            text = QStringLiteral("$$\n%1\n$$").arg(lines.join(u'\n'));
            caret = int(text.size());
        } else {
            for (QString &line : lines)
                line.prepend(QStringLiteral("> "));
            text = QStringLiteral("> [!note]\n") + lines.join(u'\n');
            caret = 9;
        }
        QTextCursor edit(document());
        edit.setPosition(first.position());
        edit.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
        const int start = first.position();
        edit.insertText(text);
        edit.setPosition(start + caret);
        setTextCursor(edit);
        return;
    }

    switch (snippet) {
    case Snippet::CodeBlock:
        insertBlockText(QStringLiteral("```\n\n```"), 3); // the caret goes where the language is written
        break;
    case Snippet::MathBlock:
        insertBlockText(QStringLiteral("$$\n\n$$"), 3); // the empty line between the delimiters
        break;
    case Snippet::Callout:
        insertBlockText(QStringLiteral("> [!note]\n> "), 12);
        break;
    case Snippet::HorizontalRule:
        insertBlockText(QStringLiteral("---\n"), 4);
        break;
    case Snippet::Footnote: {
        static const QRegularExpression reference(QStringLiteral(R"(\[\^(\d+)\])"));
        int number = 0;
        for (auto it = reference.globalMatch(markdown()); it.hasNext();)
            number = std::max(number, it.next().captured(1).toInt());
        const QString label = QStringLiteral("[^%1]").arg(number + 1);

        cursor.beginEditBlock();
        cursor.setPosition(cursor.selectionEnd());
        cursor.insertText(label);
        // The definition goes at the end of the note, below any earlier ones.
        cursor.movePosition(QTextCursor::End);
        const QString lastLine = document()->lastBlock().text();
        QString definition;
        if (!lastLine.isEmpty())
            definition += u'\n';
        if (!lastLine.trimmed().isEmpty() && !lastLine.startsWith(QStringLiteral("[^")))
            definition += u'\n';
        cursor.insertText(definition + label + QStringLiteral(": "));
        cursor.endEditBlock();
        setTextCursor(cursor);
        ensureCursorVisible();
        break;
    }
    }
}

bool MarkdownEditor::cursorInTable() const
{
    return m_markdown && isTableBlock(textCursor().block());
}

int MarkdownEditor::tableColumnAt(const QTextCursor &cursor) const
{
    const md::TableRow row = md::splitTableRow(toView(cursor.block().text()));
    const int pos = cursor.positionInBlock();
    for (int i = 0; i < int(row.cells.size()); ++i) {
        if (pos <= row.cells[i].segmentEnd)
            return i;
    }
    return std::max(0, int(row.cells.size()) - 1);
}

bool MarkdownEditor::tableAction(TableAction action)
{
    if (!cursorInTable())
        return false;
    const QTextCursor caret = textCursor();
    const QTextBlock header = tableHeader(caret.block());
    const QTextBlock last = tableEnd(caret.block());
    const int firstLine = header.blockNumber();
    std::vector<std::u16string> lines;
    for (QTextBlock b = header; b.isValid(); b = b.next()) {
        lines.push_back(b.text().toStdU16String());
        if (b == last)
            break;
    }
    auto grid = md::TableGrid::parse(lines);
    if (!grid)
        return false;

    int row = std::max(0, md::tableRowOfLine(caret.blockNumber() - firstLine)); // the delimiter row counts as the header
    int column = std::clamp(tableColumnAt(caret), 0, grid->columnCount() - 1);
    bool ok = true;
    using A = TableAction;
    switch (action) {
    case A::RowAbove:
        ok = row > 0; // nothing goes above the header
        if (ok)
            grid->insertRow(row);
        break;
    case A::RowBelow:
        grid->insertRow(++row);
        break;
    case A::DuplicateRow:
        ok = row > 0;
        if (ok) {
            grid->rows.insert(grid->rows.begin() + row + 1, grid->rows[row]);
            ++row;
        }
        break;
    case A::DeleteRow:
        ok = grid->removeRow(row);
        row = std::min(row, grid->rowCount() - 1);
        break;
    case A::MoveRowUp:
        ok = grid->moveRow(row, row - 1);
        --row;
        break;
    case A::MoveRowDown:
        ok = grid->moveRow(row, row + 1);
        ++row;
        break;
    case A::ColumnLeft:
        grid->insertColumn(column);
        break;
    case A::ColumnRight:
        grid->insertColumn(++column);
        break;
    case A::DeleteColumn:
        ok = grid->removeColumn(column);
        column = std::min(column, grid->columnCount() - 1);
        break;
    case A::MoveColumnLeft:
        ok = grid->moveColumn(column, column - 1);
        --column;
        break;
    case A::MoveColumnRight:
        ok = grid->moveColumn(column, column + 1);
        ++column;
        break;
    case A::AlignLeft:
        grid->setAlign(column, md::Align::Left);
        break;
    case A::AlignCenter:
        grid->setAlign(column, md::Align::Center);
        break;
    case A::AlignRight:
        grid->setAlign(column, md::Align::Right);
        break;
    case A::SortAscending:
    case A::SortDescending:
        grid->sortRows(column, action == A::SortDescending);
        break;
    case A::DeleteTable:
        break;
    }
    if (!ok)
        return false;

    const int start = header.position();
    const int end = last.position() + last.length() - 1;
    QTextCursor edit(document());
    if (action == A::DeleteTable) {
        // Take one line break along, so no empty line is left in its place.
        const bool atEnd = !last.next().isValid();
        edit.setPosition(atEnd && start > 0 ? start - 1 : start);
        edit.setPosition(atEnd ? end : end + 1, QTextCursor::KeepAnchor);
        edit.removeSelectedText();
        setTextCursor(edit);
        return true;
    }

    QStringList text;
    for (const std::u16string &line : fmt::formatTable(grid->render()))
        text << fromStd(line);
    edit.setPosition(start);
    edit.setPosition(end, QTextCursor::KeepAnchor);
    edit.insertText(text.join(u'\n'));
    placeInTableCell(document()->findBlockByNumber(firstLine + (row == 0 ? 0 : row + 1)), column, false);
    return true;
}

// --- Menus --------------------------------------------------------------------------------------

void MarkdownEditor::addFormatActions(QMenu *menu, const EditorGetter &target, bool ownShortcuts)
{
    const struct {
        const char *name;
        QKeySequence shortcut;
        const char *marker;
    } wraps[] = {
        {QT_TR_NOOP("&Bold"), QKeySequence(Qt::CTRL | Qt::Key_B), "**"},
        {QT_TR_NOOP("&Italic"), QKeySequence(Qt::CTRL | Qt::Key_I), "*"},
        {QT_TR_NOOP("&Strikethrough"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X), "~~"},
        {QT_TR_NOOP("&Highlight"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H), "=="},
        {QT_TR_NOOP("Inline &Code"), QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft), "`"},
        {QT_TR_NOOP("&Math"), QKeySequence(), "$"},
        {QT_TR_NOOP("Comm&ent"), QKeySequence(), "%%"},
    };
    for (const auto &wrap : wraps) {
        const QString marker = QString::fromLatin1(wrap.marker);
        QAction *action = menu->addAction(ownShortcuts ? tr(wrap.name) : withShortcut(tr(wrap.name), wrap.shortcut));
        if (ownShortcuts)
            action->setShortcut(wrap.shortcut);
        connect(action, &QAction::triggered, menu, [target, marker] {
            if (MarkdownEditor *editor = target())
                editor->toggleWrap(marker);
        });
    }
    menu->addSeparator();
    connect(menu->addAction(tr("Clear &Formatting")), &QAction::triggered, menu, [target] {
        if (MarkdownEditor *editor = target())
            editor->clearFormatting();
    });
}

void MarkdownEditor::addLinkActions(QMenu *menu, const EditorGetter &target, bool ownShortcuts)
{
    const QKeySequence shortcut(Qt::CTRL | Qt::Key_K);
    connect(menu->addAction(tr("Add &Link")), &QAction::triggered, menu, [target] {
        if (MarkdownEditor *editor = target())
            editor->insertLink(true);
    });
    QAction *external = menu->addAction(ownShortcuts ? tr("Add E&xternal Link") : withShortcut(tr("Add E&xternal Link"), shortcut));
    if (ownShortcuts)
        external->setShortcut(shortcut);
    connect(external, &QAction::triggered, menu, [target] {
        if (MarkdownEditor *editor = target())
            editor->insertLink(false);
    });
}

void MarkdownEditor::addParagraphActions(QMenu *menu, const EditorGetter &target)
{
    using md::LineStyle;
    struct Entry {
        QString name;
        LineStyle style;
        int level;
    };
    std::vector<Entry> entries{
        {tr("&Bullet List"), LineStyle::Bullet, 0},
        {tr("&Numbered List"), LineStyle::Numbered, 0},
        {tr("&Task List"), LineStyle::Task, 0},
    };
    for (int level = 1; level <= 6; ++level)
        entries.push_back({tr("Heading &%1").arg(level), LineStyle::Heading, level});
    entries.push_back({tr("B&ody"), LineStyle::Body, 0});
    entries.push_back({tr("&Quote"), LineStyle::Quote, 0});

    std::vector<std::pair<QAction *, Entry>> actions;
    for (const Entry &entry : entries) {
        if (entry.style == LineStyle::Heading && entry.level == 1)
            menu->addSeparator();
        if (entry.style == LineStyle::Quote)
            menu->addSeparator();
        QAction *action = menu->addAction(entry.name);
        action->setCheckable(true);
        const LineStyle style = entry.style;
        const int level = entry.level;
        connect(action, &QAction::triggered, menu, [target, style, level] {
            if (MarkdownEditor *editor = target())
                editor->setLineStyle(style, level);
        });
        actions.emplace_back(action, entry);
    }
    // Check what the current line is. A quote is checked along with what it contains.
    connect(menu, &QMenu::aboutToShow, menu, [target, actions] {
        MarkdownEditor *editor = target();
        const QString line = editor ? editor->textCursor().block().text() : QString();
        const std::u16string_view view = toView(line);
        int level = 0;
        const bool quoted = md::lineStyleOf(view) == LineStyle::Quote;
        const md::BlockInfo info = md::parseLine(view, {}).info;
        const std::u16string_view inner = quoted ? view.substr(info.contentStart) : view;
        const LineStyle style = md::lineStyleOf(inner, &level);
        for (const auto &[action, entry] : actions) {
            const bool matches = entry.style == LineStyle::Quote
                ? quoted
                : entry.style == style && (style != LineStyle::Heading || entry.level == level);
            action->setChecked(matches);
        }
    });
}

void MarkdownEditor::addInsertActions(QMenu *menu, const EditorGetter &target)
{
    const auto add = [&](const QString &name, std::function<void(MarkdownEditor *)> run) {
        connect(menu->addAction(name), &QAction::triggered, menu, [target, run] {
            if (MarkdownEditor *editor = target())
                run(editor);
        });
    };
    add(tr("&Table…"), [](MarkdownEditor *editor) {
        emit editor->insertTableRequested(editor->viewport()->mapToGlobal(editor->cursorRect().bottomLeft()));
    });
    add(tr("&Callout"), [](MarkdownEditor *editor) { editor->insertSnippet(Snippet::Callout); });
    add(tr("Code &Block"), [](MarkdownEditor *editor) { editor->insertSnippet(Snippet::CodeBlock); });
    add(tr("&Math Block"), [](MarkdownEditor *editor) { editor->insertSnippet(Snippet::MathBlock); });
    add(tr("&Horizontal Rule"), [](MarkdownEditor *editor) { editor->insertSnippet(Snippet::HorizontalRule); });
    add(tr("&Footnote"), [](MarkdownEditor *editor) { editor->insertSnippet(Snippet::Footnote); });
    menu->addSeparator();
    add(tr("&Symbol or Emoji…"), [](MarkdownEditor *editor) { emit editor->insertSymbolRequested(); });
    add(tr("Tem&plate…"), [](MarkdownEditor *editor) { emit editor->insertTemplateRequested(); });
}

void MarkdownEditor::addTableActions(QMenu *menu)
{
    const QTextCursor caret = textCursor();
    const QTextBlock header = tableHeader(caret.block());
    const int row = std::max(0, md::tableRowOfLine(caret.blockNumber() - header.blockNumber()));
    const int rows = tableEnd(caret.block()).blockNumber() - header.blockNumber(); // header + body rows
    const int columns = std::max<int>(1, int(md::splitTableRow(toView(header.text())).cells.size()));
    const int column = std::min(tableColumnAt(caret), columns - 1);

    const auto add = [&](QMenu *to, const QString &name, TableAction action, bool enabled = true) {
        QAction *item = to->addAction(name, this, [this, action] { tableAction(action); });
        item->setEnabled(enabled);
    };
    add(menu, tr("Add Row &Above"), TableAction::RowAbove, row > 0);
    add(menu, tr("Add Row &Below"), TableAction::RowBelow);
    add(menu, tr("D&uplicate Row"), TableAction::DuplicateRow, row > 0);
    add(menu, tr("Move Row U&p"), TableAction::MoveRowUp, row > 1);
    add(menu, tr("Move Row &Down"), TableAction::MoveRowDown, row > 0 && row + 1 < rows);
    add(menu, tr("Delete &Row"), TableAction::DeleteRow, row > 0);
    menu->addSeparator();
    add(menu, tr("Add Column &Left"), TableAction::ColumnLeft);
    add(menu, tr("Add Column Ri&ght"), TableAction::ColumnRight);
    add(menu, tr("Move Column Le&ft"), TableAction::MoveColumnLeft, column > 0);
    add(menu, tr("Move Column R&ight"), TableAction::MoveColumnRight, column + 1 < columns);
    add(menu, tr("Delete &Column"), TableAction::DeleteColumn, columns > 1);
    menu->addSeparator();
    QMenu *align = menu->addMenu(tr("Align Colu&mn"));
    add(align, tr("&Left"), TableAction::AlignLeft);
    add(align, tr("&Center"), TableAction::AlignCenter);
    add(align, tr("&Right"), TableAction::AlignRight);
    QMenu *sort = menu->addMenu(tr("&Sort by Column"));
    add(sort, tr("&Ascending"), TableAction::SortAscending, rows > 2);
    add(sort, tr("&Descending"), TableAction::SortDescending, rows > 2);
    menu->addSeparator();
    menu->addAction(withShortcut(tr("Prettif&y Table"), prettifyShortcut()), this, [this] { formatAtCursor(); });
    add(menu, tr("Delete &Table"), TableAction::DeleteTable);
}

std::pair<int, int> MarkdownEditor::misspelledWordAt(int position) const
{
    const QTextBlock block = document()->findBlock(position);
    if (!block.isValid())
        return {};
    const QString text = block.text();
    const int local = position - block.position();

    // The same prose rules as the highlighters (which skip the word under the cursor, so their underline
    // can't be asked: a right click has just put the cursor there).
    std::function<bool(int)> skip = [](int) { return false; };
    md::InlineResult parsed;
    int contentStart = 0;
    if (m_markdown) {
        const md::BlockInfo info = blockInfo(block);
        const md::BlockType type = info.type;
        if (isCode(type) || md::isTableType(type) || md::isMathType(type) || type == BlockType::CommentLine
            || type == BlockType::HorizontalRule)
            return {};
        contentStart = info.contentStart;
        parsed = md::parseInline(toView(text).substr(contentStart));
        constexpr std::uint16_t notProse = md::Marker | md::Code | md::Link | md::LinkUrl | md::Math | md::Entity
            | md::Comment;
        skip = [&](int i) { return i < contentStart || (parsed.styles[i - contentStart] & notProse); };
    } else if (syntax::languageForFileName(toView(QFileInfo(m_documentPath).fileName())) != 0) {
        return {}; // code
    }
    for (const auto &[start, end] : SpellChecker::instance().misspelled(text, skip)) {
        if (local >= start && local <= end)
            return {block.position() + start, block.position() + end};
    }
    return {};
}

void MarkdownEditor::addSpellingActions(QMenu *menu, int position)
{
    const auto [start, end] = misspelledWordAt(position);
    if (end <= start)
        return;
    QTextCursor word(document());
    word.setPosition(start);
    word.setPosition(end, QTextCursor::KeepAnchor);
    const QString text = word.selectedText();

    const QStringList suggestions = SpellChecker::instance().suggestions(text);
    QFont bold = menu->font();
    bold.setBold(true);
    for (const QString &suggestion : suggestions) {
        QAction *action = menu->addAction(QString(suggestion).replace(u'&', QStringLiteral("&&")), this,
                                          [this, start, end, suggestion] {
            QTextCursor edit(document());
            edit.setPosition(start);
            edit.setPosition(end, QTextCursor::KeepAnchor);
            edit.insertText(suggestion);
        });
        action->setFont(bold);
    }
    if (suggestions.isEmpty())
        menu->addAction(tr("No Suggestions"))->setEnabled(false);
    menu->addAction(tr("Add “%1” to Dictionary").arg(QString(text).replace(u'&', QStringLiteral("&&"))), this,
                    [text] { SpellChecker::instance().addToDictionary(text); });
    menu->addAction(tr("Ignore “%1”").arg(QString(text).replace(u'&', QStringLiteral("&&"))), this,
                    [text] { SpellChecker::instance().ignore(text); });
    menu->addSeparator();
}

std::unique_ptr<QMenu> MarkdownEditor::createContextMenu(const QPoint &pos)
{
    auto menu = std::make_unique<QMenu>(this);
    const EditorGetter self = [this] { return this; };
    const bool selection = textCursor().hasSelection();
    addSpellingActions(menu.get(), cursorForPosition(pos).position());

    if (m_markdown) {
        if (const auto target = linkTargetAt(pos)) {
            const QString link = *target;
            const QString file = linkFile(link);
            menu->addAction(withShortcut(tr("&Open Link"), QKeySequence(Qt::ALT | Qt::Key_Return)), this,
                            [this, link] { openLink(link); });
            if (!file.isEmpty() || link.startsWith(u'#'))
                menu->addAction(withShortcut(tr("Open to the Ri&ght"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Return)),
                                this, [this, link] { openLink(link, true); });
            menu->addAction(tr("Cop&y Link Address"), this, [link] { QGuiApplication::clipboard()->setText(link); });
            if (QFileInfo(file).isFile()) { // what the linked file can do, as in the folder panel
                menu->addAction(tr("Copy File Pat&h"), this,
                                [file] { QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(file)); });
                menu->addAction(tr("Re&veal in Folder Panel"), this, [this, file] { emit revealRequested(file); });
                menu->addAction(tr("Rena&me Linked File…"), this, [this, file] {
                    // Queued: the rename dialog shouldn't run inside the menu's event loop.
                    QMetaObject::invokeMethod(this, [this, file] { emit renameRequested(file); }, Qt::QueuedConnection);
                });
            }
            menu->addSeparator();
        }
        if (cursorInTable()) {
            addTableActions(menu->addMenu(tr("Ta&ble")));
            menu->addSeparator();
        }
        addLinkActions(menu.get(), self, false);
        menu->addSeparator();
        addFormatActions(menu->addMenu(tr("&Format")), self, false);
        addParagraphActions(menu->addMenu(tr("Pa&ragraph")), self);
        addInsertActions(menu->addMenu(tr("&Insert")), self);
        menu->addSeparator();
    } else {
        menu->addAction(withShortcut(tr("&Undo"), QKeySequence::Undo), this, &QPlainTextEdit::undo)
            ->setEnabled(document()->isUndoAvailable());
        menu->addAction(withShortcut(tr("&Redo"), QKeySequence::Redo), this, &QPlainTextEdit::redo)
            ->setEnabled(document()->isRedoAvailable());
        menu->addSeparator();
    }

    menu->addAction(withShortcut(tr("Cu&t"), QKeySequence::Cut), this, &QPlainTextEdit::cut)
        ->setEnabled(selection && !isReadOnly());
    menu->addAction(withShortcut(tr("&Copy"), QKeySequence::Copy), this, &QPlainTextEdit::copy)->setEnabled(selection);
    menu->addAction(withShortcut(tr("&Paste"), QKeySequence::Paste), this, &QPlainTextEdit::paste)
        ->setEnabled(canPaste());
    menu->addAction(withShortcut(tr("Paste as Plain T&ext"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V)), this,
                    [this] { insertPlainText(QGuiApplication::clipboard()->text()); })
        ->setEnabled(!QGuiApplication::clipboard()->text().isEmpty());
    menu->addAction(withShortcut(tr("Select &All"), QKeySequence::SelectAll), this, &QPlainTextEdit::selectAll);

    menu->addSeparator();
    // No mnemonics: P and D belong to the Paste entries.
    if (m_markdown || !documentLanguage().isEmpty()) {
        menu->addAction(withShortcut(tr("Prettify"), prettifyShortcut()), this, [this] { formatAtCursor(); });
        menu->addAction(withShortcut(tr("Prettify Document"), prettifyDocumentShortcut()), this,
                        [this] { formatDocument(); });
    }
    return menu;
}

// --- Folding ------------------------------------------------------------------------------------------

bool MarkdownEditor::isFoldable(const QTextBlock &block) const
{
    if (!m_markdown || !block.isValid())
        return false;
    const auto *data = static_cast<const BlockData *>(block.userData());
    const QTextBlock next = block.next();
    if (!data || !next.isValid())
        return false;
    const md::BlockInfo &info = data->info;
    const auto *nextData = static_cast<const BlockData *>(next.userData());
    if (!nextData)
        return false;
    const md::BlockInfo &after = nextData->info;
    if (info.type == BlockType::Heading)
        return !(after.type == BlockType::Heading && after.headingLevel <= info.headingLevel);
    if (info.isList())
        return after.type != BlockType::Blank && after.indent > info.indent && after.type != BlockType::Heading;
    if (info.type == BlockType::Quote && info.calloutStart >= 0)
        return after.type == BlockType::Quote && after.calloutStart < 0;
    return false;
}

bool MarkdownEditor::isFolded(const QTextBlock &block) const
{
    const auto *data = static_cast<const BlockData *>(block.userData());
    return data && data->folded && isFoldable(block);
}

bool MarkdownEditor::toggleFold(const QTextBlock &block)
{
    if (!isFoldable(block))
        return false;
    auto *data = static_cast<BlockData *>(block.userData());
    data->folded = !data->folded;
    applyFolds();
    if (!textCursor().block().isVisible()) { // the cursor was in what's now hidden: put it on the fold
        QTextCursor cursor(block);
        cursor.movePosition(QTextCursor::EndOfBlock);
        setTextCursor(cursor);
    }
    viewport()->update();
    return true;
}

bool MarkdownEditor::toggleFoldAtCursor()
{
    const QTextBlock current = textCursor().block();
    if (toggleFold(current))
        return true;
    for (QTextBlock block = current.previous(); block.isValid(); block = block.previous()) {
        if (blockType(block) == BlockType::Heading)
            return toggleFold(block);
    }
    return false;
}

void MarkdownEditor::setAllFolded(bool folded)
{
    for (QTextBlock block = document()->firstBlock(); block.isValid(); block = block.next()) {
        auto *data = static_cast<BlockData *>(block.userData());
        if (!data)
            continue;
        if (!folded)
            data->folded = false;
        else if (data->info.type == BlockType::Heading && isFoldable(block))
            data->folded = true;
    }
    applyFolds();
    if (!textCursor().block().isVisible())
        moveCursor(QTextCursor::Start);
    viewport()->update();
}

// Hides what folded lines fold away and shows everything else; only blocks whose visibility changes are
// laid out again.
void MarkdownEditor::applyFolds()
{
    std::vector<md::BlockInfo> infos;
    std::vector<QTextBlock> blocks;
    bool anyFolded = false;
    for (QTextBlock block = document()->firstBlock(); block.isValid(); block = block.next()) {
        const auto *data = static_cast<const BlockData *>(block.userData());
        infos.push_back(data ? data->info : md::BlockInfo{});
        blocks.push_back(block);
        anyFolded = anyFolded || (data && data->folded);
    }
    std::vector<char> hidden(blocks.size(), 0);
    if (m_markdown && anyFolded) {
        for (size_t i = 0; i < blocks.size(); ++i) {
            const auto *data = static_cast<const BlockData *>(blocks[i].userData());
            if (!data || !data->folded || hidden[i])
                continue;
            const int end = md::foldEnd(infos, int(i));
            for (int j = int(i) + 1; j <= end; ++j)
                hidden[size_t(j)] = 1;
        }
    }
    bool changed = false;
    for (size_t i = 0; i < blocks.size(); ++i) {
        QTextBlock block = blocks[i];
        const bool visible = !hidden[i];
        if (block.isVisible() == visible)
            continue;
        block.setVisible(visible);
        block.setLineCount(visible ? std::max(1, block.layout() ? block.layout()->lineCount() : 1) : 0);
        document()->markContentsDirty(block.position(), block.length());
        changed = true;
    }
    if (changed)
        viewport()->update(); // the layout updates the scroll range itself
}

// The fold marker left of a foldable line's text.
QRectF MarkdownEditor::foldMarkerRect(const QTextBlock &block) const
{
    const auto *data = static_cast<const BlockData *>(block.userData());
    if (!data)
        return {};
    const QTextLayout *layout = block.layout();
    if (!layout || layout->lineCount() == 0)
        return {};
    const QRectF r = blockBoundingGeometry(block).translated(contentOffset());
    const QTextLine first = layout->lineAt(0);
    const qreal left = data->info.isList() ? characterRect(block, data->info.indent).left()
                                           : contentOffset().x() + document()->documentMargin();
    const qreal side = 10;
    return QRectF(left - side - 4, r.top() + layout->position().y() + first.y() + (first.height() - side) / 2, side,
                  side);
}

QTextBlock MarkdownEditor::foldableBlockAt(const QPoint &pos) const
{
    if (!m_markdown)
        return {};
    const QTextBlock block = cursorForPosition(pos).block();
    if (!isFoldable(block))
        return {};
    const QRectF marker = foldMarkerRect(block);
    return marker.adjusted(-4, -4, 4, 4).contains(pos) ? block : QTextBlock();
}

// A chevron on foldable lines (on the hovered or folded ones only, like Obsidian), and "…" after a fold.
void MarkdownEditor::paintFolds(QPainter &painter, const QTextBlock &block, const QRectF &rect) const
{
    Q_UNUSED(rect);
    const bool folded = isFolded(block);
    if (!folded && block.blockNumber() != m_hoverBlock)
        return;
    if (!isFoldable(block))
        return;
    const QRectF box = foldMarkerRect(block);
    if (box.isEmpty())
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(m_theme.faint, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    QPainterPath chevron;
    const QPointF c = box.center();
    const qreal h = box.width() * 0.28;
    if (folded) { // ›
        chevron.moveTo(c.x() - h * 0.6, c.y() - h * 1.2);
        chevron.lineTo(c.x() + h * 0.6, c.y());
        chevron.lineTo(c.x() - h * 0.6, c.y() + h * 1.2);
    } else { // ⌄
        chevron.moveTo(c.x() - h * 1.2, c.y() - h * 0.6);
        chevron.lineTo(c.x(), c.y() + h * 0.6);
        chevron.lineTo(c.x() + h * 1.2, c.y() - h * 0.6);
    }
    painter.drawPath(chevron);

    if (folded) { // a small "…" pill after the line
        const QTextLayout *layout = block.layout();
        const QTextLine last = layout->lineAt(layout->lineCount() - 1);
        const QRectF r = blockBoundingGeometry(block).translated(contentOffset());
        const qreal x = r.left() + layout->position().x() + last.naturalTextRect().right() + 8;
        const qreal y = r.top() + layout->position().y() + last.y();
        const QFontMetricsF metrics(font());
        const QString dots = QStringLiteral("…");
        const QRectF pill(x, y + (last.height() - metrics.height()) / 2, metrics.horizontalAdvance(dots) + 12,
                          metrics.height());
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_theme.codeBackground);
        painter.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
        painter.setPen(m_theme.faint);
        painter.setFont(font());
        painter.drawText(pill, Qt::AlignCenter, dots);
    }
    painter.restore();
}

// Callout boxes (tinted, with a colored bar) under the text.
void MarkdownEditor::paintBoxes(QPainter &painter, const QRect &area)
{
    const QPointF offset = contentOffset();
    const qreal margin = document()->documentMargin();
    QTextBlock block = firstVisibleBlock();
    // A callout that started above the first visible line still gets its box.
    if (QTextBlock start = calloutStart(block); start.isValid())
        block = start;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    while (block.isValid()) {
        const QRectF r = blockBoundingGeometry(block).translated(offset);
        if (r.top() > area.bottom())
            break;
        const auto *data = static_cast<const BlockData *>(block.userData());
        if (!data || data->info.type != BlockType::Quote || data->info.calloutStart < 0 || !block.isVisible()) {
            block = block.next();
            continue;
        }
        QTextBlock last = block;
        for (QTextBlock next = block.next(); next.isValid() && blockType(next) == BlockType::Quote; next = next.next()) {
            const auto *nextData = static_cast<const BlockData *>(next.userData());
            if (nextData && nextData->info.calloutStart >= 0)
                break;
            last = next;
        }
        QRectF box = r;
        for (QTextBlock b = block; b.isValid(); b = b.next()) {
            if (b.isVisible())
                box = box.united(blockBoundingGeometry(b).translated(offset));
            if (b == last)
                break;
        }
        const QRectF c = characterRect(block, data->info.markerStart);
        box.setLeft(c.isValid() ? c.left() : offset.x() + margin);
        box.setRight(offset.x() + viewport()->width() - margin + 6);
        box.adjust(0, 2, 0, -2);
        const QColor color = EditorTheme::calloutColor(data->callout);
        painter.setPen(Qt::NoPen);
        painter.setBrush(EditorTheme::mix(palette().color(QPalette::Base), color, 0.10));
        painter.drawRoundedRect(box, 5, 5);
        painter.setBrush(color);
        painter.drawRoundedRect(QRectF(box.left(), box.top(), 3, box.height()), 1.5, 1.5);
        block = last.next();
    }
    painter.restore();
}

// --- Links, embeds, footnotes ---------------------------------------------------------------------------

QString MarkdownEditor::resolveLink(const QString &target, bool wiki) const
{
    if (QUrl(target).scheme().size() > 1)
        return {};
    if (const LinkResolver &resolver = linkResolver()) {
        const QString from = m_documentPath.isEmpty() ? QDir(m_baseDirectory).filePath(QStringLiteral("untitled.md"))
                                                      : m_documentPath;
        return resolver(target.section(u'#', 0, 0), wiki, from);
    }
    return {};
}

const MarkdownEditor::Embed *MarkdownEditor::embed(const QString &target) const
{
    QString path = resolveLink(target, true);
    if (path.isEmpty()) {
        const QString relative = resolvePath(target);
        path = QFileInfo(relative).suffix().isEmpty() ? relative + QStringLiteral(".md") : relative;
    }
    if (path.isEmpty() || !isMarkdownFile(path))
        return nullptr;
    const QFileInfo info(path);
    if (!info.isFile() || info.size() > 2 * 1024 * 1024 || QFileInfo(path) == QFileInfo(m_documentPath))
        return nullptr; // missing, huge, or this note itself
    const QString anchor = target.section(u'#', 1);
    const qreal width = viewport()->width() - 2 * document()->documentMargin();
    if (width < 80)
        return nullptr;
    m_hasImages = true; // re-laid out when the width changes, like images
    const QString key = path + u'#' + anchor;
    auto it = m_embeds.find(key);
    if (it != m_embeds.end() && it->modified == info.lastModified() && qFuzzyCompare(it->width, width))
        return &it.value();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return nullptr;
    QString text = QString::fromUtf8(file.readAll());
    text.remove(u'\r');
    const QString content = excerpt(text, anchor); // ![[Note#Heading]]: that section only

    Embed result;
    result.modified = info.lastModified();
    result.width = width;
    result.title = anchor.isEmpty() ? info.completeBaseName() : info.completeBaseName() + QStringLiteral(" › ") + anchor;
    result.document = std::make_shared<QTextDocument>();
    result.document->setDefaultFont(font());
    result.document->setDocumentMargin(0);
    result.document->setMarkdown(content, QTextDocument::MarkdownDialectGitHub);
    result.document->setTextWidth(width - 32);
    const qreal title = QFontMetricsF(font()).height() + 10;
    const qreal body = result.document->size().height();
    result.clipped = body > embedMaxHeight;
    result.height = title + std::min(body, embedMaxHeight) + 20;
    it = m_embeds.insert(key, result);
    return &it.value();
}

void MarkdownEditor::paintEmbed(QPainter &painter, const QTextBlock &block, const Embed &embed) const
{
    const QRectF r = blockBoundingGeometry(block).translated(contentOffset());
    const QRectF box(contentOffset().x() + document()->documentMargin(), r.top() + imageVerticalPadding, embed.width,
                     embed.height);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(m_theme.rule, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    painter.fillRect(QRectF(box.left(), box.top() + 6, 3, box.height() - 12), m_theme.accent);

    const QFontMetricsF metrics(font());
    QFont titleFont = font();
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(m_theme.faint);
    painter.drawText(QRectF(box.left() + 16, box.top() + 8, box.width() - 32, metrics.height()),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     metrics.elidedText(embed.title, Qt::ElideMiddle, box.width() - 32));

    const QRectF body(box.left() + 16, box.top() + metrics.height() + 14, box.width() - 32,
                      box.height() - metrics.height() - 24);
    painter.translate(body.topLeft());
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette = palette();
    context.palette.setColor(QPalette::Text, m_theme.text);
    context.clip = QRectF(0, 0, body.width(), body.height());
    painter.setClipRect(context.clip);
    embed.document->documentLayout()->draw(&painter, context);
    painter.restore();

    if (embed.clipped) { // fades out at the bottom: the note goes on
        QLinearGradient fade(0, body.bottom() - 30, 0, body.bottom());
        QColor base = palette().color(QPalette::Base);
        base.setAlpha(0);
        fade.setColorAt(0, base);
        base.setAlpha(255);
        fade.setColorAt(1, base);
        painter.fillRect(QRectF(body.left(), body.bottom() - 30, body.width(), 30), fade);
    }
}

std::optional<std::pair<QString, QTextBlock>> MarkdownEditor::footnoteAt(const QPoint &pos) const
{
    const QTextCursor hit = cursorForPosition(pos);
    const QTextBlock block = hit.block();
    const md::BlockInfo info = blockInfo(block);
    if (isCode(info.type) || info.type == BlockType::Blank || md::isTableType(info.type))
        return std::nullopt;
    const QString content = block.text().mid(info.contentStart);
    const md::InlineResult parsed = md::parseInline(toView(content));
    const int local = hit.positionInBlock() - info.contentStart;
    for (const md::FootnoteRange &footnote : parsed.footnotes) {
        if (footnote.definition || local < footnote.start || local > footnote.end)
            continue;
        // Only when the mouse is really over it, not anywhere on the rest of the line.
        const QRectF first = characterRect(block, info.contentStart + footnote.start);
        const QRectF last = characterRect(block, info.contentStart + footnote.end - 1);
        if (!first.united(last).adjusted(-3, -3, 3, 3).contains(pos))
            continue;
        const QString label = content.mid(footnote.labelStart, footnote.labelEnd - footnote.labelStart);
        const QString prefix = QStringLiteral("[^%1]:").arg(label);
        for (QTextBlock b = document()->firstBlock(); b.isValid(); b = b.next()) {
            const QString text = b.text().trimmed();
            if (!text.startsWith(prefix))
                continue;
            // The definition and its indented continuation lines.
            QStringList lines{text.mid(prefix.size()).trimmed()};
            for (QTextBlock more = b.next(); more.isValid() && more.text().startsWith(QStringLiteral("    ")); more = more.next())
                lines << more.text().trimmed();
            return std::pair{lines.join(u' '), b};
        }
        return std::pair{tr("Footnote [^%1] has no definition.").arg(label), QTextBlock()};
    }
    return std::nullopt;
}
