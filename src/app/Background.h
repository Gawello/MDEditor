#pragma once

#include <QFuture>
#include <QPromise>
#include <QString>

#include <functional>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

class QWidget;

// Runs `work` on its own thread. Not the thread pool: a hung network drive would block the thread
// forever, and it must be able to outlive whoever stopped waiting for it, so `work` may share no state.
template <typename Work>
auto runInBackground(Work work)
{
    using Result = std::invoke_result_t<Work>;
    QPromise<Result> promise;
    QFuture<Result> future = promise.future();
    std::thread([promise = std::move(promise), work = std::move(work)]() mutable {
        promise.start();
        promise.addResult(work());
        promise.finish();
    }).detach();
    return future;
}

// Keeps the window painting until `finished()` returns true. User input is held back at first, so a fast
// operation looks instant and nothing can change under it; after a moment a modal dialog says what's
// going on and offers to stop waiting. Returns false if the user stopped waiting.
bool waitWithProgress(const std::function<bool()> &finished, const QString &label, QWidget *parent);

// Runs `work` in the background and waits for it as above. Empty if the user stopped waiting; the work
// itself can't be stopped and may still finish later.
template <typename Work>
auto runAndWait(Work work, const QString &label, QWidget *parent) -> std::optional<std::invoke_result_t<Work>>
{
    auto future = runInBackground(std::move(work));
    if (!waitWithProgress([future] { return future.isFinished(); }, label, parent))
        return std::nullopt;
    return future.result();
}
