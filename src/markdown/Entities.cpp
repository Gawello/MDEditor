#include "markdown/Entities.h"

#include <algorithm>
#include <iterator>

namespace md {

namespace {

struct NamedEntity {
    std::u16string_view name;
    std::u16string_view text;
};

constexpr NamedEntity namedEntities[] = {
#include "markdown/EntityTable.inc"
};

struct EntityName {
    std::u16string_view text;
    std::u16string_view name;
};

constexpr EntityName entityNames[] = {
#include "markdown/EntityNames.inc"
};

constexpr int maxDecimalDigits = 7;
constexpr int maxHexDigits = 6;
constexpr int maxNameLength = 32;

bool isAsciiAlnum(char16_t c)
{
    return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
}

int digitValue(char16_t c)
{
    if (c >= u'0' && c <= u'9')
        return c - u'0';
    if (c >= u'a' && c <= u'f')
        return c - u'a' + 10;
    if (c >= u'A' && c <= u'F')
        return c - u'A' + 10;
    return 99;
}

std::u16string encode(char32_t code)
{
    if (code == 0 || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
        code = 0xFFFD;
    if (code < 0x10000)
        return std::u16string(1, char16_t(code));
    code -= 0x10000;
    return {char16_t(0xD800 + (code >> 10)), char16_t(0xDC00 + (code & 0x3FF))};
}

std::optional<EntityMatch> numeric(std::u16string_view text, int at)
{
    const int size = int(text.size());
    int i = at + 2; // past "&#"
    int base = 10;
    int maxDigits = maxDecimalDigits;
    if (i < size && (text[i] == u'x' || text[i] == u'X')) {
        base = 16;
        maxDigits = maxHexDigits;
        ++i;
    }
    const int first = i;
    char32_t code = 0;
    while (i < size && digitValue(text[i]) < base) {
        if (i - first == maxDigits)
            return std::nullopt;
        code = code * base + char32_t(digitValue(text[i]));
        ++i;
    }
    if (i == first || i >= size || text[i] != u';')
        return std::nullopt;
    return EntityMatch{i + 1 - at, encode(code)};
}

std::optional<EntityMatch> named(std::u16string_view text, int at)
{
    const int size = int(text.size());
    int i = at + 1;
    while (i < size && i - at <= maxNameLength && isAsciiAlnum(text[i]))
        ++i;
    if (i == at + 1 || i >= size || text[i] != u';')
        return std::nullopt;

    const std::u16string_view name = text.substr(at + 1, i - at - 1);
    const auto found = std::lower_bound(std::begin(namedEntities), std::end(namedEntities), name,
                                        [](const NamedEntity &entity, std::u16string_view value) {
                                            return entity.name < value;
                                        });
    if (found == std::end(namedEntities) || found->name != name)
        return std::nullopt;
    return EntityMatch{i + 1 - at, std::u16string(found->text)};
}

std::u16string nameFor(std::u16string_view text)
{
    const auto found = std::lower_bound(std::begin(entityNames), std::end(entityNames), text,
                                        [](const EntityName &entity, std::u16string_view value) {
                                            return entity.text < value;
                                        });
    if (found == std::end(entityNames) || found->text != text)
        return {};
    return u"&" + std::u16string(found->name) + u";";
}

std::u16string hex(char32_t code)
{
    static constexpr char16_t digits[] = u"0123456789ABCDEF";
    std::u16string out;
    for (; code != 0; code >>= 4)
        out.insert(out.begin(), digits[code & 0xF]);
    return out;
}

} // namespace

std::optional<EntityMatch> matchEntity(std::u16string_view text, int at)
{
    if (at < 0 || at + 1 >= int(text.size()) || text[at] != u'&')
        return std::nullopt;
    return text[at + 1] == u'#' ? numeric(text, at) : named(text, at);
}

std::u16string entityFor(std::u16string_view symbol)
{
    if (std::u16string whole = nameFor(symbol); !whole.empty())
        return whole;

    std::u16string out;
    for (size_t i = 0; i < symbol.size(); ++i) {
        const size_t start = i;
        char32_t code = symbol[i];
        if (code >= 0xD800 && code <= 0xDBFF && i + 1 < symbol.size()) {
            code = 0x10000 + ((code - 0xD800) << 10) + (symbol[i + 1] - 0xDC00);
            ++i;
        }
        const std::u16string_view character = symbol.substr(start, i + 1 - start);
        if (std::u16string name = nameFor(character); !name.empty())
            out += name;
        else if (code < 0x80)
            out += character;
        else
            out += u"&#x" + hex(code) + u";";
    }
    return out;
}

} // namespace md
