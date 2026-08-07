#include "massive_transfusion_statistics_module.h"

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
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const wchar_t* WND_CLASS = L"MassiveTransfusionStatisticsModuleChild";
constexpr const wchar_t* WINDOW_TITLE = L"大量输血统计";
constexpr const wchar_t* PROP_STATE = L"MassiveTransfusionStatisticsSt";
constexpr const char* RULE_VERSION = "v2";
constexpr const wchar_t* RULE_VERSION_W = L"v2";
constexpr UINT WM_QUERY_LOADED = WM_APP + 0x576;

enum ControlId {
    IDC_START_DATE = 7201,
    IDC_END_DATE,
    IDC_CAMPUS,
    IDC_QUERY,
    IDC_EXPORT_EVENTS,
    IDC_EXPORT_COMPONENTS,
    IDC_SUMMARY,
    IDC_EVENTS,
    IDC_COMPONENTS,
    IDC_STATUS,
    IDC_DETAIL_MODE,
    IDC_INCLUDE_PLATELET_CRYO,
};

struct Column { const wchar_t* title; int width; };

constexpr Column SUMMARY_COLUMNS[] = {
    {L"大量输血事件", 130}, {L"涉及患者", 110}, {L"有效申请单", 120},
    {L"计量制品项", 120}, {L"累计申请量(ml)", 145}, {L"异常事件", 110},
    {L"异常制品项", 120}, {L"已驳回申请", 120}, {L"空病人号", 105},
    {L"空申请单号", 120},
};

enum EventColumn {
    EVENT_RESULT,
    EVENT_CAMPUS,
    EVENT_PATIENT_NO,
    EVENT_PATIENT_NAME,
    EVENT_PATIENT_TYPE,
    EVENT_FIRST_TIME,
    EVENT_WINDOW_END,
    EVENT_LAST_TIME,
    EVENT_TOTAL_ML,
    EVENT_APPLY_COUNT,
    EVENT_COMPONENT_COUNT,
    EVENT_REJECTED_COUNT,
    EVENT_FIRST_FORM,
    EVENT_FORMS,
    EVENT_COMPOSITIONS,
    EVENT_DEPT,
    EVENT_BED,
    EVENT_STATUSES,
    EVENT_DATA_STATUS,
    EVENT_COLUMN_COUNT,
};

constexpr Column EVENT_COLUMNS[] = {
    {L"结果", 90}, {L"院区", 65}, {L"病人号", 125}, {L"姓名", 85},
    {L"患者类型", 85}, {L"首次申请时间", 145}, {L"窗口结束时间", 145},
    {L"最后申请时间", 145}, {L"折算总量(ml)", 115}, {L"有效申请单", 95},
    {L"计量制品项", 95}, {L"已驳回申请", 95}, {L"第一张申请单", 155},
    {L"全部申请单", 260}, {L"制品构成", 320}, {L"申请科室", 175},
    {L"床号", 65}, {L"申请状态", 145}, {L"数据状态", 125},
};

enum ComponentColumn {
    COMPONENT_EVENT,
    COMPONENT_CAMPUS,
    COMPONENT_PATIENT_NO,
    COMPONENT_PATIENT_NAME,
    COMPONENT_FORM,
    COMPONENT_TIME,
    COMPONENT_STATUS,
    COMPONENT_COUNTED,
    COMPONENT_NAME,
    COMPONENT_NUM,
    COMPONENT_UNIT,
    COMPONENT_FACTOR,
    COMPONENT_ML,
    COMPONENT_DEPT,
    COMPONENT_BED,
    COMPONENT_DOCTOR,
    COMPONENT_DATA_STATUS,
    COMPONENT_COLUMN_COUNT,
};

constexpr Column COMPONENT_COLUMNS[] = {
    {L"事件起点", 145}, {L"院区", 65}, {L"病人号", 125}, {L"姓名", 85},
    {L"申请单号", 155}, {L"申请时间", 145}, {L"申请状态", 90},
    {L"是否计量", 80}, {L"血液制品", 190}, {L"申请数量", 90},
    {L"原单位", 75}, {L"换算因子", 85}, {L"折算量(ml)", 105},
    {L"申请科室", 175}, {L"床号", 65}, {L"申请医生", 90}, {L"数据状态", 190},
};

