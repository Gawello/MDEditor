#include "TestHarness.h"
#include "app/Bookmarks.h"
#include "app/EditorTab.h"
#include "app/FileActions.h"
#include "app/MainWindow.h"
#include "document/Document.h"
#include "editor/MarkdownEditor.h"

#include <QAction>
#include <QDir>
#include <QApplication>
#include <QFile>
#include <QKeyEvent>
#include <QMap>
#include <QMenuBar>
#include <QMenu>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextLayout>

#include <iostream>
#include <memory>

namespace {

QString plain(QString text)
{
    text = text.section(u'\t', 0, 0);
    return text.remove(u'&');
}

QAction *findAction(QMenu *menu, const QString &text)
{
    for (QAction *action : menu->actions()) {
        if (plain(action->text()) == text)
            return action;
    }
    return nullptr;
}

QMenu *findMenu(QMenu *menu, const QString &text)
{
    QAction *action = findAction(menu, text);
    return action ? action->menu() : nullptr;
}

void putCursor(MarkdownEditor &editor, int block, int column)
{
    QTextCursor cursor(editor.document()->findBlockByNumber(block));
    cursor.setPosition(cursor.position() + column);
    editor.setTextCursor(cursor);
}

void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(data);
}

const QString table = QStringLiteral("| a | b |\n| --- | --- |\n| 1 | 2 |\n| 3 | 4 |");

} // namespace

TEST_CASE(context_menu_offers_markdown_commands)
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("text\n\n") + table);
    putCursor(editor, 0, 1);
    std::unique_ptr<QMenu> menu = editor.createContextMenu(editor.cursorRect().center());
    CHECK(findMenu(menu.get(), QStringLiteral("Format")));
    CHECK(findMenu(menu.get(), QStringLiteral("Paragraph")));
    CHECK(findMenu(menu.get(), QStringLiteral("Insert")));
    CHECK(findAction(menu.get(), QStringLiteral("Add Link")));
    CHECK(!findMenu(menu.get(), QStringLiteral("Table"))); // not in a table

    putCursor(editor, 4, 3);
    menu = editor.createContextMenu(editor.cursorRect().center());
    QMenu *tableMenu = findMenu(menu.get(), QStringLiteral("Table"));
    CHECK(tableMenu);
    if (tableMenu) {
        CHECK(findAction(tableMenu, QStringLiteral("Delete Row"))->isEnabled());
        CHECK(!findAction(tableMenu, QStringLiteral("Move Column Left"))->isEnabled()); // first column
    }
}

TEST_CASE(context_menu_paragraph_checks_current_style)
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("> ## quoted heading"));
    std::unique_ptr<QMenu> menu = editor.createContextMenu(editor.cursorRect().center());
    QMenu *paragraph = findMenu(menu.get(), QStringLiteral("Paragraph"));
    CHECK(paragraph);
    if (!paragraph)
        return;
    emit paragraph->aboutToShow();
    CHECK(findAction(paragraph, QStringLiteral("Heading 2"))->isChecked());
    CHECK(findAction(paragraph, QStringLiteral("Quote"))->isChecked());
    CHECK(!findAction(paragraph, QStringLiteral("Heading 1"))->isChecked());

    findAction(paragraph, QStringLiteral("Bullet List"))->trigger();
    CHECK(editor.markdown() == QStringLiteral("> - quoted heading"));
}

TEST_CASE(table_actions_edit_rows_and_columns)
{
    MarkdownEditor editor;
    editor.setMarkdown(table);
    putCursor(editor, 2, 2); // "1"

    CHECK(editor.tableAction(MarkdownEditor::TableAction::RowBelow));
    CHECK(editor.textCursor().blockNumber() == 3); // in the new row
    CHECK(editor.document()->blockCount() == 5);

    putCursor(editor, 4, 7); // "4"
    CHECK(editor.tableAction(MarkdownEditor::TableAction::MoveRowUp));
    CHECK(editor.textCursor().blockNumber() == 3);
    CHECK(editor.document()->findBlockByNumber(3).text().contains(u'4'));

    CHECK(editor.tableAction(MarkdownEditor::TableAction::DeleteColumn)); // column b
    CHECK(!editor.markdown().contains(u'b'));
    CHECK(editor.markdown().contains(u'a'));

    editor.document()->undo(); // one undo step per action
    CHECK(editor.markdown().contains(u'b'));

    putCursor(editor, 0, 2);
    CHECK(!editor.tableAction(MarkdownEditor::TableAction::DeleteRow)); // the header stays
    CHECK(editor.tableAction(MarkdownEditor::TableAction::DeleteTable));
    CHECK(editor.markdown().isEmpty());
}

