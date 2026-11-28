#include "TestHarness.h"
#include "app/AppSettings.h"
#include "app/MainWindow.h"
#include "editor/CompletionPopup.h"
#include "editor/LivePreviewHighlighter.h"
#include "editor/RemoteImages.h"
#include "editor/MarkdownEditor.h"
#include "markdown/LineStyle.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDragEnterEvent>
#include <QFile>
#include <QBuffer>
#include <QElapsedTimer>
#include <QImage>
#include <QTcpServer>
#include <QTcpSocket>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMenu>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>
#include <QMimeData>
#include <QTextBlock>
#include <QTextLayout>

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

TEST_CASE(line_numbers_follow_the_setting)
{
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::lineNumbersKey), false);
    MarkdownEditor editor;
    editor.setAttribute(Qt::WA_DontShowOnScreen);
    editor.resize(800, 400);
    editor.show();
    editor.setMarkdown(QStringLiteral("a\nb\nc"));
    CHECK(editor.lineNumberArea() == nullptr);
    const int before = editor.viewport()->geometry().left();
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::lineNumbersKey), true);
    QWidget *area = editor.lineNumberArea();
    CHECK(area && area->isVisible() && area->width() == editor.lineNumberWidth());
    // Left of the text, never over it.
    CHECK(area && area->geometry().right() < editor.viewport()->geometry().left() + 1);
    CHECK(editor.viewport()->geometry().left() >= before);
    // Wider for more digits.
    const int narrow = editor.lineNumberWidth();
    editor.setMarkdown(QString(QStringLiteral("x\n")).repeated(1200));
    CHECK(editor.lineNumberWidth() > narrow);
    CHECK(!editor.grab().isNull()); // paints
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::lineNumbersKey), false);
    CHECK(editor.lineNumberArea() == nullptr);
}

TEST_CASE(image_width_hints)
{
    CHECK(MarkdownEditor::withImageWidth(QStringLiteral("![[a.png]]"), 300) == "![[a.png|300]]");
    CHECK(MarkdownEditor::withImageWidth(QStringLiteral("![[a b.png|200]]"), 300) == "![[a b.png|300]]");
    CHECK(MarkdownEditor::withImageWidth(QStringLiteral("![[a.png|200]]"), 0) == "![[a.png]]");
    CHECK(MarkdownEditor::withImageWidth(QStringLiteral("![alt](a.png)"), 300) == "![alt|300](a.png)");
    CHECK(MarkdownEditor::withImageWidth(QStringLiteral("![alt|200](a.png)"), 120) == "![alt|120](a.png)");
    CHECK(MarkdownEditor::withImageWidth(QStringLiteral("![alt|200](a.png)"), 0) == "![alt](a.png)");
    CHECK(MarkdownEditor::withImageWidth(QStringLiteral("  ![](a.png)"), 50) == "  ![|50](a.png)");
    CHECK(MarkdownEditor::withImageWidth(QStringLiteral("text ![[a.png]] [[b]]"), 50) == "text ![[a.png]] [[b]]");
}

