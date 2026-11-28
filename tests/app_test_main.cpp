#include "TestHarness.h"
#include "editor/MathRenderer.h"
#include "notes/Snapshots.h"
#include "plugins/PluginManager.h"
#include "theme/ThemeManager.h"

#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("MDEditorTests")); // never touch the user's settings
    QApplication::setApplicationName(QStringLiteral("MDEditorTests"));
    QSettings().clear();
    QTemporaryDir snapshots; // version history copies made by the tests go here, not in the profile
    Snapshots::setRoot(snapshots.path());
    ThemeManager::instance().initialize();

    // An argument runs only the tests whose name contains it: app_tests backlinks
    const std::string filter = argc > 1 ? argv[1] : "";
    for (const auto &c : test::registry()) {
        if (!filter.empty() && std::string(c.name).find(filter) == std::string::npos)
            continue;
        const int before = test::failures();
        c.fn();
        std::cout << (test::failures() == before ? "[ OK ] " : "[FAIL] ") << c.name << "\n";
    }
    std::cout << test::registry().size() << " test cases, " << test::failures() << " failed checks\n";

    PluginManager::instance().shutdown();
    MathRenderer::shutdown();
    QSettings().clear();
    return test::failures() == 0 ? 0 : 1;
}
