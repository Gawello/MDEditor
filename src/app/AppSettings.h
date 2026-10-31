#pragma once

#include <QObject>
#include <QStringList>
#include <QVariant>

// Application preferences, persisted in QSettings. Every change emits changed(key), so open
// windows and editors can apply it live.
class AppSettings : public QObject {
    Q_OBJECT

public:
    static AppSettings &instance();

    static constexpr auto themeKey = "appearance/theme";
    static constexpr auto fontFamilyKey = "editor/fontFamily";
    static constexpr auto fontSizeKey = "editor/fontSize";
    static constexpr auto readableWidthKey = "editor/readableWidth";
    static constexpr auto indentSizeKey = "editor/indentSize";
    static constexpr auto attachmentsFolderKey = "editor/attachmentsFolder";
    static constexpr auto formatOnSaveKey = "format/onSave";
    static constexpr auto enabledPluginsKey = "plugins/enabled";

    QString theme() const { return value(themeKey, QStringLiteral("system")).toString(); }
    QString fontFamily() const { return value(fontFamilyKey).toString(); } // empty: system default
    qreal fontSize() const { return value(fontSizeKey, 12.0).toReal(); }
    int readableWidth() const { return value(readableWidthKey, 100).toInt(); } // characters, 0 = full width
    int indentSize() const { return value(indentSizeKey, 4).toInt(); }
    QString attachmentsFolder() const { return value(attachmentsFolderKey, QStringLiteral("assets")).toString(); }
    bool formatOnSave() const { return value(formatOnSaveKey, false).toBool(); }
    QStringList enabledPlugins() const { return value(enabledPluginsKey).toStringList(); }

    // External formatter command line for a language (empty: auto-detect / built-in).
    QString formatterCommand(const QString &language) const;
    void setFormatterCommand(const QString &language, const QString &command);

    QVariant value(const QString &key, const QVariant &defaultValue = {}) const;
    void setValue(const QString &key, const QVariant &value);

signals:
    void changed(const QString &key);

private:
    AppSettings() = default;
};
