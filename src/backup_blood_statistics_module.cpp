#include "backup_blood_statistics_module.h"

#ifdef _WIN32

#include "blood_module.h"
#include "main_app.h"
#include "resource.h"
#include "search_core.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"

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

constexpr const wchar_t* WND_CLASS = L"BackupBloodStatisticsModuleChild";
constexpr const wchar_t* LEGEND_CLASS = L"BackupBloodStatisticsStatusLegend";
constexpr const wchar_t* WINDOW_TITLE = L"备血统计";
constexpr const wchar_t* PROP_STATE = L"BackupBloodStatisticsSt";
constexpr UINT WM_BACKUP_BLOOD_LOADED = WM_APP + 0x575;

constexpr COLORREF COLOR_UNREVIEWED = RGB(0xFF, 0xE0, 0xB2);
constexpr COLORREF COLOR_REVIEWED = RGB(0xBB, 0xDE, 0xFB);
constexpr COLORREF COLOR_COMPLETED = RGB(0xC8, 0xE6, 0xC9);
constexpr COLORREF COLOR_REJECTED = RGB(0xFF, 0xCD, 0xD2);
constexpr COLORREF COLOR_DELETED = RGB(0xE0, 0xE0, 0xE0);
constexpr COLORREF COLOR_OTHER_STATUS = RGB(0xFF, 0xFF, 0xFF);

enum ControlId {
    IDC_START_DATE = 7001,
    IDC_END_DATE = 7002,
    IDC_APPLY_STATUS = 7003,
    IDC_BACKUP_TYPE = 7004,
    IDC_QUERY = 7005,
    IDC_EXPORT = 7006,
    IDC_SUMMARY = 7007,
    IDC_DETAILS = 7008,
    IDC_STATUS = 7009,
    IDC_INCLUDE_DELETED = 7010,
    IDC_STATUS_SUMMARY = 7011,
    IDC_CAMPUS = 7012,
};

struct ListColumn {
    const wchar_t* title;
    int width;
};

enum DetailColumn {
    COL_CAMPUS,
    COL_MATCH_SOURCE,
    COL_APPLY_FORM_NO,
    COL_APPLY_TIME,
    COL_TRAN_PROPERTY,
    COL_USE_BLOOD_NOTE,
    COL_APPLY_PURPOSE,
    COL_APPLY_STATUS,
    COL_PATIENT_NO,
    COL_PATIENT_NAME,
    COL_APPLY_DEPT,
    COL_BED_NO,
    COL_DELETE_BIT,
    DETAIL_COLUMN_COUNT,
};

constexpr ListColumn SUMMARY_COLUMNS[] = {
    {L"备血申请单总数", 260},
    {L"申请类型为备血", 260},
    {L"用血备注含备血", 260},
    {L"输血目的含备血", 260},
    {L"多项命中", 200},
    {L"空申请单号异常", 260},
};

constexpr ListColumn DETAIL_COLUMNS[] = {
    {L"院区", 70},
    {L"命中来源", 110},
    {L"申请单号", 160},
    {L"申请时间", 150},
    {L"申请类型", 150},
    {L"用血备注", 240},
    {L"输血目的", 200},
    {L"申请状态", 90},
    {L"病人号", 130},
    {L"姓名", 90},
    {L"申请科室", 180},
    {L"床号", 70},
    {L"删除标志", 80},
};

constexpr ListColumn STATUS_SUMMARY_COLUMNS[] = {
    {L"未审核", 180},
    {L"已审核", 180},
    {L"已完结", 180},
    {L"已驳回", 180},
    {L"已删除", 180},
    {L"其他状态", 180},
};

static_assert(std::size(DETAIL_COLUMNS) == DETAIL_COLUMN_COUNT);

using Summary = search::BackupBloodStatSummary;
using DetailRow = search::BackupBloodStatDetailRow;

