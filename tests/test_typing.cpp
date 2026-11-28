#include "TestHarness.h"
#include "app/AppSettings.h"
#include "app/MainWindow.h"
#include "editor/CompletionPopup.h"
#include "editor/MarkdownEditor.h"
#include "markdown/LineStyle.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDragEnterEvent>
#include <QFile>
#include <QKeyEvent>
#include <QMenu>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>
#include <QMimeData>
#include <QTextBlock>

namespace {

void type(MarkdownEditor &editor, const QString &text)
{
    for (const QChar c : text) {
        QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
        QApplication::sendEvent(&editor, &press);
    }
}

void key(MarkdownEditor &editor, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QKeyEvent press(QEvent::KeyPress, key, modifiers);
    QApplication::sendEvent(&editor, &press);
}

QString text(const MarkdownEditor &editor)
{
    return editor.markdown();
}

// The text with '|' where the cursor is (and [ ] around a selection).
QString shown(const MarkdownEditor &editor)
{
    QString result = editor.markdown();
    const QTextCursor cursor = editor.textCursor();
    if (!cursor.hasSelection())
        return result.insert(cursor.position(), u'|');
    result.insert(cursor.selectionEnd(), u']');
    return result.insert(cursor.selectionStart(), u'[');
}

void select(MarkdownEditor &editor, int from, int to)
{
    QTextCursor cursor(editor.document());
    cursor.setPosition(from);
    cursor.setPosition(to, QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);
}

void setPairing(bool brackets, bool markdown)
{
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::autoPairBracketsKey), brackets);
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::autoPairMarkdownKey), markdown);
}

} // namespace

TEST_CASE(typing_closes_brackets_and_quotes)
{
    setPairing(true, true);
    MarkdownEditor editor;
    type(editor, "see (");
    CHECK(shown(editor) == "see (|)");
    type(editor, "x)");
    CHECK(shown(editor) == "see (x)|");
    type(editor, " \"hi\" don't [[");
    CHECK(shown(editor) == "see (x) \"hi\" don't [[|]]");
    type(editor, "Note]]");
    CHECK(shown(editor) == "see (x) \"hi\" don't [[Note]]|");

    // One undo step takes a pair back; Backspace in an empty pair removes both.
    editor.setMarkdown(QString());
    type(editor, "(");
    editor.undo();
    CHECK(text(editor).isEmpty());
    type(editor, "a `");
    CHECK(shown(editor) == "a `|`");
    key(editor, Qt::Key_Backspace);
    CHECK(shown(editor) == "a |");
    type(editor, "(word");
    key(editor, Qt::Key_Backspace);
    CHECK(shown(editor) == "a (wor|)"); // not an empty pair: one character
}

TEST_CASE(typing_wraps_a_selection)
{
    setPairing(true, true);
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("make this bold"));
    select(editor, 5, 9);
    type(editor, "*");
    CHECK(shown(editor) == "make *[this]* bold");
    type(editor, "*");
    CHECK(shown(editor) == "make **[this]** bold");
    editor.undo();
    CHECK(shown(editor) == "make *this* bold" || text(editor) == "make *this* bold");
    select(editor, 0, 4);
    type(editor, "(");
    CHECK(shown(editor) == "([make]) *this* bold");
    // Across lines, typing replaces the selection as usual.
    editor.setMarkdown(QStringLiteral("a\nb"));
    select(editor, 0, 3);
    type(editor, "(");
    CHECK(text(editor) == "()" || text(editor) == "(");
}

