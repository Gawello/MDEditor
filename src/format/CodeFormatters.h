#pragma once

#include <string>
#include <string_view>
#include <vector>

// Built-in, dependency-free code formatters ("prettify"). Input and output are UTF-16 so they
// map 1:1 onto QString. None of them reflow code; they fix indentation and spacing.
namespace fmt {

struct Result {
    std::u16string text;
    bool ok = true;
    std::string error;    // ASCII description when !ok
    int errorOffset = -1; // position in the input, if known
};

// Pretty-prints (or minifies) JSON, keeping key order and string contents intact.
Result formatJson(std::u16string_view input, int indent, bool minify = false);

// One declaration per line, nested blocks indented, "prop: value" spacing. Comments are kept.
Result formatCss(std::u16string_view input, int indent);

// Re-indents brace languages (C, C++, Java, JavaScript, C#, Go, ...) by bracket depth.
Result formatCLike(std::u16string_view input, int indent);

// Normalizes Python indentation to `indent` spaces per level, keeping the block structure exactly.
Result formatPython(std::u16string_view input, int indent);

// Aligns the columns of a Markdown table given as its lines (header, delimiter row, body rows).
std::vector<std::u16string> formatTable(const std::vector<std::u16string> &lines);

} // namespace fmt
