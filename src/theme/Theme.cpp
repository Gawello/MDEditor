#include "theme/Theme.h"

#include "editor/EditorTheme.h"
#include "theme/ThemeManager.h"

QPalette Theme::palette() const
{
    const auto mix = EditorTheme::mix;
    const QColor button = mix(window, text, 0.08);
    const QColor placeholder = mix(base, text, 0.5);
    const QColor disabledText = mix(window, text, 0.4);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, mix(base, text, 0.04));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::PlaceholderText, placeholder);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, dark ? QColor(Qt::white) : QColor(Qt::black));
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, accent.lightnessF() > 0.6 ? QColor(Qt::black) : QColor(Qt::white));
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::LinkVisited, accent.darker(120));
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    // Bevel colors used by the Fusion style for borders and frames.
    p.setColor(QPalette::Light, mix(button, Qt::white, dark ? 0.08 : 0.6));
    p.setColor(QPalette::Midlight, mix(button, Qt::white, dark ? 0.04 : 0.3));
    p.setColor(QPalette::Mid, mix(window, text, 0.25));
    p.setColor(QPalette::Dark, mix(window, text, dark ? 0.12 : 0.45));
    p.setColor(QPalette::Shadow, dark ? QColor(0, 0, 0) : mix(window, Qt::black, 0.5));

    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabledText);
    p.setColor(QPalette::Disabled, QPalette::Highlight, mix(window, text, 0.3));
    return p;
}

QJsonObject Theme::toJson() const
{
    QJsonObject colors{
        {"window", window.name()},     {"base", base.name()},
        {"text", text.name()},         {"accent", accent.name()},
        {"codeText", codeText.name()}, {"codeBackground", codeBackground.name()},
        {"highlight", highlight.name()},
    };
    return QJsonObject{{"id", id}, {"name", name}, {"dark", dark}, {"colors", colors}};
}

std::optional<Theme> Theme::fromJson(const QJsonObject &json)
{
    const QString id = json.value("id").toString();
    if (id.isEmpty())
        return std::nullopt;

    const bool dark = json.value("dark").toBool();
    Theme theme = ThemeManager::builtinFallback(dark);
    theme.id = id;
    theme.name = json.value("name").toString(id);
    theme.dark = dark;
    theme.system = false;

    const QJsonObject colors = json.value("colors").toObject();
    auto read = [&](const char *key, QColor &target) {
        const QColor color(colors.value(QLatin1String(key)).toString());
        if (color.isValid())
            target = color;
    };
    read("window", theme.window);
    read("base", theme.base);
    read("text", theme.text);
    read("accent", theme.accent);
    read("codeText", theme.codeText);
    read("codeBackground", theme.codeBackground);
    read("highlight", theme.highlight);
    return theme;
}
