#include "editor/NotePreview.h"

#include <QCursor>
#include <QGuiApplication>
#include <QLabel>
#include <QScreen>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

NotePreview::NotePreview(QWidget *editor)
    : QWidget(editor, Qt::ToolTip | Qt::FramelessWindowHint)
    , m_title(new QLabel(this))
    , m_browser(new QTextBrowser(this))
    , m_watch(new QTimer(this))
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_StyledBackground); // a plain QWidget draws no style sheet border or background
    setFocusPolicy(Qt::NoFocus);
    setObjectName(QStringLiteral("preview"));
    setStyleSheet(QStringLiteral("QWidget#preview { border: 1px solid palette(mid); background: palette(base); }"));
    QFont bold = m_title->font();
    bold.setBold(true);
    m_title->setFont(bold);
    m_title->setForegroundRole(QPalette::PlaceholderText);
    m_browser->setFrameShape(QFrame::NoFrame);
    m_browser->setFocusPolicy(Qt::NoFocus);
    m_browser->setOpenLinks(false);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 4, 4);
    layout->addWidget(m_title);
    layout->addWidget(m_browser);
    m_watch->setInterval(200);
    connect(m_watch, &QTimer::timeout, this, &NotePreview::checkMouse);
}

void NotePreview::showNote(const QString &title, const QString &markdown, const QString &folder,
                           const QPoint &globalPos, const QRect &keepOpenOver)
{
    m_title->setText(title);
    m_browser->setSearchPaths({folder});
    m_browser->document()->setBaseUrl(QUrl::fromLocalFile(folder + u'/'));
    m_browser->setMarkdown(markdown);
    m_keepOpenOver = keepOpenOver;
    resize(480, 360);
    QPoint pos = globalPos;
    if (const QScreen *screen = QGuiApplication::screenAt(globalPos)) {
        const QRect area = screen->availableGeometry();
        if (pos.y() + height() > area.bottom())
            pos.setY(keepOpenOver.top() - height() - 4);
        pos.setX(std::clamp(pos.x(), area.left(), std::max(area.left(), area.right() - width())));
    }
    move(pos);
    show();
    raise();
    m_watch->start();
}

QString NotePreview::title() const
{
    return m_title->text();
}

void NotePreview::checkMouse()
{
    const QPoint mouse = QCursor::pos();
    if (geometry().adjusted(-8, -8, 8, 8).contains(mouse) || m_keepOpenOver.contains(mouse))
        return;
    m_watch->stop();
    hide();
}
