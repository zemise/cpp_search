#pragma once

#ifdef _WIN32

#include <windows.h>

#include <string>

namespace search {

// Shared, non-blocking feedback used by query-oriented Win32 pages.
struct PageFeedback {
    HWND owner = nullptr;
    HWND status = nullptr;
    HWND anchor = nullptr;
    HWND activityPanel = nullptr;
    HWND activityLabel = nullptr;
    HWND progress = nullptr;
    HWND tooltip = nullptr;
    HBRUSH alertBrush = nullptr;
    HBRUSH activityBrush = nullptr;
    bool statusIsAlert = false;
    std::wstring lastAlertText;
};

void initialize_page_feedback(PageFeedback& feedback, HWND owner, HWND status,
                              HWND anchor, HFONT font);
void destroy_page_feedback(PageFeedback& feedback);
void layout_page_feedback(PageFeedback& feedback);
void show_page_activity(PageFeedback& feedback, const std::wstring& text);
void hide_page_activity(PageFeedback& feedback);
void set_page_status(PageFeedback& feedback, const std::wstring& text);
void show_page_alert(PageFeedback& feedback, const std::wstring& text);
void add_page_tooltip(PageFeedback& feedback, HWND control, const wchar_t* text);
bool handle_page_feedback_command(PageFeedback& feedback, LPARAM controlHandle,
                                  const wchar_t* title);
bool page_feedback_static_color(PageFeedback& feedback, HDC dc, HWND control,
                                LRESULT& result);

}  // namespace search

#endif
