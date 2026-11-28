#pragma once

#include <string>
#include <string_view>

// What is being typed at the cursor that the editor can complete, like Obsidian's suggestions: a note name
// after "[[" (or "![["), a heading after "[[Note#", a block after "[[Note#^", a tag after "#", a command after
// "/" at the start of the line or after a space (Obsidian's slash commands).
namespace md {

enum class CompletionKind { None, Note, Heading, Block, Tag, Command, PropertyKey, PropertyValue };

struct CompletionContext {
    CompletionKind kind = CompletionKind::None;
    int start = 0;          // where the typed query starts in the line; the completion replaces [start, cursor)
    std::u16string query;   // what was typed so far
    std::u16string note;    // Heading / Block: the note before '#' (empty: this note); PropertyValue: the key
    bool closed = false;    // "]]" already follows the cursor
};

// `line` is the whole line, `cursor` the position in it. Text inside inline code isn't completed; code blocks
// are the caller's to skip.
CompletionContext completionAt(std::u16string_view line, int cursor);

// The same for a line of the front matter: a property name being typed at the start of the line, a value after
// "key: " (in "[a, b" lists, the item being typed), or a "  - item" under `listKey` (the key of the nearest
// "key:" line above with nothing after its colon).
CompletionContext propertyCompletionAt(std::u16string_view line, int cursor, std::u16string_view listKey);

} // namespace md
