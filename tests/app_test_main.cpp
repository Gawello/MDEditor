#include "TestHarness.h"
#include "plugins/PluginManager.h"
#include "theme/ThemeManager.h"

#include <QApplication>
#include <QSettings>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("MDEditorTests")); // never touch the user's settings
    QApplication::setApplicationName(QStringLiteral("MDEditorTests"));
    QSettings().clear();
    ThemeManager::instance().initialize();

    for (const auto &c : test::registry()) {
        const int before = test::failures();
        c.fn();
        std::cout << (test::failures() == before ? "[ OK ] " : "[FAIL] ") << c.name << "\n";
    }
    std::cout << test::registry().size() << " test cases, " << test::failures() << " failed checks\n";

    PluginManager::instance().shutdown();
    QSettings().clear();
    return test::failures() == 0 ? 0 : 1;
}
