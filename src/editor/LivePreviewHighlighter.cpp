#include "editor/LivePreviewHighlighter.h"

#include "markdown/InlineParser.h"
#include "markdown/TableParser.h"

#include <QFontMetricsF>
#include <QTextDocument>
#include <QTimer>

#include <vector>

using md::BlockType;

md::BlockType blockType(const QTextBlock &block)
{
    const auto *data = static_cast<const BlockData *>(block.userData());
    return data ? data->info.type : BlockType::Paragraph;
}

bool isTableBlock(const QTextBlock &block)
{
    return block.isValid() && md::isTableType(blockType(block));
}

QTextBlock tableHeader(QTextBlock block)
{
    while (blockType(block) != BlockType::TableHeader && isTableBlock(block.previous()))
        block = block.previous();
    return block;
}

QTextBlock tableEnd(QTextBlock block)
{
    while (block.next().isValid()) {
        const BlockType next = blockType(block.next());
        if (next != BlockType::TableDelimiter && next != BlockType::TableRow)
            break;
        block = block.next();
    }
    return block;
}

LivePreviewHighlighter::LivePreviewHighlighter(QTextDocument *document, const EditorTheme *theme)
    : QSyntaxHighlighter(document)
    , m_theme(theme)
{
}

bool LivePreviewHighlighter::setActiveBlock(int blockNumber)
{
    int first = -1;
    int last = -1;
    const QTextBlock block = document()->findBlockByNumber(blockNumber);
    if (isTableBlock(block)) {
        first = tableHeader(block).blockNumber();
        last = tableEnd(block).blockNumber();
    }
    if (blockNumber == m_activeBlock && first == m_tableFirst && last == m_tableLast)
        return false;

    std::vector<int> dirty{m_activeBlock, blockNumber};
    if (first != m_tableFirst || last != m_tableLast) {
        for (int i = m_tableFirst; i >= 0 && i <= m_tableLast; ++i)
            dirty.push_back(i);
        for (int i = first; i >= 0 && i <= last; ++i)
            dirty.push_back(i);
    }
    m_activeBlock = blockNumber;
    m_tableFirst = first;
    m_tableLast = last;

    for (const int number : dirty) {
        const QTextBlock b = document()->findBlockByNumber(number);
        if (b.isValid())
            rehighlightBlock(b);
    }
    return true;
}

