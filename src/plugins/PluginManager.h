#pragma once

#include <QJSValue>
#include <QKeySequence>
#include <QObject>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

class MarkdownEditor;
class QJSEngine;

// Discovers, loads and runs JavaScript plugins from <AppData>/plugins/<id>/ (manifest.json + main.js).
// Every plugin runs in its own QJSEngine; calls into plugins are guarded by a timeout, and errors are
// logged per plugin instead of affecting the application. See docs/plugins.md for the API.
class PluginManager : public QObject {
    Q_OBJECT

public:
    struct Info {
        QString id;
        QString name;
        QString version;
        QString author;
        QString description;
        QString directory;
        QString mainScript = QStringLiteral("main.js"); // "main" in manifest.json
        bool enabled = false;
        bool loaded = false;
        QString error;
        QStringList log;
    };
    struct Command {
        QString plugin;
        QString id;
        QString name;
        QKeySequence shortcut;
    };

    static PluginManager &instance();

    // Hooks into the application, set by the main window.
    void setEditorProvider(std::function<MarkdownEditor *()> provider) { m_editor = std::move(provider); }
    void setMessageSink(std::function<void(const QString &)> sink) { m_message = std::move(sink); }
    MarkdownEditor *currentEditor() const { return m_editor ? m_editor() : nullptr; }
    void showMessage(const QString &message) const;

    QString pluginsDirectory() const;
    void setPluginsDirectory(const QString &directory) { m_directory = directory; } // tests
    void reload(); // unloads everything, rescans the folder, loads enabled plugins
    void shutdown() { unloadAll(); } // before QApplication goes away
    QList<Info> plugins() const;
    void setEnabled(const QString &id, bool enabled);
    // Creates a plugin skeleton (disabled) and returns its folder, or an empty string and *error.
    QString createPlugin(const QString &name, QString *error);

    QList<Command> commands() const;
    void runCommand(const QString &plugin, const QString &command);

    // Application events.
    void notifyOpen(const QString &path);
    QString notifyBeforeSave(const QString &path, const QString &text); // returns the text to save
    void notifySave(const QString &path);

    // Used by the plugin API objects.
    struct Plugin;
    QJSValue call(Plugin &plugin, QJSValue function, const QJSValueList &arguments, QString *error);
    void addCommand(Plugin &plugin, const Command &command, const QJSValue &function);
    void log(Plugin &plugin, const QString &line);

signals:
    void pluginsChanged();
    void commandsChanged();

private:
    PluginManager() = default;
    ~PluginManager() override;
    void unloadAll();
    void load(Plugin &plugin);
    QJSValue guarded(Plugin &plugin, const std::function<QJSValue()> &body, QString *error);
    void notify(const QString &event, const QJSValueList &arguments);

    std::vector<std::unique_ptr<Plugin>> m_plugins;
    QString m_directory; // empty: <AppData>/plugins
    std::function<MarkdownEditor *()> m_editor;
    std::function<void(const QString &)> m_message;
};
