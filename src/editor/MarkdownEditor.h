#pragma once

#include "editor/EditorTheme.h"
#include "markdown/BlockParser.h"
#include "markdown/TableParser.h"

#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QTextBlock>

#include <optional>
#include <vector>

class LivePreviewHighlighter;
class QTimer;

class MarkdownEditor : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit MarkdownEditor(QWidget *parent = nullptr);

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

    // Wraps the selection in `marker` (e.g. "**"), or unwraps it if already wrapped.
    void toggleWrap(const QString &marker);

    // Inserts an empty table with `rows` rows (including the header) and `columns` columns.
    void insertTable(int rows, int columns);

    // Formats the selection, the code block or table under the cursor (or a non-Markdown file).
    void formatAtCursor();
    // Formats every code block with a known language and every table (or a whole non-Markdown file).
    // Returns the number of formatted blocks; with `quiet`, no status message is emitted.
    int formatDocument(bool quiet = false);

signals:
    void openFileRequested(const QString &path);
    void filesDropped(const QStringList &paths);
    void statusMessage(const QString &message);

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool canInsertFromMimeData(const QMimeData *source) const override;
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
    void applyTheme();
    void applyFontSettings();
    void refreshActiveBlock();
    QString resolvePath(const QString &target) const;
    CachedImage *cachedImage(const QString &target) const;
    QSizeF imageDisplaySize(const QString &target, int requestedWidth) const;
    void paintImage(QPainter &painter, const QTextBlock &block, const QString &target, int requestedWidth) const;
    void insertImageLinks(const QStringList &files, QTextCursor cursor);
    void pasteImage(const QImage &image);
    TableGeometry tableGeometry(const QTextBlock &header) const;
    std::vector<TextRun> cellRuns(const QString &text, bool header) const;
    void paintTable(QPainter &painter, const TableGeometry &table) const;
    bool focusTableCellAt(const QPoint &pos);
    bool moveToTableCell(bool backwards);
    bool tableEnter();
    void insertTableRowAfter(const QTextBlock &block);
    void placeInTableCell(const QTextBlock &block, int cell, bool select);
    void updateMargins();
    md::BlockInfo blockInfo(const QTextBlock &block) const;
    QRectF characterRect(const QTextBlock &block, int position) const;
    bool continueList();
    bool indentListItem(bool outdent);
    std::optional<QString> linkTargetAt(const QPoint &pos) const;
    void openLink(const QString &target);
    bool toggleTaskAt(const QPoint &pos);

    EditorTheme m_theme;
    LivePreviewHighlighter *m_highlighter = nullptr;
    QString m_baseDirectory;
    QString m_documentPath; // empty for an unsaved document
    bool m_applyingTheme = false;
    mutable QHash<QString, CachedImage> m_images; // by absolute path
    mutable bool m_hasImages = false;              // re-layout images when the width changes
    int m_layoutWidth = 0;
    QTimer *m_imageRelayout = nullptr;
};
