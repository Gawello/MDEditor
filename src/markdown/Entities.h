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

// How to spell `symbol` with character references: "&rarr;" if it has a name, otherwise each code point by
// name or number ("&#x1F680;"). ASCII without a name stays as it is. matchEntity undoes this.
std::u16string entityFor(std::u16string_view symbol);

} // namespace md
