#include "panels/FolderPanel.h"

#include "app/AppSettings.h"
#include "app/Bookmarks.h"
#include "app/FileActions.h"
#include "editor/EditorTheme.h"

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QUrl>
#include <QScopedValueRollback>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <functional>

// Obsidian-style listing: no icons, notes shown without their .md extension. Renaming in place goes
// through `renamer` (the main window), never QFileSystemModel's own rename, which would block the GUI
// thread on a slow drive and leave open tabs pointing at the old name.
class NotesModel : public QFileSystemModel {
public:
    using QFileSystemModel::QFileSystemModel;

    std::function<void(const QString &path, const QString &newName)> renamer;
    std::function<void(const QString &path, const QString &folder)> mover;
    QString root;

    // Dragging items within the tree moves them, through `mover` like renames. Drops from elsewhere (a file
    // manager) are refused rather than moving the user's files around by surprise.
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }

    bool canDropMimeData(const QMimeData *data, Qt::DropAction, int, int, const QModelIndex &parent) const override
    {
        return mover && data->hasUrls() && insideRoot(data) && (!parent.isValid() || isDir(parent));
    }

    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override
    {
        if (!canDropMimeData(data, action, row, column, parent))
            return false;
        const QString folder = parent.isValid() ? filePath(parent) : root;
        for (const QUrl &url : data->urls()) {
            const QString path = url.toLocalFile();
            if (QFileInfo(path).absolutePath() != folder && !FileActions::isSameOrInside(folder, path))
                mover(path, folder);
        }
        return false; // the model sees the move on disk by itself
    }

private:
    bool insideRoot(const QMimeData *data) const
    {
        const QList<QUrl> urls = data->urls();
        return !root.isEmpty() && std::all_of(urls.begin(), urls.end(), [this](const QUrl &url) {
            return url.isLocalFile() && FileActions::isSameOrInside(url.toLocalFile(), root);
        });
    }

public:

    static QString hiddenSuffix(const QString &name)
    {
        for (const QString &suffix : {QStringLiteral(".md"), QStringLiteral(".markdown")}) {
            if (name.endsWith(suffix, Qt::CaseInsensitive) && name.size() > suffix.size())
                return name.right(suffix.size());
        }
        return {};
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (index.column() == 0) {
            if (role == Qt::DecorationRole)
                return {};
            if ((role == Qt::DisplayRole || role == Qt::EditRole) && !isDir(index)) {
                const QString name = fileName(index);
                return name.chopped(hiddenSuffix(name).size());
            }
        }
        return QFileSystemModel::data(index, role);
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override
    {
        if (role != Qt::EditRole || index.column() != 0 || !renamer)
            return false;
        const QString old = fileName(index);
        QString name = value.toString().trimmed();
        const QString suffix = isDir(index) ? QString() : hiddenSuffix(old);
        if (!suffix.isEmpty() && !name.endsWith(suffix, Qt::CaseInsensitive))
            name += suffix; // the extension wasn't shown, so it wasn't meant to go
        if (!name.isEmpty() && name != old)
            renamer(filePath(index), name);
        return false; // the model sees the rename on disk by itself
    }
};

