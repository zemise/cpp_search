#include "regular_report_state.h"

#ifdef _WIN32

#include "app_settings_io.h"
#include "resource.h"
#include "search_text.h"

#include <windows.h>
#include <commctrl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// ============================================================================
// Picture view (inline in middle panel)
// ============================================================================

void regularClearPictureView(RegularReportState* st, const std::wstring& status) {
    if (!st) return;
    // Release current image
    delete st->pictureImage;
    st->pictureImage = nullptr;
    if (st->pictureStream) {
        st->pictureStream->Release();
        st->pictureStream = nullptr;
    }
    st->pictureStatus = status;
    if (IsWindow(st->pictureView)) InvalidateRect(st->pictureView, nullptr, TRUE);
}

namespace {

bool ensureGdiplus(RegularReportState* st) {
    if (!st) return false;
    if (st->gdiplusReady) return true;
    Gdiplus::GdiplusStartupInput input;
    st->gdiplusReady =
        Gdiplus::GdiplusStartup(&st->gdiplusToken, &input, nullptr) == Gdiplus::Ok;
    return st->gdiplusReady;
}

bool createImageFromBytes(const std::vector<unsigned char>& bytes,
                          IStream*& stream, Gdiplus::Image*& image,
                          std::wstring& error) {
    stream = nullptr;
    image = nullptr;
    if (bytes.empty()) return true;

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!memory) { error = L"图像内存分配失败"; return false; }
    void* data = GlobalLock(memory);
    if (!data) { GlobalFree(memory); error = L"图像内存锁定失败"; return false; }
    std::memcpy(data, bytes.data(), bytes.size());
    GlobalUnlock(memory);

    if (CreateStreamOnHGlobal(memory, TRUE, &stream) != S_OK || !stream) {
        GlobalFree(memory);
        error = L"图像流创建失败";
        return false;
    }
    image = Gdiplus::Image::FromStream(stream, FALSE);
    if (!image || image->GetLastStatus() != Gdiplus::Ok ||
        image->GetWidth() == 0 || image->GetHeight() == 0) {
        delete image; image = nullptr;
        stream->Release(); stream = nullptr;
        error = L"图像解码失败";
        return false;
    }
    return true;
}

bool loadPictureView(RegularReportState* st, const std::vector<unsigned char>& bytes,
                     std::wstring& error) {
    if (!st) return false;
    regularClearPictureView(st, L"");
    if (bytes.empty()) return true;
    if (!ensureGdiplus(st)) {
        error = L"GDI+ 初始化失败";
        st->pictureStatus = error;
        if (IsWindow(st->pictureView)) InvalidateRect(st->pictureView, nullptr, TRUE);
        return false;
    }

    IStream* stream = nullptr;
    Gdiplus::Image* image = nullptr;
    if (!createImageFromBytes(bytes, stream, image, error)) {
        st->pictureStatus = error;
        if (IsWindow(st->pictureView)) InvalidateRect(st->pictureView, nullptr, TRUE);
        return false;
    }
    st->pictureStream = stream;
    st->pictureImage = image;
    st->pictureStatus.clear();
    if (IsWindow(st->pictureView)) InvalidateRect(st->pictureView, nullptr, TRUE);
    return true;
}

void finishPictureQuery(RegularReportState* st, HWND hwnd,
                         std::unique_ptr<PictureLoadResult> result) {
    if (!st || !result || result->generation != st->pictureQueryGeneration) return;
    st->pictureQueryLoading = false;
    if (!result->ok) {
        regularClearPictureView(st, L"图像查询失败：" + search::utf8_to_wide(result->error));
        return;
    }
    std::wstring error;
    if (!loadPictureView(st, result->picture, error) && !error.empty()) {
        regularClearPictureView(st, error);
    }
    (void)hwnd;
}

// ============================================================================
// Picture popup helpers
// ============================================================================

void releasePicturePopupImage(PicturePopupState* popup) {
    if (!popup) return;
    delete popup->image;
    popup->image = nullptr;
    if (popup->stream) {
        popup->stream->Release();
        popup->stream = nullptr;
    }
}

