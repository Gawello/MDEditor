#pragma once

#include <QColor>
#include <QFont>
#include <QPalette>
#include <QStringList>

#include "syntax/Syntax.h"

#include <array>

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
    std::array<QColor, syntax::tokenCount> syntax; // code colors by syntax::Token; invalid: plain text

    QColor syntaxColor(syntax::Token token) const { return syntax[size_t(token)]; }

    qreal headingPointSize(int level) const;

    static EditorTheme fromPalette(const QPalette &palette, const QFont &baseFont);

    // The family `font` resolves to, plus an installed color emoji font. Left to font fallback, emoji come
    // out as empty boxes or in a gray symbol font on Linux.
    static QStringList withEmoji(const QFont &font);

    // An Obsidian callout's color by its type ("note", "warning", ...); unknown types are notes.
    static QColor calloutColor(const QString &type);

    // Linear blend between two colors, t = 0 gives a, t = 1 gives b.
    static QColor mix(const QColor &a, const QColor &b, qreal t);
};
