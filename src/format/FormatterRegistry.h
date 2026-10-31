#pragma once

#include <QObject>
#include <QStringList>

#include <functional>
#include <optional>

// Chooses how to format code in a given language: a plugin formatter, an external tool
// (clang-format, ruff/black, prettier, gofmt, or a user-configured command), or a built-in one.
class FormatterRegistry : public QObject {
    Q_OBJECT

public:
    struct Outcome {
        bool ok = false;
        QString text;
        QString error;
        QString formatter; // who did it, for status messages
    };
    // Returns the formatted text, or nullopt and sets *error.
    using PluginFormatter = std::function<std::optional<QString>(const QString &text, int indent, QString *error)>;

    static FormatterRegistry &instance();

    // Canonical language id for a fence info string or alias ("py" -> "python"), or empty if unknown.
    QString canonical(const QString &language) const;
    QString languageForSuffix(const QString &suffix) const;
    // Languages with a built-in formatter, for the settings page.
    QStringList builtinLanguages() const;

    Outcome format(const QString &language, const QString &text) const;

    // The external command that would be used (configured or auto-detected), empty if none.
    QString externalCommand(const QString &language) const;
    // The auto-detected default command, empty if the tool isn't installed.
    QString detectedCommand(const QString &language) const;

    void registerPluginFormatter(const QString &owner, const QStringList &languages, PluginFormatter formatter);
    void removePluginFormatters(const QString &owner);

private:
    FormatterRegistry() = default;
    Outcome runExternal(const QString &command, const QString &text) const;

    struct Registered {
        QString owner;
        QString language;
        PluginFormatter formatter;
    };
    QList<Registered> m_plugins;
};
