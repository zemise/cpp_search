#include "page_feedback.h"

#ifdef _WIN32

#include "search_ui_layout.h"

#include <commctrl.h>

#include <algorithm>

namespace search {
namespace {

constexpr COLORREF COLOR_ALERT_BACKGROUND = RGB(0xFF, 0xEA, 0xEA);
constexpr COLORREF COLOR_ALERT_TEXT = RGB(0xA8, 0x07, 0x1A);
constexpr COLORREF COLOR_ACTIVITY_BACKGROUND = RGB(0xFF, 0xFF, 0xFF);

void show_activity_controls(PageFeedback& feedback) {
    layout_page_feedback(feedback);
    const HWND controls[] = {
        feedback.activityPanel, feedback.activityLabel, feedback.progress};
    for (HWND control : controls) {
        if (!control) continue;
        SetWindowPos(control, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

}  // namespace

void initialize_page_feedback(PageFeedback& feedback, HWND owner, HWND status,
                              HWND anchor, HFONT font) {
    feedback.owner = owner;
    feedback.status = status;
    feedback.anchor = anchor;
    feedback.alertBrush = CreateSolidBrush(COLOR_ALERT_BACKGROUND);
    feedback.activityBrush = CreateSolidBrush(COLOR_ACTIVITY_BACKGROUND);

    if (status) {
        SetWindowLongPtrW(status, GWL_STYLE,
                          GetWindowLongPtrW(status, GWL_STYLE) | SS_NOTIFY);
    }
    feedback.activityPanel = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"STATIC", L"", WS_CHILD,
        0, 0, 0, 0, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    feedback.activityLabel = CreateWindowExW(
        0, L"STATIC", L"", WS_CHILD | SS_LEFT,
        0, 0, 0, 0, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    feedback.progress = CreateWindowExW(
        0, PROGRESS_CLASSW, L"", WS_CHILD | PBS_SMOOTH,
        0, 0, 0, 0, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    feedback.tooltip = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        owner, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (font) {
        if (feedback.activityLabel) SendMessageW(feedback.activityLabel, WM_SETFONT,
                                                 reinterpret_cast<WPARAM>(font), TRUE);
    }
    if (feedback.tooltip) {
        SetWindowPos(feedback.tooltip, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SendMessageW(feedback.tooltip, TTM_SETMAXTIPWIDTH, 0,
                     static_cast<LPARAM>(420 * dpi_scale_factor(owner)));
    }
    add_page_tooltip(feedback, feedback.progress,
                     L"显示当前查询正在执行；完成后会自动收起。");
    add_page_tooltip(feedback, status,
                     L"显示当前操作状态；红色错误提示可点击查看完整详情。");
}

void destroy_page_feedback(PageFeedback& feedback) {
    if (feedback.alertBrush) DeleteObject(feedback.alertBrush);
    if (feedback.activityBrush) DeleteObject(feedback.activityBrush);
    feedback.alertBrush = nullptr;
    feedback.activityBrush = nullptr;
}

void layout_page_feedback(PageFeedback& feedback) {
    if (!feedback.owner || !feedback.activityPanel) return;
    RECT area{};
    if (feedback.anchor && IsWindow(feedback.anchor)) {
        GetWindowRect(feedback.anchor, &area);
        MapWindowPoints(nullptr, feedback.owner, reinterpret_cast<POINT*>(&area), 2);
    } else {
        GetClientRect(feedback.owner, &area);
    }
    const float scale = dpi_scale_factor(feedback.owner);
    const int margin = static_cast<int>(12 * scale);
    const int availableWidth = (std::max)(0, static_cast<int>(area.right - area.left));
    const int cardWidth = (std::min)(static_cast<int>(400 * scale),
        (std::max)(static_cast<int>(260 * scale), availableWidth - margin * 2));
    const int cardHeight = static_cast<int>(96 * scale);
    const int x = area.left + (std::max)(margin, (availableWidth - cardWidth) / 2);
    const int availableHeight = (std::max)(
        cardHeight, static_cast<int>(area.bottom - area.top));
    const int y = area.top + (std::max)(margin, (availableHeight - cardHeight) / 2);
    const int contentX = x + static_cast<int>(24 * scale);
    const int contentWidth = (std::max)(static_cast<int>(160 * scale),
        cardWidth - static_cast<int>(48 * scale));

    MoveWindow(feedback.activityPanel, x, y, cardWidth, cardHeight, TRUE);
    MoveWindow(feedback.activityLabel, contentX, y + static_cast<int>(18 * scale),
               contentWidth, static_cast<int>(26 * scale), TRUE);
    MoveWindow(feedback.progress, contentX, y + static_cast<int>(55 * scale),
               contentWidth, static_cast<int>(16 * scale), TRUE);
}

void show_page_activity(PageFeedback& feedback, const std::wstring& text) {
    if (!feedback.owner) return;
    if (feedback.activityLabel) SetWindowTextW(feedback.activityLabel, text.c_str());
    if (feedback.progress) {
        LONG_PTR style = GetWindowLongPtrW(feedback.progress, GWL_STYLE);
        SetWindowLongPtrW(feedback.progress, GWL_STYLE, style | PBS_MARQUEE);
        SetWindowPos(feedback.progress, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        SendMessageW(feedback.progress, PBM_SETMARQUEE, TRUE, 35);
    }
    show_activity_controls(feedback);
}

void hide_page_activity(PageFeedback& feedback) {
    if (!feedback.owner) return;
    RECT dirty{};
    if (feedback.activityPanel) {
        GetWindowRect(feedback.activityPanel, &dirty);
        MapWindowPoints(nullptr, feedback.owner, reinterpret_cast<POINT*>(&dirty), 2);
    }
    HDWP deferred = BeginDeferWindowPos(3);
    const HWND controls[] = {
        feedback.progress, feedback.activityLabel, feedback.activityPanel};
    for (HWND control : controls) {
        if (!control || !deferred) continue;
        deferred = DeferWindowPos(
            deferred, control, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                SWP_HIDEWINDOW | SWP_NOREDRAW);
    }
    const bool hiddenAsBatch = deferred && EndDeferWindowPos(deferred);
    if (!hiddenAsBatch) {
        for (HWND control : controls) {
            if (control) ShowWindow(control, SW_HIDE);
        }
    }
    if (feedback.progress) SendMessageW(feedback.progress, PBM_SETMARQUEE, FALSE, 0);
    RedrawWindow(feedback.owner, IsRectEmpty(&dirty) ? nullptr : &dirty, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void set_page_status(PageFeedback& feedback, const std::wstring& text) {
    if (!feedback.status) return;
    feedback.statusIsAlert = false;
    feedback.lastAlertText.clear();
    SetWindowTextW(feedback.status, text.c_str());
    InvalidateRect(feedback.status, nullptr, TRUE);
}

void show_page_alert(PageFeedback& feedback, const std::wstring& text) {
    if (!feedback.status) return;
    feedback.statusIsAlert = true;
    feedback.lastAlertText = text;
    SetWindowTextW(feedback.status,
                   (L"  !  " + text + L"（点击查看详情）").c_str());
    InvalidateRect(feedback.status, nullptr, TRUE);
}

void add_page_tooltip(PageFeedback& feedback, HWND control, const wchar_t* text) {
    if (!feedback.tooltip || !control || !text) return;
    TTTOOLINFOW tool{};
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = GetParent(control);
    tool.uId = reinterpret_cast<UINT_PTR>(control);
    tool.lpszText = const_cast<wchar_t*>(text);
    SendMessageW(feedback.tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
}

bool handle_page_feedback_command(PageFeedback& feedback, LPARAM controlHandle,
                                  const wchar_t* title) {
    if (reinterpret_cast<HWND>(controlHandle) != feedback.status ||
        !feedback.statusIsAlert || feedback.lastAlertText.empty()) {
        return false;
    }
    MessageBoxW(feedback.owner, feedback.lastAlertText.c_str(), title, MB_ICONERROR);
    return true;
}

bool page_feedback_static_color(PageFeedback& feedback, HDC dc, HWND control,
                                LRESULT& result) {
    if (control == feedback.status && feedback.statusIsAlert) {
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, COLOR_ALERT_BACKGROUND);
        SetTextColor(dc, COLOR_ALERT_TEXT);
        result = reinterpret_cast<LRESULT>(feedback.alertBrush);
        return true;
    }
    if (control == feedback.activityPanel || control == feedback.activityLabel) {
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, COLOR_ACTIVITY_BACKGROUND);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        result = reinterpret_cast<LRESULT>(feedback.activityBrush);
        return true;
    }
    return false;
}

}  // namespace search

#endif
