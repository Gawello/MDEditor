#include "plugins/PluginManager.h"

#include "app/AppSettings.h"
#include "format/FormatterRegistry.h"
#include "plugins/PluginApi.h"
#include "theme/ThemeManager.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace {

constexpr auto callTimeout = std::chrono::seconds(3);
constexpr int maxLogLines = 200;

// console.* forwards to app._log so output shows up in Settings → Plugins.
const char *consoleShim = R"(
var console = (function () {
    function format(args) { return Array.prototype.map.call(args, String).join(" "); }
    return {
        log: function () { app._log("log", format(arguments)); },
        info: function () { app._log("log", format(arguments)); },
        warn: function () { app._log("warning", format(arguments)); },
        error: function () { app._log("error", format(arguments)); }
    };
})();
)";

const char *pluginTemplate = R"(// %1: an MDEditor plugin.
// API reference: https://github.com/Gawello/MDEditor/blob/main/docs/plugins.md
//
// `app` registers things (commands, formatters, event handlers, themes); `editor` reads and changes
// the document in the current tab. console.log output appears in Settings → Plugins.

app.registerCommand({ id: "hello", name: "%1: Say hello", shortcut: "Ctrl+Alt+H" }, function () {
    editor.insert("Hello from %1!");
});

app.on("save", function (path) {
    console.log("saved", path);
});
)";

QString describe(const QJSValue &error)
{
    const QJSValue line = error.property("lineNumber");
    return line.isUndefined() ? error.toString() : QStringLiteral("%1 (line %2)").arg(error.toString()).arg(line.toInt());
}

} // namespace

PluginManager &PluginManager::instance()
{
    static PluginManager manager;
    return manager;
}

PluginManager::~PluginManager()
{
    m_plugins.clear();
}

void PluginManager::showMessage(const QString &message) const
{
    if (m_message)
        m_message(message);
}

QString PluginManager::pluginsDirectory() const
{
    if (!m_directory.isEmpty())
        return m_directory;
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("plugins"));
}

QList<PluginManager::Info> PluginManager::plugins() const
{
    QList<Info> list;
    for (const auto &plugin : m_plugins)
        list << plugin->info;
    return list;
}

QList<PluginManager::Command> PluginManager::commands() const
{
    QList<Command> list;
    for (const auto &plugin : m_plugins)
        list << plugin->commands;
    return list;
}

void PluginManager::unloadAll()
{
    for (const auto &plugin : m_plugins)
        FormatterRegistry::instance().removePluginFormatters(plugin->info.id);
    m_plugins.clear();
    ThemeManager::instance().clearPluginThemes();
}

void PluginManager::reload()
{
    unloadAll();
    const QStringList enabled = AppSettings::instance().enabledPlugins();
    const QDir root(pluginsDirectory());
    for (const QFileInfo &folder : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        auto plugin = std::make_unique<Plugin>();
        Info &info = plugin->info;
        info.directory = folder.absoluteFilePath();
        info.id = folder.fileName();

        QFile manifest(QDir(info.directory).filePath(QStringLiteral("manifest.json")));
        if (!manifest.open(QIODevice::ReadOnly))
            continue; // not a plugin folder
        QJsonParseError parseError;
        const QJsonObject json = QJsonDocument::fromJson(manifest.readAll(), &parseError).object();
        if (parseError.error != QJsonParseError::NoError) {
            info.error = QStringLiteral("manifest.json: %1").arg(parseError.errorString());
        } else {
            info.id = json.value("id").toString(info.id);
            info.name = json.value("name").toString(info.id);
            info.version = json.value("version").toString();
            info.author = json.value("author").toString();
            info.description = json.value("description").toString();
            info.mainScript = json.value("main").toString(info.mainScript);
        }
        if (info.name.isEmpty())
            info.name = info.id;
        const bool duplicate = std::any_of(m_plugins.begin(), m_plugins.end(),
                                           [&](const auto &p) { return p->info.id == info.id; });
        if (duplicate)
            info.error = QStringLiteral("another plugin already uses the id \"%1\"").arg(info.id);
        info.enabled = enabled.contains(info.id);

        Plugin &ref = *plugin;
        m_plugins.push_back(std::move(plugin));
        if (ref.info.enabled && ref.info.error.isEmpty())
            load(ref);
    }
    emit pluginsChanged();
    emit commandsChanged();
}

void PluginManager::load(Plugin &plugin)
{
    QFile file(QDir(plugin.info.directory).filePath(plugin.info.mainScript));
    if (!file.open(QIODevice::ReadOnly)) {
        plugin.info.error = QStringLiteral("cannot read %1").arg(plugin.info.mainScript);
        return;
    }
    const QString code = QString::fromUtf8(file.readAll());

    plugin.engine = std::make_unique<QJSEngine>();
    QJSEngine *engine = plugin.engine.get();
    auto *host = new PluginHost(plugin, engine);
    auto *editor = new EditorApi(engine);
    QJSEngine::setObjectOwnership(host, QJSEngine::CppOwnership);
    QJSEngine::setObjectOwnership(editor, QJSEngine::CppOwnership);
    engine->globalObject().setProperty("app", engine->newQObject(host));
    engine->globalObject().setProperty("editor", engine->newQObject(editor));
    engine->evaluate(QString::fromLatin1(consoleShim));

    QString error;
    const QString fileName = file.fileName();
    guarded(plugin, [&] { return engine->evaluate(code, fileName); }, &error);
    if (!error.isEmpty()) {
        plugin.info.error = error;
        FormatterRegistry::instance().removePluginFormatters(plugin.info.id);
        plugin.commands.clear();
        plugin.commandFunctions.clear();
        plugin.events.clear();
        return;
    }
    plugin.info.loaded = true;
}