bool ensurePopupGdiplus(PicturePopupState* popup) {
    if (!popup) return false;
    if (popup->gdiplusReady) return true;
    Gdiplus::GdiplusStartupInput input;
    popup->gdiplusReady =
        Gdiplus::GdiplusStartup(&popup->gdiplusToken, &input, nullptr) == Gdiplus::Ok;
    return popup->gdiplusReady;
}

bool loadPicturePopupImage(PicturePopupState* popup,
                           const std::vector<unsigned char>& bytes,
                           std::wstring& error) {
    if (!popup) return false;
    releasePicturePopupImage(popup);
    if (bytes.empty()) { popup->status.clear(); return true; }
    if (!ensurePopupGdiplus(popup)) {
        error = L"GDI+ 初始化失败";
        popup->status = error;
        return false;
    }
    if (!createImageFromBytes(bytes, popup->stream, popup->image, error)) {
        popup->status = error;
        return false;
    }
    popup->status.clear();
    return true;
}

bool clonePicturePopupImage(PicturePopupState* popup, const RegularReportState* st,
                            std::wstring& error) {
    if (!popup || !st || !st->pictureImage) return false;
    if (!ensurePopupGdiplus(popup)) {
        error = L"GDI+ 初始化失败";
        return false;
    }
    Gdiplus::Image* cloned = st->pictureImage->Clone();
    if (!cloned || cloned->GetLastStatus() != Gdiplus::Ok) {
        delete cloned;
        error = L"图像复制失败";
        return false;
    }
    releasePicturePopupImage(popup);
    popup->image = cloned;
    popup->status.clear();
    return true;
}

void paintPicturePopup(HWND hwnd, PicturePopupState* popup, HDC dc) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    FillRect(dc, &rc, GetSysColorBrush(COLOR_WINDOW));
    if (!popup) return;

    if (popup->image) {
        const double imageW = static_cast<double>(popup->image->GetWidth());
        const double imageH = static_cast<double>(popup->image->GetHeight());
        const int clientW = std::max(1, static_cast<int>(rc.right - rc.left));
        const int clientH = std::max(1, static_cast<int>(rc.bottom - rc.top));
        const double scale = std::min(clientW / imageW, clientH / imageH);
        const int drawW = std::max(1, static_cast<int>(imageW * scale));
        const int drawH = std::max(1, static_cast<int>(imageH * scale));
        Gdiplus::Graphics graphics(dc);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.DrawImage(popup->image, rc.left, rc.top, drawW, drawH);
        return;
    }
    if (!popup->status.empty() || popup->loading) {
        const std::wstring text = popup->status.empty() ? L"正在加载图像..." : popup->status;
        HGDIOBJ oldFont = popup->font ? SelectObject(dc, popup->font) : nullptr;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(80, 80, 80));
        RECT textRc = rc;
        InflateRect(&textRc, -12, -12);
        DrawTextW(dc, text.c_str(), -1, &textRc, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        if (oldFont) SelectObject(dc, oldFont);
    }
}

void savePicturePopupSize(HWND hwnd) {
    if (!hwnd || IsIconic(hwnd)) return;
    RECT rc{};
    if (!GetWindowRect(hwnd, &rc)) return;
    const int w = static_cast<int>(rc.right - rc.left);
    const int h = static_cast<int>(rc.bottom - rc.top);
    if (w >= REGULAR_PICTURE_POPUP_MIN_W && h >= REGULAR_PICTURE_POPUP_MIN_H) {
        search::save_module_int(L"RegularReport", L"PicturePopupWidth", w);
        search::save_module_int(L"RegularReport", L"PicturePopupHeight", h);
    }
}

