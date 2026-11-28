#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QListWidget;

// One suggestion for what's being typed in the editor.
struct CompletionItem {
    QString label;  // shown
    QString detail; // shown dimmed on the right
    QString insert; // replaces what was typed
};

// The list of suggestions under the cursor ([[links]], #tags). A frameless tool-tip window that never takes
// the focus: the editor keeps it and forwards the navigation keys (not a Qt::Popup, which on X11 grabs the
// whole desktop's input).
class CompletionPopup : public QWidget {
    Q_OBJECT

public:
    explicit CompletionPopup(QWidget *editor);

    void setItems(const QList<CompletionItem> &items);
    const QList<CompletionItem> &items() const { return m_items; }
    // Shows the list with its top-left corner at `globalPos`, kept on the screen (above `aboveY` when it
    // doesn't fit below).
    void showAt(const QPoint &globalPos, int aboveY);
    void move(int rows); // the selection, wrapping around for single steps
    int currentRow() const;
    void setCurrentRow(int row);
    QListWidget *list() const { return m_list; }

signals:
    void activated(int row); // clicked

private:
    QListWidget *m_list;
    QList<CompletionItem> m_items;
};
