#include "markdown/LineStyle.h"

#include "markdown/BlockParser.h"
#include "markdown/InlineParser.h"

#include <algorithm>

namespace md {

namespace {

// A quoted line split into its quote markers ("> > ") and what they quote.
struct Quoted {
    std::u16string_view prefix;
    std::u16string_view inner;
};

Quoted splitQuote(std::u16string_view line)
{
    const BlockInfo info = parseLine(line, {}).info;
    if (info.type != BlockType::Quote)
        return {{}, line};
    return {line.substr(0, info.contentStart), line.substr(info.contentStart)};
}

LineStyle styleOf(const BlockInfo &info)
{
    switch (info.type) {
    case BlockType::Heading:
        return LineStyle::Heading;
    case BlockType::BulletItem:
        return LineStyle::Bullet;
    case BlockType::OrderedItem:
        return LineStyle::Numbered;
    case BlockType::TaskItem:
        return LineStyle::Task;
    case BlockType::Quote:
        return LineStyle::Quote;
    default:
        return LineStyle::Body;
    }
}

bool hasStyle(std::u16string_view line, LineStyle style, int level)
{
    const std::u16string_view text = style == LineStyle::Quote ? line : splitQuote(line).inner;
    int actual = 0;
    return lineStyleOf(text, &actual) == style && (style != LineStyle::Heading || actual == level);
}

// `line` without a quote, restyled. Lists keep their indentation (nesting); headings and body text lose it.
std::u16string restyle(std::u16string_view line, LineStyle style, int level, int number)
{
    const BlockInfo info = parseLine(line, {}).info;
    const std::u16string indent(line.substr(0, info.indent));
    const std::u16string_view content = info.type == BlockType::Paragraph || info.type == BlockType::Blank
        ? line.substr(info.indent)
        : line.substr(info.contentStart);

    switch (style) {
    case LineStyle::Body:
    case LineStyle::Quote:
        return std::u16string(content);
    case LineStyle::Heading:
        return std::u16string(std::clamp(level, 1, 6), u'#') + u' ' + std::u16string(content);
    case LineStyle::Bullet:
        return indent + u"- " + std::u16string(content);
    case LineStyle::Numbered: {
        std::u16string digits;
        for (const char c : std::to_string(number))
            digits += char16_t(c);
        return indent + digits + u". " + std::u16string(content);
    }
    case LineStyle::Task:
        return indent + u"- [ ] " + std::u16string(content);
    }
    return std::u16string(line);
}

std::u16string removeQuoteLevel(std::u16string_view line)
{
    size_t i = 0;
    while (i < line.size() && (line[i] == u' ' || line[i] == u'\t'))
        ++i;
    if (i == line.size() || line[i] != u'>')
        return std::u16string(line);
    size_t end = i + 1;
    if (end < line.size() && line[end] == u' ')
        ++end;
    return std::u16string(line.substr(0, i)) + std::u16string(line.substr(end));
}

bool isBlankLine(std::u16string_view line)
{
    return std::all_of(line.begin(), line.end(), [](char16_t c) { return c == u' ' || c == u'\t'; });
}

} // namespace

LineStyle lineStyleOf(std::u16string_view line, int *level)
{
    const BlockInfo info = parseLine(line, {}).info;
    if (level)
        *level = info.headingLevel;
    return styleOf(info);
}

std::vector<std::u16string> toggleLineStyle(const std::vector<std::u16string> &lines, LineStyle style, int level)
{
    // Only text lines take part: fenced code, rules and tables keep their meaning.
    std::vector<bool> text(lines.size(), false);
    BlockState state;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::optional<std::u16string_view> next =
            i + 1 < lines.size() ? std::optional<std::u16string_view>(lines[i + 1]) : std::nullopt;
        const LineResult parsed = parseLine(lines[i], state, next);
        state = parsed.next;
        const BlockType type = parsed.info.type;
        text[i] = !isTableType(type) && type != BlockType::FenceOpen && type != BlockType::FenceClose
            && type != BlockType::CodeLine && type != BlockType::HorizontalRule;
    }

    // Blank lines between paragraphs stay blank, unless there is nothing else (a new, empty heading or item).
    bool anyText = false;
    for (size_t i = 0; i < lines.size(); ++i)
        anyText = anyText || (text[i] && !isBlankLine(lines[i]));
    std::vector<bool> affected(lines.size(), false);
    bool already = style != LineStyle::Body;
    bool any = false;
    for (size_t i = 0; i < lines.size(); ++i) {
        affected[i] = text[i] && (!anyText || !isBlankLine(lines[i]));
        if (affected[i]) {
            any = true;
            already = already && hasStyle(lines[i], style, level);
        }
    }

    std::vector<std::u16string> result = lines;
    if (!any)
        return result;
    int number = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (!affected[i])
            continue;
        const std::u16string_view line = lines[i];
        if (style == LineStyle::Quote) {
            result[i] = already ? removeQuoteLevel(line) : u"> " + std::u16string(line);
            continue;
        }
        const Quoted quoted = splitQuote(line);
        const LineStyle target = already ? LineStyle::Body : style;
        result[i] = std::u16string(quoted.prefix) + restyle(quoted.inner, target, level, ++number);
    }
    return result;
}

std::u16string stripInlineFormatting(std::u16string_view text)
{
    const InlineResult parsed = parseInline(text);
    std::u16string result;
    result.reserve(text.size());
    for (int i = 0; i < int(text.size()); ++i) {
        const bool inLink = std::any_of(parsed.links.begin(), parsed.links.end(),
                                        [i](const LinkRange &link) { return i >= link.start && i < link.end; });
        if (!(parsed.styles[i] & Marker) || inLink || text[i] == u'\\')
            result += text[i];
    }
    return result;
}

} // namespace md