TEST_CASE(images_resize_by_dragging_and_have_a_menu)
{
    QTemporaryDir dir;
    QImage picture(400, 200, QImage::Format_RGB32);
    picture.fill(Qt::darkCyan);
    CHECK(picture.save(dir.filePath(QStringLiteral("pic.png"))));
    MarkdownEditor editor;
    editor.setAttribute(Qt::WA_DontShowOnScreen);
    editor.resize(900, 600);
    editor.show();
    editor.setDocumentPath(dir.filePath(QStringLiteral("Note.md")));
    editor.setMarkdown(QStringLiteral("top\n![](pic.png)\nend"));
    editor.moveCursor(QTextCursor::End);
    QCoreApplication::processEvents();
    const QTextBlock line = editor.document()->findBlockByNumber(1);
    const auto rect = editor.imageRect(line);
    CHECK(rect.has_value() && std::abs(rect->width() - 400) < 1);
    if (!rect)
        return;

    // Drag the right edge 100 px to the left: the link gets |300, the cursor stays on "end".
    const QPoint grab(int(rect->right()), int(rect->bottom() - 10));
    QMouseEvent press(QEvent::MouseButtonPress, grab, editor.viewport()->mapToGlobal(grab), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(editor.viewport(), &press);
    const QPoint to = grab - QPoint(100, 0);
    QMouseEvent move(QEvent::MouseMove, to, editor.viewport()->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(editor.viewport(), &move);
    QMouseEvent release(QEvent::MouseButtonRelease, to, editor.viewport()->mapToGlobal(to), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(editor.viewport(), &release);
    CHECK(editor.document()->findBlockByNumber(1).text() == "![|300](pic.png)");
    CHECK(editor.textCursor().blockNumber() == 2);
    editor.undo();
    CHECK(editor.document()->findBlockByNumber(1).text() == "![](pic.png)");
    editor.moveCursor(QTextCursor::End); // undo put the cursor on the image's line, which shows it as text

    // The menu on the image: its own entries, and the cursor doesn't go there.
    QCoreApplication::processEvents();
    const auto shownRect = editor.imageRect(editor.document()->findBlockByNumber(1));
    CHECK(shownRect.has_value());
    if (!shownRect)
        return;
    std::unique_ptr<QMenu> menu = editor.createContextMenu(shownRect->center().toPoint());
    QStringList entries;
    for (QAction *action : menu->actions())
        entries << action->text().remove(u'&').section(u'\t', 0, 0);
    CHECK(entries.contains("Open Image") && entries.contains("Copy Image") && entries.contains("Delete Image File…"));
    CHECK(!entries.contains("Open Link"));
    for (QAction *action : menu->actions()) {
        if (action->text().remove(u'&') == QStringLiteral("Reset Size"))
            CHECK(!action->isEnabled()); // no width to reset
    }
    CHECK(editor.textCursor().blockNumber() == 2);
}

TEST_CASE(images_from_the_web)
{
    // A tiny web server with one picture, and one address that fails.
    QImage picture(120, 60, QImage::Format_RGB32);
    picture.fill(Qt::darkMagenta);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    picture.save(&buffer, "PNG");
    QTcpServer server;
    CHECK(server.listen(QHostAddress::LocalHost));
    int requests = 0;
    QObject::connect(&server, &QTcpServer::newConnection, [&server, &png, &requests] {
        QTcpSocket *socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &png, &requests] {
            const QByteArray request = socket->readAll();
            ++requests;
            if (request.startsWith("GET /pic.png")) {
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nCache-Control: no-store\r\nContent-Length: "
                              + QByteArray::number(png.size()) + "\r\nConnection: close\r\n\r\n" + png);
            } else {
                socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            }
            socket->disconnectFromHost();
        });
    });
    const QString base = QStringLiteral("http://127.0.0.1:%1/").arg(server.serverPort());

    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::remoteImagesKey), true);
    MarkdownEditor editor;
    editor.setAttribute(Qt::WA_DontShowOnScreen);
    editor.resize(800, 500);
    editor.show();
    editor.setMarkdown(QStringLiteral("![](%1pic.png)\n![](%1missing.png)\nend").arg(base));
    editor.moveCursor(QTextCursor::End);
    const auto shown = [&editor](int line) {
        const auto *data = static_cast<const BlockData *>(editor.document()->findBlockByNumber(line).userData());
        return data && data->imageShown;
    };
    QElapsedTimer timer;
    timer.start();
    while (!shown(0) && timer.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    CHECK(shown(0));
    CHECK(!shown(1)); // stays a link
    const auto rect = editor.imageRect(editor.document()->findBlockByNumber(0));
    CHECK(rect && std::abs(rect->width() - 120) < 1);
    CHECK(RemoteImages::instance().failed(QUrl(base + QStringLiteral("missing.png"))));
    // Kept: another editor doesn't fetch it again.
    const int before = requests;
    MarkdownEditor other;
    other.setMarkdown(QStringLiteral("![](%1pic.png)\nx").arg(base));
    other.moveCursor(QTextCursor::End);
    QCoreApplication::processEvents();
    CHECK(requests == before);
}

TEST_CASE(colored_highlights_like_obsidian)
{
    const QString red = highlightColors()[0].first;
    const QString blue = highlightColors()[4].first;
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("word here"));
    select(editor, 0, 4);
    editor.toggleHighlightColor(0);
    CHECK(text(editor) == "==" + red + "word== here");
    CHECK(editor.textCursor().selectedText() == "word");
    editor.toggleHighlightColor(4); // another color
    CHECK(text(editor) == "==" + blue + "word== here");
    editor.toggleHighlightColor(4); // the same: off
    CHECK(text(editor) == "word here" && editor.textCursor().selectedText() == "word");
    editor.setMarkdown(QStringLiteral("==word== x"));
    select(editor, 2, 6);
    editor.toggleHighlightColor(0); // a plain highlight gets the color
    CHECK(text(editor) == "==" + red + "word== x");
    editor.undo();
    CHECK(text(editor) == "==word== x");

    // Shown: the emoji hidden (outside the cursor's line), the text on the color.
    editor.setMarkdown("a ==" + red + "warm== b\nnext");
    editor.moveCursor(QTextCursor::End);
    QCoreApplication::processEvents();
    const QTextBlock line = editor.document()->firstBlock();
    QColor background;
    bool emojiHidden = false;
    for (const QTextLayout::FormatRange &range : line.layout()->formats()) {
        if (range.start <= 6 && range.start + range.length > 6) // "w"
            background = range.format.background().color();
        if (range.start <= 4 && range.start + range.length > 4) // the emoji
            emojiHidden = range.format.foreground().color().alpha() == 0;
    }
    CHECK(background.isValid() && background.red() > 200 && background.alpha() < 120);
    CHECK(emojiHidden);
}