TEST_CASE(pairing_follows_the_settings_and_the_mode)
{
    MarkdownEditor editor;
    setPairing(false, false);
    type(editor, "(\"[");
    CHECK(shown(editor) == "(\"[|");

    // Plain-text files: brackets yes, Markdown markers no.
    setPairing(true, true);
    editor.setMarkdown(QString());
    editor.setMarkdownMode(false);
    type(editor, "f(");
    CHECK(shown(editor) == "f(|)");
    editor.setMarkdown(QStringLiteral("x = y"));
    select(editor, 0, 1);
    type(editor, "*");
    CHECK(text(editor) == "* = y");

    // Shortcuts aren't typing.
    editor.setMarkdown(QString());
    QKeyEvent ctrl(QEvent::KeyPress, Qt::Key_ParenLeft, Qt::ControlModifier, QStringLiteral("("));
    QApplication::sendEvent(&editor, &ctrl);
    CHECK(text(editor) != "()");
    setPairing(true, true);
}

TEST_CASE(moving_and_deleting_lines)
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("a\nbb\nc"));
    QTextCursor cursor(editor.document()->findBlockByNumber(1));
    cursor.movePosition(QTextCursor::Right);
    editor.setTextCursor(cursor);
    CHECK(editor.moveLines(-1));
    CHECK(shown(editor) == "b|b\na\nc");
    CHECK(!editor.moveLines(-1)); // already first
    CHECK(editor.moveLines(1) && editor.moveLines(1));
    CHECK(shown(editor) == "a\nc\nb|b");
    CHECK(!editor.moveLines(1));
    editor.undo();
    CHECK(text(editor) == "a\nbb\nc");

    // A selection over two lines moves both and stays selected.
    editor.setMarkdown(QStringLiteral("1\n2\n3\n4"));
    select(editor, 2, 5); // "2\n3"
    CHECK(editor.moveLines(1));
    CHECK(shown(editor) == "1\n4\n[2\n3]");

    // A folded section moves past as a whole.
    editor.setMarkdown(QStringLiteral("# A\nx\n# B\ny"));
    QCoreApplication::processEvents();
    CHECK(editor.toggleFold(editor.document()->findBlockByNumber(0)));
    editor.setTextCursor(QTextCursor(editor.document()->findBlockByNumber(2)));
    CHECK(editor.moveLines(-1));
    CHECK(text(editor) == "# B\n# A\nx\ny");

    // Deleting: a middle line, the last line, the only line.
    editor.setMarkdown(QStringLiteral("a\nb\nc"));
    editor.setTextCursor(QTextCursor(editor.document()->findBlockByNumber(1)));
    editor.deleteLines();
    CHECK(shown(editor) == "a\n|c");
    editor.deleteLines();
    CHECK(text(editor) == "a");
    editor.deleteLines();
    CHECK(text(editor).isEmpty());
    editor.undo();
    CHECK(text(editor) == "a");
}

TEST_CASE(toggling_checkboxes)
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("buy milk"));
    editor.moveCursor(QTextCursor::End);
    editor.transformLines(md::toggleCheckbox);
    CHECK(shown(editor) == "- [ ] buy milk|");
    editor.transformLines(md::toggleCheckbox);
    CHECK(shown(editor) == "- [x] buy milk|");
    editor.setMarkdown(QStringLiteral("- a\n- [x] b\nc"));
    editor.selectAll();
    editor.transformLines(md::toggleCheckbox);
    CHECK(text(editor) == "- [ ] a\n- [ ] b\n- [ ] c");
    editor.undo();
    CHECK(text(editor) == "- a\n- [x] b\nc");
}

TEST_CASE(line_shortcuts_reach_the_window)
{
    MarkdownEditor editor;
    for (const QKeyCombination combination : {QKeyCombination(Qt::CTRL, Qt::Key_D), QKeyCombination(Qt::CTRL, Qt::Key_L),
                                              QKeyCombination(Qt::ALT, Qt::Key_Up), QKeyCombination(Qt::ALT, Qt::Key_Down)}) {
        QKeyEvent override(QEvent::ShortcutOverride, combination.key(), combination.keyboardModifiers());
        QApplication::sendEvent(&editor, &override);
        CHECK(!override.isAccepted());
    }
}

