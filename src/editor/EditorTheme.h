#pragma once

#include <QColor>
#include <QFont>
#include <QPalette>
#include <QStringList>

// Colors and font metrics used by the live preview. Derived from the widget palette,
// so it follows the system light/dark mode.
struct EditorTheme {
    QFont baseFont;
    QStringList monoFamilies;
    QColor text;
    QColor faint;       // revealed Markdown markers, bullets
    QColor accent;      // links, checked checkboxes
    QColor codeText;
    QColor codeBackground;
    QColor highlightBackground;
    QColor quoteBar;
    QColor rule;

    qreal headingPointSize(int level) const;

    static EditorTheme fromPalette(const QPalette &palette, const QFont &baseFont);

    // Linear blend between two colors, t = 0 gives a, t = 1 gives b.
    static QColor mix(const QColor &a, const QColor &b, qreal t);
};
