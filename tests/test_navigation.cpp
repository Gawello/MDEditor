#include "TestHarness.h"
#include "app/EditorTab.h"
#include "app/MainWindow.h"
#include "app/NavigationHistory.h"
#include "editor/MarkdownEditor.h"
#include "panels/SearchPanel.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QMouseEvent>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>

namespace {

using Place = NavigationHistory::Place;

void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(data);
}

QAction *action(MainWindow &window, const QString &text)
{
    for (QAction *candidate : window.findChildren<QAction *>()) {
        if (QString(candidate->text()).remove(u'&') == text)
            return candidate;
    }
    return nullptr;
}

QTabWidget *pane(MainWindow &window)
{
    for (QTabWidget *candidate : window.findChildren<QTabWidget *>()) {
        if (candidate->isVisibleTo(&window))
            return candidate;
    }
    return nullptr;
}

EditorTab *current(MainWindow &window)
{
    return qobject_cast<EditorTab *>(pane(window)->currentWidget());
}

QString currentPath(MainWindow &window)
{
    EditorTab *tab = current(window);
    return tab ? tab->filePath() : QString();
}

int cursorPosition(MainWindow &window)
{
    return current(window)->editor()->textCursor().position();
}

void moveCursor(MainWindow &window, int position)
{
    MarkdownEditor *editor = current(window)->editor();
    QTextCursor cursor(editor->document());
    cursor.setPosition(position);
    editor->setTextCursor(cursor);
}

// Three notes with enough text to put the cursor somewhere.
struct Notes {
    QTemporaryDir dir;
    QString a, b, c, d;
    Notes()
    {
        a = dir.filePath(QStringLiteral("a.md"));
        b = dir.filePath(QStringLiteral("b.md"));
        c = dir.filePath(QStringLiteral("c.md"));
        d = dir.filePath(QStringLiteral("d.md"));
        for (const QString &path : {a, b, c, d})
            writeFile(path, "# Title\n\nfirst line\nsecond line\nthird line\n");
    }
};

void closeQuietly(MainWindow &window)
{
    for (EditorTab *tab : window.findChildren<EditorTab *>())
        tab->editor()->document()->setModified(false);
    window.close();
}

} // namespace

TEST_CASE(navigation_history_list)
{
    NavigationHistory history;
    CHECK(!history.canGoBack() && !history.canGoForward());
    history.visit({"/n/a.md", 0});
    history.visit({"/n/a.md", 5}); // moving inside a file isn't a new place
    CHECK(history.places().size() == 1 && history.places()[0].position == 5);
    history.visit({"/n/b.md", 1});
    history.visit({"/n/b.md", 9, }, true); // a jump inside the file is
    CHECK(history.places().size() == 3);
    history.visit({"/n/b.md", 9}, true); // ...unless it lands where the cursor is
    CHECK(history.places().size() == 3);

    CHECK((history.back() == Place{"/n/b.md", 1}));
    CHECK((history.back() == Place{"/n/a.md", 5}));
    CHECK(!history.back().has_value() && !history.canGoBack());
    CHECK((history.forward() == Place{"/n/b.md", 1}));
    history.updatePosition("/n/b.md", 3);
    history.updatePosition("/n/a.md", 7); // not the current place: ignored
    CHECK(history.places()[1].position == 3 && history.places()[0].position == 5);

    // A new place drops the forward entries.
    history.visit({"/n/c.md", 0});
    CHECK(history.places().size() == 3 && !history.canGoForward());
    CHECK(history.places()[2].path == "/n/c.md");

    // Renaming a folder moves its entries; deleting removes them and keeps the index sensible.
    history.visit({"/n/sub/d.md", 0});
    history.rename("/n/sub", "/n/other");
    CHECK(history.places()[3].path == "/n/other/d.md");
    history.rename("/n/a.md", "/n/z.md");
    CHECK(history.places()[0].path == "/n/z.md");
    history.remove("/n/other");
    CHECK(history.places().size() == 3 && history.index() == 2);
    history.remove("/n/b.md"); // z c: the index stays on c
    CHECK(history.places().size() == 2 && history.index() == 1 && history.places()[1].path == "/n/c.md");
    history.remove("/n/c.md");
    CHECK(history.places().size() == 1 && history.index() == 0);
    history.remove("/n/z.md");
    CHECK(history.places().isEmpty() && history.index() == -1 && !history.canGoBack());

    // Two places that meet after a removal and are the same become one.
    history.visit({"/n/a.md", 0});
    history.visit({"/n/b.md", 0});
    history.visit({"/n/a.md", 0});
    history.remove("/n/b.md");
    CHECK(history.places().size() == 1 && history.index() == 0);

    // At most maxPlaces entries.
    NavigationHistory longOne;
    for (int i = 0; i < NavigationHistory::maxPlaces + 20; ++i)
        longOne.visit({QStringLiteral("/n/%1.md").arg(i), 0});
    CHECK(longOne.places().size() == NavigationHistory::maxPlaces);
    CHECK(longOne.index() == NavigationHistory::maxPlaces - 1);
    CHECK(longOne.places().front().path == "/n/20.md");
}

