#pragma once

#include "editor/EditorTheme.h"
#include "markdown/BlockParser.h"
#include "markdown/TableParser.h"

#include <QPlainTextEdit>
#include <QTextBlock>

#include <optional>
#include <vector>

class LivePreviewHighlighter;

class MarkdownEditor : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit MarkdownEditor(QWidget *parent = nullptr);

    // The document text exactly as it should be written to disk.
    QString markdown() const;
    void setMarkdown(const QString &text);

    // Directory used to resolve relative links and [[wiki links]].
    void setBaseDirectory(const QString &directory) { m_baseDirectory = directory; }

    // Wraps the selection in `marker` (e.g. "**"), or unwraps it if already wrapped.
    void toggleWrap(const QString &marker);

    // Inserts an empty table with `rows` rows (including the header) and `columns` columns.
    void insertTable(int rows, int columns);

signals:
    void openFileRequested(const QString &path);
    void filesDropped(const QStringList &paths);

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

    void applyTheme();
    void refreshActiveBlock();
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
    bool m_applyingTheme = false;
};
