#include "TestHarness.h"
#include "app/AppSettings.h"
#include "editor/MarkdownEditor.h"
#include "format/FormatterRegistry.h"
#include "plugins/PluginManager.h"
#include "theme/ThemeManager.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <algorithm>

namespace {

MarkdownEditor *editor()
{
    static MarkdownEditor *instance = new MarkdownEditor;
    return instance;
}

void loadPlugins(const QString &directory, const QStringList &enabled)
{
    PluginManager &plugins = PluginManager::instance();
    plugins.setEditorProvider([] { return editor(); });
    plugins.setPluginsDirectory(directory);
    AppSettings::instance().setValue(AppSettings::enabledPluginsKey, enabled);
    plugins.reload();
}

PluginManager::Info info(const QString &id)
{
    for (const PluginManager::Info &plugin : PluginManager::instance().plugins()) {
        if (plugin.id == id)
            return plugin;
    }
    return {};
}

void writePlugin(const QDir &root, const QString &id, const QString &code)
{
    root.mkpath(id);
    QFile manifest(root.filePath(id + "/manifest.json"));
    manifest.open(QIODevice::WriteOnly);
    manifest.write(QStringLiteral("{\"id\": \"%1\", \"name\": \"%1\"}").arg(id).toUtf8());
    QFile main(root.filePath(id + "/main.js"));
    main.open(QIODevice::WriteOnly);
    main.write(code.toUtf8());
}

const QStringList examples{"insert-date", "sort-lines", "word-count", "yaml-formatter", "nord-plus", "last-modified"};

} // namespace

TEST_CASE(example_plugins_load)
{
    loadPlugins(QStringLiteral(MDEDITOR_EXAMPLE_PLUGINS), examples);
    CHECK(PluginManager::instance().plugins().size() == examples.size());
    for (const QString &id : examples) {
        CHECK(info(id).loaded);
        CHECK(info(id).error.isEmpty());
    }
    const auto commands = PluginManager::instance().commands();
    CHECK(std::any_of(commands.begin(), commands.end(), [](const auto &c) { return c.id == "insert-date"; }));
}

TEST_CASE(example_commands)
{
    editor()->setMarkdown(QStringLiteral("banana\ncherry\napple"));
    editor()->selectAll();
    PluginManager::instance().runCommand("sort-lines", "sort-asc");
    CHECK(editor()->markdown() == "apple\nbanana\ncherry");

    editor()->setMarkdown(QString());
    PluginManager::instance().runCommand("insert-date", "insert-date");
    CHECK(QRegularExpression("^\\d{4}-\\d{2}-\\d{2}$").match(editor()->markdown()).hasMatch());
}

TEST_CASE(example_formatter_and_theme)
{
    const auto outcome = FormatterRegistry::instance().format("yml", "a:1\nb:\n  c:2  ");
    CHECK(outcome.ok);
    CHECK(outcome.text == "a: 1\nb:\n    c: 2");
    CHECK(outcome.formatter.contains("yaml-formatter"));

    const auto themes = ThemeManager::instance().themes();
    CHECK(std::any_of(themes.begin(), themes.end(), [](const Theme &t) { return t.id == "nord-plus"; }));
}

TEST_CASE(example_before_save_event)
{
    const QString text = "---\ntitle: x\nmodified: 2000-01-01\n---\nbody";
    const QString saved = PluginManager::instance().notifyBeforeSave("note.md", text);
    CHECK(!saved.contains("2000-01-01"));
    CHECK(saved.startsWith("---\ntitle: x\nmodified: "));
    CHECK(saved.endsWith("\n---\nbody"));
    CHECK(PluginManager::instance().notifyBeforeSave("plain.md", "no front matter") == "no front matter");
}

TEST_CASE(broken_plugins_are_isolated)
{
    QTemporaryDir directory;
    const QDir root(directory.path());
    writePlugin(root, "throws", "throw new Error('boom');");
    writePlugin(root, "loops", "app.registerCommand('Loop', function () { while (true) {} });");
    writePlugin(root, "fine", "app.registerCommand('Fine', function () { editor.setText('still works'); });");
    loadPlugins(directory.path(), {"throws", "loops", "fine"});

    CHECK(!info("throws").loaded);
    CHECK(info("throws").error.contains("boom"));
    CHECK(info("loops").loaded);

    QElapsedTimer timer;
    timer.start();
    PluginManager::instance().runCommand("loops", "Loop");
    CHECK(timer.elapsed() < 6000); // interrupted by the watchdog
    CHECK(!info("loops").log.isEmpty());

    PluginManager::instance().runCommand("fine", "Fine");
    CHECK(editor()->markdown() == "still works");
}

TEST_CASE(create_plugin_scaffold)
{
    QTemporaryDir directory;
    loadPlugins(directory.path(), {});
    QString error;
    const QString folder = PluginManager::instance().createPlugin("My First Plugin!", &error);
    CHECK(error.isEmpty());
    CHECK(QDir(folder).dirName() == "my-first-plugin");
    CHECK(QFile::exists(folder + "/manifest.json"));
    CHECK(!info("my-first-plugin").enabled); // new plugins start disabled

    PluginManager::instance().setEnabled("my-first-plugin", true);
    CHECK(info("my-first-plugin").loaded);
    editor()->setMarkdown(QString());
    PluginManager::instance().runCommand("my-first-plugin", "hello");
    CHECK(editor()->markdown() == "Hello from My First Plugin!!");

    CHECK(PluginManager::instance().createPlugin("My First Plugin!", &error).isEmpty()); // already exists
    CHECK(!error.isEmpty());
}
