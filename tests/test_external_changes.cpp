#include "TestHarness.h"
#include "app/Background.h"
#include "app/EditorTab.h"
#include "app/ExternalChangeBar.h"
#include "app/MainWindow.h"
#include "document/Document.h"
#include "editor/MarkdownEditor.h"

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

using SaveResult = EditorTab::SaveResult;

namespace {

void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(data);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    file.open(QIODevice::ReadOnly);
    return file.readAll();
}

struct Fixture {
    QTemporaryDir dir;
    QString path = dir.filePath(QStringLiteral("note.md"));
    EditorTab tab;
    QString error;

    Fixture()
    {
        writeFile(path, "first");
        tab.load(path, error);
    }
    void type(const QString &text) { tab.editor()->insertPlainText(text); }
    void checkDisk()
    {
        bool finished = false;
        QObject::connect(&tab, &EditorTab::diskCheckFinished, &tab, [&] { finished = true; });
        tab.checkDisk();
        QElapsedTimer timer;
        timer.start();
        while (!finished && timer.elapsed() < 5000)
            QApplication::processEvents(QEventLoop::AllEvents, 10);
        CHECK(finished);
    }
    ExternalChangeBar *bar() const { return tab.findChild<ExternalChangeBar *>(); }
    bool barShown() const { return !bar()->isHidden(); }
    void clickBar(const QString &text)
    {
        for (QPushButton *button : bar()->findChildren<QPushButton *>()) {
            if (button->text() == text)
                button->click();
        }
    }
};

} // namespace

TEST_CASE(document_check_finds_changes_and_ignores_rewrites)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("a.md"));
    writeFile(path, "same");
    const FileStamp stamp = Document::read(path).stamp;
    CHECK(stamp.exists);
    CHECK(Document::check(path, stamp).state == DiskState::Unchanged);

    // A sync client rewriting identical bytes changes the date only.
    QFile file(path);
    file.open(QIODevice::ReadWrite);
    file.setFileTime(stamp.modified.addSecs(60), QFileDevice::FileModificationTime);
    file.close();
    CHECK(Document::check(path, stamp).state == DiskState::Unchanged);

    writeFile(path, "different");
    CHECK(Document::check(path, stamp).state == DiskState::Changed);
    QFile::remove(path);
    CHECK(Document::check(path, stamp).state == DiskState::Missing);
}

TEST_CASE(document_keeps_bom_and_crlf_through_read_and_write)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("a.md"));
    const QByteArray original("\xEF\xBB\xBFone\r\ntwo\r\n");
    writeFile(path, original);
    Document document;
    const LoadedFile file = Document::read(path);
    document.adopt(file);
    CHECK(file.text == QStringLiteral("one\ntwo\n"));
    CHECK(document.encode(file.text) == original);
}

TEST_CASE(read_reports_a_missing_file)
{
    const LoadedFile file = Document::read(QStringLiteral("/nonexistent/dir/a.md"));
    CHECK(!file.ok);
    CHECK(!file.error.isEmpty());
}

TEST_CASE(save_refuses_to_replace_a_file_changed_elsewhere)
{
    Fixture f;
    f.type(QStringLiteral("mine "));
    writeFile(f.path, "theirs");

    CHECK(f.tab.save(f.path, f.error) == SaveResult::Conflict);
    CHECK(f.tab.conflictState() == DiskState::Changed);
    CHECK(readFile(f.path) == "theirs");
    CHECK(f.tab.isModified());

    CHECK(f.tab.save(f.path, f.error, true) == SaveResult::Saved);
    CHECK(readFile(f.path) == "mine first");
    CHECK(!f.tab.isModified());
}

TEST_CASE(save_notices_a_deleted_file)
{
    Fixture f;
    f.type(QStringLiteral("x"));
    QFile::remove(f.path);
    CHECK(f.tab.save(f.path, f.error) == SaveResult::Conflict);
    CHECK(f.tab.conflictState() == DiskState::Missing);
    CHECK(!QFile::exists(f.path));
}

TEST_CASE(consecutive_saves_do_not_conflict_with_themselves)
{
    Fixture f;
    f.type(QStringLiteral("a"));
    CHECK(f.tab.save(f.path, f.error) == SaveResult::Saved);
    f.type(QStringLiteral("b"));
    CHECK(f.tab.save(f.path, f.error) == SaveResult::Saved);
    CHECK(readFile(f.path) == "abfirst");
}

