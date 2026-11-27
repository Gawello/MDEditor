#pragma once

#include <cstdint>
#include <string>
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
    Entity    = 1 << 8, // character reference, shown as what it stands for (see InlineResult::entities)
    Comment   = 1 << 9, // %%Obsidian comment%%
    Math      = 1 << 10, // $inline$ or $$display$$ TeX (see InlineResult::math)
};

struct MathRange {
    int start = 0; // including the dollar signs, [start, end)
    int end = 0;
    int contentStart = 0; // the TeX source, [contentStart, contentEnd)
    int contentEnd = 0;
    bool display = false; // $$...$$
};

struct LinkRange {
    int start = 0; // whole link including its markers, [start, end)
    int end = 0;
    int urlStart = 0; // link target, [urlStart, urlEnd)
    int urlEnd = 0;
    int textStart = 0; // link text / image alt / wiki alias, [textStart, textEnd)
    int textEnd = 0;
    bool image = false; // ![alt](src) or ![[embed]]
    bool wiki = false;  // [[target]] / [[target|alias]]
};

struct EntityRange {
    int start = 0; // the reference, [start, end)
    int end = 0;
    std::u16string text; // what it stands for
};

struct InlineResult {
    std::vector<std::uint16_t> styles; // one entry per UTF-16 code unit
    std::vector<LinkRange> links;
    std::vector<EntityRange> entities; // in text order
    std::vector<MathRange> math;       // in text order
};

InlineResult parseInline(std::u16string_view text);

const LinkRange *linkAt(const InlineResult &result, int pos);

} // namespace md
