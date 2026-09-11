#include "transfusion_order_statistics_module.h"

#ifdef _WIN32

#include "blood_module.h"
#include "main_app.h"
#include "page_feedback.h"
#include "resource.h"
#include "search_core.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"
#include "xlsx_writer.h"

#include <commctrl.h>
#include <commdlg.h>
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const wchar_t* WND_CLASS = L"TransfusionOrderStatisticsModuleChild";
constexpr const wchar_t* LEGEND_CLASS = L"TransfusionOrderStatisticsLegend";
constexpr const wchar_t* SUMMARY_GROUP_CLASS = L"TransfusionOrderStatisticsSummaryGroup";
constexpr const wchar_t* WINDOW_TITLE = L"输血单统计";
constexpr const wchar_t* PROP_STATE = L"TransfusionOrderStatisticsSt";
constexpr UINT WM_TRANSFUSION_ORDER_LOADED = WM_APP + 0x578;

constexpr COLORREF COLOR_UNREVIEWED = RGB(0xFF, 0xE0, 0xB2);
constexpr COLORREF COLOR_REVIEWED = RGB(0xBB, 0xDE, 0xFB);
constexpr COLORREF COLOR_COMPLETED = RGB(0xC8, 0xE6, 0xC9);
constexpr COLORREF COLOR_REJECTED = RGB(0xFF, 0xCD, 0xD2);
constexpr COLORREF COLOR_DELETED = RGB(0xE0, 0xE0, 0xE0);
constexpr COLORREF COLOR_OTHER = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF COLOR_EMERGENCY = RGB(0xEA, 0x33, 0x23);
constexpr COLORREF COLOR_SUMMARY_TOTAL = RGB(0xEE, 0xEE, 0xEE);
constexpr COLORREF COLOR_SUMMARY_STATUS = RGB(0xE3, 0xF2, 0xFD);
constexpr COLORREF COLOR_SUMMARY_URGENCY = RGB(0xFF, 0xF3, 0xE0);

enum ControlId {
    IDC_START_DATE = 7301,
    IDC_END_DATE,
    IDC_CAMPUS,
    IDC_INCLUDE_REJECTED,
    IDC_INCLUDE_DELETED,
    IDC_QUERY,
    IDC_EXPORT,
    IDC_MAIN_SUMMARY,
    IDC_DETAILS,
    IDC_STATUS,
};

struct ListColumn {
    const wchar_t* title;
    int width;
};

enum DetailColumn {
    COL_CAMPUS,
    COL_URGENCY,
    COL_APPLY_FORM_NO,
    COL_APPLY_TIME,
    COL_APPLY_STATUS,
    COL_REMARK,
    COL_PATIENT_NO,
    COL_PATIENT_NO_TYPE,
    COL_PATIENT_NAME,
    COL_APPLY_DEPT,
    COL_BED_NO,
    COL_APPLY_DOCTOR,
    COL_TRAN_PROPERTY,
    COL_DELETE_BIT,
    COL_DATA_STATUS,
    DETAIL_COLUMN_COUNT,
};

enum SummaryColumn {
    SUMMARY_TOTAL,
    SUMMARY_UNREVIEWED,
    SUMMARY_REVIEWED,
    SUMMARY_COMPLETED,
    SUMMARY_REJECTED,
    SUMMARY_DELETED,
    SUMMARY_EMERGENCY,
    SUMMARY_ROUTINE,
    SUMMARY_BACKUP,
    SUMMARY_COLUMN_COUNT,
};

constexpr ListColumn MAIN_SUMMARY_COLUMNS[] = {
    {L"输血申请单总数", 190},
    {L"未审核", 135},
    {L"已审核", 135},
    {L"已完结", 135},
    {L"已驳回", 135},
    {L"已删除", 135},
    {L"紧急", 145},
    {L"常规", 145},
    {L"备血", 145},
};

constexpr int MAIN_SUMMARY_COLUMN_PERCENTAGES[] = {14, 10, 10, 10, 10, 10, 12, 12, 12};

constexpr ListColumn DETAIL_COLUMNS[] = {
    {L"院区", 70},
    {L"紧急程度", 150},
    {L"申请单号", 160},
    {L"申请时间", 150},
    {L"申请状态", 90},
    {L"原因", 240},
    {L"病人号", 130},
    {L"病人类型", 90},
    {L"姓名", 90},
    {L"申请科室", 180},
    {L"床号", 70},
    {L"申请医生", 100},
    {L"申请类型", 100},
    {L"删除标志", 80},
    {L"数据状态", 180},
};