TEST_CASE(back_and_forward_between_notes)
{
    Notes notes;
    QSettings().remove(QStringLiteral("session"));
    MainWindow window;
    window.show();
    QAction *back = action(window, QStringLiteral("Back"));
    QAction *forward = action(window, QStringLiteral("Forward"));
    CHECK(back && forward);
    CHECK(!back->isEnabled());

    CHECK(window.openFile(notes.a));
    moveCursor(window, 12);
    CHECK(window.openFile(notes.b));
    moveCursor(window, 20);
    CHECK(window.openFile(notes.c));
    CHECK(back->isEnabled() && !forward->isEnabled());

    back->trigger();
    CHECK(currentPath(window) == notes.b && cursorPosition(window) == 20);
    back->trigger();
    CHECK(currentPath(window) == notes.a && cursorPosition(window) == 12);
    CHECK(!back->isEnabled() && forward->isEnabled());
    moveCursor(window, 3); // remembered when leaving with Forward
    forward->trigger();
    CHECK(currentPath(window) == notes.b);
    back->trigger();
    CHECK(currentPath(window) == notes.a && cursorPosition(window) == 3);
    forward->trigger();
    forward->trigger();
    CHECK(currentPath(window) == notes.c && !forward->isEnabled());

    // Switching tabs is navigation too.
    pane(window)->setCurrentIndex(0);
    CHECK(currentPath(window) == notes.a);
    back->trigger();
    CHECK(currentPath(window) == notes.c);

    // Opening something new drops the forward entries.
    back->trigger();
    CHECK(currentPath(window) == notes.b);
    CHECK(window.openFile(notes.d));
    CHECK(!forward->isEnabled());
    back->trigger();
    CHECK(currentPath(window) == notes.b);

    // The mouse's back button.
    QWidget *viewport = current(window)->editor()->viewport();
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(5, 5), viewport->mapToGlobal(QPointF(5, 5)),
                      Qt::ForwardButton, Qt::ForwardButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &press);
    CHECK(currentPath(window) == notes.d);
    closeQuietly(window);
}

TEST_CASE(back_skips_deleted_notes_and_follows_renames)
{
    Notes notes;
    QSettings().remove(QStringLiteral("session"));
    MainWindow window;
    window.show();
    QAction *back = action(window, QStringLiteral("Back"));
    CHECK(window.openFile(notes.a));
    CHECK(window.openFile(notes.b));
    CHECK(window.openFile(notes.c));
    // b is gone from the disk (its tab closed): Back goes straight to a.
    pane(window)->setCurrentIndex(1);
    action(window, QStringLiteral("Close Tab"))->trigger();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(QFile::remove(notes.b));
    pane(window)->setCurrentWidget(pane(window)->widget(pane(window)->count() - 1));
    CHECK(currentPath(window) == notes.c);
    back->trigger();
    CHECK(currentPath(window) == notes.a);

    // A search result is a jump, even in the same note: Back returns to where the cursor was.
    moveCursor(window, 2);
    auto *search = window.findChild<SearchPanel *>();
    CHECK(search != nullptr);
    emit search->openRequested(notes.a, 4, 0, 5);
    CHECK(currentPath(window) == notes.a);
    CHECK(current(window)->editor()->textCursor().block().blockNumber() == 4);
    back->trigger();
    CHECK(currentPath(window) == notes.a && cursorPosition(window) == 2);
    closeQuietly(window);
}

