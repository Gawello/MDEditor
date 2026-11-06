#pragma once

#include <QWidget>

class CloudDrive;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

// The Google Drive page of the settings dialog.
class CloudDrivePage : public QWidget {
    Q_OBJECT

public:
    explicit CloudDrivePage(CloudDrive *drive, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void update();
    void primaryAction();

    CloudDrive *m_drive;
    QLabel *m_status;
    QLabel *m_message;
    QLineEdit *m_folder;
    QLineEdit *m_mountPoint;
    QPushButton *m_browse;
    QCheckBox *m_connectOnStart;
    QPushButton *m_primary;
    QPushButton *m_signOut;
};
