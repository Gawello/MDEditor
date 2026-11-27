#pragma once

#include <QColor>
#include <QHash>
#include <QPixmap>
#include <QString>

// TeX formulas ($...$, $$...$$) drawn by MicroTeX, cached by source, size and color. GUI thread only:
// MicroTeX keeps global state and loads its fonts into the application's font database.
class MathRenderer {
public:
    struct Formula {
        QPixmap pixmap; // device pixel ratio set; empty if nothing could be drawn
        qreal baseline = 0; // from the top, in device-independent pixels
        bool error = false; // the TeX didn't parse: the pixmap shows the source in red
        QSizeF size() const { return pixmap.isNull() ? QSizeF() : pixmap.deviceIndependentSize(); }
    };

    static MathRenderer &instance();

    // `textSize` is the formula's font size in pixels. Display formulas use TeX's display style.
    const Formula &render(const QString &tex, qreal textSize, const QColor &color, bool display, qreal devicePixelRatio);

    // Frees MicroTeX before QApplication goes (its fonts belong to the application).
    static void shutdown();

private:
    MathRenderer() = default;
    Formula draw(const QString &tex, qreal textSize, const QColor &color, bool display, qreal devicePixelRatio);

    QHash<QString, Formula> m_cache;
    bool m_initialized = false;
};