TEST_CASE(line_style_keeps_the_caret_in_its_text)
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("- item"));
    putCursor(editor, 0, 4); // before "em"
    editor.setLineStyle(md::LineStyle::Heading, 1);
    CHECK(editor.markdown() == QStringLiteral("# item"));
    CHECK(editor.textCursor().positionInBlock() == 4);
}

TEST_CASE(links_and_footnotes)
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("see docs"));
    QTextCursor cursor = editor.textCursor();
    cursor.setPosition(4);
    cursor.setPosition(8, QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);
    editor.insertLink(false);
    CHECK(editor.markdown() == QStringLiteral("see [docs]()"));
    CHECK(editor.textCursor().position() == 11); // between the parentheses

    editor.setMarkdown(QStringLiteral("a[^1] b\n\n[^1]: one"));
    putCursor(editor, 0, 7);
    editor.insertSnippet(MarkdownEditor::Snippet::Footnote);
    CHECK(editor.markdown() == QStringLiteral("a[^1] b[^2]\n\n[^1]: one\n[^2]: "));
    CHECK(editor.textCursor().atEnd());
}

TEST_CASE(plain_text_mode_shows_text_as_is)
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("# not a heading\n**x**"));
    CHECK(!editor.document()->firstBlock().layout()->formats().isEmpty());
    editor.setMarkdownMode(false);
    CHECK(!editor.document()->isModified());
    for (QTextBlock b = editor.document()->firstBlock(); b.isValid(); b = b.next())
        CHECK(b.layout()->formats().isEmpty() && !b.userData());

    std::unique_ptr<QMenu> menu = editor.createContextMenu(QPoint(5, 5));
    CHECK(!findMenu(menu.get(), QStringLiteral("Format")));
    CHECK(findAction(menu.get(), QStringLiteral("Undo")));

    editor.setMarkdownMode(true);
    CHECK(!editor.document()->firstBlock().next().layout()->formats().isEmpty());
}

TEST_CASE(tabs_pick_the_mode_from_the_file_type)
{
    QTemporaryDir dir;
    const QString script = dir.filePath(QStringLiteral("tool.py"));
    const QString note = dir.filePath(QStringLiteral("note.md"));
    writeFile(script, "# comment\nprint(1)\n");
    writeFile(note, "# Title\n");

    EditorTab tab;
    QString error;
    CHECK(tab.load(script, error));
    CHECK(!tab.editor()->markdownMode());
    CHECK(tab.load(note, error));
    CHECK(tab.editor()->markdownMode());

    tab.setFilePath(dir.filePath(QStringLiteral("note.txt"))); // renamed elsewhere in the app
    CHECK(!tab.editor()->markdownMode());
    CHECK(!tab.isModified());
}

