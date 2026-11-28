#include "TestHarness.h"
#include "app/EditorTab.h"
#include "app/MainWindow.h"
#include "editor/MarkdownEditor.h"
#include "editor/NotePreview.h"
#include "notes/NoteIndex.h"
#include "panels/LinksPanel.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeWidget>

namespace {

void writeBytes(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

QByteArray readBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool waitFor(const std::function<bool()> &done, int ms = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

struct Folder {
    QTemporaryDir dir;
    QString path(const QString &relative) const { return QDir::cleanPath(dir.filePath(relative)); }
    Folder()
    {
        writeBytes(path("Alpha.md"), "---\naliases: [Start]\n---\n# Alpha\n## Goals\n- ship ^g1\n## Other\nx\n");
        writeBytes(path("Home.md"), "# Home\nSee [[Alpha]].\nAgain [[Alpha#Goals]] and [[Missing]]\n");
        writeBytes(path("sub/Beta.md"), "# Beta\n[a](../Alpha.md) and ![[Alpha]]\n");
        // Plain mentions: two count (a word, the alias); the rest are a longer word, code, a tag, a link, code
        // block and front matter. Written with a BOM and CRLF, which linking one must keep.
        writeBytes(path("Notes.md"), "\xEF\xBB\xBF---\r\ntitle: alpha\r\n---\r\nI like alpha a lot.\r\n"
                                     "alphabet `alpha` #alpha [[Alpha]]\r\nThe Start of it\r\n```\r\nalpha\r\n```\r\n");
    }
};

NoteIndex *indexOf(const Folder &folder)
{
    auto *index = new NoteIndex;
    index->setRoot(folder.dir.path());
    CHECK(waitFor([index] { return !index->scanning() && index->notes().size() == 4; }));
    return index;
}

QStringList texts(QTreeWidgetItem *item)
{
    QStringList result;
    for (int i = 0; i < item->childCount(); ++i)
        result << item->child(i)->text(0);
    return result;
}

QAction *action(QWidget &window, const QString &text)
{
    for (QAction *candidate : window.findChildren<QAction *>()) {
        if (QString(candidate->text()).remove(u'&') == text)
            return candidate;
    }
    return nullptr;
}

} // namespace

TEST_CASE(index_knows_backlinks)
{
    Folder folder;
    std::unique_ptr<NoteIndex> index(indexOf(folder));
    const QList<NoteIndex::Backlink> links = index->backlinks(folder.path("Alpha.md"));
    CHECK(links.size() == 5); // Home twice, Beta twice (a link and an embed), Notes once
    QSet<QString> sources;
    for (const NoteIndex::Backlink &link : links)
        sources.insert(QFileInfo(link.source).fileName());
    CHECK((sources == QSet<QString>{"Home.md", "Beta.md", "Notes.md"}));
    CHECK(links.size() > 2 && links[1].anchor == "Goals" && links[1].line == 2);
    CHECK(index->backlinks(folder.path("Home.md")).isEmpty());
    CHECK(index->backlinks(folder.path("Missing.md")).isEmpty());
    // Resolving without the disk gives the same answers.
    CHECK(index->resolveIndexed("Alpha", true, folder.path("Home.md")) == folder.path("Alpha.md"));
    CHECK(index->resolveIndexed("../Alpha.md", false, folder.path("sub/Beta.md")) == folder.path("Alpha.md"));
    CHECK(index->resolveIndexed("Start", true, folder.path("Home.md")) == folder.path("Alpha.md"));
    CHECK(index->resolveIndexed("Nope", true, folder.path("Home.md")).isEmpty());
    // A saved change counts right away.
    index->update(folder.path("Home.md"), QStringLiteral("# Home\nnothing\n"));
    CHECK(index->backlinks(folder.path("Alpha.md")).size() == 3);
}

TEST_CASE(backlinks_collect_lines_and_unlinked_mentions)
{
    Folder folder;
    std::unique_ptr<NoteIndex> index(indexOf(folder));
    const QString alpha = folder.path("Alpha.md");
    QStringList notes;
    for (const QString &path : index->files())
        notes << path;
    const BacklinksPanel::Result result =
        BacklinksPanel::collect(alpha, index->backlinks(alpha), notes, {"Alpha", "Start"}, nullptr);
    CHECK(result.linked.size() == 4); // Beta's two links share a line
    CHECK(result.linked.value(0).text.contains("[[Alpha"));
    CHECK(result.unlinked.size() == 2);
    if (result.unlinked.size() == 2) {
        CHECK(result.unlinked[0].line == 3 && result.unlinked[0].column == 7 && result.unlinked[0].length == 5);
        CHECK(result.unlinked[1].text.contains("Start") && result.unlinked[1].column == 4);
    }
}

TEST_CASE(backlinks_panel_and_linking_a_mention)
{
    Folder folder;
    QSettings().remove(QStringLiteral("session"));
    QSettings().remove(QStringLiteral("window")); // a sidebar left open by an earlier test would be toggled off
    MainWindow window;
    window.show();
    CHECK(window.openFile(folder.path("Alpha.md")));
    auto *index = window.findChild<NoteIndex *>();
    CHECK(waitFor([index] { return !index->scanning() && index->notes().size() == 4; }));

    // The status bar counts the notes linking here.
    QToolButton *count = nullptr;
    for (QToolButton *button : window.findChildren<QToolButton *>()) {
        if (button->text().endsWith(QStringLiteral("backlinks")))
            count = button;
    }
    CHECK(count && count->text() == "3 backlinks");

    action(window, QStringLiteral("Backlinks"))->trigger();
    auto *panel = window.findChild<BacklinksPanel *>();
    CHECK(panel && panel->isVisible());
    CHECK(waitFor([panel] { return !panel->busy() && panel->tree()->topLevelItemCount() == 2; }));
    QTreeWidgetItem *linked = panel->tree()->topLevelItem(0);
    QTreeWidgetItem *unlinked = panel->tree()->topLevelItem(1);
    CHECK((texts(linked) == QStringList{"Home", "Notes", "Beta"} || texts(linked).size() == 3));
    CHECK(unlinked->text(1) == "1" && unlinked->childCount() == 1 && unlinked->child(0)->childCount() == 2);

    // Clicking a line opens the note there.
    QTreeWidgetItem *homeLine = nullptr;
    for (int i = 0; i < linked->childCount(); ++i) {
        if (linked->child(i)->text(0) == "Home")
            homeLine = linked->child(i)->child(1);
    }
    CHECK(homeLine != nullptr);
    if (homeLine) {
        emit panel->tree()->itemClicked(homeLine, 0);
        auto *tab = qobject_cast<EditorTab *>(window.findChild<QTabWidget *>()->currentWidget());
        CHECK(tab && tab->filePath() == folder.path("Home.md"));
        CHECK(tab && tab->editor()->textCursor().blockNumber() == 2);
        action(window, QStringLiteral("Back"))->trigger();
    }

    // Back on Alpha, the panel shows its mentions again (opening Home showed Home's).
    CHECK(waitFor([panel, &folder] { return panel->note() == folder.path("Alpha.md") && !panel->busy()
                                            && panel->tree()->topLevelItemCount() == 2; }));
    unlinked = panel->tree()->topLevelItem(1);
    CHECK(unlinked && unlinked->childCount() == 1);
    if (!unlinked || unlinked->childCount() != 1)
        return;

    // Link: the mention becomes [[alpha]] (as written) in the closed note, which keeps its BOM and CRLF.
    QTreeWidgetItem *mention = unlinked->child(0)->child(0);
    auto *link = qobject_cast<QToolButton *>(panel->tree()->itemWidget(mention, 1));
    CHECK(link != nullptr);
    if (link) {
        link->click();
        QCoreApplication::processEvents();
        CHECK(waitFor([&folder] { return readBytes(folder.path("Notes.md")).contains("I like [[alpha]] a lot."); }));
        const QByteArray bytes = readBytes(folder.path("Notes.md"));
        CHECK(bytes.startsWith("\xEF\xBB\xBF") && bytes.count("\r\n") == 9 && !bytes.contains("a\nalpha"));
        CHECK(waitFor([panel] { return !panel->busy() && panel->tree()->topLevelItemCount() == 2
                                       && panel->tree()->topLevelItem(1)->text(1) == "1"
                                       && panel->tree()->topLevelItem(1)->child(0)->childCount() == 1; }));
        // A mention with another text: [[Alpha|Start]].
        link = qobject_cast<QToolButton *>(
            panel->tree()->itemWidget(panel->tree()->topLevelItem(1)->child(0)->child(0), 1));
        CHECK(link != nullptr);
        if (link) {
            link->click();
            CHECK(waitFor([&folder] { return readBytes(folder.path("Notes.md")).contains("The [[Alpha|Start]] of it"); }));
        }
    }
    for (EditorTab *tab : window.findChildren<EditorTab *>())
        tab->editor()->document()->setModified(false);
    window.close();
}

TEST_CASE(changing_open_notes)
{
    Folder folder;
    QSettings().remove(QStringLiteral("session"));
    QSettings().remove(QStringLiteral("window")); // a sidebar left open by an earlier test would be toggled off
    MainWindow window;
    window.show();
    CHECK(window.openFile(folder.path("Home.md")));
    CHECK(window.openFile(folder.path("Alpha.md")));
    auto *index = window.findChild<NoteIndex *>();
    CHECK(waitFor([index] { return !index->scanning() && index->notes().size() == 4; }));
    action(window, QStringLiteral("Backlinks"))->trigger();
    auto *panel = window.findChild<BacklinksPanel *>();
    CHECK(waitFor([panel] { return !panel->busy() && panel->tree()->topLevelItemCount() == 2; }));

    // Notes.md opened and edited (not saved): linking goes into the tab and leaves saving to the user.
    CHECK(window.openFile(folder.path("Notes.md")));
    EditorTab *notes = nullptr;
    for (EditorTab *tab : window.findChildren<EditorTab *>()) {
        if (tab->filePath() == folder.path("Notes.md"))
            notes = tab;
    }
    CHECK(notes != nullptr);
    QTextCursor end(notes->editor()->document());
    end.movePosition(QTextCursor::End);
    end.insertText(QStringLiteral("added"));
    CHECK(window.openFile(folder.path("Alpha.md")));
    CHECK(waitFor([panel] { return !panel->busy() && panel->tree()->topLevelItemCount() == 2; }));
    QTreeWidgetItem *mention = panel->tree()->topLevelItem(1)->child(0)->child(0);
    emit panel->linkMentionRequested({folder.path("Notes.md"), 3, 7, 5, mention->toolTip(0)}, folder.path("Alpha.md"));
    CHECK(notes->editor()->markdown().contains("I like [[alpha]] a lot.") && notes->editor()->markdown().endsWith("added"));
    CHECK(notes->isModified());
    CHECK(!readBytes(folder.path("Notes.md")).contains("[[alpha]] a lot"));
    notes->editor()->undo(); // one undo step
    CHECK(notes->editor()->markdown().contains("I like alpha a lot."));
    notes->editor()->redo();

    // Open but unchanged: changed and saved, without format-on-save or plugin hooks.
    notes->editor()->document()->setModified(false);
    QString error;
    CHECK(notes->save(folder.path("Notes.md"), error) == EditorTab::SaveResult::Saved);
    emit panel->linkMentionRequested({folder.path("Notes.md"), 5, 4, 5, QStringLiteral("The Start of it")},
                                     folder.path("Alpha.md"));
    CHECK(!notes->isModified());
    CHECK(readBytes(folder.path("Notes.md")).contains("The [[Alpha|Start]] of it\r\n"));

    // A line that changed since: nothing happens.
    const QByteArray before = readBytes(folder.path("Notes.md"));
    emit panel->linkMentionRequested({folder.path("Notes.md"), 3, 7, 5, QStringLiteral("I like alpha a lot.")},
                                     folder.path("Alpha.md"));
    CHECK(readBytes(folder.path("Notes.md")) == before);
    for (EditorTab *tab : window.findChildren<EditorTab *>())
        tab->editor()->document()->setModified(false);
    window.close();
}

TEST_CASE(outgoing_links_panel)
{
    Folder folder;
    std::unique_ptr<NoteIndex> index(indexOf(folder));
    MarkdownEditor editor;
    editor.setDocumentPath(folder.path("Home.md"));
    editor.setMarkdown(QStringLiteral("[[Alpha]] [[Alpha#Goals]] [[Missing]] [[Alpha]] [b](sub/Beta.md)"));
    OutgoingLinksPanel panel(index.get());
    panel.setAttribute(Qt::WA_DontShowOnScreen);
    panel.show();
    panel.setEditor(&editor);
    QTreeWidget *tree = panel.tree();
    CHECK(tree->topLevelItemCount() == 2);
    CHECK(tree->topLevelItem(0)->text(0) == "Links");
    CHECK((texts(tree->topLevelItem(0)) == QStringList{"Alpha", "Alpha › Goals", "Beta"}));
    CHECK((texts(tree->topLevelItem(1)) == QStringList{"Missing"}));

    // A click follows the link like Ctrl+click.
    QString followed;
    QObject::connect(&editor, &MarkdownEditor::linkRequested,
                     [&followed](const QString &path, const QString &anchor, bool) { followed = path + u'#' + anchor; });
    emit tree->itemClicked(tree->topLevelItem(0)->child(1), 0);
    CHECK(followed == folder.path("Alpha.md") + "#Goals");
}

TEST_CASE(note_excerpts_and_previews)
{
    const QString note = QStringLiteral("---\ntags: [x]\n---\n# Alpha\nintro [[Beta|the beta]] and [[Gamma#Part]]\n"
                                        "## Goals\n- ship ^g1\n![[pic.png]] ![[Other]]\n## Next\nz\n");
    const QString all = MarkdownEditor::excerpt(note, {});
    CHECK(!all.contains("tags") && all.startsWith("# Alpha"));
    CHECK(all.contains("intro the beta and Gamma › Part"));
    CHECK(all.contains("![image](pic.png) Other")); // Qt drops images without alt text
    const QString goals = MarkdownEditor::excerpt(note, QStringLiteral("goals"));
    CHECK(goals.startsWith("## Goals") && !goals.contains("Next") && !goals.contains("intro"));
    CHECK(MarkdownEditor::excerpt(note, QStringLiteral("^g1")) == "- ship ^g1");

    Folder folder;
    MarkdownEditor editor;
    editor.setDocumentPath(folder.path("Home.md"));
    editor.setMarkdown(QStringLiteral("x"));
    CHECK(editor.previewLink(QStringLiteral("Alpha#Goals"), QPoint(10, 10), QRect(0, 0, 100, 20)));
    CHECK(editor.notePreview() && editor.notePreview()->isVisible());
    const QString shown = editor.notePreview()->browser()->toPlainText();
    CHECK(shown.contains("ship") && !shown.contains("Other"));
    CHECK(editor.notePreview()->title() == "Alpha › Goals");
    CHECK(!editor.previewLink(QStringLiteral("Missing"), QPoint(10, 10), QRect()));
    CHECK(!editor.previewLink(QStringLiteral("https://example.com"), QPoint(10, 10), QRect()));
    editor.notePreview()->hide();
}
