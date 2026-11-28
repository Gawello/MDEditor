#pragma once

#include <QWidget>

class NotesModel;
class QModelIndex;
class QSortFilterProxyModel;
class QLabel;
class QListWidget;
class QTreeView;

// Browses an arbitrary folder chosen ad hoc. Not a vault: nothing is indexed or stored there.
class FolderPanel : public QWidget {
    Q_OBJECT

public:
    explicit FolderPanel(QWidget *parent = nullptr);

    QString rootPath() const { return m_root; }
    void setRootPath(const QString &path);
    // Selects `path`, expanding its folders and scrolling to it.
    void selectFile(const QString &path);
    // Edits the name of `path` in place (it must be inside the root folder).
    bool startRename(const QString &path);
    // While the tree has the focus: renames its current item in place.
    bool renameCurrent();
    // Name A–Z / Z–A, modified newest / oldest first (folders stay first); kept in the settings.
    enum class Sort { NameAscending, NameDescending, ModifiedNewest, ModifiedOldest };
    void setSort(Sort sort);
    Sort sort() const { return m_sort; }
    void collapseAll();
    QStringList selectedPaths() const;
    QTreeView *tree() const { return m_tree; }

signals:
    void fileActivated(const QString &path);
    void openFolderRequested();
    // File management is done by the main window, which also updates the tabs of the files involved.
    void renameRequested(const QString &path, const QString &newName);
    void deleteRequested(const QString &path);
    void duplicateRequested(const QString &path);
    void newNoteRequested(const QString &folder);
    void newFolderRequested(const QString &folder);
    void rootRequested(const QString &folder);
    // `folder` empty: ask where to.
    void moveRequested(const QString &path, const QString &folder);
    // Several selected items at once: one question for all of them.
    void moveManyRequested(const QStringList &paths, const QString &folder); // folder empty: ask
    void deleteManyRequested(const QStringList &paths);
    void searchInFolderRequested(const QString &folder);
    void rootChanged(const QString &root);

protected:
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void applyStyle();
    void showContextMenu(const QPoint &pos);
    void showBookmarkMenu(const QPoint &pos);
    void refreshBookmarks();

    QModelIndex source(const QModelIndex &index) const; // the tree's index in the file model
    QModelIndex view(const QModelIndex &index) const;   // and back
    NotesModel *m_model;
    QSortFilterProxyModel *m_proxy;
    QTreeView *m_tree;
    QLabel *m_title;
    QLabel *m_bookmarksTitle;
    QListWidget *m_bookmarks;
    QString m_root;
    bool m_applyingStyle = false;
    Sort m_sort = Sort::NameAscending;
};
