#include "TestHarness.h"
#include "app/EditorTab.h"
#include "app/Hotkeys.h"
#include "app/HotkeysPage.h"
#include "app/MainWindow.h"
#include "editor/MarkdownEditor.h"

#include <QAction>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QTableWidget>

namespace {

QAction *action(MainWindow &window, const QString &text)
{
    for (QAction *candidate : window.findChildren<QAction *>()) {
        if (QString(candidate->text()).remove(u'&') == text)
            return candidate;
    }
    return nullptr;
}

void closeQuietly(MainWindow &window)
{
    for (EditorTab *tab : window.findChildren<EditorTab *>())
        tab->editor()->document()->setModified(false);
    window.close();
}

} // namespace

TEST_CASE(hotkeys_can_be_changed_and_are_kept)
{
    QSettings().remove(QStringLiteral("hotkeys"));
    QSettings().remove(QStringLiteral("session"));
    Hotkeys &hotkeys = Hotkeys::instance();
    {
        MainWindow window;
        CHECK(hotkeys.entries().size() > 60);
        CHECK(hotkeys.shortcuts(QStringLiteral("File/Save")) == QList<QKeySequence>{QKeySequence::Save});
        CHECK(!hotkeys.isCustomized(QStringLiteral("File/Save")));
        // Submenus are part of the id; the Plugins menu isn't listed.
        bool nested = false;
        for (const Hotkeys::Entry &entry : hotkeys.entries()) {
            CHECK(!entry.id.startsWith(u"Plugins/"));
            nested = nested || entry.id == u"Go/Go to Tab/Go to Tab 1";
        }
        CHECK(nested);

        hotkeys.setShortcuts(QStringLiteral("File/Save"), {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S)});
        CHECK(action(window, QStringLiteral("Save"))->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S));
        hotkeys.setShortcuts(QStringLiteral("Edit/Delete Line"), {});
        CHECK(action(window, QStringLiteral("Delete Line"))->shortcut().isEmpty());
        // Conflicts are named.
        CHECK(hotkeys.conflicts(QStringLiteral("Edit/Move Line Up"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S))
              == QStringList{"File › Save"});
        closeQuietly(window);
    }
    {
        MainWindow window; // a new start keeps them
        CHECK(action(window, QStringLiteral("Save"))->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S));
        CHECK(action(window, QStringLiteral("Delete Line"))->shortcut().isEmpty());
        hotkeys.reset(QStringLiteral("File/Save"));
        CHECK(action(window, QStringLiteral("Save"))->shortcut() == QKeySequence(QKeySequence::Save));
        hotkeys.setShortcuts(QStringLiteral("Edit/Delete Line"), {QKeySequence(Qt::CTRL | Qt::Key_D)}); // the default
        CHECK(!hotkeys.isCustomized(QStringLiteral("Edit/Delete Line")));
        closeQuietly(window);
    }
    QSettings().remove(QStringLiteral("hotkeys"));
}

TEST_CASE(hotkeys_settings_page)
{
    QSettings().remove(QStringLiteral("hotkeys"));
    MainWindow window;
    HotkeysPage page;
    QTableWidget *table = page.table();
    CHECK(table->rowCount() == Hotkeys::instance().entries().size());
    page.filter(QStringLiteral("bold"));
    int shown = 0;
    int boldRow = -1;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (!table->isRowHidden(row)) {
            ++shown;
            if (table->item(row, 0)->text() == u"Format › Bold")
                boldRow = row;
        }
    }
    CHECK(shown >= 1 && boldRow >= 0);
    page.filter(QKeySequence(Qt::CTRL | Qt::Key_B).toString(QKeySequence::NativeText)); // by key
    CHECK(boldRow >= 0 && !table->isRowHidden(boldRow));

    // A new key in the table: applied, and a conflict is pointed out.
    auto *edit = qobject_cast<QKeySequenceEdit *>(table->cellWidget(boldRow, 1));
    CHECK(edit != nullptr);
    if (edit) {
        edit->setKeySequence(QKeySequence(QKeySequence::Save));
        emit edit->editingFinished();
        CHECK(action(window, QStringLiteral("Bold"))->shortcut() == QKeySequence(QKeySequence::Save));
        CHECK(!page.warning()->isHidden() && page.warning()->text().contains("File › Save"));
    }
    Hotkeys::instance().reset(QStringLiteral("Format/Bold"));
    CHECK(action(window, QStringLiteral("Bold"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_B));
    QSettings().remove(QStringLiteral("hotkeys"));
    closeQuietly(window);
}
