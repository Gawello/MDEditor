#include "TestHarness.h"
#include "app/PaletteDialog.h"
#include "editor/LivePreviewHighlighter.h"
#include "editor/MarkdownEditor.h"
#include "notes/NoteIndex.h"
#include "notes/Templates.h"
#include "panels/GraphView.h"
#include "panels/OutlinePanel.h"
#include "panels/SearchPanel.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTreeWidget>

namespace {

void writeNote(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(text.toUtf8());
}

bool waitFor(const std::function<bool()> &done)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

// A small folder of notes: links across folders, tags (nested, in frontmatter), an embed.
struct Vault {
    QTemporaryDir dir;
    QString path(const QString &relative) const { return QDir::cleanPath(dir.filePath(relative)); }

    Vault()
    {
        writeNote(path("Home.md"), "---\ntags: [start]\n---\n# Home\nSee [[Alpha]] and [b](sub/Beta.md). #idea #proj/x\n"
                                   "![[Alpha#Goals]]\n");
        writeNote(path("sub/Alpha.md"), "# Alpha\n## Goals\n- ship\n## Other\nback to [[Home]] #idea\n");
        writeNote(path("sub/Beta.md"), "# Beta\nnothing #proj/y\n");
        writeNote(path("notes.txt"), "plain text with idea inside\n");
    }
};

NoteIndex *indexOf(const Vault &vault)
{
    auto *index = new NoteIndex;
    index->setRoot(vault.dir.path());
    CHECK(waitFor([index] { return !index->scanning() && index->notes().size() == 3; }));
    return index;
}

} // namespace

TEST_CASE(note_index_reads_the_folder)
{
    Vault vault;
    std::unique_ptr<NoteIndex> index(indexOf(vault));
    CHECK(index->files().size() == 4);
    CHECK(index->resolve("Alpha", true, vault.path("Home.md")) == vault.path("sub/Alpha.md"));
    CHECK(index->resolve("sub/Beta.md", false, vault.path("Home.md")) == vault.path("sub/Beta.md"));
    CHECK(index->resolve("Home", true, vault.path("sub/Alpha.md")) == vault.path("Home.md"));
    CHECK(index->resolve("Missing", true, vault.path("Home.md")).isEmpty());

    const QHash<QString, int> tags = index->tagCounts();
    CHECK(tags.value("idea") == 2);
    CHECK(tags.value("proj") == 2); // proj/x and proj/y count for their parent
    CHECK(tags.value("proj/x") == 1);
    CHECK(tags.value("start") == 1);

    // A saved tab updates its note right away.
    index->update(vault.path("sub/Beta.md"), "# Beta\n#fresh\n");
    CHECK(index->tagCounts().value("fresh") == 1);
}

TEST_CASE(search_in_folder)
{
    Vault vault;
    std::unique_ptr<NoteIndex> index(indexOf(vault));
    const auto results = SearchPanel::run(index->files(), {"idea", false, false, {}}, nullptr);
    int matches = 0;
    for (const auto &file : results)
        matches += int(file.matches.size());
    CHECK(results.size() == 3); // Home, Alpha and notes.txt
    CHECK(matches == 3);
    const auto regex = SearchPanel::run(index->files(), {"^#+ (Goals|Beta)$", true, true, {}}, nullptr);
    CHECK(regex.size() == 2);
    CHECK(SearchPanel::run(index->files(), {"IDEA", true, false, {}}, nullptr).isEmpty());

    SearchPanel panel(index.get());
    panel.showTag("proj");
    CHECK(panel.results()->topLevelItemCount() == 2);
}

TEST_CASE(palette_matches_fuzzily)
{
    CHECK(PaletteDialog::score("alp", "Project Alpha") > 0);
    CHECK(PaletteDialog::score("xyz", "Project Alpha") < 0);
    CHECK(PaletteDialog::score("pa", "Project Alpha") > 0); // word starts
    CHECK(PaletteDialog::score("alp", "Alpha") > PaletteDialog::score("alp", "Project Alpha"));
    CHECK(PaletteDialog::score("fold", "Fold All Headings") > PaletteDialog::score("fold", "Folder Panel") - 100000);
    CHECK(PaletteDialog::score("", "anything") == 0);
}

TEST_CASE(templates_and_daily_notes)
{
    CHECK(Templates::qtDateFormat("YYYY-MM-DD") == "yyyy-MM-dd");
    CHECK(Templates::qtDateFormat("dddd, D MMMM YYYY [at] HH:mm") == "dddd, d MMMM yyyy 'at' HH:mm");
    const QDateTime when(QDate(2026, 3, 7), QTime(9, 5));
    CHECK(Templates::formatDate(when, "YYYY-MM-DD HH:mm") == "2026-03-07 09:05");
    CHECK(Templates::expand("# {{title}}\n{{date}} {{time}} {{date:YYYY}} {{ Title }}", "Note", when)
          == "# Note\n2026-03-07 09:05 2026 Note");
    CHECK(Templates::dailyNotePath("/notes", "daily", "YYYY-MM-DD", QDate(2026, 3, 7)) == "/notes/daily/2026-03-07.md");
    CHECK(Templates::dailyNotePath("/notes", "", "", QDate(2026, 3, 7)) == "/notes/2026-03-07.md");

    QTemporaryDir dir;
    writeNote(dir.filePath("templates/Meeting.md"), "x");
    writeNote(dir.filePath("templates/sub/Daily.md"), "y");
    CHECK(Templates::templateFiles(dir.filePath("templates")).size() == 2);
    CHECK(Templates::templateFiles(dir.filePath("none")).isEmpty());
}

