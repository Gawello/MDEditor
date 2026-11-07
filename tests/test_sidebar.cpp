#include "TestHarness.h"
#include "app/AppSettings.h"
#include "panels/FavoritesPanel.h"
#include "panels/FolderPanel.h"
#include "panels/Sidebar.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QListWidget>
#include <QSettings>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>

namespace {

// Each test starts from a known list, or from "never set" to see the first run.
void setFavorites(const QStringList &folders)
{
    AppSettings::instance().setValue(AppSettings::favoritesKey, folders);
}

struct Clean {
    ~Clean() { setFavorites({}); }
};

QString folder(QTemporaryDir &dir, const QString &name)
{
    QDir(dir.path()).mkpath(name);
    return QDir::cleanPath(dir.filePath(name));
}

template <typename Condition>
bool waitUntil(Condition condition, int milliseconds = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < milliseconds)
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    return condition();
}

// Answers a context menu by triggering the action with this text.
void chooseFromMenu(const QString &text)
{
    QTimer::singleShot(0, qApp, [text] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu)
            return;
        QAction *chosen = nullptr;
        for (QAction *action : menu->actions()) {
            if (action->text() == text)
                chosen = action;
        }
        menu->close();
        if (chosen)
            chosen->trigger();
    });
}

void rightClick(QWidget *viewport, const QPoint &pos)
{
    QContextMenuEvent event(QContextMenuEvent::Mouse, pos, viewport->mapToGlobal(pos));
    QApplication::sendEvent(viewport, &event);
}

} // namespace

TEST_CASE(favorites_start_with_the_usual_folders)
{
    Clean clean;
    QSettings().remove(QString::fromLatin1(AppSettings::favoritesKey));
    FavoritesPanel panel;
    CHECK(!panel.folders().isEmpty());
    CHECK(panel.folders().removeDuplicates() == 0);
}

TEST_CASE(favorites_the_user_emptied_stay_empty)
{
    Clean clean;
    setFavorites({});
    FavoritesPanel panel;
    CHECK(panel.folders().isEmpty());
}

TEST_CASE(favorites_are_remembered_by_a_new_instance)
{
    Clean clean;
    setFavorites({});
    QTemporaryDir dir;
    const QString a = folder(dir, QStringLiteral("a"));
    const QString b = folder(dir, QStringLiteral("zażółć gęślą"));
    {
        FavoritesPanel panel;
        CHECK(panel.addFolder(a));
        CHECK(panel.addFolder(b));
    }
    FavoritesPanel afterUpdate;
    CHECK(afterUpdate.folders() == QStringList({a, b}));
}

TEST_CASE(favorites_ignore_the_same_folder_spelled_differently)
{
    Clean clean;
    setFavorites({});
    QTemporaryDir dir;
    const QString a = folder(dir, QStringLiteral("a"));
    FavoritesPanel panel;
    CHECK(panel.addFolder(a));
    CHECK(!panel.addFolder(a + QStringLiteral("/")));
    CHECK(!panel.addFolder(a + QStringLiteral("/../a")));
    CHECK(panel.folders().size() == 1);
}

TEST_CASE(favorites_remove_and_reorder_persist)
{
    Clean clean;
    setFavorites({});
    QTemporaryDir dir;
    const QString a = folder(dir, QStringLiteral("a"));
    const QString b = folder(dir, QStringLiteral("b"));
    const QString c = folder(dir, QStringLiteral("c"));
    {
        FavoritesPanel panel;
        panel.addFolder(a);
        panel.addFolder(b);
        panel.addFolder(c);
        panel.findChild<QListWidget *>()->model()->moveRow(QModelIndex(), 2, QModelIndex(), 0);
        panel.removeFolder(a);
    }
    FavoritesPanel again;
    CHECK(again.folders() == QStringList({c, b}));
}

TEST_CASE(favorites_a_click_opens_the_folder_in_the_files_section)
{
    Clean clean;
    setFavorites({});
    QTemporaryDir dir;
    const QString a = folder(dir, QStringLiteral("a"));
    Sidebar sidebar;
    sidebar.favorites()->addFolder(a);
    auto *list = sidebar.favorites()->findChild<QListWidget *>();
    emit list->itemClicked(list->item(0));
    CHECK(sidebar.rootPath() == a);
}

TEST_CASE(favorites_a_missing_folder_says_so_and_changes_nothing)
{
    Clean clean;
    setFavorites({});
    QTemporaryDir dir;
    const QString gone = folder(dir, QStringLiteral("gone"));
    Sidebar sidebar;
    sidebar.favorites()->addFolder(gone);
    QDir(gone).removeRecursively();

    QString message;
    QObject::connect(&sidebar, &Sidebar::statusMessage, &sidebar, [&](const QString &text) { message = text; });
    auto *list = sidebar.favorites()->findChild<QListWidget *>();
    emit list->itemClicked(list->item(0));
    CHECK(sidebar.rootPath().isEmpty());
    CHECK(message.contains(QDir::toNativeSeparators(gone)));
    CHECK(sidebar.favorites()->folders() == QStringList(gone)); // the drive may come back
}

TEST_CASE(tree_context_menu_adds_a_folder_to_favorites)
{
    Clean clean;
    setFavorites({});
    QTemporaryDir dir;
    const QString sub = folder(dir, QStringLiteral("sub"));
    Sidebar sidebar;
    sidebar.resize(300, 700);
    sidebar.show();
    sidebar.setRootPath(dir.path());
    auto *tree = sidebar.files()->findChild<QTreeView *>();
    const QModelIndex root = tree->rootIndex();
    CHECK(waitUntil([&] { return tree->model()->rowCount(root) == 1; }));
    QApplication::processEvents();

    chooseFromMenu(QStringLiteral("Add to Favorites"));
    rightClick(tree->viewport(), tree->visualRect(tree->model()->index(0, 0, root)).center());
    CHECK(sidebar.favorites()->folders() == QStringList(sub));
}