static_assert(std::size(EVENT_COLUMNS) == EVENT_COLUMN_COUNT);
static_assert(std::size(COMPONENT_COLUMNS) == COMPONENT_COLUMN_COUNT);

using Summary = search::MassiveTransfusionStatSummary;
using EventRow = search::MassiveTransfusionEventRow;
using ComponentRow = search::MassiveTransfusionComponentDetailRow;

struct State {
    ModuleContext ctx;
    HWND startLabel = nullptr;
    HWND toLabel = nullptr;
    HWND campusLabel = nullptr;
    HWND detailLabel = nullptr;
    HWND startDate = nullptr;
    HWND endDate = nullptr;
    HWND campus = nullptr;
    HWND includePlateletCryo = nullptr;
    HWND detailMode = nullptr;
    HWND query = nullptr;
    HWND exportEvents = nullptr;
    HWND exportComponents = nullptr;
    HWND summary = nullptr;
    HWND events = nullptr;
    HWND components = nullptr;
    HWND status = nullptr;
    HBRUSH bgBrush = nullptr;
    bool querying = false;
    bool hasResult = false;
    std::string loadedStart;
    std::string loadedEnd;
    std::wstring loadedCampus = L"全部";
    bool loadedIncludePlateletCryo = false;
    int eventSortColumn = EVENT_FIRST_TIME;
    bool eventSortAscending = false;
    Summary totals;
    std::vector<EventRow> eventRows;
    std::vector<ComponentRow> orphanRejected;
    std::vector<ComponentRow> visibleComponents;
};

struct QueryResult {
    bool ok = false;
    std::string startDate;
    std::string endDate;
    std::string campus;
    bool includePlateletCryo = false;
    std::string error;
    Summary summary;
    std::vector<EventRow> events;
    std::vector<ComponentRow> orphanRejected;
};

int S(HWND hwnd, int value) {
    return static_cast<int>(value * search::dpi_scale_factor(hwnd));
}

HWND makeLabel(HWND parent, const wchar_t* text, DWORD align = SS_RIGHT) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | align,
                           0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND makeDate(HWND parent, int id) {
    HWND control = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATECENTURYFORMAT,
        0, 0, 0, 0, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
    DateTime_SetFormat(control, L"yyyy-MM-dd");
    return control;
}

HWND makeCombo(HWND parent, int id) {
    return CreateWindowExW(0, WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 0, 0, 0, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

HWND makeList(HWND parent, int id) {
    HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        0, 0, 0, 0, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(list,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    return list;
}

void initList(HWND list, const Column* columns, int count) {
    for (int i = 0; i < count; ++i) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = const_cast<wchar_t*>(columns[i].title);
        col.cx = columns[i].width;
        col.iSubItem = i;
        ListView_InsertColumn(list, i, &col);
    }
}

void addComboItems(HWND combo, const wchar_t* const* items, int count) {
    for (int i = 0; i < count; ++i) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(items[i]));
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

std::wstring comboText(HWND combo) {
    const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (index < 0) return L"";
    wchar_t text[128]{};
    SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(text));
    return text;
}

std::string dateText(HWND picker) {
    SYSTEMTIME value{};
    if (DateTime_GetSystemtime(picker, &value) != GDT_VALID) return {};
    char text[11]{};
    std::snprintf(text, sizeof(text), "%04u-%02u-%02u", value.wYear, value.wMonth, value.wDay);
    return text;
}

void setDefaultDates(HWND start, HWND end) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    SYSTEMTIME first = now;
    first.wDay = 1;
    DateTime_SetSystemtime(start, GDT_VALID, &first);
    DateTime_SetSystemtime(end, GDT_VALID, &now);
}

void setCell(HWND list, int row, int column, const std::string& value) {
    const std::wstring wide = search::utf8_to_wide(value);
    if (column == 0) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = row;
        item.pszText = const_cast<wchar_t*>(wide.c_str());
        ListView_InsertItem(list, &item);
    } else {
        ListView_SetItemText(list, row, column, const_cast<wchar_t*>(wide.c_str()));
    }
}

std::string numberText(double value) {
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    std::string text(buffer);
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text;
}

