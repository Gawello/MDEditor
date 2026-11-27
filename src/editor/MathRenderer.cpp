#include "editor/MathRenderer.h"

#include "latex.h"
#include "platform/qt/graphic_qt.h"

#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>

#include <exception>
#include <memory>

namespace {

constexpr int maxCacheSize = 500;
constexpr int maxWidth = 4000; // MicroTeX wraps nothing at this width; formulas are clipped by the editor

} // namespace

MathRenderer &MathRenderer::instance()
{
    static MathRenderer renderer;
    return renderer;
}

void MathRenderer::shutdown()
{
    MathRenderer &renderer = instance();
    renderer.m_cache.clear();
    if (renderer.m_initialized)
        tex::LaTeX::release();
    renderer.m_initialized = false;
}

const MathRenderer::Formula &MathRenderer::render(const QString &tex, qreal textSize, const QColor &color, bool display,
                                                  qreal devicePixelRatio)
{
    const QString key = QStringLiteral("%1|%2|%3|%4|%5")
                            .arg(display ? u'D' : u'I')
                            .arg(textSize)
                            .arg(color.rgba())
                            .arg(devicePixelRatio)
                            .arg(tex);
    auto it = m_cache.find(key);
    if (it != m_cache.end())
        return *it;
    if (m_cache.size() > maxCacheSize)
        m_cache.clear(); // simple: formulas on screen come back on the next paint
    return *m_cache.insert(key, draw(tex, textSize, color, display, devicePixelRatio));
}

MathRenderer::Formula MathRenderer::draw(const QString &tex, qreal textSize, const QColor &color, bool display,
                                         qreal devicePixelRatio)
{
    if (!m_initialized) {
        // The resources aren't on disk: MicroTeX falls back to ":/res/..." for its fonts.
        tex::LaTeX::init("res");
        m_initialized = true;
    }

    Formula formula;
    const QString source = (display ? QStringLiteral("\\displaystyle ") : QStringLiteral("\\textstyle ")) + tex;
    try {
        std::unique_ptr<tex::TeXRender> render(
            tex::LaTeX::parse(source.toStdWString(), maxWidth, float(textSize), float(textSize) / 3, color.rgba()));
        if (render) {
            const int width = render->getWidth() + 2;
            const int height = render->getHeight() + 2;
            QPixmap pixmap(QSize(width, height) * devicePixelRatio);
            pixmap.setDevicePixelRatio(devicePixelRatio);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::TextAntialiasing);
            tex::Graphics2D_qt graphics(&painter);
            render->draw(graphics, 1, 1);
            painter.end();
            formula.pixmap = pixmap;
            formula.baseline = 1 + render->getHeight() - render->getDepth();
            return formula;
        }
    } catch (const std::exception &) {
        // fall through: show the source
    }

    // Unparsable TeX: the source in red, like MicroTeX shows unknown commands.
    QFont font = QGuiApplication::font();
    font.setPixelSize(qMax(1, qRound(textSize)));
    const QFontMetricsF metrics(font);
    const QSizeF size(metrics.horizontalAdvance(tex) + 2, metrics.height() + 2);
    QPixmap pixmap((size * devicePixelRatio).toSize());
    pixmap.setDevicePixelRatio(devicePixelRatio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setFont(font);
    painter.setPen(QColor(0xd0, 0x30, 0x30));
    painter.drawText(QPointF(1, 1 + metrics.ascent()), tex);
    painter.end();
    formula.pixmap = pixmap;
    formula.baseline = 1 + metrics.ascent();
    formula.error = true;
    return formula;
}
