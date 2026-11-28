#pragma once

#include "notes/Snapshots.h"

#include <QDialog>

class QListWidget;
class QPlainTextEdit;
class QPushButton;

// File → Version History: the snapshots of a note, newest first, each shown in full; Restore puts one back in
// the editor (an edit like any other: undo takes it back, saving keeps it).
class VersionHistoryDialog : public QDialog {
    Q_OBJECT

public:
    VersionHistoryDialog(const QString &notePath, const QList<Snapshots::Entry> &entries, QWidget *parent = nullptr);
    QListWidget *list() const { return m_list; }
    QString selectedText() const;

signals:
    void restoreRequested(const QString &text);

private:
    QList<Snapshots::Entry> m_entries;
    QListWidget *m_list;
    QPlainTextEdit *m_preview;
    QPushButton *m_restore;
};
