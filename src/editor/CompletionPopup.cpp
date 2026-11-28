#include "editor/CompletionPopup.h"

#include <QApplication>
#include <QListWidget>
#include <QPainter>
#include <QScreen>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int detailRole = Qt::UserRole + 1;
constexpr int visibleRows = 10;

// The label, and the detail dimmed on the right.
class Delegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        opt.text.clear();
        QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
        const bool selected = opt.state & QStyle::State_Selected;
        const QColor text = opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
        QColor faint = text;
        faint.setAlphaF(0.6f);
        QRect r = opt.rect.adjusted(8, 0, -8, 0);
        const QString detail = index.data(detailRole).toString();
        painter->save();
        painter->setFont(opt.font);
        if (!detail.isEmpty()) {
            const QString shown = opt.fontMetrics.elidedText(detail, Qt::ElideLeft, r.width() / 2);
            painter->setPen(faint);
            painter->drawText(r, Qt::AlignRight | Qt::AlignVCenter, shown);
            r.setRight(r.right() - opt.fontMetrics.horizontalAdvance(shown) - 12);
        }
        painter->setPen(text);
        painter->drawText(r, Qt::AlignLeft | Qt::AlignVCenter,
                          opt.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideMiddle, r.width()));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(size.height(), option.fontMetrics.height() + 8));
        return size;
    }
};

} // namespace

CompletionPopup::CompletionPopup(QWidget *editor)
    : QWidget(editor, Qt::ToolTip | Qt::FramelessWindowHint)
    , m_list(new QListWidget(this))
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_StyledBackground); // a plain QWidget draws no style sheet border or background
    setFocusPolicy(Qt::NoFocus);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setItemDelegate(new Delegate(m_list));
    m_list->setUniformItemSizes(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setFrameShape(QFrame::NoFrame);
    setObjectName(QStringLiteral("completion"));
    setStyleSheet(QStringLiteral("QWidget#completion { border: 1px solid palette(mid); background: palette(base); }"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->addWidget(m_list);
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { emit activated(m_list->row(item)); });
}

void CompletionPopup::setItems(const QList<CompletionItem> &items)
{
    m_items = items;
    m_list->clear();
    for (const CompletionItem &item : items) {
        auto *row = new QListWidgetItem(item.label, m_list);
        row->setData(detailRole, item.detail);
        row->setToolTip(item.insert);
    }
    m_list->setCurrentRow(items.isEmpty() ? -1 : 0);
}

void CompletionPopup::showAt(const QPoint &globalPos, int aboveY)
{
    const int rowHeight = std::max(m_list->sizeHintForRow(0), fontMetrics().height() + 8);
    const int rows = std::min<int>(int(m_items.size()), visibleRows);
    int width = 320;
    for (int i = 0; i < std::min<int>(int(m_items.size()), 50); ++i) {
        width = std::max(width, fontMetrics().horizontalAdvance(m_items[i].label)
                                    + fontMetrics().horizontalAdvance(m_items[i].detail) + 48);
    }
    width = std::min(width, 560);
    resize(width, rows * rowHeight + 2);

    QPoint pos = globalPos;
    if (const QScreen *screen = QGuiApplication::screenAt(globalPos)) {
        const QRect area = screen->availableGeometry();
        if (pos.y() + height() > area.bottom())
            pos.setY(aboveY - height());
        pos.setX(std::clamp(pos.x(), area.left(), std::max(area.left(), area.right() - width)));
    }
    QWidget::move(pos);
    show();
    raise();
}

void CompletionPopup::move(int rows)
{
    const int count = m_list->count();
    if (count == 0)
        return;
    int row = m_list->currentRow() + rows;
    if (rows == 1 || rows == -1)
        row = (row + count) % count;
    m_list->setCurrentRow(std::clamp(row, 0, count - 1));
}

int CompletionPopup::currentRow() const
{
    return m_list->currentRow();
}

void CompletionPopup::setCurrentRow(int row)
{
    m_list->setCurrentRow(row);
}