TEST_CASE(folding_hides_sections)
{
    MarkdownEditor editor;
    editor.resize(700, 500);
    editor.setMarkdown("# A\ntext\n## B\n- item\n  - child\n# C\n> [!note]- Folded\n> body\nend");
    QCoreApplication::processEvents();
    QTextDocument *doc = editor.document();
    const auto visible = [doc](int n) { return doc->findBlockByNumber(n).isVisible(); };

    // "[!note]-" starts folded.
    CHECK(waitFor([&] { return !visible(7); }));
    CHECK(editor.isFolded(doc->findBlockByNumber(6)));
    CHECK(visible(8)); // "end" isn't part of the callout

    CHECK(editor.toggleFold(doc->findBlockByNumber(0)));
    CHECK(!visible(1) && !visible(4) && visible(5));
    CHECK(!editor.toggleFold(doc->findBlockByNumber(1))); // a paragraph has nothing to fold

    // Moving the cursor into hidden text unfolds it.
    QTextCursor cursor(doc->findBlockByNumber(4));
    editor.setTextCursor(cursor);
    CHECK(visible(4) && visible(1));

    editor.setAllFolded(true);
    CHECK(!visible(1) && !visible(6));
    editor.setAllFolded(false);
    for (int i = 0; i < doc->blockCount(); ++i)
        CHECK(visible(i));

    // Edits keep the fold attached to its heading.
    CHECK(editor.toggleFold(doc->findBlockByNumber(2)));
    QTextCursor top(doc);
    top.insertText("intro\n");
    QCoreApplication::processEvents();
    CHECK(editor.isFolded(doc->findBlockByNumber(3)) && !visible(4) && !visible(5));
}

TEST_CASE(outline_follows_the_note)
{
    MarkdownEditor editor;
    editor.setMarkdown("# One\ntext\n## Two\n```\n# not\n```\n### Three\n# Four");
    OutlinePanel outline;
    outline.setAttribute(Qt::WA_DontShowOnScreen);
    outline.show();
    outline.setEditor(&editor);
    QTreeWidget *tree = outline.tree();
    CHECK(tree->topLevelItemCount() == 2);
    CHECK(tree->topLevelItem(0)->text(0) == "One");
    CHECK(tree->topLevelItem(0)->childCount() == 1 && tree->topLevelItem(0)->child(0)->child(0)->text(0) == "Three");

    editor.moveCursor(QTextCursor::End);
    editor.insertPlainText("\n## Five");
    CHECK(waitFor([tree] { return tree->topLevelItemCount() == 2 && tree->topLevelItem(1)->childCount() == 1; }));
    CHECK(tree->currentItem() && tree->currentItem()->text(0) == "Five");
    editor.setMarkdownMode(false);
    CHECK(tree->topLevelItemCount() == 0);
}

TEST_CASE(embeds_and_footnotes_render)
{
    Vault vault;
    std::unique_ptr<NoteIndex> index(indexOf(vault));
    NoteIndex *raw = index.get();
    MarkdownEditor::setLinkResolver(
        [raw](const QString &target, bool wiki, const QString &from) { return raw->resolve(target, wiki, from); });

    MarkdownEditor editor;
    editor.setAttribute(Qt::WA_DontShowOnScreen);
    editor.resize(800, 600);
    editor.show();
    editor.setDocumentPath(vault.path("Home.md"));
    editor.setMarkdown("first\n![[Alpha#Goals]]\n![[Missing]]\ntext[^1]\n\n[^1]: The note.");
    QCoreApplication::processEvents();
    const auto data = [&editor](int n) {
        return static_cast<const BlockData *>(editor.document()->findBlockByNumber(n).userData());
    };
    CHECK(data(1) && data(1)->imageShown);  // the note is drawn in place of the line
    CHECK(data(2) && !data(2)->imageShown); // a missing note stays a link
    // The footnote label is a superscript outside the cursor line.
    const QTextBlock block = editor.document()->findBlockByNumber(3);
    bool superscript = false;
    for (const QTextLayout::FormatRange &range : block.layout()->formats()) {
        if (range.format.verticalAlignment() == QTextCharFormat::AlignSuperScript)
            superscript = true;
    }
    CHECK(superscript);
    MarkdownEditor::setLinkResolver({});
}

TEST_CASE(graph_links_notes)
{
    Vault vault;
    std::unique_ptr<NoteIndex> index(indexOf(vault));
    GraphView graph(index.get());
    CHECK(graph.nodeCount() == 3);
    CHECK(graph.edgeCount() == 2); // Home-Alpha (both ways counts once), Home-Beta
    for (int i = 0; i < 400 && !graph.settled(); ++i)
        graph.step();
    CHECK(graph.settled());

    graph.setCurrent(vault.path("sub/Beta.md"));
    graph.setLocal(true);
    CHECK(graph.nodeCount() == 2 && graph.edgeCount() == 1);
}
