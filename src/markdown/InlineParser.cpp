#include "markdown/InlineParser.h"

namespace md {

namespace {

bool isWs(char16_t c) { return c == u' ' || c == u'\t' || c == 0x00A0; }

bool isAsciiPunct(char16_t c)
{
    return (c >= u'!' && c <= u'/') || (c >= u':' && c <= u'@') || (c >= u'[' && c <= u'`')
        || (c >= u'{' && c <= u'~');
}

bool isAsciiAlnum(char16_t c)
{
    return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
}

bool startsWith(std::u16string_view s, int at, std::u16string_view prefix)
{
    return s.substr(at).substr(0, prefix.size()) == prefix;
}

class Parser {
public:
    explicit Parser(std::u16string_view text)
        : s(text)
        , n(int(text.size()))
        , locked(text.size(), false)
    {
        r.styles.assign(text.size(), 0);
    }

    InlineResult run()
    {
        codeSpansAndEscapes();
        links();
        autolinks();
        emphasis();
        return std::move(r);
    }

private:
    void mark(int from, int to, std::uint16_t style, bool lock)
    {
        for (int i = from; i < to; ++i) {
            r.styles[i] |= style;
            if (lock)
                locked[i] = true;
        }
    }

    int runLength(int from, char16_t c) const
    {
        int i = from;
        while (i < n && s[i] == c && !locked[i])
            ++i;
        return i - from;
    }

    // Code spans take precedence over everything else; backslash escapes make the
    // next punctuation character literal.
    void codeSpansAndEscapes()
    {
        int i = 0;
        while (i < n) {
            const char16_t c = s[i];
            if (c == u'\\' && i + 1 < n && isAsciiPunct(s[i + 1])) {
                mark(i, i + 1, Marker, true);
                locked[i + 1] = true;
                i += 2;
                continue;
            }
            if (c == u'`') {
                const int len = runLength(i, u'`');
                int j = i + len;
                int close = -1;
                while (j < n) {
                    if (s[j] == u'`') {
                        const int closeLen = runLength(j, u'`');
                        if (closeLen == len) {
                            close = j;
                            break;
                        }
                        j += closeLen;
                    } else {
                        ++j;
                    }
                }
                if (close >= 0) {
                    mark(i, i + len, Marker | Code, true);
                    mark(i + len, close, Code, true);
                    mark(close, close + len, Marker | Code, true);
                    i = close + len;
                } else {
                    mark(i, i + len, 0, true); // unmatched backticks are literal
                    i += len;
                }
                continue;
            }
            ++i;
        }
    }

    bool wikiLink(int i, int &end)
    {
        if (!(s[i] == u'[' && i + 1 < n && s[i + 1] == u'[' && !locked[i + 1]))
            return false;
        int close = -1;
        for (int j = i + 2; j + 1 < n; ++j) {
            if (locked[j])
                return false;
            if (s[j] == u']' && s[j + 1] == u']') {
                close = j;
                break;
            }
        }
        if (close < 0 || close == i + 2)
            return false;

        int pipe = -1;
        for (int j = i + 2; j < close; ++j) {
            if (s[j] == u'|') {
                pipe = j;
                break;
            }
        }
        mark(i, i + 2, Marker, true);
        mark(close, close + 2, Marker, true);
        if (pipe >= 0) {
            mark(i + 2, pipe + 1, Marker | LinkUrl, true);
            mark(pipe + 1, close, Link, true);
            r.links.push_back({i, close + 2, i + 2, pipe});
        } else {
            mark(i + 2, close, Link, true);
            r.links.push_back({i, close + 2, i + 2, close});
        }
        end = close + 2;
        return true;
    }

    // [text](url) and ![alt](src)
    bool inlineLink(int i, int &end)
    {
        const bool image = s[i] == u'!';
        const int open = image ? i + 1 : i;
        if (open >= n || s[open] != u'[' || locked[open])
            return false;

        int depth = 0;
        int close = -1;
        for (int j = open + 1; j < n; ++j) {
            if (locked[j])
                continue;
            if (s[j] == u'[') {
                ++depth;
            } else if (s[j] == u']') {
                if (depth == 0) {
                    close = j;
                    break;
                }
                --depth;
            }
        }
        if (close < 0 || close + 1 >= n || s[close + 1] != u'(' || locked[close + 1])
            return false;

        int parens = 0;
        int paren = -1;
        for (int j = close + 2; j < n; ++j) {
            if (locked[j])
                return false;
            if (s[j] == u'(') {
                ++parens;
            } else if (s[j] == u')') {
                if (parens == 0) {
                    paren = j;
                    break;
                }
                --parens;
            }
        }
        if (paren < 0)
            return false;

        int urlStart = close + 2;
        int urlEnd = paren;
        while (urlStart < urlEnd && isWs(s[urlStart]))
            ++urlStart;
        while (urlEnd > urlStart && isWs(s[urlEnd - 1]))
            --urlEnd;
        // Drop an optional title: [text](url "title")
        for (int j = urlStart; j < urlEnd; ++j) {
            if (isWs(s[j])) {
                urlEnd = j;
                break;
            }
        }
        if (urlStart < urlEnd && s[urlStart] == u'<' && s[urlEnd - 1] == u'>') {
            ++urlStart;
            --urlEnd;
        }

        mark(i, open + 1, Marker, true);
        mark(open + 1, close, Link, false);
        mark(close, close + 2, Marker, true);
        mark(close + 2, paren, Marker | LinkUrl, true);
        mark(paren, paren + 1, Marker, true);
        r.links.push_back({i, paren + 1, urlStart, urlEnd});
        end = paren + 1;
        return true;
    }

