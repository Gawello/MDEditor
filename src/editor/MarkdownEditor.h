#pragma once

#include "editor/CompletionPopup.h"
#include "editor/EditorTheme.h"
#include "editor/NotePreview.h"
#include "editor/MathRenderer.h"
#include "markdown/BlockParser.h"
#include "markdown/Completion.h"
#include "markdown/LineStyle.h"
#include "markdown/TableParser.h"

#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QKeySequence>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QTextBlock>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class CodeHighlighter;
class LivePreviewHighlighter;
class QMenu;
class QTimer;

class BlockData;

class MarkdownEditor : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit MarkdownEditor(QWidget *parent = nullptr);

    // Markdown files get the live preview; any other text file is edited as plain text.
    static bool isMarkdownFile(const QString &path);
    // Live preview on or off. Off, the text is shown as it is, in a monospace font, at full width.
    void setMarkdownMode(bool on);
    bool markdownMode() const { return m_markdown; }

    // The document text exactly as it should be written to disk.
    QString markdown() const;
    void setMarkdown(const QString &text);

    // Directory used to resolve relative links, [[wiki links]] and images.
    void setBaseDirectory(const QString &directory);
    // The file being edited (also sets the base directory). Pasted images are stored next to it.
    void setDocumentPath(const QString &filePath);
    QString documentPath() const { return m_documentPath; }

    // Replaces the whole text as one undo step, keeping the cursor position where possible.
    void replaceAll(const QString &text);
    // Line commands, on the cursor's line or every line of the selection, each one undo step. A folded section
    // moves past as a whole.
    bool moveLines(int direction); // -1 up, +1 down; false at the start / end
    // Obsidian's colored highlight (index into highlightColors()): "==🔴text==". Wraps the selection, changes
    // the color of a highlight around it or removes it when it has that color already.
    void toggleHighlightColor(int index);
    void deleteLines();
    void transformLines(const std::function<std::u16string(std::u16string_view)> &change);
    // Changes the text to `text` by replacing only the part that differs, as one undo step: the cursor, the
    // scroll position and the folds elsewhere stay as they are.
    void applyText(const QString &text);

    // Wraps the selection in `marker` (e.g. "**"), or unwraps it if already wrapped.
    void toggleWrap(const QString &marker);

    // Inserts an empty table with `rows` rows (including the header) and `columns` columns.
    void insertTable(int rows, int columns);

    // Gives the lines of the selection a paragraph style, or removes it if they all have it.
    void setLineStyle(md::LineStyle style, int level = 0);
    // Removes inline formatting from the selection (or the current line).
    void clearFormatting();
    // Turns the selection into a [[wiki link]] or a [text](url) link, or inserts an empty one.
    void insertLink(bool wiki);

    enum class Snippet { CodeBlock, MathBlock, Callout, HorizontalRule, Footnote };
    void insertSnippet(Snippet snippet);

    enum class TableAction {
        RowAbove, RowBelow, DuplicateRow, DeleteRow, MoveRowUp, MoveRowDown,
        ColumnLeft, ColumnRight, DeleteColumn, MoveColumnLeft, MoveColumnRight,
        AlignLeft, AlignCenter, AlignRight, SortAscending, SortDescending, DeleteTable
    };
    // Edits the table under the cursor; false if the cursor isn't in a table or the edit isn't possible.
    bool tableAction(TableAction action);
    bool cursorInTable() const;

    // The right-click menu for a click at `pos` (viewport coordinates).
    std::unique_ptr<QMenu> createContextMenu(const QPoint &pos);

    // Menu builders shared by the right-click menu and the main window's Format menu. `target` gives the
    // editor to act on when an action runs. With `ownShortcuts` the actions get their shortcuts; otherwise
    // the shortcut is only displayed (the main menu owns it).
    using EditorGetter = std::function<MarkdownEditor *()>;
    static void addFormatActions(QMenu *menu, const EditorGetter &target, bool ownShortcuts);
    static void addParagraphActions(QMenu *menu, const EditorGetter &target);
    static void addInsertActions(QMenu *menu, const EditorGetter &target);
    static void addLinkActions(QMenu *menu, const EditorGetter &target, bool ownShortcuts);

    // Formats the selection, the code block or table under the cursor (or a non-Markdown file).
    void formatAtCursor();
    // Formats every code block with a known language and every table (or a whole non-Markdown file).
    // Returns the number of formatted blocks; with `quiet`, no status message is emitted.
    int formatDocument(bool quiet = false);
    static QKeySequence prettifyShortcut();
    static QKeySequence prettifyDocumentShortcut();

    // Folding (Markdown mode): headings hide their section, list items their children, callouts their body.
    bool isFoldable(const QTextBlock &block) const;
    bool toggleFold(const QTextBlock &block); // false if nothing to fold there
    bool toggleFoldAtCursor();                // the line of the cursor, or the heading above it
    void setAllFolded(bool folded);           // every heading (folding) or everything (unfolding)
    bool isFolded(const QTextBlock &block) const;

    // How [[links]] find their files (the main window resolves them in the open folder). Without one, links
    // are relative to the note.
    using LinkResolver = std::function<QString(const QString &target, bool wiki, const QString &from)>;
    static void setLinkResolver(LinkResolver resolver);
    // Suggestions while typing [[links]] and #tags (the main window gives them from the open folder). `text` is
    // this note's text, given only for its own headings and blocks.
    using CompletionProvider =
        std::function<QList<CompletionItem>(const md::CompletionContext &context, const QString &from, const QString &text)>;
    static void setCompletionProvider(CompletionProvider provider);
    // The open folder, for attachments kept in a folder there (Settings → Editor).
    static void setNotesRoot(const QString &root);
    // Formatted text (a web page, Word, Excel) as Markdown: headings, emphasis, links, images, lists, tables, code.
    // Empty when the HTML holds nothing worth converting (plain text wrapped in tags).
    static QString htmlToMarkdown(const QString &html);
    // `line` (a line holding one image link) with the width hint set: ![[img.png|300]], ![alt|300](img.png);
    // 0 removes it. The line as it was when it isn't such a line.
    static QString withImageWidth(const QString &line, int width);
    // The image shown on `block` (not an embedded note), in viewport coordinates.
    std::optional<QRectF> imageRect(const QTextBlock &block) const;
    // What an embed or a preview shows of a note: without the front matter, only the section (`Heading`) or
    // block (`^id`) when `anchor` names one, and [[links]] as their text, which QTextDocument can't show.
    // `keepLinks`: [[links]] become Markdown links (to their target as written) instead of text.
    static QString excerpt(const QString &text, const QString &anchor, bool keepLinks = false);
    // Ctrl+hover: shows the note `target` links to under `globalPos`, next to the line `lineRect` (global).
    // False when it isn't a note that can be shown.
    bool previewLink(const QString &target, const QPoint &globalPos, const QRect &lineRect);
    NotePreview *notePreview() const { return m_preview; } // null until first shown
    // Follows a link as written ("Note#Heading", "path/x.md", a URL), as Ctrl+click does.
    void openLink(const QString &target, bool toTheRight = false);
    CompletionPopup *completionPopup() const { return m_completion; } // null until first needed
    // Line numbers in a strip left of the text (Settings / View → Line Numbers); null while off.
    QWidget *lineNumberArea() const { return m_lineNumbers; }
    int lineNumberWidth() const;
    void paintLineNumbers(QPaintEvent *event);

