#include "editor/LivePreviewHighlighter.h"

#include "editor/CodeHighlighter.h"

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

QTextBlock mathBlockStart(QTextBlock block)
{
    while (blockType(block) != BlockType::MathOpen && blockType(block) != BlockType::MathSingle
           && block.previous().isValid() && md::isMathType(blockType(block.previous())))
        block = block.previous();
    return block;
}

QTextBlock mathBlockEnd(QTextBlock block)
{
    while (blockType(block) != BlockType::MathClose && blockType(block) != BlockType::MathSingle
           && block.next().isValid() && md::isMathType(blockType(block.next()))
           && blockType(block.next()) != BlockType::MathOpen && blockType(block.next()) != BlockType::MathSingle)
        block = block.next();
    return block;
}

QTextBlock frontmatterEnd(QTextBlock block)
{
    while (blockType(block) != BlockType::FrontmatterClose && block.next().isValid()
           && md::isFrontmatterType(blockType(block.next())))
        block = block.next();
    return block;
}

QTextBlock calloutStart(QTextBlock block)
{
    while (block.isValid() && blockType(block) == BlockType::Quote) {
        const auto *data = static_cast<const BlockData *>(block.userData());
        if (data && data->info.calloutStart >= 0)
            return block;
        block = block.previous();
    }
    return {};
}

LivePreviewHighlighter::LivePreviewHighlighter(QTextDocument *document, const EditorTheme *theme)
    : QSyntaxHighlighter(document)
    , m_theme(theme)
    , m_document(document)
{
}

void LivePreviewHighlighter::setAttached(bool attached)
{
    if (attached == isAttached())
        return;
    m_activeBlock = -1;
    m_tableFirst = -1;
    m_tableLast = -1;
    if (attached) {
        setDocument(m_document); // highlights everything
        return;
    }
    setDocument(nullptr); // clears the formats
    for (QTextBlock block = m_document->firstBlock(); block.isValid(); block = block.next()) {
        block.setUserData(nullptr);
        block.setUserState(-1);
    }
}

