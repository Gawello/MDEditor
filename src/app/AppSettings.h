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
    static constexpr auto autoPairBracketsKey = "editor/autoPairBrackets";
    static constexpr auto autoPairMarkdownKey = "editor/autoPairMarkdown";
    static constexpr auto convertPastedHtmlKey = "editor/convertPastedHtml";
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
    // Daily notes and templates; folders are relative to the open folder.
    static constexpr auto dailyFolderKey = "notes/dailyFolder";
    static constexpr auto dailyFormatKey = "notes/dailyFormat";     // moment.js tokens, like Obsidian
    static constexpr auto dailyTemplateKey = "notes/dailyTemplate"; // a note used as the template
    static constexpr auto templatesFolderKey = "notes/templatesFolder";
    static constexpr auto updateLinksKey = "notes/updateLinks"; // after a rename or move: "ask", "always", "never"
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
    bool autoPairBrackets() const { return value(autoPairBracketsKey, true).toBool(); }
    bool autoPairMarkdown() const { return value(autoPairMarkdownKey, true).toBool(); }
    bool convertPastedHtml() const { return value(convertPastedHtmlKey, true).toBool(); }
    QString dailyFolder() const { return value(dailyFolderKey).toString(); }
    QString dailyFormat() const { return value(dailyFormatKey, QStringLiteral("YYYY-MM-DD")).toString(); }
    QString dailyTemplate() const { return value(dailyTemplateKey).toString(); }
    QString templatesFolder() const { return value(templatesFolderKey, QStringLiteral("templates")).toString(); }
    QString updateLinks() const { return value(updateLinksKey, QStringLiteral("ask")).toString(); }
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