QJSValue PluginManager::call(Plugin &plugin, QJSValue function, const QJSValueList &arguments, QString *error)
{
    if (!function.isCallable() || !plugin.engine) {
        if (error)
            *error = QStringLiteral("not a function");
        return {};
    }
    return guarded(plugin, [&] { return function.call(arguments); }, error);
}

// Runs plugin code with a watchdog: a script running longer than callTimeout is interrupted.
QJSValue PluginManager::guarded(Plugin &plugin, const std::function<QJSValue()> &body, QString *error)
{
    if (error)
        error->clear();
    QJSEngine *engine = plugin.engine.get();
    std::mutex mutex;
    std::condition_variable finished;
    bool done = false;
    bool timedOut = false;
    std::thread watchdog([&] {
        std::unique_lock lock(mutex);
        if (!finished.wait_for(lock, callTimeout, [&] { return done; })) {
            timedOut = true;
            engine->setInterrupted(true);
        }
    });
    const QJSValue result = body();
    {
        std::lock_guard lock(mutex);
        done = true;
    }
    finished.notify_one();
    watchdog.join();
    engine->setInterrupted(false);

    if (timedOut || result.isError()) {
        const QString message = timedOut ? QStringLiteral("stopped: took longer than 3 seconds") : describe(result);
        log(plugin, QStringLiteral("error: ") + message);
        if (error)
            *error = message;
        return {};
    }
    return result;
}

void PluginManager::addCommand(Plugin &plugin, const Command &command, const QJSValue &function)
{
    plugin.commands.removeIf([&](const Command &c) { return c.id == command.id; });
    plugin.commands << command;
    plugin.commandFunctions.insert(command.id, function);
    emit commandsChanged();
}

void PluginManager::log(Plugin &plugin, const QString &line)
{
    plugin.info.log << line;
    while (plugin.info.log.size() > maxLogLines)
        plugin.info.log.removeFirst();
    emit pluginsChanged();
}

void PluginManager::runCommand(const QString &pluginId, const QString &commandId)
{
    for (const auto &plugin : m_plugins) {
        if (plugin->info.id != pluginId || !plugin->commandFunctions.contains(commandId))
            continue;
        QString error;
        call(*plugin, plugin->commandFunctions.value(commandId), {}, &error);
        if (!error.isEmpty())
            showMessage(QStringLiteral("%1: %2").arg(plugin->info.name, error));
        return;
    }
}

void PluginManager::setEnabled(const QString &id, bool enabled)
{
    QStringList list = AppSettings::instance().enabledPlugins();
    list.removeAll(id);
    if (enabled)
        list << id;
    AppSettings::instance().setValue(AppSettings::enabledPluginsKey, list);
    reload();
}

QString PluginManager::createPlugin(const QString &name, QString *error)
{
    QString id = name.trimmed().toLower();
    id.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    while (id.startsWith(u'-'))
        id.remove(0, 1);
    while (id.endsWith(u'-'))
        id.chop(1);
    if (id.isEmpty()) {
        *error = tr("Use letters or digits in the plugin name.");
        return {};
    }

    const QDir root(pluginsDirectory());
    if (root.exists(id)) {
        *error = tr("A plugin folder named \"%1\" already exists.").arg(id);
        return {};
    }
    const QString directory = root.filePath(id);
    QDir().mkpath(directory);

    const QJsonObject manifest{{"id", id},
                               {"name", name.trimmed()},
                               {"version", "0.1.0"},
                               {"author", ""},
                               {"description", "Describe what the plugin does."},
                               {"main", "main.js"},
                               {"minAppVersion", "0.1.0"}};
    QFile manifestFile(QDir(directory).filePath(QStringLiteral("manifest.json")));
    QFile mainFile(QDir(directory).filePath(QStringLiteral("main.js")));
    if (!manifestFile.open(QIODevice::WriteOnly) || !mainFile.open(QIODevice::WriteOnly)) {
        *error = tr("Cannot write to %1.").arg(QDir::toNativeSeparators(directory));
        return {};
    }
    manifestFile.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    mainFile.write(QString::fromLatin1(pluginTemplate).arg(name.trimmed()).toUtf8());
    manifestFile.close();
    mainFile.close();

    reload();
    return directory;
}

void PluginManager::notify(const QString &event, const QJSValueList &arguments)
{
    for (const auto &plugin : m_plugins) {
        for (const auto &[name, handler] : plugin->events) {
            if (name == event)
                call(*plugin, handler, arguments, nullptr);
        }
    }
}

void PluginManager::notifyOpen(const QString &path)
{
    notify(QStringLiteral("open"), {path});
}

QString PluginManager::notifyBeforeSave(const QString &path, const QString &text)
{
    QString result = text;
    for (const auto &plugin : m_plugins) {
        for (const auto &[name, handler] : plugin->events) {
            if (name != QLatin1String("beforeSave"))
                continue;
            const QJSValue value = call(*plugin, handler, {path, result}, nullptr);
            if (value.isString())
                result = value.toString();
        }
    }
    return result;
}

void PluginManager::notifySave(const QString &path)
{
    notify(QStringLiteral("save"), {path});
}