bool LivePreviewHighlighter::setActiveBlock(int blockNumber)
{
    if (!isAttached())
        return false;
    int first = -1;
    int last = -1;
    const QTextBlock block = document()->findBlockByNumber(blockNumber);
    if (isTableBlock(block)) {
        first = tableHeader(block).blockNumber();
        last = tableEnd(block).blockNumber();
    } else if (md::isMathType(blockType(block))) { // a formula shows its source as a whole, like a table
        first = mathBlockStart(block).blockNumber();
        last = mathBlockEnd(block).blockNumber();
    } else if (md::isFrontmatterType(blockType(block))) { // so do properties
        first = 0;
        last = frontmatterEnd(block).blockNumber();
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
    // 0.1pt rounds to 0 px, which fontconfig can resolve to a bitmap emoji font. Without this, every
    // glyph that font lacks searches all system fonts: many seconds on a machine with many fonts.
    format.setFontStyleStrategy(QFont::NoFontMerging);
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
    const md::BlockState previous = md::BlockState::decode(previousBlockState());
    const auto result = md::parseLine(toView(text), previous, nextLine, currentBlock().blockNumber() == 0);
    md::BlockState nextState = result.next;

    // Code in fences is colored by its language (from the info string), carried in the state's `code` bits:
    // language << stateBits | what the previous code line left open.
    constexpr int stateMask = (1 << syntax::stateBits) - 1;
    int codeLanguage = 0;
    if (result.info.type == BlockType::FenceOpen) {
        nextState.code = syntax::languageForName(toView(text).substr(result.info.contentStart)) << syntax::stateBits;
    } else if (result.info.type == BlockType::CodeLine) {
        codeLanguage = previous.code >> syntax::stateBits;
        nextState.code = previous.code; // set below once the line is colored
    }
    const int codeState = previous.code & stateMask;
    setCurrentBlockState(nextState.encode()); // code lines set it again once colored

    auto *data = static_cast<BlockData *>(currentBlockUserData());
    if (!data) {
        data = new BlockData;
        setCurrentBlockUserData(data);
    }
    data->info = result.info;
    data->image.clear();
    data->imageWidth = 0;
    data->imageShown = false;
    data->entities.clear();
    data->math.clear();
    data->mathBlock.clear();
    data->mathShown = false;
    data->callout.clear();
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
    case BlockType::CodeLine: {
        setFormat(0, length, codeFormat());
        if (codeLanguage) {
            const int after = applySyntax(codeLanguage, text, codeState, *m_theme, codeFormat(),
                                          [this](int start, int count, const QTextCharFormat &format) {
                                              setFormat(start, count, format);
                                          });
            nextState.code = (codeLanguage << syntax::stateBits) | after;
        }
        setCurrentBlockState(nextState.encode());
        return;
    }
    case BlockType::HorizontalRule:
        setFormat(0, length, active ? dimmed({}) : invisible({}));
        return;
    case BlockType::MathOpen:
    case BlockType::MathLine:
    case BlockType::MathClose:
    case BlockType::MathSingle:
        highlightMathBlock(text, b, data, active);
        return;
    case BlockType::CommentLine: {
        QTextCharFormat comment;
        comment.setForeground(m_theme->faint);
        comment.setFontItalic(true);
        setFormat(0, length, comment);
        return;
    }
    case BlockType::TableHeader:
    case BlockType::TableDelimiter:
    case BlockType::TableRow:
        highlightTableRow(text, b.type, active);
        return;
    case BlockType::FrontmatterOpen:
    case BlockType::FrontmatterLine:
    case BlockType::FrontmatterClose:
        highlightFrontmatter(text, b.type, active);
        return;
    case BlockType::Quote:
        if (b.calloutStart >= 0) {
            highlightCallout(text, b, data, active);
            return;
        }
        break;
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

        // Spelling: prose only, not code, links, math, comments or hidden markers.
        const md::InlineResult parsed = md::parseInline(toView(text).substr(b.contentStart));
        constexpr std::uint16_t notProse = md::Marker | md::Code | md::Link | md::LinkUrl | md::Math | md::Entity
            | md::Comment;
        const int contentStart = b.contentStart;
        underlineMisspelled(
            text, currentBlock().position(), m_caret ? m_caret() : -1,
            [&](int i) { return i < contentStart || (parsed.styles[i - contentStart] & notProse); },
            [this](int i) { return format(i); },
            [this](int start, int count, const QTextCharFormat &f) { setFormat(start, count, f); });
    }
}

// Properties: the source while the cursor is in them; otherwise the "---" lines shrink away, keys are dimmed
// and the editor paints a box behind them.
void LivePreviewHighlighter::highlightFrontmatter(const QString &text, md::BlockType type, bool active)
{
    const int length = int(text.size());
    QTextCharFormat source = codeFormat();
    source.setForeground(m_theme->codeText);
    if (active) {
        setFormat(0, length, type == BlockType::FrontmatterLine ? source : dimmed(source));
        return;
    }
    if (type != BlockType::FrontmatterLine) {
        setFormat(0, length, collapsed({}));
        return;
    }
    QTextCharFormat value;
    value.setFontPointSize(m_theme->baseFont.pointSizeF() * 0.95);
    setFormat(0, length, value);
    const int colon = int(text.indexOf(u':'));
    if (colon > 0 && !text.at(0).isSpace()) {
        QTextCharFormat key = value;
        key.setForeground(m_theme->faint);
        setFormat(0, colon + 1, key);
    } else if (const int dash = int(text.indexOf(u'-')); dash >= 0 && text.left(dash).trimmed().isEmpty()) {
        setFormat(dash, 1, dimmed(value)); // "  - item"
    }
}