std::string eventCell(const EventRow& row, int column) {
    switch (column) {
        case EVENT_RESULT: return row.qualifies ? "大量输血" : "折算异常";
        case EVENT_CAMPUS: return row.campus;
        case EVENT_PATIENT_NO: return row.patient_no;
        case EVENT_PATIENT_NAME: return row.patient_name;
        case EVENT_PATIENT_TYPE: return row.patient_no_type;
        case EVENT_FIRST_TIME: return row.first_apply_time;
        case EVENT_WINDOW_END: return row.window_end_time;
        case EVENT_LAST_TIME: return row.last_apply_time;
        case EVENT_TOTAL_ML: return row.total_ml;
        case EVENT_APPLY_COUNT: return std::to_string(row.application_count);
        case EVENT_COMPONENT_COUNT: return std::to_string(row.component_count);
        case EVENT_REJECTED_COUNT: return std::to_string(row.rejected_application_count);
        case EVENT_FIRST_FORM: return row.first_apply_form_no;
        case EVENT_FORMS: return row.apply_form_nos;
        case EVENT_COMPOSITIONS: return row.composition_summary;
        case EVENT_DEPT: return row.apply_dept;
        case EVENT_BED: return row.bed_no;
        case EVENT_STATUSES: return row.status_summary;
        case EVENT_DATA_STATUS: return row.data_status;
        default: return {};
    }
}

std::string componentCell(const ComponentRow& row, int column) {
    switch (column) {
        case COMPONENT_EVENT: return row.event_id.empty() ? "独立已驳回" : row.event_id.substr(row.event_id.find('@') + 1);
        case COMPONENT_CAMPUS: return row.campus;
        case COMPONENT_PATIENT_NO: return row.patient_no;
        case COMPONENT_PATIENT_NAME: return row.patient_name;
        case COMPONENT_FORM: return row.apply_form_no;
        case COMPONENT_TIME: return row.apply_time;
        case COMPONENT_STATUS: return row.apply_status;
        case COMPONENT_COUNTED: return row.counted ? "是" : "否";
        case COMPONENT_NAME: return row.composition;
        case COMPONENT_NUM: return row.apply_num;
        case COMPONENT_UNIT: return row.apply_unit;
        case COMPONENT_FACTOR: return row.conversion_factor;
        case COMPONENT_ML: return row.converted_ml;
        case COMPONENT_DEPT: return row.apply_dept;
        case COMPONENT_BED: return row.bed_no;
        case COMPONENT_DOCTOR: return row.apply_doctor;
        case COMPONENT_DATA_STATUS: return row.data_status;
        default: return {};
    }
}

void setStatus(State* state, const std::wstring& text) {
    if (state && state->status) SetWindowTextW(state->status, text.c_str());
}

void populateSummary(State* state) {
    ListView_DeleteAllItems(state->summary);
    const std::string values[] = {
        std::to_string(state->totals.event_count),
        std::to_string(state->totals.patient_count),
        std::to_string(state->totals.application_count),
        std::to_string(state->totals.component_count),
        numberText(state->totals.total_ml),
        std::to_string(state->totals.issue_event_count),
        std::to_string(state->totals.issue_component_count),
        std::to_string(state->totals.rejected_application_count),
        std::to_string(state->totals.missing_patient_no_count),
        std::to_string(state->totals.missing_apply_form_no_count),
    };
    for (int col = 0; col < static_cast<int>(std::size(values)); ++col) setCell(state->summary, 0, col, values[col]);
}