TEST_CASE(save_as_does_not_check_the_old_file)
{
    Fixture f;
    f.type(QStringLiteral("x"));
    writeFile(f.path, "theirs");
    const QString other = f.dir.filePath(QStringLiteral("copy.md"));
    CHECK(f.tab.save(other, f.error) == SaveResult::Saved);
    CHECK(readFile(other) == "xfirst");
    CHECK(readFile(f.path) == "theirs");
    CHECK(f.tab.filePath() == other);
}

TEST_CASE(failed_save_keeps_the_text_and_the_modified_flag)
{
    Fixture f;
    f.type(QStringLiteral("x"));
    CHECK(f.tab.save(QStringLiteral("/nonexistent/dir/a.md"), f.error) == SaveResult::Failed);
    CHECK(!f.error.isEmpty());
    CHECK(f.tab.isModified());
    CHECK(f.tab.editor()->markdown() == QStringLiteral("xfirst"));
}

TEST_CASE(unmodified_tab_reloads_by_itself)
{
    Fixture f;
    QString message;
    QObject::connect(&f.tab, &EditorTab::statusMessage, &f.tab, [&](const QString &m) { message = m; });
    writeFile(f.path, "theirs");
    f.checkDisk();
    CHECK(f.tab.editor()->markdown() == QStringLiteral("theirs"));
    CHECK(!f.tab.isModified());
    CHECK(!f.barShown());
    CHECK(message.contains(QStringLiteral("note.md")));
}

TEST_CASE(modified_tab_asks_and_can_reload_with_undo)
{
    Fixture f;
    f.type(QStringLiteral("mine "));
    writeFile(f.path, "theirs");
    f.checkDisk();
    CHECK(f.barShown());
    CHECK(f.tab.editor()->markdown() == QStringLiteral("mine first"));

    f.clickBar(QStringLiteral("Reload"));
    CHECK(f.tab.editor()->markdown() == QStringLiteral("theirs"));
    CHECK(!f.tab.isModified());
    CHECK(!f.barShown());
    f.tab.editor()->undo();
    CHECK(f.tab.editor()->markdown() == QStringLiteral("mine first"));
}

TEST_CASE(keeping_my_version_lets_the_next_save_through)
{
    Fixture f;
    f.type(QStringLiteral("mine "));
    writeFile(f.path, "theirs");
    f.checkDisk();
    f.clickBar(QStringLiteral("Keep My Version"));
    CHECK(!f.barShown());
    CHECK(f.tab.save(f.path, f.error) == SaveResult::Saved);
    CHECK(readFile(f.path) == "mine first");

    // A newer change still counts.
    f.type(QStringLiteral("again "));
    writeFile(f.path, "theirs, later");
    CHECK(f.tab.save(f.path, f.error) == SaveResult::Conflict);
}

TEST_CASE(missing_file_shows_the_bar_and_keeps_the_text)
{
    Fixture f;
    QFile::remove(f.path);
    f.checkDisk();
    CHECK(f.barShown());
    CHECK(f.tab.editor()->markdown() == QStringLiteral("first"));
    f.clickBar(QStringLiteral("Keep My Version"));
    f.type(QStringLiteral("x"));
    CHECK(f.tab.save(f.path, f.error) == SaveResult::Saved);
    CHECK(readFile(f.path) == "xfirst");
}

TEST_CASE(a_file_that_comes_back_hides_the_bar)
{
    Fixture f;
    QFile::rename(f.path, f.path + QStringLiteral(".moved"));
    f.checkDisk();
    CHECK(f.barShown());
    QFile::rename(f.path + QStringLiteral(".moved"), f.path);
    f.checkDisk();
    CHECK(!f.barShown());
}

TEST_CASE(a_save_during_a_check_is_not_reported_as_a_change)
{
    Fixture f;
    bool finished = false;
    QObject::connect(&f.tab, &EditorTab::diskCheckFinished, &f.tab, [&] { finished = true; });
    f.type(QStringLiteral("x"));
    f.tab.checkDisk();
    CHECK(f.tab.save(f.path, f.error, true) == SaveResult::Saved); // spins the event loop itself
    QElapsedTimer timer;
    timer.start();
    while (!finished && timer.elapsed() < 2000)
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    CHECK(!f.barShown());
    CHECK(f.tab.editor()->markdown() == QStringLiteral("xfirst"));
}