TEST_CASE(pasting_html_gives_markdown)
{
    const QString web = QStringLiteral(
        "<h2>Title</h2><p>Some <b>bold</b>, <i>slanted</i> and <a href=\"https://x.org/a\">a link</a>.</p>"
        "<ul><li>one</li><li>two</li></ul><table><tr><th>A</th><th>B</th></tr><tr><td>1</td><td>2</td></tr></table>"
        "<pre><code>let x = 1;</code></pre>");
    const QString markdown = MarkdownEditor::htmlToMarkdown(web);
    CHECK(markdown.contains("## Title"));
    CHECK(markdown.contains("**bold**") && markdown.contains("*slanted*"));
    CHECK(markdown.contains("[a link](https://x.org/a)"));
    CHECK(markdown.contains("- one") && markdown.contains("- two"));
    CHECK(markdown.contains("|A|B|") || markdown.contains("| A | B |") || markdown.contains("|A |B |"));
    CHECK(markdown.contains("let x = 1;"));
    const QString code = MarkdownEditor::htmlToMarkdown(QStringLiteral("<pre><code>a\n\nb\nc</code></pre><ol><li>x</li></ol>"));
    CHECK(code.contains("```\na\n\nb\nc\n```") || code.contains("```\na\n\nb\nc\n\n```"));
    CHECK(code.contains("\n1. x"));
    CHECK(!markdown.contains("\n\n\n") && !markdown.endsWith(u'\n'));
    // Plain text in tags isn't converted (it pastes as it is).
    CHECK(MarkdownEditor::htmlToMarkdown(QStringLiteral("<html><body><p>just text</p></body></html>")).isEmpty());
    // Word: bold by style.
    CHECK(MarkdownEditor::htmlToMarkdown(QStringLiteral("<p><span style='font-weight:bold'>W</span> x</p>")).contains("**W**"));

    const auto copyHtml = [&web] {
        auto *data = new QMimeData;
        data->setHtml(web);
        data->setText(QStringLiteral("Title Some bold"));
        QGuiApplication::clipboard()->setMimeData(data);
    };
    MarkdownEditor editor;
    copyHtml();
    editor.paste();
    CHECK(text(editor).startsWith("## Title"));
    editor.undo(); // one step
    CHECK(text(editor).isEmpty());
    // Turned off, or in a plain-text file: the text.
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::convertPastedHtmlKey), false);
    copyHtml();
    editor.paste();
    CHECK(text(editor) == "Title Some bold");
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::convertPastedHtmlKey), true);
    editor.setMarkdown(QString());
    editor.setMarkdownMode(false);
    copyHtml();
    editor.paste();
    CHECK(text(editor) == "Title Some bold");
}

TEST_CASE(copying_gives_the_markdown_as_plain_text)
{
    MarkdownEditor editor;
    const QString note = QStringLiteral("# Head\n**bold**") + QChar(0x00A0) + QStringLiteral("x");
    editor.setMarkdown(note);
    editor.selectAll();
    editor.copy();
    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    CHECK(data && !data->hasHtml());
    CHECK(data && data->text() == note); // the non-breaking space too
}

