#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace md {

struct EntityMatch {
    int length = 0;      // of the reference, from '&' to ';' inclusive
    std::u16string text; // what it stands for
};

// The character reference starting at text[at] ("&rarr;", "&#8594;", "&#x2192;"), if it is a valid one
// by CommonMark's rules. Numeric references to invalid code points give U+FFFD.
std::optional<EntityMatch> matchEntity(std::u16string_view text, int at);

} // namespace md
