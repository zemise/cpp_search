#include "window_task.h"

#ifdef _WIN32

#include <windows.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <exception>
#include <functional>
#include <optional>
#include <stdexcept>
#include <thread>

namespace {

bool pumpUntil(const std::function<bool()>& condition, DWORD timeoutMs) {
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    while (!condition() && GetTickCount64() < deadline) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (!app::dispatch_window_task_message(message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        Sleep(1);
    }
    return condition();
}

}  // namespace

int main() {
    // Ensure this thread owns a Win32 message queue before workers can post back.
    MSG message{};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    {
        app::WindowTask task;
        int delivered = 0;
        assert(task.start<int>(
            []() -> int { throw std::runtime_error("expected"); },
            [&](std::optional<int> result, std::exception_ptr error) {
                assert(!result);
                assert(error);
                ++delivered;
            }));
        assert(pumpUntil([&] { return delivered == 1; }, 3000));
        assert(!task.active());
    }

    {
        app::WindowTask task;
        int delivered = 0;
        assert(task.start<int>(
            [] { return 42; },
            [&](std::optional<int> result, std::exception_ptr error) {
                assert(!error);
                assert(result && *result == 42);
                ++delivered;
            }));
        assert(pumpUntil([&] { return delivered == 1; }, 3000));
        assert(!task.active());
    }

    {
        app::WindowTask task;
        std::atomic<bool> releaseOld{false};
        int delivered = 0;
        int value = 0;
        assert(task.start<int>(
            [&] {
                while (!releaseOld.load()) Sleep(1);
                return 1;
            },
            [&](std::optional<int> result, std::exception_ptr) {
                ++delivered;
                value = result.value_or(-1);
            }));
        assert(task.start<int>(
            [] { return 2; },
            [&](std::optional<int> result, std::exception_ptr) {
                ++delivered;
                value = result.value_or(-1);
            }));
        assert(pumpUntil([&] { return delivered == 1; }, 3000));
        assert(value == 2);
        releaseOld.store(true);
        pumpUntil([&] { return false; }, 100);
        assert(delivered == 1);
    }

    {
        app::WindowTask task;
        std::atomic<bool> release{false};
        int delivered = 0;
        assert(task.start<int>(
            [&] {
                while (!release.load()) Sleep(1);
                return 7;
            },
            [&](std::optional<int>, std::exception_ptr) { ++delivered; }));
        task.cancel();
        release.store(true);
        pumpUntil([&] { return false; }, 100);
        assert(delivered == 0);
    }

    return 0;
}

#endif