namespace {

void dropFiles(MarkdownEditor &editor, const QStringList &files, const QPoint &at)
{
    QMimeData data;
    QList<QUrl> urls;
    for (const QString &file : files)
        urls << QUrl::fromLocalFile(file);
    data.setUrls(urls);
    QDragEnterEvent enter(at, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(editor.viewport(), &enter);
    QDragMoveEvent move(at, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(editor.viewport(), &move);
    QDropEvent drop(QPointF(at), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(editor.viewport(), &drop);
}

} // namespace

TEST_CASE(dropped_files_become_links)
{
    QTemporaryDir dir;
    const auto touch = [&dir](const QString &name) {
        QDir().mkpath(QFileInfo(dir.filePath(name)).absolutePath());
        QFile file(dir.filePath(name));
        CHECK(file.open(QIODevice::WriteOnly));
        return QDir::cleanPath(dir.filePath(name));
    };
    const QString report = touch(QStringLiteral("docs/Q1 report.pdf"));
    const QString note = touch(QStringLiteral("Other.md"));
    MarkdownEditor::setLinkResolver([](const QString &target, bool, const QString &from) {
        const QString path = QFileInfo(from).dir().filePath(target + QStringLiteral(".md"));
        return QFileInfo::exists(path) ? QDir::cleanPath(path) : QString();
    });
    MarkdownEditor editor;
    editor.setAttribute(Qt::WA_DontShowOnScreen);
    editor.resize(600, 400);
    editor.show();
    editor.setDocumentPath(dir.filePath(QStringLiteral("Here.md")));
    editor.setMarkdown(QStringLiteral("See"));
    editor.moveCursor(QTextCursor::End);
    dropFiles(editor, {report, note}, editor.cursorRect().center());
    CHECK(text(editor) == "See [Q1 report.pdf](docs/Q1%20report.pdf) [[Other]]");
    editor.undo();
    CHECK(text(editor) == "See");

    // Plain-text files open what's dropped instead.
    QStringList opened;
    QObject::connect(&editor, &MarkdownEditor::filesDropped, [&opened](const QStringList &files) { opened = files; });
    editor.setMarkdownMode(false);
    dropFiles(editor, {report}, editor.cursorRect().center());
    CHECK(opened == QStringList{report} && text(editor) == "See");
    MarkdownEditor::setLinkResolver({});
}

TEST_CASE(search_for_the_selection)
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("find this word\nnext"));
    select(editor, 5, 9);
    std::unique_ptr<QMenu> menu = editor.createContextMenu(editor.cursorRect().center());
    QAction *search = nullptr;
    for (QAction *action : menu->actions()) {
        if (action->text().remove(u'&') == QStringLiteral("Search for “this”"))
            search = action;
    }
    CHECK(search != nullptr);
    QString asked;
    QObject::connect(&editor, &MarkdownEditor::searchRequested, [&asked](const QString &text) { asked = text; });
    if (search)
        search->trigger();
    CHECK(asked == "this");
    // Mnemonics stay unique with it.
    QSet<QChar> keys;
    for (QAction *action : menu->actions()) {
        const QString label = action->text().section(u'\t', 0, 0);
        const qsizetype amp = label.indexOf(u'&');
        if (amp >= 0 && amp + 1 < label.size() && label[amp + 1] != u'&') {
            CHECK(!keys.contains(label[amp + 1].toLower()));
            keys.insert(label[amp + 1].toLower());
        }
    }
    // Several lines: no entry.
    select(editor, 0, 17);
    menu = editor.createContextMenu(editor.cursorRect().center());
    for (QAction *action : menu->actions())
        CHECK(!action->text().startsWith(QStringLiteral("&Search for")));
}

TEST_CASE(slash_commands_run_menu_commands)
{
    QSettings().remove(QStringLiteral("session"));
    QSettings().remove(QStringLiteral("window"));
    MainWindow window;
    window.show();
    auto *editor = window.findChild<MarkdownEditor *>();
    CHECK(editor != nullptr);
    if (!editor)
        return;
    editor->setFocus();
    type(*editor, "x /bol");
    CHECK(editor->completionPopup() && editor->completionPopup()->isVisible());
    CHECK(editor->completionPopup() && editor->completionPopup()->items().value(0).label == "Bold");
    key(*editor, Qt::Key_Return);
    QCoreApplication::processEvents();
    CHECK(shown(*editor) == "x **|**"); // "/bol" went, then Format → Bold ran
    // Turned off: no list.
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::slashCommandsKey), false);
    type(*editor, " /bol");
    CHECK(!editor->completionPopup()->isVisible());
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::slashCommandsKey), true);
    editor->document()->setModified(false);
    window.close();
}
