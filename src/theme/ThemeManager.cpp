#include "theme/ThemeManager.h"

#include "app/AppSettings.h"

#include <QApplication>
#include <QDir>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>

#include <algorithm>

namespace {

Theme preset(const char *id, const char *name, bool dark, const char *window, const char *base, const char *text,
             const char *accent, const char *codeText, const char *codeBackground, const char *highlight)
{
    Theme t;
    t.id = QLatin1String(id);
    t.name = QLatin1String(name);
    t.dark = dark;
    t.window = QColor(window);
    t.base = QColor(base);
    t.text = QColor(text);
    t.accent = QColor(accent);
    t.codeText = QColor(codeText);
    t.codeBackground = QColor(codeBackground);
    t.highlight = QColor(highlight);
    return t;
}

QList<Theme> builtinThemes()
{
    Theme system;
    system.id = QStringLiteral("system");
    system.name = QStringLiteral("System");
    system.system = true;

    return {
        system,
        //     id                 name               dark   window     base       text       accent     code       codeBg     mark
        preset("light",           "Light",           false, "#f3f3f3", "#ffffff", "#222222", "#7052d8", "#b04030", "#f2f2f2", "#fff38a"),
        preset("dark",            "Dark",            true,  "#202020", "#2b2b2b", "#dcddde", "#a88bfa", "#e09f7d", "#353535", "#6b5b10"),
        preset("obsidian",        "Obsidian",        true,  "#161616", "#1e1e1e", "#dadada", "#8a5cf5", "#e7a37a", "#262626", "#5c4a00"),
        preset("solarized-light", "Solarized Light", false, "#eee8d5", "#fdf6e3", "#586e75", "#268bd2", "#d33682", "#eee8d5", "#f3e2a2"),
        preset("solarized-dark",  "Solarized Dark",  true,  "#00212b", "#002b36", "#93a1a1", "#268bd2", "#d33682", "#073642", "#4a4a10"),
        preset("nord",            "Nord",            true,  "#2e3440", "#3b4252", "#e5e9f0", "#88c0d0", "#ebcb8b", "#434c5e", "#5e5a3a"),
        preset("dracula",         "Dracula",         true,  "#21222c", "#282a36", "#f8f8f2", "#bd93f9", "#50fa7b", "#343746", "#6b5f2a"),
    };
}

} // namespace

ThemeManager &ThemeManager::instance()
{
    static ThemeManager manager;
    return manager;
}

Theme ThemeManager::builtinFallback(bool dark)
{
    const QList<Theme> themes = builtinThemes();
    return themes[dark ? 2 : 1];
}

void ThemeManager::initialize()
{
    m_builtin = builtinThemes();
    m_platformStyle = QApplication::style()->name();
    reloadUserThemes();
    apply(AppSettings::instance().theme());

    connect(&AppSettings::instance(), &AppSettings::changed, this, [this](const QString &key) {
        if (key == QLatin1String(AppSettings::themeKey))
            apply(AppSettings::instance().theme());
    });
}

QList<Theme> ThemeManager::themes() const
{
    QList<Theme> all = m_builtin;
    for (const QList<Theme> *extra : {&m_user, &m_plugin}) {
        for (const Theme &theme : *extra) {
            if (std::none_of(all.begin(), all.end(), [&](const Theme &t) { return t.id == theme.id; }))
                all << theme;
        }
    }
    return all;
}

const Theme *ThemeManager::find(const QString &id) const
{
    for (const QList<Theme> *list : {&m_builtin, &m_user, &m_plugin}) {
        for (const Theme &theme : *list) {
            if (theme.id == id)
                return &theme;
        }
    }
    return nullptr;
}

void ThemeManager::apply(const QString &id)
{
    const Theme *theme = find(id);
    m_current = theme ? *theme : m_builtin.front();

    if (m_current.system) {
        // Back to the platform look; a default-constructed palette resets to the platform palette.
        QApplication::setStyle(QStyleFactory::create(m_platformStyle));
        QApplication::setPalette(QPalette());
    } else {
        // Native Windows styles ignore custom palettes, Fusion follows them everywhere.
        if (QApplication::style()->name().compare(QLatin1String("fusion"), Qt::CaseInsensitive) != 0)
            QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QApplication::setPalette(m_current.palette());
    }
    emit themeApplied();
}

QString ThemeManager::userThemesDirectory() const
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("themes"));
}

void ThemeManager::reloadUserThemes()
{
    m_user.clear();
    const QDir directory(userThemesDirectory());
    for (const QFileInfo &file : directory.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        QFile json(file.absoluteFilePath());
        if (!json.open(QIODevice::ReadOnly))
            continue;
        if (const auto theme = Theme::fromJson(QJsonDocument::fromJson(json.readAll()).object()))
            m_user << *theme;
    }
    emit themesChanged();
}

void ThemeManager::clearPluginThemes()
{
    if (m_plugin.isEmpty())
        return;
    m_plugin.clear();
    emit themesChanged();
}

void ThemeManager::registerTheme(const Theme &theme)
{
    m_plugin.removeIf([&](const Theme &t) { return t.id == theme.id; });
    m_plugin << theme;
    emit themesChanged();
    if (AppSettings::instance().theme() == theme.id)
        apply(theme.id);
}
