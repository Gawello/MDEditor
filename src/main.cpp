#include "app/MainWindow.h"
#include "plugins/PluginManager.h"
#include "theme/ThemeManager.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("MDEditor"));
    QApplication::setOrganizationName(QStringLiteral("MDEditor"));
    QApplication::setApplicationVersion(QStringLiteral(MDEDITOR_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Markdown editor with live preview"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Markdown files to open."),
                                 QStringLiteral("[files...]"));
    parser.process(app);

    ThemeManager::instance().initialize();
    MainWindow window;
    window.show();
    window.restoreSession(parser.positionalArguments());

    const int result = app.exec();
    PluginManager::instance().shutdown();
    return result;
}
