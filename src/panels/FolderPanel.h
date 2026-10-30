#pragma once

#include <QWidget>

class QFileSystemModel;
class QLabel;
class QTreeView;

// Browses an arbitrary folder chosen ad hoc. Not a vault: nothing is indexed or stored there.
class FolderPanel : public QWidget {
    Q_OBJECT

public:
    explicit FolderPanel(QWidget *parent = nullptr);

    QString rootPath() const { return m_root; }
    void setRootPath(const QString &path);
    void selectFile(const QString &path);

signals:
    void fileActivated(const QString &path);
    void openFolderRequested();

protected:
    void changeEvent(QEvent *event) override;

private:
    void applyStyle();

    QFileSystemModel *m_model;
    QTreeView *m_tree;
    QLabel *m_title;
    QString m_root;
    bool m_applyingStyle = false;
};
