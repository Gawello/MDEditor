#include "format/FormatterRegistry.h"

#include "app/AppSettings.h"
#include "format/CodeFormatters.h"

#include <QHash>
#include <QProcess>
#include <QStandardPaths>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace {

std::u16string_view view(const QString &text)
{
    return {reinterpret_cast<const char16_t *>(text.utf16()), size_t(text.size())};
}

const QHash<QString, QString> &aliases()
{
    static const QHash<QString, QString> map{
        {"json", "json"}, {"jsonc", "json"}, {"geojson", "json"},
        {"xml", "xml"}, {"svg", "xml"}, {"xsd", "xml"}, {"xaml", "xml"}, {"plist", "xml"},
        {"html", "html"}, {"htm", "html"}, {"xhtml", "html"},
        {"css", "css"}, {"scss", "css"}, {"less", "css"},
        {"c", "c"}, {"h", "c"},
        {"cpp", "cpp"}, {"c++", "cpp"}, {"cc", "cpp"}, {"cxx", "cpp"}, {"hpp", "cpp"}, {"hh", "cpp"},
        {"java", "java"}, {"kotlin", "kotlin"}, {"kt", "kotlin"},
        {"js", "javascript"}, {"javascript", "javascript"}, {"jsx", "javascript"}, {"mjs", "javascript"},
        {"ts", "typescript"}, {"typescript", "typescript"}, {"tsx", "typescript"},
        {"cs", "csharp"}, {"csharp", "csharp"}, {"c#", "csharp"},
        {"go", "go"}, {"golang", "go"}, {"rust", "rust"}, {"rs", "rust"}, {"swift", "swift"},
        {"php", "php"}, {"dart", "dart"}, {"scala", "scala"},
        {"python", "python"}, {"py", "python"}, {"python3", "python"},
    };
    return map;
}

bool isBraceLanguage(const QString &language)
{
    static const QStringList languages{"c", "cpp", "java", "kotlin", "javascript", "typescript", "csharp",
                                       "go", "rust", "swift", "php", "dart", "scala"};
    return languages.contains(language);
}

FormatterRegistry::Outcome fromResult(const fmt::Result &result)
{
    FormatterRegistry::Outcome outcome;
    outcome.ok = result.ok;
    outcome.text = QString::fromStdU16String(result.text);
    outcome.error = QString::fromStdString(result.error);
    outcome.formatter = QStringLiteral("built-in");
    return outcome;
}

FormatterRegistry::Outcome formatXml(const QString &text, int indent)
{
    QString output;
    QXmlStreamReader reader(text);
    QXmlStreamWriter writer(&output);
    writer.setAutoFormatting(true);
    writer.setAutoFormattingIndent(indent);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isCharacters() && reader.isWhitespace())
            continue; // let the writer decide the layout
        if (reader.tokenType() == QXmlStreamReader::StartDocument && reader.documentVersion().isEmpty())
            continue;
        if (reader.hasError())
            break;
        writer.writeCurrentToken(reader);
    }

    FormatterRegistry::Outcome outcome;
    outcome.formatter = QStringLiteral("built-in");
    if (reader.hasError()) {
        outcome.error = QStringLiteral("line %1: %2 (HTML must be well-formed XML)")
                            .arg(reader.lineNumber())
                            .arg(reader.errorString());
        return outcome;
    }
    outcome.ok = true;
    outcome.text = output.trimmed();
    return outcome;
}

} // namespace

FormatterRegistry &FormatterRegistry::instance()
{
    static FormatterRegistry registry;
    return registry;
}

QString FormatterRegistry::canonical(const QString &language) const
{
    const QString key = language.trimmed().toLower();
    if (aliases().contains(key))
        return aliases().value(key);
    for (const Registered &plugin : m_plugins) {
        if (plugin.language == key)
            return key;
    }
    return {};
}

QString FormatterRegistry::languageForSuffix(const QString &suffix) const
{
    return canonical(suffix);
}

QStringList FormatterRegistry::builtinLanguages() const
{
    return {"json", "xml", "html", "css", "c", "cpp", "java", "javascript", "typescript", "csharp", "go", "python"};
}

