#include "app/MainWindow.h"

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
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Markdown file to open."));
    parser.process(app);

    MainWindow window;
    window.show();
    const QStringList files = parser.positionalArguments();
    window.restoreSession(files.isEmpty() ? QString() : files.first());

    return app.exec();
}
