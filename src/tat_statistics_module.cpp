#include "tat_statistics_module.h"

#ifdef _WIN32

#include "app_settings_io.h"
#include "main_app.h"
#include "page_feedback.h"
#include "regular_report_module.h"
#include "resource.h"
#include "search_core.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"
#include "xlsx_writer.h"

#include <commctrl.h>
#include <commdlg.h>
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const wchar_t* WND_CLASS = L"TatStatisticsModuleChild";
constexpr const wchar_t* DIALOG_CLASS = L"TatThresholdSettingsWindow";
constexpr const wchar_t* WINDOW_TITLE = L"检验周转时间统计";
constexpr const wchar_t* PROP_STATE = L"TatStatisticsState";
constexpr const wchar_t* PROP_DIALOG_STATE = L"TatThresholdDialogState";
constexpr const wchar_t* CONFIG_SECTION = L"TatStatistics";
constexpr UINT WM_TAT_LOADED = WM_APP + 0x579;
constexpr UINT WM_TAT_ROOMS_LOADED = WM_APP + 0x57A;
constexpr UINT WM_TAT_EXPORTED = WM_APP + 0x57B;
constexpr UINT_PTR TIMER_REFRESH = 1;

constexpr COLORREF COLOR_NORMAL = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF COLOR_OVERTIME = RGB(0xFF, 0xF5, 0x7A);
constexpr COLORREF COLOR_ABNORMAL = RGB(0xFF, 0xCD, 0xD2);

enum ControlId {
    IDC_START = 7401,
    IDC_END = 7402,
    IDC_ROOM = 7403,
    IDC_PATIENT_TYPE = 7404,
    IDC_EMERGENCY = 7405,
    IDC_DEPARTMENT = 7406,
    IDC_ORDER = 7407,
    IDC_TAT_STATUS = 7408,
    IDC_QUERY = 7409,
    IDC_RESET = 7410,
    IDC_EXPORT = 7411,
    IDC_THRESHOLDS = 7412,
    IDC_SUMMARY = 7413,
    IDC_DETAILS = 7414,
    IDC_STATUS = 7415,
    IDC_RESERVED_CYCLE = 7416,
    IDC_RESERVED_CALC = 7417,
    IDC_RESERVED_HISTORY = 7418,
    IDC_RESERVED_PRINT = 7419,
};

enum DetailColumn {
    COL_PATIENT_TYPE,
    COL_BARCODE,
    COL_REG_NO,
    COL_NAME,
    COL_SEX,
    COL_DIAGNOSIS,
    COL_BED_NO,
    COL_AGE,
    COL_SAMPLE,
    COL_DEPARTMENT,
    COL_ROOM,
    COL_ORDER,
    COL_COLLECTION_TIME,
    COL_COLLECTION_RECEIVE,
    COL_RECEIVE_TIME,
    COL_RECEIVE_MACHINE,
    COL_MACHINE_TIME,
    COL_RECEIVE_REVIEW,
    COL_RECEIVER,
    COL_REVIEWER,
    COL_REVIEW_TIME,
    COL_COLLECTION_REVIEW,
    COL_WORKFLOW_STATUS,
    COL_TAT_STATUS,
    DETAIL_COLUMN_COUNT,
};

struct ListColumn { const wchar_t* title; int width; };

constexpr ListColumn SUMMARY_COLUMNS[] = {
    {L"条码总数", 150}, {L"正常", 130}, {L"超时", 130},
    {L"时间异常", 150}, {L"等待上机", 150}, {L"等待审核", 150},
};

constexpr ListColumn DETAIL_COLUMNS[] = {
    {L"病人类型", 80}, {L"条码号", 125}, {L"病人号", 130}, {L"姓名", 85},
    {L"性别", 50}, {L"诊断", 150}, {L"床号", 65}, {L"年龄", 60},
    {L"标本", 90}, {L"申请科室", 150}, {L"专业组", 120}, {L"医嘱项目", 220},
    {L"采集时间", 145}, {L"采集-接收", 155}, {L"接收时间", 145},
    {L"接收-上机", 155}, {L"上机时间", 145}, {L"接收-发送", 165},
    {L"签收人", 90}, {L"审核人", 90}, {L"发送时间", 145},
    {L"采集-发送", 165}, {L"流程状态", 125}, {L"TAT状态", 85},
};
static_assert(std::size(DETAIL_COLUMNS) == DETAIL_COLUMN_COUNT);

using DetailRow = search::TatStatDetailRow;

struct TatState {
    ModuleContext ctx;
    HWND start = nullptr;
    HWND end = nullptr;
    HWND room = nullptr;
    HWND patientType = nullptr;
    HWND emergency = nullptr;
    HWND department = nullptr;
    HWND order = nullptr;
    HWND tatStatus = nullptr;
    HWND query = nullptr;
    HWND reset = nullptr;
    HWND exportExcel = nullptr;
    HWND thresholdsButton = nullptr;
    HWND summary = nullptr;
    HWND details = nullptr;
    HWND status = nullptr;
    search::PageFeedback feedback;
    HBRUSH bgBrush = nullptr;
    bool querying = false;
    bool exporting = false;
    bool hasLoaded = false;
    int sortColumn = COL_RECEIVE_TIME;
    bool sortAscending = false;
    search::TatThresholds thresholds;
    search::TatStatSummary statSummary;
    std::vector<search::RoomOption> rooms;
    std::vector<DetailRow> allRows;
    std::vector<DetailRow> rows;
    std::string loadedStart;
    std::string loadedEnd;
};

