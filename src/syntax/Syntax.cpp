#include "syntax/Syntax.h"

#include <algorithm>
#include <unordered_set>

namespace syntax {

namespace {

using Words = std::unordered_set<std::u16string>;

enum class Kind { Generic, Markup, Ini, Diff };

struct Language {
    std::u16string name;
    std::vector<std::u16string> aliases; // lower case, including `name`
    std::vector<std::u16string> suffixes;
    std::vector<std::u16string> fileNames;
    Kind kind = Kind::Generic;
    Words keywords;
    Words types;
    Words constants;
    std::vector<std::u16string> lineComments;
    std::vector<std::pair<std::u16string, std::u16string>> blockComments;
    std::u16string quotes = u"\"'";   // string delimiters
    std::u16string multiline;         // quotes whose strings may span lines (JS template, Go raw)
    bool tripleQuotes = false;        // Python """ and '''
    bool caseInsensitive = false;     // SQL, PowerShell...
    bool propertyBeforeColon = false; // a name or string followed by ':' is a key
    bool variables = false;           // $name, ${name}
    bool atNames = false;             // @decorator, @media
    bool preprocessor = false;        // # at the start of a line
    bool hashColors = false;          // CSS #fff
    bool hashCommentAtWordStart = false; // shells: '#' starts a comment only at the start of a word
    bool rawStrings = false;          // r"..." (Rust, Python): no escapes
};

Words words(std::u16string_view list)
{
    Words result;
    size_t i = 0;
    while (i < list.size()) {
        while (i < list.size() && list[i] == u' ')
            ++i;
        size_t j = i;
        while (j < list.size() && list[j] != u' ')
            ++j;
        if (j > i)
            result.insert(std::u16string(list.substr(i, j - i)));
        i = j;
    }
    return result;
}

std::vector<std::u16string> list(std::u16string_view items)
{
    const Words set = words(items);
    return {set.begin(), set.end()};
}

const std::u16string_view cTypes = u"void char short int long float double signed unsigned bool size_t "
                                   u"int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t wchar_t auto";

std::vector<Language> makeLanguages()
{
    std::vector<Language> all;
    const auto cLike = [](Language l) {
        l.lineComments = {u"//"};
        l.blockComments = {{u"/*", u"*/"}};
        return l;
    };

    {
        Language l;
        l.name = u"C++";
        l.aliases = list(u"c cpp c++ cxx cc h hpp objc objective-c arduino");
        l.suffixes = list(u"c cpp cxx cc c++ h hpp hxx hh inl ino m mm");
        l.keywords = words(u"if else for while do switch case default break continue return goto struct union enum "
                           u"class public private protected virtual override final const constexpr consteval constinit "
                           u"static extern inline volatile mutable typedef typename template namespace using new delete "
                           u"this operator friend explicit noexcept throw try catch sizeof alignof decltype static_assert "
                           u"co_await co_return co_yield concept requires export import module register thread_local");
        l.types = words(std::u16string(cTypes) + u" std string vector map unordered_map set optional QString QList");
        l.constants = words(u"true false nullptr NULL");
        l.preprocessor = true;
        all.push_back(cLike(l));
    }
    {
        Language l;
        l.name = u"C#";
        l.aliases = list(u"cs csharp c#");
        l.suffixes = list(u"cs csx");
        l.keywords = words(u"abstract as base break case catch checked class const continue default delegate do else "
                           u"enum event explicit extern finally fixed for foreach goto if implicit in interface internal "
                           u"is lock namespace new operator out override params private protected public readonly ref "
                           u"return sealed sizeof stackalloc static struct switch this throw try typeof unchecked unsafe "
                           u"using virtual volatile while async await var get set init record yield where when");
        l.types = words(u"bool byte char decimal double float int long object sbyte short string uint ulong ushort void "
                        u"dynamic nint nuint");
        l.constants = words(u"true false null");
        l.preprocessor = true;
        l.atNames = true;
        all.push_back(cLike(l));
    }
    {
        Language l;
        l.name = u"Java";
        l.aliases = list(u"java kotlin kt groovy scala");
        l.suffixes = list(u"java kt kts groovy gradle scala");
        l.keywords = words(u"abstract assert break case catch class const continue default do else enum extends final "
                           u"finally for goto if implements import instanceof interface native new package private "
                           u"protected public return static strictfp super switch synchronized this throw throws "
                           u"transient try volatile while var record sealed permits yield fun val object companion data "
                           u"when is in out override open internal lateinit suspend def");
        l.types = words(u"boolean byte char double float int long short void String Integer Object List Map Any Unit");
        l.constants = words(u"true false null");
        l.atNames = true;
        l.multiline = u"\"";
        l.tripleQuotes = true; // Java text blocks, Kotlin raw strings
        all.push_back(cLike(l));
    }
    {
        Language l;
        l.name = u"JavaScript";
        l.aliases = list(u"js javascript jsx ts typescript tsx mjs cjs node");
        l.suffixes = list(u"js jsx ts tsx mjs cjs mts cts");
        l.keywords = words(u"break case catch class const continue debugger default delete do else export extends "
                           u"finally for function if import in instanceof let new return super switch this throw try "
                           u"typeof var void while with yield async await of static get set from as interface type "
                           u"enum implements namespace declare abstract private public protected readonly keyof");
        l.types = words(u"string number boolean any unknown never object symbol bigint Array Promise Map Set Record");
        l.constants = words(u"true false null undefined NaN Infinity");
        l.quotes = u"\"'`";
        l.multiline = u"`";
        l.atNames = true;
        all.push_back(cLike(l));
    }
    {
        Language l;
        l.name = u"JSON";
        l.aliases = list(u"json jsonc json5 geojson");
        l.suffixes = list(u"json jsonc json5 geojson webmanifest");
        l.fileNames = list(u".prettierrc .eslintrc .babelrc");
        l.constants = words(u"true false null");
        l.quotes = u"\"";
        l.propertyBeforeColon = true;
        all.push_back(cLike(l)); // JSONC allows comments
    }
    {
        Language l;
        l.name = u"Python";
        l.aliases = list(u"py python python3 py3 gyp");
        l.suffixes = list(u"py pyw pyi gyp");
        l.keywords = words(u"and as assert async await break class continue def del elif else except finally for from "
                           u"global if import in is lambda nonlocal not or pass raise return try while with yield match "
                           u"case");
        l.types = words(u"int float str bool list dict set tuple bytes object type self cls");
        l.constants = words(u"True False None");
        l.lineComments = {u"#"};
        l.tripleQuotes = true;
        l.atNames = true;
        l.rawStrings = true;
        all.push_back(l);
    }
    {
        Language l;
        l.name = u"Go";
        l.aliases = list(u"go golang");
        l.suffixes = list(u"go");
        l.keywords = words(u"break case chan const continue default defer else fallthrough for func go goto if import "
                           u"interface map package range return select struct switch type var");
        l.types = words(u"bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune string "
                        u"uint uint8 uint16 uint32 uint64 uintptr any");
        l.constants = words(u"true false nil iota");
        l.quotes = u"\"'`";
        l.multiline = u"`";
        all.push_back(cLike(l));
    }
    {
        Language l;
        l.name = u"Rust";
        l.aliases = list(u"rs rust");
        l.suffixes = list(u"rs");
        l.keywords = words(u"as async await break const continue crate dyn else enum extern fn for if impl in let loop "
                           u"match mod move mut pub ref return self Self static struct super trait type unsafe use where "
                           u"while macro_rules");
        l.types = words(u"i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize f32 f64 bool char str String Vec Option "
                        u"Result Box");
        l.constants = words(u"true false None Some Ok Err");
        l.quotes = u"\"";
        l.multiline = u"\"";
        l.rawStrings = true;
        all.push_back(cLike(l));
    }
    {
        Language l;
        l.name = u"SQL";
        l.aliases = list(u"sql mysql postgresql psql sqlite plsql tsql");
        l.suffixes = list(u"sql");
        l.caseInsensitive = true;
        l.keywords = words(u"select from where and or not insert into values update set delete create table drop alter "
                           u"add column index view join left right inner outer full cross on as group by order having "
                           u"limit offset union all distinct case when then else end is null in exists between like "
                           u"primary key foreign references default unique constraint begin commit rollback with "
                           u"returning if replace trigger function procedure declare asc desc");
        l.types = words(u"int integer bigint smallint serial decimal numeric real float double varchar char text date "
                        u"time timestamp boolean blob json uuid");
        l.constants = words(u"true false null");
        l.lineComments = {u"--"};
        l.blockComments = {{u"/*", u"*/"}};
        all.push_back(l);
    }
    {
        Language l;
        l.name = u"Shell";
        l.aliases = list(u"sh bash zsh shell console shellscript ksh fish");
        l.suffixes = list(u"sh bash zsh ksh fish command");
        l.fileNames = list(u".bashrc .bash_profile .profile .zshrc .zprofile .bash_aliases PKGBUILD");
        l.keywords = words(u"if then else elif fi for while until do done case esac in function return local export "
                           u"readonly declare unset shift exit break continue select time source alias echo cd set");
        l.constants = words(u"true false");
        l.lineComments = {u"#"};
        l.hashCommentAtWordStart = true;
        l.variables = true;
        l.multiline = u"\"'";
        all.push_back(l);
    }
    {
        Language l;
        l.name = u"PowerShell";
        l.aliases = list(u"ps1 powershell pwsh posh");
        l.suffixes = list(u"ps1 psm1 psd1");
        l.caseInsensitive = true;
        l.keywords = words(u"begin break catch class continue data do dynamicparam else elseif end exit filter finally "
                           u"for foreach from function if in param process return switch throw trap try until using "
                           u"while");
        l.constants = words(u"$true $false $null");
        l.lineComments = {u"#"};
        l.blockComments = {{u"<#", u"#>"}};
        l.variables = true;
        all.push_back(l);
    }
    {
        Language l;
        l.name = u"PHP";
        l.aliases = list(u"php");
        l.suffixes = list(u"php phtml");
        l.caseInsensitive = true;
        l.keywords = words(u"abstract and array as break callable case catch class clone const continue declare default "
                           u"do echo else elseif empty enddeclare endfor endforeach endif endswitch endwhile extends "
                           u"final finally fn for foreach function global goto if implements include instanceof "
                           u"insteadof interface isset list match namespace new or print private protected public "
                           u"readonly require return static switch throw trait try unset use var while yield");
        l.constants = words(u"true false null");
        l.lineComments = {u"//", u"#"};
        l.blockComments = {{u"/*", u"*/"}};
        l.variables = true;
        all.push_back(l);
    }
    {
        Language l;
        l.name = u"Ruby";
        l.aliases = list(u"rb ruby");
        l.suffixes = list(u"rb rake gemspec");
        l.fileNames = list(u"Gemfile Rakefile");
        l.keywords = words(u"alias and begin break case class def defined? do else elsif end ensure for if in module "
                           u"next not or redo rescue retry return self super then undef unless until when while yield "
                           u"require attr_accessor attr_reader private");
        l.constants = words(u"true false nil");
        l.lineComments = {u"#"};
        all.push_back(l);
    }
    {
        Language l;
        l.name = u"Lua";
        l.aliases = list(u"lua");
        l.suffixes = list(u"lua");
        l.keywords = words(u"and break do else elseif end for function goto if in local not or repeat return then until "
                           u"while");
        l.constants = words(u"true false nil");
        l.lineComments = {u"--"};
        l.blockComments = {{u"--[[", u"]]"}};
        all.push_back(l);
    }
    {
        Language l;
        l.name = u"CMake";
        l.aliases = list(u"cmake");
        l.suffixes = list(u"cmake");
        l.fileNames = list(u"CMakeLists.txt");
        l.caseInsensitive = true;
        l.keywords = words(u"if elseif else endif foreach endforeach while endwhile function endfunction macro endmacro "
                           u"return set unset option project add_executable add_library target_link_libraries "
                           u"target_include_directories target_compile_definitions target_sources find_package include "
                           u"install add_subdirectory add_test message list string file");
        l.constants = words(u"on off true false yes no");
        l.lineComments = {u"#"};
        l.quotes = u"\"";
        l.multiline = u"\"";
        l.variables = true;
        all.push_back(l);
    }
    {
        Language l;
        l.name = u"Makefile";
        l.aliases = list(u"make makefile mk");
        l.suffixes = list(u"mk mak");
        l.fileNames = list(u"Makefile makefile GNUmakefile");
        l.keywords = words(u"ifeq ifneq ifdef ifndef else endif include define endef export override");
        l.lineComments = {u"#"};
        l.variables = true;
        all.push_back(l);
    }
    {
        Language l;
        l.name = u"YAML";
        l.aliases = list(u"yaml yml");
        l.suffixes = list(u"yaml yml");
        l.fileNames = list(u".clang-format .clang-tidy");
        l.constants = words(u"true false null yes no on off ~");
        l.lineComments = {u"#"};
        l.hashCommentAtWordStart = true;
        l.propertyBeforeColon = true;
        all.push_back(l);
    }
    {
        Language l;
        l.name = u"CSS";
        l.aliases = list(u"css scss sass less");
        l.suffixes = list(u"css scss sass less");
        l.blockComments = {{u"/*", u"*/"}};
        l.lineComments = {}; // `//` only in SCSS; URLs would turn into comments in CSS
        l.constants = words(u"important inherit initial unset none auto");
        l.propertyBeforeColon = true;
        l.atNames = true;
        l.hashColors = true;
        all.push_back(l);
    }
    {
        Language l;
        l.name = u"XML";
        l.aliases = list(u"xml html htm xhtml svg plist xaml vue svelte");
        l.suffixes = list(u"xml html htm xhtml svg plist xaml xsd xsl xslt ui qrc csproj vcxproj props targets");
        l.kind = Kind::Markup;
        all.push_back(l);
    }
    {
        Language l;
        l.name = u"INI";
        l.aliases = list(u"ini toml cfg conf properties dosini env editorconfig desktop");
        l.suffixes = list(u"ini toml cfg conf properties env desktop service inf reg");
        l.fileNames = list(u".editorconfig .gitconfig .npmrc .env");
        l.kind = Kind::Ini;
        l.constants = words(u"true false yes no on off");
        all.push_back(l);
    }
    {
        Language l;
        l.name = u"Diff";
        l.aliases = list(u"diff patch udiff");
        l.suffixes = list(u"diff patch rej");
        l.kind = Kind::Diff;
        all.push_back(l);
    }
    for (Language &l : all) {
        std::u16string lower = l.name;
        for (char16_t &c : lower)
            c = (c >= u'A' && c <= u'Z') ? char16_t(c - u'A' + u'a') : c;
        l.aliases.push_back(lower);
    }
    return all;
}

const std::vector<Language> &languages()
{
    static const std::vector<Language> all = makeLanguages();
    return all;
}

std::u16string lowered(std::u16string_view text)
{
    std::u16string result(text);
    for (char16_t &c : result) {
        if (c >= u'A' && c <= u'Z')
            c = char16_t(c - u'A' + u'a');
    }
    return result;
}

bool isWordStart(char16_t c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || c == u'_' || c > 0x7f;
}

bool isWord(char16_t c)
{
    return isWordStart(c) || (c >= u'0' && c <= u'9');
}

bool isDigit(char16_t c)
{
    return c >= u'0' && c <= u'9';
}

bool startsWith(std::u16string_view text, size_t at, std::u16string_view prefix)
{
    return text.size() - at >= prefix.size() && text.substr(at, prefix.size()) == prefix;
}

// Spans are collected in order; neighbors of the same kind are merged.
void push(std::vector<Span> &spans, int start, int end, Token token)
{
    if (end <= start || token == Token::Plain)
        return;
    if (!spans.empty() && spans.back().token == token && spans.back().start + spans.back().length == start) {
        spans.back().length += end - start;
        return;
    }
    spans.push_back({start, end - start, token});
}

// --- Generic (C-like, scripting, data) languages ---
//
// States: 1..8 inside block comment n-1; 16 + 2 * quote index (+1 for a triple quote) inside a string.

constexpr int blockCommentState = 1;
constexpr int stringState = 16;

int stringStateFor(int quoteIndex, bool triple)
{
    return stringState + quoteIndex * 2 + (triple ? 1 : 0);
}

// Scans a string body from `i` (just after the opening quote); returns the end and whether it closed.
size_t scanString(const Language &l, std::u16string_view line, size_t i, char16_t quote, bool triple, bool raw,
                  bool *closed)
{
    const size_t n = line.size();
    while (i < n) {
        if (line[i] == u'\\' && !raw && i + 1 < n) {
            i += 2;
            continue;
        }
        if (line[i] == quote) {
            if (!triple) {
                *closed = true;
                return i + 1;
            }
            if (i + 2 < n && line[i + 1] == quote && line[i + 2] == quote) {
                *closed = true;
                return i + 3;
            }
        }
        ++i;
    }
    (void)l;
    *closed = false;
    return n;
}

bool nextNonSpaceIs(std::u16string_view line, size_t i, char16_t c)
{
    while (i < line.size() && (line[i] == u' ' || line[i] == u'\t'))
        ++i;
    return i < line.size() && line[i] == c && !(c == u':' && i + 1 < line.size() && line[i + 1] == u':');
}

int highlightGeneric(const Language &l, std::u16string_view line, int state, std::vector<Span> &spans)
{
    const size_t n = line.size();
    size_t i = 0;

    // Continue what the previous line left open.
    if (state >= blockCommentState && state < blockCommentState + int(l.blockComments.size())) {
        const std::u16string &close = l.blockComments[state - blockCommentState].second;
        const size_t end = line.find(close);
        if (end == std::u16string_view::npos) {
            push(spans, 0, int(n), Token::Comment);
            return state;
        }
        i = end + close.size();
        push(spans, 0, int(i), Token::Comment);
    } else if (state >= stringState) {
        const int quoteIndex = (state - stringState) / 2;
        const bool triple = (state - stringState) % 2 == 1;
        if (quoteIndex < int(l.quotes.size())) {
            bool closed = false;
            i = scanString(l, line, 0, l.quotes[quoteIndex], triple, false, &closed);
            push(spans, 0, int(i), Token::String);
            if (!closed)
                return state;
        }
    }

    // Preprocessor directives take the whole line (C, C#).
    if (l.preprocessor && i == 0) {
        size_t p = 0;
        while (p < n && (line[p] == u' ' || line[p] == u'\t'))
            ++p;
        if (p < n && line[p] == u'#') {
            size_t end = n;
            for (const auto &comment : l.lineComments) {
                const size_t at = line.find(comment, p);
                end = std::min(end, at);
            }
            push(spans, int(p), int(end), Token::Meta);
            if (end < n)
                push(spans, int(end), int(n), Token::Comment);
            return 0;
        }
    }

    while (i < n) {
        const char16_t c = line[i];

        bool matched = false;
        for (const auto &comment : l.lineComments) {
            if (!startsWith(line, i, comment))
                continue;
            if (l.hashCommentAtWordStart && comment == u"#" && i > 0 && line[i - 1] != u' ' && line[i - 1] != u'\t')
                continue;
            push(spans, int(i), int(n), Token::Comment);
            return 0;
        }
        for (size_t b = 0; b < l.blockComments.size() && !matched; ++b) {
            const auto &[open, close] = l.blockComments[b];
            if (!startsWith(line, i, open))
                continue;
            const size_t end = line.find(close, i + open.size());
            if (end == std::u16string_view::npos) {
                push(spans, int(i), int(n), Token::Comment);
                return blockCommentState + int(b);
            }
            push(spans, int(i), int(end + close.size()), Token::Comment);
            i = end + close.size();
            matched = true;
        }
        if (matched)
            continue;

        const size_t quoteIndex = l.quotes.find(c);
        if (quoteIndex != std::u16string::npos) {
            const bool triple = l.tripleQuotes && i + 2 < n && line[i + 1] == c && line[i + 2] == c;
            // r"..." / R"...": the prefix is a word of its own, not the end of a longer name.
            const bool raw = l.rawStrings && i > 0 && (line[i - 1] == u'r' || line[i - 1] == u'R')
                && (i == 1 || !isWord(line[i - 2]));
            bool closed = false;
            const size_t end = scanString(l, line, i + (triple ? 3 : 1), c, triple, raw, &closed);
            const int start = raw ? int(i) - 1 : int(i);
            const Token token = l.propertyBeforeColon && closed && nextNonSpaceIs(line, end, u':') ? Token::Property
                                                                                                  : Token::String;
            push(spans, start, int(end), token);
            i = end;
            if (!closed && (triple || l.multiline.find(c) != std::u16string::npos))
                return stringStateFor(int(quoteIndex), triple);
            continue;
        }

        if (isDigit(c) || (c == u'.' && i + 1 < n && isDigit(line[i + 1]))) {
            if (i > 0 && isWord(line[i - 1])) {
                ++i;
                continue;
            }
            size_t end = i + 1;
            while (end < n && (isWord(line[end]) || line[end] == u'.'
                               || ((line[end] == u'+' || line[end] == u'-') && (line[end - 1] == u'e' || line[end - 1] == u'E'))))
                ++end;
            push(spans, int(i), int(end), Token::Number);
            i = end;
            continue;
        }

        if (l.hashColors && c == u'#' && i + 1 < n && isWord(line[i + 1])) {
            size_t end = i + 1;
            while (end < n && isWord(line[end]))
                ++end;
            push(spans, int(i), int(end), Token::Number);
            i = end;
            continue;
        }

        if (l.variables && c == u'$' && i + 1 < n) {
            size_t end = i + 1;
            if (line[end] == u'{') {
                const size_t close = line.find(u'}', end);
                end = close == std::u16string_view::npos ? n : close + 1;
            } else {
                while (end < n && isWord(line[end]))
                    ++end;
                if (end == i + 1 && (isDigit(line[end]) || line[end] == u'@' || line[end] == u'?' || line[end] == u'#'))
                    ++end;
            }
            if (end > i + 1) {
                const std::u16string word = l.caseInsensitive ? lowered(line.substr(i, end - i))
                                                              : std::u16string(line.substr(i, end - i));
                push(spans, int(i), int(end), l.constants.count(word) ? Token::Constant : Token::Variable);
                i = end;
                continue;
            }
        }

        if (l.atNames && c == u'@' && i + 1 < n && isWordStart(line[i + 1])) {
            size_t end = i + 1;
            while (end < n && (isWord(line[end]) || line[end] == u'.' || line[end] == u'-'))
                ++end;
            push(spans, int(i), int(end), Token::Meta);
            i = end;
            continue;
        }

        if (isWordStart(c)) {
            size_t end = i + 1;
            // CSS property names and shell options contain dashes.
            while (end < n && (isWord(line[end]) || (l.hashColors && line[end] == u'-')))
                ++end;
            const std::u16string_view word = line.substr(i, end - i);
            const std::u16string key = l.caseInsensitive ? lowered(word) : std::u16string(word);
            Token token = Token::Plain;
            if (l.propertyBeforeColon && nextNonSpaceIs(line, end, u':'))
                token = Token::Property;
            else if (l.keywords.count(key))
                token = Token::Keyword;
            else if (l.types.count(key))
                token = Token::Type;
            else if (l.constants.count(key))
                token = Token::Constant;
            else if (nextNonSpaceIs(line, end, u'('))
                token = Token::Function;
            push(spans, int(i), int(end), token);
            i = end;
            continue;
        }
        ++i;
    }
    return 0;
}

// --- XML / HTML ---
//
// States: 1 in a comment, 2 in a tag, 3 / 4 in a "..." / '...' attribute value inside a tag, 5 in CDATA.

int highlightMarkup(std::u16string_view line, int state, std::vector<Span> &spans)
{
    const size_t n = line.size();
    size_t i = 0;
    const auto closeAt = [&](std::u16string_view close, int open, Token token) -> bool {
        const size_t end = line.find(close, i);
        if (end == std::u16string_view::npos) {
            push(spans, int(i), int(n), token);
            i = n;
            state = open;
            return false;
        }
        push(spans, int(i), int(end + close.size()), token);
        i = end + close.size();
        state = 0;
        return true;
    };

    while (i < n) {
        if (state == 1) {
            closeAt(u"-->", 1, Token::Comment);
            continue;
        }
        if (state == 5) {
            closeAt(u"]]>", 5, Token::String);
            continue;
        }
        if (state == 3 || state == 4) {
            const char16_t quote = state == 3 ? u'"' : u'\'';
            const size_t end = line.find(quote, i);
            if (end == std::u16string_view::npos) {
                push(spans, int(i), int(n), Token::String);
                return state;
            }
            push(spans, int(i), int(end + 1), Token::String);
            i = end + 1;
            state = 2;
            continue;
        }
        if (state == 2) { // inside a tag: attributes until '>'
            const char16_t c = line[i];
            if (c == u'>' || (c == u'/' && i + 1 < n && line[i + 1] == u'>') || (c == u'?' && i + 1 < n && line[i + 1] == u'>')) {
                const size_t end = line.find(u'>', i) + 1;
                push(spans, int(i), int(end), Token::Tag);
                i = end;
                state = 0;
            } else if (c == u'"' || c == u'\'') {
                push(spans, int(i), int(i + 1), Token::String);
                ++i;
                state = c == u'"' ? 3 : 4;
            } else if (isWordStart(c)) {
                size_t end = i + 1;
                while (end < n && (isWord(line[end]) || line[end] == u'-' || line[end] == u':' || line[end] == u'.'))
                    ++end;
                push(spans, int(i), int(end), Token::Attribute);
                i = end;
            } else {
                ++i;
            }
            continue;
        }

        // Text.
        const char16_t c = line[i];
        if (startsWith(line, i, u"<!--")) {
            closeAt(u"-->", 1, Token::Comment);
        } else if (startsWith(line, i, u"<![CDATA[")) {
            closeAt(u"]]>", 5, Token::String);
        } else if (c == u'<' && i + 1 < n && (isWordStart(line[i + 1]) || line[i + 1] == u'/' || line[i + 1] == u'!' || line[i + 1] == u'?')) {
            size_t end = i + 1;
            while (end < n && (line[end] == u'/' || line[end] == u'!' || line[end] == u'?'))
                ++end;
            while (end < n && (isWord(line[end]) || line[end] == u'-' || line[end] == u':' || line[end] == u'.'))
                ++end;
            push(spans, int(i), int(end), Token::Tag);
            i = end;
            state = 2;
        } else if (c == u'&') {
            size_t end = i + 1;
            while (end < n && end - i < 32 && (isWord(line[end]) || line[end] == u'#'))
                ++end;
            if (end < n && line[end] == u';') {
                push(spans, int(i), int(end + 1), Token::Constant);
                i = end + 1;
            } else {
                ++i;
            }
        } else {
            ++i;
        }
    }
    return state;
}

// --- INI / TOML / properties / .env ---

int highlightIni(const Language &l, std::u16string_view line, std::vector<Span> &spans)
{
    const size_t n = line.size();
    size_t i = 0;
    while (i < n && (line[i] == u' ' || line[i] == u'\t'))
        ++i;
    if (i == n)
        return 0;
    if (line[i] == u';' || line[i] == u'#') {
        push(spans, int(i), int(n), Token::Comment);
        return 0;
    }
    if (line[i] == u'[') {
        const size_t end = line.find(u']', i);
        push(spans, int(i), end == std::u16string_view::npos ? int(n) : int(end + 1), Token::Tag);
        return 0;
    }
    size_t separator = i;
    while (separator < n && line[separator] != u'=' && line[separator] != u':')
        ++separator;
    if (separator == n)
        return 0;
    push(spans, int(i), int(separator), Token::Property);

    // The value: strings, numbers, booleans; comments after it.
    size_t v = separator + 1;
    while (v < n && (line[v] == u' ' || line[v] == u'\t'))
        ++v;
    if (v == n)
        return 0;
    const std::u16string_view value = line.substr(v);
    size_t valueEnd = n;
    while (valueEnd > v && (line[valueEnd - 1] == u' ' || line[valueEnd - 1] == u'\t'))
        --valueEnd;
    const std::u16string word = lowered(line.substr(v, valueEnd - v));
    if (value[0] == u'"' || value[0] == u'\'') {
        const size_t close = line.find(value[0], v + 1);
        const size_t end = close == std::u16string_view::npos ? n : close + 1;
        push(spans, int(v), int(end), Token::String);
        const size_t hash = line.find(u'#', end);
        if (hash != std::u16string_view::npos)
            push(spans, int(hash), int(n), Token::Comment);
    } else if (l.constants.count(word)) {
        push(spans, int(v), int(valueEnd), Token::Constant);
    } else if (isDigit(value[0]) || ((value[0] == u'-' || value[0] == u'+') && value.size() > 1 && isDigit(value[1]))) {
        size_t end = v + 1;
        while (end < valueEnd && (isWord(line[end]) || line[end] == u'.' || line[end] == u'-' || line[end] == u':'))
            ++end;
        if (end == valueEnd)
            push(spans, int(v), int(end), Token::Number);
    }
    return 0;
}

// --- Unified diff ---

int highlightDiff(std::u16string_view line, std::vector<Span> &spans)
{
    const int n = int(line.size());
    if (n == 0)
        return 0;
    if (startsWith(line, 0, u"+++") || startsWith(line, 0, u"---") || startsWith(line, 0, u"diff ")
        || startsWith(line, 0, u"index ") || startsWith(line, 0, u"@@"))
        push(spans, 0, n, Token::Meta);
    else if (line[0] == u'+' || line[0] == u'>')
        push(spans, 0, n, Token::Inserted);
    else if (line[0] == u'-' || line[0] == u'<')
        push(spans, 0, n, Token::Deleted);
    return 0;
}

const Language *languageAt(int language)
{
    const auto &all = languages();
    return language >= 1 && language <= int(all.size()) ? &all[language - 1] : nullptr;
}

} // namespace

int languageForName(std::u16string_view name)
{
    // Fence info strings may carry more: "python title=x.py", "{.cpp}".
    size_t start = 0;
    while (start < name.size() && (name[start] == u'{' || name[start] == u'.' || name[start] == u' '))
        ++start;
    size_t end = start;
    while (end < name.size() && name[end] != u' ' && name[end] != u'}' && name[end] != u',')
        ++end;
    const std::u16string key = lowered(name.substr(start, end - start));
    if (key.empty())
        return 0;
    const auto &all = languages();
    for (size_t i = 0; i < all.size(); ++i) {
        if (std::find(all[i].aliases.begin(), all[i].aliases.end(), key) != all[i].aliases.end())
            return int(i) + 1;
    }
    return 0;
}

int languageForSuffix(std::u16string_view suffix)
{
    const std::u16string key = lowered(suffix);
    const auto &all = languages();
    for (size_t i = 0; i < all.size(); ++i) {
        if (std::find(all[i].suffixes.begin(), all[i].suffixes.end(), key) != all[i].suffixes.end())
            return int(i) + 1;
    }
    return 0;
}

int languageForFileName(std::u16string_view name)
{
    const auto &all = languages();
    for (size_t i = 0; i < all.size(); ++i) {
        if (std::find(all[i].fileNames.begin(), all[i].fileNames.end(), name) != all[i].fileNames.end())
            return int(i) + 1;
    }
    const size_t dot = name.rfind(u'.');
    return dot == std::u16string_view::npos || dot == 0 ? 0 : languageForSuffix(name.substr(dot + 1));
}

std::u16string languageName(int language)
{
    const Language *l = languageAt(language);
    return l ? l->name : std::u16string();
}

int highlightLine(int language, std::u16string_view line, int state, std::vector<Span> &spans)
{
    const Language *l = languageAt(language);
    if (!l)
        return 0;
    switch (l->kind) {
    case Kind::Markup:
        return highlightMarkup(line, state, spans);
    case Kind::Ini:
        return highlightIni(*l, line, spans);
    case Kind::Diff:
        return highlightDiff(line, spans);
    case Kind::Generic:
        break;
    }
    return highlightGeneric(*l, line, state, spans);
}

} // namespace syntax
