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

constexpr const wchar_t* DISPATCHER_CLASS = L"LISWorkbenchWindowTaskDispatcher";

LRESULT CALLBACK dispatcherProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == completionMessage()) {
        std::unique_ptr<Completion> completion(reinterpret_cast<Completion*>(lParam));
        if (completion) completion->deliver();
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

ATOM dispatcherClass() noexcept {
    static const ATOM atom = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = dispatcherProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = DISPATCHER_CLASS;
        return RegisterClassExW(&wc);
    }();
    return atom;
}

DWORD WINAPI runWork(void* parameter) noexcept {
    std::unique_ptr<Work> work(static_cast<Work*>(parameter));
    work->execute();
    return 0;
}

class CallbackCompletion final : public Completion {
public:
    CallbackCompletion(std::weak_ptr<State> state,
                       std::uint64_t generation,
                       std::function<void()> callback)
        : state_(std::move(state)),
          generation_(generation),
          callback_(std::move(callback)) {}

    void deliver() noexcept override {
        auto state = state_.lock();
        if (!state || !state->active.load(std::memory_order_acquire) ||
            state->generation.load(std::memory_order_acquire) != generation_) {
            return;
        }
        try {
            callback_();
        } catch (...) {
            // Progress callbacks must not escape the Win32 message loop.
        }
    }

private:
    std::weak_ptr<State> state_;
    std::uint64_t generation_ = 0;
    std::function<void()> callback_;
};

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

HWND dispatcher() noexcept {
    thread_local HWND window = nullptr;
    if (window && IsWindow(window)) return window;
    if (!dispatcherClass() || completionMessage() == 0) return nullptr;

    window = CreateWindowExW(0, DISPATCHER_CLASS, L"", 0,
                             0, 0, 0, 0, HWND_MESSAGE, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    return window;
}

bool post(HWND dispatcherWindow, std::unique_ptr<Completion> completion) noexcept {
    if (!dispatcherWindow || !completion || completionMessage() == 0) return false;
    Completion* raw = completion.release();
    if (PostMessageW(dispatcherWindow, completionMessage(), 0,
                     reinterpret_cast<LPARAM>(raw))) {
        return true;
    }
    delete raw;
    return false;
}

bool postCallback(std::weak_ptr<State> state,
                  std::uint64_t generation,
                  HWND dispatcherWindow,
                  std::function<void()> callback) noexcept {
    if (!callback) return false;
    try {
        return post(dispatcherWindow, std::make_unique<CallbackCompletion>(
            std::move(state), generation, std::move(callback)));
    } catch (...) {
        return false;
    }
}

}  // namespace window_task_detail

bool WindowTaskContext::cancelled() const noexcept {
    auto state = state_.lock();
    return !state || !state->active.load(std::memory_order_acquire) ||
           state->generation.load(std::memory_order_acquire) != generation_;
}

bool WindowTaskContext::post(std::function<void()> callback) const noexcept {
    if (cancelled()) return false;
    return window_task_detail::postCallback(
        state_, generation_, dispatcherWindow_, std::move(callback));
}

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

}  // namespace app

#endif