static_assert(std::size(DETAIL_COLUMNS) == DETAIL_COLUMN_COUNT);
static_assert(std::size(MAIN_SUMMARY_COLUMNS) == SUMMARY_COLUMN_COUNT);
static_assert(std::size(MAIN_SUMMARY_COLUMN_PERCENTAGES) == SUMMARY_COLUMN_COUNT);

using Summary = search::TransfusionOrderStatSummary;
using DetailRow = search::TransfusionOrderStatDetailRow;

struct State {
    ModuleContext ctx;
    HWND dateLabel = nullptr;
    HWND dateToLabel = nullptr;
    HWND campusLabel = nullptr;
    HWND startDate = nullptr;
    HWND endDate = nullptr;
    HWND campus = nullptr;
    HWND includeRejected = nullptr;
    HWND includeDeleted = nullptr;
    HWND query = nullptr;
    HWND exportExcel = nullptr;
    HWND legend = nullptr;
    HWND summaryGroups = nullptr;
    HWND mainSummary = nullptr;
    HWND details = nullptr;
    HWND status = nullptr;
    search::PageFeedback feedback;
    HBRUSH bgBrush = nullptr;
    bool querying = false;
    bool hasLoadedResult = false;
    bool loadedIncludeRejected = false;
    bool loadedIncludeDeleted = false;
    std::wstring loadedCampus = L"全部";
    std::string loadedStartDate;
    std::string loadedEndDate;
    int sortColumn = COL_APPLY_TIME;
    bool sortAscending = false;
    Summary summary;
    std::vector<DetailRow> rows;
};

struct QueryResult {
    bool ok = false;
    bool includeRejected = false;
    bool includeDeleted = false;
    std::string campus;
    std::string startDate;
    std::string endDate;
    std::string error;
    Summary summary;
    std::vector<DetailRow> rows;
};

int S(HWND hwnd, int value) {
    return static_cast<int>(value * search::dpi_scale_factor(hwnd));
}

HWND label(HWND parent, const wchar_t* text, DWORD align = SS_RIGHT) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | align,
                           0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND datePicker(HWND parent, int id) {
    HWND control = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATECENTURYFORMAT,
        0, 0, 0, 0, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
    DateTime_SetFormat(control, L"yyyy-MM-dd");
    return control;
}

HWND comboBox(HWND parent, int id) {
    return CreateWindowExW(0, WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 0, 0, 0, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

void addComboItems(HWND combo, const wchar_t* const* values, int count) {
    for (int i = 0; i < count; ++i) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(values[i]));
    }
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

std::wstring selectedComboText(HWND combo) {
    const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (index < 0) return {};
    wchar_t text[64]{};
    SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(text));
    return text;
}

std::string dateText(HWND control) {
    SYSTEMTIME value{};
    if (DateTime_GetSystemtime(control, &value) != GDT_VALID) return {};
    char text[16]{};
    sprintf_s(text, "%04u-%02u-%02u", value.wYear, value.wMonth, value.wDay);
    return text;
}

void setDefaultDates(HWND startDate, HWND endDate) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    now.wHour = now.wMinute = now.wSecond = now.wMilliseconds = 0;
    DateTime_SetSystemtime(startDate, GDT_VALID, &now);
    DateTime_SetSystemtime(endDate, GDT_VALID, &now);
}

void setStatus(State* st, const std::wstring& text) {
    if (st) search::set_page_status(st->feedback, text);
}

void setQueryControlsEnabled(State* st, bool enabled) {
    if (!st) return;
    const BOOL value = enabled ? TRUE : FALSE;
    EnableWindow(st->query, value);
    EnableWindow(st->startDate, value);
    EnableWindow(st->endDate, value);
    EnableWindow(st->campus, value);
    EnableWindow(st->includeRejected, value);
    EnableWindow(st->includeDeleted, value);
}

void initList(HWND list, const ListColumn* columns, int count) {
    for (int i = 0; i < count; ++i) {
        search::add_list_column(list, i, columns[i].title, columns[i].width);
    }
}

void populateOneRow(HWND list, const std::vector<int>& values) {
    if (!list || values.empty()) return;
    ListView_DeleteAllItems(list);
    auto first = std::to_wstring(values.front());
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = 0;
    item.pszText = const_cast<wchar_t*>(first.c_str());
    ListView_InsertItem(list, &item);
    for (int col = 1; col < static_cast<int>(values.size()); ++col) {
        auto value = std::to_wstring(values[static_cast<size_t>(col)]);
        ListView_SetItemText(list, 0, col, const_cast<wchar_t*>(value.c_str()));
    }
}

