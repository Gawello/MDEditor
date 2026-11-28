#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QTableWidget;

// Settings → Hotkeys: every menu command with its shortcut, searchable by name or key; click a shortcut and
// press new keys to change it, Backspace to remove it, Reset for the default. Conflicts are pointed out.
class HotkeysPage : public QWidget {
    Q_OBJECT

public:
    explicit HotkeysPage(QWidget *parent = nullptr);
    QTableWidget *table() const { return m_table; }
    QLineEdit *filterField() const { return m_filter; }
    QLabel *warning() const { return m_warning; }
    void filter(const QString &text);

private:
    void fill();
    void showConflicts(const QString &id);

    QLineEdit *m_filter;
    QTableWidget *m_table;
    QLabel *m_warning;
};
