#pragma once

#include "document/Document.h"

#include <QFutureWatcher>
#include <QPointer>
#include <QWidget>

class ExternalChangeBar;
class FindBar;
class MarkdownEditor;

// One open file: the editor widget plus the file it's bound to.
class EditorTab : public QWidget {
    Q_OBJECT

public:
    explicit EditorTab(QWidget *parent = nullptr);
    ~EditorTab() override { unlinkTwin(); }

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
        Conflict,   // nothing written: the file changed on disk, see conflictState()
        Unencodable // nothing written: some characters don't exist in the file's encoding
    };

    // Both keep the window alive while the disk works, and may show a "waiting" dialog.
    // `binary` is set when the file isn't text (and so wasn't loaded).
    bool load(const QString &path, QString &error, bool *binary = nullptr);
    // `overwrite` skips the check for changes made elsewhere.
    // `hooks`: format on save and the plugins' beforeSave; off for changes the app makes to other notes.
    SaveResult save(const QString &path, QString &error, bool overwrite = false, bool hooks = true);
    // With `encoding`, the file is read in that encoding whatever its bytes look like.
    bool reload(QString &error, const QString &encoding = {});

    QString encoding() const { return m_document.encoding(); }
    bool hasBom() const { return m_document.hasBom(); }
    bool usesCrlf() const { return m_document.usesCrlf(); }
    // Take effect on the next save, so they mark the document modified.
    void setEncoding(const QString &encoding, bool bom);
    void setCrlf(bool crlf);
    DiskState conflictState() const { return m_conflictState; }

    // The file was renamed or moved on disk by the app: keeps the contents and the modified state.
    void setFilePath(const QString &path);

    // The same file shown in another pane: `twin` starts as a copy of this tab, and from then on every edit in
    // either is mirrored to the other, and a save or reload in one counts for both.
    void openTwin(EditorTab *twin);
    EditorTab *twin() const { return m_twin; }
    void unlinkTwin();

    // Compares the file with what this tab last read or wrote, without blocking. An unmodified tab
    // reloads by itself; one with unsaved changes asks through a bar above the editor.
    void checkDisk();

signals:
    void stateChanged(); // file path or modified flag changed
    void statusMessage(const QString &message);
    void diskCheckFinished();

private:
    void mirror(int position, int removed, int added);
    void shareFileState(); // after a save or reload: the twin has the same file version now

    MarkdownEditor *m_editor;
    QPointer<EditorTab> m_twin;
    bool m_mirroring = false;
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
