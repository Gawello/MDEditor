#include "panels/PropertiesPanel.h"

#include "markdown/NoteScan.h"
#include "notes/NoteIndex.h"

#include <QHeaderView>
#include <QSet>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int queryRole = Qt::UserRole;
constexpr int maxValues = 30;
}

PropertiesPanel::PropertiesPanel(NoteIndex *index, QWidget *parent)
    : QWidget(parent)
    , m_index(index)
    , m_tree(new QTreeWidget(this))
    , m_timer(new QTimer(this))
{
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(2);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setIndentation(14);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tree);
    m_timer->setSingleShot(true);
    m_timer->setInterval(300);
    connect(m_timer, &QTimer::timeout, this, &PropertiesPanel::rebuild);
    connect(m_index, &NoteIndex::changed, m_timer, qOverload<>(&QTimer::start));
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        emit searchRequested(item->data(0, queryRole).toString());
    });
}

void PropertiesPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    rebuild(); // not kept up to date while hidden
}

void PropertiesPanel::rebuild()
{
    m_timer->stop();
    if (!isVisible())
        return;
    // Notes per key, and per value of each key.
    QHash<QString, int> keys;
    QHash<QString, QHash<QString, int>> values;
    for (const auto &note : m_index->notes()) {
        QSet<QString> seen;
        for (const md::Property &property : note->scan.properties) {
            const QString key = QString::fromStdU16String(property.key);
            if (seen.contains(key))
                continue;
            seen.insert(key);
            ++keys[key];
            QSet<QString> valuesSeen;
            for (const std::u16string &value : md::propertyList(property.value)) {
                const QString text = QString::fromStdU16String(value).trimmed();
                if (!text.isEmpty() && !valuesSeen.contains(text)) {
                    valuesSeen.insert(text);
                    ++values[key][text];
                }
            }
        }
    }
    QStringList order = keys.keys();
    std::sort(order.begin(), order.end(), [&keys](const QString &a, const QString &b) {
        return keys.value(a) != keys.value(b) ? keys.value(a) > keys.value(b) : a.compare(b, Qt::CaseInsensitive) < 0;
    });
    // Keep which keys were open.
    QSet<QString> open;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        if (m_tree->topLevelItem(i)->isExpanded())
            open.insert(m_tree->topLevelItem(i)->text(0));
    }
    m_tree->clear();
    for (const QString &key : std::as_const(order)) {
        auto *item = new QTreeWidgetItem(m_tree, {key, QString::number(keys.value(key))});
        item->setData(0, queryRole, u'[' + key + u']');
        item->setForeground(1, palette().brush(QPalette::PlaceholderText));
        const QHash<QString, int> &counts = values[key];
        QStringList texts = counts.keys();
        std::sort(texts.begin(), texts.end(), [&counts](const QString &a, const QString &b) {
            return counts.value(a) != counts.value(b) ? counts.value(a) > counts.value(b) : a.compare(b, Qt::CaseInsensitive) < 0;
        });
        for (const QString &text : texts.mid(0, maxValues)) {
            auto *child = new QTreeWidgetItem(item, {text, QString::number(counts.value(text))});
            const QString quoted = text.contains(u' ') ? u'"' + text + u'"' : text;
            child->setData(0, queryRole, QStringLiteral("[%1:%2]").arg(key, quoted));
            child->setForeground(1, palette().brush(QPalette::PlaceholderText));
        }
        item->setExpanded(open.contains(key));
    }
}
