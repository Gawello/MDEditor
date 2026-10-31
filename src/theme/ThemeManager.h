#pragma once

#include "theme/Theme.h"

#include <QList>
#include <QObject>
#include <QPalette>

// Owns the list of themes (built-in presets, JSON files in the user's themes folder, themes registered
// by plugins) and applies the selected one to the whole application.
class ThemeManager : public QObject {
    Q_OBJECT

public:
    static ThemeManager &instance();

    // Call once QApplication exists: remembers the platform style/palette and applies the saved theme.
    void initialize();

    QList<Theme> themes() const;
    const Theme &current() const { return m_current; }
    void apply(const QString &id);

    QString userThemesDirectory() const;
    void reloadUserThemes();
    void registerTheme(const Theme &theme); // from plugins; replaces a theme with the same id
    void clearPluginThemes();

    static Theme builtinFallback(bool dark);

signals:
    void themesChanged();
    void themeApplied();

private:
    ThemeManager() { m_current.system = true; }
    const Theme *find(const QString &id) const;

    QList<Theme> m_builtin;
    QList<Theme> m_user;
    QList<Theme> m_plugin;
    Theme m_current;
    QString m_platformStyle;
};
