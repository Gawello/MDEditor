#include "format/CodeFormatters.h"

#include "markdown/TableParser.h"

#include <algorithm>

namespace fmt {

namespace {

bool isWs(char16_t c) { return c == u' ' || c == u'\t' || c == u'\r' || c == u'\n'; }
bool isDigit(char16_t c) { return c >= u'0' && c <= u'9'; }
bool isAlpha(char16_t c) { return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z'); }

std::u16string_view trim(std::u16string_view s)
{
    while (!s.empty() && isWs(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && isWs(s.back()))
        s.remove_suffix(1);
    return s;
}

std::u16string_view trimRight(std::u16string_view s)
{
    while (!s.empty() && isWs(s.back()))
        s.remove_suffix(1);
    return s;
}

std::vector<std::u16string_view> splitLines(std::u16string_view s)
{
    std::vector<std::u16string_view> lines;
    size_t start = 0;
    while (true) {
        const size_t end = s.find(u'\n', start);
        std::u16string_view line = s.substr(start, end == std::u16string_view::npos ? std::u16string_view::npos : end - start);
        if (!line.empty() && line.back() == u'\r')
            line.remove_suffix(1);
        lines.push_back(line);
        if (end == std::u16string_view::npos)
            break;
        start = end + 1;
    }
    return lines;
}

std::u16string joinLines(const std::vector<std::u16string> &lines)
{
    std::u16string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0)
            out += u'\n';
        out += lines[i];
    }
    return out;
}

Result failure(std::string message, int offset = -1)
{
    Result r;
    r.ok = false;
    r.error = std::move(message);
    r.errorOffset = offset;
    return r;
}

// ---------------------------------------------------------------------------------------------- JSON

class JsonPrinter {
public:
    JsonPrinter(std::u16string_view input, int indent, bool minify)
        : s(input)
        , n(int(input.size()))
        , indent(indent)
        , minify(minify)
    {
    }

    Result run()
    {
        skipWs();
        if (pos >= n)
            return failure("empty input", 0);
        if (!value(0))
            return failure(error, pos);
        skipWs();
        if (pos < n)
            return failure("unexpected content after the JSON value", pos);
        Result r;
        r.text = std::move(out);
        return r;
    }

private:
    char16_t peek() const { return pos < n ? s[pos] : 0; }

    void skipWs()
    {
        while (pos < n && isWs(s[pos]))
            ++pos;
    }

    bool fail(const char *message)
    {
        error = message;
        return false;
    }

    void newline(int depth)
    {
        if (minify)
            return;
        out += u'\n';
        out.append(size_t(depth * indent), u' ');
    }

    bool value(int depth)
    {
        skipWs();
        switch (peek()) {
        case 0:
            return fail("unexpected end of input");
        case u'{':
            return container(depth, u'{', u'}');
        case u'[':
            return container(depth, u'[', u']');
        case u'"':
            return string();
        default:
            return literal();
        }
    }

    bool container(int depth, char16_t open, char16_t close)
    {
        const bool object = open == u'{';
        out += open;
        ++pos;
        skipWs();
        if (peek() == close) {
            ++pos;
            out += close;
            return true;
        }
        while (true) {
            newline(depth + 1);
            if (object) {
                skipWs();
                if (peek() != u'"')
                    return fail("expected a string key");
                if (!string())
                    return false;
                skipWs();
                if (peek() != u':')
                    return fail("expected ':'");
                ++pos;
                out += minify ? u":" : u": ";
            }
            if (!value(depth + 1))
                return false;
            skipWs();
            if (peek() == u',') {
                ++pos;
                out += u',';
                continue;
            }
            if (peek() == close) {
                ++pos;
                newline(depth);
                out += close;
                return true;
            }
            return fail(object ? "expected ',' or '}'" : "expected ',' or ']'");
        }
    }

    bool string()
    {
        const int start = pos++;
        while (pos < n) {
            const char16_t c = s[pos];
            if (c == u'\\') {
                pos += 2;
            } else if (c == u'"') {
                ++pos;
                out.append(s.substr(start, pos - start));
                return true;
            } else if (c == u'\n') {
                break;
            } else {
                ++pos;
            }
        }
        pos = start;
        return fail("unterminated string");
    }

