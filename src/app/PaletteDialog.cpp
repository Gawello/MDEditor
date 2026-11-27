#include "app/PaletteDialog.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int detailRole = Qt::UserRole + 1;
constexpr int shortcutRole = Qt::UserRole + 2;
constexpr int maxShown = 300;

// Text, then the detail dimmed, and the shortcut on the right.
class ItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QString detail = index.data(detailRole).toString();
        const QString shortcut = index.data(shortcutRole).toString();
        opt.text.clear();
        QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        const bool selected = opt.state & QStyle::State_Selected;
        const QPalette &palette = opt.palette;
        const QColor text = palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
        QColor faint = text;
        faint.setAlphaF(0.6f);
        QRect r = opt.rect.adjusted(10, 0, -10, 0);
        painter->save();
        painter->setFont(opt.font);
        if (!shortcut.isEmpty()) {
            painter->setPen(faint);
            painter->drawText(r, Qt::AlignRight | Qt::AlignVCenter, shortcut);
            r.setRight(r.right() - opt.fontMetrics.horizontalAdvance(shortcut) - 16);
        }
        const QString main = index.data(Qt::DisplayRole).toString();
        const QString elided = opt.fontMetrics.elidedText(main, Qt::ElideMiddle, r.width());
        painter->setPen(text);
        painter->drawText(r, Qt::AlignLeft | Qt::AlignVCenter, elided);
        const int used = opt.fontMetrics.horizontalAdvance(elided) + 12;
        if (!detail.isEmpty() && used < r.width()) {
            painter->setPen(faint);
            painter->drawText(r.adjusted(used, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                              opt.fontMetrics.elidedText(detail, Qt::ElideLeft, r.width() - used));
        }
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(size.height(), option.fontMetrics.height() + 12));
        return size;
    }
};

} // namespace

PaletteDialog::PaletteDialog(QWidget *window, const QString &placeholder, const QString &hint)
    : QDialog(window, Qt::FramelessWindowHint | Qt::Dialog)
    , m_search(new QLineEdit(this))
    , m_list(new QListWidget(this))
    , m_hint(new QLabel(hint, this))
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    m_search->setPlaceholderText(placeholder);
    m_search->setClearButtonEnabled(true);
    m_search->installEventFilter(this);
    QFont big = m_search->font();
    big.setPointSizeF(big.pointSizeF() * 1.15);
    m_search->setFont(big);
    m_search->setMinimumHeight(m_search->fontMetrics().height() + 14);
    m_list->setItemDelegate(new ItemDelegate(m_list));
    m_list->setUniformItemSizes(true);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_hint->setForegroundRole(QPalette::PlaceholderText);
    m_hint->setVisible(!hint.isEmpty());

    // Frameless: a border of its own, so it stands out from the editor behind it.
    setObjectName(QStringLiteral("palette"));
    setStyleSheet(QStringLiteral("QDialog#palette { border: 1px solid palette(mid); border-radius: 8px; }"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(m_search);
    layout->addWidget(m_list, 1);
    layout->addWidget(m_hint);

    connect(m_search, &QLineEdit::textChanged, this, &PaletteDialog::filter);
    connect(m_list, &QListWidget::itemClicked, this, [this] { accept(); });

    if (window) {
        const QWidget *top = window->window();
        const int width = std::min(640, top->width() - 40);
        resize(width, std::min(420, top->height() - 120));
        move(top->mapToGlobal(QPoint((top->width() - width) / 2, 60)));
    }
}

void PaletteDialog::setItems(QList<Item> items)
{
    m_items = std::move(items);
    filter();
}

int PaletteDialog::score(const QString &query, const QString &text)
{
    if (query.isEmpty())
        return 0;
    int score = 0;
    int at = 0;
    int previous = -2;
    for (const QChar c : query) {
        if (c.isSpace())
            continue;
        const int found = int(text.indexOf(c, at, Qt::CaseInsensitive));
        if (found < 0)
            return -1;
        score += 1;
        if (found == previous + 1)
            score += 5; // consecutive
        if (found == 0 || !text.at(found - 1).isLetterOrNumber())
            score += 8; // start of a word
        previous = found;
        at = found + 1;
    }
    if (text.startsWith(query, Qt::CaseInsensitive))
        score += 20;
    else if (text.contains(query, Qt::CaseInsensitive))
        score += 10;
    return score * 1000 - int(text.size()); // shorter first among equals
}

void PaletteDialog::filter()
{
    const QString query = m_search->text().trimmed();
    QList<std::pair<int, int>> ranked; // score, index
    for (int i = 0; i < m_items.size(); ++i) {
        const int s = score(query, m_items[i].text);
        // The detail (folder, menu) counts too, a little less.
        const int d = query.isEmpty() ? -1 : score(query, m_items[i].detail + u' ' + m_items[i].text);
        const int best = std::max(s, d >= 0 ? d / 2 : -1);
        if (best >= 0 || query.isEmpty())
            ranked.append({query.isEmpty() ? 0 : best, i});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });

    m_shown.clear();
    for (const auto &[s, i] : ranked) {
        if (m_shown.size() >= maxShown)
            break;
        m_shown << m_items[i];
    }
    if (m_fallback && !query.isEmpty()) {
        if (const std::optional<Item> extra = m_fallback(query))
            m_shown << *extra;
    }

    m_list->clear();
    for (const Item &item : std::as_const(m_shown)) {
        auto *row = new QListWidgetItem(item.text, m_list);
        row->setData(detailRole, item.detail);
        row->setData(shortcutRole, item.shortcut);
    }
    m_list->setCurrentRow(m_shown.isEmpty() ? -1 : 0);
}

bool PaletteDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        const int count = m_list->count();
        switch (key->key()) {
        case Qt::Key_Down:
        case Qt::Key_Up:
        case Qt::Key_PageDown:
        case Qt::Key_PageUp: {
            if (count == 0)
                return true;
            const int step = (key->key() == Qt::Key_PageDown || key->key() == Qt::Key_PageUp) ? 10 : 1;
            const int direction = (key->key() == Qt::Key_Down || key->key() == Qt::Key_PageDown) ? step : -step;
            int row = m_list->currentRow() + direction;
            if (step == 1)
                row = (row + count) % count; // wraps around
            m_list->setCurrentRow(std::clamp(row, 0, count - 1));
            return true;
        }
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if ((key->modifiers() & Qt::ShiftModifier) && m_fallback) {
                if (const std::optional<Item> extra = m_fallback(m_search->text().trimmed())) {
                    m_shown << *extra;
                    m_list->addItem(extra->text);
                    m_list->setCurrentRow(m_list->count() - 1);
                }
            }
            accept();
            return true;
        default:
            break;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void PaletteDialog::accept()
{
    m_chosen = m_list->currentRow();
    if (m_chosen < 0 || m_chosen >= m_shown.size())
        return;
    QDialog::accept();
}

bool PaletteDialog::choose()
{
    m_search->setFocus();
    filter();
    if (exec() != QDialog::Accepted || m_chosen < 0 || m_chosen >= m_shown.size())
        return false;
    if (const auto &run = m_shown[m_chosen].run)
        run();
    return true;
}
