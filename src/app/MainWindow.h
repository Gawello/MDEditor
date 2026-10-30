#pragma once

#include "document/Document.h"

#include <QMainWindow>

class FolderPanel;
class MarkdownEditor;
class QSplitter;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Opens `file` if given, otherwise restores the previous session.
    void restoreSession(const QString &file);
    bool openFile(const QString &path);
    void openFolder(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void createMenus();
    void newFile();
    void openFileDialog();
    void openFolderDialog();
    bool save();
    bool saveAs();
    bool saveTo(const QString &path);
    bool maybeSave();
    void openFiles(const QStringList &paths);
    void updateTitle();
    QString dialogDirectory() const;
    void about();

    MarkdownEditor *m_editor;
    FolderPanel *m_folderPanel;
    QSplitter *m_splitter;
    Document m_document;
};
