#include "markdown/LinkRewrite.h"

#include "markdown/BlockParser.h"
#include "markdown/InlineParser.h"
#include "markdown/NoteScan.h"

#include <vector>

namespace md {

namespace {

bool hasScheme(std::u16string_view target)
{
    // "https:", "mailto:" (not "C:" drive letters)
    const size_t colon = target.find(u':');
    if (colon == std::u16string_view::npos || colon < 2)
        return false;
    for (size_t i = 0; i < colon; ++i) {
        const char16_t c = target[i];
        if (!((c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'+'
              || c == u'-' || c == u'.'))
            return false;
    }
    return true;
}

bool skipped(BlockType type)
{
    return type == BlockType::CodeLine || type == BlockType::FenceOpen || type == BlockType::FenceClose
           || isMathType(type) || type == BlockType::CommentLine || type == BlockType::FrontmatterOpen
           || type == BlockType::FrontmatterClose || type == BlockType::Blank;
}

} // namespace

std::u16string rewriteLinks(std::u16string_view text, const LinkRewriter &rewrite)
{
    // Line by line with their exact offsets, so everything outside the links is copied as it is.
    std::vector<std::u16string_view> lines;
    std::vector<size_t> starts;
    size_t start = 0;
    while (true) {
        const size_t end = text.find(u'\n', start);
        std::u16string_view line = text.substr(start, end == std::u16string_view::npos ? std::u16string_view::npos : end - start);
        if (!line.empty() && line.back() == u'\r')
            line.remove_suffix(1);
        lines.push_back(line);
        starts.push_back(start);
        if (end == std::u16string_view::npos)
            break;
        start = end + 1;
    }
    const std::vector<BlockInfo> blocks = parseBlocks(lines);

    struct Edit {
        size_t from;
        size_t length;
        std::u16string text;
    };
    std::vector<Edit> edits;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (skipped(blocks[i].type))
            continue;
        const size_t contentStart = std::min<size_t>(blocks[i].contentStart, lines[i].size());
        const std::u16string_view content = lines[i].substr(contentStart);
        const InlineResult parsed = parseInline(content);
        for (const LinkRange &range : parsed.links) {
            const std::u16string_view url = content.substr(size_t(range.urlStart), size_t(range.urlEnd - range.urlStart));
            if (url.empty() || hasScheme(url) || (!range.wiki && range.urlStart == range.start))
                continue; // URLs and bare autolinks
            const size_t hash = url.find(u'#');
            const std::u16string_view target = url.substr(0, hash);
            if (target.empty())
                continue; // [[#Heading]]: this note
            LinkToRewrite link{std::u16string(target), range.wiki, range.image,
                               !range.wiki && range.urlStart > 0 && content[size_t(range.urlStart) - 1] == u'<'};
            const std::optional<std::u16string> replacement = rewrite(link);
            if (!replacement || *replacement == link.target)
                continue;
            edits.push_back({starts[i] + contentStart + size_t(range.urlStart), target.size(), *replacement});
        }
    }

    std::u16string result;
    result.reserve(text.size() + 64);
    size_t done = 0;
    for (const Edit &edit : edits) {
        result.append(text.substr(done, edit.from - done));
        result.append(edit.text);
        done = edit.from + edit.length;
    }
    result.append(text.substr(done));
    return result;
}

} // namespace md
