#pragma once

#include <QWidget>

class QListWidget;

// Folders kept for quick access, like the list on the left of a file manager.
class FavoritesPanel : public QWidget {
    Q_OBJECT

public:
    explicit FavoritesPanel(QWidget *parent = nullptr);

    QStringList folders() const;
    // Returns false if `path` is in the list already.
    bool addFolder(const QString &path);
    void removeFolder(const QString &path);

signals:
    void folderActivated(const QString &path);
    void statusMessage(const QString &message);

protected:
    void changeEvent(QEvent *event) override;

private:
    void store();
    void addItem(const QString &path);
    void showMenu(const QPoint &pos);
    void activate(const QString &path);
    void applyStyle();

    QListWidget *m_list;
    bool m_applyingStyle = false;
};
