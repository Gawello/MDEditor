#pragma once

#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class QAction;
class QMenuBar;

// Customizable shortcuts (Obsidian's Hotkeys settings) for every command in the menu bar. A command's id is
// its menu path in English ("File/Save"); changed shortcuts are kept in the settings as "hotkeys/<id>".
class Hotkeys : public QObject {
    Q_OBJECT

public:
    struct Entry {
        QString id;
        QString name; // "Save", shown with its menu: "File › Save"
        QString menu;
        QList<QKeySequence> defaults;
        QPointer<QAction> action;
    };

    static Hotkeys &instance();

    // Collects the commands of `menuBar` (the Plugins menu, which rebuilds itself, is left out) and applies
    // the saved shortcuts.
    void registerMenus(QMenuBar *menuBar);
    const QList<Entry> &entries() const { return m_entries; }
    QList<QKeySequence> shortcuts(const QString &id) const;
    bool isCustomized(const QString &id) const;
    void setShortcuts(const QString &id, const QList<QKeySequence> &shortcuts); // saved; empty: none
    void reset(const QString &id);
    // The other commands that use `sequence`, by name.
    QStringList conflicts(const QString &id, const QKeySequence &sequence) const;

signals:
    void changed(const QString &id);

private:
    Hotkeys() = default;
    const Entry *find(const QString &id) const;
    void apply(const Entry &entry);
    QList<Entry> m_entries;
};