FolderPanel::FolderPanel(QWidget *parent)
    : QWidget(parent)
    , m_model(new NotesModel(this))
    , m_tree(new QTreeView(this))
    , m_title(new QLabel(tr("No folder open"), this))
    , m_bookmarksTitle(new QLabel(tr("Bookmarks"), this))
    , m_bookmarks(new QListWidget(this))
{
    // Every file: any text file can be edited, and others open in their own application.
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_model->setReadOnly(false); // names are editable; see NotesModel::setData
    // Queued: the rename waits for the disk in a nested event loop, which must not run inside the view's
    // commitData (the name editor is destroyed meanwhile, and the view then touches it).
    m_model->renamer = [this](const QString &path, const QString &name) {
        QMetaObject::invokeMethod(this, [this, path, name] { emit renameRequested(path, name); }, Qt::QueuedConnection);
    };
    m_model->mover = [this](const QString &path, const QString &folder) { // queued for the same reason
        QMetaObject::invokeMethod(this, [this, path, folder] { emit moveRequested(path, folder); }, Qt::QueuedConnection);
    };

    m_tree->setModel(m_model);
    m_tree->setHeaderHidden(true);
    for (int column = 1; column < m_model->columnCount(); ++column)
        m_tree->hideColumn(column);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(0, Qt::AscendingOrder);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setIndentation(14);
    m_tree->setUniformRowHeights(true);
    m_tree->setAnimated(true);
    m_tree->setVisible(false);
    m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed); // F2; a click opens the file
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QWidget::customContextMenuRequested, this, &FolderPanel::showContextMenu);
    m_tree->installEventFilter(this);
    m_tree->setDragDropMode(QAbstractItemView::DragDrop);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->setDropIndicatorShown(true);

    // Bookmarks: above the tree, hidden while there are none. Clicking opens; dragging reorders.
    m_bookmarksTitle->setContentsMargins(12, 6, 6, 2);
    m_bookmarks->setFrameShape(QFrame::NoFrame);
    m_bookmarks->setDragDropMode(QAbstractItemView::InternalMove);
    m_bookmarks->setContextMenuPolicy(Qt::CustomContextMenu);
    m_bookmarks->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_bookmarks, &QWidget::customContextMenuRequested, this, &FolderPanel::showBookmarkMenu);
    connect(m_bookmarks, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QString path = item->data(Qt::UserRole).toString();
        if (QFileInfo(path).isDir())
            emit rootRequested(path);
        else
            emit fileActivated(path);
    });
    connect(m_bookmarks->model(), &QAbstractItemModel::rowsMoved, this, [this] {
        QStringList order;
        for (int i = 0; i < m_bookmarks->count(); ++i)
            order << m_bookmarks->item(i)->data(Qt::UserRole).toString();
        Bookmarks::setOrder(order);
    });
    connect(&AppSettings::instance(), &AppSettings::changed, this, [this](const QString &key) {
        if (key == QLatin1String(AppSettings::bookmarksKey))
            refreshBookmarks();
    });

    auto *openButton = new QToolButton(this);
    openButton->setText(QStringLiteral("…"));
    openButton->setToolTip(tr("Open folder"));
    openButton->setAutoRaise(true);
    connect(openButton, &QToolButton::clicked, this, &FolderPanel::openFolderRequested);

    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    m_title->setTextInteractionFlags(Qt::NoTextInteraction);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(12, 8, 6, 4);
    header->addWidget(m_title, 1);
    header->addWidget(openButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addWidget(m_bookmarksTitle);
    layout->addWidget(m_bookmarks);
    layout->addWidget(m_tree, 1);
    layout->addStretch(); // keeps the header at the top while no folder is open

    // Single click opens a file, like in Obsidian; folders toggle.
    connect(m_tree, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        if (m_model->isDir(index))
            m_tree->setExpanded(index, !m_tree->isExpanded(index));
        else
            emit fileActivated(m_model->filePath(index));
    });

    applyStyle();
    refreshBookmarks();
}

void FolderPanel::refreshBookmarks()
{
    m_bookmarks->clear();
    for (const QString &path : Bookmarks::list()) {
        const QFileInfo info(path);
        QString name = info.fileName().isEmpty() ? path : info.fileName();
        name.chop(NotesModel::hiddenSuffix(name).size());
        // No stat here: a bookmark on a disconnected network drive must not hang the window.
        auto *item = new QListWidgetItem(name, m_bookmarks);
        item->setData(Qt::UserRole, path);
        item->setToolTip(QDir::toNativeSeparators(path));
    }
    const bool any = m_bookmarks->count() > 0;
    m_bookmarksTitle->setVisible(any);
    m_bookmarks->setVisible(any);
    const int rows = std::min(m_bookmarks->count(), 8);
    m_bookmarks->setFixedHeight(any ? rows * m_bookmarks->sizeHintForRow(0) + 6 : 0);
}

void FolderPanel::showBookmarkMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_bookmarks->itemAt(pos);
    if (!item)
        return;
    const QString path = item->data(Qt::UserRole).toString();
    QMenu menu(this);
    menu.addAction(tr("&Remove Bookmark"), this, [path] { Bookmarks::remove(path); });
    menu.addSeparator();
    menu.addAction(FileActions::showInFileManagerLabel(), this, [path] { FileActions::showInFileManager(path); });
    FileActions::addCopyPathMenu(menu.addMenu(tr("Copy &Path")), path, m_root);
    menu.exec(m_bookmarks->viewport()->mapToGlobal(pos));
}

void FolderPanel::setRootPath(const QString &path)
{
    m_root = QDir(path).absolutePath();
    m_model->root = m_root;
    m_model->setRootPath(m_root);
    m_tree->setRootIndex(m_model->index(m_root));
    m_tree->setVisible(true);
    m_title->setText(QDir(m_root).dirName().isEmpty() ? m_root : QDir(m_root).dirName());
    m_title->setToolTip(QDir::toNativeSeparators(m_root));
    emit rootChanged(m_root);
}

void FolderPanel::selectFile(const QString &path)
{
    if (m_root.isEmpty())
        return;
    const QModelIndex index = m_model->index(path);
    if (index.isValid()) {
        m_tree->setCurrentIndex(index);
        m_tree->scrollTo(index); // expands the folders above it
    }
}

bool FolderPanel::startRename(const QString &path)
{
    if (m_root.isEmpty() || !FileActions::isSameOrInside(path, m_root))
        return false;
    const QModelIndex index = m_model->index(path);
    if (!index.isValid())
        return false;
    m_tree->setCurrentIndex(index);
    m_tree->scrollTo(index);
    m_tree->setFocus(); // without it the name editor closes right away, and typing goes to the note
    m_tree->edit(index);
    return true;
}

