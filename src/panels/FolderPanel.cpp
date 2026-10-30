#include "panels/FolderPanel.h"

#include "editor/EditorTheme.h"

#include <QDir>
#include <QEvent>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
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

void FolderPanel::selectFile(const QString &path)
{
    if (m_root.isEmpty())
        return;
    const QModelIndex index = m_model->index(path);
    if (index.isValid())
        m_tree->setCurrentIndex(index);
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
}
