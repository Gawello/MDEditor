#pragma once

#include <QFrame>

class QLabel;
class QPushButton;

// Shown above the editor when the file changed on disk while the tab has unsaved changes.
class ExternalChangeBar : public QFrame {
    Q_OBJECT

public:
    explicit ExternalChangeBar(QWidget *parent = nullptr);

    void showChanged(const QString &fileName);
    void showMissing(const QString &fileName);

signals:
    void reloadRequested();
    void keepRequested();

protected:
    void changeEvent(QEvent *event) override;

private:
    void updateColors();

    QLabel *m_label;
    QPushButton *m_reload;
    QPushButton *m_keep;
};
