#include "app/SymbolPicker.h"

#include "app/SymbolSets.h"
#include "editor/EditorTheme.h"
#include "editor/LivePreviewHighlighter.h"
#include "markdown/Entities.h"

#include <QApplication>
#include <QGridLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QScrollArea>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int columns = 10;
constexpr int cellSize = 34;
constexpr int symbolPointSize = 14;
constexpr int deactivationDelay = 200;
constexpr int margin = 8;

// "&rarr;  U+2192": what to type to get the symbol, then its number. Written as rich text on purpose: left to
// guess, Qt could take a plain "&lt;" for rich text and show "<" instead.
QString tooltip(const QString &symbol)
{
    QStringList codes;
    const QList<uint> values = symbol.toUcs4();
    for (const uint value : values)
        codes << QStringLiteral("U+%1").arg(value, 4, 16, QLatin1Char('0')).toUpper();
    const QString entity = QString::fromStdU16String(md::entityFor(toView(symbol)));
    return QStringLiteral("<code>%1</code>&nbsp;&nbsp;%2").arg(entity.toHtmlEscaped(), codes.join(u' '));
}

} // namespace

SymbolPicker::SymbolPicker(QWidget *toggleButton, QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus)
    , m_toggleButton(toggleButton)
{
    auto *tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);
    tabs->setUsesScrollButtons(true);
    tabs->setFocusPolicy(Qt::NoFocus);
    tabs->tabBar()->setFocusPolicy(Qt::NoFocus);
    for (const SymbolSet &set : symbolSets())
        tabs->addTab(createPage(set.symbols), set.name);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(margin, margin, margin, margin);
    layout->addWidget(tabs);
    setFixedSize(columns * cellSize + 44, 340);

    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive || !isVisible())
            return;
        // Going from one of our windows to another can pass through "inactive" for a moment.
        QTimer::singleShot(deactivationDelay, this, [this] {
            if (QGuiApplication::applicationState() != Qt::ApplicationActive)
                hide();
        });
    });
}

void SymbolPicker::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    qApp->installEventFilter(this); // only while open: every event of the application passes through it
}

void SymbolPicker::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    qApp->removeEventFilter(this);
}

// The application's events: this window never has the focus, so Esc reaches it only this way.
bool SymbolPicker::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        const auto *widget = qobject_cast<QWidget *>(watched);
        if (widget && widget->window() != this && widget != m_toggleButton)
            hide();
        break;
    }
    case QEvent::KeyPress:
        if (static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            hide();
            return true; // used up: it mustn't also close the find bar
        }
        break;
    case QEvent::Move:
    case QEvent::Resize:
        if (watched == parentWidget())
            hide(); // anchored to a button of that window, and there's no title bar to drag it along
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

// The same frame as TablePicker.
void SymbolPicker::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette colors = palette();
    const QColor window = colors.color(QPalette::Window);
    painter.setPen(EditorTheme::mix(window, colors.color(QPalette::WindowText), 0.25));
    painter.setBrush(window);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
}

QWidget *SymbolPicker::createPage(const QStringList &symbols)
{
    QFont font = this->font();
    font.setFamilies(EditorTheme::withEmoji(font));
    font.setPointSize(symbolPointSize);

    auto *grid = new QWidget;
    auto *layout = new QGridLayout(grid);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    for (int i = 0; i < symbols.size(); ++i) {
        const QString symbol = symbols[i];
        auto *button = new QToolButton(grid);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedSize(cellSize, cellSize);
        button->setFont(font);
        button->setText(symbol);
        button->setToolTip(tooltip(symbol));
        connect(button, &QToolButton::clicked, this, [this, symbol] { emit symbolPicked(symbol); });
        layout->addWidget(button, i / columns, i % columns);
    }
    layout->setRowStretch(symbols.size() / columns + 1, 1);
    layout->setColumnStretch(columns, 1);

    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(grid);
    return scroll;
}

void SymbolPicker::showAt(const QPoint &globalTopRight)
{
    QPoint position = globalTopRight - QPoint(width(), 0);
    if (const QScreen *screen = QGuiApplication::screenAt(globalTopRight)) {
        const QRect available = screen->availableGeometry();
        position.setX(std::clamp(position.x(), available.left(), available.right() - width()));
        position.setY(std::clamp(position.y(), available.top(), available.bottom() - height()));
    }
    move(position);
    show();
    raise();
}
