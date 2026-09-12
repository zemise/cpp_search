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
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    }
    return condition();
}

}  // namespace

int main() {
    {
        app::WindowTask task;
        int progress = 0;
        int delivered = 0;
        assert(task.start<int>(
            [&progress](app::WindowTaskContext context) {
                assert(!context.cancelled());
                assert(context.post([] { /* Proves worker-to-UI delivery. */ }));
                assert(context.post([&progress] { progress = 3; }));
                return 9;
            },
            [&](std::optional<int> result, std::exception_ptr error) {
                assert(!error);
                assert(result && *result == 9);
                ++delivered;
            }));
        assert(pumpUntil([&] { return delivered == 1; }, 3000));
        assert(progress == 3);
    }

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
        std::atomic<bool> cancellationObserved{false};
        int delivered = 0;
        assert(task.start<int>(
            [&](app::WindowTaskContext context) {
                while (!release.load()) Sleep(1);
                cancellationObserved.store(context.cancelled());
                return 7;
            },
            [&](std::optional<int>, std::exception_ptr) { ++delivered; }));
        task.cancel();
        release.store(true);
        assert(pumpUntil([&] { return cancellationObserved.load(); }, 3000));
        pumpUntil([&] { return false; }, 100);
        assert(delivered == 0);
    }

    return 0;
}

#endif
