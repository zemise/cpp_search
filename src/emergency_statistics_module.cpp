#include "emergency_statistics_module.h"

#ifdef _WIN32

#include "main_app.h"
#include "page_feedback.h"
#include "regular_report_module.h"
#include "resource.h"
#include "search_core.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"

#include <commctrl.h>
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const wchar_t* WND_CLASS = L"EmergencyStatisticsModuleChild";
constexpr const wchar_t* WINDOW_TITLE = L"急诊样本统计";
constexpr const wchar_t* PROP_STATE = L"EmergencyStatisticsSt";
constexpr UINT WM_EMERGENCY_STATS_LOADED = WM_APP + 0x571;
constexpr UINT_PTR TIMER_REFRESH_DURATIONS = 1;

const COLORREF COLOR_WAITING = RGB(0xFF, 0xFF, 0x54);
const COLORREF COLOR_SENT = RGB(0x99, 0xBB, 0x90);
const COLORREF COLOR_REVIEWED = RGB(0x6F, 0x94, 0xE6);
const COLORREF COLOR_WHITE = RGB(0xFF, 0xFF, 0xFF);
const COLORREF COLOR_EMERGENCY_TEXT = RGB(0xCC, 0x00, 0x00);

enum ControlId {
    IDC_START_TIME = 6601,
    IDC_END_TIME = 6602,
    IDC_ONLY_UNFINISHED = 6603,
    IDC_QUERY = 6604,
    IDC_SUMMARY = 6605,
    IDC_DETAILS = 6606,
    IDC_STATUS = 6607,
    IDC_LAB_DEPARTMENT = 6608,
};

struct ListColumn {
    const wchar_t* title;
    int width;
};

constexpr ListColumn SUMMARY_COLUMNS[] = {
    {L"急诊条码总数", 300},
    {L"未完成数", 220},
    {L"未上机数", 220},
    {L"已上机未审核数", 300},
    {L"审核完成数", 240},
    {L"医生已查看数", 260},
    {L"报告已发送数", 260},
    {L"条码急诊数", 240},
    {L"报告急诊补充数", 300},
    {L"双重命中数", 240},
};

constexpr ListColumn DETAIL_COLUMNS[] = {
    {L"院区", 50},
    {L"样本号", 70},
    {L"条码号", 120},
    {L"急诊来源", 96},
    {L"当前状态", 132},
    {L"签收-审核用时", 160},
    {L"签收人", 90},
    {L"病人号", 130},
    {L"类型", 55},
    {L"姓名", 88},
    {L"性别", 52},
    {L"年龄", 62},
    {L"申请科室", 150},
    {L"床号", 70},
    {L"医嘱内容", 230},
    {L"标本", 90},
    {L"报告号", 110},
    {L"仪器", 120},
    {L"申请时间", 148},
    {L"签收时间", 148},
    {L"上机时间", 140},
    {L"报告时间", 148},
    {L"审核", 60},
    {L"发送", 60},
    {L"", 50},
};

struct EmergencyStatisticsState {
    ModuleContext ctx;
    HWND startDate = nullptr;
    HWND endDate = nullptr;
    HWND labDepartment = nullptr;
    HWND onlyUnfinished = nullptr;
    HWND query = nullptr;
    HWND summary = nullptr;
    HWND details = nullptr;
    HWND status = nullptr;
    search::PageFeedback feedback;
    HBRUSH bgBrush = nullptr;
    bool querying = false;
    int sortColumn = 4;
    bool sortAscending = true;
    search::EmergencyStatSummary statSummary;
    std::vector<search::EmergencyStatDetailRow> rows;
};

struct EmergencyQueryResult {
    bool ok = false;
    std::string error;
    search::EmergencyStatSummary summary;
    std::vector<search::EmergencyStatDetailRow> rows;
};

int S(HWND hwnd, int value) {
    return static_cast<int>(value * search::dpi_scale_factor(hwnd));
}

HWND label(HWND parent, const wchar_t* text, int x, int y, int w, int h, DWORD align = SS_RIGHT) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | align,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND dateTimePicker(HWND parent, int id, int x, int y, int w, int h) {
    HWND hwnd = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATECENTURYFORMAT,
                                x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
    DateTime_SetFormat(hwnd, L"yyyy-MM-dd HH:mm");
    return hwnd;
}

