#include "editor/EditorTheme.h"

#include <QFontDatabase>

#include <algorithm>

QColor EditorTheme::mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(float(a.redF() * (1 - t) + b.redF() * t),
                            float(a.greenF() * (1 - t) + b.greenF() * t),
                            float(a.blueF() * (1 - t) + b.blueF() * t));
}

qreal EditorTheme::headingPointSize(int level) const
{
    static constexpr qreal scale[] = {1.9, 1.55, 1.3, 1.15, 1.05, 1.0};
    const qreal base = baseFont.pointSizeF() > 0 ? baseFont.pointSizeF() : 11.0;
    return base * scale[std::clamp(level, 1, 6) - 1];
}

EditorTheme EditorTheme::fromPalette(const QPalette &palette, const QFont &baseFont)
{
    EditorTheme t;
    t.baseFont = baseFont;
    t.monoFamilies = QFontDatabase::systemFont(QFontDatabase::FixedFont).families();

    const QColor background = palette.color(QPalette::Base);
    const bool dark = background.lightnessF() < 0.5;

    t.text = palette.color(QPalette::Text);
    t.faint = mix(background, t.text, 0.45);
    t.accent = dark ? QColor(0xa8, 0x8b, 0xfa) : QColor(0x70, 0x52, 0xd8);
    t.codeText = dark ? QColor(0xe0, 0x9f, 0x7d) : QColor(0xb0, 0x40, 0x30);
    t.codeBackground = mix(background, t.text, dark ? 0.08 : 0.05);
    t.highlightBackground = dark ? QColor(0x7a, 0x6a, 0x10) : QColor(0xff, 0xf3, 0x8a);
    t.quoteBar = t.accent;
    t.rule = mix(background, t.text, 0.25);
    return t;
}
