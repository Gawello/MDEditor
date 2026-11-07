#include "panels/FavoritesPanel.h"

#include "app/AppSettings.h"
#include "app/Background.h"
#include "panels/FolderPanel.h"

#include <QAbstractItemModel>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QListWidget>
#include <QMenu>
#include <QScopedValueRollback>
#include <QStandardPaths>
#include <QStyle>
#include <QVBoxLayout>

namespace {

QStringList defaultFolders()
{
    QStringList folders;
    for (const auto location : {QStandardPaths::DocumentsLocation, QStandardPaths::DesktopLocation,
                                QStandardPaths::HomeLocation}) {
        const QString path = QDir::cleanPath(QStandardPaths::writableLocation(location));
        if (!path.isEmpty() && !folders.contains(path))
            folders << path;
    }
    return folders;
}

QString displayName(const QString &path)
{
    const QString name = QDir(path).dirName();
    return name.isEmpty() ? QDir::toNativeSeparators(path) : name;
}

} // namespace

FavoritesPanel::FavoritesPanel(QWidget *parent)
    : QWidget(parent)
    , m_list(new QListWidget(this))
{
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setIconSize(QSize(16, 16));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_list);

    const QVariant stored = AppSettings::instance().value(AppSettings::favoritesKey);
    // Unset is a first run. A list the user emptied stays empty.
    const QStringList folders = stored.isValid() ? stored.toStringList() : defaultFolders();
    for (const QString &path : folders)
        addItem(path);

    connect(m_list, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) { activate(item->data(Qt::UserRole).toString()); });
    connect(m_list, &QListWidget::customContextMenuRequested, this, &FavoritesPanel::showMenu);
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, &FavoritesPanel::store);
    applyStyle();
}

QStringList FavoritesPanel::folders() const
{
    QStringList paths;
    for (int i = 0; i < m_list->count(); ++i)
        paths << m_list->item(i)->data(Qt::UserRole).toString();
    return paths;
}

void FavoritesPanel::addItem(const QString &path)
{
    auto *item = new QListWidgetItem(style()->standardIcon(QStyle::SP_DirIcon), displayName(path), m_list);
    item->setData(Qt::UserRole, path);
    item->setToolTip(QDir::toNativeSeparators(path));
    item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled); // dropping between rows, not onto one
}

bool FavoritesPanel::addFolder(const QString &path)
{
    const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (folders().contains(clean))
        return false;
    addItem(clean);
    store();
    return true;
}

void FavoritesPanel::removeFolder(const QString &path)
{
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole).toString() == path) {
            delete m_list->takeItem(i);
            store();
            return;
        }
    }
}

void FavoritesPanel::store()
{
    AppSettings::instance().setValue(AppSettings::favoritesKey, folders());
}

void FavoritesPanel::activate(const QString &path)
{
    // A favorite can be on a network drive that's gone: asking the disk may take a while.
    const auto exists = runAndWait([path] { return QFileInfo(path).isDir(); },
                                   tr("Waiting for %1…").arg(displayName(path)), this);
    if (exists.value_or(false))
        emit folderActivated(path);
    else
        emit statusMessage(tr("%1 isn't available").arg(QDir::toNativeSeparators(path)));
}

void FavoritesPanel::showMenu(const QPoint &pos)
{
    const QListWidgetItem *item = m_list->itemAt(pos);
    if (!item)
        return;
    const QString path = item->data(Qt::UserRole).toString();
    QMenu menu(this);
    menu.addAction(tr("Remove from Favorites"), this, [this, path] { removeFolder(path); });
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

void FavoritesPanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        applyStyle();
}

void FavoritesPanel::applyStyle()
{
    if (m_applyingStyle)
        return;
    const QScopedValueRollback guard(m_applyingStyle, true);
    m_list->setStyleSheet(FolderPanel::itemStyleSheet(QStringLiteral("QListWidget"), palette()));
}
