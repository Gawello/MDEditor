#include "app/TabCloseButton.h"

#include <QPainter>

TabCloseButton::TabCloseButton(QWidget *parent)
    : QAbstractButton(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::ArrowCursor);
    setToolTip(tr("Close"));
    resize(sizeHint());
}

void TabCloseButton::setModified(bool modified)
{
    if (modified == m_modified)
        return;
    m_modified = modified;
    update();
}

void TabCloseButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool hovered = underMouse();
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);

    QColor color = palette().color(QPalette::WindowText);
    if (hovered) {
        QColor background = color;
        background.setAlphaF(0.15f);
        painter.setPen(Qt::NoPen);
        painter.setBrush(background);
        painter.drawRoundedRect(r, 4, 4);
    }
    color.setAlphaF(hovered ? 0.9f : 0.55f);

    if (m_modified && !hovered) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(r.center(), 3.5, 3.5);
        return;
    }

    const QRectF cross = r.adjusted(4.5, 4.5, -4.5, -4.5);
    painter.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(cross.topLeft(), cross.bottomRight());
    painter.drawLine(cross.topRight(), cross.bottomLeft());
}

void TabCloseButton::enterEvent(QEnterEvent *event)
{
    QAbstractButton::enterEvent(event);
    update();
}

void TabCloseButton::leaveEvent(QEvent *event)
{
    QAbstractButton::leaveEvent(event);
    update();
}
