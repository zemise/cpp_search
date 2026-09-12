#include "barcode_module.h"

#ifdef _WIN32

#include "main_app.h"
#include "resource.h"
#include "regular_report_module.h"
#include "search_core.h"
#include "log.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"
#include "window_task.h"
#include "xlsx_writer.h"

#include <commctrl.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr const wchar_t* WND_CLASS = L"BarcodeModuleChild";
constexpr const wchar_t* LEGEND_CLASS = L"BarcodeStatusLegend";
constexpr const wchar_t* CHECK_DROPDOWN_CLASS = L"BarcodeCheckDropdown";
constexpr const wchar_t* WINDOW_TITLE = L"已签收条码查询";
constexpr const wchar_t* PROP_STATE = L"BarcodeSt";

constexpr int IDC_DATE_FIELD = 4101;
constexpr int IDC_START_DATE = 4102;
constexpr int IDC_END_DATE = 4103;
constexpr int IDC_BARCODE = 4104;
constexpr int IDC_NAME = 4105;
constexpr int IDC_REG_NO = 4106;
constexpr int IDC_MACHINE_STATUS = 4107;
constexpr int IDC_ROOM = 4108;
constexpr int IDC_CAMPUS = 4109;
constexpr int IDC_NOT_CANCELED = 4110;
constexpr int IDC_CANCELED = 4111;
constexpr int IDC_QUERY = 4112;
constexpr int IDC_CANCEL_BARCODE = 4113;
constexpr int IDC_CANCEL_MEDICAL = 4114;
constexpr int IDC_REFRESH = 4115;
constexpr int IDC_CANCEL_REASON = 4116;
constexpr int IDC_EXPORT = 4118;
constexpr int IDC_LIST = 4120;
constexpr int IDC_STATUS = 4121;
constexpr int IDC_PROGRESS = 4123;
constexpr int IDC_ACTIVITY_PANEL = 4126;
constexpr int IDC_ACTIVITY_LABEL = 4127;
constexpr int IDC_DROPDOWN_LIST = 4510;
constexpr int FIRST_DATA_COLUMN = 0;
constexpr int FIRST_SHARED_BARCODE_COLUMN = 0;
constexpr int LAST_SHARED_BARCODE_COLUMN = 7;
constexpr int LAST_DATA_COLUMN = 28;
constexpr int REVIEW_ELAPSED_COLUMN = 16;
constexpr int FEE_COLUMN = 18;
constexpr UINT IDM_COPY_CELL = 41201;
const COLORREF COLOR_NOT_MACHINE = RGB(0xFF, 0xFF, 0x54);
const COLORREF COLOR_LOADED_NOT_REVIEWED = RGB(0xFF, 0xFF, 0xFF);
const COLORREF COLOR_REVIEWED_NOT_SENT = RGB(0x6F, 0x94, 0xE6);
const COLORREF COLOR_SENT = RGB(0x99, 0xBB, 0x90);
const COLORREF COLOR_ALERT_BACKGROUND = RGB(0xFF, 0xE8, 0xE8);
const COLORREF COLOR_ALERT_TEXT = RGB(0xA4, 0x00, 0x00);
const COLORREF COLOR_ACTIVITY_BACKGROUND = RGB(0xFF, 0xFF, 0xFF);

struct BarcodeState {
    ModuleContext ctx;
    HWND dateField = nullptr;
    HWND startDate = nullptr;
    HWND endDate = nullptr;
    HWND barcode = nullptr;
    HWND name = nullptr;
    HWND regNo = nullptr;
    HWND campus = nullptr;
    HWND room = nullptr;
    HWND machineStatus = nullptr;
    HWND notCanceled = nullptr;
    HWND canceled = nullptr;
    HWND query = nullptr;
    HWND cancelBarcode = nullptr;
    HWND cancelMedical = nullptr;
    HWND refresh = nullptr;
    HWND cancelReason = nullptr;
    HWND exportExcel = nullptr;
    HWND legend = nullptr;
    HWND list = nullptr;
    HWND status = nullptr;
    HWND progress = nullptr;
    HWND activityPanel = nullptr;
    HWND activityLabel = nullptr;
    HWND tooltip = nullptr;
    HBRUSH bgBrush = nullptr;
    HBRUSH alertBrush = nullptr;
    HBRUSH activityBrush = nullptr;
    std::vector<search::RoomOption> allRooms;
    std::vector<search::RoomOption> rooms;
    std::vector<search::BarcodeQueryRow> rows;
    std::vector<size_t> displayOrder;
    std::vector<std::string> selectedRoomCodes;
    std::vector<std::string> selectedMachineStatuses;
    bool roomDropdownOpen = false;
    bool statusDropdownOpen = false;
    int listSortColumn = -1;
    bool listSortAscending = true;
    app::WindowTask queryTask;
    app::WindowTask exportTask;
    bool querying = false;
    bool exporting = false;
    bool statusIsAlert = false;
    std::wstring lastAlertText;
};

struct BarcodeQueryResult {
    bool ok = false;
    std::string error;
    std::vector<search::BarcodeQueryRow> rows;
};

struct BarcodeExportResult {
    bool ok = false;
    bool canceled = false;
    std::wstring path;
    std::wstring error;
    size_t rowCount = 0;
    long long elapsedMs = 0;
};

void finishQuery(HWND hwnd, BarcodeState* st,
                 std::unique_ptr<BarcodeQueryResult> result);

struct ListColumn {
    int index;
    const wchar_t* title;
    int width;
};

const ListColumn BARCODE_COLUMNS[] = {
    {0, L"样本号", 58},
    {1, L"急诊", 44},
    {2, L"条形码", 104},
    {3, L"病人号", 90},
    {4, L"类型", 56},
    {5, L"姓名", 86},
    {6, L"性别", 48},
    {7, L"申请科室", 110},
    {8, L"床号", 70},
    {9, L"签收人", 100},
    {10, L"签收时间", 150},
    {11, L"医嘱内容", 230},
    {12, L"标本", 72},
    {13, L"检验者", 80},
    {14, L"审核者", 80},
    {15, L"审核时间", 150},
    {16, L"签收-审核时间差", 160},
    {17, L"上机状态", 112},
    {18, L"费用", 76},
    {19, L"申请医生", 90},
    {20, L"状态", 66},
    {21, L"备注", 58},
    {22, L"原因", 58},
    {23, L"送检", 70},
    {24, L"送检时间", 140},
    {25, L"申请时间", 150},
    {26, L"取消时间", 140},
    {27, L"取消人", 82},
    {28, L"HZID", 70},
};

void runQuery(HWND hwnd, BarcodeState* st);

std::string textOf(HWND hwnd) {
    wchar_t buf[512]{};
    GetWindowTextW(hwnd, buf, 512);
    return search::trim(search::wide_to_utf8(buf));
}

std::string comboText(HWND hwnd) {
    const int idx = static_cast<int>(SendMessageW(hwnd, CB_GETCURSEL, 0, 0));
    if (idx < 0) return "";
    wchar_t buf[256]{};
    SendMessageW(hwnd, CB_GETLBTEXT, static_cast<WPARAM>(idx), reinterpret_cast<LPARAM>(buf));
    return search::trim(search::wide_to_utf8(buf));
}

std::string dateText(HWND hwnd) {
    SYSTEMTIME st{};
    if (DateTime_GetSystemtime(hwnd, &st) != GDT_VALID) return "";
    char buf[20]{};
    sprintf_s(buf, "%04u-%02u-%02u %02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return buf;
}

void setToday(HWND hwnd, bool endOfDay) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    st.wHour = endOfDay ? 23 : 0;
    st.wMinute = endOfDay ? 59 : 0;
    st.wSecond = 0;
    st.wMilliseconds = 0;
    DateTime_SetSystemtime(hwnd, GDT_VALID, &st);
}

HWND dateTimePicker(HWND parent, int id, int x, int y, int w, int h) {
    HWND hwnd = search::create_date_picker(parent, id, x, y, w, h);
    DateTime_SetFormat(hwnd, L"yyyy-MM-dd HH:mm");
    return hwnd;
}

void setStatus(BarcodeState* st, const std::wstring& text) {
    if (!st || !st->status) return;
    st->statusIsAlert = false;
    st->lastAlertText.clear();
    SetWindowTextW(st->status, text.c_str());
    InvalidateRect(st->status, nullptr, TRUE);
}

void showAlert(BarcodeState* st, const std::wstring& text) {
    if (!st || !st->status) return;
    st->statusIsAlert = true;
    st->lastAlertText = text;
    SetWindowTextW(st->status, (L"  !  " + text + L"（点击查看详情）").c_str());
    InvalidateRect(st->status, nullptr, TRUE);
}