TEST_CASE(binary_files_are_not_loaded)
{
    QTemporaryDir dir;
    const QString image = dir.filePath(QStringLiteral("image.png"));
    writeFile(image, QByteArray("\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16));
    const LoadedFile file = Document::read(image);
    CHECK(!file.ok);
    CHECK(file.binary);

    EditorTab tab;
    QString error;
    bool binary = false;
    CHECK(!tab.load(image, error, &binary));
    CHECK(binary);
}

TEST_CASE(file_actions_rename_copy_and_create)
{
    QTemporaryDir dir;
    const QString note = dir.filePath(QStringLiteral("a.md"));
    writeFile(note, "x");

    const FileActions::Result copy = FileActions::duplicate(note, nullptr);
    CHECK(copy.ok);
    CHECK(copy.path == dir.filePath(QStringLiteral("a 1.md")));
    CHECK(FileActions::duplicate(note, nullptr).path == dir.filePath(QStringLiteral("a 2.md")));

    CHECK(!FileActions::rename(note, copy.path, nullptr).ok); // never replaces another file
    const FileActions::Result renamed = FileActions::rename(note, dir.filePath(QStringLiteral("b.md")), nullptr);
    CHECK(renamed.ok);
    CHECK(QFile::exists(dir.filePath(QStringLiteral("b.md"))));

    CHECK(FileActions::createFile(dir.filePath(QStringLiteral("c.md")), nullptr).ok);
    CHECK(!FileActions::createFile(dir.filePath(QStringLiteral("c.md")), nullptr).ok);
    CHECK(FileActions::createFolder(dir.filePath(QStringLiteral("sub")), nullptr).ok);

    CHECK(FileActions::isSameOrInside(dir.filePath(QStringLiteral("sub/x.md")), dir.filePath(QStringLiteral("sub"))));
    CHECK(!FileActions::isSameOrInside(dir.filePath(QStringLiteral("subway.md")), dir.filePath(QStringLiteral("sub"))));
    CHECK(!FileActions::checkName(QStringLiteral("a/b")).isEmpty());
    CHECK(FileActions::checkName(QStringLiteral("notes 2024")).isEmpty());
}

// QPlainTextEdit takes Ctrl+K (delete to end of line on X11) before the window's Add External Link.
TEST_CASE(ctrl_k_is_left_to_the_window_in_markdown)
{
    MarkdownEditor editor;
    QKeyEvent override(QEvent::ShortcutOverride, Qt::Key_K, Qt::ControlModifier);
    QApplication::sendEvent(&editor, &override);
    CHECK(!override.isAccepted());
}

TEST_CASE(bookmarks_follow_moves_and_deletes)
{
    // Absolute paths on every system ("/notes" would become "C:/notes" on Windows).
    const QString base = QDir::cleanPath(QDir::temp().absoluteFilePath(QStringLiteral("notes")));
    const auto at = [&](const QString &relative) { return base + u'/' + relative; };
    Bookmarks::setOrder({});
    Bookmarks::add(at(QStringLiteral("a.md")));
    Bookmarks::add(at(QStringLiteral("sub")));
    Bookmarks::add(at(QStringLiteral("sub/b.md")));
    Bookmarks::add(at(QStringLiteral("a.md"))); // once only
    CHECK(Bookmarks::list().size() == 3);

    Bookmarks::moved(at(QStringLiteral("sub")), at(QStringLiteral("other")));
    CHECK(Bookmarks::list()
          == (QStringList{at(QStringLiteral("a.md")), at(QStringLiteral("other")), at(QStringLiteral("other/b.md"))}));
    Bookmarks::removed(at(QStringLiteral("other")));
    CHECK(Bookmarks::list() == QStringList{at(QStringLiteral("a.md"))});
    Bookmarks::toggle(at(QStringLiteral("a.md")));
    CHECK(Bookmarks::list().isEmpty());
}

TEST_CASE(file_actions_move_into_another_folder)
{
    QTemporaryDir dir;
    QDir(dir.path()).mkdir(QStringLiteral("sub"));
    const QString note = dir.filePath(QStringLiteral("a.md"));
    writeFile(note, "x");
    const FileActions::Result moved = FileActions::move(note, dir.filePath(QStringLiteral("sub/a.md")), nullptr);
    CHECK(moved.ok);
    CHECK(!QFile::exists(note) && QFile::exists(dir.filePath(QStringLiteral("sub/a.md"))));
    writeFile(note, "y");
    CHECK(!FileActions::move(note, dir.filePath(QStringLiteral("sub/a.md")), nullptr).ok); // never replaces
}

namespace {

// The mnemonic letter of a menu entry ("Paste as Plain T&ext" -> 'e'), 0 if none. "&&" is a literal '&'.
QChar mnemonic(const QString &text)
{
    for (int i = 0; i + 1 < text.size(); ++i) {
        if (text[i] != u'&')
            continue;
        if (text[i + 1] == u'&') {
            ++i;
            continue;
        }
        return text[i + 1].toLower();
    }
    return {};
}

void checkMnemonics(QMenu *menu)
{
    QMap<QChar, QString> seen;
    for (QAction *action : menu->actions()) {
        const QChar key = mnemonic(action->text().section(u'\t', 0, 0));
        if (!key.isNull()) {
            if (seen.contains(key))
                std::cerr << "  duplicate &" << QString(key).toStdString() << ": " << seen[key].toStdString() << " / "
                          << action->text().toStdString() << "\n";
            CHECK(!seen.contains(key));
            seen.insert(key, action->text());
        }
        if (action->menu())
            checkMnemonics(action->menu());
    }
}

} // namespace

TEST_CASE(context_menu_mnemonics_are_unique)
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("[link](http://x)\n\n") + table);
    putCursor(editor, 4, 2);
    std::unique_ptr<QMenu> inTable = editor.createContextMenu(editor.cursorRect().center());
    checkMnemonics(inTable.get());
    putCursor(editor, 0, 2);
    std::unique_ptr<QMenu> onLink = editor.createContextMenu(editor.cursorRect().center());
    checkMnemonics(onLink.get());
}

// F2 is the window's Rename; the editor must not keep it.
TEST_CASE(f2_is_left_to_the_window)
{
    MarkdownEditor editor;
    QKeyEvent override(QEvent::ShortcutOverride, Qt::Key_F2, Qt::NoModifier);
    QApplication::sendEvent(&editor, &override);
    CHECK(!override.isAccepted());
}

TEST_CASE(main_menu_mnemonics_are_unique)
{
    MainWindow window;
    QMap<QChar, QString> menus;
    for (QAction *action : window.menuBar()->actions()) {
        const QChar key = mnemonic(action->text());
        CHECK(!key.isNull() && !menus.contains(key));
        menus.insert(key, action->text());
        if (action->menu())
            checkMnemonics(action->menu());
    }
}
