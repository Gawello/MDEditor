#pragma once

#include <QColor>
#include <QJsonObject>
#include <QPalette>
#include <QString>

#include <optional>

// A color theme for the whole application (via QPalette) and the editor's Markdown colors.
struct Theme {
    QString id;
    QString name;
    bool dark = false;
    bool system = false; // follow the platform style and palette; colors below are unused

    QColor window;         // panels, tab bar, menus
    QColor base;           // editor and list backgrounds
    QColor text;
    QColor accent;         // links, selection, checkboxes, quote bars
    QColor codeText;
    QColor codeBackground;
    QColor highlight;      // ==marked== text background

    QPalette palette() const;

    QJsonObject toJson() const;
    // Missing colors fall back to the built-in light or dark theme (depending on "dark").
    static std::optional<Theme> fromJson(const QJsonObject &json);
};
