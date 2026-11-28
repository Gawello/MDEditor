#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace md {

// What a line is, as far as the editor's Paragraph menu is concerned.
enum class LineStyle { Body, Heading, Bullet, Numbered, Task, Quote };

// The style of `line` on its own (no fence or table context). `level` receives a heading's level.
LineStyle lineStyleOf(std::u16string_view line, int *level = nullptr);

// Gives each line the style, like Obsidian's Paragraph menu: when every (non-blank) line already has it,
// the style is removed instead. Headings take `level`; numbered items count up from 1. A quote wraps the
// whole line, so a quoted list item stays a list item; other styles go inside an existing quote. Lines
// that aren't text (rules, fences, table rows) are left alone.
std::vector<std::u16string> toggleLineStyle(const std::vector<std::u16string> &lines, LineStyle style, int level = 0);

// Obsidian's "Toggle checkbox status" (Ctrl+L): a task is checked or unchecked, a list item or a plain line
// becomes an unchecked task. Headings, quotes, rules and the like are returned as they are.
std::u16string toggleCheckbox(std::u16string_view line);
// "Cycle bullet/checkbox": plain line -> bullet -> task -> plain line.
std::u16string cycleListItem(std::u16string_view line);

// Removes inline formatting markers (**, *, ~~, ==, `) from `text`. Links, images, character references and
// backslash escapes are kept, since dropping them would change what the text says.
std::u16string stripInlineFormatting(std::u16string_view text);

} // namespace md