struct QueryResult {
    bool ok = false;
    std::string error;
    std::string start;
    std::string end;
    search::TatStatSummary summary;
    std::vector<DetailRow> rows;
};

struct RoomResult {
    bool ok = false;
    std::vector<search::RoomOption> rows;
};

struct ExportResult {
    bool ok = false;
    std::wstring path;
    std::string error;
};

struct ThresholdDialogState {
    search::TatThresholds values;
    bool accepted = false;
};

int S(HWND hwnd, int value) {
    return static_cast<int>(value * search::dpi_scale_factor(hwnd));
}

HWND label(HWND parent, const wchar_t* text) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_RIGHT,
                           0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND edit(HWND parent, int id) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                           0, 0, 0, 0, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

HWND combo(HWND parent, int id) {
    return CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                           CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 0, 0, parent,
                           win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

HWND dateTimePicker(HWND parent, int id) {
    HWND control = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_VISIBLE |
        WS_TABSTOP | DTS_SHORTDATECENTURYFORMAT, 0, 0, 0, 0, parent,
        win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
    DateTime_SetFormat(control, L"yyyy-MM-dd HH:mm");
    return control;
}

void addCombo(HWND control, const wchar_t* text) {
    SendMessageW(control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
}

std::wstring comboText(HWND control) {
    const int index = static_cast<int>(SendMessageW(control, CB_GETCURSEL, 0, 0));
    if (index < 0) return {};
    wchar_t text[128]{};
    SendMessageW(control, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(text));
    return text;
}

std::wstring controlText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    if (length > 0) GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<size_t>(length));
    return value;
}

std::string dateTimeText(HWND control) {
    SYSTEMTIME value{};
    if (DateTime_GetSystemtime(control, &value) != GDT_VALID) return {};
    char text[32]{};
    sprintf_s(text, "%04u-%02u-%02u %02u:%02u:%02u", value.wYear, value.wMonth,
              value.wDay, value.wHour, value.wMinute, value.wSecond);
    return text;
}

std::string currentTimeText() {
    SYSTEMTIME value{};
    GetLocalTime(&value);
    char text[32]{};
    sprintf_s(text, "%04u-%02u-%02u %02u:%02u:%02u", value.wYear, value.wMonth,
              value.wDay, value.wHour, value.wMinute, value.wSecond);
    return text;
}

void setToday(HWND start, HWND end) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    SYSTEMTIME first = now;
    first.wHour = first.wMinute = first.wSecond = first.wMilliseconds = 0;
    now.wHour = 23;
    now.wMinute = 59;
    now.wSecond = now.wMilliseconds = 0;
    DateTime_SetSystemtime(start, GDT_VALID, &first);
    DateTime_SetSystemtime(end, GDT_VALID, &now);
}

search::TatThresholds loadThresholds() {
    search::TatThresholds values;
    values.collection_to_receive_minutes = search::load_module_int(CONFIG_SECTION, L"CollectionToReceiveMinutes", 30);
    values.receive_to_machine_minutes = search::load_module_int(CONFIG_SECTION, L"ReceiveToMachineMinutes", 30);
    values.receive_to_review_minutes = search::load_module_int(CONFIG_SECTION, L"ReceiveToReviewMinutes", 180);
    values.collection_to_review_minutes = search::load_module_int(CONFIG_SECTION, L"CollectionToReviewMinutes", 240);
    return values;
}

void saveThresholds(const search::TatThresholds& values) {
    search::save_module_int(CONFIG_SECTION, L"CollectionToReceiveMinutes", values.collection_to_receive_minutes);
    search::save_module_int(CONFIG_SECTION, L"ReceiveToMachineMinutes", values.receive_to_machine_minutes);
    search::save_module_int(CONFIG_SECTION, L"ReceiveToReviewMinutes", values.receive_to_review_minutes);
    search::save_module_int(CONFIG_SECTION, L"CollectionToReviewMinutes", values.collection_to_review_minutes);
}