void addTooltip(HWND tooltip, HWND control, const wchar_t* text) {
    if (!tooltip || !control || !text) return;
    TTTOOLINFOW tool{};
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = GetParent(control);
    tool.uId = reinterpret_cast<UINT_PTR>(control);
    tool.lpszText = const_cast<wchar_t*>(text);
    SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
}

void positionActivity(HWND owner, BarcodeState* st) {
    if (!owner || !st || !st->activityPanel) return;
    RECT rc{};
    GetClientRect(owner, &rc);
    const float scale = search::dpi_scale_factor(owner);
    const int margin = static_cast<int>(12 * scale);
    const int listTop = static_cast<int>(120 * scale);
    const int cardWidth = (std::min)(static_cast<int>(400 * scale),
                                     (std::max)(static_cast<int>(260 * scale),
                                                static_cast<int>(rc.right) - margin * 2));
    const int cardHeight = static_cast<int>(96 * scale);
    const int x = (std::max)(margin, (static_cast<int>(rc.right) - cardWidth) / 2);
    const int availableHeight = (std::max)(cardHeight,
        static_cast<int>(rc.bottom) - listTop);
    const int y = listTop + (std::max)(static_cast<int>(18 * scale),
        (availableHeight - cardHeight) / 2);
    const int contentX = x + static_cast<int>(24 * scale);
    const int contentWidth = (std::max)(static_cast<int>(160 * scale),
        cardWidth - static_cast<int>(48 * scale));

    MoveWindow(st->activityPanel, x, y, cardWidth, cardHeight, TRUE);
    MoveWindow(st->activityLabel, contentX, y + static_cast<int>(18 * scale),
               contentWidth, static_cast<int>(26 * scale), TRUE);
    MoveWindow(st->progress, contentX, y + static_cast<int>(55 * scale),
               contentWidth, static_cast<int>(16 * scale), TRUE);
}

