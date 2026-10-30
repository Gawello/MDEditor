#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace md {

// Per-character style flags. A character may carry several of them at once.
enum Style : std::uint16_t {
    Marker    = 1 << 0, // Markdown syntax (hidden outside of the line being edited)
    Bold      = 1 << 1,
    Italic    = 1 << 2,
    Strike    = 1 << 3,
    Code      = 1 << 4,
    Link      = 1 << 5,
    LinkUrl   = 1 << 6,
    Highlight = 1 << 7,
};

struct LinkRange {
    int start = 0; // whole link including its markers, [start, end)
    int end = 0;
    int urlStart = 0; // link target, [urlStart, urlEnd)
    int urlEnd = 0;
};

struct InlineResult {
    std::vector<std::uint16_t> styles; // one entry per UTF-16 code unit
    std::vector<LinkRange> links;
};

InlineResult parseInline(std::u16string_view text);

const LinkRange *linkAt(const InlineResult &result, int pos);

} // namespace md
