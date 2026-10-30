#include "markdown/BlockParser.h"

namespace md {

namespace {

bool isWs(char16_t c) { return c == u' ' || c == u'\t'; }
bool isDigit(char16_t c) { return c >= u'0' && c <= u'9'; }

int leadingWs(std::u16string_view s)
{
    int i = 0;
    while (i < int(s.size()) && isWs(s[i]))
        ++i;
    return i;
}

bool isBlank(std::u16string_view s) { return leadingWs(s) == int(s.size()); }

int runLength(std::u16string_view s, int from, char16_t c)
{
    int i = from;
    while (i < int(s.size()) && s[i] == c)
        ++i;
    return i - from;
}

int skipWs(std::u16string_view s, int from)
{
    while (from < int(s.size()) && isWs(s[from]))
        ++from;
    return from;
}

} // namespace

int FenceState::encode() const
{
    if (!inside())
        return -1;
    return (fenceLength << 1) | (fenceChar == u'~' ? 1 : 0);
}

FenceState FenceState::decode(int state)
{
    if (state < 0)
        return {};
    return {(state & 1) ? u'~' : u'`', state >> 1};
}

LineResult parseLine(std::u16string_view line, FenceState state)
{
    LineResult r;
    r.next = state;
    BlockInfo &b = r.info;
    const int n = int(line.size());
    const int ind = leadingWs(line);
    b.indent = ind;
    b.markerStart = ind;
    b.contentStart = ind;

    if (state.inside()) {
        if (ind <= 3) {
            const int len = runLength(line, ind, state.fenceChar);
            if (len >= state.fenceLength && isBlank(line.substr(ind + len))) {
                b.type = BlockType::FenceClose;
                b.markerLength = n - ind;
                b.contentStart = n;
                r.next = {};
                return r;
            }
        }
        b.type = BlockType::CodeLine;
        b.markerStart = 0;
        b.contentStart = 0;
        return r;
    }

    if (ind == n) {
        b.type = BlockType::Blank;
        return r;
    }

    const char16_t c = line[ind];

    if (ind <= 3 && (c == u'`' || c == u'~')) {
        const int len = runLength(line, ind, c);
        if (len >= 3) {
            const auto infoString = line.substr(ind + len);
            if (c == u'~' || infoString.find(u'`') == std::u16string_view::npos) {
                b.type = BlockType::FenceOpen;
                b.markerLength = len;
                b.contentStart = ind + len;
                r.next = {c, len};
                return r;
            }
        }
    }

    if (ind <= 3) {
        if (c == u'#') {
            const int level = runLength(line, ind, u'#');
            if (level <= 6 && (ind + level == n || isWs(line[ind + level]))) {
                const int p = skipWs(line, ind + level);
                b.type = BlockType::Heading;
                b.headingLevel = level;
                b.markerLength = p - ind;
                b.contentStart = p;
                return r;
            }
        }

        if (c == u'-' || c == u'*' || c == u'_') {
            int count = 0;
            bool onlyRule = true;
            for (int i = ind; i < n; ++i) {
                if (line[i] == c)
                    ++count;
                else if (!isWs(line[i])) {
                    onlyRule = false;
                    break;
                }
            }
            if (onlyRule && count >= 3) {
                b.type = BlockType::HorizontalRule;
                b.markerLength = n - ind;
                b.contentStart = n;
                return r;
            }
        }

        if (c == u'>') {
            int p = ind;
            while (p < n && (line[p] == u'>' || isWs(line[p])))
                ++p;
            b.type = BlockType::Quote;
            b.markerLength = p - ind;
            b.contentStart = p;
            return r;
        }
    }

    if ((c == u'-' || c == u'*' || c == u'+') && (ind + 1 == n || isWs(line[ind + 1]))) {
        int p = skipWs(line, ind + 1);
        b.type = BlockType::BulletItem;
        b.listMarker = c;
        if (p + 2 < n && line[p] == u'[' && line[p + 2] == u']'
            && (line[p + 1] == u' ' || line[p + 1] == u'x' || line[p + 1] == u'X')
            && (p + 3 == n || isWs(line[p + 3]))) {
            b.type = BlockType::TaskItem;
            b.checkboxStart = p;
            b.checked = line[p + 1] != u' ';
            p = skipWs(line, p + 3);
        }
        b.markerLength = p - ind;
        b.contentStart = p;
        return r;
    }

    if (isDigit(c)) {
        int p = ind;
        int number = 0;
        while (p < n && isDigit(line[p]) && p - ind < 9) {
            number = number * 10 + (line[p] - u'0');
            ++p;
        }
        if (p < n && (line[p] == u'.' || line[p] == u')') && (p + 1 == n || isWs(line[p + 1]))) {
            b.type = BlockType::OrderedItem;
            b.orderedNumber = number;
            b.listMarker = line[p];
            p = skipWs(line, p + 1);
            b.markerLength = p - ind;
            b.contentStart = p;
            return r;
        }
    }

    b.type = BlockType::Paragraph;
    return r;
}

} // namespace md
