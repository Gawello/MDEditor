#pragma once

#include <QWidget>

class NoteIndex;
class QTimer;
class QTreeWidget;

// Obsidian's "All properties": every front matter key used in the open folder with the number of notes, and
// under each its values. A click searches for the notes with it ([key] or [key:value]).
class PropertiesPanel : public QWidget {
    Q_OBJECT

public:
    explicit PropertiesPanel(NoteIndex *index, QWidget *parent = nullptr);
    void rebuild();
    QTreeWidget *tree() const { return m_tree; }

signals:
    void searchRequested(const QString &query);

protected:
    void showEvent(QShowEvent *event) override;

private:
    NoteIndex *m_index;
    QTreeWidget *m_tree;
    QTimer *m_timer;
};