    void links()
    {
        int i = 0;
        while (i < n) {
            int end = 0;
            if (!locked[i] && (s[i] == u'[' || s[i] == u'!')
                && (wikiLink(i, end) || inlineLink(i, end))) {
                i = end;
                continue;
            }
            ++i;
        }
    }

    void autolinks()
    {
        int i = 0;
        while (i < n) {
            if (locked[i] || (i > 0 && isAsciiAlnum(s[i - 1]))
                || !(startsWith(s, i, u"http://") || startsWith(s, i, u"https://"))) {
                ++i;
                continue;
            }
            int end = i;
            while (end < n && !locked[end] && !isWs(s[end]) && s[end] != u'<')
                ++end;
            while (end > i && std::u16string_view(u".,;:!?\"')*_~").find(s[end - 1]) != std::u16string_view::npos)
                --end;
            const int prefix = startsWith(s, i, u"https://") ? 8 : 7;
            if (end - i > prefix) {
                mark(i, end, Link, true);
                r.links.push_back({i, end, i, end});
            }
            i = end > i ? end : i + 1;
        }
    }

    struct Delimiter {
        int pos;
        int length;
        char16_t ch;
        bool canOpen;
        bool canClose;
        int remaining;  // characters not yet matched
        int unmatchedStart; // first character not yet matched (closers consume from the left)
    };

    // Simplified CommonMark delimiter-run algorithm for *, _, ~~ and ==.
    void emphasis()
    {
        std::vector<Delimiter> delims;
        int i = 0;
        while (i < n) {
            const char16_t c = s[i];
            if (locked[i] || (c != u'*' && c != u'_' && c != u'~' && c != u'=')) {
                ++i;
                continue;
            }
            const int len = runLength(i, c);
            const char16_t prev = i > 0 ? s[i - 1] : u' ';
            const char16_t next = i + len < n ? s[i + len] : u' ';
            const bool left = !isWs(next) && (!isAsciiPunct(next) || isWs(prev) || isAsciiPunct(prev));
            const bool right = !isWs(prev) && (!isAsciiPunct(prev) || isWs(next) || isAsciiPunct(next));

            bool canOpen = left;
            bool canClose = right;
            if (c == u'_') {
                canOpen = left && (!right || isAsciiPunct(prev));
                canClose = right && (!left || isAsciiPunct(next));
            }
            if ((c == u'~' || c == u'=') && len != 2)
                canOpen = canClose = false;

            if (canOpen || canClose)
                delims.push_back({i, len, c, canOpen, canClose, len, i});
            i += len;
        }

        std::vector<int> openers;
        for (int ci = 0; ci < int(delims.size()); ++ci) {
            Delimiter &closer = delims[ci];
            if (closer.canClose) {
                while (closer.remaining > 0) {
                    int found = -1;
                    for (int si = int(openers.size()) - 1; si >= 0; --si) {
                        const Delimiter &op = delims[openers[si]];
                        if (op.ch != closer.ch || op.remaining == 0)
                            continue;
                        const bool emphasisChar = op.ch == u'*' || op.ch == u'_';
                        if (emphasisChar && (op.canClose || closer.canOpen)
                            && (op.length + closer.length) % 3 == 0
                            && !(op.length % 3 == 0 && closer.length % 3 == 0))
                            continue;
                        found = si;
                        break;
                    }
                    if (found < 0)
                        break;

                    Delimiter &op = delims[openers[found]];
                    int use;
                    std::uint16_t style;
                    if (op.ch == u'~') {
                        use = 2;
                        style = Strike;
                    } else if (op.ch == u'=') {
                        use = 2;
                        style = Highlight;
                    } else {
                        use = (op.remaining >= 2 && closer.remaining >= 2) ? 2 : 1;
                        style = use == 2 ? Bold : Italic;
                    }

                    const int contentStart = op.unmatchedStart + op.remaining; // openers consume from the right
                    const int contentEnd = closer.unmatchedStart;
                    mark(contentStart - use, contentStart, Marker, false);
                    mark(contentEnd, contentEnd + use, Marker, false);
                    mark(contentStart, contentEnd, style, false);

                    op.remaining -= use;
                    closer.remaining -= use;
                    closer.unmatchedStart += use;

                    openers.resize(found + 1); // delimiters between opener and closer can't match anymore
                    if (op.remaining == 0)
                        openers.pop_back();
                }
            }
            if (closer.remaining > 0 && closer.canOpen)
                openers.push_back(ci);
        }
    }

    std::u16string_view s;
    int n;
    std::vector<bool> locked;
    InlineResult r;
};

} // namespace

InlineResult parseInline(std::u16string_view text)
{
    return Parser(text).run();
}

const LinkRange *linkAt(const InlineResult &result, int pos)
{
    for (const auto &link : result.links) {
        if (pos >= link.start && pos < link.end)
            return &link;
    }
    return nullptr;
}

} // namespace md
