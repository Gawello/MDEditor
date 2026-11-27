#include "editor/EditorTheme.h"

#include <QHash>

#include "theme/ThemeManager.h"

#include <QFontDatabase>
#include <QFontInfo>

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

QStringList EditorTheme::withEmoji(const QFont &font)
{
    static const QString emoji = [] {
        const QStringList installed = QFontDatabase::families();
        for (const char *candidate : {"Noto Color Emoji", "Apple Color Emoji", "Segoe UI Emoji", "Twemoji Mozilla"}) {
            if (installed.contains(QLatin1String(candidate)))
                return QString::fromLatin1(candidate);
        }
        return QString();
    }();
    // The resolved name: an alias such as "Monospace" stops working as soon as the list has a second entry.
    QStringList families{QFontInfo(font).family()};
    if (!emoji.isEmpty() && !families.contains(emoji))
        families << emoji;
    return families;
}

EditorTheme EditorTheme::fromPalette(const QPalette &palette, const QFont &baseFont)
{
    EditorTheme t;
    t.baseFont = baseFont;
    t.monoFamilies = withEmoji(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    const QColor background = palette.color(QPalette::Base);
    const bool dark = background.lightnessF() < 0.5;

    t.text = palette.color(QPalette::Text);
    t.faint = mix(background, t.text, 0.45);
    t.accent = dark ? QColor(0xa8, 0x8b, 0xfa) : QColor(0x70, 0x52, 0xd8);
    t.codeText = dark ? QColor(0xe0, 0x9f, 0x7d) : QColor(0xb0, 0x40, 0x30);
    t.codeBackground = mix(background, t.text, dark ? 0.08 : 0.05);
    t.highlightBackground = dark ? QColor(0x7a, 0x6a, 0x10) : QColor(0xff, 0xf3, 0x8a);
    // A non-system theme defines its own Markdown colors.
    const Theme &theme = ThemeManager::instance().current();
    if (!theme.system) {
        t.accent = theme.accent;
        t.codeText = theme.codeText;
        t.codeBackground = theme.codeBackground;
        t.highlightBackground = theme.highlight;
    }

    t.quoteBar = t.accent;
    t.rule = mix(background, t.text, 0.25);

    // Code colors: One Dark / GitHub-like, with the theme's accent for keywords and its faint text for comments,
    // so custom themes stay coherent.
    using syntax::Token;
    const auto set = [&t](Token token, QColor color) { t.syntax[size_t(token)] = color; };
    set(Token::Keyword, t.accent);
    set(Token::Comment, mix(background, t.text, 0.5));
    if (dark) {
        set(Token::Type, QColor(0xe5, 0xc0, 0x7b));
        set(Token::Constant, QColor(0xd1, 0x9a, 0x66));
        set(Token::String, QColor(0x98, 0xc3, 0x79));
        set(Token::Number, QColor(0xd1, 0x9a, 0x66));
        set(Token::Function, QColor(0x61, 0xaf, 0xef));
        set(Token::Property, QColor(0xe0, 0x6c, 0x75));
        set(Token::Tag, QColor(0xe0, 0x6c, 0x75));
        set(Token::Attribute, QColor(0xd1, 0x9a, 0x66));
        set(Token::Variable, QColor(0xe0, 0x6c, 0x75));
        set(Token::Meta, QColor(0x56, 0xb6, 0xc2));
        set(Token::Inserted, QColor(0x98, 0xc3, 0x79));
        set(Token::Deleted, QColor(0xe0, 0x6c, 0x75));
    } else {
        set(Token::Type, QColor(0x95, 0x38, 0x00));
        set(Token::Constant, QColor(0x05, 0x50, 0xae));
        set(Token::String, QColor(0x0a, 0x30, 0x69));
        set(Token::Number, QColor(0x05, 0x50, 0xae));
        set(Token::Function, QColor(0x82, 0x50, 0xdf));
        set(Token::Property, QColor(0x05, 0x50, 0xae));
        set(Token::Tag, QColor(0x11, 0x63, 0x29));
        set(Token::Attribute, QColor(0x95, 0x38, 0x00));
        set(Token::Variable, QColor(0x95, 0x38, 0x00));
        set(Token::Meta, QColor(0x66, 0x39, 0xba));
        set(Token::Inserted, QColor(0x11, 0x63, 0x29));
        set(Token::Deleted, QColor(0x82, 0x07, 0x1e));
    }
    return t;
}

QColor EditorTheme::calloutColor(const QString &type)
{
    // Obsidian's default callout colors; readable on light and dark backgrounds alike.
    static const QHash<QString, QColor> colors{
        {QStringLiteral("note"), QColor(0x44, 0x8a, 0xff)},     {QStringLiteral("info"), QColor(0x44, 0x8a, 0xff)},
        {QStringLiteral("todo"), QColor(0x44, 0x8a, 0xff)},     {QStringLiteral("abstract"), QColor(0x00, 0xb0, 0xff)},
        {QStringLiteral("summary"), QColor(0x00, 0xb0, 0xff)},  {QStringLiteral("tldr"), QColor(0x00, 0xb0, 0xff)},
        {QStringLiteral("tip"), QColor(0x00, 0xbf, 0xa5)},      {QStringLiteral("hint"), QColor(0x00, 0xbf, 0xa5)},
        {QStringLiteral("important"), QColor(0x00, 0xbf, 0xa5)}, {QStringLiteral("success"), QColor(0x00, 0xc8, 0x53)},
        {QStringLiteral("check"), QColor(0x00, 0xc8, 0x53)},    {QStringLiteral("done"), QColor(0x00, 0xc8, 0x53)},
        {QStringLiteral("question"), QColor(0xff, 0x91, 0x00)}, {QStringLiteral("help"), QColor(0xff, 0x91, 0x00)},
        {QStringLiteral("faq"), QColor(0xff, 0x91, 0x00)},      {QStringLiteral("warning"), QColor(0xff, 0x91, 0x00)},
        {QStringLiteral("caution"), QColor(0xff, 0x91, 0x00)},  {QStringLiteral("attention"), QColor(0xff, 0x91, 0x00)},
        {QStringLiteral("failure"), QColor(0xff, 0x52, 0x52)},  {QStringLiteral("fail"), QColor(0xff, 0x52, 0x52)},
        {QStringLiteral("missing"), QColor(0xff, 0x52, 0x52)},  {QStringLiteral("danger"), QColor(0xff, 0x17, 0x44)},
        {QStringLiteral("error"), QColor(0xff, 0x17, 0x44)},    {QStringLiteral("bug"), QColor(0xf5, 0x00, 0x57)},
        {QStringLiteral("example"), QColor(0x7c, 0x4d, 0xff)},  {QStringLiteral("quote"), QColor(0x9e, 0x9e, 0x9e)},
        {QStringLiteral("cite"), QColor(0x9e, 0x9e, 0x9e)},
    };
    return colors.value(type.toLower(), QColor(0x44, 0x8a, 0xff));
}
