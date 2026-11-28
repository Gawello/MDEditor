#include "app/HotkeysPage.h"

#include "app/Hotkeys.h"

#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
constexpr int idRole = Qt::UserRole;
}

HotkeysPage::HotkeysPage(QWidget *parent)
    : QWidget(parent)
    , m_filter(new QLineEdit(this))
    , m_table(new QTableWidget(this))
    , m_warning(new QLabel(this))
{
    m_filter->setPlaceholderText(tr("Filter by command or shortcut…"));
    m_filter->setClearButtonEnabled(true);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("Command"), tr("Shortcut"), QString()});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->setColumnWidth(1, 170);
    m_table->verticalHeader()->hide();
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_warning->setWordWrap(true);
    m_warning->setStyleSheet(QStringLiteral("color: #c0392b;"));
    m_warning->hide();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_filter);
    layout->addWidget(m_table, 1);
    layout->addWidget(m_warning);
    auto *hint = new QLabel(tr("Click a shortcut and press the new keys; Backspace removes it."), this);
    hint->setForegroundRole(QPalette::PlaceholderText);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    connect(m_filter, &QLineEdit::textChanged, this, &HotkeysPage::filter);
    fill();
}

void HotkeysPage::fill()
{
    Hotkeys &hotkeys = Hotkeys::instance();
    const QList<Hotkeys::Entry> &entries = hotkeys.entries();
    m_table->setRowCount(int(entries.size()));
    for (int row = 0; row < entries.size(); ++row) {
        const Hotkeys::Entry &entry = entries[row];
        auto *name = new QTableWidgetItem(entry.menu + QStringLiteral(" › ") + entry.name);
        name->setData(idRole, entry.id);
        m_table->setItem(row, 0, name);

        auto *edit = new QKeySequenceEdit(m_table);
        edit->setMaximumSequenceLength(1);
        edit->setClearButtonEnabled(true);
        const QList<QKeySequence> current = hotkeys.shortcuts(entry.id);
        edit->setKeySequence(current.value(0));
        if (current.size() > 1)
            edit->setToolTip(tr("Also: %1").arg(QKeySequence::listToString(current.mid(1), QKeySequence::NativeText)));
        m_table->setCellWidget(row, 1, edit);

        auto *reset = new QPushButton(tr("Reset"), m_table);
        reset->setEnabled(hotkeys.isCustomized(entry.id));
        m_table->setCellWidget(row, 2, reset);

        const QString id = entry.id;
        connect(edit, &QKeySequenceEdit::editingFinished, this, [this, edit, reset, id] {
            QList<QKeySequence> shortcuts;
            if (!edit->keySequence().isEmpty())
                shortcuts << edit->keySequence();
            Hotkeys::instance().setShortcuts(id, shortcuts);
            reset->setEnabled(Hotkeys::instance().isCustomized(id));
            showConflicts(id);
        });
        connect(edit, &QKeySequenceEdit::keySequenceChanged, this, [edit](const QKeySequence &sequence) {
            if (sequence.isEmpty())
                emit edit->editingFinished(); // the clear button: no shortcut
        });
        connect(reset, &QPushButton::clicked, this, [this, edit, reset, id] {
            Hotkeys::instance().reset(id);
            const QSignalBlocker block(edit);
            edit->setKeySequence(Hotkeys::instance().shortcuts(id).value(0));
            reset->setEnabled(false);
            showConflicts(id);
        });
    }
}

void HotkeysPage::showConflicts(const QString &id)
{
    const QKeySequence sequence = Hotkeys::instance().shortcuts(id).value(0);
    const QStringList others = Hotkeys::instance().conflicts(id, sequence);
    m_warning->setVisible(!others.isEmpty());
    if (!others.isEmpty())
        m_warning->setText(tr("%1 is also the shortcut of %2. Only one of them will work: change one.")
                               .arg(sequence.toString(QKeySequence::NativeText), others.join(QStringLiteral(", "))));
}

void HotkeysPage::filter(const QString &text)
{
    const QString wanted = text.trimmed();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QString name = m_table->item(row, 0)->text();
        const auto *edit = qobject_cast<QKeySequenceEdit *>(m_table->cellWidget(row, 1));
        const QString keys = edit ? edit->keySequence().toString(QKeySequence::NativeText) : QString();
        m_table->setRowHidden(row, !wanted.isEmpty() && !name.contains(wanted, Qt::CaseInsensitive)
                                       && !keys.contains(wanted, Qt::CaseInsensitive));
    }
}