std::wstring trimWide(std::wstring value) {
    auto notSpace = [](wchar_t ch) { return std::iswspace(ch) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::wstring reportListCellText(HWND list, int row, int col) {
    if (!list || row < 0 || col < 0) return L"";
    wchar_t text[256]{};
    ListView_GetItemText(list, row, col, text, static_cast<int>(std::size(text)));
    return trimWide(text);
}

std::wstring picturePopupTitle(const RegularReportState* st, int selected) {
    std::wstring sampleNo = reportListCellText(st ? st->reportList : nullptr, selected, 1);
    std::wstring patientName = reportListCellText(st ? st->reportList : nullptr, selected, 2);
    if (sampleNo.empty() && st && selected >= 0 &&
        selected < static_cast<int>(st->reportRows.size())) {
        sampleNo = search::utf8_to_wide(
            search::trim(st->reportRows[static_cast<size_t>(selected)].oper_no));
    }
    if (patientName.empty() && st && selected >= 0 &&
        selected < static_cast<int>(st->reportRows.size())) {
        patientName = search::utf8_to_wide(
            search::trim(st->reportRows[static_cast<size_t>(selected)].name));
    }
    std::wstring title = L"结果图";
    if (!sampleNo.empty() || !patientName.empty()) {
        title += L" - ";
        title += sampleNo;
        if (!sampleNo.empty() && !patientName.empty()) title += L" ";
        title += patientName;
    }
    return title;
}

void startPicturePopupQuery(PicturePopupState* popup, const std::string& connection,
                            const std::string& repNo) {
    if (!popup || !popup->hwnd) return;
    popup->loading = true;
    popup->status = L"正在加载图像...";
    releasePicturePopupImage(popup);
    InvalidateRect(popup->hwnd, nullptr, FALSE);
    const HWND hwnd = popup->hwnd;
    const int generation = ++popup->generation;
    const bool queued = popup->pictureTask.start<PicturePopupLoadResult>(
        [connection, repNo, generation] {
            PicturePopupLoadResult result;
            result.generation = generation;
            result.ok = search::query_report_picture(
                connection, repNo, result.picture, result.error);
            return result;
        },
        [hwnd](std::optional<PicturePopupLoadResult> result, std::exception_ptr error) {
            auto* state = reinterpret_cast<PicturePopupState*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!state) return;
            state->loading = false;
            if (error || !result) {
                releasePicturePopupImage(state);
                state->status = L"图像后台任务异常";
            } else if (!result->ok) {
                releasePicturePopupImage(state);
                state->status = L"图像查询失败：" + search::utf8_to_wide(result->error);
            } else {
                std::wstring loadError;
                if (!loadPicturePopupImage(state, result->picture, loadError) &&
                    !loadError.empty()) {
                    state->status = loadError;
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        });
    if (!queued) {
        popup->loading = false;
        popup->status = L"无法启动图像后台任务";
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

void clearPicturePopup(PicturePopupState* popup, const std::wstring& title,
                       const std::wstring& status) {
    if (!popup || !popup->hwnd) return;
    popup->pictureTask.cancel();
    ++popup->generation;
    popup->repNo.clear();
    popup->loading = false;
    releasePicturePopupImage(popup);
    popup->status = status;
    SetWindowTextW(popup->hwnd, title.c_str());
    InvalidateRect(popup->hwnd, nullptr, FALSE);
}

void refreshPicturePopupForSelection(RegularReportState* st, int selected) {
    if (!st || !IsWindow(st->picturePopup)) return;
    auto* popup = reinterpret_cast<PicturePopupState*>(
        GetWindowLongPtrW(st->picturePopup, GWLP_USERDATA));
    if (!popup) return;

    const std::wstring title = picturePopupTitle(st, selected);
    SetWindowTextW(st->picturePopup, title.c_str());

    if (selected < 0 || selected >= static_cast<int>(st->reportRows.size())) {
        clearPicturePopup(popup, L"结果图", L"请先选择一条报告记录。");
        return;
    }
    const std::string repNo =
        search::trim(st->reportRows[static_cast<size_t>(selected)].rep_no);
    if (repNo.empty()) {
        clearPicturePopup(popup, title, L"当前报告没有验单号，无法查询图像。");
        return;
    }
    if (popup->repNo == repNo && (popup->loading || popup->image || !popup->status.empty()))
        return;

    std::string connection = st->reportConnectionString;
    if (connection.empty()) {
        connection = search::wide_to_utf8(search::build_connection_string_w(st->ctx.dbSettings));
    }
    if (connection.empty()) {
        clearPicturePopup(popup, title, L"请先在“设置”中填写数据库连接信息。");
        return;
    }
    popup->repNo = repNo;
    std::wstring cloneError;
    if (st->pictureImage && st->pictureRepNo == repNo &&
        clonePicturePopupImage(popup, st, cloneError)) {
        popup->loading = false;
        InvalidateRect(st->picturePopup, nullptr, FALSE);
        return;
    }
    startPicturePopupQuery(popup, connection, repNo);
}

// ============================================================================
// Picture popup window proc
// ============================================================================

LRESULT CALLBACK picturePopupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* popup = reinterpret_cast<PicturePopupState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            popup = reinterpret_cast<PicturePopupState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(popup));
            if (popup) popup->hwnd = hwnd;
            return TRUE;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            const int w = std::max(1, static_cast<int>(rc.right - rc.left));
            const int h = std::max(1, static_cast<int>(rc.bottom - rc.top));
            HDC memDc = CreateCompatibleDC(dc);
            HBITMAP memBmp = memDc ? CreateCompatibleBitmap(dc, w, h) : nullptr;
            if (memDc && memBmp) {
                HGDIOBJ oldBmp = SelectObject(memDc, memBmp);
                paintPicturePopup(hwnd, popup, memDc);
                BitBlt(dc, 0, 0, w, h, memDc, 0, 0, SRCCOPY);
                SelectObject(memDc, oldBmp);
                DeleteObject(memBmp);
                DeleteDC(memDc);
            } else {
                paintPicturePopup(hwnd, popup, dc);
                if (memBmp) DeleteObject(memBmp);
                if (memDc) DeleteDC(memDc);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SIZE:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_EXITSIZEMOVE:
            savePicturePopupSize(hwnd);
            return 0;
        case WM_CLOSE:
            savePicturePopupSize(hwnd);
            DestroyWindow(hwnd);
            return 0;
        case WM_NCDESTROY:
            if (popup) {
                popup->pictureTask.cancel();
                if (popup->owner && popup->owner->picturePopup == hwnd)
                    popup->owner->picturePopup = nullptr;
                ++popup->generation;
                releasePicturePopupImage(popup);
                if (popup->gdiplusReady) {
                    Gdiplus::GdiplusShutdown(popup->gdiplusToken);
                    popup->gdiplusReady = false;
                    popup->gdiplusToken = 0;
                }
                delete popup;
            }
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void registerPicturePopupClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = picturePopupProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP),
                                               IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = REGULAR_REPORT_PICTURE_POPUP_CLASS;
    const ATOM atom = RegisterClassExW(&wc);
    registered = atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

}  // namespace

// ============================================================================
// Picture viewport (scrollable area in middle panel)
// ============================================================================

void regularUpdatePictureViewport(RegularReportState* st) {
    if (!st || !st->pictureViewport || !st->pictureView) return;
    RECT rc{};
    GetClientRect(st->pictureViewport, &rc);
    const int pageW = std::max(1, static_cast<int>(rc.right - rc.left));
    const int pageH = std::max(1, static_cast<int>(rc.bottom - rc.top));
    const int contentW = regularS(st->pictureViewport, REGULAR_PICTURE_FIXED_W);
    const int contentH = regularS(st->pictureViewport, REGULAR_PICTURE_FIXED_H);
    st->pictureScrollX = std::clamp(st->pictureScrollX, 0, std::max(0, contentW - pageW));
    st->pictureScrollY = std::clamp(st->pictureScrollY, 0, std::max(0, contentH - pageH));
    MoveWindow(st->pictureView, -st->pictureScrollX, -st->pictureScrollY,
               contentW, contentH, TRUE);

    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = std::max(0, contentW - 1);
    si.nPage = static_cast<UINT>(pageW);
    si.nPos = st->pictureScrollX;
    if (st->pictureHScroll) SetScrollInfo(st->pictureHScroll, SB_CTL, &si, TRUE);
    si.nMax = std::max(0, contentH - 1);
    si.nPage = static_cast<UINT>(pageH);
    si.nPos = st->pictureScrollY;
    if (st->pictureVScroll) SetScrollInfo(st->pictureVScroll, SB_CTL, &si, TRUE);

    const bool showPicturePage = st->middleTab && TabCtrl_GetCurSel(st->middleTab) == 1 &&
                                 IsWindowVisible(st->pictureViewport);
    if (st->pictureHScroll)
        ShowWindow(st->pictureHScroll, showPicturePage && contentW > pageW ? SW_SHOW : SW_HIDE);
    if (st->pictureVScroll)
        ShowWindow(st->pictureVScroll, showPicturePage && contentH > pageH ? SW_SHOW : SW_HIDE);
}

void regularScrollPictureViewport(RegularReportState* st, int targetX, int targetY) {
    if (!st || !st->pictureViewport) return;
    st->pictureScrollX = targetX;
    st->pictureScrollY = targetY;
    regularUpdatePictureViewport(st);
}

// ============================================================================
// Public entry: picture view subclass proc
// ============================================================================

LRESULT CALLBACK pictureViewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                 UINT_PTR subclassId, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(dc, &rc, GetSysColorBrush(COLOR_WINDOW));
            if (st && st->pictureImage) {
                const int availW = regularS(hwnd, REGULAR_PICTURE_FIXED_W);
                const int availH = regularS(hwnd, REGULAR_PICTURE_FIXED_H);
                const double imageW = static_cast<double>(st->pictureImage->GetWidth());
                const double imageH = static_cast<double>(st->pictureImage->GetHeight());
                const double scale = std::min(availW / imageW, availH / imageH);
                const int drawW = std::max(1, static_cast<int>(imageW * scale));
                const int drawH = std::max(1, static_cast<int>(imageH * scale));
                Gdiplus::Graphics graphics(dc);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                graphics.DrawImage(st->pictureImage, rc.left, rc.top, drawW, drawH);
            } else if (st && !st->pictureStatus.empty()) {
                HGDIOBJ oldFont = st->ctx.uiFont ? SelectObject(dc, st->ctx.uiFont) : nullptr;
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(80, 80, 80));
                RECT textRc = rc;
                InflateRect(&textRc, -regularS(hwnd, 8), -regularS(hwnd, 8));
                DrawTextW(dc, st->pictureStatus.c_str(), -1, &textRc,
                          DT_CENTER | DT_VCENTER | DT_WORDBREAK);
                if (oldFont) SelectObject(dc, oldFont);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEWHEEL:
            if (st) {
                const int delta = GET_WHEEL_DELTA_WPARAM(wp);
                regularScrollPictureViewport(st, st->pictureScrollX,
                    st->pictureScrollY - (delta / WHEEL_DELTA) *
                                          regularS(hwnd, REGULAR_LEFT_SCROLL_STEP * 3));
                return 0;
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, pictureViewProc, subclassId);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// ============================================================================
// Public entry: picture viewport subclass proc
// ============================================================================

LRESULT CALLBACK pictureViewportProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                     UINT_PTR subclassId, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_SIZE:
            regularUpdatePictureViewport(st);
            return 0;
        case WM_HSCROLL: {
            if (!st) break;
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_HORZ, &si);
            int target = st->pictureScrollX;
            switch (LOWORD(wp)) {
                case SB_LINELEFT:  target -= regularS(hwnd, REGULAR_LEFT_SCROLL_STEP); break;
                case SB_LINERIGHT: target += regularS(hwnd, REGULAR_LEFT_SCROLL_STEP); break;
                case SB_PAGELEFT:  target -= static_cast<int>(si.nPage); break;
                case SB_PAGERIGHT: target += static_cast<int>(si.nPage); break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: target = si.nTrackPos; break;
                case SB_LEFT:  target = 0; break;
                case SB_RIGHT: target = si.nMax; break;
                default: return 0;
            }
            regularScrollPictureViewport(st, target, st->pictureScrollY);
            return 0;
        }
        case WM_VSCROLL: {
            if (!st) break;
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            int target = st->pictureScrollY;
            switch (LOWORD(wp)) {
                case SB_LINEUP:   target -= regularS(hwnd, REGULAR_LEFT_SCROLL_STEP); break;
                case SB_LINEDOWN: target += regularS(hwnd, REGULAR_LEFT_SCROLL_STEP); break;
                case SB_PAGEUP:   target -= static_cast<int>(si.nPage); break;
                case SB_PAGEDOWN: target += static_cast<int>(si.nPage); break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: target = si.nTrackPos; break;
                case SB_TOP:    target = 0; break;
                case SB_BOTTOM: target = si.nMax; break;
                default: return 0;
            }
            regularScrollPictureViewport(st, st->pictureScrollX, target);
            return 0;
        }
        case WM_MOUSEWHEEL:
            if (st) {
                const int delta = GET_WHEEL_DELTA_WPARAM(wp);
                regularScrollPictureViewport(st, st->pictureScrollX,
                    st->pictureScrollY - (delta / WHEEL_DELTA) *
                                          regularS(hwnd, REGULAR_LEFT_SCROLL_STEP * 3));
                return 0;
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, pictureViewportProc, subclassId);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

int regularScrollTargetFromCode(HWND hwnd, int code, const SCROLLINFO& si, int current) {
    switch (code) {
        case SB_LINELEFT:  return current - regularS(hwnd, REGULAR_LEFT_SCROLL_STEP);
        case SB_LINERIGHT: return current + regularS(hwnd, REGULAR_LEFT_SCROLL_STEP);
        case SB_PAGELEFT:  return current - static_cast<int>(si.nPage);
        case SB_PAGERIGHT: return current + static_cast<int>(si.nPage);
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: return si.nTrackPos;
        case SB_LEFT:  return 0;
        case SB_RIGHT: return si.nMax;
        default: return current;
    }
}

// ============================================================================
// Public entry: open picture popup for selection
// ============================================================================

void regularOpenPicturePopupForSelection(RegularReportState* st) {
    int selected = -1;
    if (st) {
        if (st->reportList) {
            const int sel = ListView_GetNextItem(st->reportList, -1, LVNI_SELECTED);
            if (sel >= 0 && sel < static_cast<int>(st->reportRows.size())) selected = sel;
        }
        if (selected < 0 && st->selectedReportIndex >= 0 &&
            st->selectedReportIndex < static_cast<int>(st->reportRows.size())) {
            selected = st->selectedReportIndex;
        }
    }
    if (!st || selected < 0) {
        MessageBoxW(st ? st->hwnd : nullptr,
                    L"请先选择一条报告记录。", L"常规报告", MB_ICONINFORMATION);
        return;
    }
    const auto& row = st->reportRows[static_cast<size_t>(selected)];
    const std::string repNo = search::trim(row.rep_no);
    if (repNo.empty()) {
        MessageBoxW(st->hwnd, L"当前报告没有验单号，无法查询图像。",
                    L"常规报告", MB_ICONINFORMATION);
        return;
    }
    std::string connection = st->reportConnectionString;
    if (connection.empty()) {
        connection = search::wide_to_utf8(search::build_connection_string_w(st->ctx.dbSettings));
    }
    if (connection.empty()) {
        MessageBoxW(st->hwnd, L"请先在“设置”中填写数据库连接信息。",
                    L"缺少数据库设置", MB_ICONWARNING);
        return;
    }

    registerPicturePopupClass(st->ctx.instance);
    if (IsWindow(st->picturePopup)) {
        ShowWindow(st->picturePopup, SW_SHOWNORMAL);
        SetForegroundWindow(st->picturePopup);
        refreshPicturePopupForSelection(st, selected);
        return;
    }

    auto* popup = new PicturePopupState;
    popup->owner = st;
    popup->font = st->ctx.uiFont;
    popup->repNo = repNo;
    popup->status = L"正在加载图像...";

    RECT ownerRc{};
    GetWindowRect(st->hwnd, &ownerRc);
    const int popupW = std::max(regularS(st->hwnd, REGULAR_PICTURE_POPUP_MIN_W),
                                search::load_module_int(L"RegularReport", L"PicturePopupWidth",
                                    regularS(st->hwnd, REGULAR_PICTURE_POPUP_DEFAULT_W)));
    const int popupH = std::max(regularS(st->hwnd, REGULAR_PICTURE_POPUP_MIN_H),
                                search::load_module_int(L"RegularReport", L"PicturePopupHeight",
                                    regularS(st->hwnd, REGULAR_PICTURE_POPUP_DEFAULT_H)));
    const int x = static_cast<int>(ownerRc.left) +
                  std::max(0, static_cast<int>(ownerRc.right - ownerRc.left - popupW) / 2);
    const int y = static_cast<int>(ownerRc.top) +
                  std::max(0, static_cast<int>(ownerRc.bottom - ownerRc.top - popupH) / 2);
    const std::wstring title = picturePopupTitle(st, selected);

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, REGULAR_REPORT_PICTURE_POPUP_CLASS,
                                title.c_str(), WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                x, y, popupW, popupH,
                                st->hwnd, nullptr, st->ctx.instance, popup);
    if (!hwnd) {
        delete popup;
        MessageBoxW(st->hwnd, L"结果图窗口创建失败。", L"常规报告", MB_ICONERROR);
        return;
    }
    st->picturePopup = hwnd;
    SetWindowTextW(hwnd, title.c_str());

    std::wstring cloneError;
    if (st->pictureImage && st->pictureRepNo == repNo &&
        clonePicturePopupImage(popup, st, cloneError)) {
        popup->loading = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    startPicturePopupQuery(popup, connection, repNo);
}

// ============================================================================
// Public entry: picture query dispatch
// ============================================================================

void regularQuerySelectedPicture(RegularReportState* st, int selected) {
    if (!st || !st->middleTab || TabCtrl_GetCurSel(st->middleTab) != 1) return;
    if (selected < 0 || selected >= static_cast<int>(st->reportRows.size())) {
        if (st) {
            st->pictureQueryLoading = false;
            st->pictureQueryTask.cancel();
            ++st->pictureQueryGeneration;
            st->pictureRepNo.clear();
            regularClearPictureView(st, L"");
        }
        return;
    }
    const std::string repNo = st->reportRows[static_cast<size_t>(selected)].rep_no;
    if (search::trim(repNo).empty()) {
        st->pictureQueryLoading = false;
        st->pictureQueryTask.cancel();
        ++st->pictureQueryGeneration;
        st->pictureRepNo.clear();
        regularClearPictureView(st, L"");
        return;
    }
    if (st->pictureRepNo == repNo) return;
    if (st->pictureQueryLoading && st->selectedReportIndex == selected) return;
    st->pictureQueryLoading = true;
    st->pictureRepNo = repNo;
    regularClearPictureView(st, L"");
    const int generation = ++st->pictureQueryGeneration;
    const HWND hwnd = st->hwnd;
    const std::string connection = st->reportConnectionString;
    const bool queued = st->pictureQueryTask.start<PictureLoadResult>(
        [connection, repNo, generation] {
            PictureLoadResult result;
            result.generation = generation;
            result.ok = search::query_report_picture(
                connection, repNo, result.picture, result.error);
            return result;
        },
        [hwnd](std::optional<PictureLoadResult> result, std::exception_ptr error) {
            auto* state = reinterpret_cast<RegularReportState*>(
                GetPropW(hwnd, REGULAR_REPORT_PROP_STATE));
            if (!state) return;
            if (error || !result) {
                state->pictureQueryLoading = false;
                regularClearPictureView(state, L"图像后台任务异常");
                return;
            }
            finishPictureQuery(state, hwnd,
                std::make_unique<PictureLoadResult>(std::move(*result)));
        });
    if (!queued) {
        st->pictureQueryLoading = false;
        regularClearPictureView(st, L"无法启动图像后台任务");
    }
}

#endif