TEST_CASE(the_window_stays_alive_during_slow_work)
{
    int ticks = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &timer, [&] { ++ticks; });
    timer.start(10);
    const auto result = runAndWait([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        return 42;
    }, QStringLiteral("Waiting"), nullptr);
    CHECK(result.has_value());
    CHECK(result.value_or(0) == 42);
    CHECK(ticks >= 5);
}

TEST_CASE(fast_work_shows_no_dialog)
{
    bool dialogSeen = false;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &timer, [&] {
        dialogSeen = dialogSeen || qobject_cast<QProgressDialog *>(QApplication::activeModalWidget());
    });
    timer.start(1);
    CHECK(runAndWait([] { return 1; }, QStringLiteral("Waiting"), nullptr).has_value());
    CHECK(!dialogSeen);
}

namespace {

void abandonHungWork(const std::function<void(QProgressDialog *)> &stop)
{
    auto release = std::make_shared<std::atomic<bool>>(false);
    QTimer press;
    QObject::connect(&press, &QTimer::timeout, &press, [&] {
        if (auto *dialog = qobject_cast<QProgressDialog *>(QApplication::activeModalWidget())) {
            press.stop();
            stop(dialog);
        }
    });
    press.start(20);
    QElapsedTimer elapsed;
    elapsed.start();
    const auto result = runAndWait([release] {
        while (!release->load())
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return 1;
    }, QStringLiteral("Waiting"), nullptr);
    CHECK(!result.has_value());
    CHECK(elapsed.elapsed() < 3000);
    release->store(true); // the shared state outlives the wait: this must be safe
}

} // namespace

TEST_CASE(a_hung_drive_can_be_abandoned_with_the_button)
{
    abandonHungWork([](QProgressDialog *dialog) { dialog->findChild<QPushButton *>()->click(); });
}

TEST_CASE(a_hung_drive_can_be_abandoned_with_escape)
{
    abandonHungWork([](QProgressDialog *dialog) { dialog->reject(); });
}

namespace {

QAction *saveAction(MainWindow &window)
{
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text() == QStringLiteral("&Save"))
            return action;
    }
    return nullptr;
}

// Answers the next message box by clicking the button with this text; returns the buttons it offered.
QStringList answerConflict(QAction *save, const QString &answer)
{
    QStringList offered;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &timer, [&] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box)
            return;
        timer.stop();
        for (QAbstractButton *button : box->buttons())
            offered << button->text();
        for (QAbstractButton *button : box->buttons()) {
            if (button->text() == answer)
                button->click();
        }
    });
    timer.start(10);
    save->trigger();
    return offered;
}

struct WindowFixture {
    QTemporaryDir dir;
    QString path = dir.filePath(QStringLiteral("note.md"));
    MainWindow window;
    QAction *save = nullptr;

    WindowFixture()
    {
        writeFile(path, "first");
        window.openFile(path);
        save = saveAction(window);
        CHECK(save != nullptr);
        window.findChild<EditorTab *>()->editor()->insertPlainText(QStringLiteral("mine "));
        writeFile(path, "theirs");
    }
    EditorTab *tab() { return window.findChild<EditorTab *>(); }
};

} // namespace

TEST_CASE(save_conflict_dialog_cancel_writes_nothing)
{
    WindowFixture f;
    const QStringList offered = answerConflict(f.save, QStringLiteral("Cancel"));
    CHECK(offered.contains(QStringLiteral("Overwrite")));
    CHECK(offered.contains(QStringLiteral("Discard Mine and Reload")));
    CHECK(readFile(f.path) == "theirs");
    CHECK(f.tab()->isModified());
}

TEST_CASE(save_conflict_dialog_overwrite_replaces_the_file)
{
    WindowFixture f;
    answerConflict(f.save, QStringLiteral("Overwrite"));
    CHECK(readFile(f.path) == "mine first");
    CHECK(!f.tab()->isModified());
}

TEST_CASE(save_conflict_dialog_reload_takes_the_file_on_disk)
{
    WindowFixture f;
    answerConflict(f.save, QStringLiteral("Discard Mine and Reload"));
    CHECK(readFile(f.path) == "theirs");
    CHECK(f.tab()->editor()->markdown() == QStringLiteral("theirs"));
    CHECK(!f.tab()->isModified());
}