struct BackupBloodState {
    ModuleContext ctx;
    HWND dateLabel = nullptr;
    HWND dateToLabel = nullptr;
    HWND applyStatusLabel = nullptr;
    HWND campusLabel = nullptr;
    HWND backupTypeLabel = nullptr;
    HWND startDate = nullptr;
    HWND endDate = nullptr;
    HWND applyStatus = nullptr;
    HWND campus = nullptr;
    HWND backupType = nullptr;
    HWND includeDeleted = nullptr;
    HWND query = nullptr;
    HWND exportCsv = nullptr;
    HWND legend = nullptr;
    HWND summaryList = nullptr;
    HWND statusSummaryList = nullptr;
    HWND details = nullptr;
    HWND status = nullptr;
    HBRUSH bgBrush = nullptr;
    bool querying = false;
    bool hasLoadedResult = false;
    bool loadedIncludeDeleted = false;
    std::wstring loadedCampus = L"全部";
    std::string loadedStartDate;
    std::string loadedEndDate;
    int sortColumn = COL_APPLY_TIME;
    bool sortAscending = false;
    Summary summary;
    std::vector<DetailRow> allRows;
    std::vector<DetailRow> rows;
};

struct BackupBloodQueryResult {
    bool ok = false;
    bool includeDeleted = false;
    std::string campus;
    std::string startDate;
    std::string endDate;
    std::string error;
    Summary summary;
    std::vector<DetailRow> rows;
};

COLORREF rowStatusColor(const DetailRow& row) {
    if (row.delete_bit || row.apply_status == "已删除") return COLOR_DELETED;
    if (row.apply_status == "未审核") return COLOR_UNREVIEWED;
    if (row.apply_status == "已审核") return COLOR_REVIEWED;
    if (row.apply_status == "已完结") return COLOR_COMPLETED;
    if (row.apply_status == "已驳回") return COLOR_REJECTED;
    return COLOR_OTHER_STATUS;
}

struct LegendItem {
    const wchar_t* text;
    COLORREF color;
    int x;
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
            if (auto* st = reinterpret_cast<BackupBloodState*>(GetPropW(GetParent(hwnd), PROP_STATE))) {
                font = st->ctx.uiFont;
            }
            HGDIOBJ oldFont = nullptr;
            if (font) oldFont = SelectObject(dc, font);

