#pragma once

#include <QDialog>
#include <QList>

#include <functional>
#include <optional>

class QLabel;
class QLineEdit;
class QListWidget;

// Obsidian's quick switcher and command palette: a search field over a list, at the top of the window.
// Typing filters fuzzily; Enter runs the selected item after the dialog has closed.
class PaletteDialog : public QDialog {
    Q_OBJECT

public:
    struct Item {
        QString text;
        QString detail;   // dimmed after the text (a folder, a menu path)
        QString shortcut; // right-aligned
        std::function<void()> run;
    };
    // Offered below the matches for what's typed, e.g. "Create note …"; also run by Shift+Enter.
    using Fallback = std::function<std::optional<Item>(const QString &query)>;

    PaletteDialog(QWidget *window, const QString &placeholder, const QString &hint = {});

    void setItems(QList<Item> items);
    void setFallback(Fallback fallback) { m_fallback = std::move(fallback); }
    // Shows the dialog and runs the chosen item; false if nothing was chosen.
    bool choose();

    // Fuzzy match: every character of `query` in order (case-insensitive). Higher is better; -1: no match.
    static int score(const QString &query, const QString &text);

    QLineEdit *searchField() const { return m_search; }
    QListWidget *list() const { return m_list; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void filter();
    void accept() override;

    QLineEdit *m_search;
    QListWidget *m_list;
    QLabel *m_hint;
    QList<Item> m_items;
    QList<Item> m_shown;
    Fallback m_fallback;
    int m_chosen = -1;
};
