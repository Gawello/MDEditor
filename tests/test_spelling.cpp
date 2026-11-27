#include "TestHarness.h"
#include "app/AppSettings.h"
#include "editor/MarkdownEditor.h"
#include "spell/SpellChecker.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QSettings>
#include <QTextBlock>
#include <QTextLayout>

#include <iostream>
#include <memory>

namespace {

bool hasDictionary(const QString &code)
{
    for (const SpellChecker::Dictionary &dictionary : SpellChecker::available()) {
        if (dictionary.code == code)
            return true;
    }
    std::cout << "  (no " << code.toStdString() << " dictionary: skipped)\n";
    return false;
}

void useLanguages(const QString &codes)
{
    AppSettings::instance().setValue(AppSettings::spellLanguagesKey, codes);
    SpellChecker::instance().reload(true);
}

bool underlined(const MarkdownEditor &editor, int block, int column)
{
    for (const QTextLayout::FormatRange &range : editor.document()->findBlockByNumber(block).layout()->formats()) {
        if (column >= range.start && column < range.start + range.length
            && range.format.underlineStyle() == QTextCharFormat::SpellCheckUnderline)
            return true;
    }
    return false;
}

QString plain(QString text)
{
    return text.remove(u'&');
}

} // namespace

TEST_CASE(spelling_checks_words_in_any_enabled_language)
{
    if (!hasDictionary(QStringLiteral("en_US")) || !hasDictionary(QStringLiteral("pl_PL")))
        return;
    SpellChecker &spell = SpellChecker::instance();
    useLanguages(QStringLiteral("en_US,pl_PL"));
    CHECK(spell.isActive());
    CHECK(spell.check(QStringLiteral("hello")));
    CHECK(spell.check(QStringLiteral("zażółć"))); // pl_PL is in ISO-8859-2
    CHECK(spell.check(QStringLiteral("don’t")));  // typographic apostrophe
    CHECK(!spell.check(QStringLiteral("wrold"))); // ("helo" is Polish)
    CHECK(!spell.check(QStringLiteral("gęśla")));
    CHECK(spell.suggestions(QStringLiteral("wrold")).contains(QStringLiteral("world")));
    CHECK(spell.suggestions(QStringLiteral("zażułć")).contains(QStringLiteral("zażółć")));

    const auto ranges = spell.misspelled(QStringLiteral("A tset of HTML, snake_case, 123abc and wrold"),
                                         [](int) { return false; }, 43);
    CHECK(ranges.size() == 1); // "wrold" is being typed (the caret touches it)
    CHECK(ranges.size() == 1 && ranges.front().first == 2 && ranges.front().second == 6);

    useLanguages(QString()); // nothing ticked: off
    CHECK(!spell.isActive());
    CHECK(spell.check(QStringLiteral("wrold")));
}

TEST_CASE(spelling_is_underlined_in_prose_only)
{
    if (!hasDictionary(QStringLiteral("en_US")))
        return;
    useLanguages(QStringLiteral("en_US"));
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("Some speling `codde` [linkk](x) $\\fraq$ %%commnet%%\n\nnext"));
    QTextCursor cursor(editor.document()->lastBlock());
    editor.setTextCursor(cursor);
    CHECK(underlined(editor, 0, 6));   // speling
    CHECK(!underlined(editor, 0, 15)); // code
    CHECK(!underlined(editor, 0, 22)); // link text
    CHECK(!underlined(editor, 0, 40)); // comment

    // Plain text files are checked too; code files aren't.
    editor.setDocumentPath(QDir::temp().filePath(QStringLiteral("notes.txt")));
    editor.setMarkdownMode(false);
    CHECK(underlined(editor, 0, 6));
    editor.setDocumentPath(QDir::temp().filePath(QStringLiteral("tool.py")));
    CHECK(!underlined(editor, 0, 6));
}

TEST_CASE(spelling_menu_replaces_and_learns_words)
{
    if (!hasDictionary(QStringLiteral("en_US")))
        return;
    useLanguages(QStringLiteral("en_US"));
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("A speling test\n\nend"));
    QTextCursor cursor(editor.document()->lastBlock());
    editor.setTextCursor(cursor);

    QTextCursor onWord(editor.document());
    onWord.setPosition(4);
    std::unique_ptr<QMenu> menu = editor.createContextMenu(editor.cursorRect(onWord).center());
    QAction *fix = nullptr;
    QAction *add = nullptr;
    for (QAction *action : menu->actions()) {
        if (plain(action->text()) == QStringLiteral("spelling"))
            fix = action;
        if (plain(action->text()).startsWith(QStringLiteral("Add “speling”")))
            add = action;
    }
    CHECK(fix && add);
    if (fix)
        fix->trigger();
    CHECK(editor.markdown().startsWith(QStringLiteral("A spelling test")));

    // A learned word is kept in the user's words.dic (the test profile's), and no longer underlined.
    editor.setMarkdown(QStringLiteral("Mdeditorish word\n\nend"));
    editor.setTextCursor(QTextCursor(editor.document()->lastBlock()));
    CHECK(underlined(editor, 0, 0));
    SpellChecker::instance().addToDictionary(QStringLiteral("Mdeditorish"));
    CHECK(!underlined(editor, 0, 0));
    QFile::remove(QDir(SpellChecker::userFolder()).filePath(QStringLiteral("words.dic")));
}

TEST_CASE(default_spelling_languages_take_one_dictionary_per_language)
{
    AppSettings::instance().setValue(AppSettings::spellLanguagesKey, QVariant()); // unset: the default
    QSettings().remove(QLatin1String(AppSettings::spellLanguagesKey));
    const QStringList languages = SpellChecker::configuredLanguages();
    QStringList seen;
    for (const QString &code : languages) {
        CHECK(!seen.contains(code.section(u'_', 0, 0)));
        seen << code.section(u'_', 0, 0);
    }
    if (hasDictionary(QStringLiteral("en_US")))
        CHECK(languages.contains(QStringLiteral("en_US")));
}
