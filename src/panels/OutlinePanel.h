#pragma once

#include <QPointer>
#include <QWidget>

class MarkdownEditor;
class QLabel;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

// The headings of the current note as a tree; a click moves the cursor there. Follows edits (debounced) and
// marks the section the cursor is in.
class OutlinePanel : public QWidget {
    Q_OBJECT

public:
    explicit OutlinePanel(QWidget *parent = nullptr);

    void setEditor(MarkdownEditor *editor); // null or a plain-text file: empty
    QTreeWidget *tree() const { return m_tree; }
    void rebuild();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void markCurrent();

    QPointer<MarkdownEditor> m_editor;
    QTreeWidget *m_tree;
    QLabel *m_empty;
    QTimer *m_timer;
    QList<QMetaObject::Connection> m_connections;
};