void showActivityCard(HWND owner, BarcodeState* st) {
    if (!owner || !st) return;
    positionActivity(owner, st);
    const HWND controls[] = {st->activityPanel, st->activityLabel, st->progress};
    for (HWND control : controls) {
        if (!control) continue;
        ShowWindow(control, SW_SHOWNA);
        SetWindowPos(control, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

void showQueryActivity(HWND owner, BarcodeState* st) {
    if (!owner || !st) return;
    if (st->activityLabel) SetWindowTextW(st->activityLabel, L"正在查询全部条码，请稍候…");
    if (st->progress) {
        LONG_PTR style = GetWindowLongPtrW(st->progress, GWL_STYLE);
        SetWindowLongPtrW(st->progress, GWL_STYLE, style | PBS_MARQUEE);
        SetWindowPos(st->progress, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        SendMessageW(st->progress, PBM_SETMARQUEE, TRUE, 35);
    }
    showActivityCard(owner, st);
}

void showExportActivity(HWND owner, BarcodeState* st, size_t total) {
    if (!owner || !st) return;
    if (st->activityLabel) {
        SetWindowTextW(st->activityLabel,
                       (L"正在导出全部 " + std::to_wstring(total) + L" 条记录…").c_str());
    }
    if (st->progress) {
        SendMessageW(st->progress, PBM_SETMARQUEE, FALSE, 0);
        LONG_PTR style = GetWindowLongPtrW(st->progress, GWL_STYLE);
        SetWindowLongPtrW(st->progress, GWL_STYLE, style & ~static_cast<LONG_PTR>(PBS_MARQUEE));
        SetWindowPos(st->progress, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        SendMessageW(st->progress, PBM_SETRANGE32, 0,
                     static_cast<LPARAM>((std::min)(total, static_cast<size_t>(INT_MAX))));
        SendMessageW(st->progress, PBM_SETPOS, 0, 0);
    }
    showActivityCard(owner, st);
}

void hideActivity(HWND owner, BarcodeState* st) {
    if (!owner || !st) return;
    RECT dirty{};
    if (st->activityPanel) {
        GetWindowRect(st->activityPanel, &dirty);
        MapWindowPoints(nullptr, owner, reinterpret_cast<POINT*>(&dirty), 2);
    }
    HDWP deferred = BeginDeferWindowPos(3);
    const HWND controls[] = {st->progress, st->activityLabel, st->activityPanel};
    for (HWND control : controls) {
        if (!control) continue;
        if (!deferred) break;
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
    if (st->progress) SendMessageW(st->progress, PBM_SETMARQUEE, FALSE, 0);
    RedrawWindow(owner, IsRectEmpty(&dirty) ? nullptr : &dirty, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void addComboItem(HWND combo, const wchar_t* text) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
}

void fillStaticCombos(BarcodeState* st) {
    addComboItem(st->dateField, L"申请日期");
    addComboItem(st->dateField, L"签收日期");
    addComboItem(st->dateField, L"上机日期");
    SendMessageW(st->dateField, CB_SETCURSEL, 1, 0);

    addComboItem(st->campus, L"全部");
    addComboItem(st->campus, L"老院");
    addComboItem(st->campus, L"新院");
    SendMessageW(st->campus, CB_SETCURSEL, 0, 0);
}

void filterRoomsForCampus(BarcodeState* st) {
    if (!st) return;
    const std::string campus = comboText(st->campus);
    std::string deptCode;
    if (campus == "老院") {
        deptCode = "102";
    } else if (campus == "新院") {
        deptCode = "401";
    }

    st->rooms.clear();
    for (const auto& room : st->allRooms) {
        const std::string roomDeptCode = search::trim(room.dept_code);
        if (deptCode.empty() || roomDeptCode == deptCode) {
            st->rooms.push_back(room);
        }
    }

    st->selectedRoomCodes.erase(
        std::remove_if(st->selectedRoomCodes.begin(), st->selectedRoomCodes.end(),
                       [st](const std::string& selected) {
                           return std::none_of(st->rooms.begin(), st->rooms.end(),
                                               [&selected](const search::RoomOption& room) {
                                                   return search::trim(room.room_code) == search::trim(selected);
                                               });
                       }),
        st->selectedRoomCodes.end());
}

void loadRooms(BarcodeState* st) {
    st->allRooms.clear();

    const auto conn = search::wide_to_utf8(search::build_connection_string_w(st->ctx.dbSettings));
    if (!conn.empty()) {
        std::string error;
        search::query_barcode_rooms(conn, st->allRooms, error);
    }
    filterRoomsForCampus(st);
}

HWND label(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_RIGHT,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND leftLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND button(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                           x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

HWND dropdownButton(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                           x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

void addColumn(HWND list, int index, const wchar_t* title, int width) {
    search::add_list_column(list, index, title, width);
}

struct LegendItem {
    const wchar_t* text;
    COLORREF color;
    int x;
    int y;
};

LRESULT CALLBACK legendProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(dc, &rc, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(0, 0, 0));

            HFONT font = nullptr;
            if (auto* st = reinterpret_cast<BarcodeState*>(GetPropW(GetParent(hwnd), PROP_STATE))) {
                font = st->ctx.uiFont;
            }
            HGDIOBJ oldFont = nullptr;
            if (font) oldFont = SelectObject(dc, font);

            const float s = search::dpi_scale_factor(hwnd);
            auto S = [s](int v) { return static_cast<int>(v * s); };
            const LegendItem items[] = {
                {L"已签收未上机", COLOR_NOT_MACHINE, 0, 2},
                {L"已上机未审核", COLOR_LOADED_NOT_REVIEWED, 116, 2},
                {L"已审核未发送", COLOR_REVIEWED_NOT_SENT, 248, 2},
                {L"发送完成", COLOR_SENT, 380, 2},
            };
            for (const auto& item : items) {
                RECT swatch{S(item.x), S(item.y + 3), S(item.x + 12), S(item.y + 15)};
                HBRUSH brush = CreateSolidBrush(item.color);
                FillRect(dc, &swatch, brush);
                DeleteObject(brush);
                FrameRect(dc, &swatch, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
                RECT textRc{S(item.x + 17), S(item.y), rc.right, S(item.y + 20)};
                DrawTextW(dc, item.text, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            if (oldFont) SelectObject(dc, oldFont);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void registerLegendClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = legendProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = LEGEND_CLASS;
    RegisterClassW(&wc);
    registered = true;
}

std::string sanitizeFilenamePart(std::string text) {
    text = search::trim(text);
    for (char& ch : text) {
        switch (ch) {
            case '\\':
            case '/':
            case ':':
            case '*':
            case '?':
            case '"':
            case '<':
            case '>':
            case '|':
                ch = '_';
                break;
            default:
                break;
        }
    }
    return text;
}

std::wstring defaultExportFilename(BarcodeState* st) {
    std::string filename = "已签收条码";
    const std::string dateField = sanitizeFilenamePart(comboText(st->dateField));
    const std::string start = sanitizeFilenamePart(dateText(st->startDate));
    const std::string end = sanitizeFilenamePart(dateText(st->endDate));
    if (!dateField.empty()) filename += "-" + dateField;
    if (!start.empty()) filename += "-" + start;
    if (!end.empty() && end != start) filename += "至" + end;
    const std::string campus = sanitizeFilenamePart(comboText(st->campus));
    if (!campus.empty()) filename += "-" + campus;
    filename += ".xlsx";
    return search::utf8_to_wide(filename);
}

bool chooseExportPath(HWND owner, BarcodeState* st, std::wstring& path) {
    wchar_t buffer[MAX_PATH]{};
    const std::wstring defaultName = defaultExportFilename(st);
    lstrcpynW(buffer, defaultName.c_str(), MAX_PATH);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Excel 工作簿 (*.xlsx)\0*.xlsx\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"xlsx";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return false;
    path = buffer;
    return true;
}

bool copyTextToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) {
        CloseClipboard();
        return false;
    }
    void* locked = GlobalLock(mem);
    if (!locked) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }
    std::memcpy(locked, text.c_str(), bytes);
    GlobalUnlock(mem);
    if (!SetClipboardData(CF_UNICODETEXT, mem)) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

LRESULT CALLBACK searchEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                 UINT_PTR subclassId, DWORD_PTR refData) {
    if (msg == WM_GETDLGCODE) {
        return DefSubclassProc(hwnd, msg, wp, lp) | DLGC_WANTALLKEYS;
    }
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        HWND parent = reinterpret_cast<HWND>(refData);
        SendMessageW(parent, WM_COMMAND, MAKEWPARAM(IDC_QUERY, BN_CLICKED), 0);
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, searchEditProc, subclassId);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK dropdownButtonProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                    UINT_PTR subclassId, DWORD_PTR refData) {
    if (msg == WM_KEYDOWN &&
        (wp == VK_F4 || (wp == VK_DOWN && (GetKeyState(VK_MENU) & 0x8000)))) {
        HWND parent = reinterpret_cast<HWND>(refData);
        SendMessageW(parent, WM_COMMAND,
                     MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED),
                     reinterpret_cast<LPARAM>(hwnd));
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, dropdownButtonProc, subclassId);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

struct CheckDropdownItem {
    std::string code;
    std::wstring text;
};

struct CheckDropdownState {
    HWND list = nullptr;
    std::vector<CheckDropdownItem> items;
    std::vector<std::string> selected;
    bool adjusting = false;
    bool closeRequested = false;
};

bool containsValue(const std::vector<std::string>& values, const std::string& value) {
    const std::string target = search::trim(value);
    return std::any_of(values.begin(), values.end(), [&](const std::string& item) {
        return search::trim(item) == target;
    });
}

void addDropdownItem(HWND list, int index, const std::wstring& text) {
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = index;
    item.pszText = const_cast<wchar_t*>(text.c_str());
    ListView_InsertItem(list, &item);
}

std::vector<std::string> checkedDropdownValues(CheckDropdownState* ds) {
    std::vector<std::string> values;
    if (!ds || !ds->list) return values;
    const int count = ListView_GetItemCount(ds->list);
    for (int i = 1; i < count && i < static_cast<int>(ds->items.size()); ++i) {
        if (ListView_GetCheckState(ds->list, i)) {
            values.push_back(ds->items[static_cast<size_t>(i)].code);
        }
    }
    return values;
}

bool allSpecificDropdownItemsChecked(CheckDropdownState* ds) {
    if (!ds || !ds->list) return false;
    const int count = ListView_GetItemCount(ds->list);
    if (count <= 1) return false;
    for (int i = 1; i < count; ++i) {
        if (!ListView_GetCheckState(ds->list, i)) return false;
    }
    return true;
}

void normalizeDropdownChecks(CheckDropdownState* ds, int changedIndex) {
    if (!ds || !ds->list || ds->adjusting) return;
    ds->adjusting = true;
    const int count = ListView_GetItemCount(ds->list);
    if (changedIndex == 0) {
        const BOOL checkAll = ListView_GetCheckState(ds->list, 0);
        for (int i = 1; i < count; ++i) {
            ListView_SetCheckState(ds->list, i, checkAll ? TRUE : FALSE);
        }
    } else {
        ListView_SetCheckState(ds->list, 0, allSpecificDropdownItemsChecked(ds) ? TRUE : FALSE);
    }
    ds->selected = checkedDropdownValues(ds);
    ds->adjusting = false;
}

LRESULT CALLBACK dropdownListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                  UINT_PTR subclassId, DWORD_PTR refData) {
    if (msg == WM_KEYDOWN && (wp == VK_ESCAPE || wp == VK_RETURN)) {
        HWND popup = reinterpret_cast<HWND>(refData);
        if (auto* ds = reinterpret_cast<CheckDropdownState*>(GetWindowLongPtrW(popup, GWLP_USERDATA))) {
            ds->closeRequested = true;
        }
        DestroyWindow(popup);
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, dropdownListProc, subclassId);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK checkDropdownProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ds = reinterpret_cast<CheckDropdownState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            ds = reinterpret_cast<CheckDropdownState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ds));
            RECT rc{};
            GetClientRect(hwnd, &rc);
            ds->list = CreateWindowExW(0, WC_LISTVIEWW, L"",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL |
                                           LVS_NOCOLUMNHEADER | LVS_SHOWSELALWAYS,
                                       0, 0, rc.right, rc.bottom, hwnd,
                                       win32_control_id(IDC_DROPDOWN_LIST), GetModuleHandleW(nullptr), nullptr);
            SetWindowSubclass(ds->list, dropdownListProc, 1, reinterpret_cast<DWORD_PTR>(hwnd));
            ListView_SetExtendedListViewStyle(ds->list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            search::add_list_column(ds->list, 0, L"", rc.right - 4);
            ds->adjusting = true;
            for (size_t i = 0; i < ds->items.size(); ++i) {
                addDropdownItem(ds->list, static_cast<int>(i), ds->items[i].text);
                const bool checked = i > 0 && containsValue(ds->selected, ds->items[i].code);
                ListView_SetCheckState(ds->list, static_cast<int>(i), checked ? TRUE : FALSE);
            }
            ds->adjusting = false;
            normalizeDropdownChecks(ds, -1);
            return 0;
        }
        case WM_SIZE:
            if (ds && ds->list) {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                MoveWindow(ds->list, 0, 0, rc.right, rc.bottom, TRUE);
                ListView_SetColumnWidth(ds->list, 0, rc.right - 4);
            }
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE || wp == VK_RETURN) {
                if (ds) ds->closeRequested = true;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_NOTIFY:
            if (ds) {
                auto* nm = reinterpret_cast<NMHDR*>(lp);
                if (nm->idFrom == IDC_DROPDOWN_LIST && nm->code == LVN_ITEMCHANGED) {
                    auto* item = reinterpret_cast<NMLISTVIEW*>(lp);
                    const UINT oldState = item->uOldState & LVIS_STATEIMAGEMASK;
                    const UINT newState = item->uNewState & LVIS_STATEIMAGEMASK;
                    if (oldState != newState) normalizeDropdownChecks(ds, item->iItem);
                    return 0;
                }
                if (nm->idFrom == IDC_DROPDOWN_LIST && nm->code == NM_CLICK) {
                    POINT pt{};
                    GetCursorPos(&pt);
                    ScreenToClient(ds->list, &pt);
                    LVHITTESTINFO hit{};
                    hit.pt = pt;
                    const int row = ListView_HitTest(ds->list, &hit);
                    if (row >= 0 && !(hit.flags & LVHT_ONITEMSTATEICON)) {
                        const BOOL checked = ListView_GetCheckState(ds->list, row);
                        ListView_SetCheckState(ds->list, row, checked ? FALSE : TRUE);
                        normalizeDropdownChecks(ds, row);
                    }
                    return 0;
                }
            }
            break;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) {
                if (ds) ds->closeRequested = true;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_DESTROY:
            if (ds) ds->closeRequested = true;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void registerCheckDropdownClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = checkDropdownProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = CHECK_DROPDOWN_CLASS;
    RegisterClassW(&wc);
    registered = true;
}

void showCheckDropdown(HWND owner, HWND anchor, const std::vector<CheckDropdownItem>& items,
                       std::vector<std::string>& selected, int minWidth, int visibleRows) {
    if (!owner || !anchor || items.empty()) return;
    registerCheckDropdownClass(GetModuleHandleW(nullptr));
    CheckDropdownState ds;
    ds.items = items;
    ds.selected = selected;

    RECT anchorRc{};
    GetWindowRect(anchor, &anchorRc);
    const float s = search::dpi_scale_factor(owner);
    const int rowH = static_cast<int>(24 * s);
    const int rows = (std::min)(visibleRows, (std::max)(1, static_cast<int>(items.size())));
    const int anchorW = static_cast<int>(anchorRc.right - anchorRc.left);
    int w = (std::max)(minWidth, anchorW);
    int h = (std::max)(rowH * rows + static_cast<int>(6 * s), static_cast<int>(80 * s));

    RECT workRc{};
    HMONITOR monitor = MonitorFromRect(&anchorRc, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (monitor && GetMonitorInfoW(monitor, &mi)) {
        workRc = mi.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workRc, 0);
    }
    const int workW = workRc.right - workRc.left;
    const int workH = workRc.bottom - workRc.top;
    if (workW > 0) w = (std::min)(w, workW);
    if (workH > 0) h = (std::min)(h, workH);

    int x = anchorRc.left;
    if (x + w > workRc.right) x = workRc.right - w;
    if (x < workRc.left) x = workRc.left;

    int y = anchorRc.bottom;
    const bool fitsBelow = y + h <= workRc.bottom;
    const bool fitsAbove = anchorRc.top - h >= workRc.top;
    if (!fitsBelow && fitsAbove) {
        y = anchorRc.top - h;
    } else if (!fitsBelow) {
        y = (std::max)(workRc.top, workRc.bottom - h);
    }

    HWND popup = CreateWindowExW(WS_EX_TOOLWINDOW,
                                 CHECK_DROPDOWN_CLASS, L"",
                                 WS_POPUP | WS_BORDER,
                                 x, y, w, h,
                                 owner, nullptr, GetModuleHandleW(nullptr), &ds);
    if (!popup) return;

    ShowWindow(popup, SW_SHOW);
    SetFocus(ds.list ? ds.list : popup);
    MSG msg{};
    BOOL gotMessage = TRUE;
    while (IsWindow(popup) && !ds.closeRequested &&
           (gotMessage = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        if (!IsDialogMessageW(popup, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    selected = ds.selected;
    SetFocus(anchor);
    if (gotMessage == 0) PostQuitMessage(static_cast<int>(msg.wParam));
}

const wchar_t* STATUS_LABELS[] = {
    L"已签收未上机",
    L"已上机未审核",
    L"已审核未发送",
    L"发送完成",
};

std::vector<CheckDropdownItem> roomDropdownItems(const BarcodeState* st) {
    std::vector<CheckDropdownItem> items;
    items.push_back({"", L"全部"});
    if (!st) return items;
    for (const auto& room : st->rooms) {
        CheckDropdownItem item;
        item.code = search::trim(room.room_code);
        item.text = search::utf8_to_wide(search::trim(room.room_name));
        if (item.text.empty()) item.text = search::utf8_to_wide(item.code);
        if (!item.code.empty()) items.push_back(std::move(item));
    }
    return items;
}

std::vector<CheckDropdownItem> statusDropdownItems() {
    std::vector<CheckDropdownItem> items;
    items.push_back({"", L"全部"});
    for (const auto* label : STATUS_LABELS) {
        items.push_back({search::wide_to_utf8(label), label});
    }
    return items;
}

bool allRoomsSelected(const BarcodeState* st) {
    return st && !st->rooms.empty() && st->selectedRoomCodes.size() == st->rooms.size();
}

bool allStatusesSelected(const BarcodeState* st) {
    return st && st->selectedMachineStatuses.size() == (sizeof(STATUS_LABELS) / sizeof(STATUS_LABELS[0]));
}

std::wstring roomSummary(const BarcodeState* st) {
    if (!st || st->selectedRoomCodes.empty()) return L"全部";
    if (allRoomsSelected(st)) return L"全部";
    if (st->selectedRoomCodes.size() == 1) {
        const std::string code = search::trim(st->selectedRoomCodes.front());
        for (const auto& room : st->rooms) {
            if (search::trim(room.room_code) == code) {
                return search::utf8_to_wide(search::trim(room.room_name));
            }
        }
        return search::utf8_to_wide(code);
    }
    return L"已选 " + std::to_wstring(st->selectedRoomCodes.size()) + L" 个";
}

std::wstring statusSummary(const BarcodeState* st) {
    if (!st || st->selectedMachineStatuses.empty()) return L"全部";
    if (allStatusesSelected(st)) return L"全部";
    if (st->selectedMachineStatuses.size() == 1) {
        return search::utf8_to_wide(st->selectedMachineStatuses.front());
    }
    const bool unfinished =
        st->selectedMachineStatuses.size() == 3 &&
        containsValue(st->selectedMachineStatuses, "已签收未上机") &&
        containsValue(st->selectedMachineStatuses, "已上机未审核") &&
        containsValue(st->selectedMachineStatuses, "已审核未发送");
    if (unfinished) return L"未完成检验";
    return L"已选 " + std::to_wstring(st->selectedMachineStatuses.size()) + L" 项";
}

void updateFilterButtonText(BarcodeState* st, bool roomOpen = false, bool statusOpen = false) {
    if (!st) return;
    st->roomDropdownOpen = roomOpen;
    st->statusDropdownOpen = statusOpen;
    if (st->room) {
        SetWindowTextW(st->room, roomSummary(st).c_str());
        InvalidateRect(st->room, nullptr, TRUE);
    }
    if (st->machineStatus) {
        SetWindowTextW(st->machineStatus, statusSummary(st).c_str());
        InvalidateRect(st->machineStatus, nullptr, TRUE);
    }
}

void drawDropdownButton(BarcodeState* st, DRAWITEMSTRUCT* dis) {
    if (!st || !dis || !dis->hwndItem) return;
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool focus = (dis->itemState & ODS_FOCUS) != 0;
    const bool hot = (dis->itemState & ODS_HOTLIGHT) != 0;
    const bool open =
        (dis->CtlID == IDC_ROOM && st->roomDropdownOpen) ||
        (dis->CtlID == IDC_MACHINE_STATUS && st->statusDropdownOpen);

    RECT rc = dis->rcItem;
    const float s = search::dpi_scale_factor(dis->hwndItem);
    const int arrowW = static_cast<int>(22 * s);
    RECT arrowRc = rc;
    arrowRc.left = (std::max)(arrowRc.left, arrowRc.right - arrowW);

    const int comboState = disabled ? CBXS_DISABLED :
                           (pressed || open) ? CBXS_PRESSED :
                           hot ? CBXS_HOT :
                           CBXS_NORMAL;
    HTHEME theme = OpenThemeData(dis->hwndItem, L"COMBOBOX");
    if (theme) {
        if (FAILED(DrawThemeBackground(theme, dis->hDC, CP_READONLY, comboState, &rc, nullptr))) {
            DrawThemeBackground(theme, dis->hDC, CP_BORDER, comboState, &rc, nullptr);
        }
        if (FAILED(DrawThemeBackground(theme, dis->hDC, CP_DROPDOWNBUTTONRIGHT, comboState, &arrowRc, nullptr))) {
            DrawThemeBackground(theme, dis->hDC, CP_DROPDOWNBUTTON, comboState, &arrowRc, nullptr);
        }
        CloseThemeData(theme);
    } else {
        FillRect(dis->hDC, &rc, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        DrawEdge(dis->hDC, &rc, pressed ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
        RECT fallbackArrow = arrowRc;
        InflateRect(&fallbackArrow, -2, -2);
        DrawFrameControl(dis->hDC, &fallbackArrow, DFC_SCROLL, DFCS_SCROLLDOWN);
    }

    RECT textRc = rc;
    textRc.left += static_cast<int>(6 * s);
    textRc.right = (std::max)(textRc.left, rc.right - arrowW - static_cast<int>(4 * s));
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, disabled ? GetSysColor(COLOR_GRAYTEXT) : GetSysColor(COLOR_WINDOWTEXT));
    HGDIOBJ oldFont = nullptr;
    if (st->ctx.uiFont) oldFont = SelectObject(dis->hDC, st->ctx.uiFont);
    wchar_t text[256]{};
    GetWindowTextW(dis->hwndItem, text, 256);
    DrawTextW(dis->hDC, text, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (oldFont) SelectObject(dis->hDC, oldFont);
    if (focus) {
        RECT focusRc = rc;
        InflateRect(&focusRc, -3, -3);
        DrawFocusRect(dis->hDC, &focusRc);
    }
}

void openRoomDropdown(HWND hwnd, BarcodeState* st) {
    if (!st) return;
    const float s = search::dpi_scale_factor(hwnd);
    updateFilterButtonText(st, true, false);
    showCheckDropdown(hwnd, st->room, roomDropdownItems(st), st->selectedRoomCodes,
                      static_cast<int>(220 * s), 10);
    updateFilterButtonText(st);
}

void openStatusDropdown(HWND hwnd, BarcodeState* st) {
    if (!st) return;
    const float s = search::dpi_scale_factor(hwnd);
    updateFilterButtonText(st, false, true);
    showCheckDropdown(hwnd, st->machineStatus, statusDropdownItems(), st->selectedMachineStatuses,
                      static_cast<int>(180 * s), 6);
    updateFilterButtonText(st);
}

void createControls(HWND hwnd, BarcodeState* st) {
    const float s = search::dpi_scale_factor(hwnd);
    auto S = [s](int v) { return static_cast<int>(v * s); };
    registerLegendClass(GetModuleHandleW(nullptr));

    st->dateField = search::create_combo(hwnd, IDC_DATE_FIELD, S(8), S(8), S(88), S(160), false);
    st->startDate = dateTimePicker(hwnd, IDC_START_DATE, S(104), S(8), S(160), S(24));
    label(hwnd, L"至", S(268), S(11), S(20), S(22));
    st->endDate = dateTimePicker(hwnd, IDC_END_DATE, S(292), S(8), S(160), S(24));
    label(hwnd, L"条形码", S(456), S(11), S(58), S(22));
    st->barcode = search::create_edit(hwnd, IDC_BARCODE, S(518), S(8), S(106), S(24));
    SetWindowSubclass(st->barcode, searchEditProc, 1, reinterpret_cast<DWORD_PTR>(hwnd));
    label(hwnd, L"姓  名", S(628), S(11), S(54), S(22));
    st->name = search::create_edit(hwnd, IDC_NAME, S(686), S(8), S(72), S(24));
    SetWindowSubclass(st->name, searchEditProc, 2, reinterpret_cast<DWORD_PTR>(hwnd));
    label(hwnd, L"病人号", S(762), S(11), S(54), S(22));
    st->regNo = search::create_edit(hwnd, IDC_REG_NO, S(820), S(8), S(95), S(24));
    SetWindowSubclass(st->regNo, searchEditProc, 3, reinterpret_cast<DWORD_PTR>(hwnd));
    label(hwnd, L"院区", S(919), S(11), S(38), S(22));
    st->campus = search::create_combo(hwnd, IDC_CAMPUS, S(961), S(8), S(70), S(120), false);
    label(hwnd, L"专业组", S(1035), S(11), S(46), S(22));
    st->room = dropdownButton(hwnd, IDC_ROOM, L"全部", S(1085), S(8), S(105), S(24));
    SetWindowSubclass(st->room, dropdownButtonProc, 1, reinterpret_cast<DWORD_PTR>(hwnd));
    label(hwnd, L"上机状态", S(1194), S(11), S(68), S(22));
    st->machineStatus = dropdownButton(hwnd, IDC_MACHINE_STATUS, L"全部", S(1266), S(8), S(140), S(24));
    SetWindowSubclass(st->machineStatus, dropdownButtonProc, 2, reinterpret_cast<DWORD_PTR>(hwnd));

    st->notCanceled = CreateWindowExW(0, L"BUTTON", L"未取消签收", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
                                      S(10), S(42), S(98), S(24), hwnd, win32_control_id(IDC_NOT_CANCELED), GetModuleHandleW(nullptr), nullptr);
    st->canceled = CreateWindowExW(0, L"BUTTON", L"取消签收", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
                                   S(10), S(70), S(98), S(24), hwnd, win32_control_id(IDC_CANCELED), GetModuleHandleW(nullptr), nullptr);
    Button_SetCheck(st->notCanceled, BST_CHECKED);

    st->query = button(hwnd, IDC_QUERY, L"查    询", S(116), S(50), S(76), S(32));
    st->cancelBarcode = button(hwnd, IDC_CANCEL_BARCODE, L"取消条码签收", S(200), S(50), S(112), S(32));
    st->cancelMedical = button(hwnd, IDC_CANCEL_MEDICAL, L"取消医嘱签收", S(320), S(50), S(112), S(32));
    st->refresh = button(hwnd, IDC_REFRESH, L"刷    新", S(440), S(50), S(76), S(32));
    st->cancelReason = button(hwnd, IDC_CANCEL_REASON, L"取消原因限制", S(524), S(50), S(118), S(32));
    st->exportExcel = button(hwnd, IDC_EXPORT, L"导出Excel", S(650), S(50), S(100), S(32));
    EnableWindow(st->cancelBarcode, FALSE);
    EnableWindow(st->cancelMedical, FALSE);
    EnableWindow(st->cancelReason, FALSE);
    EnableWindow(st->exportExcel, FALSE);

    st->legend = CreateWindowExW(0, LEGEND_CLASS, L"", WS_CHILD | WS_VISIBLE,
                                 S(882), S(52), S(460), S(24), hwnd, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);

    st->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL |
                                   LVS_OWNERDATA,
                               S(4), S(120), S(1240), S(420), hwnd, win32_control_id(IDC_LIST), GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(st->list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    for (const auto& column : BARCODE_COLUMNS) {
        addColumn(st->list, column.index, column.title, S(column.width));
    }

    st->status = leftLabel(hwnd, L"", S(116), S(92), S(900), S(24));
    SetWindowLongPtrW(st->status, GWL_STYLE,
                      GetWindowLongPtrW(st->status, GWL_STYLE) | SS_NOTIFY);
    st->activityPanel = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
                                        WS_CHILD,
                                        S(500), S(260), S(400), S(96), hwnd,
                                        win32_control_id(IDC_ACTIVITY_PANEL), GetModuleHandleW(nullptr), nullptr);
    st->activityLabel = CreateWindowExW(0, L"STATIC", L"",
                                        WS_CHILD | SS_LEFT | SS_CENTERIMAGE,
                                        S(568), S(278), S(312), S(26), hwnd,
                                        win32_control_id(IDC_ACTIVITY_LABEL), GetModuleHandleW(nullptr), nullptr);
    st->progress = CreateWindowExW(0, PROGRESS_CLASSW, L"",
                                   WS_CHILD | PBS_SMOOTH | PBS_MARQUEE,
                                   S(568), S(315), S(312), S(16), hwnd,
                                   win32_control_id(IDC_PROGRESS), GetModuleHandleW(nullptr), nullptr);
    st->tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                  WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                  CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                  hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (st->tooltip) {
        SetWindowPos(st->tooltip, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SendMessageW(st->tooltip, TTM_SETMAXTIPWIDTH, 0, S(420));
        addTooltip(st->tooltip, st->query, L"按当前筛选条件查询全部已签收条码；输入框中可按 Enter 查询。");
        addTooltip(st->tooltip, st->refresh, L"使用当前条件重新查询并刷新全部结果。");
        addTooltip(st->tooltip, st->exportExcel, L"将当前已加载、已排序的全部结果导出为 Excel 工作簿，不会只导出可见行。");
        addTooltip(st->tooltip, st->room, L"可选择全部或多个检验专业组；F4 或 Alt+向下键可打开。");
        addTooltip(st->tooltip, st->machineStatus, L"可组合选择未上机、未审核、未发送或发送完成状态。");
        addTooltip(st->tooltip, st->list, L"右键复制完整单元格内容；双击已上机记录可跳转常规报告。");
        addTooltip(st->tooltip, st->progress, L"显示当前查询或全量导出的执行进度。");
        addTooltip(st->tooltip, st->status, L"显示当前操作状态；红色错误提示可点击查看完整详情。");
    }

    fillStaticCombos(st);
    setToday(st->startDate, false);
    setToday(st->endDate, true);
    loadRooms(st);
    st->selectedMachineStatuses = {"已上机未审核"};
    updateFilterButtonText(st);
    search::apply_font_to_children(hwnd, st->ctx.uiFont);
}

void layout(HWND hwnd, BarcodeState* st) {
    if (!st) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const float s = search::dpi_scale_factor(hwnd);
    auto S = [s](int v) { return static_cast<int>(v * s); };
    const int listTop = S(120);
    const int clientW = static_cast<int>(rc.right);
    const int clientH = static_cast<int>(rc.bottom);
    MoveWindow(st->status, S(116), S(92),
               (std::max)(S(300), clientW - S(128)), S(24), TRUE);
    MoveWindow(st->list, S(4), listTop,
               (std::max)(S(300), clientW - S(8)),
               (std::max)(S(160), clientH - listTop - S(4)), TRUE);
    positionActivity(hwnd, st);
}

const std::string& barcodeSortValue(const search::BarcodeQueryRow& row, int col) {
    static const std::string empty;
    switch (col) {
        case 0: return row.sample_no;
        case 1: return row.emergency;
        case 2: return row.barcode;
        case 3: return row.reg_no;
        case 4: return row.type_name;
        case 5: return row.name;
        case 6: return row.sex;
        case 7: return row.dept_name;
        case 8: return row.bed_no;
        case 9: return row.receiver;
        case 10: return row.receive_time;
        case 11: return row.order_text;
        case 12: return row.sample_name;
        case 13: return row.tester;
        case 14: return row.reviewer;
        case 15: return row.review_time;
        case 16: return row.review_elapsed;
        case 17: return row.machine_status;
        case 18: return row.fee;
        case 19: return row.request_doctor;
        case 20: return row.status;
        case 21: return row.note;
        case 22: return row.reason;
        case 23: return row.submitter;
        case 24: return row.submit_time;
        case 25: return row.request_time;
        case 26: return row.cancel_time;
        case 27: return row.cancel_operator;
        case 28: return row.hzid;
        default: return empty;
    }
}

bool parseDouble(const std::string& text, double& value) {
    const std::string trimmed = search::trim(text);
    if (trimmed.empty()) return false;
    char* end = nullptr;
    value = std::strtod(trimmed.c_str(), &end);
    return end && *end == '\0';
}

int compareBarcodeSortValue(const search::BarcodeQueryRow& a,
                            const search::BarcodeQueryRow& b,
                            int col) {
    const std::string left = search::trim(barcodeSortValue(a, col));
    const std::string right = search::trim(barcodeSortValue(b, col));
    if (col == REVIEW_ELAPSED_COLUMN) {
        const bool leftValid = a.review_elapsed_seconds >= 0;
        const bool rightValid = b.review_elapsed_seconds >= 0;
        if (leftValid && rightValid) {
            if (a.review_elapsed_seconds < b.review_elapsed_seconds) return -1;
            if (a.review_elapsed_seconds > b.review_elapsed_seconds) return 1;
            return 0;
        }
        if (leftValid != rightValid) return leftValid ? -1 : 1;
    }
    if (col == FEE_COLUMN) {
        double ln = 0.0, rn = 0.0;
        const bool lok = parseDouble(left, ln);
        const bool rok = parseDouble(right, rn);
        if (lok && rok) {
            if (ln < rn) return -1;
            if (ln > rn) return 1;
            return 0;
        }
        if (lok != rok) return lok ? -1 : 1;
    }
    if (left < right) return -1;
    if (left > right) return 1;
    return 0;
}

std::string barcodeRowKey(const search::BarcodeQueryRow& row) {
    return row.barcode + "|" + row.sample_no + "|" + row.reg_no + "|" + row.order_text;
}

void updateActionButtons(BarcodeState* st) {
    if (!st) return;
    const bool idle = !st->querying && !st->exporting;
    if (st->query) EnableWindow(st->query, idle);
    if (st->refresh) EnableWindow(st->refresh, idle);
    if (st->exportExcel) EnableWindow(st->exportExcel, idle && !st->rows.empty());
}

std::string barcodeGroupKey(const search::BarcodeQueryRow& row) {
    return search::trim(row.barcode);
}

bool sameNonEmptyBarcodeGroup(const search::BarcodeQueryRow& left,
                              const search::BarcodeQueryRow& right) {
    const std::string leftKey = barcodeGroupKey(left);
    return !leftKey.empty() && leftKey == barcodeGroupKey(right);
}

const search::BarcodeQueryRow* displayedRow(const BarcodeState* st, int displayIndex) {
    if (!st || displayIndex < 0 ||
        displayIndex >= static_cast<int>(st->displayOrder.size())) {
        return nullptr;
    }
    const size_t rowIndex = st->displayOrder[static_cast<size_t>(displayIndex)];
    if (rowIndex >= st->rows.size()) return nullptr;
    return &st->rows[rowIndex];
}

const std::string& displayedCellValue(const BarcodeState* st, int displayIndex, int column) {
    static const std::string empty;
    const auto* row = displayedRow(st, displayIndex);
    if (!row) return empty;
    if (column >= FIRST_SHARED_BARCODE_COLUMN && column <= LAST_SHARED_BARCODE_COLUMN &&
        displayIndex > 0) {
        const auto* previous = displayedRow(st, displayIndex - 1);
        if (previous && sameNonEmptyBarcodeGroup(*previous, *row)) return empty;
    }
    return barcodeSortValue(*row, column);
}

void groupBarcodeRowsForDisplay(BarcodeState* st) {
    if (!st) return;
    struct BarcodeRowGroup {
        std::vector<size_t> rowIndexes;
    };

    std::vector<BarcodeRowGroup> groups;
    groups.reserve(st->displayOrder.size());
    std::unordered_map<std::string, size_t> groupIndexes;
    groupIndexes.reserve(st->displayOrder.size());

    for (const size_t rowIndex : st->displayOrder) {
        const auto& row = st->rows[rowIndex];
        const std::string barcode = barcodeGroupKey(row);
        if (barcode.empty()) {
            BarcodeRowGroup group;
            group.rowIndexes.push_back(rowIndex);
            groups.push_back(std::move(group));
            continue;
        }

        const auto inserted = groupIndexes.emplace(barcode, groups.size());
        if (inserted.second) {
            groups.push_back(BarcodeRowGroup{});
        }
        groups[inserted.first->second].rowIndexes.push_back(rowIndex);
    }

    st->displayOrder.clear();
    for (auto& group : groups) {
        for (const size_t rowIndex : group.rowIndexes) {
            st->displayOrder.push_back(rowIndex);
        }
    }
}

void sortBarcodeRowsForDisplay(BarcodeState* st) {
    if (!st) return;
    st->displayOrder.resize(st->rows.size());
    for (size_t i = 0; i < st->rows.size(); ++i) st->displayOrder[i] = i;
    if (st->listSortColumn >= FIRST_DATA_COLUMN && st->listSortColumn <= LAST_DATA_COLUMN) {
        const int col = st->listSortColumn;
        const bool ascending = st->listSortAscending;
        std::stable_sort(st->displayOrder.begin(), st->displayOrder.end(),
                         [st, col, ascending](const size_t a, const size_t b) {
                             const int cmp = compareBarcodeSortValue(st->rows[a], st->rows[b], col);
                             return ascending ? cmp < 0 : cmp > 0;
                         });
    }
    groupBarcodeRowsForDisplay(st);
}

void presentRows(BarcodeState* st) {
    if (!st || !st->list) return;
    SendMessageW(st->list, WM_SETREDRAW, FALSE, 0);
    ListView_SetItemCountEx(st->list, static_cast<int>(st->displayOrder.size()),
                            LVSICF_NOINVALIDATEALL);
    SendMessageW(st->list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(st->list, nullptr, TRUE);
}

void sortRowsByColumn(BarcodeState* st, int col) {
    if (!st || !st->list || st->rows.empty() || st->exporting ||
        col < FIRST_DATA_COLUMN || col > LAST_DATA_COLUMN) return;
    if (st->listSortColumn == col) {
        st->listSortAscending = !st->listSortAscending;
    } else {
        st->listSortColumn = col;
        st->listSortAscending = true;
    }

    std::string selectedKey;
    const int selected = ListView_GetNextItem(st->list, -1, LVNI_SELECTED);
    if (const auto* row = displayedRow(st, selected)) {
        selectedKey = barcodeRowKey(*row);
    }

    sortBarcodeRowsForDisplay(st);
    presentRows(st);

    if (!selectedKey.empty()) {
        for (int i = 0; i < static_cast<int>(st->displayOrder.size()); ++i) {
            const auto* row = displayedRow(st, i);
            if (row && barcodeRowKey(*row) == selectedKey) {
                ListView_SetItemState(st->list, i, LVIS_SELECTED | LVIS_FOCUSED,
                                      LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(st->list, i, FALSE);
                break;
            }
        }
    }
}

void openRegularReportForRow(HWND owner, BarcodeState* st, int index) {
    const auto* displayed = displayedRow(st, index);
    if (!displayed) return;
    const auto& row = *displayed;
    if (search::trim(row.report_no).empty() || search::trim(row.machine_code).empty() ||
        search::trim(row.inspect_date).empty()) {
        MessageBoxW(owner, L"该条码为已签收未上机，无法跳转到常规报告。", WINDOW_TITLE, MB_ICONINFORMATION);
        return;
    }

    auto* target = new RegularReportOpenTarget;
    target->rep_no = search::trim(row.report_no);
    target->oper_no = search::trim(row.sample_no);
    target->inspect_date = search::trim(row.inspect_date);
    target->mach_code = search::trim(row.machine_code);
    target->mach_name = search::trim(row.machine_name);
    target->room_code = search::trim(row.room_code);

    HWND regular = create_regular_report_module(st->ctx);
    if (!regular || !PostMessageW(regular, WM_REGULAR_OPEN_REPORT, 0, reinterpret_cast<LPARAM>(target))) {
        delete target;
        MessageBoxW(owner, L"常规报告页面打开失败。", WINDOW_TITLE, MB_ICONERROR);
    }
}

void showCellContextMenu(HWND hwnd, BarcodeState* st) {
    if (!st || !st->list) return;
    POINT screenPt{};
    GetCursorPos(&screenPt);
    POINT listPt = screenPt;
    ScreenToClient(st->list, &listPt);

    LVHITTESTINFO hit{};
    hit.pt = listPt;
    const int row = ListView_SubItemHitTest(st->list, &hit);
    if (row < 0 || row >= static_cast<int>(st->displayOrder.size()) ||
        hit.iSubItem < FIRST_DATA_COLUMN || hit.iSubItem > LAST_DATA_COLUMN) {
        return;
    }

    ListView_SetItemState(st->list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(st->list, row, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);

    const auto* displayed = displayedRow(st, row);
    if (!displayed) return;
    const std::wstring text = search::utf8_to_wide(barcodeSortValue(*displayed, hit.iSubItem));
    const std::wstring menuLabel = search::copy_menu_label(text);

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, IDM_COPY_CELL, menuLabel.c_str());
    const UINT command = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                        screenPt.x, screenPt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (command != IDM_COPY_CELL) return;

    if (copyTextToClipboard(hwnd, text)) {
        setStatus(st, L"已复制单元格：" + std::wstring(BARCODE_COLUMNS[hit.iSubItem].title));
    } else {
        showAlert(st, L"复制单元格失败，请稍后重试。");
    }
}

search::BarcodeQueryFilters collectFilters(BarcodeState* st) {
    search::BarcodeQueryFilters f;
    f.connection_string = search::wide_to_utf8(search::build_connection_string_w(st->ctx.dbSettings));
    const auto dateField = comboText(st->dateField);
    f.date_field = dateField == "签收日期" ? "Receive" : dateField == "上机日期" ? "Machine" : "Apply";
    f.start_date = dateText(st->startDate);
    f.end_date = dateText(st->endDate);
    f.barcode = textOf(st->barcode);
    f.patient_name = textOf(st->name);
    f.reg_no = textOf(st->regNo);
    f.campus = comboText(st->campus);
    if (!allStatusesSelected(st)) {
        f.machine_statuses = st->selectedMachineStatuses;
    }
    if (f.machine_statuses.empty()) f.machine_status = "全部";
    f.canceled = Button_GetCheck(st->canceled) == BST_CHECKED;
    if (!allRoomsSelected(st)) {
        f.room_codes = st->selectedRoomCodes;
    }

    return f;
}

void runQuery(HWND hwnd, BarcodeState* st) {
    if (!st || st->querying || st->exporting) return;
    if (search::build_connection_string_w(st->ctx.dbSettings).empty()) {
        MessageBoxW(hwnd, L"请先在系统设置中配置数据库连接。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }
    auto filters = collectFilters(st);
    st->querying = true;
    updateActionButtons(st);
    setStatus(st, L"正在查询...");
    showQueryActivity(hwnd, st);

    const bool queued = st->queryTask.start<BarcodeQueryResult>(
        [filters] {
            BarcodeQueryResult result;
            result.ok = search::query_barcodes(
                filters, result.rows, result.error,
                [](const std::string& message) {
                    if (message.rfind("barcode query timing:", 0) == 0) {
                        LOG_DEBUG(message);
                    } else if (message.rfind("barcode employee dictionary unavailable:", 0) == 0) {
                        LOG_WARN(message);
                    }
                });
            return result;
        },
        [hwnd](std::optional<BarcodeQueryResult> result, std::exception_ptr error) {
            auto* state = reinterpret_cast<BarcodeState*>(GetPropW(hwnd, PROP_STATE));
            if (!state) return;
            if (error || !result) {
                state->querying = false;
                hideActivity(hwnd, state);
                showAlert(state, L"查询失败：后台任务异常");
                updateActionButtons(state);
                LOG_ERROR("Barcode query task failed");
                return;
            }
            finishQuery(hwnd, state,
                std::make_unique<BarcodeQueryResult>(std::move(*result)));
        });
    if (!queued) {
        st->querying = false;
        hideActivity(hwnd, st);
        showAlert(st, L"无法启动后台查询任务。");
        updateActionButtons(st);
    }
}

void finishQuery(HWND hwnd, BarcodeState* st, std::unique_ptr<BarcodeQueryResult> result) {
    st->querying = false;
    hideActivity(hwnd, st);
    if (!result->ok) {
        showAlert(st, L"查询失败：" + search::utf8_to_wide(result->error));
        updateActionButtons(st);
        return;
    }
    st->rows = std::move(result->rows);
    const auto sort_started = std::chrono::steady_clock::now();
    sortBarcodeRowsForDisplay(st);
    const auto list_started = std::chrono::steady_clock::now();
    presentRows(st);
    const auto list_finished = std::chrono::steady_clock::now();
    const auto sort_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        list_started - sort_started).count();
    const auto list_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        list_finished - list_started).count();
    LOG_DEBUG("barcode UI timing: sort_ms=" + std::to_string(sort_ms) +
              ", listview_ms=" + std::to_string(list_ms) +
              ", rows=" + std::to_string(st->rows.size()));
    updateActionButtons(st);
    setStatus(st, L"查询完成：院区 " + search::utf8_to_wide(comboText(st->campus)) +
              L"，专业组 " + roomSummary(st) +
              L"，上机状态 " + statusSummary(st) +
              L"，共 " + std::to_wstring(st->rows.size()) + L" 条。");
}

void exportRowsXlsx(HWND hwnd, BarcodeState* st) {
    if (!st || st->querying || st->exporting) return;
    if (st->rows.empty()) {
        showAlert(st, L"当前没有可导出的条码记录。");
        return;
    }

    std::wstring path;
    if (!chooseExportPath(hwnd, st, path)) return;

    st->exporting = true;
    updateActionButtons(st);
    setStatus(st, L"正在导出全部 " + std::to_wstring(st->displayOrder.size()) + L" 条记录...");
    showExportActivity(hwnd, st, st->displayOrder.size());

    const std::vector<size_t> exportOrder = st->displayOrder;
    const std::vector<search::BarcodeQueryRow> exportRows = st->rows;
    const bool queued = st->exportTask.start<BarcodeExportResult>(
        [hwnd, path, exportOrder, exportRows](app::WindowTaskContext task) {
            const auto started = std::chrono::steady_clock::now();
            BarcodeExportResult result;
            result.path = path;
            const std::wstring temporaryPath = path + L".lis-export.tmp";
            DeleteFileW(temporaryPath.c_str());

            FILE* file = nullptr;
            try {
#ifdef _MSC_VER
                _wfopen_s(&file, temporaryPath.c_str(), L"wb");
#else
                file = _wfopen(temporaryPath.c_str(), L"wb");
#endif
                bool writeOk = file != nullptr;
                if (!file) {
                    result.error = L"导出文件创建失败，请确认目标位置可写。";
                } else {
                    std::vector<std::string> headers;
                    headers.reserve(sizeof(BARCODE_COLUMNS) / sizeof(BARCODE_COLUMNS[0]));
                    for (const auto& column : BARCODE_COLUMNS) {
                        if (column.index < FIRST_DATA_COLUMN || column.index > LAST_DATA_COLUMN) continue;
                        headers.push_back(search::wide_to_utf8(column.title));
                    }
                    std::string exportError;
                    writeOk = search::write_xlsx(
                        file, "已签收条码", headers, exportOrder.size(),
                        [&exportRows, &exportOrder](size_t row, size_t column) -> std::string {
                            if (row >= exportOrder.size()) return {};
                            const size_t rowIndex = exportOrder[row];
                            if (rowIndex >= exportRows.size()) return {};
                            return barcodeSortValue(exportRows[rowIndex],
                                                    static_cast<int>(column) + FIRST_DATA_COLUMN);
                        },
                        [task]() { return task.cancelled(); },
                        [task, hwnd](size_t completed, size_t total) {
                            task.post([hwnd, completed, total] {
                                auto* state = reinterpret_cast<BarcodeState*>(
                                    GetPropW(hwnd, PROP_STATE));
                                if (!state || !state->exporting) return;
                                if (state->progress) {
                                    SendMessageW(state->progress, PBM_SETPOS,
                                        static_cast<WPARAM>((std::min)(
                                            completed, static_cast<size_t>(INT_MAX))), 0);
                                }
                                setStatus(state, L"正在导出全部记录：" +
                                    std::to_wstring(completed) + L" / " +
                                    std::to_wstring(total) + L"...");
                            });
                        },
                        result.rowCount, result.canceled, exportError);
                    if (fclose(file) != 0 && writeOk) {
                        writeOk = false;
                        exportError = "failed to flush XLSX file";
                    }
                    file = nullptr;
                    if (!writeOk && !result.canceled) {
                        result.error = L"生成 Excel 工作簿失败：" +
                                       search::utf8_to_wide(exportError);
                    }
                }

                if (result.canceled) {
                    DeleteFileW(temporaryPath.c_str());
                } else if (!writeOk) {
                    DeleteFileW(temporaryPath.c_str());
                    if (result.error.empty()) result.error = L"写入导出文件失败。";
                } else if (!MoveFileExW(temporaryPath.c_str(), path.c_str(),
                                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    DeleteFileW(temporaryPath.c_str());
                    result.error = L"导出文件保存失败。";
                } else {
                    result.ok = true;
                }
            } catch (const std::exception& ex) {
                if (file) fclose(file);
                DeleteFileW(temporaryPath.c_str());
                result.error = L"导出失败：" + search::utf8_to_wide(ex.what());
            } catch (...) {
                if (file) fclose(file);
                DeleteFileW(temporaryPath.c_str());
                result.error = L"导出过程中发生未知错误。";
            }
            result.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            return result;
        },
        [hwnd](std::optional<BarcodeExportResult> result, std::exception_ptr error) {
            auto* state = reinterpret_cast<BarcodeState*>(GetPropW(hwnd, PROP_STATE));
            if (!state) return;
            state->exporting = false;
            hideActivity(hwnd, state);
            updateActionButtons(state);
            if (error || !result) {
                showAlert(state, L"导出过程中发生后台任务异常。");
                LOG_ERROR("Barcode export task failed");
                return;
            }
            LOG_DEBUG("barcode export timing: elapsed_ms=" +
                      std::to_string(result->elapsedMs) + ", rows=" +
                      std::to_string(result->rowCount));
            if (result->canceled) {
                setStatus(state, L"导出已取消。");
            } else if (!result->ok) {
                showAlert(state, L"导出失败：" + result->error);
            } else {
                setStatus(state, L"已导出全部 " + std::to_wstring(result->rowCount) +
                                  L" 条记录：" + result->path);
            }
        });
    if (!queued) {
        st->exporting = false;
        hideActivity(hwnd, st);
        updateActionButtons(st);
        showAlert(st, L"无法启动导出后台任务。");
    }
}

COLORREF rowColor(const search::BarcodeQueryRow& row) {
    if (row.machine_status == "已签收未上机") return COLOR_NOT_MACHINE;
    if (row.machine_status == "已审核未发送") return COLOR_REVIEWED_NOT_SENT;
    if (row.machine_status == "发送完成") return COLOR_SENT;
    return COLOR_LOADED_NOT_REVIEWED;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<BarcodeState*>(GetPropW(hwnd, PROP_STATE));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* mcs = reinterpret_cast<MDICREATESTRUCTW*>(cs->lpCreateParams);
            st = reinterpret_cast<BarcodeState*>(mcs->lParam);
            if (!st) {
                LOG_ERROR("WM_CREATE: lpCreateParams is null (BarcodeState)");
                return -1;
            }
            SetPropW(hwnd, PROP_STATE, reinterpret_cast<HANDLE>(st));
            st->bgBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
            st->alertBrush = CreateSolidBrush(COLOR_ALERT_BACKGROUND);
            st->activityBrush = CreateSolidBrush(COLOR_ACTIVITY_BACKGROUND);
            createControls(hwnd, st);
            layout(hwnd, st);
            return 0;
        }
        case WM_SIZE:
            layout(hwnd, st);
            return 0;
        case app::WM_APP_FONT_CHANGED:
            if (st && lp) {
                st->ctx.uiFont = reinterpret_cast<HFONT>(lp);
                search::apply_font_to_children(hwnd, st->ctx.uiFont);
                layout(hwnd, st);
                if (st->legend) InvalidateRect(st->legend, nullptr, TRUE);
            }
            return 0;
        case WM_DRAWITEM:
            if (st) {
                auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
                if (dis && (dis->CtlID == IDC_ROOM || dis->CtlID == IDC_MACHINE_STATUS)) {
                    drawDropdownButton(st, dis);
                    return TRUE;
                }
            }
            break;
        case WM_COMMAND:
            if (!st) break;
            switch (LOWORD(wp)) {
                case IDC_CAMPUS:
                    if (HIWORD(wp) == CBN_SELCHANGE) {
                        filterRoomsForCampus(st);
                        updateFilterButtonText(st);
                    }
                    return 0;
                case IDC_ROOM:
                    openRoomDropdown(hwnd, st);
                    return 0;
                case IDC_MACHINE_STATUS:
                    openStatusDropdown(hwnd, st);
                    return 0;
                case IDC_QUERY:
                case IDC_REFRESH:
                    runQuery(hwnd, st);
                    return 0;
                case IDC_EXPORT:
                    exportRowsXlsx(hwnd, st);
                    return 0;
                case IDC_STATUS:
                    if (HIWORD(wp) == STN_CLICKED && !st->lastAlertText.empty()) {
                        MessageBoxW(hwnd, st->lastAlertText.c_str(), L"错误详情",
                                    MB_OK | MB_ICONERROR);
                    }
                    return 0;
            }
            break;
        case WM_NOTIFY:
            if (st) {
                auto* nm = reinterpret_cast<NMHDR*>(lp);
                if (nm->idFrom == IDC_LIST && nm->code == LVN_GETDISPINFOW) {
                    auto* info = reinterpret_cast<NMLVDISPINFOW*>(lp);
                    if ((info->item.mask & LVIF_TEXT) && info->item.pszText &&
                        info->item.cchTextMax > 0) {
                        const auto text = search::utf8_to_wide(
                            displayedCellValue(st, info->item.iItem, info->item.iSubItem));
                        lstrcpynW(info->item.pszText, text.c_str(), info->item.cchTextMax);
                    }
                    return 0;
                }
                if (nm->idFrom == IDC_LIST && nm->code == LVN_COLUMNCLICK) {
                    auto* clicked = reinterpret_cast<NMLISTVIEW*>(lp);
                    sortRowsByColumn(st, clicked->iSubItem);
                    return 0;
                }
                if (nm->idFrom == IDC_LIST && nm->code == NM_RCLICK) {
                    showCellContextMenu(hwnd, st);
                    return 0;
                }
                if (nm->idFrom == IDC_LIST && nm->code == NM_DBLCLK) {
                    auto* item = reinterpret_cast<NMITEMACTIVATE*>(lp);
                    openRegularReportForRow(hwnd, st, item ? item->iItem : -1);
                    return 0;
                }
                if (nm->idFrom == IDC_LIST && nm->code == NM_CUSTOMDRAW) {
                    auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                    if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                    if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                        const int idx = static_cast<int>(cd->nmcd.dwItemSpec);
                        if (const auto* row = displayedRow(st, idx)) {
                            cd->clrTextBk = rowColor(*row);
                            cd->clrText = RGB(0, 0, 0);
                        }
                        return CDRF_NEWFONT;
                    }
                }
            }
            break;
        case WM_CTLCOLORSTATIC:
            if (st) {
                HDC hdc = reinterpret_cast<HDC>(wp);
                HWND control = reinterpret_cast<HWND>(lp);
                if (control == st->status && st->statusIsAlert) {
                    SetTextColor(hdc, COLOR_ALERT_TEXT);
                    SetBkColor(hdc, COLOR_ALERT_BACKGROUND);
                    return reinterpret_cast<LRESULT>(st->alertBrush);
                }
                if (control == st->activityPanel || control == st->activityLabel) {
                    SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
                    SetBkColor(hdc, COLOR_ACTIVITY_BACKGROUND);
                    return reinterpret_cast<LRESULT>(st->activityBrush);
                }
                SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
                return reinterpret_cast<LRESULT>(st->bgBrush);
            }
            break;
        case WM_DESTROY:
            RemovePropW(hwnd, PROP_STATE);
            if (st) {
                st->queryTask.cancel();
                st->exportTask.cancel();
                if (st->bgBrush) DeleteObject(st->bgBrush);
                if (st->alertBrush) DeleteObject(st->alertBrush);
                if (st->activityBrush) DeleteObject(st->activityBrush);
                delete st;
            }
            break;
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND create_barcode_module(const ModuleContext& ctx) {
    if (HWND existing = activate_existing_mdi_child_by_title(ctx.mdiClient, WINDOW_TITLE)) {
        return existing;
    }

    REGISTER_MDI_CHILD_CLASS(ctx.instance, wndProc, WND_CLASS, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));

    auto* st = new BarcodeState;
    st->ctx = ctx;

    MDICREATESTRUCTW mcs{};
    mcs.szClass = WND_CLASS;
    mcs.szTitle = WINDOW_TITLE;
    mcs.hOwner = ctx.instance;
    mcs.x = mcs.y = mcs.cx = mcs.cy = CW_USEDEFAULT;

    mcs.lParam = reinterpret_cast<LPARAM>(st);
    HWND child = reinterpret_cast<HWND>(SendMessageW(ctx.mdiClient, WM_MDICREATE, 0, reinterpret_cast<LPARAM>(&mcs)));
    if (child) {
        SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    } else {
        delete st;
    }
    return child;
}

#endif
