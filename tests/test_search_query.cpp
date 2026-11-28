#include "TestHarness.h"
#include "notes/SearchQuery.h"
#include "panels/SearchPanel.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

namespace {

const QString meeting = QStringLiteral("---\ntags: [work]\n---\n# Meeting\nWe talked about the budget.\n\n## Actions\n"
                                       "- [ ] send the report\n- [x] book a room\n\n## Notes\nThe report is late. #urgent\n");
const QString recipe = QStringLiteral("# Pancakes\nFlour, milk and eggs.\n- [ ] buy eggs\n");

bool hit(const QString &query, const QString &path, const QString &text)
{
    return SearchQuery::parse(query).matches(path, text);
}

} // namespace

TEST_CASE(search_syntax_like_obsidian)
{
    CHECK(SearchQuery::hasOperators(QStringLiteral("tag:work")) && SearchQuery::hasOperators(QStringLiteral("\"a b\""))
          && SearchQuery::hasOperators(QStringLiteral("a -b")) && SearchQuery::hasOperators(QStringLiteral("a OR b")));
    CHECK(!SearchQuery::hasOperators(QStringLiteral("plain words")) && !SearchQuery::hasOperators(QStringLiteral("x-y")));

    CHECK(hit("budget report", "work/Meeting.md", meeting));          // all words, anywhere
    CHECK(!hit("budget eggs", "work/Meeting.md", meeting));
    CHECK(hit("\"talked about the budget\"", "a.md", meeting));
    CHECK(!hit("\"budget talked\"", "a.md", meeting));
    CHECK(hit("report -eggs", "a.md", meeting) && !hit("report -late", "a.md", meeting));
    CHECK(hit("eggs OR budget", "a.md", meeting) && hit("eggs OR budget", "b.md", recipe));
    CHECK(hit("path:work", "work/Meeting.md", meeting) && !hit("path:work", "food/Pancakes.md", recipe));
    CHECK(hit("file:pan", "food/Pancakes.md", recipe) && !hit("file:food", "food/Pancakes.md", recipe));
    CHECK(hit("tag:work", "a.md", meeting) && hit("tag:#urgent", "a.md", meeting) && !hit("tag:work", "b.md", recipe));
    CHECK(hit("line:(report late)", "a.md", meeting) && !hit("line:(report room)", "a.md", meeting));
    CHECK(hit("section:(report late)", "a.md", meeting) && !hit("section:(budget late)", "a.md", meeting));
    CHECK(hit("task:report", "a.md", meeting) && hit("task-todo:report", "a.md", meeting));
    CHECK(!hit("task-done:report", "a.md", meeting) && hit("task-done:room", "a.md", meeting));
    CHECK(hit("task-todo:eggs", "b.md", recipe) && !hit("task:flour", "b.md", recipe));
    CHECK(hit("match-case:Meeting", "a.md", meeting) && !hit("match-case:meeting", "a.md", meeting));
    CHECK(hit("(eggs OR budget) path:work", "work/Meeting.md", meeting));
    CHECK(hit("-tag:work eggs", "b.md", recipe) && !hit("-tag:work report", "a.md", meeting));

    // The lines shown for a match.
    const QList<SearchQuery::Hit> lines = SearchQuery::parse(QStringLiteral("line:(report late)")).hits(meeting);
    CHECK(!lines.isEmpty() && lines[0].line == 7); // "- [ ] send the report" holds "report" too
}

TEST_CASE(search_panel_runs_obsidian_queries)
{
    QTemporaryDir dir;
    const auto write = [&dir](const QString &name, const QString &text) {
        QDir().mkpath(QFileInfo(dir.filePath(name)).absolutePath());
        QFile file(dir.filePath(name));
        CHECK(file.open(QIODevice::WriteOnly));
        file.write(text.toUtf8());
        return QDir::cleanPath(dir.filePath(name));
    };
    const QString a = write(QStringLiteral("work/Meeting.md"), meeting);
    const QString b = write(QStringLiteral("food/Pancakes.md"), recipe);
    SearchPanel::Query query{QStringLiteral("path:food eggs"), false, false, dir.path()};
    QList<SearchPanel::FileMatches> found = SearchPanel::run({a, b}, query, nullptr);
    CHECK(found.size() == 1 && found[0].path == b && found[0].matches.size() == 2);
    query.text = QStringLiteral("tag:work");
    found = SearchPanel::run({a, b}, query, nullptr);
    CHECK(found.size() == 1 && found[0].path == a);
}
