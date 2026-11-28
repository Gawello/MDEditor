#pragma once

#include "app/NavigationHistory.h"
#include "app/PaletteDialog.h"
#include "editor/CompletionPopup.h"

#include <QMainWindow>
#include <QPointer>

#include <functional>
#include <optional>

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
class NoteIndex;
class OutlinePanel;
class QLabel;
class SearchPanel;
class SideBar;
class TagPanel;
class BacklinksPanel;
class OutgoingLinksPanel;
class GraphView;

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
    void renameFile(const QString &path); // in the folder panel when it's listed there, else with a dialog
    // A link followed in `from`'s editor: opens (or creates) `path`, in the other pane if `toTheRight`, and
    // scrolls to `anchor`. An empty path is `from` itself.
    void followLink(EditorTab *from, const QString &path, const QString &anchor, bool toTheRight);
    void openToTheRight(EditorTab *from, const QString &path);
    static void scrollToLine(MarkdownEditor *editor, int line); // the line at the top, cursor on it
    // The app changes notes the user isn't editing (a linked mention, links after a rename). An open note is
    // changed in its tab, and saved if it had no unsaved changes; a closed one on disk, as it is encoded.
    // Returns the paths that couldn't be changed, with why.
    QStringList changeNotes(const QList<std::pair<QString, std::function<std::optional<QString>(const QString &)>>> &changes);
    void revealInFolderPanel(const QString &path);
    // File management, from the folder panel and the tab menu. Open tabs follow their files.
    void renamePath(const QString &path, const QString &newName);
    // `folder` empty: asks for one.
    void movePath(const QString &path, const QString &folder);
    // Renames or moves `path` to `target`, with tabs, the root folder, recent files and bookmarks following it.
    bool relocate(const QString &path, const QString &target, const QString &title);
    // After a rename or move (old path -> new path for every file that moved): the links to them, and the
    // relative links of moved notes, are updated (asking first, per Settings → Notes).
    void updateLinksAfterMove(const QHash<QString, QString> &moved);
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
    // Obsidian's navigation: quick switcher (Ctrl+O), command palette (Ctrl+P), the right sidebar.
    void showQuickSwitcher();
    void showCommandPalette();
    // Every enabled menu command, for the palette and the slash commands.
    QList<PaletteDialog::Item> commandItems();
public:
    QList<CompletionItem> slashCommands(const QString &query); // "/query" in the editor
private:
    void toggleSidePage(QWidget *page); // shows the sidebar on `page`, or hides it if it's showing it already
    // Opens `path` and selects [column, column + length) on `line` (0-based).
    void openAt(const QString &path, int line, int column, int length);
    // A new note `name`.md in the folder (or next to the current file), opened in a tab.
    void createNamedNote(const QString &name);
    void updateWordCount();
    void updateBacklinkCount();
    void exportNote(bool pdf); // File → Export to PDF / HTML
    void openDailyNote();
    void showGraph();
    void insertTemplate(MarkdownEditor *editor);
    QString notesRoot() const; // the open folder, or where dialogs start without one
    // Back / Forward. noteShown records the current tab's place (a new entry when `jump` or another file);
    // call rememberPosition first when the cursor is about to jump inside the same file.
    void noteShown(bool jump = false);
    void rememberPosition();
    void goBack();
    void goForward();
    void showPlace(bool forward);
    void updateHistoryActions();
    void reopenClosedTab();
    void goToTab(int index); // -1: the last one
    // Places a file at `position` (clamped) in the current tab's editor and scrolls to it.
    void placeCursor(int position);

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
    NoteIndex *m_index;
    SideBar *m_sideBar = nullptr;
    OutlinePanel *m_outline = nullptr;
    SearchPanel *m_search = nullptr;
    TagPanel *m_tagPanel = nullptr;
    BacklinksPanel *m_backlinks = nullptr;
    OutgoingLinksPanel *m_outgoing = nullptr;
    QToolButton *m_backlinkCount = nullptr; // status bar: "3 backlinks", opens the panel
    QLabel *m_wordCount = nullptr;
    QWidget *m_graphWindow = nullptr;
    GraphView *m_graph = nullptr;
    NavigationHistory m_history;
    QPointer<EditorTab> m_historyTab; // the tab whose place is the history's current entry
    int m_quietHistory = 0;           // > 0: opening files doesn't record places (session restore, Back)
    QAction *m_backAction = nullptr;
    QAction *m_forwardAction = nullptr;
    QAction *m_reopenAction = nullptr;
    struct ClosedTab {
        QString path;
        int position = 0;
        int pane = 0;
    };
    QList<ClosedTab> m_closedTabs; // most recent last
    static constexpr int maxClosedTabs = 20;
    // Session items on Google Drive, waiting for the mount.
    QString m_pendingFolder;
    QStringList m_pendingFiles;
    QString m_pendingCurrent;
};
