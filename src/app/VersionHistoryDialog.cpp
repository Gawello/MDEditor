#include "app/VersionHistoryDialog.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QGuiApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

VersionHistoryDialog::VersionHistoryDialog(const QString &notePath, const QList<Snapshots::Entry> &entries, QWidget *parent)
    : QDialog(parent)
    , m_entries(entries)
    , m_list(new QListWidget(this))
    , m_preview(new QPlainTextEdit(this))
{
    setWindowTitle(tr("Version History – %1").arg(QFileInfo(notePath).fileName()));
    resize(860, 560);
    m_preview->setReadOnly(true);
    m_list->setMaximumWidth(260);
    const QLocale locale;
    for (const Snapshots::Entry &entry : entries) {
        auto *item = new QListWidgetItem(locale.toString(entry.time, QLocale::ShortFormat), m_list);
        item->setToolTip(tr("%1 bytes").arg(entry.size));
    }
    auto *empty = new QLabel(tr("No versions of this note yet. A copy is kept when a note is opened, and when it's "
                                "saved (at most every few minutes; see Settings → Notes)."), this);
    empty->setWordWrap(true);
    empty->setVisible(entries.isEmpty());

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_restore = buttons->addButton(tr("&Restore This Version"), QDialogButtonBox::AcceptRole);
    QPushButton *copy = buttons->addButton(tr("&Copy"), QDialogButtonBox::ActionRole);
    m_restore->setEnabled(false);
    copy->setEnabled(false);

    auto *content = new QHBoxLayout;
    content->addWidget(m_list);
    content->addWidget(m_preview, 1);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(empty);
    layout->addLayout(content, 1);
    layout->addWidget(buttons);

    connect(m_list, &QListWidget::currentRowChanged, this, [this, copy](int row) {
        const bool valid = row >= 0 && row < m_entries.size();
        m_preview->setPlainText(valid ? Snapshots::read(m_entries[row]) : QString());
        m_restore->setEnabled(valid);
        copy->setEnabled(valid);
    });
    connect(copy, &QPushButton::clicked, this, [this] { QGuiApplication::clipboard()->setText(selectedText()); });
    connect(m_restore, &QPushButton::clicked, this, [this] {
        emit restoreRequested(selectedText());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    if (!entries.isEmpty())
        m_list->setCurrentRow(0);
}

QString VersionHistoryDialog::selectedText() const
{
    return m_preview->toPlainText();
}
