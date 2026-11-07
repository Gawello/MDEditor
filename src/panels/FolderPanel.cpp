#include "panels/FolderPanel.h"

#include "editor/EditorTheme.h"

#include <QDir>
#include <QEvent>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QScopedValueRollback>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

// Obsidian-style listing: no icons, notes shown without their .md extension.
class NotesModel : public QFileSystemModel {
public:
    using QFileSystemModel::QFileSystemModel;

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (index.column() == 0) {
            if (role == Qt::DecorationRole)
                return {};
            if (role == Qt::DisplayRole && !isDir(index)) {
                const QString name = fileName(index);
                for (const QString &suffix :{QStringLiteral(".md"), QStringLiteral(".markdown")}) {
                    if (name.endsWith(suffix, Qt::CaseInsensitive) && name.size() > suffix.size())
                        return name.chopped(suffix.size());
                }
            }
        }
        return QFileSystemModel::data(index, role);
    }
};

} // namespace

FolderPanel::FolderPanel(QWidget *parent)
    : QWidget(parent)
    , m_model(new NotesModel(this))
    , m_tree(new QTreeView(this))
    , m_title(new QLabel(tr("No folder open"), this))
{
    m_model->setNameFilters({QStringLiteral("*.md"), QStringLiteral("*.markdown"), QStringLiteral("*.txt")});
    m_model->setNameFilterDisables(false);
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);

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
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeView::customContextMenuRequested, this, &FolderPanel::showMenu);

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
    layout->addWidget(m_tree, 1);
    layout->addStretch(); // keeps the header at the top while no folder is open

    // Single click opens a file, like in Obsidian; folders toggle.
    connect(m_tree, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        if (m_model->isDir(index))
            m_tree->setExpanded(index, !m_tree->isExpanded(index));
        else
            emit fileActivated(m_model->filePath(index));
    });

    // Directories load on a worker thread. Without this a tree could keep showing the old contents until
    // something else, such as losing the focus, made it paint.
    connect(m_model, &QFileSystemModel::directoryLoaded, this, [this] {
        m_tree->doItemsLayout();
        m_tree->viewport()->update();
    });

    applyStyle();
}

void FolderPanel::setRootPath(const QString &path)
{
    m_root = QDir(path).absolutePath();
    m_model->setRootPath(m_root);
    m_tree->setRootIndex(m_model->index(m_root));
    m_tree->setVisible(true);
    m_title->setText(QDir(m_root).dirName().isEmpty() ? m_root : QDir(m_root).dirName());
    m_title->setToolTip(QDir::toNativeSeparators(m_root));
}

void FolderPanel::clearRoot()
{
    m_root.clear();
    m_tree->setVisible(false);
    m_title->setText(tr("No folder open"));
    m_title->setToolTip(QString());
}

void FolderPanel::selectFile(const QString &path)
{
    if (m_root.isEmpty())
        return;
    const QModelIndex index = m_model->index(path);
    if (index.isValid())
        m_tree->setCurrentIndex(index);
}

void FolderPanel::showMenu(const QPoint &pos)
{
    const QModelIndex index = m_tree->indexAt(pos);
    // A folder, or the one being shown when the click is on empty space.
    const QString path = !index.isValid() ? m_root : m_model->isDir(index) ? m_model->filePath(index) : QString();
    if (path.isEmpty())
        return;
    QMenu menu(this);
    menu.addAction(tr("Add to Favorites"), this, [this, path] { emit addToFavoritesRequested(path); });
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void FolderPanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        applyStyle();
}

QString FolderPanel::itemStyleSheet(const QString &view, const QPalette &palette)
{
    const QColor window = palette.color(QPalette::Window);
    const QColor text = palette.color(QPalette::Text);
    return QStringLiteral(R"(
        %5 {
            background: %1;
            color: %2;
            border: none;
            padding: 2px 6px;
            show-decoration-selected: 0;
            outline: 0;
        }
        %5::item { padding: 3px 4px; border-radius: 5px; }
        %5::item:hover { background: %3; }
        %5::item:selected { background: %4; color: %2; }
    )")
        .arg(window.name(), EditorTheme::mix(window, text, 0.85).name(), EditorTheme::mix(window, text, 0.07).name(),
             EditorTheme::mix(window, text, 0.15).name(), view);
}

void FolderPanel::applyStyle()
{
    if (m_applyingStyle)
        return;
    const QScopedValueRollback guard(m_applyingStyle, true);

    const QPalette p = palette();
    m_tree->setStyleSheet(itemStyleSheet(QStringLiteral("QTreeView"), p));
    m_title->setStyleSheet(QStringLiteral("color: %1;").arg(EditorTheme::mix(p.color(QPalette::Window), p.color(QPalette::Text), 0.6).name()));
}