bool LivePreviewHighlighter::isActive(int blockNumber) const
{
    return blockNumber == m_activeBlock || (m_tableFirst >= 0 && blockNumber >= m_tableFirst && blockNumber <= m_tableLast);
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
    const QTextBlock next = currentBlock().next();
    const QString nextText = next.isValid() ? next.text() : QString();
    const auto nextLine = next.isValid() ? std::optional(toView(nextText)) : std::nullopt;
    const auto result = md::parseLine(toView(text), md::BlockState::decode(previousBlockState()), nextLine);
    setCurrentBlockState(result.next.encode());

    auto *data = static_cast<BlockData *>(currentBlockUserData());
    if (!data) {
        data = new BlockData;
        setCurrentBlockUserData(data);
    }
    data->info = result.info;
    data->image.clear();
    data->imageWidth = 0;
    data->imageShown = false;
    recheckPreviousBlock(text);

    const md::BlockInfo &b = result.info;
    const bool active = isActive(currentBlock().blockNumber());
    const int length = int(text.size());

    if (b.type == BlockType::Paragraph && highlightImageLine(text, data, active))
        return;

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
    case BlockType::TableHeader:
    case BlockType::TableDelimiter:
    case BlockType::TableRow:
        highlightTableRow(text, b.type, active);
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

// Outside of the active table the text is hidden and the editor paints a grid instead;
// the delimiter row collapses to (almost) no height.
void LivePreviewHighlighter::highlightTableRow(const QString &text, md::BlockType type, bool active)
{
    const int length = int(text.size());
    if (!active) {
        if (type == BlockType::TableDelimiter) {
            setFormat(0, length, collapsed({}));
            return;
        }
        setFormat(0, length, invisible({}));
        // One taller (invisible) character gives the painted row some vertical padding.
        QTextCharFormat tall = invisible({});
        tall.setFontPointSize(m_theme->baseFont.pointSizeF() * 1.7);
        setFormat(0, 1, tall);
        return;
    }
    if (type == BlockType::TableDelimiter) {
        setFormat(0, length, dimmed({}));
        return;
    }

    QTextCharFormat base;
    if (type == BlockType::TableHeader)
        base.setFontWeight(QFont::Bold);
    setFormat(0, length, base);

    const md::TableRow row = md::splitTableRow(toView(text));
    for (const int pipe : row.pipes)
        setFormat(pipe, 1, dimmed(base));
    for (const md::TableCell &cell : row.cells) {
        if (cell.end > cell.start)
            applyInline(text.mid(cell.start, cell.end - cell.start), cell.start, base, true);
    }
}

// A line holding nothing but an image is replaced by the image (painted by the editor). The line gets
// the image's height from one invisible, very tall character; everything else is collapsed.
// Returns true when the line has been formatted.
bool LivePreviewHighlighter::highlightImageLine(const QString &text, BlockData *data, bool active)
{
    const QString trimmed = text.trimmed();
    if (!trimmed.startsWith(u'!'))
        return false;
    const md::InlineResult parsed = md::parseInline(toView(text));
    if (parsed.links.size() != 1 || !parsed.links.front().image)
        return false;
    const md::LinkRange &link = parsed.links.front();
    const int first = int(text.indexOf(trimmed));
    if (link.start != first || link.end != first + trimmed.size())
        return false;

    data->image = text.mid(link.urlStart, link.urlEnd - link.urlStart);
    // Width hint: ![[img.png|300]] (the alias) or ![alt|300](img.png) (after the last '|').
    const QString alt = text.mid(link.textStart, link.textEnd - link.textStart);
    const QString hint = link.wiki ? alt : (alt.contains(u'|') ? alt.section(u'|', -1) : QString());
    data->imageWidth = hint.section(u'x', 0, 0).trimmed().toInt();

    if (active || !m_imageSize)
        return false;
    const QSizeF size = m_imageSize(data->image, data->imageWidth);
    if (size.isEmpty())
        return false; // not loadable: keep it visible as a link

    QFont probe = m_theme->baseFont;
    probe.setPointSizeF(100);
    const QFontMetricsF metrics(probe);
    QTextCharFormat tall = invisible({});
    tall.setFontPointSize(100 * (size.height() + 2 * imageVerticalPadding) / (metrics.ascent() + metrics.descent()));

    setFormat(0, int(text.size()), collapsed({}));
    setFormat(0, 1, tall);
    data->imageShown = true;
    return true;
}

// Whether a line is a table header depends on the line below it. When this line changes in a way
// that changes the previous line's classification, re-highlight the previous line (deferred,
// since we're in the middle of highlighting).
void LivePreviewHighlighter::recheckPreviousBlock(const QString &text)
{
    const QTextBlock previous = currentBlock().previous();
    if (!previous.isValid() || !previous.userData())
        return;
    const QString previousText = previous.text();
    if (!previousText.contains(u'|'))
        return;

    const QTextBlock beforePrevious = previous.previous();
    const auto state = md::BlockState::decode(beforePrevious.isValid() ? beforePrevious.userState() : -1);
    const BlockType expected = md::parseLine(toView(previousText), state, toView(text)).info.type;
    if (expected == blockType(previous))
        return;

    const int number = previous.blockNumber();
    QTimer::singleShot(0, this, [this, number] {
        const QTextBlock block = document()->findBlockByNumber(number);
        if (block.isValid())
            rehighlightBlock(block);
        // The table under the cursor may have appeared or disappeared.
        const int active = m_activeBlock;
        m_activeBlock = -2;
        setActiveBlock(active);
    });
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