LRESULT CALLBACK thresholdDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<ThresholdDialogState*>(GetPropW(hwnd, PROP_DIALOG_STATE));
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        state = reinterpret_cast<ThresholdDialogState*>(cs->lpCreateParams);
        SetPropW(hwnd, PROP_DIALOG_STATE, state);
        const wchar_t* labels[] = {L"采集 → 接收（分钟）：", L"接收 → 上机（分钟）：",
                                   L"接收 → 发送（分钟）：", L"采集 → 发送（分钟）："};
        const int values[] = {state->values.collection_to_receive_minutes,
                              state->values.receive_to_machine_minutes,
                              state->values.receive_to_review_minutes,
                              state->values.collection_to_review_minutes};
        for (int i = 0; i < 4; ++i) {
            CreateWindowExW(0, L"STATIC", labels[i], WS_CHILD | WS_VISIBLE | SS_RIGHT,
                18, 20 + i * 38, 170, 24, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            HWND field = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", std::to_wstring(values[i]).c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_RIGHT,
                196, 18 + i * 38, 100, 25, hwnd, win32_control_id(7501 + i),
                GetModuleHandleW(nullptr), nullptr);
            SendMessageW(field, EM_SETLIMITTEXT, 5, 0);
        }
        CreateWindowExW(0, L"STATIC", L"任一阶段超过阈值，整条条码记录即判为超时。",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 174, 330, 22, hwnd, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        search::create_button(hwnd, IDOK, L"保存", 188, 208, 76, 28);
        search::create_button(hwnd, IDCANCEL, L"取消", 274, 208, 76, 28);
        search::apply_font_to_children(hwnd, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
        return 0;
    }
    if (msg == WM_COMMAND && LOWORD(wp) == IDOK && state) {
        int values[4]{};
        for (int i = 0; i < 4; ++i) {
            wchar_t text[16]{};
            GetDlgItemTextW(hwnd, 7501 + i, text, static_cast<int>(std::size(text)));
            values[i] = _wtoi(text);
            if (values[i] <= 0 || values[i] > 10080) {
                MessageBoxW(hwnd, L"阈值必须为 1 至 10080 分钟之间的整数。", L"阈值设置", MB_ICONWARNING);
                SetFocus(GetDlgItem(hwnd, 7501 + i));
                return 0;
            }
        }
        state->values = {values[0], values[1], values[2], values[3]};
        state->accepted = true;
        DestroyWindow(hwnd);
        return 0;
    }
    if ((msg == WM_COMMAND && LOWORD(wp) == IDCANCEL) || msg == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        RemovePropW(hwnd, PROP_DIALOG_STATE);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool showThresholdDialog(HWND owner, search::TatThresholds& values) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = thresholdDialogProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = DIALOG_CLASS;
        RegisterClassW(&wc);
        registered = true;
    }
    ThresholdDialogState state;
    state.values = values;
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int width = 390;
    const int height = 290;
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, DIALOG_CLASS, L"TAT 阈值设置",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, width, height, owner, nullptr,
        GetModuleHandleW(nullptr), &state);
    if (!dialog) return false;
    EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    MSG message{};
    bool quit = false;
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (message.message == WM_QUIT) { quit = true; break; }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (quit) PostQuitMessage(static_cast<int>(message.wParam));
    if (state.accepted) values = state.values;
    return state.accepted;
}

std::string cellValue(const DetailRow& row, int column) {
    switch (column) {
        case COL_PATIENT_TYPE: return row.patient_type;
        case COL_BARCODE: return row.barcode;
        case COL_REG_NO: return row.reg_no;
        case COL_NAME: return row.name;
        case COL_SEX: return row.sex;
        case COL_DIAGNOSIS: return row.diagnosis;
        case COL_BED_NO: return row.bed_no;
        case COL_AGE: return row.age;
        case COL_SAMPLE: return row.sample_name;
        case COL_DEPARTMENT: return row.department_name;
        case COL_ROOM: return row.room_name;
        case COL_ORDER: return row.order_text;
        case COL_COLLECTION_TIME: return row.collection_time.empty() ? "-" : row.collection_time;
        case COL_COLLECTION_RECEIVE: return row.collection_to_receive.empty() ? "-" : row.collection_to_receive;
        case COL_RECEIVE_TIME: return row.receive_time;
        case COL_RECEIVE_MACHINE: return row.receive_to_machine.empty() ? "-" : row.receive_to_machine;
        case COL_MACHINE_TIME: return row.machine_time.empty() ? "-" : row.machine_time;
        case COL_RECEIVE_REVIEW: return row.receive_to_review.empty() ? "-" : row.receive_to_review;
        case COL_RECEIVER: return row.receiver;
        case COL_REVIEWER: return row.reviewer;
        case COL_REVIEW_TIME: return row.review_time.empty() ? "-" : row.review_time;
        case COL_COLLECTION_REVIEW: return row.collection_to_review.empty() ? "-" : row.collection_to_review;
        case COL_WORKFLOW_STATUS: return row.workflow_status;
        case COL_TAT_STATUS: return row.tat_status;
        default: return {};
    }
}

long long durationValue(const DetailRow& row, int column) {
    switch (column) {
        case COL_COLLECTION_RECEIVE: return row.collection_to_receive_seconds;
        case COL_RECEIVE_MACHINE: return row.receive_to_machine_seconds;
        case COL_RECEIVE_REVIEW: return row.receive_to_review_seconds;
        case COL_COLLECTION_REVIEW: return row.collection_to_review_seconds;
        default: return -2;
    }
}

void sortRows(TatState* state, int column, bool toggle) {
    if (!state) return;
    if (toggle) {
        if (state->sortColumn == column) state->sortAscending = !state->sortAscending;
        else { state->sortColumn = column; state->sortAscending = true; }
    }
    const int sortColumn = state->sortColumn;
    const bool ascending = state->sortAscending;
    std::stable_sort(state->rows.begin(), state->rows.end(), [=](const DetailRow& lhs, const DetailRow& rhs) {
        const long long leftDuration = durationValue(lhs, sortColumn);
        const long long rightDuration = durationValue(rhs, sortColumn);
        if (leftDuration != -2 || rightDuration != -2) {
            return ascending ? leftDuration < rightDuration : leftDuration > rightDuration;
        }
        const auto left = cellValue(lhs, sortColumn);
        const auto right = cellValue(rhs, sortColumn);
        return ascending ? left < right : left > right;
    });
}

