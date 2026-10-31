#include "plugins/PluginApi.h"

#include "editor/MarkdownEditor.h"
#include "format/FormatterRegistry.h"
#include "theme/ThemeManager.h"

#include <QApplication>
#include <QJsonObject>
#include <QTextCursor>

#include <algorithm>

PluginHost::PluginHost(PluginManager::Plugin &plugin, QObject *parent)
    : QObject(parent)
    , m_plugin(plugin)
{
}

void PluginHost::registerCommand(const QJSValue &spec, const QJSValue &function)
{
    PluginManager::Command command;
    command.plugin = m_plugin.info.id;
    if (spec.isString()) {
        command.name = spec.toString();
        command.id = command.name;
    } else {
        command.name = spec.property("name").toString();
        command.id = spec.property("id").isUndefined() ? command.name : spec.property("id").toString();
        if (spec.property("shortcut").isString())
            command.shortcut = QKeySequence(spec.property("shortcut").toString());
    }
    if (command.name.isEmpty() || !function.isCallable()) {
        _log("error", "registerCommand needs a name and a function");
        return;
    }
    PluginManager::instance().addCommand(m_plugin, command, function);
}

void PluginHost::registerFormatter(const QJSValue &languages, const QJSValue &function)
{
    QStringList list;
    if (languages.isArray()) {
        const int length = languages.property("length").toInt();
        for (int i = 0; i < length; ++i)
            list << languages.property(i).toString();
    } else {
        list << languages.toString();
    }
    if (list.isEmpty() || !function.isCallable()) {
        _log("error", "registerFormatter needs languages and a function");
        return;
    }

    PluginManager::Plugin &plugin = m_plugin;
    FormatterRegistry::instance().registerPluginFormatter(
        plugin.info.id, list,
        [&plugin, function](const QString &text, int indent, QString *error) -> std::optional<QString> {
            QJSValue options = plugin.engine->newObject();
            options.setProperty("indent", indent);
            const QJSValue result = PluginManager::instance().call(plugin, function, {text, options}, error);
            if (!error->isEmpty())
                return std::nullopt;
            if (!result.isString()) {
                *error = QStringLiteral("the formatter did not return a string");
                return std::nullopt;
            }
            return result.toString();
        });
}

void PluginHost::on(const QString &event, const QJSValue &function)
{
    static const QStringList events{"open", "beforeSave", "save"};
    if (!events.contains(event) || !function.isCallable()) {
        _log("error", QStringLiteral("unknown event \"%1\" (use %2)").arg(event, events.join(", ")));
        return;
    }
    m_plugin.events.append({event, function});
}

void PluginHost::registerTheme(const QJSValue &spec)
{
    const QJsonObject json = QJsonObject::fromVariantMap(spec.toVariant().toMap());
    if (const auto theme = Theme::fromJson(json))
        ThemeManager::instance().registerTheme(*theme);
    else
        _log("error", "registerTheme needs at least an id");
}

void PluginHost::showMessage(const QString &message)
{
    PluginManager::instance().showMessage(QStringLiteral("%1: %2").arg(m_plugin.info.name, message));
}

QString PluginHost::version() const
{
    return QApplication::applicationVersion();
}

void PluginHost::_log(const QString &level, const QString &text)
{
    PluginManager::instance().log(m_plugin, level == QLatin1String("log") ? text : level + ": " + text);
}

namespace {

MarkdownEditor *editor()
{
    return PluginManager::instance().currentEditor();
}

} // namespace

QString EditorApi::text() const
{
    return editor() ? editor()->markdown() : QString();
}

void EditorApi::setText(const QString &text)
{
    if (editor())
        editor()->replaceAll(text);
}

QString EditorApi::selectedText() const
{
    if (!editor())
        return {};
    return editor()->textCursor().selectedText().replace(QChar::ParagraphSeparator, u'\n');
}

void EditorApi::replaceSelection(const QString &text)
{
    if (MarkdownEditor *e = editor()) {
        QTextCursor cursor = e->textCursor();
        cursor.insertText(text);
        e->setTextCursor(cursor);
    }
}

void EditorApi::insert(const QString &text)
{
    if (MarkdownEditor *e = editor()) {
        QTextCursor cursor = e->textCursor();
        cursor.clearSelection();
        cursor.insertText(text);
        e->setTextCursor(cursor);
    }
}

int EditorApi::cursorPosition() const
{
    return editor() ? editor()->textCursor().position() : 0;
}

void EditorApi::setCursorPosition(int position)
{
    if (MarkdownEditor *e = editor()) {
        QTextCursor cursor = e->textCursor();
        cursor.setPosition(std::clamp(position, 0, e->document()->characterCount() - 1));
        e->setTextCursor(cursor);
    }
}

QString EditorApi::filePath() const
{
    return editor() ? editor()->documentPath() : QString();
}
