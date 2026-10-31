#pragma once

// Private to the plugin system: the objects exposed to plugin JavaScript as `app` and `editor`.

#include "plugins/PluginManager.h"

#include <QHash>
#include <QJSEngine>
#include <QList>
#include <QPair>

struct PluginManager::Plugin {
    Info info;
    std::unique_ptr<QJSEngine> engine; // declared first: destroyed after the QJSValues below
    QList<Command> commands;
    QHash<QString, QJSValue> commandFunctions;
    QList<QPair<QString, QJSValue>> events; // (event name, handler), in registration order
};

// `app`: registration functions and application services.
class PluginHost : public QObject {
    Q_OBJECT

public:
    PluginHost(PluginManager::Plugin &plugin, QObject *parent);

    Q_INVOKABLE void registerCommand(const QJSValue &spec, const QJSValue &function);
    Q_INVOKABLE void registerFormatter(const QJSValue &languages, const QJSValue &function);
    Q_INVOKABLE void on(const QString &event, const QJSValue &function);
    Q_INVOKABLE void registerTheme(const QJSValue &spec);
    Q_INVOKABLE void showMessage(const QString &message);
    Q_INVOKABLE QString version() const;
    Q_INVOKABLE void _log(const QString &level, const QString &text); // backs console.log/warn/error

private:
    PluginManager::Plugin &m_plugin;
};

// `editor`: the document in the current tab. Every change is one undo step.
class EditorApi : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    Q_INVOKABLE QString text() const;
    Q_INVOKABLE void setText(const QString &text);
    Q_INVOKABLE QString selectedText() const;
    Q_INVOKABLE void replaceSelection(const QString &text);
    Q_INVOKABLE void insert(const QString &text);
    Q_INVOKABLE int cursorPosition() const;
    Q_INVOKABLE void setCursorPosition(int position);
    Q_INVOKABLE QString filePath() const;
};