void populateDetails(TatState* state) {
    if (!state || !state->details) return;
    SendMessageW(state->details, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(state->details);
    for (int rowIndex = 0; rowIndex < static_cast<int>(state->rows.size()); ++rowIndex) {
        const auto first = search::utf8_to_wide(cellValue(state->rows[rowIndex], 0));
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = rowIndex;
        item.pszText = const_cast<wchar_t*>(first.c_str());
        ListView_InsertItem(state->details, &item);
        for (int column = 1; column < DETAIL_COLUMN_COUNT; ++column) {
            const auto value = search::utf8_to_wide(cellValue(state->rows[rowIndex], column));
            ListView_SetItemText(state->details, rowIndex, column, const_cast<wchar_t*>(value.c_str()));
        }
    }
    SendMessageW(state->details, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(state->details, nullptr, TRUE);
}

void populateSummary(TatState* state) {
    if (!state || !state->summary) return;
    const int values[] = {state->statSummary.total_count, state->statSummary.normal_count,
        state->statSummary.overtime_count, state->statSummary.time_abnormal_count,
        state->statSummary.waiting_machine_count, state->statSummary.waiting_review_count};
    ListView_DeleteAllItems(state->summary);
    const auto first = std::to_wstring(values[0]);
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = 0;
    item.pszText = const_cast<wchar_t*>(first.c_str());
    ListView_InsertItem(state->summary, &item);
    for (int column = 1; column < static_cast<int>(std::size(values)); ++column) {
        const auto value = std::to_wstring(values[column]);
        ListView_SetItemText(state->summary, 0, column, const_cast<wchar_t*>(value.c_str()));
    }
}

void applyStatusFilter(TatState* state, bool updateStatus = true) {
    if (!state) return;
    const auto selected = comboText(state->tatStatus);
    state->rows.clear();
    for (const auto& row : state->allRows) {
        if (selected == L"正常" && row.tat_status != "正常") continue;
        if (selected == L"超时" && row.tat_status != "超时") continue;
        if (selected == L"时间异常" && row.tat_status != "时间异常") continue;
        state->rows.push_back(row);
    }
    sortRows(state, state->sortColumn, false);
    populateDetails(state);
    EnableWindow(state->exportExcel, state->hasLoaded && !state->rows.empty());
    if (updateStatus && state->hasLoaded) {
        search::set_page_status(state->feedback,
            L"共查询到 " + std::to_wstring(state->statSummary.total_count) +
            L" 个条码，当前显示 " + std::to_wstring(state->rows.size()) + L" 个。正常 " +
            std::to_wstring(state->statSummary.normal_count) + L"，超时 " +
            std::to_wstring(state->statSummary.overtime_count) + L"，时间异常 " +
            std::to_wstring(state->statSummary.time_abnormal_count) + L"。");
    }
}

void setQueryControls(TatState* state, bool enabled) {
    const BOOL value = enabled ? TRUE : FALSE;
    HWND controls[] = {state->start, state->end, state->room, state->patientType, state->emergency,
                       state->department, state->order, state->tatStatus, state->query, state->reset,
                       state->thresholdsButton};
    for (HWND control : controls) EnableWindow(control, value);
}

std::string selectedRoomCode(TatState* state) {
    const int index = static_cast<int>(SendMessageW(state->room, CB_GETCURSEL, 0, 0));
    if (index <= 0 || index - 1 >= static_cast<int>(state->rooms.size())) return {};
    return state->rooms[static_cast<size_t>(index - 1)].room_code;
}

void runQuery(HWND hwnd, TatState* state) {
    if (!state || state->querying) return;
    const auto connection = search::build_connection_string_w(state->ctx.dbSettings);
    if (connection.empty()) {
        MessageBoxW(hwnd, L"请先在系统设置中配置数据库连接。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }
    search::TatStatQuery query;
    query.connection_string = search::wide_to_utf8(connection);
    query.start_time = dateTimeText(state->start);
    query.end_time = dateTimeText(state->end);
    query.room_code = selectedRoomCode(state);
    query.department_keyword = search::wide_to_utf8(controlText(state->department));
    query.order_keyword = search::wide_to_utf8(controlText(state->order));
    query.patient_type = search::wide_to_utf8(comboText(state->patientType));
    query.emergency_only = Button_GetCheck(state->emergency) == BST_CHECKED;
    query.thresholds = state->thresholds;
    query.current_time = currentTimeText();
    if (query.start_time.empty() || query.end_time.empty() || query.start_time > query.end_time) {
        MessageBoxW(hwnd, L"签收开始时间不能晚于结束时间。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }
    state->querying = true;
    setQueryControls(state, false);
    EnableWindow(state->exportExcel, FALSE);
    search::set_page_status(state->feedback, L"正在查询检验周转时间...");
    search::show_page_activity(state->feedback, L"正在查询检验周转时间，请稍候…");
    std::thread([hwnd, query]() {
        auto* result = new QueryResult();
        result->start = query.start_time;
        result->end = query.end_time;
        result->ok = search::query_tat_statistics(query, result->summary, result->rows, result->error);
        if (!PostMessageW(hwnd, WM_TAT_LOADED, 0, reinterpret_cast<LPARAM>(result))) delete result;
    }).detach();
}

void resetFilters(TatState* state) {
    if (!state) return;
    setToday(state->start, state->end);
    SendMessageW(state->room, CB_SETCURSEL, 0, 0);
    SendMessageW(state->patientType, CB_SETCURSEL, 0, 0);
    SendMessageW(state->tatStatus, CB_SETCURSEL, 0, 0);
    Button_SetCheck(state->emergency, BST_UNCHECKED);
    SetWindowTextW(state->department, L"");
    SetWindowTextW(state->order, L"");
}

void openReport(HWND owner, TatState* state, int index) {
    if (!state || index < 0 || index >= static_cast<int>(state->rows.size())) return;
    const auto& row = state->rows[static_cast<size_t>(index)];
    if (search::trim(row.report_no).empty()) {
        MessageBoxW(owner, L"该条码尚未生成报告，无法跳转到常规报告。", WINDOW_TITLE, MB_ICONINFORMATION);
        return;
    }
    auto* target = new RegularReportOpenTarget{row.report_no, row.oper_no, row.inspect_date,
                                                row.machine_code, row.machine_name, row.room_code};
    HWND report = create_regular_report_module(state->ctx);
    if (!report || !PostMessageW(report, WM_REGULAR_OPEN_REPORT, 0, reinterpret_cast<LPARAM>(target))) {
        delete target;
        MessageBoxW(owner, L"常规报告页面打开失败。", WINDOW_TITLE, MB_ICONERROR);
    }
}

void copyCell(HWND owner, TatState* state) {
    POINT point{};
    GetCursorPos(&point);
    POINT client = point;
    ScreenToClient(state->details, &client);
    LVHITTESTINFO hit{};
    hit.pt = client;
    const int row = ListView_SubItemHitTest(state->details, &hit);
    if (row < 0 || hit.iSubItem < 0 || row >= static_cast<int>(state->rows.size())) return;
    const auto value = search::utf8_to_wide(cellValue(state->rows[static_cast<size_t>(row)], hit.iSubItem));
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    const SIZE_T bytes = (value.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void* target = GlobalLock(memory);
        if (target) {
            memcpy(target, value.c_str(), bytes);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
    search::set_page_status(state->feedback, L"已复制当前单元格内容。");
}

void exportXlsx(HWND hwnd, TatState* state) {
    if (!state || state->exporting || state->rows.empty()) {
        MessageBoxW(hwnd, L"当前没有可导出的明细。", WINDOW_TITLE, MB_ICONINFORMATION);
        return;
    }
    wchar_t path[MAX_PATH] = L"检验周转时间统计.xlsx";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrFilter = L"Excel 工作簿 (*.xlsx)\0*.xlsx\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"xlsx";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&dialog)) return;
    std::vector<std::string> headers;
    for (const auto& column : DETAIL_COLUMNS) headers.push_back(search::wide_to_utf8(column.title));
    const std::wstring outputPath(path);
    const auto rows = state->rows;
    state->exporting = true;
    setQueryControls(state, false);
    EnableWindow(state->exportExcel, FALSE);
    search::show_page_activity(state->feedback, L"正在导出检验周转时间明细，请稍候…");
    search::set_page_status(state->feedback, L"正在导出 Excel...");
    std::thread([hwnd, outputPath, headers = std::move(headers), rows]() {
        auto* result = new ExportResult();
        result->path = outputPath;
        result->ok = search::write_xlsx_file(outputPath, "检验周转时间统计", headers, rows.size(),
            [&rows](size_t row, size_t column) { return cellValue(rows[row], static_cast<int>(column)); }, result->error);
        if (!PostMessageW(hwnd, WM_TAT_EXPORTED, 0, reinterpret_cast<LPARAM>(result))) delete result;
    }).detach();
}

void resizeLayout(HWND hwnd, TatState* state) {
    if (!state) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int width = rc.right;
    const int height = rc.bottom;
    const int pad = S(hwnd, 10);
    const int h = S(hwnd, 25);
    const int row1 = S(hwnd, 9);
    const int row2 = S(hwnd, 42);
    const int row3 = S(hwnd, 74);
    const int statusY = S(hwnd, 107);
    const int summaryY = S(hwnd, 132);
    const int summaryH = S(hwnd, 58);
    int x = pad;
    auto placeLabel = [&](HWND control, int logicalWidth) {
        const int w = S(hwnd, logicalWidth);
        MoveWindow(control, x, row1 + S(hwnd, 3), w, h, TRUE);
        x += w + S(hwnd, 5);
    };
    HWND startLabel = GetDlgItem(hwnd, 7601);
    HWND toLabel = GetDlgItem(hwnd, 7602);
    HWND roomLabel = GetDlgItem(hwnd, 7603);
    HWND patientLabel = GetDlgItem(hwnd, 7604);
    placeLabel(startLabel, 72);
    MoveWindow(state->start, x, row1, S(hwnd, 145), h, TRUE); x += S(hwnd, 151);
    MoveWindow(toLabel, x, row1 + S(hwnd, 3), S(hwnd, 18), h, TRUE); x += S(hwnd, 24);
    MoveWindow(state->end, x, row1, S(hwnd, 145), h, TRUE); x += S(hwnd, 158);
    MoveWindow(roomLabel, x, row1 + S(hwnd, 3), S(hwnd, 58), h, TRUE); x += S(hwnd, 63);
    MoveWindow(state->room, x, row1, S(hwnd, 130), S(hwnd, 240), TRUE); x += S(hwnd, 143);
    MoveWindow(patientLabel, x, row1 + S(hwnd, 3), S(hwnd, 70), h, TRUE); x += S(hwnd, 75);
    MoveWindow(state->patientType, x, row1, S(hwnd, 86), S(hwnd, 180), TRUE); x += S(hwnd, 96);
    MoveWindow(state->emergency, x, row1, S(hwnd, 76), h, TRUE);

    x = pad;
    MoveWindow(GetDlgItem(hwnd, 7605), x, row2 + S(hwnd, 3), S(hwnd, 72), h, TRUE); x += S(hwnd, 77);
    MoveWindow(state->department, x, row2, S(hwnd, 190), h, TRUE); x += S(hwnd, 204);
    MoveWindow(GetDlgItem(hwnd, 7606), x, row2 + S(hwnd, 3), S(hwnd, 72), h, TRUE); x += S(hwnd, 77);
    MoveWindow(state->order, x, row2, S(hwnd, 230), h, TRUE); x += S(hwnd, 244);
    MoveWindow(GetDlgItem(hwnd, 7607), x, row2 + S(hwnd, 3), S(hwnd, 72), h, TRUE); x += S(hwnd, 77);
    MoveWindow(state->tatStatus, x, row2, S(hwnd, 92), S(hwnd, 150), TRUE);

    x = pad;
    HWND buttons[] = {state->query, state->reset, state->exportExcel, state->thresholdsButton,
        GetDlgItem(hwnd, IDC_RESERVED_CYCLE), GetDlgItem(hwnd, IDC_RESERVED_CALC),
        GetDlgItem(hwnd, IDC_RESERVED_HISTORY), GetDlgItem(hwnd, IDC_RESERVED_PRINT)};
    const int widths[] = {64, 64, 90, 86, 86, 86, 86, 86};
    for (int i = 0; i < 8; ++i) {
        MoveWindow(buttons[i], x, row3, S(hwnd, widths[i]), S(hwnd, 27), TRUE);
        x += S(hwnd, widths[i] + 8);
    }
    MoveWindow(state->status, pad, statusY, (std::max)(S(hwnd, 300), width - pad * 2), S(hwnd, 22), TRUE);
    MoveWindow(state->summary, pad, summaryY, width - pad * 2, summaryH, TRUE);
    MoveWindow(state->details, pad, summaryY + summaryH + pad, width - pad * 2,
               (std::max)(S(hwnd, 100), height - summaryY - summaryH - pad * 2), TRUE);
    search::layout_page_feedback(state->feedback);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<TatState*>(GetPropW(hwnd, PROP_STATE));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* mcs = reinterpret_cast<MDICREATESTRUCTW*>(cs->lpCreateParams);
            state = reinterpret_cast<TatState*>(mcs->lParam);
            SetPropW(hwnd, PROP_STATE, state);
            state->bgBrush = CreateSolidBrush(RGB(0xF0, 0xF0, 0xF0));
            state->thresholds = loadThresholds();

            CreateWindowExW(0, L"STATIC", L"签收时间：", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0,
                            hwnd, win32_control_id(7601), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"STATIC", L"至", WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 0, 0,
                            hwnd, win32_control_id(7602), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"STATIC", L"专业组：", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0,
                            hwnd, win32_control_id(7603), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"STATIC", L"病人类型：", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0,
                            hwnd, win32_control_id(7604), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"STATIC", L"申请科室：", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0,
                            hwnd, win32_control_id(7605), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"STATIC", L"医嘱项目：", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0,
                            hwnd, win32_control_id(7606), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"STATIC", L"TAT状态：", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0,
                            hwnd, win32_control_id(7607), GetModuleHandleW(nullptr), nullptr);

            state->start = dateTimePicker(hwnd, IDC_START);
            state->end = dateTimePicker(hwnd, IDC_END);
            setToday(state->start, state->end);
            state->room = combo(hwnd, IDC_ROOM); addCombo(state->room, L"全部"); SendMessageW(state->room, CB_SETCURSEL, 0, 0);
            state->patientType = combo(hwnd, IDC_PATIENT_TYPE);
            addCombo(state->patientType, L"全部"); addCombo(state->patientType, L"住院"); addCombo(state->patientType, L"门诊");
            SendMessageW(state->patientType, CB_SETCURSEL, 0, 0);
            state->emergency = CreateWindowExW(0, L"BUTTON", L"仅急诊", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_EMERGENCY), GetModuleHandleW(nullptr), nullptr);
            state->department = edit(hwnd, IDC_DEPARTMENT);
            state->order = edit(hwnd, IDC_ORDER);
            state->tatStatus = combo(hwnd, IDC_TAT_STATUS);
            addCombo(state->tatStatus, L"全部"); addCombo(state->tatStatus, L"正常");
            addCombo(state->tatStatus, L"超时"); addCombo(state->tatStatus, L"时间异常");
            SendMessageW(state->tatStatus, CB_SETCURSEL, 0, 0);
            state->query = search::create_button(hwnd, IDC_QUERY, L"查询", 0, 0, 0, 0);
            state->reset = search::create_button(hwnd, IDC_RESET, L"重置", 0, 0, 0, 0);
            state->exportExcel = search::create_button(hwnd, IDC_EXPORT, L"导出Excel", 0, 0, 0, 0);
            state->thresholdsButton = search::create_button(hwnd, IDC_THRESHOLDS, L"阈值设置", 0, 0, 0, 0);
            search::create_button(hwnd, IDC_RESERVED_CYCLE, L"周期配置", 0, 0, 0, 0);
            search::create_button(hwnd, IDC_RESERVED_CALC, L"计算信息", 0, 0, 0, 0);
            search::create_button(hwnd, IDC_RESERVED_HISTORY, L"往期信息", 0, 0, 0, 0);
            search::create_button(hwnd, IDC_RESERVED_PRINT, L"报表打印", 0, 0, 0, 0);
            EnableWindow(GetDlgItem(hwnd, IDC_RESERVED_CYCLE), FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_RESERVED_CALC), FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_RESERVED_HISTORY), FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_RESERVED_PRINT), FALSE);
            EnableWindow(state->exportExcel, FALSE);
            state->status = CreateWindowExW(0, L"STATIC", L"请选择签收时间后查询。", WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_STATUS), GetModuleHandleW(nullptr), nullptr);

            state->summary = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE |
                LVS_REPORT | LVS_SINGLESEL, 0, 0, 0, 0, hwnd, win32_control_id(IDC_SUMMARY), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(state->summary, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            for (int i = 0; i < static_cast<int>(std::size(SUMMARY_COLUMNS)); ++i)
                search::add_list_column(state->summary, i, SUMMARY_COLUMNS[i].title, SUMMARY_COLUMNS[i].width);

            state->details = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE |
                LVS_REPORT | LVS_SINGLESEL, 0, 0, 0, 0, hwnd, win32_control_id(IDC_DETAILS), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(state->details, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            for (int i = 0; i < DETAIL_COLUMN_COUNT; ++i)
                search::add_list_column(state->details, i, DETAIL_COLUMNS[i].title, DETAIL_COLUMNS[i].width);

            search::initialize_page_feedback(state->feedback, hwnd, state->status, state->details, state->ctx.uiFont);
            search::add_page_tooltip(state->feedback, state->query, L"按当前条件查询已签收条码的检验周转时间。");
            search::add_page_tooltip(state->feedback, state->thresholdsButton, L"设置四个 TAT 阶段的全局超时阈值。");
            search::add_page_tooltip(state->feedback, state->details, L"单击列标题排序；双击记录跳转常规报告；右键复制单元格。");
            search::apply_font_to_children(hwnd, state->ctx.uiFont);
            populateSummary(state);
            resizeLayout(hwnd, state);
            SetTimer(hwnd, TIMER_REFRESH, 60000, nullptr);

            const auto connection = search::build_connection_string_w(state->ctx.dbSettings);
            if (!connection.empty()) {
                std::thread([hwnd, connection]() {
                    auto* result = new RoomResult();
                    std::string error;
                    result->ok = search::query_barcode_rooms(search::wide_to_utf8(connection), result->rows, error);
                    if (!PostMessageW(hwnd, WM_TAT_ROOMS_LOADED, 0, reinterpret_cast<LPARAM>(result))) delete result;
                }).detach();
            }
            return 0;
        }
        case WM_SIZE:
            resizeLayout(hwnd, state);
            return 0;
        case WM_COMMAND:
            if (state && search::handle_page_feedback_command(state->feedback, lp, WINDOW_TITLE)) return 0;
            if (!state) break;
            if (LOWORD(wp) == IDC_QUERY) { runQuery(hwnd, state); return 0; }
            if (LOWORD(wp) == IDC_RESET) { resetFilters(state); return 0; }
            if (LOWORD(wp) == IDC_EXPORT) { exportXlsx(hwnd, state); return 0; }
            if (LOWORD(wp) == IDC_THRESHOLDS) {
                auto values = state->thresholds;
                if (showThresholdDialog(hwnd, values)) {
                    state->thresholds = values;
                    saveThresholds(values);
                    if (state->hasLoaded) {
                        search::refresh_tat_statistics(values, currentTimeText(), state->statSummary, state->allRows);
                        populateSummary(state);
                        applyStatusFilter(state);
                    }
                }
                return 0;
            }
            if (LOWORD(wp) == IDC_TAT_STATUS && HIWORD(wp) == CBN_SELCHANGE) {
                applyStatusFilter(state);
                return 0;
            }
            break;
        case WM_NOTIFY: {
            auto* header = reinterpret_cast<NMHDR*>(lp);
            if (!state || header->idFrom != IDC_DETAILS) break;
            if (header->code == NM_CUSTOMDRAW) {
                auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    const size_t index = static_cast<size_t>(draw->nmcd.dwItemSpec);
                    if (index < state->rows.size()) {
                        draw->clrText = RGB(0, 0, 0);
                        draw->clrTextBk = state->rows[index].time_abnormal ? COLOR_ABNORMAL :
                            state->rows[index].tat_status == "超时" ? COLOR_OVERTIME : COLOR_NORMAL;
                    }
                    return CDRF_NEWFONT;
                }
            }
            if (header->code == LVN_COLUMNCLICK) {
                const auto* info = reinterpret_cast<NMLISTVIEW*>(lp);
                sortRows(state, info->iSubItem, true);
                populateDetails(state);
                return 0;
            }
            if (header->code == NM_DBLCLK) {
                openReport(hwnd, state, reinterpret_cast<NMITEMACTIVATE*>(lp)->iItem);
                return 0;
            }
            if (header->code == NM_RCLICK) {
                copyCell(hwnd, state);
                return 0;
            }
            break;
        }
        case WM_TAT_LOADED: {
            std::unique_ptr<QueryResult> result(reinterpret_cast<QueryResult*>(lp));
            if (!state) return 0;
            state->querying = false;
            setQueryControls(state, true);
            search::hide_page_activity(state->feedback);
            if (!result->ok) {
                EnableWindow(state->exportExcel, state->hasLoaded && !state->rows.empty());
                search::show_page_alert(state->feedback, L"查询失败：" + search::utf8_to_wide(result->error));
                return 0;
            }
            state->loadedStart = result->start;
            state->loadedEnd = result->end;
            state->allRows = std::move(result->rows);
            state->statSummary = result->summary;
            state->hasLoaded = true;
            state->sortColumn = COL_RECEIVE_TIME;
            state->sortAscending = false;
            populateSummary(state);
            applyStatusFilter(state);
            return 0;
        }
        case WM_TAT_ROOMS_LOADED: {
            std::unique_ptr<RoomResult> result(reinterpret_cast<RoomResult*>(lp));
            if (!state || !result->ok) return 0;
            state->rooms = std::move(result->rows);
            SendMessageW(state->room, CB_RESETCONTENT, 0, 0);
            addCombo(state->room, L"全部");
            for (const auto& room : state->rooms) {
                const std::wstring text = search::utf8_to_wide(room.room_name.empty() ? room.room_code : room.room_name);
                addCombo(state->room, text.c_str());
            }
            SendMessageW(state->room, CB_SETCURSEL, 0, 0);
            return 0;
        }
        case WM_TAT_EXPORTED: {
            std::unique_ptr<ExportResult> result(reinterpret_cast<ExportResult*>(lp));
            if (!state) return 0;
            state->exporting = false;
            setQueryControls(state, true);
            search::hide_page_activity(state->feedback);
            EnableWindow(state->exportExcel, state->hasLoaded && !state->rows.empty());
            if (!result->ok) {
                search::show_page_alert(state->feedback, L"导出失败：" + search::utf8_to_wide(result->error));
            } else {
                search::set_page_status(state->feedback, L"已导出：" + result->path);
            }
            return 0;
        }
        case WM_TIMER:
            if (state && wp == TIMER_REFRESH && state->hasLoaded && !state->querying && !state->exporting) {
                search::refresh_tat_statistics(state->thresholds, currentTimeText(), state->statSummary, state->allRows);
                populateSummary(state);
                applyStatusFilter(state, false);
            }
            return 0;
        case app::WM_APP_SETTINGS_CHANGED:
        case app::WM_APP_FONT_CHANGED:
            if (state) {
                if (msg == app::WM_APP_FONT_CHANGED && lp) state->ctx.uiFont = reinterpret_cast<HFONT>(lp);
                search::apply_font_to_children(hwnd, state->ctx.uiFont);
                resizeLayout(hwnd, state);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        case WM_CTLCOLORSTATIC:
            if (state) {
                LRESULT result = 0;
                if (search::page_feedback_static_color(state->feedback, reinterpret_cast<HDC>(wp),
                        reinterpret_cast<HWND>(lp), result)) return result;
            }
            SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
            return reinterpret_cast<LRESULT>(state ? state->bgBrush : nullptr);
        case WM_ERASEBKGND: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc, state ? state->bgBrush :
                     reinterpret_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH)));
            return 1;
        }
        case WM_DESTROY:
            if (state) {
                KillTimer(hwnd, TIMER_REFRESH);
                search::destroy_page_feedback(state->feedback);
                if (state->bgBrush) DeleteObject(state->bgBrush);
                RemovePropW(hwnd, PROP_STATE);
                delete state;
            }
            return 0;
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND create_tat_statistics_module(const ModuleContext& ctx) {
    if (HWND existing = activate_existing_mdi_child_by_title(ctx.mdiClient, WINDOW_TITLE)) return existing;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = ctx.instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(ctx.instance, MAKEINTRESOURCEW(IDI_APP));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = WND_CLASS;
    RegisterClassExW(&wc);
    auto* state = new TatState();
    state->ctx = ctx;
    MDICREATESTRUCTW mcs{};
    mcs.szTitle = WINDOW_TITLE;
    mcs.szClass = WND_CLASS;
    mcs.hOwner = ctx.instance;
    mcs.x = mcs.y = mcs.cx = mcs.cy = CW_USEDEFAULT;
    mcs.lParam = reinterpret_cast<LPARAM>(state);
    HWND child = reinterpret_cast<HWND>(SendMessageW(ctx.mdiClient, WM_MDICREATE, 0, reinterpret_cast<LPARAM>(&mcs)));
    if (!child) {
        delete state;
        MessageBoxW(ctx.mdiClient, L"检验周转时间统计窗口创建失败。", WINDOW_TITLE, MB_ICONERROR);
        return nullptr;
    }
    SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    return child;
}

#endif
