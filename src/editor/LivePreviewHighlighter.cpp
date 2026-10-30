#include "editor/LivePreviewHighlighter.h"

#include "markdown/InlineParser.h"

#include <QTextDocument>

using md::BlockType;

LivePreviewHighlighter::LivePreviewHighlighter(QTextDocument *document, const EditorTheme *theme)
    : QSyntaxHighlighter(document)
    , m_theme(theme)
{
}

void LivePreviewHighlighter::setActiveBlock(int blockNumber)
{
    if (blockNumber == m_activeBlock)
        return;
    const int previous = m_activeBlock;
    m_activeBlock = blockNumber;
    for (const int number : {previous, blockNumber}) {
        const QTextBlock block = document()->findBlockByNumber(number);
        if (block.isValid())
            rehighlightBlock(block);
    }
}

QTextCharFormat LivePreviewHighlighter::dimmed(QTextCharFormat format) const
{
    format.setForeground(m_theme->faint);
    format.setFontUnderline(false);
    format.setFontStrikeOut(false);
    format.setFontItalic(false);
    return format;
}

// Hidden but still taking up space, so the editor can paint a decoration there.
QTextCharFormat LivePreviewHighlighter::invisible(QTextCharFormat format) const
{
    format.setForeground(Qt::transparent);
    format.clearBackground();
    format.setFontUnderline(false);
    format.setFontStrikeOut(false);
    return format;
}

// Hidden and shrunk to (almost) zero width.
QTextCharFormat LivePreviewHighlighter::collapsed(QTextCharFormat format) const
{
    format = invisible(format);
    format.setFontPointSize(0.1);
    format.setFontLetterSpacingType(QFont::AbsoluteSpacing);
    format.setFontLetterSpacing(0);
    return format;
}

QTextCharFormat LivePreviewHighlighter::codeFormat() const
{
    QTextCharFormat format;
    format.setFontFamilies(m_theme->monoFamilies);
    return format;
}

void LivePreviewHighlighter::highlightBlock(const QString &text)
{
    const auto result = md::parseLine(toView(text), md::FenceState::decode(previousBlockState()));
    setCurrentBlockState(result.next.encode());

    auto *data = static_cast<BlockData *>(currentBlockUserData());
    if (!data) {
        data = new BlockData;
        setCurrentBlockUserData(data);
    }
    data->info = result.info;

    const md::BlockInfo &b = result.info;
    const bool active = currentBlock().blockNumber() == m_activeBlock;
    const int length = int(text.size());

    switch (b.type) {
    case BlockType::Blank:
        return;
    case BlockType::FenceOpen:
    case BlockType::FenceClose:
        setFormat(0, length, dimmed(codeFormat()));
        return;
    case BlockType::CodeLine:
        setFormat(0, length, codeFormat());
        return;
    case BlockType::HorizontalRule:
        setFormat(0, length, active ? dimmed({}) : invisible({}));
        return;
    default:
        break;
    }

    QTextCharFormat base;
    if (b.type == BlockType::Heading) {
        base.setFontPointSize(m_theme->headingPointSize(b.headingLevel));
        base.setFontWeight(QFont::Bold);
    }

    if (b.markerLength > 0) {
        QTextCharFormat marker;
        switch (b.type) {
        case BlockType::Heading:
            marker = active ? dimmed(base) : collapsed(base);
            break;
        case BlockType::OrderedItem:
            marker = dimmed(base);
            break;
        default: // quotes, bullets and tasks keep their width; the editor paints over it
            marker = active ? dimmed(base) : invisible(base);
            break;
        }
        setFormat(b.markerStart, b.markerLength, marker);
        // The checkbox takes the place of the bullet.
        if (b.type == BlockType::TaskItem && !active)
            setFormat(b.markerStart, b.checkboxStart - b.markerStart, collapsed(base));
    }

    QTextCharFormat content = base;
    if (b.type == BlockType::TaskItem && b.checked) {
        content.setForeground(m_theme->faint);
        content.setFontStrikeOut(true);
    }
    if (b.contentStart < length) {
        setFormat(b.contentStart, length - b.contentStart, content);
        applyInline(text.mid(b.contentStart), b.contentStart, content, active);
    }
}

void LivePreviewHighlighter::applyInline(const QString &text, int offset, const QTextCharFormat &base,
                                         bool active)
{
    const md::InlineResult parsed = md::parseInline(toView(text));
    const int n = int(parsed.styles.size());

    int i = 0;
    while (i < n) {
        const std::uint16_t style = parsed.styles[i];
        int j = i + 1;
        while (j < n && parsed.styles[j] == style)
            ++j;

        if (style != 0) {
            QTextCharFormat format = base;
            if (style & md::Bold)
                format.setFontWeight(QFont::Bold);
            if (style & md::Italic)
                format.setFontItalic(true);
            if (style & md::Strike)
                format.setFontStrikeOut(true);
            if (style & md::Highlight)
                format.setBackground(m_theme->highlightBackground);
            if (style & md::Code) {
                format.setFontFamilies(m_theme->monoFamilies);
                format.setForeground(m_theme->codeText);
                format.setBackground(m_theme->codeBackground);
            }
            if (style & md::Link) {
                format.setForeground(m_theme->accent);
                format.setFontUnderline(true);
            }
            if (style & md::Marker)
                format = active ? dimmed(format) : collapsed(format);
            setFormat(offset + i, j - i, format);
        }
        i = j;
    }
}