            const float scale = search::dpi_scale_factor(hwnd);
            const auto scaleValue = [scale](int value) { return static_cast<int>(value * scale); };
            RECT titleRc{0, 0, scaleValue(64), rc.bottom};
            DrawTextW(dc, L"状态图例：", -1, &titleRc,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            const LegendItem items[] = {
                {L"未审核", COLOR_UNREVIEWED, 68},
                {L"已审核", COLOR_REVIEWED, 150},
                {L"已完结", COLOR_COMPLETED, 232},
                {L"已驳回", COLOR_REJECTED, 314},
                {L"已删除", COLOR_DELETED, 396},
            };
            for (const auto& item : items) {
                RECT swatch{scaleValue(item.x), scaleValue(5),
                            scaleValue(item.x + 14), scaleValue(19)};
                HBRUSH brush = CreateSolidBrush(item.color);
                FillRect(dc, &swatch, brush);
                DeleteObject(brush);
                FrameRect(dc, &swatch, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
                RECT textRc{scaleValue(item.x + 19), 0, rc.right, rc.bottom};
                DrawTextW(dc, item.text, -1, &textRc,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
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

int S(HWND hwnd, int value) {
    return static_cast<int>(value * search::dpi_scale_factor(hwnd));
}

HWND label(HWND parent, const wchar_t* text, int x, int y, int w, int h, DWORD align = SS_RIGHT) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | align,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND datePicker(HWND parent, int id, int x, int y, int w, int h) {
    HWND control = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATECENTURYFORMAT,
        x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
    DateTime_SetFormat(control, L"yyyy-MM-dd");
    return control;
}

HWND comboBox(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowExW(0, WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
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
    SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(text));
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
    SYSTEMTIME start = now;
    start.wDay = 1;
    start.wHour = start.wMinute = start.wSecond = start.wMilliseconds = 0;
    now.wHour = now.wMinute = now.wSecond = now.wMilliseconds = 0;
    DateTime_SetSystemtime(startDate, GDT_VALID, &start);
    DateTime_SetSystemtime(endDate, GDT_VALID, &now);
}

void setStatus(BackupBloodState* st, const std::wstring& text) {
    if (st && st->status) SetWindowTextW(st->status, text.c_str());
}

void setQueryControlsEnabled(BackupBloodState* st, bool enabled) {
    if (!st) return;
    const BOOL value = enabled ? TRUE : FALSE;
    EnableWindow(st->query, value);
    EnableWindow(st->startDate, value);
    EnableWindow(st->endDate, value);
    EnableWindow(st->applyStatus, value);
    EnableWindow(st->campus, value);
    EnableWindow(st->includeDeleted, value);
}

void initList(HWND list, const ListColumn* columns, int count) {
    for (int i = 0; i < count; ++i) {
        search::add_list_column(list, i, columns[i].title, columns[i].width);
    }
}

void setCellUtf8(HWND list, int row, int col, const std::string& value) {
    const auto wide = search::utf8_to_wide(value);
    ListView_SetItemText(list, row, col, const_cast<wchar_t*>(wide.c_str()));
}

int summaryValue(const Summary& summary, int column) {
    switch (column) {
        case 0: return summary.total_count;
        case 1: return summary.apply_type_count;
        case 2: return summary.use_blood_note_count;
        case 3: return summary.apply_purpose_count;
        case 4: return summary.multiple_match_count;
        case 5: return summary.missing_apply_form_no_count;
        default: return 0;
    }
}

void populateSummary(BackupBloodState* st) {
    if (!st || !st->summaryList) return;
    ListView_DeleteAllItems(st->summaryList);
    const auto first = std::to_wstring(summaryValue(st->summary, 0));
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = 0;
    item.pszText = const_cast<wchar_t*>(first.c_str());
    ListView_InsertItem(st->summaryList, &item);
    for (int col = 1; col < static_cast<int>(std::size(SUMMARY_COLUMNS)); ++col) {
        const auto value = std::to_wstring(summaryValue(st->summary, col));
        ListView_SetItemText(st->summaryList, 0, col, const_cast<wchar_t*>(value.c_str()));
    }
}

void populateStatusSummary(BackupBloodState* st) {
    if (!st || !st->statusSummaryList) return;
    const int values[] = {
        st->summary.unreviewed_count,
        st->summary.reviewed_count,
        st->summary.completed_count,
        st->summary.rejected_count,
        st->summary.deleted_count,
        st->summary.other_status_count,
    };
    ListView_DeleteAllItems(st->statusSummaryList);
    const auto first = std::to_wstring(values[0]);
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = 0;
    item.pszText = const_cast<wchar_t*>(first.c_str());
    ListView_InsertItem(st->statusSummaryList, &item);
    for (int col = 1; col < static_cast<int>(std::size(STATUS_SUMMARY_COLUMNS)); ++col) {
        const auto value = std::to_wstring(values[col]);
        ListView_SetItemText(st->statusSummaryList, 0, col, const_cast<wchar_t*>(value.c_str()));
    }
}

std::string cellValue(const DetailRow& row, int column) {
    switch (column) {
        case COL_CAMPUS: return row.campus;
        case COL_MATCH_SOURCE: return row.match_source;
        case COL_APPLY_FORM_NO: return row.apply_form_no;
        case COL_APPLY_TIME: return row.apply_time;
        case COL_TRAN_PROPERTY: return row.tran_property;
        case COL_USE_BLOOD_NOTE: return row.use_blood_note;
        case COL_APPLY_PURPOSE: return row.apply_purpose;
        case COL_APPLY_STATUS: return row.apply_status;
        case COL_PATIENT_NO: return row.patient_no;
        case COL_PATIENT_NAME: return row.patient_name;
        case COL_APPLY_DEPT: return row.apply_dept;
        case COL_BED_NO: return row.bed_no;
        case COL_DELETE_BIT: return row.delete_bit ? "是" : "否";
        default: return {};
    }
}

void populateDetails(BackupBloodState* st) {
    if (!st || !st->details) return;
    SendMessageW(st->details, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(st->details);
    for (int i = 0; i < static_cast<int>(st->rows.size()); ++i) {
        const auto& row = st->rows[static_cast<size_t>(i)];
        const auto first = search::utf8_to_wide(cellValue(row, COL_CAMPUS));
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<wchar_t*>(first.c_str());
        ListView_InsertItem(st->details, &item);
        for (int col = 1; col < static_cast<int>(std::size(DETAIL_COLUMNS)); ++col) {
            setCellUtf8(st->details, i, col, cellValue(row, col));
        }
    }
    SendMessageW(st->details, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(st->details, nullptr, TRUE);
}

void sortRows(BackupBloodState* st, int column, bool toggle) {
    if (!st) return;
    if (toggle) {
        if (st->sortColumn == column) {
            st->sortAscending = !st->sortAscending;
        } else {
            st->sortColumn = column;
            st->sortAscending = true;
        }
    } else if (st->sortColumn != column) {
        st->sortColumn = column;
    }
    const int sortColumn = st->sortColumn;
    const bool ascending = st->sortAscending;
    std::stable_sort(st->rows.begin(), st->rows.end(), [sortColumn, ascending](const auto& a, const auto& b) {
        const auto av = cellValue(a, sortColumn);
        const auto bv = cellValue(b, sortColumn);
        return ascending ? av < bv : av > bv;
    });
}

bool matchesCurrentBackupType(const DetailRow& row, const std::wstring& type) {
    if (type == L"申请类型") return row.apply_type_match;
    if (type == L"用血备注") return row.use_blood_note_match;
    if (type == L"输血目的") return row.apply_purpose_match;
    if (type == L"多项命中") {
        const int count = static_cast<int>(row.apply_type_match) +
                          static_cast<int>(row.use_blood_note_match) +
                          static_cast<int>(row.apply_purpose_match);
        return count >= 2;
    }
    return row.apply_type_match || row.use_blood_note_match || row.apply_purpose_match;
}

void applyBackupTypeFilter(BackupBloodState* st) {
    if (!st) return;
    const auto type = selectedComboText(st->backupType);
    st->rows.clear();
    for (const auto& row : st->allRows) {
        if (matchesCurrentBackupType(row, type)) st->rows.push_back(row);
    }
    sortRows(st, st->sortColumn, false);
    populateDetails(st);
    EnableWindow(st->exportCsv, st->hasLoadedResult && !st->rows.empty());
    if (st->hasLoadedResult) {
        setStatus(st, L"查询结果共 " + std::to_wstring(st->summary.total_count) +
                      L" 个备血申请单，当前显示 " + std::to_wstring(st->rows.size()) + L" 个。" +
                      L" 院区：" + st->loadedCampus + L"。" +
                      (st->loadedIncludeDeleted ? L" 当前结果包含已删除申请单。" : L""));
    }
}

void openBloodRequestForRow(HWND owner, BackupBloodState* st, int index) {
    if (!st || index < 0 || index >= static_cast<int>(st->rows.size())) return;
    const auto& row = st->rows[static_cast<size_t>(index)];
    if (row.delete_bit || search::trim(row.apply_status) == "已删除") {
        MessageBoxW(owner, L"该申请单已删除，当前输血结果查询不显示已删除记录。",
                    WINDOW_TITLE, MB_ICONINFORMATION);
        return;
    }
    if (search::trim(row.apply_form_no).empty()) {
        MessageBoxW(owner, L"该记录缺少申请单号，无法跳转到输血结果查询。",
                    WINDOW_TITLE, MB_ICONINFORMATION);
        return;
    }

    auto* target = new BloodRequestOpenTarget{
        search::trim(row.apply_form_no),
        search::trim(row.apply_time),
    };
    HWND blood = create_blood_module(st->ctx);
    if (!blood || !PostMessageW(blood, WM_BLOOD_OPEN_REQUEST, 0,
                                reinterpret_cast<LPARAM>(target))) {
        delete target;
        MessageBoxW(owner, L"输血结果查询页面打开失败。", WINDOW_TITLE, MB_ICONERROR);
    }
}

void resizeLayout(HWND hwnd, BackupBloodState* st) {
    if (!st) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    const int pad = S(hwnd, 10);
    const int labelGap = S(hwnd, 6);
    const int controlGap = S(hwnd, 8);
    const int groupGap = S(hwnd, 18);
    const int controlHeight = S(hwnd, 25);
    const int labelYOffset = S(hwnd, 2);
    const int firstRow = S(hwnd, 9);
    const int secondRow = S(hwnd, 42);
    const int topHeight = S(hwnd, 102);
    const int summaryHeight = S(hwnd, 62);
    const int statusSummaryHeight = S(hwnd, 62);

    int x = pad;
    int labelWidth = search::measure_control_text_width(hwnd, st->dateLabel, 72);
    MoveWindow(st->dateLabel, x, firstRow + labelYOffset, labelWidth, controlHeight, TRUE);
    x += labelWidth + labelGap;
    MoveWindow(st->startDate, x, firstRow, S(hwnd, 118), controlHeight, TRUE);
    x += S(hwnd, 118) + controlGap;
    labelWidth = search::measure_control_text_width(hwnd, st->dateToLabel, 20);
    MoveWindow(st->dateToLabel, x, firstRow + labelYOffset, labelWidth, controlHeight, TRUE);
    x += labelWidth + controlGap;
    MoveWindow(st->endDate, x, firstRow, S(hwnd, 118), controlHeight, TRUE);
    x += S(hwnd, 118) + groupGap;
    labelWidth = search::measure_control_text_width(hwnd, st->applyStatusLabel, 72);
    MoveWindow(st->applyStatusLabel, x, firstRow + labelYOffset, labelWidth, controlHeight, TRUE);
    x += labelWidth + labelGap;
    MoveWindow(st->applyStatus, x, firstRow, S(hwnd, 104), S(hwnd, 220), TRUE);
    x += S(hwnd, 104) + groupGap;
    labelWidth = search::measure_control_text_width(hwnd, st->campusLabel, 48);
    MoveWindow(st->campusLabel, x, firstRow + labelYOffset, labelWidth, controlHeight, TRUE);
    x += labelWidth + labelGap;
    MoveWindow(st->campus, x, firstRow, S(hwnd, 82), S(hwnd, 180), TRUE);

    x = pad;
    labelWidth = search::measure_control_text_width(hwnd, st->backupTypeLabel, 72);
    MoveWindow(st->backupTypeLabel, x, secondRow + labelYOffset, labelWidth, controlHeight, TRUE);
    x += labelWidth + labelGap;
    MoveWindow(st->backupType, x, secondRow, S(hwnd, 110), S(hwnd, 220), TRUE);
    x += S(hwnd, 110) + groupGap;
    MoveWindow(st->includeDeleted, x, secondRow, S(hwnd, 126), controlHeight, TRUE);
    x += S(hwnd, 126) + groupGap;
    MoveWindow(st->query, x, secondRow - S(hwnd, 1), S(hwnd, 64), S(hwnd, 27), TRUE);
    x += S(hwnd, 64) + controlGap;
    MoveWindow(st->exportCsv, x, secondRow - S(hwnd, 1), S(hwnd, 88), S(hwnd, 27), TRUE);
    x += S(hwnd, 88) + groupGap;
    const int legendWidth = width - x - pad;
    ShowWindow(st->legend, legendWidth >= S(hwnd, 460) ? SW_SHOW : SW_HIDE);
    if (legendWidth > 0) {
        MoveWindow(st->legend, x, secondRow, legendWidth, controlHeight, TRUE);
    }

    MoveWindow(st->status, pad, S(hwnd, 72), (std::max)(S(hwnd, 200), width - pad * 2), S(hwnd, 22), TRUE);
    MoveWindow(st->summaryList, pad, topHeight, width - pad * 2, summaryHeight, TRUE);
    MoveWindow(st->statusSummaryList, pad, topHeight + summaryHeight + pad,
               width - pad * 2, statusSummaryHeight, TRUE);
    MoveWindow(st->details, pad, topHeight + summaryHeight + statusSummaryHeight + pad * 2,
               width - pad * 2,
               (std::max)(S(hwnd, 100), height - topHeight - summaryHeight - statusSummaryHeight - pad * 3), TRUE);
}

void runQuery(HWND hwnd, BackupBloodState* st) {
    if (!st || st->querying) return;
    const auto connection = search::build_connection_string_w(st->ctx.dbSettings);
    if (connection.empty()) {
        MessageBoxW(hwnd, L"请先在系统设置中配置数据库连接。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }

    search::BackupBloodStatQuery query;
    query.connection_string = search::wide_to_utf8(connection);
    query.start_date = dateText(st->startDate);
    query.end_date = dateText(st->endDate);
    query.apply_status = search::wide_to_utf8(selectedComboText(st->applyStatus));
    query.campus = search::wide_to_utf8(selectedComboText(st->campus));
    query.include_deleted = SendMessageW(st->includeDeleted, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (query.start_date.empty() || query.end_date.empty() || query.start_date > query.end_date) {
        MessageBoxW(hwnd, L"申请开始日期不能晚于结束日期。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }

    st->querying = true;
    setQueryControlsEnabled(st, false);
    EnableWindow(st->exportCsv, FALSE);
    setStatus(st, L"正在查询备血申请单...");

    std::thread([hwnd, query]() {
        auto* result = new BackupBloodQueryResult();
        result->includeDeleted = query.include_deleted;
        result->campus = query.campus;
        result->startDate = query.start_date;
        result->endDate = query.end_date;
        result->ok = search::query_backup_blood_statistics(
            query, result->summary, result->rows, result->error);
        if (!PostMessageW(hwnd, WM_BACKUP_BLOOD_LOADED, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }).detach();
}

std::string csvEscape(const std::string& text) {
    const bool quoted = text.find_first_of(",\"\r\n") != std::string::npos;
    std::string result;
    if (quoted) result.push_back('"');
    for (const char ch : text) {
        if (ch == '"') result += "\"\"";
        else result.push_back(ch);
    }
    if (quoted) result.push_back('"');
    return result;
}

bool writeBytes(const std::wstring& path, const std::string& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = bytes.empty() || WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == bytes.size();
}

std::wstring defaultCsvName(BackupBloodState* st) {
    std::wstring start = search::utf8_to_wide(st->loadedStartDate);
    std::wstring end = search::utf8_to_wide(st->loadedEndDate);
    std::replace(start.begin(), start.end(), L'-', L'.');
    std::replace(end.begin(), end.end(), L'-', L'.');
    return start + L"-" + end + L"备血统计明细-" + st->loadedCampus + L".csv";
}

void exportCsv(HWND hwnd, BackupBloodState* st) {
    if (!st || !st->hasLoadedResult || st->rows.empty()) {
        MessageBoxW(hwnd, L"当前没有可导出的备血明细。", WINDOW_TITLE, MB_ICONINFORMATION);
        return;
    }

    wchar_t path[MAX_PATH]{};
    const auto defaultName = defaultCsvName(st);
    lstrcpynW(path, defaultName.c_str(), MAX_PATH);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"CSV 文件 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"csv";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;

    std::string csv = "\xEF\xBB\xBF";
    for (int col = 0; col < static_cast<int>(std::size(DETAIL_COLUMNS)); ++col) {
        if (col > 0) csv.push_back(',');
        csv += csvEscape(search::wide_to_utf8(DETAIL_COLUMNS[col].title));
    }
    csv.push_back('\n');
    for (const auto& row : st->rows) {
        for (int col = 0; col < static_cast<int>(std::size(DETAIL_COLUMNS)); ++col) {
            if (col > 0) csv.push_back(',');
            csv += csvEscape(cellValue(row, col));
        }
        csv.push_back('\n');
    }

    if (!writeBytes(path, csv)) {
        MessageBoxW(hwnd, L"导出失败，请确认目标文件可写。", WINDOW_TITLE, MB_ICONERROR);
        return;
    }
    setStatus(st, L"已导出备血统计明细：" + std::wstring(path));
    MessageBoxW(hwnd, (L"已导出：\n" + std::wstring(path)).c_str(), WINDOW_TITLE, MB_ICONINFORMATION);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<BackupBloodState*>(GetPropW(hwnd, PROP_STATE));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* mcs = reinterpret_cast<MDICREATESTRUCTW*>(cs->lpCreateParams);
            st = reinterpret_cast<BackupBloodState*>(mcs->lParam);
            SetPropW(hwnd, PROP_STATE, st);
            st->bgBrush = CreateSolidBrush(RGB(0xF0, 0xF0, 0xF0));
            registerLegendClass(GetModuleHandleW(nullptr));

            st->dateLabel = label(hwnd, L"申请日期：", 0, 0, 0, 0);
            st->startDate = datePicker(hwnd, IDC_START_DATE, S(hwnd, 82), S(hwnd, 9), S(hwnd, 118), S(hwnd, 25));
            st->dateToLabel = label(hwnd, L"至", 0, 0, 0, 0, SS_CENTER);
            st->endDate = datePicker(hwnd, IDC_END_DATE, S(hwnd, 232), S(hwnd, 9), S(hwnd, 118), S(hwnd, 25));
            setDefaultDates(st->startDate, st->endDate);

            st->applyStatusLabel = label(hwnd, L"申请状态：", 0, 0, 0, 0);
            st->applyStatus = comboBox(hwnd, IDC_APPLY_STATUS, S(hwnd, 438), S(hwnd, 9), S(hwnd, 124), S(hwnd, 220));
            const wchar_t* statuses[] = {L"全部", L"未审核", L"已审核", L"已完结", L"已驳回"};
            addComboItems(st->applyStatus, statuses, static_cast<int>(std::size(statuses)));

            st->campusLabel = label(hwnd, L"院区：", 0, 0, 0, 0);
            st->campus = comboBox(hwnd, IDC_CAMPUS, S(hwnd, 630), S(hwnd, 9), S(hwnd, 82), S(hwnd, 180));
            const wchar_t* campuses[] = {L"全部", L"老院", L"新院"};
            addComboItems(st->campus, campuses, static_cast<int>(std::size(campuses)));

            st->includeDeleted = CreateWindowExW(0, L"BUTTON", L"包含已删除",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                S(hwnd, 82), S(hwnd, 42), S(hwnd, 126), S(hwnd, 25), hwnd,
                win32_control_id(IDC_INCLUDE_DELETED), GetModuleHandleW(nullptr), nullptr);

            st->backupTypeLabel = label(hwnd, L"备血类型：", 0, 0, 0, 0);
            st->backupType = comboBox(hwnd, IDC_BACKUP_TYPE, S(hwnd, 306), S(hwnd, 42), S(hwnd, 110), S(hwnd, 220));
            const wchar_t* backupTypes[] = {
                L"全部", L"申请类型", L"用血备注", L"输血目的", L"多项命中"
            };
            addComboItems(st->backupType, backupTypes, static_cast<int>(std::size(backupTypes)));

            st->query = search::create_button(hwnd, IDC_QUERY, L"查询", S(hwnd, 468), S(hwnd, 41), S(hwnd, 58), S(hwnd, 27));
            st->exportCsv = search::create_button(hwnd, IDC_EXPORT, L"导出明细", S(hwnd, 534), S(hwnd, 41), S(hwnd, 82), S(hwnd, 27));
            EnableWindow(st->exportCsv, FALSE);
            st->legend = CreateWindowExW(0, LEGEND_CLASS, L"", WS_CHILD | WS_VISIBLE,
                S(hwnd, 650), S(hwnd, 42), S(hwnd, 548), S(hwnd, 25), hwnd, nullptr,
                GetModuleHandleW(nullptr), nullptr);
            st->status = label(hwnd, L"请选择申请日期后查询。", 0, 0, 0, 0, SS_LEFT);

            st->summaryList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_SUMMARY), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(st->summaryList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            initList(st->summaryList, SUMMARY_COLUMNS, static_cast<int>(std::size(SUMMARY_COLUMNS)));

            st->statusSummaryList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_STATUS_SUMMARY), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(st->statusSummaryList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            initList(st->statusSummaryList, STATUS_SUMMARY_COLUMNS,
                     static_cast<int>(std::size(STATUS_SUMMARY_COLUMNS)));

            st->details = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_DETAILS), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(st->details, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            initList(st->details, DETAIL_COLUMNS, static_cast<int>(std::size(DETAIL_COLUMNS)));

            search::apply_font_to_children(hwnd, st->ctx.uiFont);
            populateSummary(st);
            populateStatusSummary(st);
            resizeLayout(hwnd, st);
            return 0;
        }
        case WM_SIZE:
            resizeLayout(hwnd, st);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_QUERY) {
                runQuery(hwnd, st);
                return 0;
            }
            if (LOWORD(wp) == IDC_EXPORT) {
                exportCsv(hwnd, st);
                return 0;
            }
            if (LOWORD(wp) == IDC_BACKUP_TYPE && HIWORD(wp) == CBN_SELCHANGE) {
                applyBackupTypeFilter(st);
                return 0;
            }
            break;
        case WM_NOTIFY: {
            auto* header = reinterpret_cast<NMHDR*>(lp);
            if (st && header->idFrom == IDC_DETAILS && header->code == NM_CUSTOMDRAW) {
                auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    const size_t index = static_cast<size_t>(draw->nmcd.dwItemSpec);
                    if (index < st->rows.size()) {
                        const auto& row = st->rows[index];
                        draw->clrText = RGB(0, 0, 0);
                        draw->clrTextBk = rowStatusColor(row);
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
        case WM_BACKUP_BLOOD_LOADED: {
            std::unique_ptr<BackupBloodQueryResult> result(reinterpret_cast<BackupBloodQueryResult*>(lp));
            if (!st) return 0;
            st->querying = false;
            setQueryControlsEnabled(st, true);
            if (!result->ok) {
                EnableWindow(st->exportCsv, st->hasLoadedResult && !st->rows.empty());
                setStatus(st, L"查询失败：" + search::utf8_to_wide(result->error));
                MessageBoxW(hwnd, search::utf8_to_wide(result->error).c_str(), WINDOW_TITLE, MB_ICONERROR);
                return 0;
            }
            st->summary = result->summary;
            st->allRows = std::move(result->rows);
            st->hasLoadedResult = true;
            st->loadedIncludeDeleted = result->includeDeleted;
            st->loadedCampus = search::utf8_to_wide(result->campus.empty() ? "全部" : result->campus);
            st->loadedStartDate = result->startDate;
            st->loadedEndDate = result->endDate;
            st->sortColumn = COL_APPLY_TIME;
            st->sortAscending = false;
            populateSummary(st);
            populateStatusSummary(st);
            applyBackupTypeFilter(st);
            if (st->summary.total_count == 0) {
                setStatus(st, L"该日期范围内未找到符合条件的备血申请单。院区：" +
                              st->loadedCampus + L"。"
                              + (st->summary.missing_apply_form_no_count > 0
                                     ? L" 空申请单号异常 " + std::to_wstring(st->summary.missing_apply_form_no_count) + L" 条。"
                                     : L""));
            }
            return 0;
        }
        case app::WM_APP_SETTINGS_CHANGED:
        case app::WM_APP_FONT_CHANGED:
            if (st) {
                if (msg == app::WM_APP_FONT_CHANGED && lp) st->ctx.uiFont = reinterpret_cast<HFONT>(lp);
                search::apply_font_to_children(hwnd, st->ctx.uiFont);
                resizeLayout(hwnd, st);
                if (st->legend) InvalidateRect(st->legend, nullptr, TRUE);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        case WM_CTLCOLORSTATIC:
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
                if (st->bgBrush) DeleteObject(st->bgBrush);
                RemovePropW(hwnd, PROP_STATE);
                delete st;
            }
            return 0;
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND create_backup_blood_statistics_module(const ModuleContext& ctx) {
    if (HWND existing = activate_existing_mdi_child_by_title(ctx.mdiClient, WINDOW_TITLE)) {
        return existing;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = ctx.instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(ctx.instance, MAKEINTRESOURCEW(IDI_APP));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = WND_CLASS;
    RegisterClassExW(&wc);

    auto* st = new BackupBloodState();
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
        MessageBoxW(ctx.mdiClient, L"备血统计窗口创建失败。", WINDOW_TITLE, MB_ICONERROR);
        return nullptr;
    }
    SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    return child;
}

#endif
