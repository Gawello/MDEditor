#pragma once

#include <QMainWindow>

#include <functional>

class EditorTab;
class FolderPanel;
class MarkdownEditor;
class QMenu;
class QSplitter;
class QTabWidget;
class QToolButton;
class CloudDrive;
class SettingsDialog;
class SymbolPicker;
class UpdateController;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Opens `files` if given, otherwise restores the tabs of the previous session.
    void restoreSession(const QStringList &files);
    bool openFile(const QString &path);
    void openFolder(const QString &path);
    // A few seconds after startup, if enabled in Settings (Help → Check for Updates works any time).
    void checkForUpdatesOnStart();

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void createMenus();
    void applyStyle();
    EditorTab *currentTab() const;
    QList<EditorTab *> allTabs() const;      // in every pane
    QTabWidget *paneOf(const EditorTab *tab) const;
    QTabWidget *createPane();
    void setCurrentPane(QTabWidget *pane);
    // Shows `tab` in a second pane (created right of or below the first), as a twin of the same file.
    void splitTab(EditorTab *tab, Qt::Orientation orientation);
    void moveToOtherPane(EditorTab *tab);
    void removePaneIfEmpty(QTabWidget *pane);
    MarkdownEditor *currentEditor() const;
    MarkdownEditor *markdownEditor() const; // the current editor if it shows Markdown, else null
    EditorTab *findTab(const QString &path) const; // the current pane's first
    void activate(EditorTab *tab);
    EditorTab *addTab(QTabWidget *pane = nullptr);
    bool closeTab(EditorTab *tab);
    // Closes the tabs of `pane` for which `which(tab)` is true, asking about unsaved changes; stops on Cancel.
    void closeTabs(QTabWidget *pane, const std::function<bool(EditorTab *)> &which);
    void discardTab(EditorTab *tab); // no questions: its file is gone
    void showTabMenu(QTabWidget *pane, const QPoint &pos);
    void renameTab(EditorTab *tab);
    void revealInFolderPanel(const QString &path);
    // File management, from the folder panel and the tab menu. Open tabs follow their files.
    void renamePath(const QString &path, const QString &newName);
    // `folder` empty: asks for one.
    void movePath(const QString &path, const QString &folder);
    // Renames or moves `path` to `target`, with tabs, the root folder, recent files and bookmarks following it.
    bool relocate(const QString &path, const QString &target, const QString &title);
    void deletePath(const QString &path);
    void newNote(const QString &folder);
    void newFolder(const QString &folder);
    void duplicateFile(const QString &path);
    void newFile();
    void openFileDialog();
    void openFolderDialog();
    void openFiles(const QStringList &paths);
    bool save(EditorTab *tab);
    bool saveAs(EditorTab *tab);
    bool saveTo(EditorTab *tab, const QString &path, bool overwrite = false);
    bool resolveConflict(EditorTab *tab, const QString &path);
    bool maybeSave(EditorTab *tab);
    void updateTabTitle(EditorTab *tab);
    void updateWindowTitle();
    void populateRecentMenu();
    void showTablePicker(const QPoint &globalPos, bool alignRight);
    void openPendingDriveItems();
    QIcon tableIcon() const;
    QIcon symbolIcon() const;
    void toggleSymbolPicker();
    QString dialogDirectory() const;
    void about();
    void showSettings(const QString &page = {});
    void populatePluginsMenu();
    void updateMarkdownActions();
    void updateEncodingButton();
    void populateEncodingMenu(QMenu *menu);

    QTabWidget *m_tabs; // the current pane
    QList<QTabWidget *> m_panes; // one, or two when split
    QSplitter *m_paneSplitter;
    FolderPanel *m_folderPanel;
    QSplitter *m_splitter;
    QMenu *m_recentMenu = nullptr;
    QMenu *m_pluginsMenu = nullptr;
    QList<QAction *> m_markdownActions; // enabled while the current tab shows Markdown
    QToolButton *m_tableButton = nullptr;
    QToolButton *m_symbolButton = nullptr;
    QToolButton *m_encodingButton = nullptr; // status bar: encoding and line endings of the current tab
    SymbolPicker *m_symbolPicker = nullptr;
    SettingsDialog *m_settingsDialog = nullptr;
    CloudDrive *m_cloudDrive;
    UpdateController *m_updates;
    // Session items on Google Drive, waiting for the mount.
    QString m_pendingFolder;
    QStringList m_pendingFiles;
    QString m_pendingCurrent;
};
