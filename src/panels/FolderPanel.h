#pragma once

#include <QWidget>

class QFileSystemModel;
class QLabel;
class QTreeView;

// Browses a folder: the Files section of the sidebar, and the tree of the vault.
class FolderPanel : public QWidget {
    Q_OBJECT

public:
    explicit FolderPanel(QWidget *parent = nullptr);

    QString rootPath() const { return m_root; }
    void setRootPath(const QString &path);
    void clearRoot();
    void selectFile(const QString &path);

    // The look of the rows, shared with the other sidebar lists.
    static QString itemStyleSheet(const QString &view, const QPalette &palette);

signals:
    void fileActivated(const QString &path);
    void openFolderRequested();
    void addToFavoritesRequested(const QString &path);

protected:
    void changeEvent(QEvent *event) override;

private:
    void applyStyle();
    void showMenu(const QPoint &pos);

    QFileSystemModel *m_model;
    QTreeView *m_tree;
    QLabel *m_title;
    QString m_root;
    bool m_applyingStyle = false;
};
