#include "panels/TagPanel.h"

#include "notes/NoteIndex.h"

#include <QHeaderView>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
constexpr int tagRole = Qt::UserRole;
}

TagPanel::TagPanel(NoteIndex *index, QWidget *parent)
    : QWidget(parent)
    , m_index(index)
    , m_tree(new QTreeWidget(this))
    , m_empty(new QLabel(this))
{
    m_tree->setColumnCount(2);
    m_tree->setHeaderHidden(true);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setIndentation(14);
    m_tree->setRootIsDecorated(true);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_empty->setForegroundRole(QPalette::PlaceholderText);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tree);
    layout->addWidget(m_empty);
    layout->addStretch();

    connect(m_index, &NoteIndex::changed, this, [this] {
        if (isVisible())
            rebuild();
    });
    connect(m_tree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *item) { emit tagActivated(item->data(0, tagRole).toString()); });
    rebuild();
}

void TagPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    rebuild();
}

void TagPanel::rebuild()
{
    const QHash<QString, int> counts = m_index->tagCounts();
    QStringList tags = counts.keys();
    tags.sort(Qt::CaseInsensitive);
    m_tree->clear();
    QHash<QString, QTreeWidgetItem *> items;
    for (const QString &tag : std::as_const(tags)) {
        const int slash = int(tag.lastIndexOf(u'/'));
        QTreeWidgetItem *parent = slash > 0 ? items.value(tag.left(slash)) : nullptr;
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        item->setText(0, u'#' + (slash > 0 ? tag.mid(slash + 1) : tag));
        item->setText(1, QString::number(counts.value(tag)));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setForeground(1, palette().brush(QPalette::PlaceholderText));
        item->setToolTip(0, u'#' + tag);
        item->setData(0, tagRole, tag);
        items.insert(tag, item);
    }
    const bool empty = tags.isEmpty();
    m_tree->setVisible(!empty);
    m_empty->setVisible(empty);
    m_empty->setText(m_index->root().isEmpty() ? tr("Open a folder to see its tags.")
                     : m_index->scanning()     ? tr("Reading notes…")
                                               : tr("No tags in this folder."));
}