std::string dateTimeText(HWND hwnd) {
    SYSTEMTIME st{};
    if (DateTime_GetSystemtime(hwnd, &st) != GDT_VALID) return "";
    char buf[32]{};
    sprintf_s(buf, "%04u-%02u-%02u %02u:%02u:%02u",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::wstring comboText(HWND combo) {
    const int idx = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (idx < 0) return {};
    wchar_t buf[64]{};
    SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(idx), reinterpret_cast<LPARAM>(buf));
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

void setStatus(EmergencyStatisticsState* st, const std::wstring& text) {
    if (st) search::set_page_status(st->feedback, text);
}

void setCellUtf8(HWND list, int row, int col, const std::string& text) {
    const auto wide = search::utf8_to_wide(text);
    ListView_SetItemText(list, row, col, const_cast<wchar_t*>(wide.c_str()));
}

bool parseSqlDateTime(const std::string& value, std::tm& out) {
    const std::string text = search::trim(value);
    if (text.size() < 10) return false;
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (std::sscanf(text.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) < 3) {
        return false;
    }
    out = std::tm{};
    out.tm_year = year - 1900;
    out.tm_mon = month - 1;
    out.tm_mday = day;
    out.tm_hour = hour;
    out.tm_min = minute;
    out.tm_sec = second;
    out.tm_isdst = -1;
    return true;
}

int secondsFromStartToNow(const std::string& start) {
    std::tm startTm{};
    if (!parseSqlDateTime(start, startTm)) return -1;
    const std::time_t startTime = std::mktime(&startTm);
    const std::time_t now = std::time(nullptr);
    if (startTime == static_cast<std::time_t>(-1) || now == static_cast<std::time_t>(-1)) return -1;
    const double seconds = std::difftime(now, startTime);
    if (seconds < 0) return -1;
    return static_cast<int>(seconds);
}

std::string durationText(int seconds) {
    if (seconds < 0) return "-";
    const int hours = seconds / 3600;
    const int mins = (seconds % 3600) / 60;
    const int secs = seconds % 60;
    if (hours > 0) {
        return std::to_string(hours) + " 小时 " + std::to_string(mins) + " 分 " + std::to_string(secs) + " 秒";
    }
    if (mins > 0) {
        return std::to_string(mins) + " 分 " + std::to_string(secs) + " 秒";
    }
    return std::to_string(secs) + " 秒";
}

bool hasFixedReviewDuration(const search::EmergencyStatDetailRow& row) {
    return search::trim(row.chk_flag) == "T" && !search::trim(row.review_time).empty();
}

void refreshDynamicDurations(EmergencyStatisticsState* st, bool updateList) {
    if (!st) return;
    for (int i = 0; i < static_cast<int>(st->rows.size()); ++i) {
        auto& row = st->rows[static_cast<size_t>(i)];
        if (!hasFixedReviewDuration(row)) {
            row.wait_seconds = secondsFromStartToNow(row.in_date);
            row.wait_minutes = row.wait_seconds < 0 ? 0 : row.wait_seconds / 60;
        }
        if (updateList && st->details) {
            setCellUtf8(st->details, i, 5, durationText(row.wait_seconds));
        }
    }
}

void initList(HWND list, const ListColumn* columns, size_t count) {
    for (int i = 0; i < static_cast<int>(count); ++i) {
        search::add_list_column(list, i, columns[i].title, columns[i].width);
    }
}

void setSummaryValue(HWND list, int col, int value) {
    const std::wstring text = std::to_wstring(value);
    ListView_SetItemText(list, 0, col, const_cast<wchar_t*>(text.c_str()));
}

int summaryValue(const search::EmergencyStatSummary& summary, int row) {
    switch (row) {
        case 0: return summary.emergency_barcode_count;
        case 1: return summary.unfinished_count;
        case 2: return summary.not_loaded_count;
        case 3: return summary.loaded_not_reviewed_count;
        case 4: return summary.reviewed_count;
        case 5: return summary.doctor_viewed_count;
        case 6: return summary.sent_count;
        case 7: return summary.barcode_emergency_count;
        case 8: return summary.report_emergency_count - summary.both_emergency_count;
        case 9: return summary.both_emergency_count;
        default: return 0;
    }
}

void populateSummary(EmergencyStatisticsState* st) {
    ListView_DeleteAllItems(st->summary);
    const std::wstring firstValue = std::to_wstring(summaryValue(st->statSummary, 0));
    LVITEMW valueItem{};
    valueItem.mask = LVIF_TEXT;
    valueItem.iItem = 0;
    valueItem.pszText = const_cast<wchar_t*>(firstValue.c_str());
    ListView_InsertItem(st->summary, &valueItem);

    for (int i = 1; i < static_cast<int>(std::size(SUMMARY_COLUMNS)); ++i) {
        setSummaryValue(st->summary, i, summaryValue(st->statSummary, i));
    }
}

void populateDetails(EmergencyStatisticsState* st) {
    SendMessageW(st->details, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(st->details);
    for (int i = 0; i < static_cast<int>(st->rows.size()); ++i) {
        const auto& row = st->rows[static_cast<size_t>(i)];
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        const auto labDepartment = search::utf8_to_wide(row.lab_department);
        item.pszText = const_cast<wchar_t*>(labDepartment.c_str());
        ListView_InsertItem(st->details, &item);
        setCellUtf8(st->details, i, 1, row.oper_no);
        setCellUtf8(st->details, i, 2, row.barcode);
        setCellUtf8(st->details, i, 3, row.emergency_source);
        setCellUtf8(st->details, i, 4, row.barcode_status);
        setCellUtf8(st->details, i, 5, durationText(row.wait_seconds));
        setCellUtf8(st->details, i, 6, row.sign_oper);
        setCellUtf8(st->details, i, 7, row.reg_no);
        setCellUtf8(st->details, i, 8, row.type_name);
        setCellUtf8(st->details, i, 9, row.name);
        setCellUtf8(st->details, i, 10, row.sex);
        setCellUtf8(st->details, i, 11, row.age);
        setCellUtf8(st->details, i, 12, row.dept_name);
        setCellUtf8(st->details, i, 13, row.bed_code);
        setCellUtf8(st->details, i, 14, row.order_text);
        setCellUtf8(st->details, i, 15, row.sample_name);
        setCellUtf8(st->details, i, 16, row.rep_no);
        setCellUtf8(st->details, i, 17, row.mach_name.empty() ? row.mach_code : row.mach_name);
        setCellUtf8(st->details, i, 18, row.req_time);
        setCellUtf8(st->details, i, 19, row.in_date);
        setCellUtf8(st->details, i, 20, row.create_time);
        setCellUtf8(st->details, i, 21, row.rep_time);
        setCellUtf8(st->details, i, 22, row.chk_flag);
        setCellUtf8(st->details, i, 23, row.conf);
        setCellUtf8(st->details, i, 24, "");
    }
    SendMessageW(st->details, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(st->details, nullptr, TRUE);
}

std::string sortValue(const search::EmergencyStatDetailRow& row, int col) {
    switch (col) {
        case 0: return row.lab_department;
        case 1: return row.oper_no;
        case 2: return row.barcode;
        case 3: return row.emergency_source;
        case 4: return std::to_string(row.min_oper_state);
        case 5: return std::to_string(row.wait_seconds);
        case 6: return row.sign_oper;
        case 7: return row.reg_no;
        case 8: return row.type_name;
        case 9: return row.name;
        case 10: return row.sex;
        case 11: return row.age;
        case 12: return row.dept_name;
        case 13: return row.bed_code;
        case 14: return row.order_text;
        case 15: return row.sample_name;
        case 16: return row.rep_no;
        case 17: return row.mach_name.empty() ? row.mach_code : row.mach_name;
        case 18: return row.req_time;
        case 19: return row.in_date;
        case 20: return row.create_time;
        case 21: return row.rep_time;
        case 22: return row.chk_flag;
        case 23: return row.conf;
        case 24: return "";
        default: return row.barcode;
    }
}

void sortRows(EmergencyStatisticsState* st, int column, bool toggle) {
    if (toggle && st->sortColumn == column) {
        st->sortAscending = !st->sortAscending;
    } else {
        st->sortColumn = column;
        st->sortAscending = column != 5;
    }
    const int col = st->sortColumn;
    const bool asc = st->sortAscending;
    std::stable_sort(st->rows.begin(), st->rows.end(), [col, asc](const auto& a, const auto& b) {
        if (col == 4) {
            if (a.min_oper_state != b.min_oper_state) return asc ? a.min_oper_state < b.min_oper_state : a.min_oper_state > b.min_oper_state;
            return a.wait_minutes > b.wait_minutes;
        }
        if (col == 5) {
            return asc ? a.wait_seconds < b.wait_seconds : a.wait_seconds > b.wait_seconds;
        }
        const auto av = sortValue(a, col);
        const auto bv = sortValue(b, col);
        return asc ? av < bv : av > bv;
    });
}

COLORREF rowBackColor(const search::EmergencyStatDetailRow& row) {
    if (search::trim(row.conf) == "S") return COLOR_SENT;
    if (row.min_oper_state == 0 || row.min_oper_state == 1) return COLOR_WAITING;
    if (row.min_oper_state == 2 || row.min_oper_state == 3) return COLOR_REVIEWED;
    return COLOR_WHITE;
}

void resizeLayout(HWND hwnd, EmergencyStatisticsState* st) {
    if (!st) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const int pad = S(hwnd, 10);
    const int topH = S(hwnd, 42);
    const int summaryH = S(hwnd, 76);
    const int statusX = S(hwnd, 864);
    const int statusW = (std::max)(S(hwnd, 120), w - statusX - pad);
    MoveWindow(st->status, statusX, S(hwnd, 12), statusW, S(hwnd, 24), TRUE);
    MoveWindow(st->summary, pad, topH + pad, w - pad * 2, summaryH, TRUE);
    MoveWindow(st->details, pad, topH + summaryH + pad * 2, w - pad * 2,
               (std::max)(S(hwnd, 80), h - topH - summaryH - pad * 3), TRUE);
    search::layout_page_feedback(st->feedback);
}

void runQuery(HWND hwnd, EmergencyStatisticsState* st) {
    if (!st || st->querying) return;
    const auto connection = search::build_connection_string_w(st->ctx.dbSettings);
    if (connection.empty()) {
        MessageBoxW(hwnd, L"请先在系统设置中配置数据库连接。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }

    search::EmergencyStatQuery query;
    query.connection_string = search::wide_to_utf8(connection);
    query.start_time = dateTimeText(st->startDate);
    query.end_time = dateTimeText(st->endDate);
    query.time_field = "Sign";
    query.lab_department = search::wide_to_utf8(comboText(st->labDepartment));
    query.only_unfinished = Button_GetCheck(st->onlyUnfinished) == BST_CHECKED;

    st->querying = true;
    EnableWindow(st->query, FALSE);
    setStatus(st, L"正在查询急诊条码统计...");
    search::show_page_activity(st->feedback, L"正在查询急诊条码统计，请稍候…");

    std::thread([hwnd, query]() {
        auto* result = new EmergencyQueryResult();
        result->ok = search::query_emergency_statistics(query, result->summary, result->rows, result->error);
        if (!PostMessageW(hwnd, WM_EMERGENCY_STATS_LOADED, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }).detach();
}

void openRegularReportForRow(HWND owner, EmergencyStatisticsState* st, int index) {
    if (!st || index < 0 || index >= static_cast<int>(st->rows.size())) return;
    const auto& row = st->rows[static_cast<size_t>(index)];
    if (search::trim(row.rep_no).empty() || search::trim(row.mach_code).empty() ||
        search::trim(row.inspect_date).empty()) {
        MessageBoxW(owner, L"该条码未上机", L"常规报告", MB_ICONINFORMATION);
        return;
    }

    auto* target = new RegularReportOpenTarget;
    target->rep_no = search::trim(row.rep_no);
    target->oper_no = search::trim(row.oper_no);
    target->inspect_date = search::trim(row.inspect_date);
    target->mach_code = search::trim(row.mach_code);
    target->mach_name = search::trim(row.mach_name);
    target->room_code = search::trim(row.room_code);

    HWND regular = create_regular_report_module(st->ctx);
    if (!regular || !PostMessageW(regular, WM_REGULAR_OPEN_REPORT, 0, reinterpret_cast<LPARAM>(target))) {
        delete target;
        MessageBoxW(owner, L"常规报告页面打开失败。", L"常规报告", MB_ICONERROR);
    }
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<EmergencyStatisticsState*>(GetPropW(hwnd, PROP_STATE));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* mcs = reinterpret_cast<MDICREATESTRUCTW*>(cs->lpCreateParams);
            st = reinterpret_cast<EmergencyStatisticsState*>(mcs->lParam);
            SetPropW(hwnd, PROP_STATE, st);
            st->bgBrush = CreateSolidBrush(RGB(0xF0, 0xF0, 0xF0));

            label(hwnd, L"签收时间：", 0, S(hwnd, 12), S(hwnd, 88), S(hwnd, 24));
            st->startDate = dateTimePicker(hwnd, IDC_START_TIME, S(hwnd, 92), S(hwnd, 10), S(hwnd, 172), S(hwnd, 24));
            label(hwnd, L"至", S(hwnd, 270), S(hwnd, 12), S(hwnd, 24), S(hwnd, 24), SS_CENTER);
            st->endDate = dateTimePicker(hwnd, IDC_END_TIME, S(hwnd, 300), S(hwnd, 10), S(hwnd, 172), S(hwnd, 24));
            setToday(st->startDate, false);
            setToday(st->endDate, true);
            label(hwnd, L"院区：", S(hwnd, 482), S(hwnd, 12), S(hwnd, 52), S(hwnd, 24));
            st->labDepartment = CreateWindowExW(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                S(hwnd, 538), S(hwnd, 10), S(hwnd, 82), S(hwnd, 160),
                hwnd, win32_control_id(IDC_LAB_DEPARTMENT), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(st->labDepartment, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"全部"));
            SendMessageW(st->labDepartment, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"老院"));
            SendMessageW(st->labDepartment, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"新院"));
            SendMessageW(st->labDepartment, CB_SETCURSEL, 0, 0);
            st->onlyUnfinished = CreateWindowExW(0, L"BUTTON", L"只看未完成",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                S(hwnd, 634), S(hwnd, 11), S(hwnd, 112), S(hwnd, 22),
                hwnd, win32_control_id(IDC_ONLY_UNFINISHED), GetModuleHandleW(nullptr), nullptr);
            Button_SetCheck(st->onlyUnfinished, BST_CHECKED);
            st->query = search::create_button(hwnd, IDC_QUERY, L"查询", S(hwnd, 760), S(hwnd, 9), S(hwnd, 86), S(hwnd, 26));
            st->status = label(hwnd, L"请选择签收时间后查询。", S(hwnd, 864), S(hwnd, 12), S(hwnd, 520), S(hwnd, 24), SS_LEFT);

            st->summary = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_SUMMARY), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(st->summary, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            initList(st->summary, SUMMARY_COLUMNS, std::size(SUMMARY_COLUMNS));

            st->details = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_DETAILS), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(st->details, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            initList(st->details, DETAIL_COLUMNS, std::size(DETAIL_COLUMNS));

            search::initialize_page_feedback(st->feedback, hwnd, st->status,
                                             st->details, st->ctx.uiFont);
            search::add_page_tooltip(st->feedback, st->query,
                                     L"按当前签收时间、院区和完成状态查询急诊样本。");
            search::add_page_tooltip(st->feedback, st->onlyUnfinished,
                                     L"勾选后仅显示尚未完成审核或发送的急诊样本。");
            search::add_page_tooltip(st->feedback, st->details,
                                     L"单击列标题排序；双击已上机记录可跳转常规报告。");

            search::apply_font_to_children(hwnd, st->ctx.uiFont);
            populateSummary(st);
            resizeLayout(hwnd, st);
            SetTimer(hwnd, TIMER_REFRESH_DURATIONS, 1000, nullptr);
            return 0;
        }
        case WM_TIMER:
            if (wp == TIMER_REFRESH_DURATIONS) {
                refreshDynamicDurations(st, true);
                return 0;
            }
            break;
        case WM_SIZE:
            resizeLayout(hwnd, st);
            return 0;
        case WM_COMMAND:
            if (st && search::handle_page_feedback_command(st->feedback, lp, WINDOW_TITLE)) return 0;
            if (LOWORD(wp) == IDC_QUERY) {
                runQuery(hwnd, st);
                return 0;
            }
            break;
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (st && nm->idFrom == IDC_DETAILS && nm->code == NM_CUSTOMDRAW) {
                auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    const int idx = static_cast<int>(cd->nmcd.dwItemSpec);
                    if (idx >= 0 && idx < static_cast<int>(st->rows.size())) {
                        const auto& row = st->rows[static_cast<size_t>(idx)];
                        cd->clrTextBk = rowBackColor(row);
                        cd->clrText = COLOR_EMERGENCY_TEXT;
                    }
                    return CDRF_NEWFONT;
                }
            }
            if (st && nm->idFrom == IDC_DETAILS && nm->code == LVN_COLUMNCLICK) {
                auto* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                sortRows(st, lv->iSubItem, true);
                populateDetails(st);
                return 0;
            }
            if (st && nm->idFrom == IDC_DETAILS && nm->code == NM_DBLCLK) {
                auto* item = reinterpret_cast<NMITEMACTIVATE*>(lp);
                openRegularReportForRow(hwnd, st, item->iItem);
                return 0;
            }
            break;
        }
        case WM_EMERGENCY_STATS_LOADED: {
            std::unique_ptr<EmergencyQueryResult> result(reinterpret_cast<EmergencyQueryResult*>(lp));
            if (!st) return 0;
            st->querying = false;
            EnableWindow(st->query, TRUE);
            search::hide_page_activity(st->feedback);
            if (!result->ok) {
                search::show_page_alert(st->feedback,
                    L"查询失败：" + search::utf8_to_wide(result->error));
                return 0;
            }
            st->statSummary = result->summary;
            st->rows = std::move(result->rows);
            refreshDynamicDurations(st, false);
            st->sortColumn = 4;
            st->sortAscending = true;
            sortRows(st, 4, false);
            populateSummary(st);
            populateDetails(st);
            wchar_t status[200]{};
            swprintf(status, 200, L"查询完成：急诊条码 %d，未完成 %d，未上机 %d，明细 %d 条。",
                     st->statSummary.emergency_barcode_count,
                     st->statSummary.unfinished_count,
                     st->statSummary.not_loaded_count,
                     static_cast<int>(st->rows.size()));
            setStatus(st, status);
            return 0;
        }
        case app::WM_APP_SETTINGS_CHANGED:
        case app::WM_APP_FONT_CHANGED:
            if (st) {
                if (msg == app::WM_APP_FONT_CHANGED && lp) st->ctx.uiFont = reinterpret_cast<HFONT>(lp);
                search::apply_font_to_children(hwnd, st->ctx.uiFont);
                resizeLayout(hwnd, st);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        case WM_CTLCOLORSTATIC:
            if (st) {
                LRESULT result = 0;
                if (search::page_feedback_static_color(
                        st->feedback, reinterpret_cast<HDC>(wp),
                        reinterpret_cast<HWND>(lp), result)) return result;
            }
            SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
            return reinterpret_cast<LRESULT>(st ? st->bgBrush : nullptr);
        case WM_ERASEBKGND: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc, st ? st->bgBrush : reinterpret_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH)));
            return 1;
        }
        case WM_DESTROY:
            if (st) {
                KillTimer(hwnd, TIMER_REFRESH_DURATIONS);
                search::destroy_page_feedback(st->feedback);
                if (st->bgBrush) DeleteObject(st->bgBrush);
                RemovePropW(hwnd, PROP_STATE);
            }
            break;
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

void registerClass(HINSTANCE inst) {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = WND_CLASS;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP),
                                               IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);
    registered = true;
}

}  // namespace

HWND create_emergency_statistics_module(const ModuleContext& ctx) {
    if (HWND existing = activate_existing_mdi_child_by_title(ctx.mdiClient, WINDOW_TITLE)) {
        return existing;
    }

    registerClass(ctx.instance);
    auto* st = new EmergencyStatisticsState();
    st->ctx = ctx;
    MDICREATESTRUCTW mcs{};
    mcs.szClass = WND_CLASS;
    mcs.szTitle = WINDOW_TITLE;
    mcs.hOwner = ctx.instance;
    mcs.x = CW_USEDEFAULT;
    mcs.y = CW_USEDEFAULT;
    mcs.cx = CW_USEDEFAULT;
    mcs.cy = CW_USEDEFAULT;
    mcs.lParam = reinterpret_cast<LPARAM>(st);
    HWND child = reinterpret_cast<HWND>(SendMessageW(ctx.mdiClient, WM_MDICREATE, 0, reinterpret_cast<LPARAM>(&mcs)));
    if (!child) {
        delete st;
        return nullptr;
    }
    SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    return child;
}

#endif
