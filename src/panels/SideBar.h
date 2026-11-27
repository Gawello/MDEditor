#pragma once

#include <QWidget>

class QButtonGroup;
class QStackedWidget;
class QToolButton;

// The right sidebar: pages (outline, search, tags) behind a row of toggle buttons.
class SideBar : public QWidget {
    Q_OBJECT

public:
    explicit SideBar(QWidget *parent = nullptr);

    int addPage(const QString &title, QWidget *page);
    void showPage(QWidget *page);
    QWidget *currentPage() const;

private:
    QButtonGroup *m_buttons;
    QWidget *m_header;
    QStackedWidget *m_pages;
};
