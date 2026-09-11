#pragma once

#ifdef _WIN32

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace app {

namespace window_task_detail {

struct State {
    std::atomic<std::uint64_t> generation{0};
    std::atomic<bool> active{false};
};

class Completion {
public:
    virtual ~Completion() = default;
    virtual void deliver() noexcept = 0;
};

class Work {
public:
    virtual ~Work() = default;
    virtual void execute() noexcept = 0;
};

bool queue(std::unique_ptr<Work> work) noexcept;
bool post(DWORD uiThreadId, std::unique_ptr<Completion> completion) noexcept;

template <typename Result, typename Handler>
class CompletionModel final : public Completion {
public:
    CompletionModel(std::weak_ptr<State> state,
                    std::uint64_t generation,
                    std::optional<Result> result,
                    std::exception_ptr error,
                    Handler handler)
        : state_(std::move(state)),
          generation_(generation),
          result_(std::move(result)),
          error_(std::move(error)),
          handler_(std::move(handler)) {}

    void deliver() noexcept override {
        auto state = state_.lock();
        if (!state || !state->active.load(std::memory_order_acquire) ||
            state->generation.load(std::memory_order_acquire) != generation_) {
            return;
        }

        state->active.store(false, std::memory_order_release);
        try {
            handler_(std::move(result_), error_);
        } catch (...) {
            // Exceptions must not escape the Win32 message loop.
        }
    }

private:
    std::weak_ptr<State> state_;
    std::uint64_t generation_ = 0;
    std::optional<Result> result_;
    std::exception_ptr error_;
    Handler handler_;
};

template <typename Result, typename Worker, typename Handler>
class WorkModel final : public Work {
public:
    WorkModel(std::weak_ptr<State> state,
              std::uint64_t generation,
              DWORD uiThreadId,
              Worker worker,
              Handler handler)
        : state_(std::move(state)),
          generation_(generation),
          uiThreadId_(uiThreadId),
          worker_(std::move(worker)),
          handler_(std::move(handler)) {}

    void execute() noexcept override {
        std::optional<Result> result;
        std::exception_ptr error;
        try {
            result.emplace(worker_());
        } catch (...) {
            error = std::current_exception();
        }

        try {
            auto completion = std::make_unique<CompletionModel<Result, Handler>>(
                state_, generation_, std::move(result), std::move(error), std::move(handler_));
            if (!post(uiThreadId_, std::move(completion))) {
                abandon();
            }
        } catch (...) {
            abandon();
        }
    }

private:
    void abandon() noexcept {
        auto state = state_.lock();
        if (state && state->generation.load(std::memory_order_acquire) == generation_) {
            state->active.store(false, std::memory_order_release);
        }
    }

    std::weak_ptr<State> state_;
    std::uint64_t generation_ = 0;
    DWORD uiThreadId_ = 0;
    Worker worker_;
    Handler handler_;
};

}  // namespace window_task_detail

// Own one replaceable background operation for a Win32 UI object. Completion is
// delivered on the thread that calls start(), and cancel() never blocks that thread.
class WindowTask {
public:
    WindowTask();
    ~WindowTask();

    WindowTask(const WindowTask&) = delete;
    WindowTask& operator=(const WindowTask&) = delete;
    WindowTask(WindowTask&&) = delete;
    WindowTask& operator=(WindowTask&&) = delete;

    template <typename Result, typename Worker, typename Handler>
    bool start(Worker&& worker, Handler&& handler) {
        static_assert(!std::is_void_v<Result>, "WindowTask requires an owned result value");

        const auto generation =
            state_->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
        state_->active.store(true, std::memory_order_release);

        using WorkType = std::decay_t<Worker>;
        using HandlerType = std::decay_t<Handler>;
        try {
            auto work = std::make_unique<
                window_task_detail::WorkModel<Result, WorkType, HandlerType>>(
                state_, generation, GetCurrentThreadId(),
                std::forward<Worker>(worker), std::forward<Handler>(handler));
            if (window_task_detail::queue(std::move(work))) {
                return true;
            }
        } catch (...) {
        }

        cancel();
        return false;
    }

    void cancel() noexcept;
    bool active() const noexcept;

private:
    std::shared_ptr<window_task_detail::State> state_;
};

// Call before normal TranslateMessage/DispatchMessage handling.
bool dispatch_window_task_message(const MSG& message) noexcept;

}  // namespace app

#endif
