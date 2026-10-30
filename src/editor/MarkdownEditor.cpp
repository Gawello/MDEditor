#include "editor/MarkdownEditor.h"

#include "editor/LivePreviewHighlighter.h"
#include "markdown/InlineParser.h"

#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QScopedValueRollback>
#include <QTextBlock>
#include <QTextLayout>
#include <QUrl>

#include <algorithm>

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

} // namespace

MarkdownEditor::MarkdownEditor(QWidget *parent)
    : QPlainTextEdit(parent)
{
    QFont base = font();
    base.setPointSizeF(12);
    setFont(base);

    setFrameShape(QFrame::NoFrame);
    // Fill the side margins (outside of the viewport) with the text background.
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    viewport()->setMouseTracking(true);
    document()->setDocumentMargin(16);

    m_theme = EditorTheme::fromPalette(palette(), font());
    m_highlighter = new LivePreviewHighlighter(document(), &m_theme);

    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        const int block = textCursor().blockNumber();
        if (block != m_highlighter->activeBlock()) {
            m_highlighter->setActiveBlock(block);
            viewport()->update();
        }
    });

    applyTheme();
    document()->setModified(false); // setDocumentMargin() counts as a modification
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
    m_highlighter->setActiveBlock(textCursor().blockNumber());
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
    return QPlainTextEdit::event(event);
}

void MarkdownEditor::keyPressEvent(QKeyEvent *event)
{
    const auto modifiers = event->modifiers() & ~Qt::KeypadModifier;
    const int key = event->key();

    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && modifiers == Qt::NoModifier && continueList())
        return;
    if (key == Qt::Key_Tab && modifiers == Qt::NoModifier && indentListItem(false))
        return;
    if (key == Qt::Key_Backtab && indentListItem(true))
        return;

    QPlainTextEdit::keyPressEvent(event);
}

void MarkdownEditor::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QPoint pos = event->position().toPoint();
        if (event->modifiers() & Qt::ControlModifier) {
            if (const auto target = linkTargetAt(pos)) {
                openLink(*target);
                return;
            }
        } else if (toggleTaskAt(pos)) {
            return;
        }
    }
    QPlainTextEdit::mousePressEvent(event);
}

void MarkdownEditor::mouseMoveEvent(QMouseEvent *event)
{
    QPlainTextEdit::mouseMoveEvent(event);
    const bool overLink = (event->modifiers() & Qt::ControlModifier)
        && linkTargetAt(event->position().toPoint()).has_value();
    viewport()->setCursor(overLink ? Qt::PointingHandCursor : Qt::IBeamCursor);
}

void MarkdownEditor::paintEvent(QPaintEvent *event)
{
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
            if (data && block.isVisible() && isCode(data->info.type)) {
                painter.fillRect(QRectF(offset.x() + margin - 6, r.top(), viewport()->width() - 2 * margin + 12,
                                        r.height()),
                                 m_theme.codeBackground);
            }
        }
    }

    QPlainTextEdit::paintEvent(event);

    // Decorations replacing hidden markers go on top.
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);
    for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next()) {
        const QRectF r = blockBoundingGeometry(block).translated(offset);
        if (r.top() > area.bottom())
            break;
        const auto *data = static_cast<BlockData *>(block.userData());
        if (!data || !block.isVisible() || block.blockNumber() == activeBlock)
            continue;
        const md::BlockInfo &info = data->info;

        switch (info.type) {
        case BlockType::Quote: {
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

void MarkdownEditor::dropEvent(QDropEvent *event)
{
    const QStringList files = localFiles(event->mimeData());
    if (!files.isEmpty()) {
        event->acceptProposedAction();
        emit filesDropped(files);
        return;
    }
    QPlainTextEdit::dropEvent(event);
}

void MarkdownEditor::applyTheme()
{
    if (m_applyingTheme || !m_highlighter) // setFont() in the constructor gets here first
        return;
    const QScopedValueRollback guard(m_applyingTheme, true);

    m_theme = EditorTheme::fromPalette(palette(), font());
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
    m_highlighter->rehighlight();
    updateMargins();
    viewport()->update();
}

// Keeps lines at a comfortable reading width by centering the text column.
void MarkdownEditor::updateMargins()
{
    const int readableWidth = fontMetrics().averageCharWidth() * 100;
    const int side = std::max(0, (width() - readableWidth) / 2);
    if (viewportMargins().left() != side)
        setViewportMargins(side, 0, side, 0);
}

md::BlockInfo MarkdownEditor::blockInfo(const QTextBlock &block) const
{
    const auto state = md::FenceState::decode(block.previous().isValid() ? block.previous().userState() : -1);
    return md::parseLine(toView(block.text()), state).info;
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
        edit.insertText(QStringLiteral("    "));
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
    const QTextCursor hit = cursorForPosition(pos);
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

void MarkdownEditor::openLink(const QString &target)
{
    const QUrl url(target);
    if (url.isValid() && url.scheme().size() > 1) { // one-letter schemes are Windows drive letters
        QDesktopServices::openUrl(url);
        return;
    }

    const QString path = QUrl::fromPercentEncoding(target.section(u'#', 0, 0).toUtf8());
    if (path.isEmpty())
        return;

    const QDir base(m_baseDirectory.isEmpty() ? QDir::currentPath() : m_baseDirectory);
    QFileInfo file(base.filePath(path));
    if (!file.exists() && file.suffix().isEmpty())
        file.setFile(file.filePath() + QStringLiteral(".md"));

    const QString suffix = file.suffix().toLower();
    if (suffix == u"md" || suffix == u"markdown" || suffix == u"txt")
        emit openFileRequested(file.absoluteFilePath());
    else
        QDesktopServices::openUrl(QUrl::fromLocalFile(file.absoluteFilePath()));
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