void populateSummary(State* st) {
    if (!st) return;
    populateOneRow(st->mainSummary, {
        st->summary.total_count,
        st->summary.unreviewed_count,
        st->summary.reviewed_count,
        st->summary.completed_count,
        st->summary.rejected_count,
        st->summary.deleted_count,
        st->summary.emergency_count,
        st->summary.routine_count,
        st->summary.backup_count,
    });
}

std::string cellValue(const DetailRow& row, int column) {
    switch (column) {
        case COL_CAMPUS: return row.campus;
        case COL_URGENCY: return row.tran_property;
        case COL_APPLY_FORM_NO: return row.apply_form_no;
        case COL_APPLY_TIME: return row.apply_time;
        case COL_APPLY_STATUS: return row.apply_status;
        case COL_REMARK: return row.remark;
        case COL_PATIENT_NO: return row.patient_no;
        case COL_PATIENT_NO_TYPE: return row.patient_no_type;
        case COL_PATIENT_NAME: return row.patient_name;
        case COL_APPLY_DEPT: return row.apply_dept;
        case COL_BED_NO: return row.bed_no;
        case COL_APPLY_DOCTOR: return row.apply_doctor;
        case COL_TRAN_PROPERTY: return row.tran_property;
        case COL_DELETE_BIT: return row.delete_bit ? "是" : "否";
        case COL_DATA_STATUS: return row.data_status;
        default: return {};
    }
}

COLORREF rowStatusColor(const DetailRow& row) {
    if (row.delete_bit || row.apply_status == "已删除") return COLOR_DELETED;
    if (row.apply_status == "未审核") return COLOR_UNREVIEWED;
    if (row.apply_status == "已审核") return COLOR_REVIEWED;
    if (row.apply_status == "已完结") return COLOR_COMPLETED;
    if (row.apply_status == "已驳回") return COLOR_REJECTED;
    return COLOR_OTHER;
}

COLORREF detailCellColor(const DetailRow& row, int column) {
    if (column == COL_URGENCY &&
        search::classify_transfusion_order_urgency(row.tran_property) ==
            search::TransfusionOrderUrgencyCategory::Emergency) {
        return COLOR_EMERGENCY;
    }
    return rowStatusColor(row);
}

