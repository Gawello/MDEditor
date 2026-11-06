#include "app/ExternalChangeBar.h"

#include "editor/EditorTheme.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

ExternalChangeBar::ExternalChangeBar(QWidget *parent)
    : QFrame(parent)
    , m_label(new QLabel(this))
    , m_reload(new QPushButton(tr("Reload"), this))
    , m_keep(new QPushButton(tr("Keep My Version"), this))
{
    m_label->setWordWrap(true);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->addWidget(m_label, 1);
    layout->addWidget(m_reload);
    layout->addWidget(m_keep);
    setAutoFillBackground(true);
    updateColors();
    hide();

    connect(m_reload, &QPushButton::clicked, this, &ExternalChangeBar::reloadRequested);
    connect(m_keep, &QPushButton::clicked, this, &ExternalChangeBar::keepRequested);
}

void ExternalChangeBar::showChanged(const QString &fileName)
{
    m_label->setText(tr("%1 changed on disk, and you have unsaved changes here.").arg(fileName));
    m_reload->show();
    show();
}

void ExternalChangeBar::showMissing(const QString &fileName)
{
    m_label->setText(tr("%1 is no longer available. It may have been deleted or moved, or its drive may be "
                        "disconnected.").arg(fileName));
    m_reload->hide();
    show();
}

void ExternalChangeBar::changeEvent(QEvent *event)
{
    QFrame::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        updateColors();
}

void ExternalChangeBar::updateColors()
{
    // From the application palette: this widget's own Window color is the result of this very mix.
    const QPalette app = QApplication::palette();
    const QColor color = EditorTheme::mix(app.color(QPalette::Window), app.color(QPalette::Highlight), 0.2);
    if (palette().color(QPalette::Window) == color)
        return; // setPalette() sends another PaletteChange
    QPalette colors = palette();
    colors.setColor(QPalette::Window, color);
    setPalette(colors);
}