TEST_CASE(sidebar_vault_section_is_always_there_and_invites_to_open_one)
{
    Sidebar sidebar;
    sidebar.show();
    int requests = 0;
    QObject::connect(&sidebar, &Sidebar::openVaultRequested, &sidebar, [&] { ++requests; });

    QPushButton *open = nullptr;
    for (QPushButton *button : sidebar.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("Open Vault…"))
            open = button;
    }
    CHECK(open != nullptr);
    if (!open)
        return;
    CHECK(open->isVisibleTo(&sidebar));
    CHECK(!sidebar.vault()->isVisibleTo(&sidebar));
    open->click();
    CHECK(requests == 1);

    QTemporaryDir dir;
    sidebar.setVaultRoot(dir.path());
    CHECK(sidebar.vault()->isVisibleTo(&sidebar));
    CHECK(!open->isVisibleTo(&sidebar));
    CHECK(sidebar.vault()->rootPath() == QDir(dir.path()).absolutePath());

    sidebar.setVaultRoot(QString());
    CHECK(open->isVisibleTo(&sidebar));
    CHECK(sidebar.vault()->rootPath().isEmpty());
}

namespace {

struct Sections {
    Sidebar sidebar;
    QToolButton *favorites = nullptr;
    QToolButton *vault = nullptr;
    QToolButton *files = nullptr;

    Sections()
    {
        sidebar.resize(300, 700);
        sidebar.show();
        QApplication::processEvents();
        for (QToolButton *button : sidebar.findChildren<QToolButton *>()) {
            if (button->text() == QStringLiteral("Favorites"))
                favorites = button;
            else if (button->text() == QStringLiteral("Vault"))
                vault = button;
            else if (button->text() == QStringLiteral("Files"))
                files = button;
        }
    }
    int top(QToolButton *header)
    {
        QApplication::processEvents();
        return header->mapTo(&sidebar, QPoint(0, 0)).y();
    }
};

} // namespace

// The last section must not jump to the bottom of the window when it closes.
TEST_CASE(sidebar_closing_a_section_leaves_its_header_where_it_was)
{
    Sections s;
    const int favorites = s.top(s.favorites);
    const int vault = s.top(s.vault);
    const int files = s.top(s.files);
    s.files->click();
    CHECK(s.top(s.files) == files);
    CHECK(s.top(s.vault) == vault);
    CHECK(s.top(s.favorites) == favorites);
    s.files->click();
    CHECK(s.top(s.files) == files);
    CHECK(s.sidebar.files()->height() > 50);
}

TEST_CASE(sidebar_closing_a_section_moves_the_ones_below_up_to_it)
{
    Sections s;
    const int vault = s.top(s.vault);
    const int filesTop = s.top(s.files);
    const int filesHeight = s.sidebar.files()->height();
    s.vault->click();
    CHECK(s.top(s.vault) == vault);
    CHECK(s.top(s.files) < filesTop);
    CHECK(s.sidebar.files()->height() > filesHeight); // the section below takes the space
}

TEST_CASE(sidebar_all_sections_closed_stack_at_the_top)
{
    Sections s;
    s.favorites->click();
    s.vault->click();
    s.files->click();
    CHECK(s.top(s.favorites) < s.top(s.vault));
    CHECK(s.top(s.vault) < s.top(s.files));
    CHECK(s.top(s.files) < 150);
    s.vault->click();
    CHECK(s.top(s.files) > 150);
}

TEST_CASE(sidebar_files_from_both_trees_are_reported)
{
    Sidebar sidebar;
    QStringList opened;
    QObject::connect(&sidebar, &Sidebar::fileActivated, &sidebar, [&](const QString &path) { opened << path; });
    emit sidebar.files()->fileActivated(QStringLiteral("/a.md"));
    emit sidebar.vault()->fileActivated(QStringLiteral("/b.md"));
    CHECK(opened == QStringList({QStringLiteral("/a.md"), QStringLiteral("/b.md")}));
}

// Opening must undo the height limit of a closed section, or it could never grow again.
TEST_CASE(sidebar_section_reopens_to_full_height)
{
    Sidebar sidebar;
    sidebar.resize(300, 700);
    sidebar.show();
    QWidget *section = sidebar.favorites()->parentWidget();
    QToolButton *header = section->findChild<QToolButton *>();
    header->click();
    CHECK(section->maximumHeight() < 100);
    header->click();
    CHECK(section->maximumHeight() == QWIDGETSIZE_MAX);
}

TEST_CASE(sidebar_remembers_which_sections_are_open)
{
    const auto header = [](Sidebar &sidebar, const QString &title) -> QToolButton * {
        for (QToolButton *button : sidebar.findChildren<QToolButton *>()) {
            if (button->text() == title)
                return button;
        }
        return nullptr;
    };
    {
        Sidebar sidebar;
        sidebar.restoreLayout();
        header(sidebar, QStringLiteral("Favorites"))->click();
        CHECK(!sidebar.favorites()->isVisibleTo(&sidebar));
        sidebar.saveLayout();
    }
    {
        Sidebar sidebar;
        sidebar.restoreLayout();
        CHECK(!sidebar.favorites()->isVisibleTo(&sidebar));
        CHECK(sidebar.files()->isVisibleTo(&sidebar));
        header(sidebar, QStringLiteral("Favorites"))->click();
        sidebar.saveLayout();
    }
}
