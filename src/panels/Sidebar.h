#pragma once

#include <QWidget>

class FavoritesPanel;
class FolderPanel;
class QLabel;
class QSplitter;
class QStackedWidget;
class SidebarSection;

// The left column, in one view: favorite folders, the vault, and the folder being browsed.
class Sidebar : public QWidget {
    Q_OBJECT

public:
    explicit Sidebar(QWidget *parent = nullptr);

    FavoritesPanel *favorites() const { return m_favorites; }
    FolderPanel *files() const { return m_files; }
    FolderPanel *vault() const { return m_vault; }

    QString rootPath() const;
    void setRootPath(const QString &path);
    void selectFile(const QString &path);
    // An empty path shows the invitation to choose a vault.
    void setVaultRoot(const QString &path);

    void restoreLayout();
    void saveLayout() const;

signals:
    void fileActivated(const QString &path);
    void openFolderRequested();
    void openVaultRequested();
    void statusMessage(const QString &message);

protected:
    void changeEvent(QEvent *event) override;

private:
    void toggle(SidebarSection *section);
    void applyStyle();

    FavoritesPanel *m_favorites;
    FolderPanel *m_vault;
    FolderPanel *m_files;
    QStackedWidget *m_vaultStack;
    QSplitter *m_splitter;
    QWidget *m_filler; // takes the free space when no open section is left to take it
    QLabel *m_vaultHint;
    SidebarSection *m_favoritesSection;
    SidebarSection *m_vaultSection;
    SidebarSection *m_filesSection;
};