void populateEvents(State* state) {
    SendMessageW(state->events, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(state->events);
    for (int row = 0; row < static_cast<int>(state->eventRows.size()); ++row) {
        for (int col = 0; col < EVENT_COLUMN_COUNT; ++col) setCell(state->events, row, col, eventCell(state->eventRows[row], col));
    }
    SendMessageW(state->events, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(state->events, nullptr, TRUE);
}

void populateComponents(State* state) {
    SendMessageW(state->components, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(state->components);
    for (int row = 0; row < static_cast<int>(state->visibleComponents.size()); ++row) {
        for (int col = 0; col < COMPONENT_COLUMN_COUNT; ++col) {
            setCell(state->components, row, col, componentCell(state->visibleComponents[row], col));
        }
    }
    SendMessageW(state->components, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(state->components, nullptr, TRUE);
}

void refreshComponentScope(State* state) {
    if (!state) return;
    state->visibleComponents.clear();
    const std::wstring mode = comboText(state->detailMode);
    if (mode == L"独立已驳回") {
        state->visibleComponents = state->orphanRejected;
    } else if (mode == L"全部事件成分") {
        for (const auto& event : state->eventRows) {
            state->visibleComponents.insert(state->visibleComponents.end(), event.components.begin(), event.components.end());
        }
    } else {
        const int selected = ListView_GetNextItem(state->events, -1, LVNI_SELECTED);
        if (selected >= 0 && selected < static_cast<int>(state->eventRows.size())) {
            state->visibleComponents = state->eventRows[static_cast<size_t>(selected)].components;
        }
    }
    populateComponents(state);
}

void sortEvents(State* state, int column, bool toggle) {
    if (toggle) {
        if (state->eventSortColumn == column) state->eventSortAscending = !state->eventSortAscending;
        else { state->eventSortColumn = column; state->eventSortAscending = true; }
    }
    const bool ascending = state->eventSortAscending;
    std::stable_sort(state->eventRows.begin(), state->eventRows.end(), [column, ascending](const auto& left, const auto& right) {
        const std::string a = eventCell(left, column);
        const std::string b = eventCell(right, column);
        return ascending ? a < b : a > b;
    });
}

void setQueryEnabled(State* state, bool enabled) {
    EnableWindow(state->startDate, enabled);
    EnableWindow(state->endDate, enabled);
    EnableWindow(state->campus, enabled);
    EnableWindow(state->includePlateletCryo, enabled);
    EnableWindow(state->query, enabled);
}

void resizeLayout(HWND hwnd, State* state) {
    if (!state) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int width = rc.right;
    const int height = rc.bottom;
    const int pad = S(hwnd, 10);
    const int h = S(hwnd, 25);
    const int y = S(hwnd, 9);
    int x = pad;
    int labelWidth = search::measure_control_text_width(hwnd, state->startLabel, 105);
    MoveWindow(state->startLabel, x, y + S(hwnd, 2), labelWidth, h, TRUE); x += labelWidth + S(hwnd, 5);
    MoveWindow(state->startDate, x, y, S(hwnd, 118), h, TRUE); x += S(hwnd, 126);
    MoveWindow(state->toLabel, x, y + S(hwnd, 2), S(hwnd, 20), h, TRUE); x += S(hwnd, 26);
    MoveWindow(state->endDate, x, y, S(hwnd, 118), h, TRUE); x += S(hwnd, 136);
    labelWidth = search::measure_control_text_width(hwnd, state->campusLabel, 50);
    MoveWindow(state->campusLabel, x, y + S(hwnd, 2), labelWidth, h, TRUE); x += labelWidth + S(hwnd, 5);
    MoveWindow(state->campus, x, y, S(hwnd, 82), S(hwnd, 180), TRUE); x += S(hwnd, 96);
    MoveWindow(state->includePlateletCryo, x, y, S(hwnd, 174), h, TRUE); x += S(hwnd, 180);
    MoveWindow(state->query, x, y - S(hwnd, 1), S(hwnd, 62), S(hwnd, 27), TRUE); x += S(hwnd, 70);
    MoveWindow(state->exportEvents, x, y - S(hwnd, 1), S(hwnd, 88), S(hwnd, 27), TRUE); x += S(hwnd, 96);
    MoveWindow(state->exportComponents, x, y - S(hwnd, 1), S(hwnd, 104), S(hwnd, 27), TRUE);

    MoveWindow(state->status, pad, S(hwnd, 42), width - pad * 2, S(hwnd, 22), TRUE);
    const int summaryTop = S(hwnd, 68);
    const int summaryHeight = S(hwnd, 58);
    MoveWindow(state->summary, pad, summaryTop, width - pad * 2, summaryHeight, TRUE);

    const int detailControlsTop = height * 62 / 100;
    const int eventsTop = summaryTop + summaryHeight + pad;
    MoveWindow(state->events, pad, eventsTop, width - pad * 2,
               (std::max)(S(hwnd, 130), detailControlsTop - eventsTop - S(hwnd, 34)), TRUE);
    x = pad;
    labelWidth = search::measure_control_text_width(hwnd, state->detailLabel, 75);
    MoveWindow(state->detailLabel, x, detailControlsTop, labelWidth, h, TRUE); x += labelWidth + S(hwnd, 5);
    MoveWindow(state->detailMode, x, detailControlsTop - S(hwnd, 1), S(hwnd, 130), S(hwnd, 160), TRUE);
    MoveWindow(state->components, pad, detailControlsTop + S(hwnd, 30), width - pad * 2,
               (std::max)(S(hwnd, 90), height - detailControlsTop - S(hwnd, 40)), TRUE);
}

void runQuery(HWND hwnd, State* state) {
    if (!state || state->querying) return;
    const auto connection = search::build_connection_string_w(state->ctx.dbSettings);
    if (connection.empty()) {
        MessageBoxW(hwnd, L"请先在系统设置中配置数据库连接。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }
    search::MassiveTransfusionStatQuery query;
    query.connection_string = search::wide_to_utf8(connection);
    query.start_date = dateText(state->startDate);
    query.end_date = dateText(state->endDate);
    query.campus = search::wide_to_utf8(comboText(state->campus));
    query.include_platelet_and_cryoprecipitate =
        SendMessageW(state->includePlateletCryo, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (query.start_date.empty() || query.end_date.empty() || query.start_date > query.end_date) {
        MessageBoxW(hwnd, L"首次申请开始日期不能晚于结束日期。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }
    state->querying = true;
    setQueryEnabled(state, false);
    EnableWindow(state->exportEvents, FALSE);
    EnableWindow(state->exportComponents, FALSE);
    setStatus(state, L"正在查询并计算24小时大量输血事件...");
    std::thread([hwnd, query]() {
        auto* result = new QueryResult();
        result->startDate = query.start_date;
        result->endDate = query.end_date;
        result->campus = query.campus;
        result->includePlateletCryo = query.include_platelet_and_cryoprecipitate;
        result->ok = search::query_massive_transfusion_statistics(
            query, result->summary, result->events, result->orphanRejected, result->error);
        if (!PostMessageW(hwnd, WM_QUERY_LOADED, 0, reinterpret_cast<LPARAM>(result))) delete result;
    }).detach();
}

std::string csvEscape(const std::string& text) {
    const bool quote = text.find_first_of(",\"\r\n") != std::string::npos;
    std::string out;
    if (quote) out.push_back('"');
    for (char ch : text) out += ch == '"' ? "\"\"" : std::string(1, ch);
    if (quote) out.push_back('"');
    return out;
}

bool writeBytes(const std::wstring& path, const std::string& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = bytes.empty() || WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == bytes.size();
}

bool chooseCsvPath(HWND hwnd, const std::wstring& defaultName, std::wstring& path) {
    wchar_t buffer[MAX_PATH]{};
    lstrcpynW(buffer, defaultName.c_str(), MAX_PATH);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"CSV 文件 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"csv";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return false;
    path = buffer;
    return true;
}

std::wstring defaultName(State* state, const wchar_t* suffix) {
    std::wstring start = search::utf8_to_wide(state->loadedStart);
    std::wstring end = search::utf8_to_wide(state->loadedEnd);
    std::replace(start.begin(), start.end(), L'-', L'.');
    std::replace(end.begin(), end.end(), L'-', L'.');
    return start + L"-" + end + suffix + L"-" + state->loadedCampus + L".csv";
}

std::string exportMetadata(State* state) {
    return std::string(",>=1600ml,") +
           (state->loadedIncludePlateletCryo ? "是," : "否,") + RULE_VERSION + "\n";
}

void exportEventCsv(HWND hwnd, State* state) {
    if (!state || !state->hasResult || state->eventRows.empty()) {
        MessageBoxW(hwnd, L"当前没有可导出的事件明细。", WINDOW_TITLE, MB_ICONINFORMATION);
        return;
    }
    std::wstring path;
    if (!chooseCsvPath(hwnd, defaultName(state, L"大量输血事件"), path)) return;
    std::string csv = "\xEF\xBB\xBF";
    for (int col = 0; col < EVENT_COLUMN_COUNT; ++col) {
        if (col) csv.push_back(',');
        csv += csvEscape(search::wide_to_utf8(EVENT_COLUMNS[col].title));
    }
    csv += ",统计阈值,包括血小板和冷沉淀,折算规则版本\n";
    for (const auto& row : state->eventRows) {
        for (int col = 0; col < EVENT_COLUMN_COUNT; ++col) {
            if (col) csv.push_back(',');
            csv += csvEscape(eventCell(row, col));
        }
        csv += exportMetadata(state);
    }
    if (!writeBytes(path, csv)) {
        MessageBoxW(hwnd, L"导出失败，请确认目标文件可写。", WINDOW_TITLE, MB_ICONERROR);
        return;
    }
    setStatus(state, L"事件明细已导出：" + path);
}

void exportComponentCsv(HWND hwnd, State* state) {
    if (!state || !state->hasResult) return;
    std::vector<ComponentRow> rows;
    for (const auto& event : state->eventRows) rows.insert(rows.end(), event.components.begin(), event.components.end());
    rows.insert(rows.end(), state->orphanRejected.begin(), state->orphanRejected.end());
    if (rows.empty()) {
        MessageBoxW(hwnd, L"当前没有可导出的申请成分明细。", WINDOW_TITLE, MB_ICONINFORMATION);
        return;
    }
    std::wstring path;
    if (!chooseCsvPath(hwnd, defaultName(state, L"大量输血成分明细"), path)) return;
    std::string csv = "\xEF\xBB\xBF";
    for (int col = 0; col < COMPONENT_COLUMN_COUNT; ++col) {
        if (col) csv.push_back(',');
        csv += csvEscape(search::wide_to_utf8(COMPONENT_COLUMNS[col].title));
    }
    csv += ",统计阈值,包括血小板和冷沉淀,折算规则版本\n";
    for (const auto& row : rows) {
        for (int col = 0; col < COMPONENT_COLUMN_COUNT; ++col) {
            if (col) csv.push_back(',');
            csv += csvEscape(componentCell(row, col));
        }
        csv += exportMetadata(state);
    }
    if (!writeBytes(path, csv)) {
        MessageBoxW(hwnd, L"导出失败，请确认目标文件可写。", WINDOW_TITLE, MB_ICONERROR);
        return;
    }
    setStatus(state, L"申请成分明细已导出：" + path);
}

void openBloodRequest(HWND hwnd, State* state, const std::string& form, const std::string& time) {
    if (search::trim(form).empty()) return;
    auto* target = new BloodRequestOpenTarget{search::trim(form), search::trim(time)};
    HWND blood = create_blood_module(state->ctx);
    if (!blood || !PostMessageW(blood, WM_BLOOD_OPEN_REQUEST, 0, reinterpret_cast<LPARAM>(target))) {
        delete target;
        MessageBoxW(hwnd, L"输血结果查询页面打开失败。", WINDOW_TITLE, MB_ICONERROR);
    }
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<State*>(GetPropW(hwnd, PROP_STATE));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* mcs = reinterpret_cast<MDICREATESTRUCTW*>(cs->lpCreateParams);
            state = reinterpret_cast<State*>(mcs->lParam);
            SetPropW(hwnd, PROP_STATE, state);
            state->bgBrush = CreateSolidBrush(RGB(0xF0, 0xF0, 0xF0));
            state->startLabel = makeLabel(hwnd, L"首次申请日期：");
            state->startDate = makeDate(hwnd, IDC_START_DATE);
            state->toLabel = makeLabel(hwnd, L"至", SS_CENTER);
            state->endDate = makeDate(hwnd, IDC_END_DATE);
            setDefaultDates(state->startDate, state->endDate);
            state->campusLabel = makeLabel(hwnd, L"院区：");
            state->campus = makeCombo(hwnd, IDC_CAMPUS);
            const wchar_t* campuses[] = {L"全部", L"老院", L"新院"};
            addComboItems(state->campus, campuses, static_cast<int>(std::size(campuses)));
            state->includePlateletCryo = CreateWindowExW(
                0, L"BUTTON", L"包括血小板和冷沉淀",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_INCLUDE_PLATELET_CRYO),
                GetModuleHandleW(nullptr), nullptr);
            state->query = search::create_button(hwnd, IDC_QUERY, L"查询", 0, 0, 0, 0);
            state->exportEvents = search::create_button(hwnd, IDC_EXPORT_EVENTS, L"导出事件", 0, 0, 0, 0);
            state->exportComponents = search::create_button(hwnd, IDC_EXPORT_COMPONENTS, L"导出成分明细", 0, 0, 0, 0);
            EnableWindow(state->exportEvents, FALSE);
            EnableWindow(state->exportComponents, FALSE);
            state->status = makeLabel(
                hwnd,
                L"请选择首次申请日期后查询。默认不包括血小板和冷沉淀，固定阈值 >=1600ml。",
                SS_LEFT);
            state->summary = makeList(hwnd, IDC_SUMMARY);
            initList(state->summary, SUMMARY_COLUMNS, static_cast<int>(std::size(SUMMARY_COLUMNS)));
            state->events = makeList(hwnd, IDC_EVENTS);
            initList(state->events, EVENT_COLUMNS, EVENT_COLUMN_COUNT);
            state->detailLabel = makeLabel(hwnd, L"成分明细：");
            state->detailMode = makeCombo(hwnd, IDC_DETAIL_MODE);
            const wchar_t* detailModes[] = {L"当前事件", L"全部事件成分", L"独立已驳回"};
            addComboItems(state->detailMode, detailModes, static_cast<int>(std::size(detailModes)));
            state->components = makeList(hwnd, IDC_COMPONENTS);
            initList(state->components, COMPONENT_COLUMNS, COMPONENT_COLUMN_COUNT);
            search::apply_font_to_children(hwnd, state->ctx.uiFont);
            populateSummary(state);
            resizeLayout(hwnd, state);
            return 0;
        }
        case WM_SIZE:
            resizeLayout(hwnd, state);
            return 0;
        case WM_COMMAND:
            if (!state) break;
            if (LOWORD(wp) == IDC_QUERY) { runQuery(hwnd, state); return 0; }
            if (LOWORD(wp) == IDC_EXPORT_EVENTS) { exportEventCsv(hwnd, state); return 0; }
            if (LOWORD(wp) == IDC_EXPORT_COMPONENTS) { exportComponentCsv(hwnd, state); return 0; }
            if (LOWORD(wp) == IDC_DETAIL_MODE && HIWORD(wp) == CBN_SELCHANGE) {
                refreshComponentScope(state);
                return 0;
            }
            break;
        case WM_NOTIFY: {
            if (!state) break;
            auto* header = reinterpret_cast<NMHDR*>(lp);
            if (header->idFrom == IDC_EVENTS && header->code == LVN_COLUMNCLICK) {
                const auto* info = reinterpret_cast<NMLISTVIEW*>(lp);
                sortEvents(state, info->iSubItem, true);
                populateEvents(state);
                refreshComponentScope(state);
                return 0;
            }
            if (header->idFrom == IDC_EVENTS && header->code == LVN_ITEMCHANGED && comboText(state->detailMode) == L"当前事件") {
                refreshComponentScope(state);
                return 0;
            }
            if (header->idFrom == IDC_EVENTS && header->code == NM_DBLCLK) {
                const auto* item = reinterpret_cast<NMITEMACTIVATE*>(lp);
                if (item->iItem >= 0 && item->iItem < static_cast<int>(state->eventRows.size())) {
                    const auto& row = state->eventRows[static_cast<size_t>(item->iItem)];
                    openBloodRequest(hwnd, state, row.first_apply_form_no, row.first_apply_time);
                }
                return 0;
            }
            if (header->idFrom == IDC_COMPONENTS && header->code == NM_DBLCLK) {
                const auto* item = reinterpret_cast<NMITEMACTIVATE*>(lp);
                if (item->iItem >= 0 && item->iItem < static_cast<int>(state->visibleComponents.size())) {
                    const auto& row = state->visibleComponents[static_cast<size_t>(item->iItem)];
                    openBloodRequest(hwnd, state, row.apply_form_no, row.apply_time);
                }
                return 0;
            }
            if (header->idFrom == IDC_EVENTS && header->code == NM_CUSTOMDRAW) {
                auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    const size_t index = static_cast<size_t>(draw->nmcd.dwItemSpec);
                    if (index < state->eventRows.size()) {
                        draw->clrTextBk = state->eventRows[index].qualifies ? RGB(0xFF, 0xE0, 0xB2) : RGB(0xFF, 0xF3, 0xCD);
                    }
                    return CDRF_NEWFONT;
                }
            }
            if (header->idFrom == IDC_COMPONENTS && header->code == NM_CUSTOMDRAW) {
                auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    const size_t index = static_cast<size_t>(draw->nmcd.dwItemSpec);
                    if (index < state->visibleComponents.size()) {
                        const auto& row = state->visibleComponents[index];
                        if (row.rejected) draw->clrTextBk = RGB(0xE0, 0xE0, 0xE0);
                        else if (!row.excluded_by_component_filter && row.data_status != "完整") {
                            draw->clrTextBk = RGB(0xFF, 0xF3, 0xCD);
                        }
                    }
                    return CDRF_NEWFONT;
                }
            }
            break;
        }
        case WM_QUERY_LOADED: {
            std::unique_ptr<QueryResult> result(reinterpret_cast<QueryResult*>(lp));
            if (!state) return 0;
            state->querying = false;
            setQueryEnabled(state, true);
            if (!result->ok) {
                EnableWindow(state->exportEvents, state->hasResult && !state->eventRows.empty());
                EnableWindow(state->exportComponents, state->hasResult);
                setStatus(state, L"查询失败：" + search::utf8_to_wide(result->error));
                MessageBoxW(hwnd, search::utf8_to_wide(result->error).c_str(), WINDOW_TITLE, MB_ICONERROR);
                return 0;
            }
            state->totals = result->summary;
            state->eventRows = std::move(result->events);
            state->orphanRejected = std::move(result->orphanRejected);
            state->loadedStart = result->startDate;
            state->loadedEnd = result->endDate;
            state->loadedCampus = search::utf8_to_wide(result->campus.empty() ? "全部" : result->campus);
            state->loadedIncludePlateletCryo = result->includePlateletCryo;
            state->hasResult = true;
            state->eventSortColumn = EVENT_FIRST_TIME;
            state->eventSortAscending = false;
            sortEvents(state, EVENT_FIRST_TIME, false);
            populateSummary(state);
            populateEvents(state);
            if (!state->eventRows.empty()) {
                ListView_SetItemState(state->events, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
            refreshComponentScope(state);
            EnableWindow(state->exportEvents, !state->eventRows.empty());
            EnableWindow(state->exportComponents,
                         !state->eventRows.empty() || !state->orphanRejected.empty());
            setStatus(state, L"查询完成：大量输血事件 " + std::to_wstring(state->totals.event_count) +
                             L" 个，异常事件 " + std::to_wstring(state->totals.issue_event_count) +
                             L" 个，已驳回申请 " + std::to_wstring(state->totals.rejected_application_count) +
                             L" 个。院区：" + state->loadedCampus +
                             (state->loadedIncludePlateletCryo
                                  ? L"。已包括血小板和冷沉淀，规则 "
                                  : L"。未包括血小板和冷沉淀，规则 ") +
                             RULE_VERSION_W + L"。" +
                             L"固定阈值 >=1600ml。");
            return 0;
        }
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
            SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
            return reinterpret_cast<LRESULT>(state ? state->bgBrush : nullptr);
        case WM_ERASEBKGND: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc,
                     state ? state->bgBrush : reinterpret_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH)));
            return 1;
        }
        case WM_DESTROY:
            if (state) {
                if (state->bgBrush) DeleteObject(state->bgBrush);
                RemovePropW(hwnd, PROP_STATE);
                delete state;
            }
            return 0;
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND create_massive_transfusion_statistics_module(const ModuleContext& ctx) {
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
    auto* state = new State();
    state->ctx = ctx;
    MDICREATESTRUCTW mcs{};
    mcs.szTitle = WINDOW_TITLE;
    mcs.szClass = WND_CLASS;
    mcs.hOwner = ctx.instance;
    mcs.x = mcs.y = mcs.cx = mcs.cy = CW_USEDEFAULT;
    mcs.lParam = reinterpret_cast<LPARAM>(state);
    HWND child = reinterpret_cast<HWND>(SendMessageW(
        ctx.mdiClient, WM_MDICREATE, 0, reinterpret_cast<LPARAM>(&mcs)));
    if (!child) {
        delete state;
        MessageBoxW(ctx.mdiClient, L"大量输血统计窗口创建失败。", WINDOW_TITLE, MB_ICONERROR);
        return nullptr;
    }
    SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    return child;
}

#endif