    bool literal()
    {
        const int start = pos;
        while (pos < n && (isAlpha(s[pos]) || isDigit(s[pos]) || s[pos] == u'+' || s[pos] == u'-' || s[pos] == u'.'))
            ++pos;
        const std::u16string_view token = s.substr(start, pos - start);
        if (token.empty())
            return fail("unexpected character");
        if (token != u"true" && token != u"false" && token != u"null" && !isNumber(token)) {
            pos = start;
            return fail("invalid value");
        }
        out.append(token);
        return true;
    }

    static bool isNumber(std::u16string_view t)
    {
        size_t i = 0;
        if (i < t.size() && t[i] == u'-')
            ++i;
        const size_t intStart = i;
        while (i < t.size() && isDigit(t[i]))
            ++i;
        if (i == intStart)
            return false;
        if (i < t.size() && t[i] == u'.') {
            const size_t fracStart = ++i;
            while (i < t.size() && isDigit(t[i]))
                ++i;
            if (i == fracStart)
                return false;
        }
        if (i < t.size() && (t[i] == u'e' || t[i] == u'E')) {
            ++i;
            if (i < t.size() && (t[i] == u'+' || t[i] == u'-'))
                ++i;
            const size_t expStart = i;
            while (i < t.size() && isDigit(t[i]))
                ++i;
            if (i == expStart)
                return false;
        }
        return i == t.size();
    }