signals:
    // A link to a note or text file was followed: `path` (absolute; it may not exist yet, then the note is to be
    // created), the part after '#' (a heading or ^block), and whether it goes to the other pane. An empty
    // `path` means this note itself ([[#Heading]]).
    // `newName`: a [[Name]] link to a note that doesn't exist: the window decides where it goes.
    void linkRequested(const QString &path, const QString &anchor, bool toTheRight, bool newName = false);
    void revealRequested(const QString &path); // the link menu's Reveal in Folder Panel
    void renameRequested(const QString &path); // ...and Rename
    void searchRequested(const QString &text);  // the menu's Search for “selection”
    void deleteFileRequested(const QString &path); // an image's Delete Image File (the window asks first)
    void filesDropped(const QStringList &paths);
    void statusMessage(const QString &message);
    void markdownModeChanged(bool on);
    // The editor has no table picker or symbol picker of its own: the main window shows them.
    void insertTableRequested(const QPoint &globalPos);
    void insertSymbolRequested();
    void insertTemplateRequested(); // the main window knows the templates folder

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    bool autoPair(QKeyEvent *event); // brackets, quotes, Markdown markers; true if it handled the key
    void mousePressEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool canInsertFromMimeData(const QMimeData *source) const override;
    QMimeData *createMimeDataFromSelection() const override;
    void insertFromMimeData(const QMimeData *source) override;

