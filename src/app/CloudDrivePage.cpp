#include "app/CloudDrivePage.h"

#include "app/CloudDrive.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

CloudDrivePage::CloudDrivePage(CloudDrive *drive, QWidget *parent)
    : QWidget(parent)
    , m_drive(drive)
    , m_status(new QLabel(this))
    , m_message(new QLabel(this))
    , m_folder(new QLineEdit(drive->remoteFolder(), this))
    , m_mountPoint(new QLineEdit(QDir::toNativeSeparators(drive->mountPoint()), this))
    , m_browse(new QPushButton(tr("Browse…"), this))
    , m_connectOnStart(new QCheckBox(tr("Connect when MDEditor starts"), this))
    , m_primary(new QPushButton(this))
    , m_signOut(new QPushButton(tr("Sign Out"), this))
{
    auto *layout = new QVBoxLayout(this);
    auto *title = new QLabel(tr("Google Drive"), this);
    QFont font = title->font();
    font.setPointSizeF(font.pointSizeF() * 1.3);
    font.setBold(true);
    title->setFont(font);
    layout->addWidget(title);

    auto *intro = new QLabel(tr("Shows a Google Drive folder as a local folder, so its notes open like any other "
                                "file. The connection is remembered, also after updating MDEditor."), this);
    intro->setWordWrap(true);
    intro->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(intro);
    layout->addWidget(m_status);

    m_folder->setPlaceholderText(tr("Whole drive"));
    auto *mountRow = new QHBoxLayout;
    mountRow->addWidget(m_mountPoint, 1);
    mountRow->addWidget(m_browse);
    auto *form = new QFormLayout;
    form->addRow(tr("Folder on Google Drive:"), m_folder);
    form->addRow(tr("Local folder:"), mountRow);
    layout->addLayout(form);

    m_connectOnStart->setChecked(drive->connectOnStart());
    layout->addWidget(m_connectOnStart);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_primary);
    buttons->addWidget(m_signOut);
    buttons->addStretch();
    layout->addLayout(buttons);

    m_message->setWordWrap(true);
    m_message->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_message->setOpenExternalLinks(true);
    layout->addWidget(m_message);
    layout->addStretch();

    connect(m_folder, &QLineEdit::editingFinished, this, [this] { m_drive->setRemoteFolder(m_folder->text()); });
    connect(m_mountPoint, &QLineEdit::editingFinished, this,
            [this] { m_drive->setMountPoint(QDir::fromNativeSeparators(m_mountPoint->text())); });
    connect(m_connectOnStart, &QCheckBox::toggled, m_drive, &CloudDrive::setConnectOnStart);
    connect(m_browse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getExistingDirectory(this, tr("Local Folder"), m_mountPoint->text());
        if (path.isEmpty())
            return;
        m_mountPoint->setText(QDir::toNativeSeparators(path));
        m_drive->setMountPoint(path);
    });
    connect(m_primary, &QPushButton::clicked, this, &CloudDrivePage::primaryAction);
    connect(m_signOut, &QPushButton::clicked, m_drive, &CloudDrive::signOut);
    connect(m_drive, &CloudDrive::stateChanged, this, &CloudDrivePage::update);
    connect(m_drive, &CloudDrive::failed, m_message, &QLabel::setText);
    connect(m_drive, &CloudDrive::signInLinkReady, this, [this](const QString &url) {
        m_message->setText(tr("Your browser should open. If it doesn't, <a href=\"%1\">open the sign-in page</a>.")
                               .arg(url.toHtmlEscaped()));
    });
    update();
}

void CloudDrivePage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_drive->refresh(); // rclone may have been installed since the last look
}

void CloudDrivePage::primaryAction()
{
    m_message->clear();
    // Edits in progress count: clicking the button is what finishes them.
    m_drive->setRemoteFolder(m_folder->text());
    m_drive->setMountPoint(QDir::fromNativeSeparators(m_mountPoint->text()));

    switch (m_drive->state()) {
    case CloudDrive::State::NoRclone:
        m_drive->refresh();
        break;
    case CloudDrive::State::SignedOut:
        m_drive->signIn();
        break;
    case CloudDrive::State::SigningIn:
        m_drive->cancelSignIn();
        break;
    case CloudDrive::State::SignedIn:
        m_drive->connectDrive();
        break;
    case CloudDrive::State::Connected:
        m_drive->disconnectDrive();
        break;
    default:
        break;
    }
}

void CloudDrivePage::update()
{
    using State = CloudDrive::State;
    const State state = m_drive->state();

    QString status;
    QString action;
    switch (state) {
    case State::Checking:
        status = tr("Checking…");
        break;
    case State::NoRclone:
        status = tr("MDEditor connects to Google Drive through the free tool rclone, which isn't installed. "
                    "<a href=\"https://rclone.org/install/\">Install rclone</a>, then check again.");
        action = tr("Check Again");
        break;
    case State::SignedOut:
        status = tr("Not signed in.");
        action = tr("Sign In with Google…");
        break;
    case State::SigningIn:
        status = tr("Finish signing in in your browser.");
        action = tr("Cancel");
        break;
    case State::SignedIn:
        status = tr("Signed in, not connected.");
        action = tr("Connect");
        break;
    case State::Connecting:
        status = tr("Connecting…");
        break;
    case State::Connected:
        status = tr("Connected: %1").arg(QDir::toNativeSeparators(m_drive->mountPoint()));
        action = tr("Disconnect");
        break;
    case State::Disconnecting:
        status = tr("Disconnecting…");
        break;
    }

    m_status->setText(status);
    m_status->setWordWrap(true);
    m_status->setOpenExternalLinks(true);
    m_primary->setText(action.isEmpty() ? tr("Please Wait") : action);
    m_primary->setEnabled(!action.isEmpty());
    m_signOut->setVisible(state == State::SignedIn || state == State::Connected);

    // The mount uses these; they apply the next time it connects.
    const bool editable = state != State::Connected && state != State::Connecting;
    m_folder->setEnabled(editable);
    m_mountPoint->setEnabled(editable);
    m_browse->setEnabled(editable);
}
