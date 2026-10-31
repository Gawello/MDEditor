#pragma once

#include <QMainWindow>

class EditorTab;
class FolderPanel;
class MarkdownEditor;
class QMenu;
class QSplitter;
class QTabWidget;
class QToolButton;
class SettingsDialog;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Opens `files` if given, otherwise restores the tabs of the previous session.
    void restoreSession(const QStringList &files);
    bool openFile(const QString &path);
    void openFolder(const QString &path);

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
    EditorTab *tabAt(int index) const;
    MarkdownEditor *currentEditor() const;
    int findTab(const QString &path) const;
    EditorTab *addTab();
    bool closeTab(int index);
    void newFile();
    void openFileDialog();
    void openFolderDialog();
    void openFiles(const QStringList &paths);
    bool save(EditorTab *tab);
    bool saveAs(EditorTab *tab);
    bool saveTo(EditorTab *tab, const QString &path);
    bool maybeSave(EditorTab *tab);
    void updateTabTitle(EditorTab *tab);
    void updateWindowTitle();
    void populateRecentMenu();
    void showTablePicker(const QPoint &globalPos, bool alignRight);
    QIcon tableIcon() const;
    QString dialogDirectory() const;
    void about();
    void showSettings(const QString &page = {});
    void populatePluginsMenu();

    QTabWidget *m_tabs;
    FolderPanel *m_folderPanel;
    QSplitter *m_splitter;
    QMenu *m_recentMenu = nullptr;
    QMenu *m_pluginsMenu = nullptr;
    QToolButton *m_tableButton = nullptr;
    SettingsDialog *m_settingsDialog = nullptr;
};
