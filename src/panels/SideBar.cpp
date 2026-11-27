#include "panels/SideBar.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

SideBar::SideBar(QWidget *parent)
    : QWidget(parent)
    , m_buttons(new QButtonGroup(this))
    , m_header(new QWidget(this))
    , m_pages(new QStackedWidget(this))
{
    auto *header = new QHBoxLayout(m_header);
    header->setContentsMargins(4, 4, 4, 0);
    header->setSpacing(2);
    header->addStretch();
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(m_header);
    layout->addWidget(m_pages, 1);
    connect(m_buttons, &QButtonGroup::idClicked, m_pages, &QStackedWidget::setCurrentIndex);
}

int SideBar::addPage(const QString &title, QWidget *page)
{
    const int index = m_pages->addWidget(page);
    auto *button = new QToolButton(m_header);
    button->setText(title);
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    m_buttons->addButton(button, index);
    auto *header = static_cast<QHBoxLayout *>(m_header->layout());
    header->insertWidget(header->count() - 1, button);
    if (index == 0)
        button->setChecked(true);
    return index;
}

void SideBar::showPage(QWidget *page)
{
    const int index = m_pages->indexOf(page);
    if (index < 0)
        return;
    m_pages->setCurrentIndex(index);
    if (QAbstractButton *button = m_buttons->button(index))
        button->setChecked(true);
}

QWidget *SideBar::currentPage() const
{
    return m_pages->currentWidget();
}