void populateDetails(State* st) {
    if (!st || !st->details) return;
    SendMessageW(st->details, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(st->details);
    for (int i = 0; i < static_cast<int>(st->rows.size()); ++i) {
        const auto& row = st->rows[static_cast<size_t>(i)];
        auto first = search::utf8_to_wide(cellValue(row, 0));
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<wchar_t*>(first.c_str());
        ListView_InsertItem(st->details, &item);
        for (int col = 1; col < DETAIL_COLUMN_COUNT; ++col) {
            auto value = search::utf8_to_wide(cellValue(row, col));
            ListView_SetItemText(st->details, i, col, const_cast<wchar_t*>(value.c_str()));
        }
    }
    SendMessageW(st->details, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(st->details, nullptr, TRUE);
}

void sortRows(State* st, int column, bool toggle) {
    if (!st) return;
    if (toggle) {
        if (st->sortColumn == column) st->sortAscending = !st->sortAscending;
        else { st->sortColumn = column; st->sortAscending = true; }
    }
    const int sortColumn = st->sortColumn;
    const bool ascending = st->sortAscending;
    std::stable_sort(st->rows.begin(), st->rows.end(), [sortColumn, ascending](const auto& a, const auto& b) {
        const auto av = cellValue(a, sortColumn);
        const auto bv = cellValue(b, sortColumn);
        return ascending ? av < bv : av > bv;
    });
}

void openBloodRequestForRow(HWND owner, State* st, int index) {
    if (!st || index < 0 || index >= static_cast<int>(st->rows.size())) return;
    const auto& row = st->rows[static_cast<size_t>(index)];
    if (row.delete_bit || row.apply_status == "已删除") {
        MessageBoxW(owner, L"该申请单已删除，当前输血结果查询不显示已删除记录。",
                    WINDOW_TITLE, MB_ICONINFORMATION);
        return;
    }
    auto* target = new BloodRequestOpenTarget{search::trim(row.apply_form_no), search::trim(row.apply_time)};
    HWND blood = create_blood_module(st->ctx);
    if (!blood || !PostMessageW(blood, WM_BLOOD_OPEN_REQUEST, 0, reinterpret_cast<LPARAM>(target))) {
        delete target;
        MessageBoxW(owner, L"输血结果查询页面打开失败。", WINDOW_TITLE, MB_ICONERROR);
    }
}

struct LegendItem { const wchar_t* text; COLORREF color; int x; };

LRESULT CALLBACK legendProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_ERASEBKGND) return 1;
    if (msg != WM_PAINT) return DefWindowProcW(hwnd, msg, wp, lp);
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc{};
    GetClientRect(hwnd, &rc);
    FillRect(dc, &rc, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
    SetBkMode(dc, TRANSPARENT);
    HFONT font = nullptr;
    if (auto* st = reinterpret_cast<State*>(GetPropW(GetParent(hwnd), PROP_STATE))) font = st->ctx.uiFont;
    HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
    const float scale = search::dpi_scale_factor(hwnd);
    const auto sv = [scale](int value) { return static_cast<int>(value * scale); };
    RECT title{0, 0, sv(64), rc.bottom};
    DrawTextW(dc, L"状态图例：", -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    const LegendItem items[] = {
        {L"未审核", COLOR_UNREVIEWED, 68}, {L"已审核", COLOR_REVIEWED, 150},
        {L"已完结", COLOR_COMPLETED, 232}, {L"已驳回", COLOR_REJECTED, 314},
        {L"已删除", COLOR_DELETED, 396},
    };
    for (const auto& item : items) {
        RECT swatch{sv(item.x), sv(5), sv(item.x + 14), sv(19)};
        HBRUSH brush = CreateSolidBrush(item.color);
        FillRect(dc, &swatch, brush);
        DeleteObject(brush);
        FrameRect(dc, &swatch, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
        RECT text{sv(item.x + 19), 0, rc.right, rc.bottom};
        DrawTextW(dc, item.text, -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    if (oldFont) SelectObject(dc, oldFont);
    EndPaint(hwnd, &ps);
    return 0;
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

void drawSummaryGroup(HDC dc, RECT rect, const wchar_t* text, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
    FrameRect(dc, &rect, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    DrawTextW(dc, text, -1, &rect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

LRESULT CALLBACK summaryGroupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_ERASEBKGND) return 1;
    if (msg != WM_PAINT) return DefWindowProcW(hwnd, msg, wp, lp);

    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);
    FillRect(dc, &client, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
    SetBkMode(dc, TRANSPARENT);

    auto* st = reinterpret_cast<State*>(GetPropW(GetParent(hwnd), PROP_STATE));
    HGDIOBJ oldFont = st && st->ctx.uiFont ? SelectObject(dc, st->ctx.uiFont) : nullptr;
    if (st && st->mainSummary) {
        int edges[SUMMARY_COLUMN_COUNT + 1]{};
        for (int column = 0; column < SUMMARY_COLUMN_COUNT; ++column) {
            edges[column + 1] = edges[column] + ListView_GetColumnWidth(st->mainSummary, column);
        }
        const int horizontalOffset = GetScrollPos(st->mainSummary, SB_HORZ);
        for (int& edge : edges) edge -= horizontalOffset;
        drawSummaryGroup(dc, RECT{edges[SUMMARY_TOTAL], 0, edges[SUMMARY_UNREVIEWED], client.bottom},
                         L"总体", COLOR_SUMMARY_TOTAL);
        drawSummaryGroup(dc, RECT{edges[SUMMARY_UNREVIEWED], 0, edges[SUMMARY_EMERGENCY], client.bottom},
                         L"按申请状态分类", COLOR_SUMMARY_STATUS);
        drawSummaryGroup(dc, RECT{edges[SUMMARY_EMERGENCY], 0, edges[SUMMARY_COLUMN_COUNT], client.bottom},
                         L"按紧急程度分类", COLOR_SUMMARY_URGENCY);
    }
    if (oldFont) SelectObject(dc, oldFont);
    EndPaint(hwnd, &ps);
    return 0;
}

void registerSummaryGroupClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = summaryGroupProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = SUMMARY_GROUP_CLASS;
    RegisterClassW(&wc);
    registered = true;
}

void resizeSummaryColumns(State* st, int availableWidth) {
    if (!st || !st->mainSummary || availableWidth <= 0) return;
    int used = 0;
    for (int column = 0; column < SUMMARY_COLUMN_COUNT; ++column) {
        const int columnWidth = column == SUMMARY_COLUMN_COUNT - 1
            ? availableWidth - used
            : availableWidth * MAIN_SUMMARY_COLUMN_PERCENTAGES[column] / 100;
        ListView_SetColumnWidth(st->mainSummary, column, columnWidth);
        used += columnWidth;
    }
    if (st->summaryGroups) InvalidateRect(st->summaryGroups, nullptr, TRUE);
}

void resizeLayout(HWND hwnd, State* st) {
    if (!st) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int width = rc.right;
    const int height = rc.bottom;
    const int pad = S(hwnd, 10);
    const int labelGap = S(hwnd, 6);
    const int gap = S(hwnd, 9);
    const int groupGap = S(hwnd, 18);
    const int controlH = S(hwnd, 25);
    const int row1 = S(hwnd, 9);
    const int row2 = S(hwnd, 42);
    const int topH = S(hwnd, 102);
    const int groupHeaderH = S(hwnd, 24);
    const int summaryH = S(hwnd, 62);

    int x = pad;
    int labelW = search::measure_control_text_width(hwnd, st->dateLabel, 72);
    MoveWindow(st->dateLabel, x, row1 + S(hwnd, 2), labelW, controlH, TRUE);
    x += labelW + labelGap;
    MoveWindow(st->startDate, x, row1, S(hwnd, 118), controlH, TRUE);
    x += S(hwnd, 118) + gap;
    labelW = search::measure_control_text_width(hwnd, st->dateToLabel, 20);
    MoveWindow(st->dateToLabel, x, row1 + S(hwnd, 2), labelW, controlH, TRUE);
    x += labelW + gap;
    MoveWindow(st->endDate, x, row1, S(hwnd, 118), controlH, TRUE);
    x += S(hwnd, 118) + groupGap;
    labelW = search::measure_control_text_width(hwnd, st->campusLabel, 48);
    MoveWindow(st->campusLabel, x, row1 + S(hwnd, 2), labelW, controlH, TRUE);
    x += labelW + labelGap;
    MoveWindow(st->campus, x, row1, S(hwnd, 82), S(hwnd, 180), TRUE);

    x = pad;
    MoveWindow(st->includeRejected, x, row2, S(hwnd, 126), controlH, TRUE);
    x += S(hwnd, 126) + gap;
    MoveWindow(st->includeDeleted, x, row2, S(hwnd, 126), controlH, TRUE);
    x += S(hwnd, 126) + groupGap;
    MoveWindow(st->query, x, row2 - S(hwnd, 1), S(hwnd, 64), S(hwnd, 27), TRUE);
    x += S(hwnd, 64) + gap;
    MoveWindow(st->exportExcel, x, row2 - S(hwnd, 1), S(hwnd, 88), S(hwnd, 27), TRUE);
    x += S(hwnd, 88) + groupGap;
    const int legendW = width - x - pad;
    ShowWindow(st->legend, legendW >= S(hwnd, 460) ? SW_SHOW : SW_HIDE);
    if (legendW > 0) MoveWindow(st->legend, x, row2, legendW, controlH, TRUE);

    MoveWindow(st->status, pad, S(hwnd, 72), (std::max)(S(hwnd, 200), width - pad * 2), S(hwnd, 22), TRUE);
    const int summaryWidth = width - pad * 2;
    resizeSummaryColumns(st, (std::max)(S(hwnd, 1), summaryWidth - S(hwnd, 4)));
    MoveWindow(st->summaryGroups, pad + S(hwnd, 2), topH,
               (std::max)(S(hwnd, 1), summaryWidth - S(hwnd, 4)), groupHeaderH, TRUE);
    MoveWindow(st->mainSummary, pad, topH + groupHeaderH, summaryWidth, summaryH, TRUE);
    const int detailY = topH + groupHeaderH + summaryH + pad;
    MoveWindow(st->details, pad, detailY, width - pad * 2,
               (std::max)(S(hwnd, 100), height - detailY - pad), TRUE);
    search::layout_page_feedback(st->feedback);
}

void runQuery(HWND hwnd, State* st) {
    if (!st || st->querying) return;
    const auto connection = search::build_connection_string_w(st->ctx.dbSettings);
    if (connection.empty()) {
        MessageBoxW(hwnd, L"请先在系统设置中配置数据库连接。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }
    search::TransfusionOrderStatQuery query;
    query.connection_string = search::wide_to_utf8(connection);
    query.start_date = dateText(st->startDate);
    query.end_date = dateText(st->endDate);
    query.campus = search::wide_to_utf8(selectedComboText(st->campus));
    query.include_rejected = SendMessageW(st->includeRejected, BM_GETCHECK, 0, 0) == BST_CHECKED;
    query.include_deleted = SendMessageW(st->includeDeleted, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (query.start_date.empty() || query.end_date.empty() || query.start_date > query.end_date) {
        MessageBoxW(hwnd, L"申请开始日期不能晚于结束日期。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }
    st->querying = true;
    setQueryControlsEnabled(st, false);
    EnableWindow(st->exportExcel, FALSE);
    setStatus(st, L"正在查询输血申请单...");
    search::show_page_activity(st->feedback, L"正在查询输血申请单，请稍候…");
    std::thread([hwnd, query]() {
        auto* result = new QueryResult();
        result->includeRejected = query.include_rejected;
        result->includeDeleted = query.include_deleted;
        result->campus = query.campus;
        result->startDate = query.start_date;
        result->endDate = query.end_date;
        result->ok = search::query_transfusion_order_statistics(
            query, result->summary, result->rows, result->error);
        if (!PostMessageW(hwnd, WM_TRANSFUSION_ORDER_LOADED, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }).detach();
}

std::wstring defaultXlsxName(State* st) {
    std::wstring start = search::utf8_to_wide(st->loadedStartDate);
    std::wstring end = search::utf8_to_wide(st->loadedEndDate);
    std::replace(start.begin(), start.end(), L'-', L'.');
    std::replace(end.begin(), end.end(), L'-', L'.');
    return start + L"-" + end + L"输血单统计明细-" + st->loadedCampus + L".xlsx";
}

void exportXlsx(HWND hwnd, State* st) {
    if (!st || !st->hasLoadedResult || st->rows.empty()) {
        MessageBoxW(hwnd, L"当前没有可导出的输血单明细。", WINDOW_TITLE, MB_ICONINFORMATION);
        return;
    }
    wchar_t path[MAX_PATH]{};
    const auto defaultName = defaultXlsxName(st);
    lstrcpynW(path, defaultName.c_str(), MAX_PATH);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Excel 工作簿 (*.xlsx)\0*.xlsx\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"xlsx";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;

    std::vector<std::string> headers;
    for (int col = 0; col < DETAIL_COLUMN_COUNT; ++col) {
        headers.push_back(search::wide_to_utf8(DETAIL_COLUMNS[col].title));
    }
    std::string error;
    if (!search::write_xlsx_file(
            path, "输血单统计明细", headers, st->rows.size(),
            [st](size_t row, size_t column) {
                return cellValue(st->rows[row], static_cast<int>(column));
            }, error)) {
        MessageBoxW(hwnd, L"导出失败，请确认目标文件可写。", WINDOW_TITLE, MB_ICONERROR);
        return;
    }
    setStatus(st, L"已导出输血单统计明细：" + std::wstring(path));
    MessageBoxW(hwnd, (L"已导出：\n" + std::wstring(path)).c_str(), WINDOW_TITLE, MB_ICONINFORMATION);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<State*>(GetPropW(hwnd, PROP_STATE));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* mcs = reinterpret_cast<MDICREATESTRUCTW*>(cs->lpCreateParams);
            st = reinterpret_cast<State*>(mcs->lParam);
            SetPropW(hwnd, PROP_STATE, st);
            st->bgBrush = CreateSolidBrush(RGB(0xF0, 0xF0, 0xF0));
            registerLegendClass(GetModuleHandleW(nullptr));
            registerSummaryGroupClass(GetModuleHandleW(nullptr));

            st->dateLabel = label(hwnd, L"申请日期：");
            st->startDate = datePicker(hwnd, IDC_START_DATE);
            st->dateToLabel = label(hwnd, L"至", SS_CENTER);
            st->endDate = datePicker(hwnd, IDC_END_DATE);
            setDefaultDates(st->startDate, st->endDate);
            st->campusLabel = label(hwnd, L"院区：");
            st->campus = comboBox(hwnd, IDC_CAMPUS);
            const wchar_t* campuses[] = {L"全部", L"老院", L"新院"};
            addComboItems(st->campus, campuses, static_cast<int>(std::size(campuses)));

            st->includeRejected = CreateWindowExW(0, L"BUTTON", L"包含已驳回",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_INCLUDE_REJECTED), GetModuleHandleW(nullptr), nullptr);
            st->includeDeleted = CreateWindowExW(0, L"BUTTON", L"包含已删除",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_INCLUDE_DELETED), GetModuleHandleW(nullptr), nullptr);
            st->query = search::create_button(hwnd, IDC_QUERY, L"查询", 0, 0, 0, 0);
            st->exportExcel = search::create_button(hwnd, IDC_EXPORT, L"导出明细", 0, 0, 0, 0);
            EnableWindow(st->exportExcel, FALSE);
            st->legend = CreateWindowExW(0, LEGEND_CLASS, L"", WS_CHILD | WS_VISIBLE,
                0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            st->status = label(hwnd, L"请选择申请日期后查询。", SS_LEFT);

            st->summaryGroups = CreateWindowExW(0, SUMMARY_GROUP_CLASS, L"",
                WS_CHILD | WS_VISIBLE,
                0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

            st->mainSummary = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_MAIN_SUMMARY), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(st->mainSummary, LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            initList(st->mainSummary, MAIN_SUMMARY_COLUMNS, static_cast<int>(std::size(MAIN_SUMMARY_COLUMNS)));

            st->details = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_DETAILS), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(st->details, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            initList(st->details, DETAIL_COLUMNS, DETAIL_COLUMN_COUNT);

            search::initialize_page_feedback(st->feedback, hwnd, st->status,
                                             st->details, st->ctx.uiFont);
            search::add_page_tooltip(st->feedback, st->query,
                                     L"按申请日期、院区及当前状态范围查询输血申请单。");
            search::add_page_tooltip(st->feedback, st->exportExcel,
                                     L"导出当前已加载、已排序的全部输血单明细。");
            search::add_page_tooltip(st->feedback, st->includeRejected,
                                     L"勾选后将已驳回申请单纳入查询结果。");
            search::add_page_tooltip(st->feedback, st->includeDeleted,
                                     L"勾选后将已删除申请单纳入查询结果。");
            search::add_page_tooltip(st->feedback, st->details,
                                     L"单击列标题排序；双击记录可跳转输血结果查询。");

            search::apply_font_to_children(hwnd, st->ctx.uiFont);
            populateSummary(st);
            resizeLayout(hwnd, st);
            return 0;
        }
        case WM_SIZE:
            resizeLayout(hwnd, st);
            return 0;
        case WM_COMMAND:
            if (st && search::handle_page_feedback_command(st->feedback, lp, WINDOW_TITLE)) return 0;
            if (LOWORD(wp) == IDC_QUERY) { runQuery(hwnd, st); return 0; }
            if (LOWORD(wp) == IDC_EXPORT) { exportXlsx(hwnd, st); return 0; }
            break;
        case WM_NOTIFY: {
            auto* header = reinterpret_cast<NMHDR*>(lp);
            if (st && header->idFrom == IDC_MAIN_SUMMARY && header->code == LVN_ITEMCHANGING) {
                const auto* change = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((change->uChanged & LVIF_STATE) &&
                    (change->uNewState & (LVIS_SELECTED | LVIS_FOCUSED))) {
                    return TRUE;
                }
            }
            if (st && header->idFrom == IDC_MAIN_SUMMARY && header->code == NM_CUSTOMDRAW) {
                auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    return CDRF_NOTIFYSUBITEMDRAW;
                }
                if (draw->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                    if (draw->iSubItem == SUMMARY_TOTAL) {
                        draw->clrTextBk = COLOR_SUMMARY_TOTAL;
                    } else if (draw->iSubItem >= SUMMARY_UNREVIEWED &&
                               draw->iSubItem < SUMMARY_EMERGENCY) {
                        draw->clrTextBk = COLOR_SUMMARY_STATUS;
                    } else if (draw->iSubItem >= SUMMARY_EMERGENCY &&
                               draw->iSubItem < SUMMARY_COLUMN_COUNT) {
                        draw->clrTextBk = COLOR_SUMMARY_URGENCY;
                    }
                    draw->clrText = draw->iSubItem == SUMMARY_EMERGENCY
                        ? COLOR_EMERGENCY : RGB(0, 0, 0);
                    return CDRF_NEWFONT;
                }
            }
            if (st && header->idFrom == IDC_DETAILS && header->code == NM_CUSTOMDRAW) {
                auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    const size_t index = static_cast<size_t>(draw->nmcd.dwItemSpec);
                    if (index < st->rows.size()) {
                        draw->clrText = RGB(0, 0, 0);
                        draw->clrTextBk = rowStatusColor(st->rows[index]);
                    }
                    return CDRF_NOTIFYSUBITEMDRAW;
                }
                if (draw->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                    const size_t index = static_cast<size_t>(draw->nmcd.dwItemSpec);
                    if (index < st->rows.size()) {
                        draw->clrText = RGB(0, 0, 0);
                        draw->clrTextBk = detailCellColor(st->rows[index], draw->iSubItem);
                    }
                    return CDRF_NEWFONT;
                }
            }
            if (st && header->idFrom == IDC_DETAILS && header->code == LVN_COLUMNCLICK) {
                const auto* info = reinterpret_cast<NMLISTVIEW*>(lp);
                sortRows(st, info->iSubItem, true);
                populateDetails(st);
                return 0;
            }
            if (st && header->idFrom == IDC_DETAILS && header->code == NM_DBLCLK) {
                const auto* item = reinterpret_cast<NMITEMACTIVATE*>(lp);
                openBloodRequestForRow(hwnd, st, item->iItem);
                return 0;
            }
            break;
        }
        case WM_TRANSFUSION_ORDER_LOADED: {
            std::unique_ptr<QueryResult> result(reinterpret_cast<QueryResult*>(lp));
            if (!st) return 0;
            st->querying = false;
            setQueryControlsEnabled(st, true);
            search::hide_page_activity(st->feedback);
            if (!result->ok) {
                EnableWindow(st->exportExcel, st->hasLoadedResult && !st->rows.empty());
                search::show_page_alert(st->feedback,
                    L"查询失败：" + search::utf8_to_wide(result->error));
                return 0;
            }
            st->summary = result->summary;
            st->rows = std::move(result->rows);
            st->hasLoadedResult = true;
            st->loadedIncludeRejected = result->includeRejected;
            st->loadedIncludeDeleted = result->includeDeleted;
            st->loadedCampus = search::utf8_to_wide(result->campus.empty() ? "全部" : result->campus);
            st->loadedStartDate = result->startDate;
            st->loadedEndDate = result->endDate;
            st->sortColumn = COL_APPLY_TIME;
            st->sortAscending = false;
            sortRows(st, st->sortColumn, false);
            populateSummary(st);
            populateDetails(st);
            EnableWindow(st->exportExcel, !st->rows.empty());
            std::wstring text = L"查询结果共 " + std::to_wstring(st->summary.total_count) +
                L" 个输血申请单。院区：" + st->loadedCampus + L"。";
            if (st->loadedIncludeRejected) text += L" 包含已驳回。";
            if (st->loadedIncludeDeleted) text += L" 包含已删除。";
            setStatus(st, text);
            return 0;
        }
        case app::WM_APP_SETTINGS_CHANGED:
        case app::WM_APP_FONT_CHANGED:
            if (st) {
                if (msg == app::WM_APP_FONT_CHANGED && lp) st->ctx.uiFont = reinterpret_cast<HFONT>(lp);
                search::apply_font_to_children(hwnd, st->ctx.uiFont);
                resizeLayout(hwnd, st);
                if (st->legend) InvalidateRect(st->legend, nullptr, TRUE);
                if (st->summaryGroups) InvalidateRect(st->summaryGroups, nullptr, TRUE);
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
            FillRect(reinterpret_cast<HDC>(wp), &rc,
                     st ? st->bgBrush : reinterpret_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH)));
            return 1;
        }
        case WM_DESTROY:
            if (st) {
                search::destroy_page_feedback(st->feedback);
                if (st->bgBrush) DeleteObject(st->bgBrush);
                RemovePropW(hwnd, PROP_STATE);
                delete st;
            }
            return 0;
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND create_transfusion_order_statistics_module(const ModuleContext& ctx) {
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

    auto* st = new State();
    st->ctx = ctx;
    MDICREATESTRUCTW mcs{};
    mcs.szTitle = WINDOW_TITLE;
    mcs.szClass = WND_CLASS;
    mcs.hOwner = ctx.instance;
    mcs.x = mcs.y = mcs.cx = mcs.cy = CW_USEDEFAULT;
    mcs.lParam = reinterpret_cast<LPARAM>(st);
    HWND child = reinterpret_cast<HWND>(
        SendMessageW(ctx.mdiClient, WM_MDICREATE, 0, reinterpret_cast<LPARAM>(&mcs)));
    if (!child) {
        delete st;
        MessageBoxW(ctx.mdiClient, L"输血单统计窗口创建失败。", WINDOW_TITLE, MB_ICONERROR);
        return nullptr;
    }
    SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    return child;
}

#endif
