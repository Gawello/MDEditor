#include "markdown/AutoPair.h"

namespace md {

namespace {

bool isSpace(char16_t c)
{
    return c == u' ' || c == u'\t' || c == 0x00A0;
}

bool isWordChar(char16_t c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'_'
           || (c >= 0x00C0 && c != 0x00D7 && c != 0x00F7 && !(c >= 0x2000 && c <= 0x206F) && !(c >= 0x3000 && c <= 0x303F));
}

char16_t closerOf(char16_t open)
{
    switch (open) {
    case u'(':
        return u')';
    case u'[':
        return u']';
    case u'{':
        return u'}';
    default:
        return open; // quotes and Markdown markers close with themselves
    }
}

bool isBracketPair(char16_t c)
{
    return c == u'(' || c == u'[' || c == u'{' || c == u'"' || c == u'\'';
}

bool isMarkdownMarker(char16_t c)
{
    return c == u'`' || c == u'*' || c == u'_' || c == u'~' || c == u'=' || c == u'$';
}

// What may follow a freshly opened pair: nothing, a space, or something that ends a phrase.
bool closesHere(std::u16string_view after)
{
    if (after.empty())
        return true;
    const char16_t c = after.front();
    return isSpace(c) || c == u')' || c == u']' || c == u'}' || c == u',' || c == u'.' || c == u';' || c == u':'
           || c == u'!' || c == u'?' || c == u'"' || c == u'\'' || c == u'`';
}

} // namespace

PairDecision pairFor(char16_t typed, std::u16string_view before, std::u16string_view after, bool selection,
                     const PairOptions &options)
{
    const bool bracket = options.brackets && isBracketPair(typed);
    const bool marker = options.markdown && isMarkdownMarker(typed);
    const bool closingBracket = options.brackets && (typed == u')' || typed == u']' || typed == u'}');

    if (selection) {
        if (bracket || marker)
            return {PairAction::Wrap, typed, closerOf(typed)};
        return {};
    }
    // Typing the closing character that's already there (typically the one added with the opening one).
    if (!after.empty() && after.front() == typed) {
        if (closingBracket)
            return {PairAction::Skip, 0, typed};
        const bool quote = (options.brackets && (typed == u'"' || typed == u'\'')) || (options.markdown && typed == u'`');
        if (quote && !before.empty() && !isSpace(before.back()))
            return {PairAction::Skip, 0, typed};
    }
    if (options.brackets && (typed == u'(' || typed == u'[' || typed == u'{')) {
        if (closesHere(after))
            return {PairAction::Insert, typed, closerOf(typed)};
        return {};
    }
    // Quotes and code spans: only at the start of a word, so "don't" and 5' stay single.
    const bool quote = (options.brackets && (typed == u'"' || typed == u'\'')) || (options.markdown && typed == u'`');
    if (quote) {
        const bool startOfWord = before.empty() || isSpace(before.back()) || before.back() == u'(' || before.back() == u'['
                                 || before.back() == u'{';
        if (startOfWord && (after.empty() || isSpace(after.front()) || closesHere(after)) && !(!before.empty() && isWordChar(before.back())))
            return {PairAction::Insert, typed, typed};
    }
    return {};
}

bool deletesPair(std::u16string_view before, std::u16string_view after, const PairOptions &options)
{
    if (before.empty() || after.empty())
        return false;
    const char16_t open = before.back();
    const bool pairs = (options.brackets && isBracketPair(open)) || (options.markdown && open == u'`');
    return pairs && after.front() == closerOf(open);
}

} // namespace md
