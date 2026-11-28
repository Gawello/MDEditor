#pragma once

#include <string>
#include <string_view>

// Obsidian's "Auto-pair brackets" and "Auto-pair Markdown syntax": what typing a character does next to
// the cursor. Brackets and quotes close themselves where that makes sense (not an apostrophe in "don't"),
// a selection gets wrapped, typing the closing character over an existing one steps over it, and Backspace in
// an empty pair removes both.
namespace md {

enum class PairAction {
    None,   // type the character as usual
    Insert, // `open` + `close`, the cursor between them
    Wrap,   // `open` + the selection + `close`, the selection kept
    Skip,   // move over the next character, which is the one typed
};

struct PairDecision {
    PairAction action = PairAction::None;
    char16_t open = 0;
    char16_t close = 0;
};

struct PairOptions {
    bool brackets = true; // () [] {} "" ''
    bool markdown = true; // `` ** __ ~~ == $$ (all but `` only wrap a selection)
};

// `before` and `after` are the line's text on each side of the cursor (or of the selection).
PairDecision pairFor(char16_t typed, std::u16string_view before, std::u16string_view after, bool selection,
                     const PairOptions &options);
// Backspace right after an opening character whose closing one follows: both go.
bool deletesPair(std::u16string_view before, std::u16string_view after, const PairOptions &options);

} // namespace md
