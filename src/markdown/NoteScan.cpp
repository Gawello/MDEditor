#include "markdown/NoteScan.h"

#include "markdown/InlineParser.h"
#include "markdown/LineStyle.h"

#include <algorithm>

namespace md {

namespace {

bool isSpace(char16_t c) { return c == u' ' || c == u'\t'; }

std::u16string_view trim(std::u16string_view s)
{
    while (!s.empty() && isSpace(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && (isSpace(s.back()) || s.back() == u'\r'))
        s.remove_suffix(1);
    return s;
}

std::u16string_view unquote(std::u16string_view s)
{
    s = trim(s);
    if (s.size() >= 2 && (s.front() == u'"' || s.front() == u'\'') && s.back() == s.front())
        s = s.substr(1, s.size() - 2);
    return s;
}

void addTag(std::vector<std::u16string> &tags, std::u16string_view tag)
{
    while (!tag.empty() && tag.front() == u'#')
        tag.remove_prefix(1);
    if (tag.empty())
        return;
    const std::u16string value(tag);
    if (std::find(tags.begin(), tags.end(), value) == tags.end())
        tags.push_back(value);
}

bool hasScheme(std::u16string_view url)
{
    // "https:", "mailto:"; a one-letter scheme is a Windows drive letter.
    const size_t colon = url.find(u':');
    if (colon == std::u16string_view::npos || colon < 2)
        return false;
    for (size_t i = 0; i < colon; ++i) {
        const char16_t c = url[i];
        if (!((c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'+'
              || c == u'-' || c == u'.'))
            return false;
    }
    return true;
}

} // namespace

std::vector<std::u16string_view> splitLines(std::u16string_view text)
{
    std::vector<std::u16string_view> lines;
    size_t start = 0;
    while (true) {
        const size_t end = text.find(u'\n', start);
        std::u16string_view line = text.substr(start, end == std::u16string_view::npos ? std::u16string_view::npos
                                                                                       : end - start);
        if (!line.empty() && line.back() == u'\r')
            line.remove_suffix(1);
        lines.push_back(line);
        if (end == std::u16string_view::npos)
            break;
        start = end + 1;
    }
    return lines;
}

std::vector<std::u16string> propertyList(std::u16string_view value)
{
    std::vector<std::u16string> items;
    value = trim(value);
    if (value.size() >= 2 && value.front() == u'[' && value.back() == u']')
        value = value.substr(1, value.size() - 2);
    size_t start = 0;
    while (start <= value.size()) {
        size_t end = value.find(u',', start);
        if (end == std::u16string_view::npos)
            end = value.size();
        const std::u16string_view item = unquote(value.substr(start, end - start));
        if (!item.empty())
            items.emplace_back(item);
        start = end + 1;
    }
    return items;
}

std::vector<BlockInfo> parseBlocks(const std::vector<std::u16string_view> &lines)
{
    std::vector<BlockInfo> blocks;
    blocks.reserve(lines.size());
    BlockState state;
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto next = i + 1 < lines.size() ? std::optional(lines[i + 1]) : std::nullopt;
        const LineResult result = parseLine(lines[i], state, next, i == 0);
        blocks.push_back(result.info);
        state = result.next;
    }
    return blocks;
}

NoteScan scanNote(std::u16string_view text)
{
    NoteScan scan;
    const std::vector<std::u16string_view> lines = splitLines(text);
    const std::vector<BlockInfo> blocks = parseBlocks(lines);

    std::u16string listKey; // a frontmatter key whose value is the "- item" lines below it
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::u16string_view line = lines[i];
        const BlockInfo &info = blocks[i];
        const int number = int(i);

        if (info.type == BlockType::FrontmatterClose) {
            scan.frontmatterEnd = number;
            continue;
        }
        if (info.type == BlockType::FrontmatterLine) {
            const std::u16string_view trimmed = trim(line);
            if (!listKey.empty() && trimmed.size() >= 1 && trimmed.front() == u'-'
                && (trimmed.size() == 1 || isSpace(trimmed[1]))) {
                const std::u16string item(unquote(trimmed.substr(1)));
                Property &property = scan.properties.back();
                property.value += (property.value.empty() ? u"" : u", ") + item;
                if (listKey == u"tags" || listKey == u"tag")
                    addTag(scan.tags, item);
                continue;
            }
            listKey.clear();
            const size_t colon = line.find(u':');
            if (colon == std::u16string_view::npos || isSpace(line[0]))
                continue;
            Property property{std::u16string(trim(line.substr(0, colon))), std::u16string(unquote(line.substr(colon + 1))),
                              number};
            if (property.value.empty())
                listKey = property.key;
            if (property.key == u"tags" || property.key == u"tag") {
                for (const std::u16string &tag : propertyList(line.substr(colon + 1))) {
                    // "tags: a b" is a list too
                    size_t start = 0;
                    while (start < tag.size()) {
                        size_t end = tag.find(u' ', start);
                        if (end == std::u16string::npos)
                            end = tag.size();
                        addTag(scan.tags, std::u16string_view(tag).substr(start, end - start));
                        start = end + 1;
                    }
                }
            }
            scan.properties.push_back(std::move(property));
            continue;
        }
        if (info.type == BlockType::FrontmatterOpen || info.type == BlockType::CodeLine
            || info.type == BlockType::FenceOpen || info.type == BlockType::FenceClose || isMathType(info.type)
            || info.type == BlockType::CommentLine || info.type == BlockType::Blank
            || info.type == BlockType::HorizontalRule || info.type == BlockType::TableDelimiter)
            continue;

        const std::u16string_view content = line.substr(std::min<size_t>(info.contentStart, line.size()));
        const InlineResult parsed = parseInline(content);
        if (info.type == BlockType::Heading)
            scan.headings.push_back({number, info.headingLevel, std::u16string(trim(stripInlineFormatting(trim(content))))});

        for (const TagRange &tag : parsed.tags)
            addTag(scan.tags, content.substr(tag.start + 1, tag.end - tag.start - 1));
        for (const LinkRange &link : parsed.links) {
            std::u16string_view target = trim(content.substr(link.urlStart, link.urlEnd - link.urlStart));
            if (target.empty() || hasScheme(target) || (!link.wiki && link.urlStart == link.start))
                continue; // URLs and bare autolinks
            NoteLink item;
            const size_t hash = target.find(u'#');
            if (hash != std::u16string_view::npos) {
                item.anchor = std::u16string(target.substr(hash + 1));
                target = target.substr(0, hash);
            }
            if (target.empty())
                continue; // [[#Heading]]: a link inside this note
            item.target = std::u16string(target);
            item.line = number;
            item.wiki = link.wiki;
            item.embed = link.image;
            scan.links.push_back(std::move(item));
        }
    }
    return scan;
}

int foldEnd(const std::vector<BlockInfo> &blocks, int line)
{
    const int count = int(blocks.size());
    if (line < 0 || line >= count)
        return line;
    const BlockInfo &start = blocks[line];
    int last = line;

    if (start.type == BlockType::Heading) {
        for (int i = line + 1; i < count; ++i) {
            if (blocks[i].type == BlockType::Heading && blocks[i].headingLevel <= start.headingLevel)
                break;
            last = i;
        }
    } else if (start.isList()) {
        for (int i = line + 1; i < count; ++i) {
            const BlockInfo &b = blocks[i];
            if (b.type != BlockType::Blank && (b.indent <= start.indent || isTableType(b.type) || b.type == BlockType::Heading))
                break;
            last = i;
        }
    } else if (start.type == BlockType::Quote && start.calloutStart >= 0) {
        for (int i = line + 1; i < count && blocks[i].type == BlockType::Quote && blocks[i].calloutStart < 0; ++i)
            last = i;
    } else {
        return line;
    }
    while (last > line && blocks[last].type == BlockType::Blank)
        --last;
    return last;
}

} // namespace md
