#include "TestHarness.h"
#include "format/CodeFormatters.h"

#include <string>

using namespace fmt;

TEST_CASE(json_pretty_keeps_key_order)
{
    const Result r = formatJson(u"{\"b\":1,\"a\":[true,null,{\"x\":\"{,}\"}],\"e\":{}}", 2);
    CHECK(r.ok);
    CHECK(r.text == u"{\n"
                    u"  \"b\": 1,\n"
                    u"  \"a\": [\n"
                    u"    true,\n"
                    u"    null,\n"
                    u"    {\n"
                    u"      \"x\": \"{,}\"\n"
                    u"    }\n"
                    u"  ],\n"
                    u"  \"e\": {}\n"
                    u"}");
}

TEST_CASE(json_minify_and_numbers)
{
    const Result r = formatJson(u"{ \"n\" : -1.5e+3 ,\n \"s\": \"a \\\" b\" }", 4, true);
    CHECK(r.ok);
    CHECK(r.text == u"{\"n\":-1.5e+3,\"s\":\"a \\\" b\"}");
}

TEST_CASE(json_errors)
{
    CHECK(!formatJson(u"{\"a\" 1}", 2).ok);
    CHECK(!formatJson(u"[1, 2", 2).ok);
    CHECK(!formatJson(u"{\"a\": tru}", 2).ok);
    CHECK(!formatJson(u"{} extra", 2).ok);
    CHECK(!formatJson(u"", 2).ok);
    const Result r = formatJson(u"[1, 01x]", 2);
    CHECK(!r.ok);
    CHECK(r.errorOffset == 4);
}

TEST_CASE(css_formatting)
{
    const Result r = formatCss(u"a:hover,b{color:red;margin : 0 auto}/* note */@media (max-width:600px){p{font:12px \"A;B\"}}", 2);
    CHECK(r.ok);
    CHECK(r.text == u"a:hover,b {\n"
                    u"  color: red;\n"
                    u"  margin: 0 auto;\n"
                    u"}\n"
                    u"\n"
                    u"/* note */\n"
                    u"@media (max-width:600px) {\n"
                    u"  p {\n"
                    u"    font: 12px \"A;B\";\n"
                    u"  }\n"
                    u"}");
    CHECK(!formatCss(u"a { color: red;", 2).ok);
}

TEST_CASE(clike_reindent)
{
    const Result r = formatCLike(u"#include <x>\n"
                                 u"int main() {\n"
                                 u"if (a) {\n"
                                 u"      call(\"}\", '{'); // }\n"
                                 u"  }\n"
                                 u"/* block {\n"
                                 u"* still comment */\n"
                                 u"return f(1,\n"
                                 u"2);\n"
                                 u"}",
                                 4);
    CHECK(r.ok);
    CHECK(r.text == u"#include <x>\n"
                    u"int main() {\n"
                    u"    if (a) {\n"
                    u"        call(\"}\", '{'); // }\n"
                    u"    }\n"
                    u"    /* block {\n"
                    u"     * still comment */\n"
                    u"    return f(1,\n"
                    u"        2);\n"
                    u"}");
}

TEST_CASE(python_indentation)
{
    const Result r = formatPython(u"def f(x):\n"
                                  u"  if x:\n"
                                  u"      return [1,\n"
                                  u"              2]\n"
                                  u"  # comment\n"
                                  u"  s = \"\"\"\n"
                                  u"   keep   \n"
                                  u"\"\"\"\n"
                                  u"  return s   \n",
                                  4);
    CHECK(r.ok);
    CHECK(r.text == u"def f(x):\n"
                    u"    if x:\n"
                    u"        return [1,\n"
                    u"                2]\n"
                    u"    # comment\n"
                    u"    s = \"\"\"\n"
                    u"   keep   \n"
                    u"\"\"\"\n"
                    u"    return s\n");
    CHECK(!formatPython(u"if a:\n    b\n  c\n", 4).ok);
}

TEST_CASE(table_alignment)
{
    const auto lines = formatTable({u"| a | long header |", u"|:-:|--:|", u"| wide cell | 1 |", u"| x |"});
    CHECK(lines.size() == 4);
    CHECK(lines[0] == u"|     a     | long header |");
    CHECK(lines[1] == u"| :-------: | ----------: |");
    CHECK(lines[2] == u"| wide cell |           1 |");
    CHECK(lines[3] == u"|     x     |             |");
}
