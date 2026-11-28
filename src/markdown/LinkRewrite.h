#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

// Changing where a note's links point (after a file was renamed or moved): each local link is offered to a
// callback, which may give a new target; the "#heading" and "|alias" parts, the link's kind and everything
// else in the text stay as they were. Links in code, math and comments are left alone.
namespace md {

struct LinkToRewrite {
    std::u16string target; // as written, without "#anchor": "Note", "sub/x.md", "../a%20b.md"
    bool wiki = false;
    bool embed = false;
    bool angle = false; // a Markdown link written as (<path with spaces>)
};

// The new target as it is to be written, or nothing to leave the link.
using LinkRewriter = std::function<std::optional<std::u16string>(const LinkToRewrite &link)>;

std::u16string rewriteLinks(std::u16string_view text, const LinkRewriter &rewrite);

} // namespace md
