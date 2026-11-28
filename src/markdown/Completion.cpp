#include "markdown/Completion.h"

#include "markdown/InlineParser.h"

namespace md {

namespace {

bool isSpace(char16_t c)
{
    return c == u' ' || c == u'\t' || c == 0x00A0 || c == 0x3000;
}

// The characters a tag may contain (InlineParser's rule).
bool tagChar(char16_t c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'_' || c == u'-'
           || c == u'/' || (c >= 0x80 && c != 0x00A0 && c != 0x3000 && !(c >= 0x2000 && c <= 0x206F)
                            && !(c >= 0x3000 && c <= 0x303F));
}

bool inInlineCode(std::u16string_view line, int cursor)
{
    const InlineResult parsed = parseInline(line);
    // The character before the cursor decides: "`x|`" is inside, "`x`|" is not.
    return cursor > 0 && cursor <= int(parsed.styles.size()) && (parsed.styles[cursor - 1] & Code);
}

} // namespace

CompletionContext completionAt(std::u16string_view line, int cursor)
{
    CompletionContext context;
    if (cursor <= 0 || cursor > int(line.size()))
        return context;
    const std::u16string_view before = line.substr(0, cursor);

    // An open "[[" with no "]]" between it and the cursor.
    const size_t open = before.rfind(u"[[");
    if (open != std::u16string_view::npos && before.find(u"]]", open) == std::u16string_view::npos) {
        if (inInlineCode(line, cursor))
            return context;
        const std::u16string_view inside = before.substr(open + 2);
        if (inside.find(u'|') != std::u16string_view::npos || inside.find(u'[') != std::u16string_view::npos)
            return context; // the display text, or something else entirely
        context.closed = line.substr(cursor, 2) == u"]]";
        const size_t hash = inside.find(u'#');
        if (hash == std::u16string_view::npos) {
            context.kind = CompletionKind::Note;
            context.start = int(open + 2);
            context.query = std::u16string(inside);
            return context;
        }
        context.note = std::u16string(inside.substr(0, hash));
        std::u16string_view rest = inside.substr(hash + 1);
        if (!rest.empty() && rest.front() == u'^') {
            context.kind = CompletionKind::Block;
            context.start = int(open + 2 + hash + 2);
            context.query = std::u16string(rest.substr(1));
        } else {
            context.kind = CompletionKind::Heading;
            context.start = int(open + 2 + hash + 1);
            context.query = std::u16string(rest);
        }
        return context;
    }

    // "/command": a '/' at the start of the line or after a space, then anything but spaces and slashes (so
    // paths and "and/or" don't count).
    {
        int slash = cursor;
        while (slash > 0 && !isSpace(before[slash - 1]) && before[slash - 1] != u'/')
            --slash;
        if (slash > 0 && before[slash - 1] == u'/' && (slash == 1 || isSpace(before[slash - 2]))) {
            if (inInlineCode(line, cursor))
                return context;
            context.kind = CompletionKind::Command;
            context.start = slash;
            context.query = std::u16string(before.substr(slash));
            return context;
        }
    }

    // "#tag": a '#' at the start of the line or after a space, then tag characters up to the cursor. At least
    // one of them, so typing a heading ("# ") doesn't flash a list.
    int start = cursor;
    while (start > 0 && tagChar(before[start - 1]))
        --start;
    if (start == cursor || start == 0 || before[start - 1] != u'#')
        return context;
    const int hash = start - 1;
    if (hash > 0 && !isSpace(before[hash - 1]))
        return context;
    if (inInlineCode(line, cursor))
        return context;
    context.kind = CompletionKind::Tag;
    context.start = start;
    context.query = std::u16string(before.substr(start));
    return context;
}

static CompletionContext propertyContext(std::u16string_view line, int cursor, std::u16string_view listKey);

CompletionContext propertyCompletionAt(std::u16string_view line, int cursor, std::u16string_view listKey)
{
    CompletionContext context = propertyContext(line, cursor, listKey);
    // Only once something is typed: an empty line or "key:" followed by Enter must stay a line break.
    if (context.query.empty())
        return {};
    return context;
}

static CompletionContext propertyContext(std::u16string_view line, int cursor, std::u16string_view listKey)
{
    CompletionContext context;
    if (cursor < 0 || cursor > int(line.size()))
        return context;
    const std::u16string_view before = line.substr(0, size_t(cursor));
    const auto trimmedStart = [](std::u16string_view text) {
        size_t i = 0;
        while (i < text.size() && isSpace(text[i]))
            ++i;
        return i;
    };
    // "  - item" in a list under a key
    const size_t indent = trimmedStart(before);
    if (indent < before.size() && before[indent] == u'-' && indent + 1 < before.size() && isSpace(before[indent + 1])) {
        if (listKey.empty())
            return context;
        const size_t start = indent + 1 + trimmedStart(before.substr(indent + 1));
        context.kind = CompletionKind::PropertyValue;
        context.note = std::u16string(listKey);
        context.start = int(start);
        context.query = std::u16string(before.substr(start));
        return context;
    }
    const size_t colon = before.find(u':');
    if (colon == std::u16string_view::npos) { // the key, from the start of the line
        if (indent != 0 || before.find(u' ') != std::u16string_view::npos)
            return context;
        context.kind = CompletionKind::PropertyKey;
        context.start = 0;
        context.query = std::u16string(before);
        context.closed = line.substr(size_t(cursor)).find(u':') != std::u16string_view::npos;
        return context;
    }
    // "key: value", "key: [a, b"
    std::u16string_view key = before.substr(0, colon);
    while (!key.empty() && isSpace(key.back()))
        key.remove_suffix(1);
    if (key.empty() || isSpace(line[0]))
        return context;
    size_t start = colon + 1;
    start += trimmedStart(before.substr(start));
    // An inline list, "[a, b": the item being typed (not a "[[link]]" value).
    if (start < before.size() && before[start] == u'[' && before.substr(start, 2) != u"[[") {
        const size_t item = before.find_last_of(u"[,");
        start = item + 1;
        start += trimmedStart(before.substr(start));
    }
    if (start < before.size() && (before[start] == u'"' || before[start] == u'\''))
        ++start;
    context.kind = CompletionKind::PropertyValue;
    context.note = std::u16string(key);
    context.start = int(start);
    context.query = std::u16string(before.substr(start));
    return context;
}

} // namespace md
