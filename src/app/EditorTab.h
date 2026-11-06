#pragma once

#include "document/Document.h"

#include <QFutureWatcher>
#include <QWidget>

class ExternalChangeBar;
class FindBar;
class MarkdownEditor;

// One open file: the editor widget plus the file it's bound to.
class EditorTab : public QWidget {
    Q_OBJECT

public:
    explicit EditorTab(QWidget *parent = nullptr);

    MarkdownEditor *editor() const { return m_editor; }
    FindBar *findBar() const { return m_findBar; }
    QString filePath() const { return m_document.filePath(); }
    QString displayName() const { return m_document.displayName(); }
    bool isUntitled() const { return m_document.isUntitled(); }
    bool isModified() const;
    // Untitled, unmodified and empty: opening a file may reuse this tab.
    bool isPristine() const;

    enum class SaveResult {
        Saved,
        Failed,
        Conflict // nothing written: the file changed on disk, see conflictState()
    };

    // Both keep the window alive while the disk works, and may show a "waiting" dialog.
    bool load(const QString &path, QString &error);
    // `overwrite` skips the check for changes made elsewhere.
    SaveResult save(const QString &path, QString &error, bool overwrite = false);
    bool reload(QString &error);
    DiskState conflictState() const { return m_conflictState; }

    // Compares the file with what this tab last read or wrote, without blocking. An unmodified tab
    // reloads by itself; one with unsaved changes asks through a bar above the editor.
    void checkDisk();

signals:
    void stateChanged(); // file path or modified flag changed
    void statusMessage(const QString &message);
    void diskCheckFinished();

private:
    MarkdownEditor *m_editor;
    void handleDiskCheck();
    void keepMyVersion();
    QString waitLabel() const;

    FindBar *m_findBar;
    ExternalChangeBar *m_changeBar;
    Document m_document;
    QFutureWatcher<DiskCheck> m_diskCheck;
    QString m_checkedPath;
    QByteArray m_checkedHash;
    FileStamp m_pendingStamp; // the version on disk that the bar is about
    DiskState m_conflictState = DiskState::Unknown;
};
