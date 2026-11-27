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
    static constexpr auto fallbackEncodingKey = "editor/fallbackEncoding";
    // Comma-separated dictionary codes ("pl_PL,en_US"); empty: no spell checking; absent: the default.
    static constexpr auto spellLanguagesKey = "spell/languages";
    static constexpr auto bookmarksKey = "bookmarks"; // absolute paths, in the user's order
    static constexpr auto formatOnSaveKey = "format/onSave";
    static constexpr auto enabledPluginsKey = "plugins/enabled";
    static constexpr auto driveFolderKey = "drive/folder";
    static constexpr auto driveMountPointKey = "drive/mountPoint";
    static constexpr auto driveConnectOnStartKey = "drive/connectOnStart";
    static constexpr auto driveRclonePathKey = "drive/rclonePath"; // empty: rclone from PATH
    static constexpr auto driveMountPidKey = "drive/mountPid";
    static constexpr auto checkUpdatesOnStartKey = "updates/checkOnStart";
    static constexpr auto lastUpdateCheckKey = "updates/lastCheck"; // UTC
    static constexpr auto skippedUpdateKey = "updates/skippedVersion";

    QString theme() const { return value(themeKey, QStringLiteral("system")).toString(); }
    QString fontFamily() const { return value(fontFamilyKey).toString(); } // empty: system default
    qreal fontSize() const { return value(fontSizeKey, 12.0).toReal(); }
    int readableWidth() const { return value(readableWidthKey, 100).toInt(); } // characters, 0 = full width
    int indentSize() const { return value(indentSizeKey, 4).toInt(); }
    QString attachmentsFolder() const { return value(attachmentsFolderKey, QStringLiteral("assets")).toString(); }
    // For files that aren't valid UTF-8 and have no byte order mark.
    QString fallbackEncoding() const;
    bool formatOnSave() const { return value(formatOnSaveKey, false).toBool(); }
    bool checkUpdatesOnStart() const { return value(checkUpdatesOnStartKey, false).toBool(); } // off: asks GitHub
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