QString FormatterRegistry::detectedCommand(const QString &language) const
{
    auto has = [](const char *tool) { return !QStandardPaths::findExecutable(QLatin1String(tool)).isEmpty(); };
    static const QHash<QString, QString> extensions{{"c", "c"},       {"cpp", "cpp"},        {"java", "java"},
                                                    {"javascript", "js"}, {"typescript", "ts"}, {"csharp", "cs"},
                                                    {"css", "css"},   {"html", "html"}};

    if (language == "python") {
        if (has("ruff"))
            return QStringLiteral("ruff format -");
        if (has("black"))
            return QStringLiteral("black -q -");
        return {};
    }
    if (language == "go")
        return has("gofmt") ? QStringLiteral("gofmt") : QString();
    if ((language == "javascript" || language == "typescript" || language == "css" || language == "html")
        && has("prettier"))
        return QStringLiteral("prettier --stdin-filepath file.%1").arg(extensions.value(language));
    if (extensions.contains(language) && language != "css" && language != "html" && has("clang-format"))
        return QStringLiteral("clang-format --assume-filename=file.%1").arg(extensions.value(language));
    return {};
}

QString FormatterRegistry::externalCommand(const QString &language) const
{
    const QString configured = AppSettings::instance().formatterCommand(language).trimmed();
    if (configured == QLatin1String("builtin"))
        return {};
    return configured.isEmpty() ? detectedCommand(language) : configured;
}

FormatterRegistry::Outcome FormatterRegistry::format(const QString &language, const QString &text) const
{
    const QString id = canonical(language);
    const int indent = AppSettings::instance().indentSize();
    Outcome outcome;
    if (id.isEmpty()) {
        outcome.error = QStringLiteral("no formatter for \"%1\"").arg(language);
        return outcome;
    }

    for (const Registered &plugin : m_plugins) {
        if (plugin.language != id)
            continue;
        QString error;
        if (const auto formatted = plugin.formatter(text, indent, &error)) {
            outcome.ok = true;
            outcome.text = *formatted;
        } else {
            outcome.error = error.isEmpty() ? QStringLiteral("the formatter failed") : error;
        }
        outcome.formatter = QStringLiteral("plugin %1").arg(plugin.owner);
        return outcome;
    }

    const QString command = externalCommand(id);
    if (!command.isEmpty())
        return runExternal(command, text);

    if (id == "json")
        return fromResult(fmt::formatJson(view(text), indent));
    if (id == "css")
        return fromResult(fmt::formatCss(view(text), indent));
    if (id == "python")
        return fromResult(fmt::formatPython(view(text), indent));
    if (id == "xml" || id == "html")
        return formatXml(text, indent);
    if (isBraceLanguage(id))
        return fromResult(fmt::formatCLike(view(text), indent));

    outcome.error = QStringLiteral("no formatter for \"%1\"").arg(language);
    return outcome;
}

FormatterRegistry::Outcome FormatterRegistry::runExternal(const QString &command, const QString &text) const
{
    Outcome outcome;
    QStringList arguments = QProcess::splitCommand(command);
    if (arguments.isEmpty())
        return outcome;
    const QString program = arguments.takeFirst();
    outcome.formatter = program;

    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(3000)) {
        outcome.error = QStringLiteral("cannot start %1").arg(program);
        return outcome;
    }
    process.write(text.toUtf8());
    process.closeWriteChannel();
    if (!process.waitForFinished(5000)) {
        process.kill();
        outcome.error = QStringLiteral("%1 timed out").arg(program);
        return outcome;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        outcome.error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (outcome.error.isEmpty())
            outcome.error = QStringLiteral("%1 failed").arg(program);
        return outcome;
    }
    outcome.ok = true;
    outcome.text = QString::fromUtf8(process.readAllStandardOutput());
    while (outcome.text.endsWith(u'\n') || outcome.text.endsWith(u'\r'))
        outcome.text.chop(1);
    return outcome;
}

void FormatterRegistry::registerPluginFormatter(const QString &owner, const QStringList &languages,
                                                PluginFormatter formatter)
{
    for (const QString &language : languages) {
        const QString key = language.trimmed().toLower();
        m_plugins.append({owner, aliases().value(key, key), formatter});
    }
}

void FormatterRegistry::removePluginFormatters(const QString &owner)
{
    m_plugins.removeIf([&](const Registered &r) { return r.owner == owner; });
}
