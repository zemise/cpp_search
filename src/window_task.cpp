#include "window_task.h"

#ifdef _WIN32

#include <memory>

namespace app {
namespace window_task_detail {
namespace {

UINT completionMessage() noexcept {
    static const UINT message = RegisterWindowMessageW(L"LISWorkbench.WindowTask.Completion.v1");
    return message;
}

DWORD WINAPI runWork(void* parameter) noexcept {
    std::unique_ptr<Work> work(static_cast<Work*>(parameter));
    work->execute();
    return 0;
}

}  // namespace

bool queue(std::unique_ptr<Work> work) noexcept {
    if (!work) return false;
    Work* raw = work.release();
    if (QueueUserWorkItem(runWork, raw, WT_EXECUTELONGFUNCTION)) {
        return true;
    }
    delete raw;
    return false;
}

bool post(DWORD uiThreadId, std::unique_ptr<Completion> completion) noexcept {
    if (!completion || completionMessage() == 0) return false;
    Completion* raw = completion.release();
    if (PostThreadMessageW(uiThreadId, completionMessage(), 0,
                           reinterpret_cast<LPARAM>(raw))) {
        return true;
    }
    delete raw;
    return false;
}

}  // namespace window_task_detail

WindowTask::WindowTask()
    : state_(std::make_shared<window_task_detail::State>()) {}

WindowTask::~WindowTask() {
    cancel();
}

void WindowTask::cancel() noexcept {
    state_->active.store(false, std::memory_order_release);
    state_->generation.fetch_add(1, std::memory_order_acq_rel);
}

bool WindowTask::active() const noexcept {
    return state_->active.load(std::memory_order_acquire);
}

bool dispatch_window_task_message(const MSG& message) noexcept {
    if (message.hwnd != nullptr ||
        message.message != window_task_detail::completionMessage()) {
        return false;
    }

    std::unique_ptr<window_task_detail::Completion> completion(
        reinterpret_cast<window_task_detail::Completion*>(message.lParam));
    if (completion) completion->deliver();
    return true;
}

}  // namespace app

#endif
