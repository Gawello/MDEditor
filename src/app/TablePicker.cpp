#include "app/TablePicker.h"

#include "editor/EditorTheme.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>

#include <algorithm>

TablePicker::TablePicker(QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setFixedSize(2 * margin + gridColumns * cellSize + (gridColumns - 1) * gap,
                 2 * margin + gridRows * cellSize + (gridRows - 1) * gap + labelHeight);
}

void TablePicker::popup(const QPoint &globalPos)
{
    QPoint pos = globalPos;
    if (const QScreen *screen = QGuiApplication::screenAt(globalPos)) {
        const QRect available = screen->availableGeometry();
        pos.setX(std::clamp(pos.x(), available.left(), available.right() - width()));
        pos.setY(std::clamp(pos.y(), available.top(), available.bottom() - height()));
    }
    move(pos);
    show();
    setFocus();
}

QRect TablePicker::cellRect(int row, int column) const
{
    return QRect(margin + column * (cellSize + gap), margin + row * (cellSize + gap), cellSize, cellSize);
}

void TablePicker::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette p = palette();
    const QColor window = p.color(QPalette::Window);
    const QColor text = p.color(QPalette::WindowText);
    const QColor highlight = p.color(QPalette::Highlight);

    painter.setPen(EditorTheme::mix(window, text, 0.25));
    painter.setBrush(window);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);

    for (int r = 0; r < gridRows; ++r) {
        for (int c = 0; c < gridColumns; ++c) {
            const bool selected = r < m_rows && c < m_columns;
            QColor fill = selected ? highlight : p.color(QPalette::Base);
            if (selected)
                fill.setAlphaF(0.55f);
            painter.setPen(selected ? highlight : EditorTheme::mix(window, text, 0.3));
            painter.setBrush(fill);
            painter.drawRoundedRect(QRectF(cellRect(r, c)).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        }
    }

    const QString label = m_rows > 0 ? tr("%1 × %2 table").arg(m_columns).arg(m_rows) : tr("Insert table");
    painter.setPen(text);
    painter.drawText(QRect(margin, height() - margin - labelHeight + 6, width() - 2 * margin, labelHeight - 6),
                     Qt::AlignCenter, label);
}

void TablePicker::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    int rows = 0;
    int columns = 0;
    for (int r = 0; r < gridRows; ++r) {
        for (int c = 0; c < gridColumns; ++c) {
            if (cellRect(r, c).adjusted(-gap, -gap, 0, 0).contains(pos)) {
                rows = r + 1;
                columns = c + 1;
            }
        }
    }
    if (rows != m_rows || columns != m_columns) {
        m_rows = rows;
        m_columns = columns;
        update();
    }
}

void TablePicker::mouseReleaseEvent(QMouseEvent *event)
{
    if (rect().contains(event->position().toPoint()))
        choose();
}

void TablePicker::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Right:
        m_columns = std::min(gridColumns, m_columns + 1);
        m_rows = std::max(m_rows, 1);
        break;
    case Qt::Key_Left:
        m_columns = std::max(1, m_columns - 1);
        break;
    case Qt::Key_Down:
        m_rows = std::min(gridRows, m_rows + 1);
        m_columns = std::max(m_columns, 1);
        break;
    case Qt::Key_Up:
        m_rows = std::max(1, m_rows - 1);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        choose();
        return;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    update();
}

void TablePicker::choose()
{
    if (m_rows > 0 && m_columns > 0)
        emit picked(m_rows, m_columns);
    close();
}