private:
    struct TableGeometry {
        std::vector<QTextBlock> rows; // header and body rows, without the delimiter row
        std::vector<md::Align> aligns;
        std::vector<qreal> edges; // column boundaries in viewport coordinates, columns + 1 entries
    };
    struct TextRun {
        QString text;
        QFont font;
        QColor color;
        QColor background;
    };

    struct CachedImage {
        QImage image;
        QDateTime modified;
        QPixmap scaled; // last painted size, to avoid rescaling on every repaint
    };

    struct CodeBlock {
        int start = 0; // content range, without the fence lines
        int end = 0;
        QString language;
    };

    std::optional<CodeBlock> codeBlockAt(const QTextBlock &block) const;
    QString documentLanguage() const; // language of a non-Markdown file, from its suffix
    bool formatRange(int start, int end, const QString &language, QString *message);
    bool formatTable(const QTextBlock &header, QString *message);
    void restoreCursor(QTextCursor cursor, int anchor, int position);
    void applyTheme();
    void updateCodeHighlighter();
    void applyFontSettings();
    void refreshActiveBlock();
    QString resolvePath(const QString &target) const;
    CachedImage *cachedImage(const QString &target) const;
    QSizeF imageDisplaySize(const QString &target, int requestedWidth) const;
    void paintImage(QPainter &painter, const QTextBlock &block, const QString &target, int requestedWidth) const;
    // Links to dropped files at the cursor: [[Name]] for a note the name finds, else a relative Markdown link.
    void insertFileLinks(const QStringList &files, QTextCursor cursor);
    void insertImageLinks(const QStringList &files, QTextCursor cursor);
    void pasteImage(const QImage &image);
    TableGeometry tableGeometry(const QTextBlock &header) const;
    std::vector<TextRun> cellRuns(const QString &text, bool header) const;
    void paintTable(QPainter &painter, const TableGeometry &table) const;
    bool focusTableCellAt(const QPoint &pos);
    bool moveToTableCell(bool backwards);
    bool tableEnter();
    void insertTableRowAfter(const QTextBlock &block);
    int tableColumnAt(const QTextCursor &cursor) const;
    void insertBlockText(const QString &text, int caret);
    void addTableActions(QMenu *menu);
    // The misspelled (underlined) word at document position `position`, as [start, end); empty if none.
    std::pair<int, int> misspelledWordAt(int position) const;
    void addSpellingActions(QMenu *menu, int position);
    void placeInTableCell(const QTextBlock &block, int cell, bool select);
    void updateMargins();
    md::BlockInfo blockInfo(const QTextBlock &block) const;
    void paintEntities(QPainter &painter, const QTextBlock &block, const BlockData &data) const;
    void paintMath(QPainter &painter, const QTextBlock &block, const BlockData &data) const;
    const MathRenderer::Formula &formula(const QString &tex, bool display) const;
    QRectF characterRect(const QTextBlock &block, int position) const;
    bool continueList();
    bool indentListItem(bool outdent);
    std::optional<QString> linkTargetAt(const QPoint &pos) const;
    std::optional<QString> linkTargetAt(const QTextCursor &cursor) const;
    // The file an internal link points to (absolute, existing or not), or empty for URLs and nothing usable.
    QString linkFile(const QString &target) const;
    bool toggleTaskAt(const QPoint &pos);
    void applyFolds();
    QRectF foldMarkerRect(const QTextBlock &block) const;
    QTextBlock foldableBlockAt(const QPoint &pos) const; // a fold marker under `pos`
    void paintFolds(QPainter &painter, const QTextBlock &block, const QRectF &rect) const;
    void paintBoxes(QPainter &painter, const QRect &area);
    QString resolveLink(const QString &target, bool wiki) const;
    // ![[Note]] / ![[Note#Heading]]: the note rendered in a box, cached by file, date, section and width.
    struct Embed {
        QDateTime modified;
        qreal width = 0;
        QString title;
        std::shared_ptr<QTextDocument> document;
        qreal height = 0;    // of the box
        bool clipped = false; // the note is longer than the box
    };
    const Embed *embed(const QString &target) const;
    void paintEmbed(QPainter &painter, const QTextBlock &block, const Embed &embed) const;
    // The footnote reference at `pos` and its definition's text, if any.
    std::optional<std::pair<QString, QTextBlock>> footnoteAt(const QPoint &pos) const;

    EditorTheme m_theme;
    LivePreviewHighlighter *m_highlighter = nullptr;
    CodeHighlighter *m_codeHighlighter = nullptr; // plain-text mode, files of a known language
    QString m_baseDirectory;
    QString m_documentPath; // empty for an unsaved document
    // Suggestions: the popup, where the typed query starts (document position) and what is being typed.
    void updateCompletion(bool mayOpen);
    void acceptCompletion(int row);
    void hideCompletion();
    CompletionPopup *m_completion = nullptr;
    QWidget *m_lineNumbers = nullptr;
    // Dragging an image's right edge: the line and the width so far (-1: not resizing).
    int m_resizeBlock = -1;
    qreal m_resizeWidth = 0;
    qreal m_resizeGrab = 0; // where the edge was grabbed, from the image's left
    bool atImageEdge(const QPoint &pos, QTextBlock *block = nullptr) const;
    void addImageActions(QMenu *menu, const QTextBlock &block);
    void updateLineNumbers();
    // Page preview: shown after the mouse rests on a link with Ctrl held.
    void startPreview(const QPoint &viewportPos);
    NotePreview *m_preview = nullptr;
    QTimer *m_previewTimer = nullptr;
    QString m_previewTarget;
    QPoint m_previewPos; // viewport
    md::CompletionContext m_completionContext;
    int m_completionStart = -1;
    int m_completionDismissed = -1; // Escape: no list again for the query starting here
    bool m_applyingTheme = false;
    bool m_markdown = true;
    mutable QHash<QString, CachedImage> m_images; // by absolute path
    mutable bool m_hasImages = false;              // re-layout images when the width changes
    int m_layoutWidth = 0;
    std::optional<QTextCursor> m_batchCursor; // set while formatDocument edits
    QTimer *m_imageRelayout = nullptr;
    QTimer *m_foldTimer = nullptr;
    int m_hoverBlock = -1; // fold markers show on the line under the mouse
    mutable QHash<QString, Embed> m_embeds;
};