bool FolderPanel::renameCurrent()
{
    const QModelIndex index = m_tree->currentIndex();
    if (!m_tree->hasFocus() || !index.isValid())
        return false;
    m_tree->edit(index);
    return true;
}

bool FolderPanel::eventFilter(QObject *watched, QEvent *event)
{
    // While a name is edited, keys go to the editor, not the tree.
    if (watched == m_tree && event->type() == QEvent::KeyPress
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Delete) {
        const QModelIndex index = m_tree->currentIndex();
        if (index.isValid()) {
            emit deleteRequested(m_model->filePath(index));
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void FolderPanel::showContextMenu(const QPoint &pos)
{
    if (m_root.isEmpty())
        return;
    const QModelIndex index = m_tree->indexAt(pos);
    const bool item = index.isValid();
    const QString path = item ? m_model->filePath(index) : m_root;
    const bool folder = !item || m_model->isDir(index);
    const QString parent = folder ? path : QFileInfo(path).absolutePath();

    QMenu menu(this);
    // Every action is queued: it runs once the menu is gone and back in the event loop, since several
    // open dialogs or start an editor in the tree.
    const auto add = [&](QMenu *to, const QString &text, std::function<void()> run) {
        return to->addAction(text, this, [this, run] { QMetaObject::invokeMethod(this, run, Qt::QueuedConnection); });
    };
    add(&menu, tr("New &Note"), [this, parent] { emit newNoteRequested(parent); });
    add(&menu, tr("New &Folder"), [this, parent] { emit newFolderRequested(parent); });
    if (item && !folder)
        add(&menu, tr("Make a &Copy"), [this, path] { emit duplicateRequested(path); });
    if (item)
        add(&menu, tr("&Move To…"), [this, path] { emit moveRequested(path, QString()); });
    add(&menu, Bookmarks::contains(path) ? tr("Remove &Bookmark") : tr("&Bookmark"), [path] { Bookmarks::toggle(path); });
    menu.addSeparator();

    if (item && folder) {
        add(&menu, tr("&Open as Root Folder"), [this, path] { emit rootRequested(path); });
    } else if (!item) {
        const QString up = QFileInfo(m_root).absolutePath();
        if (!up.isEmpty() && up != m_root)
            add(&menu, tr("Open Pa&rent Folder"), [this, up] { emit rootRequested(up); });
    }
    if (item && !folder)
        add(&menu, tr("Open &in Default App"), [path] { FileActions::openInDefaultApp(path); });
    add(&menu, FileActions::showInFileManagerLabel(), [path] { FileActions::showInFileManager(path); });
    FileActions::addCopyPathMenu(menu.addMenu(tr("Copy &Path")), path, m_root);

    if (item) {
        menu.addSeparator();
        add(&menu, tr("&Rename") + u'\t' + QKeySequence(Qt::Key_F2).toString(QKeySequence::NativeText),
            [this, index = QPersistentModelIndex(index)] {
                if (index.isValid())
                    m_tree->edit(index);
            });
        add(&menu, tr("De&lete") + u'\t' + QKeySequence(QKeySequence::Delete).toString(QKeySequence::NativeText),
            [this, path] { emit deleteRequested(path); });
    }
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void FolderPanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        applyStyle();
}

void FolderPanel::applyStyle()
{
    if (m_applyingStyle)
        return;
    const QScopedValueRollback guard(m_applyingStyle, true);

    const QPalette p = palette();
    const QColor window = p.color(QPalette::Window);
    const QColor text = p.color(QPalette::Text);

    m_tree->setStyleSheet(QStringLiteral(R"(
        QTreeView {
            background: %1;
            color: %2;
            border: none;
            padding: 2px 6px;
            show-decoration-selected: 0;
            outline: 0;
        }
        QTreeView::item { padding: 3px 4px; border-radius: 5px; }
        QTreeView::item:hover { background: %3; }
        QTreeView::item:selected { background: %4; color: %2; }
    )")
                              .arg(window.name(), EditorTheme::mix(window, text, 0.85).name(),
                                   EditorTheme::mix(window, text, 0.07).name(),
                                   EditorTheme::mix(window, text, 0.15).name()));
    m_title->setStyleSheet(QStringLiteral("color: %1;").arg(EditorTheme::mix(window, text, 0.6).name()));
    m_bookmarksTitle->setStyleSheet(QStringLiteral("color: %1; font-size: 85%;").arg(EditorTheme::mix(window, text, 0.5).name()));
    m_bookmarks->setStyleSheet(QString(m_tree->styleSheet()).replace(QStringLiteral("QTreeView"), QStringLiteral("QListWidget")));
}