    std::u16string_view s;
    int n;
    int indent;
    bool minify;
    int pos = 0;
    std::u16string out;
    const char *error = "";
};

// ----------------------------------------------------------------------------------------------- CSS

bool isPropertyName(std::u16string_view name)
{
    if (name.empty())
        return false;
    return std::all_of(name.begin(), name.end(),
                       [](char16_t c) { return isAlpha(c) || isDigit(c) || c == u'-' || c == u'_'; });
}

std::u16string cssDeclaration(std::u16string_view text, int depth)
{
    const size_t colon = text.find(u':');
    if (depth > 0 && colon != std::u16string_view::npos && isPropertyName(trim(text.substr(0, colon)))) {
        std::u16string out(trim(text.substr(0, colon)));
        out += u": ";
        out += trim(text.substr(colon + 1));
        return out;
    }
    return std::u16string(trim(text));
}

} // namespace

Result formatJson(std::u16string_view input, int indent, bool minify)
{
    return JsonPrinter(input, indent, minify).run();
}

Result formatCss(std::u16string_view input, int indent)
{
    std::u16string out;
    std::u16string pending;
    int depth = 0;
    const int n = int(input.size());

    auto pad = [&] { return std::u16string(size_t(std::max(depth, 0) * indent), u' '); };
    auto flushPending = [&](bool addSemicolon) {
        const std::u16string_view text = trim(pending);
        if (!text.empty()) {
            out += pad() + cssDeclaration(text, depth);
            if (addSemicolon)
                out += u';';
            out += u'\n';
        }
        pending.clear();
    };

    int i = 0;
    while (i < n) {
        const char16_t c = input[i];
        if (c == u'/' && i + 1 < n && input[i + 1] == u'*') {
            const size_t end = input.find(u"*/", size_t(i) + 2);
            const int stop = end == std::u16string_view::npos ? n : int(end) + 2;
            flushPending(false);
            out += pad();
            out.append(input.substr(i, stop - i));
            out += u'\n';
            i = stop;
            continue;
        }
        if (c == u'"' || c == u'\'') {
            int j = i + 1;
            while (j < n && input[j] != c && input[j] != u'\n')
                j += input[j] == u'\\' ? 2 : 1;
            j = std::min(j + 1, n);
            pending.append(input.substr(i, j - i));
            i = j;
            continue;
        }
        if (isWs(c)) {
            if (!pending.empty() && pending.back() != u' ')
                pending += u' ';
            ++i;
            continue;
        }
        if (c == u'{') {
            const std::u16string selector(trim(pending));
            out += pad() + selector + (selector.empty() ? u"{" : u" {") + u'\n';
            pending.clear();
            ++depth;
        } else if (c == u';') {
            flushPending(true);
        } else if (c == u'}') {
            flushPending(true);
            if (depth == 0)
                return failure("unbalanced '}'", i);
            --depth;
            out += pad() + u"}\n";
            if (depth == 0)
                out += u'\n';
        } else {
            pending += c;
        }
        ++i;
    }
    flushPending(false);
    if (depth != 0)
        return failure("missing '}'", n);

    Result r;
    r.text = std::u16string(trimRight(out));
    return r;
}

Result formatCLike(std::u16string_view input, int indent)
{
    std::vector<std::u16string> out;
    int depth = 0;
    bool inBlockComment = false;
    bool inTemplate = false; // JavaScript `template strings` may span lines

    for (const std::u16string_view raw : splitLines(input)) {
        if (inTemplate) {
            out.emplace_back(raw); // inside a multi-line string: leave untouched
        }
        const std::u16string_view content = trim(raw);

        if (!inTemplate) {
            if (content.empty()) {
                out.emplace_back();
            } else if (inBlockComment) {
                std::u16string line(size_t(depth * indent), u' ');
                if (content.front() == u'*')
                    line += u' '; // align " * comment" continuation lines under "/*"
                line += content;
                out.push_back(line);
            } else if (content.front() == u'#') {
                out.emplace_back(content); // preprocessor directives stay in column 0
                continue;
            } else {
                int closers = 0;
                for (const char16_t c : content) {
                    if (c == u'}' || c == u')' || c == u']')
                        ++closers;
                    else if (c != u' ' && c != u'\t')
                        break;
                }
                std::u16string line(size_t(std::max(0, depth - closers) * indent), u' ');
                line += content;
                out.push_back(line);
            }
        }

        // Track bracket depth, skipping strings and comments.
        const int n = int(content.size());
        for (int i = 0; i < n; ++i) {
            const char16_t c = content[i];
            if (inBlockComment) {
                if (c == u'*' && i + 1 < n && content[i + 1] == u'/') {
                    inBlockComment = false;
                    ++i;
                }
                continue;
            }
            if (inTemplate) {
                if (c == u'\\')
                    ++i;
                else if (c == u'`')
                    inTemplate = false;
                continue;
            }
            if (c == u'/' && i + 1 < n && content[i + 1] == u'/')
                break;
            if (c == u'/' && i + 1 < n && content[i + 1] == u'*') {
                inBlockComment = true;
                ++i;
            } else if (c == u'`') {
                inTemplate = true;
            } else if (c == u'"' || c == u'\'') {
                ++i;
                while (i < n && content[i] != c)
                    i += content[i] == u'\\' ? 2 : 1;
            } else if (c == u'{' || c == u'(' || c == u'[') {
                ++depth;
            } else if (c == u'}' || c == u')' || c == u']') {
                depth = std::max(0, depth - 1);
            }
        }
    }

    Result r;
    r.text = joinLines(out);
    return r;
}

Result formatPython(std::u16string_view input, int indent)
{
    std::vector<std::u16string> out;
    std::vector<int> levels{0}; // original indentation columns of the open blocks
    int parens = 0;
    bool continuation = false;   // previous line ended with a backslash
    char16_t triple = 0;         // inside a ''' or """ string spanning lines
    int statementColumn = 0;     // original and new indentation of the current statement,
    int statementIndent = 0;     // for re-basing continuation lines
    int lineNumber = 0;

    for (const std::u16string_view raw : splitLines(input)) {
        ++lineNumber;
        std::u16string_view code; // part of the line to scan for brackets and strings

        if (triple) {
            out.emplace_back(raw); // string contents are never touched
            code = raw;
        } else {
            const std::u16string_view stripped = trimRight(raw);
            const std::u16string_view content = trim(stripped);
            int column = 0;
            for (const char16_t c : stripped) {
                if (c == u' ')
                    ++column;
                else if (c == u'\t')
                    column = (column / 8 + 1) * 8;
                else
                    break;
            }

            if (content.empty()) {
                out.emplace_back();
                continue;
            }
            int newIndent;
            if (parens > 0 || continuation) {
                newIndent = statementIndent + std::max(0, column - statementColumn);
            } else if (content.front() == u'#') {
                // Comments don't open or close blocks: place them at the closest level.
                int level = 0;
                while (level + 1 < int(levels.size()) && levels[level + 1] <= column)
                    ++level;
                newIndent = (column > levels[level] ? level + 1 : level) * indent;
            } else {
                bool dedented = false;
                while (levels.size() > 1 && column < levels.back()) {
                    levels.pop_back();
                    dedented = true;
                }
                if (column > levels.back() && !dedented)
                    levels.push_back(column);
                else if (column != levels.back())
                    return failure("inconsistent indentation on line " + std::to_string(lineNumber));
                newIndent = int(levels.size() - 1) * indent;
                statementColumn = column;
                statementIndent = newIndent;
            }
            out.push_back(std::u16string(size_t(newIndent), u' ') + std::u16string(content));
            code = content;
        }

        // Scan for strings, comments and brackets.
        const int n = int(code.size());
        for (int i = 0; i < n; ++i) {
            const char16_t c = code[i];
            if (triple) {
                if (c == u'\\') {
                    ++i;
                } else if (c == triple && i + 2 < n && code[i + 1] == c && code[i + 2] == c) {
                    triple = 0;
                    i += 2;
                }
                continue;
            }
            if (c == u'#')
                break;
            if (c == u'"' || c == u'\'') {
                if (i + 2 < n && code[i + 1] == c && code[i + 2] == c) {
                    triple = c;
                    i += 2;
                } else {
                    ++i;
                    while (i < n && code[i] != c)
                        i += code[i] == u'\\' ? 2 : 1;
                }
            } else if (c == u'(' || c == u'[' || c == u'{') {
                ++parens;
            } else if (c == u')' || c == u']' || c == u'}') {
                parens = std::max(0, parens - 1);
            }
        }
        const std::u16string_view stripped = trimRight(code);
        continuation = !triple && parens == 0 && !stripped.empty() && stripped.back() == u'\\';
    }

    Result r;
    r.text = joinLines(out);
    return r;
}

std::vector<std::u16string> formatTable(const std::vector<std::u16string> &lines)
{
    if (lines.size() < 2)
        return lines;

    std::vector<md::Align> aligns = md::parseDelimiterRow(lines[1]).value_or(std::vector<md::Align>{});
    std::vector<std::vector<std::u16string>> rows; // cell contents, delimiter row excluded
    size_t columns = aligns.size();
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i == 1)
            continue;
        const md::TableRow row = md::splitTableRow(lines[i]);
        std::vector<std::u16string> cells;
        for (const md::TableCell &cell : row.cells)
            cells.emplace_back(std::u16string_view(lines[i]).substr(cell.start, cell.end - cell.start));
        columns = std::max(columns, cells.size());
        rows.push_back(std::move(cells));
    }
    aligns.resize(columns, md::Align::None);

    std::vector<size_t> widths(columns, 3);
    for (const auto &cells : rows)
        for (size_t c = 0; c < cells.size(); ++c)
            widths[c] = std::max(widths[c], cells[c].size());

    auto renderRow = [&](const std::vector<std::u16string> &cells) {
        std::u16string line = u"|";
        for (size_t c = 0; c < columns; ++c) {
            const std::u16string text = c < cells.size() ? cells[c] : std::u16string();
            const size_t space = widths[c] - text.size();
            size_t left = 0;
            if (aligns[c] == md::Align::Right)
                left = space;
            else if (aligns[c] == md::Align::Center)
                left = space / 2;
            line += u' ';
            line.append(left, u' ');
            line += text;
            line.append(space - left, u' ');
            line += u" |";
        }
        return line;
    };

    std::u16string delimiter = u"|";
    for (size_t c = 0; c < columns; ++c) {
        std::u16string dashes(widths[c], u'-');
        if (aligns[c] == md::Align::Left || aligns[c] == md::Align::Center)
            dashes.front() = u':';
        if (aligns[c] == md::Align::Right || aligns[c] == md::Align::Center)
            dashes.back() = u':';
        delimiter += u' ' + dashes + u" |";
    }

    std::vector<std::u16string> result;
    result.push_back(renderRow(rows.front()));
    result.push_back(delimiter);
    for (size_t r = 1; r < rows.size(); ++r)
        result.push_back(renderRow(rows[r]));
    return result;
}

} // namespace fmt
