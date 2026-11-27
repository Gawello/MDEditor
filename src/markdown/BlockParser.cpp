#include "markdown/BlockParser.h"

#include "markdown/TableParser.h"

#include <algorithm>

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

// Fences encode as (code << 16) | (length << 2) | tilde bit, i.e. even values >= 12; table states use 1 and 3;
// math and comment blocks 5 and 7.
int BlockState::encode() const
{
    if (block == Block::Math)
        return 5;
    if (block == Block::Comment)
        return 7;
    if (inFence())
        return ((code & 0x7fff) << 16) | (std::min(fenceLength, 0x3fff) << 2) | (fenceChar == u'~' ? 2 : 0);
    if (table == Table::AfterHeader)
        return 1;
    if (table == Table::Body)
        return 3;
    return -1;
}

BlockState BlockState::decode(int state)
{
    if (state < 0)
        return {};
    if (state == 1)
        return {0, 0, Table::AfterHeader};
    if (state == 3)
        return {0, 0, Table::Body};
    if (state == 5 || state == 7) {
        BlockState s;
        s.block = state == 5 ? Block::Math : Block::Comment;
        return s;
    }
    return {(state & 2) ? u'~' : u'`', (state & 0xffff) >> 2, Table::None, state >> 16};
}

LineResult parseLine(std::u16string_view line, BlockState state, std::optional<std::u16string_view> nextLine)
{
    LineResult r;
    r.next = state;
    BlockInfo &b = r.info;
    const int n = int(line.size());
    const int ind = leadingWs(line);
    b.indent = ind;
    b.markerStart = ind;
    b.contentStart = ind;

    if (state.table == BlockState::Table::AfterHeader && parseDelimiterRow(line)) {
        b.type = BlockType::TableDelimiter;
        b.markerLength = n - ind;
        b.contentStart = n;
        r.next.table = BlockState::Table::Body;
        return r;
    }
    if (state.table == BlockState::Table::Body && !isBlank(line) && line.find(u'|') != std::u16string_view::npos) {
        b.type = BlockType::TableRow;
        b.contentStart = 0;
        return r;
    }
    r.next.table = BlockState::Table::None;

    // Inside $$ math or a %% comment: everything up to the closing delimiter belongs to it.
    if (state.block != BlockState::Block::None) {
        const bool math = state.block == BlockState::Block::Math;
        const std::u16string_view close = math ? u"$$" : u"%%";
        b.markerStart = 0;
        b.contentStart = 0;
        b.type = math ? BlockType::MathLine : BlockType::CommentLine;
        if (line.find(close) != std::u16string_view::npos) {
            if (math)
                b.type = BlockType::MathClose;
            r.next.block = BlockState::Block::None;
        }
        return r;
    }

    if (state.inFence()) {
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

    if (ind <= 3 && nextLine && line.find(u'|') != std::u16string_view::npos) {
        const auto aligns = parseDelimiterRow(*nextLine);
        if (aligns && splitTableRow(line).cells.size() == aligns->size()) {
            b.type = BlockType::TableHeader;
            b.contentStart = 0;
            r.next.table = BlockState::Table::AfterHeader;
            return r;
        }
    }

    const char16_t c = line[ind];

    // $$ display math: alone on its line ("$$ x $$"), or opening a block that a later "$$" closes.
    if (ind <= 3 && line.substr(ind).substr(0, 2) == u"$$") {
        size_t end = n;
        while (end > size_t(ind) && isWs(line[end - 1]))
            --end;
        const std::u16string_view rest = line.substr(ind + 2, end - ind - 2);
        const size_t close = rest.find(u"$$");
        if (close == std::u16string_view::npos) {
            b.type = BlockType::MathOpen;
            b.markerLength = 2;
            b.contentStart = ind + 2;
            r.next.block = BlockState::Block::Math;
            return r;
        }
        if (close + 2 == rest.size() && close > 0) {
            b.type = BlockType::MathSingle;
            b.markerLength = 2;
            b.contentStart = ind + 2;
            return r;
        }
    }
    // %% comment over several lines: an opening %% with no closing one on the same line.
    if (ind <= 3 && line.substr(ind).substr(0, 2) == u"%%" && line.find(u"%%", ind + 2) == std::u16string_view::npos) {
        b.type = BlockType::CommentLine;
        r.next.block = BlockState::Block::Comment;
        return r;
    }

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
