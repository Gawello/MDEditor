#pragma once

#include <QWidget>

class NoteIndex;
class QLabel;
class QTreeWidget;

// Every #tag in the open folder with its number of notes, nested tags ("project/alpha") under their parents.
class TagPanel : public QWidget {
    Q_OBJECT

public:
    explicit TagPanel(NoteIndex *index, QWidget *parent = nullptr);

    void rebuild();
    QTreeWidget *tree() const { return m_tree; }

signals:
    void tagActivated(const QString &tag);

protected:
    void showEvent(QShowEvent *event) override;

private:
    NoteIndex *m_index;
    QTreeWidget *m_tree;
    QLabel *m_empty;
};