TEST_CASE(reopen_closed_tab)
{
    Notes notes;
    QSettings().remove(QStringLiteral("session"));
    MainWindow window;
    window.show();
    QAction *reopen = action(window, QStringLiteral("Reopen Closed Tab"));
    QAction *close = action(window, QStringLiteral("Close Tab"));
    CHECK(reopen && !reopen->isEnabled());
    CHECK(window.openFile(notes.a));
    CHECK(window.openFile(notes.b));
    moveCursor(window, 25);
    close->trigger();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(currentPath(window) == notes.a && reopen->isEnabled());

    reopen->trigger();
    CHECK(currentPath(window) == notes.b && cursorPosition(window) == 25);
    CHECK(pane(window)->count() == 2 && !reopen->isEnabled());

    // Closed tabs whose file is gone are skipped.
    close->trigger(); // b
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    close->trigger(); // a: the window keeps an empty tab
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(QFile::remove(notes.a));
    reopen->trigger();
    CHECK(currentPath(window) == notes.b);
    CHECK(!reopen->isEnabled());
    closeQuietly(window);
}

TEST_CASE(go_to_tab_by_number)
{
    Notes notes;
    QSettings().remove(QStringLiteral("session"));
    MainWindow window;
    window.show();
    CHECK(window.openFile(notes.a));
    CHECK(window.openFile(notes.b));
    CHECK(window.openFile(notes.c));
    action(window, QStringLiteral("Tab 1"))->trigger();
    CHECK(pane(window)->currentIndex() == 0);
    action(window, QStringLiteral("Last Tab"))->trigger();
    CHECK(pane(window)->currentIndex() == 2);
    action(window, QStringLiteral("Tab 2"))->trigger();
    CHECK(pane(window)->currentIndex() == 1);
    action(window, QStringLiteral("Tab 5"))->trigger(); // no such tab: nothing happens
    CHECK(pane(window)->currentIndex() == 1);
    CHECK(action(window, QStringLiteral("Tab 1"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_1));
    closeQuietly(window);
}

TEST_CASE(back_crosses_split_panes)
{
    Notes notes;
    QSettings().remove(QStringLiteral("session"));
    MainWindow window;
    window.show();
    CHECK(window.openFile(notes.a));
    CHECK(window.openFile(notes.b));
    action(window, QStringLiteral("Split Right"))->trigger(); // b, twinned in a new right pane
    QList<QTabWidget *> panes;
    for (QTabWidget *candidate : window.findChildren<QTabWidget *>()) {
        if (candidate->isVisibleTo(&window))
            panes << candidate;
    }
    CHECK(panes.size() == 2);
    // The new pane holds b's twin and is current: c opens there.
    const auto paneWith = [&panes](const QString &path) -> QTabWidget * {
        for (QTabWidget *candidate : panes) {
            for (int i = 0; i < candidate->count(); ++i) {
                if (qobject_cast<EditorTab *>(candidate->widget(i))->filePath() == path
                    && candidate->count() == 1)
                    return candidate;
            }
        }
        return nullptr;
    };
    QTabWidget *right = paneWith(notes.b);
    CHECK(right != nullptr);
    QTabWidget *left = panes.value(0) == right ? panes.value(1) : panes.value(0);
    CHECK(window.openFile(notes.c));
    CHECK(right->count() == 2 && qobject_cast<EditorTab *>(right->currentWidget())->filePath() == notes.c);

    QAction *back = action(window, QStringLiteral("Back"));
    back->trigger(); // b: the right pane's twin
    CHECK(qobject_cast<EditorTab *>(right->currentWidget())->filePath() == notes.b);
    back->trigger(); // a, in the left pane
    back->trigger(); // nothing before a
    CHECK(qobject_cast<EditorTab *>(left->currentWidget())->filePath() == notes.a);
    CHECK(window.findChildren<EditorTab *>().size() == 4); // Back never opens duplicates
    closeQuietly(window);
}