// The first line of a callout: "> [!type]- Title" shows as the title (or the type), bold and colored; the
// editor paints the box. While the cursor is on it, it's an ordinary quote line.
void LivePreviewHighlighter::highlightCallout(const QString &text, const md::BlockInfo &info, BlockData *data,
                                              bool active)
{
    const int length = int(text.size());
    const int close = int(text.indexOf(u']', info.calloutStart));
    const QString type = text.mid(info.calloutStart + 2, close - info.calloutStart - 2).toLower();
    data->callout = type;
    if (!data->foldSeen) { // "[!type]-" starts folded, once: after that it's the user's choice
        data->foldSeen = true;
        data->folded = info.calloutFold == u'-';
    }

    QTextCharFormat base;
    base.setFontWeight(QFont::DemiBold);
    base.setForeground(EditorTheme::calloutColor(type));
    setFormat(0, length, base);
    if (active) {
        setFormat(info.markerStart, info.calloutEnd - info.markerStart, dimmed(base));
        if (info.calloutEnd < length)
            applyInline(text.mid(info.calloutEnd), info.calloutEnd, base, true);
        return;
    }
    setFormat(info.markerStart, info.markerLength, invisible(base)); // "> ": the box's padding
    const bool titled = !text.mid(info.calloutEnd).trimmed().isEmpty();
    if (titled) {
        setFormat(info.calloutStart, info.calloutEnd - info.calloutStart, collapsed(base));
        applyInline(text.mid(info.calloutEnd), info.calloutEnd, base, false);
    } else {
        // No title: the type stands in for it, capitalized ("[!tip]" reads "Tip").
        setFormat(info.calloutStart, 2, collapsed(base));
        setFormat(info.calloutEnd - (info.calloutFold ? 2 : 1), info.calloutFold ? 2 : 1, collapsed(base));
        QTextCharFormat word = base;
        word.setFontCapitalization(QFont::Capitalize);
        setFormat(info.calloutStart + 2, int(type.size()), word);
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
    const BlockType expected =
        md::parseLine(toView(previousText), state, toView(text), previous.blockNumber() == 0).info.type;
    if (expected == blockType(previous))
        return;

    const int number = previous.blockNumber();
    QTimer::singleShot(0, this, [this, number] {
        if (!isAttached())
            return; // switched to plain text meanwhile
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
            if (style & md::Math) {
                format.setFontFamilies(m_theme->monoFamilies);
                format.setForeground(m_theme->codeText);
            }
            if (style & md::Comment) { // stays visible, %% included, so it's clear what won't be published
                format.setForeground(m_theme->faint);
                format.setFontItalic(true);
            }
            if (style & md::Tag) { // a pill, like Obsidian's
                format.setForeground(m_theme->accent);
                format.setBackground(EditorTheme::mix(m_theme->codeBackground, m_theme->accent, 0.14));
            }
            if ((style & md::Footnote) && !active) { // the label as a superscript; the brackets collapse
                format.setForeground(m_theme->accent);
                format.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
            }
            if ((style & md::Marker) && !(style & md::Comment))
                format = active ? dimmed(format) : collapsed(format);
            setFormat(offset + i, j - i, format);
        }
        i = j;
    }

    if (!active) {
        for (const md::EntityRange &entity : parsed.entities)
            showEntity(entity, offset);
        for (const md::MathRange &math : parsed.math)
            showMath(text, math, offset);
    }
}

// An invisible character whose font makes its line `height` pixels tall.
QTextCharFormat LivePreviewHighlighter::tallSlot(qreal height) const
{
    QFont probe = m_theme->baseFont;
    probe.setPointSizeF(100);
    const QFontMetricsF metrics(probe);
    QTextCharFormat tall = invisible({});
    tall.setFontPointSize(100 * height / (metrics.ascent() + metrics.descent()));
    return tall;
}

// Like a character reference: the opening '$' becomes an invisible slot as wide as the formula (and as tall,
// if it's taller than the line), the rest collapses, and the editor paints the formula over the slot.
void LivePreviewHighlighter::showMath(const QString &text, const md::MathRange &math, int offset)
{
    if (!m_mathSize)
        return;
    const QString tex = text.mid(math.contentStart, math.contentEnd - math.contentStart);
    const MathMetrics metrics = m_mathSize(tex, math.display);
    if (metrics.size.isEmpty())
        return; // stays as source

    const int position = offset + math.start;
    const QTextCharFormat shown = format(position);
    QFont font = shown.font().resolve(m_theme->baseFont);
    QTextCharFormat slot = invisible(shown);
    const QFontMetricsF line(font);
    if (metrics.size.height() > line.ascent() + line.descent()) {
        slot = tallSlot(metrics.size.height());
        font.setPointSizeF(slot.fontPointSize());
    }
    slot.setFontLetterSpacingType(QFont::AbsoluteSpacing);
    slot.setFontLetterSpacing(metrics.size.width() - QFontMetricsF(font).horizontalAdvance(u'$'));
    setFormat(position, 1, slot);
    setFormat(position + 1, math.end - math.start - 1, collapsed(shown));

    if (auto *data = static_cast<BlockData *>(currentBlockUserData()))
        data->math.push_back({position, tex, math.display});
}

// $$ blocks: the source while the cursor is in the block; otherwise the first line takes the height of
// the rendered formula and the rest of the block collapses.
void LivePreviewHighlighter::highlightMathBlock(const QString &text, const md::BlockInfo &info, BlockData *data,
                                                bool active)
{
    const int length = int(text.size());
    QTextCharFormat source = codeFormat();
    source.setForeground(m_theme->codeText);

    QString tex;
    if (!active && m_mathSize) {
        if (info.type == BlockType::MathSingle) {
            const int open = int(text.indexOf(QStringLiteral("$$")));
            const int close = int(text.lastIndexOf(QStringLiteral("$$")));
            tex = text.mid(open + 2, close - open - 2);
        } else if (info.type == BlockType::MathOpen) {
            // The block's text comes from the following lines, which aren't highlighted yet.
            QStringList lines{text.mid(info.contentStart)};
            for (QTextBlock b = currentBlock().next(); b.isValid(); b = b.next()) {
                const QString line = b.text();
                const int close = int(line.indexOf(QStringLiteral("$$")));
                lines << (close < 0 ? line : line.left(close));
                if (close >= 0)
                    break;
            }
            tex = lines.join(u'\n');
        }
    }

    if (info.type == BlockType::MathOpen || info.type == BlockType::MathSingle) {
        const MathMetrics metrics = tex.trimmed().isEmpty() ? MathMetrics() : m_mathSize(tex, true);
        if (!metrics.size.isEmpty()) {
            setFormat(0, length, collapsed({}));
            setFormat(0, 1, tallSlot(metrics.size.height() + 2 * imageVerticalPadding));
            data->mathBlock = tex;
            data->mathShown = true;
            return;
        }
    } else if (!active && m_mathSize) {
        // Lines of a block drawn on its first line. A block that can't be drawn keeps its source: then the
        // first line wasn't collapsed, and neither are these.
        const QTextBlock start = mathBlockStart(currentBlock());
        const auto *first = static_cast<const BlockData *>(start.userData());
        if (first && first->mathShown) {
            setFormat(0, length, collapsed({}));
            return;
        }
    }

    setFormat(0, length, source);
    // The $$ delimiters are dimmed like other markers.
    for (int at = int(text.indexOf(QStringLiteral("$$"))); at >= 0; at = int(text.indexOf(QStringLiteral("$$"), at + 2)))
        setFormat(at, 2, dimmed(source));
}

// The text can't change, so the reference keeps its characters: '&' becomes an invisible slot as wide as
// the replacement, for the editor to paint on, and the rest collapses.
void LivePreviewHighlighter::showEntity(const md::EntityRange &entity, int offset)
{
    const int position = offset + entity.start;
    const QTextCharFormat shown = format(position);
    const QFont font = shown.font().resolve(m_theme->baseFont);
    const QString text = QString::fromStdU16String(entity.text);
    const QFontMetricsF metrics(font);

    QTextCharFormat slot = shown;
    slot.setForeground(Qt::transparent);
    slot.setFontUnderline(false);
    slot.setFontStrikeOut(false);
    slot.setFontLetterSpacingType(QFont::AbsoluteSpacing);
    slot.setFontLetterSpacing(metrics.horizontalAdvance(text) - metrics.horizontalAdvance(u'&'));
    setFormat(position, 1, slot);
    setFormat(position + 1, entity.end - entity.start - 1, collapsed(shown));

    if (auto *data = static_cast<BlockData *>(currentBlockUserData())) {
        const QBrush brush = shown.foreground();
        data->entities.push_back({position, text, font, brush.style() == Qt::NoBrush ? QColor() : brush.color()});
    }
}
