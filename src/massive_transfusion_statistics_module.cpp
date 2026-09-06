#include "massive_transfusion_statistics_module.h"

#ifdef _WIN32

#include "blood_module.h"
#include "main_app.h"
#include "page_feedback.h"
#include "resource.h"
#include "search_core.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"

#include <commctrl.h>
#include <commdlg.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const wchar_t* WND_CLASS = L"MassiveTransfusionStatisticsModuleChild";
constexpr const wchar_t* WINDOW_TITLE = L"大量输血统计";
constexpr const wchar_t* PROP_STATE = L"MassiveTransfusionStatisticsSt";
constexpr const char* RULE_VERSION = "v3";
constexpr UINT WM_QUERY_LOADED = WM_APP + 0x576;

enum ControlId {
    IDC_START_DATE = 7201,
    IDC_END_DATE,
    IDC_CAMPUS,
    IDC_QUERY,
    IDC_EXPORT_EVENTS,
    IDC_EXPORT_COMPONENTS,
    IDC_EVENTS,
    IDC_COMPONENTS,
    IDC_STATUS,
    IDC_DETAIL_MODE,
    IDC_INCLUDE_PLATELET_CRYO,
    IDC_THRESHOLD_VALUE,
    IDC_THRESHOLD_OPERATOR,
    IDC_STATISTIC_BASIS,
    IDC_EVENT_TIME_SOURCE,
};

struct Column { const wchar_t* title; int width; };

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
    {L"患者类型", 85}, {L"事件起始时间", 145}, {L"窗口结束时间", 145},
    {L"最后计量时间", 145}, {L"折算总量(ml)", 115}, {L"关联申请单", 95},
    {L"计量项/血袋", 95}, {L"核查记录", 95}, {L"首袋关联申请单", 155},
    {L"全部申请单", 260}, {L"实际输血制品构成", 320}, {L"申请科室", 175},
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
    COMPONENT_BASIS,
    COMPONENT_TIME_SOURCE,
    COMPONENT_SELECTED_TIME,
    COMPONENT_VERIFY_STATE,
    COMPONENT_CROSS_MATCH_ID,
    COMPONENT_BLOOD_IN_ID,
    COMPONENT_BAG_NO,
    COMPONENT_PRODUCT_CODE,
    COMPONENT_MATCH_DATE,
    COMPONENT_OUT_DATE,
    COMPONENT_CHECK_DATE,
    COMPONENT_OUT_COUNT,
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
    {L"统计口径", 95}, {L"时间口径", 105}, {L"事件时间", 145},
    {L"实际输血状态", 105}, {L"交叉配血ID", 105}, {L"血袋ID", 105},
    {L"血袋号", 135}, {L"产品码", 120}, {L"配血时间", 145},
    {L"出库时间", 145}, {L"血库审核时间", 145}, {L"出库记录数", 95},
    {L"是否计量", 80}, {L"血液制品", 190}, {L"数量/规格", 90},
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
    HWND thresholdLabel = nullptr;
    HWND basisLabel = nullptr;
    HWND timeSourceLabel = nullptr;
    HWND thresholdUnitLabel = nullptr;
    HWND filterGroup = nullptr;
    HWND detailContext = nullptr;
    HWND startDate = nullptr;
    HWND endDate = nullptr;
    HWND campus = nullptr;
    HWND includePlateletCryo = nullptr;
    HWND thresholdValue = nullptr;
    HWND thresholdOperator = nullptr;
    HWND statisticBasis = nullptr;
    HWND eventTimeSource = nullptr;
    HWND detailMode = nullptr;
    HWND query = nullptr;
    HWND exportEvents = nullptr;
    HWND exportComponents = nullptr;
    std::array<HWND, 6> summaryCards{};
    HWND events = nullptr;
    HWND components = nullptr;
    HWND status = nullptr;
    search::PageFeedback feedback;
    HBRUSH bgBrush = nullptr;
    bool querying = false;
    bool hasResult = false;
    std::string loadedStart;
    std::string loadedEnd;
    std::wstring loadedCampus = L"全部";
    bool loadedIncludePlateletCryo = false;
    double loadedThresholdMl = 1600.0;
    bool loadedThresholdInclusive = true;
    std::string loadedStatisticBasis = "actual";
    std::string loadedEventTimeSource = "match";
    int eventSortColumn = EVENT_FIRST_TIME;
    bool eventSortAscending = false;
    Summary totals;
    std::vector<EventRow> eventRows;
    std::vector<ComponentRow> auditRows;
    std::vector<ComponentRow> visibleComponents;
};

struct QueryResult {
    bool ok = false;
    std::string startDate;
    std::string endDate;
    std::string campus;
    bool includePlateletCryo = false;
    double thresholdMl = 1600.0;
    bool thresholdInclusive = true;
    std::string statisticBasis = "actual";
    std::string eventTimeSource = "out";
    long long elapsedMs = 0;
    std::string error;
    Summary summary;
    std::vector<EventRow> events;
    std::vector<ComponentRow> auditRows;
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

HWND makeSummaryCard(HWND parent) {
    return CreateWindowExW(
        WS_EX_CLIENTEDGE, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND makeTab(HWND parent, int id) {
    return CreateWindowExW(
        0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

void addTab(HWND tab, int index, const wchar_t* title) {
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t*>(title);
    TabCtrl_InsertItem(tab, index, &item);
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

const wchar_t* eventCompositionColumnTitle(bool actual) {
    return actual ? L"实际输血制品构成" : L"申请制品构成";
}

void setEventCompositionColumnTitle(State* state, bool actual) {
    if (!state || !state->events) return;
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT;
    column.pszText = const_cast<wchar_t*>(eventCompositionColumnTitle(actual));
    ListView_SetColumn(state->events, EVENT_COMPOSITIONS, &column);
}

const wchar_t* eventColumnTitle(const State* state, int column) {
    if (column == EVENT_COMPOSITIONS) {
        return eventCompositionColumnTitle(
            state && state->loadedStatisticBasis == "actual");
    }
    return EVENT_COLUMNS[column].title;
}

bool isDefaultEventColumn(int column) {
    switch (column) {
        case EVENT_RESULT:
        case EVENT_CAMPUS:
        case EVENT_PATIENT_NO:
        case EVENT_PATIENT_NAME:
        case EVENT_FIRST_TIME:
        case EVENT_WINDOW_END:
        case EVENT_TOTAL_ML:
        case EVENT_COMPONENT_COUNT:
        case EVENT_COMPOSITIONS:
        case EVENT_DEPT:
        case EVENT_DATA_STATUS:
            return true;
        default:
            return false;
    }
}

bool isDefaultComponentColumn(int column, bool actual) {
    switch (column) {
        case COMPONENT_EVENT:
        case COMPONENT_PATIENT_NO:
        case COMPONENT_PATIENT_NAME:
        case COMPONENT_FORM:
        case COMPONENT_SELECTED_TIME:
        case COMPONENT_COUNTED:
        case COMPONENT_NAME:
        case COMPONENT_NUM:
        case COMPONENT_UNIT:
        case COMPONENT_ML:
        case COMPONENT_DEPT:
        case COMPONENT_DATA_STATUS:
            return true;
        case COMPONENT_BAG_NO:
            return actual;
        case COMPONENT_TIME:
        case COMPONENT_STATUS:
            return !actual;
        default:
            return false;
    }
}

void applyDefaultColumnLayout(State* state, bool actual) {
    if (!state) return;
    for (int column = 0; column < EVENT_COLUMN_COUNT; ++column) {
        ListView_SetColumnWidth(
            state->events, column,
            isDefaultEventColumn(column) ? EVENT_COLUMNS[column].width : 0);
    }
    for (int column = 0; column < COMPONENT_COLUMN_COUNT; ++column) {
        ListView_SetColumnWidth(
            state->components, column,
            isDefaultComponentColumn(column, actual) ? COMPONENT_COLUMNS[column].width : 0);
    }
}

void updateDetailTabTitles(State* state, bool actual) {
    if (!state || !state->detailMode) return;
    const wchar_t* titles[] = {
        actual ? L"当前事件血袋" : L"当前事件成分",
        actual ? L"全部血袋" : L"全部成分",
        L"异常核查",
    };
    for (int index = 0; index < static_cast<int>(std::size(titles)); ++index) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(titles[index]);
        TabCtrl_SetItem(state->detailMode, index, &item);
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

std::wstring windowText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<size_t>(length + 1), L'\0');
    if (length > 0) GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<size_t>(length));
    return value;
}

bool parseThreshold(HWND control, double& value) {
    const std::string input = search::trim(search::wide_to_utf8(windowText(control)));
    if (input.empty()) return false;
    bool saw_digit = false;
    bool saw_dot = false;
    int decimal_places = 0;
    for (char ch : input) {
        if (ch == '.' && !saw_dot) {
            saw_dot = true;
            continue;
        }
        if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
        saw_digit = true;
        if (saw_dot && ++decimal_places > 2) return false;
    }
    if (!saw_digit) return false;
    char* end = nullptr;
    value = std::strtod(input.c_str(), &end);
    return end && *end == '\0' && std::isfinite(value) && value > 0.0;
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

std::string thresholdCondition(double threshold_ml, bool inclusive) {
    return std::string(inclusive ? ">=" : ">") + numberText(threshold_ml) + "ml";
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
        case EVENT_REJECTED_COUNT: return std::to_string(
            row.statistic_basis == "actual" ? row.audit_count : row.rejected_application_count);
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
        case COMPONENT_EVENT: return row.event_id.empty() ? "异常核查" : row.event_id.substr(row.event_id.find('@') + 1);
        case COMPONENT_CAMPUS: return row.campus;
        case COMPONENT_PATIENT_NO: return row.patient_no;
        case COMPONENT_PATIENT_NAME: return row.patient_name;
        case COMPONENT_FORM: return row.apply_form_no;
        case COMPONENT_TIME: return row.apply_time;
        case COMPONENT_STATUS: return row.apply_status;
        case COMPONENT_BASIS: return row.statistic_basis == "actual" ? "实际输血量" : "申请量对照";
        case COMPONENT_TIME_SOURCE: return row.time_source;
        case COMPONENT_SELECTED_TIME: return row.selected_time;
        case COMPONENT_VERIFY_STATE: return row.verify_state;
        case COMPONENT_CROSS_MATCH_ID: return row.cross_match_id;
        case COMPONENT_BLOOD_IN_ID: return row.blood_in_id;
        case COMPONENT_BAG_NO: return row.blood_bag_no;
        case COMPONENT_PRODUCT_CODE: return row.product_code;
        case COMPONENT_MATCH_DATE: return row.match_date;
        case COMPONENT_OUT_DATE: return row.blood_out_date;
        case COMPONENT_CHECK_DATE: return row.check_date;
        case COMPONENT_OUT_COUNT: return row.blood_out_record_count > 0
            ? std::to_string(row.blood_out_record_count) : std::string{};
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
    if (state) search::set_page_status(state->feedback, text);
}

void populateSummary(State* state) {
    if (!state) return;
    const bool actual = state->loadedStatisticBasis == "actual";
    const std::wstring titles[] = {
        L"大量输血事件",
        L"涉及患者",
        actual ? L"实际输血总量" : L"申请折算总量",
        actual ? L"实际血袋" : L"计量制品项",
        L"异常事件",
        L"核查记录",
    };
    const std::wstring values[] = {
        std::to_wstring(state->totals.event_count),
        std::to_wstring(state->totals.patient_count),
        search::utf8_to_wide(numberText(state->totals.total_ml)) + L" ml",
        std::to_wstring(state->totals.component_count),
        std::to_wstring(state->totals.issue_event_count),
        std::to_wstring(state->totals.audit_record_count),
    };
    for (int index = 0; index < static_cast<int>(state->summaryCards.size()); ++index) {
        const std::wstring text = titles[index] + L"\r\n" + values[index];
        SetWindowTextW(state->summaryCards[static_cast<size_t>(index)], text.c_str());
    }
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

void refreshDetailContext(State* state) {
    if (!state || !state->detailContext) return;
    if (TabCtrl_GetCurSel(state->detailMode) == 2) {
        SetWindowTextW(
            state->detailContext,
            (L"异常核查：" + std::to_wstring(state->auditRows.size()) + L" 条").c_str());
        return;
    }
    const int selected = ListView_GetNextItem(state->events, -1, LVNI_SELECTED);
    if (selected < 0 || selected >= static_cast<int>(state->eventRows.size())) {
        SetWindowTextW(state->detailContext, L"请选择一个事件查看明细。 ");
        return;
    }
    const auto& event = state->eventRows[static_cast<size_t>(selected)];
    const std::wstring itemTitle = state->loadedStatisticBasis == "actual" ? L"血袋：" : L"计量项：";
    const std::wstring text =
        L"患者：" + search::utf8_to_wide(event.patient_name) + L"（" +
        search::utf8_to_wide(event.patient_no) + L"）  事件：" +
        search::utf8_to_wide(event.first_apply_time) + L" 至 " +
        search::utf8_to_wide(event.window_end_time) + L"  折算量：" +
        search::utf8_to_wide(event.total_ml) + L" ml  " + itemTitle +
        std::to_wstring(event.component_count);
    SetWindowTextW(state->detailContext, text.c_str());
}

void refreshComponentScope(State* state) {
    if (!state) return;
    state->visibleComponents.clear();
    const int mode = TabCtrl_GetCurSel(state->detailMode);
    if (mode == 2) {
        state->visibleComponents = state->auditRows;
    } else if (mode == 1) {
        for (const auto& event : state->eventRows) {
            state->visibleComponents.insert(state->visibleComponents.end(), event.components.begin(), event.components.end());
        }
    } else {
        const int selected = ListView_GetNextItem(state->events, -1, LVNI_SELECTED);
        if (selected >= 0 && selected < static_cast<int>(state->eventRows.size())) {
            state->visibleComponents = state->eventRows[static_cast<size_t>(selected)].components;
        }
    }
    refreshDetailContext(state);
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
    EnableWindow(state->thresholdValue, enabled);
    EnableWindow(state->thresholdOperator, enabled);
    EnableWindow(state->statisticBasis, enabled);
    const bool actual = SendMessageW(state->statisticBasis, CB_GETCURSEL, 0, 0) == 0;
    EnableWindow(state->eventTimeSource, enabled && actual);
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
    const int filterTop = S(hwnd, 2);
    const int firstRowY = filterTop + S(hwnd, 19);
    const int secondRowY = firstRowY + h + S(hwnd, 5);
    const int filterBottom = secondRowY + S(hwnd, 27) + S(hwnd, 6);
    const int filterLeft = S(hwnd, 5);
    const int contentLeft = filterLeft + S(hwnd, 16);
    const int labelGap = S(hwnd, 6);
    const int unitGap = S(hwnd, 4);
    const int controlGap = S(hwnd, 8);
    const int sectionGap = S(hwnd, 16);
    MoveWindow(state->filterGroup, filterLeft, filterTop,
               width - S(hwnd, 10), filterBottom - filterTop, TRUE);
    const int leadingLabelWidth = (std::max)(
        search::measure_control_text_width(hwnd, state->startLabel, 82),
        search::measure_control_text_width(hwnd, state->thresholdLabel, 82));
    int x = contentLeft;
    int labelWidth = leadingLabelWidth;
    MoveWindow(state->startLabel, x, firstRowY + S(hwnd, 2), labelWidth, h, TRUE); x += labelWidth + labelGap;
    MoveWindow(state->startDate, x, firstRowY, S(hwnd, 118), h, TRUE); x += S(hwnd, 118) + controlGap;
    MoveWindow(state->toLabel, x, firstRowY + S(hwnd, 2), S(hwnd, 20), h, TRUE); x += S(hwnd, 20) + controlGap;
    MoveWindow(state->endDate, x, firstRowY, S(hwnd, 118), h, TRUE); x += S(hwnd, 118) + sectionGap;
    labelWidth = search::measure_control_text_width(hwnd, state->campusLabel, 50);
    MoveWindow(state->campusLabel, x, firstRowY + S(hwnd, 2), labelWidth, h, TRUE); x += labelWidth + labelGap;
    MoveWindow(state->campus, x, firstRowY, S(hwnd, 82), S(hwnd, 180), TRUE); x += S(hwnd, 82) + sectionGap;
    labelWidth = search::measure_control_text_width(hwnd, state->basisLabel, 70);
    MoveWindow(state->basisLabel, x, firstRowY + S(hwnd, 2), labelWidth, h, TRUE); x += labelWidth + labelGap;
    MoveWindow(state->statisticBasis, x, firstRowY, S(hwnd, 112), S(hwnd, 120), TRUE); x += S(hwnd, 112) + sectionGap;
    labelWidth = search::measure_control_text_width(hwnd, state->timeSourceLabel, 75);
    MoveWindow(state->timeSourceLabel, x, firstRowY + S(hwnd, 2), labelWidth, h, TRUE); x += labelWidth + labelGap;
    MoveWindow(state->eventTimeSource, x, firstRowY, S(hwnd, 116), S(hwnd, 150), TRUE);

    x = contentLeft;
    labelWidth = leadingLabelWidth;
    MoveWindow(state->thresholdLabel, x, secondRowY + S(hwnd, 2), labelWidth, h, TRUE); x += labelWidth + labelGap;
    MoveWindow(state->thresholdOperator, x, secondRowY, S(hwnd, 92), S(hwnd, 120), TRUE); x += S(hwnd, 92) + controlGap;
    MoveWindow(state->thresholdValue, x, secondRowY, S(hwnd, 88), h, TRUE); x += S(hwnd, 88) + unitGap;
    MoveWindow(state->thresholdUnitLabel, x, secondRowY + S(hwnd, 2), S(hwnd, 28), h, TRUE); x += S(hwnd, 28) + sectionGap;
    MoveWindow(state->includePlateletCryo, x, secondRowY, S(hwnd, 174), h, TRUE); x += S(hwnd, 174) + sectionGap;
    MoveWindow(state->query, x, secondRowY - S(hwnd, 1), S(hwnd, 62), S(hwnd, 27), TRUE); x += S(hwnd, 62) + controlGap;
    MoveWindow(state->exportEvents, x, secondRowY - S(hwnd, 1), S(hwnd, 88), S(hwnd, 27), TRUE); x += S(hwnd, 88) + controlGap;
    MoveWindow(state->exportComponents, x, secondRowY - S(hwnd, 1), S(hwnd, 86), S(hwnd, 27), TRUE);

    const int statusTop = filterBottom + S(hwnd, 4);
    MoveWindow(state->status, pad, statusTop, width - pad * 2, S(hwnd, 22), TRUE);
    const int summaryTop = statusTop + S(hwnd, 25);
    const int summaryHeight = S(hwnd, 56);
    const int cardGap = S(hwnd, 7);
    const int cardWidth = (width - pad * 2 - cardGap * 5) / 6;
    for (int index = 0; index < static_cast<int>(state->summaryCards.size()); ++index) {
        MoveWindow(state->summaryCards[static_cast<size_t>(index)],
                   pad + index * (cardWidth + cardGap), summaryTop,
                   cardWidth, summaryHeight, TRUE);
    }

    const int detailControlsTop = height * 62 / 100;
    const int eventsTop = summaryTop + summaryHeight + pad;
    MoveWindow(state->events, pad, eventsTop, width - pad * 2,
               (std::max)(S(hwnd, 130), detailControlsTop - eventsTop - S(hwnd, 34)), TRUE);
    MoveWindow(state->detailMode, pad, detailControlsTop - S(hwnd, 1), S(hwnd, 370), S(hwnd, 28), TRUE);
    MoveWindow(state->detailContext, pad + S(hwnd, 382), detailControlsTop + S(hwnd, 2),
               (std::max)(S(hwnd, 200), width - pad * 2 - S(hwnd, 382)), h, TRUE);
    MoveWindow(state->components, pad, detailControlsTop + S(hwnd, 29), width - pad * 2,
               (std::max)(S(hwnd, 90), height - detailControlsTop - S(hwnd, 39)), TRUE);
    search::layout_page_feedback(state->feedback);
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
    query.statistic_basis = SendMessageW(state->statisticBasis, CB_GETCURSEL, 0, 0) == 0
        ? "actual" : "application";
    const int time_index = static_cast<int>(SendMessageW(state->eventTimeSource, CB_GETCURSEL, 0, 0));
    query.event_time_source = time_index == 1 ? "out" : time_index == 2 ? "apply" :
                              time_index == 3 ? "check" : "match";
    if (query.statistic_basis == "application") query.event_time_source = "apply";
    query.include_platelet_and_cryoprecipitate =
        SendMessageW(state->includePlateletCryo, BM_GETCHECK, 0, 0) == BST_CHECKED;
    query.threshold_inclusive = SendMessageW(state->thresholdOperator, CB_GETCURSEL, 0, 0) != 1;
    if (query.start_date.empty() || query.end_date.empty() || query.start_date > query.end_date) {
        MessageBoxW(hwnd, L"事件开始日期不能晚于结束日期。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }
    if (!parseThreshold(state->thresholdValue, query.threshold_ml)) {
        MessageBoxW(hwnd, L"统计阈值必须是大于 0 且最多保留两位小数的数值。",
                    WINDOW_TITLE, MB_ICONWARNING);
        SetFocus(state->thresholdValue);
        return;
    }
    state->querying = true;
    setQueryEnabled(state, false);
    EnableWindow(state->exportEvents, FALSE);
    EnableWindow(state->exportComponents, FALSE);
    setStatus(state, L"正在查询并计算24小时事件...");
    search::show_page_activity(state->feedback, L"正在查询并计算 24 小时事件，请稍候…");
    std::thread([hwnd, query]() {
        auto* result = new QueryResult();
        const auto started = std::chrono::steady_clock::now();
        result->startDate = query.start_date;
        result->endDate = query.end_date;
        result->campus = query.campus;
        result->includePlateletCryo = query.include_platelet_and_cryoprecipitate;
        result->thresholdMl = query.threshold_ml;
        result->thresholdInclusive = query.threshold_inclusive;
        result->statisticBasis = query.statistic_basis;
        result->eventTimeSource = query.event_time_source;
        result->ok = search::query_massive_transfusion_statistics(
            query, result->summary, result->events, result->auditRows, result->error);
        result->elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
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
    const std::string basis = state->loadedStatisticBasis == "actual" ? "实际输血量" : "申请量对照";
    const std::string time = state->loadedEventTimeSource == "out" ? "出库时间" :
        state->loadedEventTimeSource == "apply" ? "申请时间" :
        state->loadedEventTimeSource == "check" ? "血库审核时间" : "配血时间";
    return "," + basis + "," + time + "," + thresholdCondition(state->loadedThresholdMl,
                                     state->loadedThresholdInclusive) + "," +
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
        csv += csvEscape(search::wide_to_utf8(eventColumnTitle(state, col)));
    }
    csv += ",统计口径,事件时间口径,统计条件,包括血小板和冷沉淀,折算规则版本\n";
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
    for (const auto& audit : state->auditRows) {
        if (audit.event_id.empty()) rows.push_back(audit);
    }
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
    csv += ",统计口径,事件时间口径,统计条件,包括血小板和冷沉淀,折算规则版本\n";
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
            state->filterGroup = CreateWindowExW(
                0, L"BUTTON", L"查询条件", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            state->startLabel = makeLabel(hwnd, L"事件日期：", SS_LEFT);
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
            state->basisLabel = makeLabel(hwnd, L"统计口径：");
            state->statisticBasis = makeCombo(hwnd, IDC_STATISTIC_BASIS);
            const wchar_t* statisticBases[] = {L"实际输血量", L"申请量对照"};
            addComboItems(state->statisticBasis, statisticBases,
                          static_cast<int>(std::size(statisticBases)));
            state->thresholdLabel = makeLabel(hwnd, L"统计阈值：", SS_LEFT);
            state->thresholdOperator = makeCombo(hwnd, IDC_THRESHOLD_OPERATOR);
            const wchar_t* thresholdOperators[] = {L"大于等于", L"大于"};
            addComboItems(state->thresholdOperator, thresholdOperators,
                          static_cast<int>(std::size(thresholdOperators)));
            state->thresholdValue = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", L"1600",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_RIGHT,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_THRESHOLD_VALUE),
                GetModuleHandleW(nullptr), nullptr);
            SendMessageW(state->thresholdValue, EM_SETLIMITTEXT, 18, 0);
            state->thresholdUnitLabel = makeLabel(hwnd, L"ml", SS_LEFT);
            state->timeSourceLabel = makeLabel(hwnd, L"事件时间：");
            state->eventTimeSource = makeCombo(hwnd, IDC_EVENT_TIME_SOURCE);
            const wchar_t* timeSources[] = {L"配血时间", L"出库时间", L"申请时间", L"血库审核时间"};
            addComboItems(state->eventTimeSource, timeSources, static_cast<int>(std::size(timeSources)));
            SendMessageW(state->eventTimeSource, CB_SETCURSEL, 1, 0);
            EnableWindow(state->eventTimeSource, TRUE);
            state->query = search::create_button(hwnd, IDC_QUERY, L"查询", 0, 0, 0, 0);
            SendMessageW(state->query, BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);
            state->exportEvents = search::create_button(hwnd, IDC_EXPORT_EVENTS, L"导出事件", 0, 0, 0, 0);
            state->exportComponents = search::create_button(hwnd, IDC_EXPORT_COMPONENTS, L"导出明细", 0, 0, 0, 0);
            EnableWindow(state->exportEvents, FALSE);
            EnableWindow(state->exportComponents, FALSE);
            state->status = makeLabel(
                hwnd,
                L"请选择日期和统计口径后查询。",
                SS_LEFT | SS_ENDELLIPSIS);
            for (auto& card : state->summaryCards) card = makeSummaryCard(hwnd);
            state->events = makeList(hwnd, IDC_EVENTS);
            initList(state->events, EVENT_COLUMNS, EVENT_COLUMN_COUNT);
            state->detailMode = makeTab(hwnd, IDC_DETAIL_MODE);
            addTab(state->detailMode, 0, L"当前事件血袋");
            addTab(state->detailMode, 1, L"全部血袋");
            addTab(state->detailMode, 2, L"异常核查");
            TabCtrl_SetCurSel(state->detailMode, 0);
            state->detailContext = makeLabel(hwnd, L"请选择一个事件查看明细。", SS_LEFT);
            state->components = makeList(hwnd, IDC_COMPONENTS);
            initList(state->components, COMPONENT_COLUMNS, COMPONENT_COLUMN_COUNT);
            applyDefaultColumnLayout(state, true);
            search::initialize_page_feedback(state->feedback, hwnd, state->status,
                                             state->events, state->ctx.uiFont);
            search::add_page_tooltip(state->feedback, state->query,
                                     L"按当前日期、院区、统计口径和阈值计算大量输血事件。");
            search::add_page_tooltip(state->feedback, state->exportEvents,
                                     L"导出当前查询得到的全部事件记录。");
            search::add_page_tooltip(state->feedback, state->exportComponents,
                                     L"导出当前查询得到的全部申请成分明细。");
            search::add_page_tooltip(state->feedback, state->events,
                                     L"单击列标题排序；双击事件可跳转输血结果查询。");
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
            if (search::handle_page_feedback_command(state->feedback, lp, WINDOW_TITLE)) return 0;
            if (LOWORD(wp) == IDC_QUERY) { runQuery(hwnd, state); return 0; }
            if (LOWORD(wp) == IDC_EXPORT_EVENTS) { exportEventCsv(hwnd, state); return 0; }
            if (LOWORD(wp) == IDC_EXPORT_COMPONENTS) { exportComponentCsv(hwnd, state); return 0; }
            if (LOWORD(wp) == IDC_STATISTIC_BASIS && HIWORD(wp) == CBN_SELCHANGE) {
                const bool actual = SendMessageW(state->statisticBasis, CB_GETCURSEL, 0, 0) == 0;
                EnableWindow(state->eventTimeSource, actual);
                SendMessageW(state->eventTimeSource, CB_SETCURSEL, actual ? 1 : 2, 0);
                return 0;
            }
            break;
        case WM_NOTIFY: {
            if (!state) break;
            auto* header = reinterpret_cast<NMHDR*>(lp);
            if (header->idFrom == IDC_DETAIL_MODE && header->code == TCN_SELCHANGE) {
                refreshComponentScope(state);
                return 0;
            }
            if (header->idFrom == IDC_EVENTS && header->code == LVN_COLUMNCLICK) {
                const auto* info = reinterpret_cast<NMLISTVIEW*>(lp);
                sortEvents(state, info->iSubItem, true);
                populateEvents(state);
                if (!state->eventRows.empty()) {
                    ListView_SetItemState(
                        state->events, 0, LVIS_SELECTED | LVIS_FOCUSED,
                        LVIS_SELECTED | LVIS_FOCUSED);
                }
                refreshComponentScope(state);
                return 0;
            }
            if (header->idFrom == IDC_EVENTS && header->code == LVN_ITEMCHANGED &&
                TabCtrl_GetCurSel(state->detailMode) == 0) {
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
            search::hide_page_activity(state->feedback);
            if (!result->ok) {
                EnableWindow(state->exportEvents, state->hasResult && !state->eventRows.empty());
                EnableWindow(state->exportComponents, state->hasResult);
                search::show_page_alert(state->feedback,
                    L"查询失败（耗时 " + std::to_wstring(result->elapsedMs) +
                    L" ms）：" + search::utf8_to_wide(result->error));
                return 0;
            }
            state->totals = result->summary;
            state->eventRows = std::move(result->events);
            state->auditRows = std::move(result->auditRows);
            state->loadedStart = result->startDate;
            state->loadedEnd = result->endDate;
            state->loadedCampus = search::utf8_to_wide(result->campus.empty() ? "全部" : result->campus);
            state->loadedIncludePlateletCryo = result->includePlateletCryo;
            state->loadedThresholdMl = result->thresholdMl;
            state->loadedThresholdInclusive = result->thresholdInclusive;
            state->loadedStatisticBasis = result->statisticBasis;
            state->loadedEventTimeSource = result->eventTimeSource;
            const bool actual = state->loadedStatisticBasis == "actual";
            setEventCompositionColumnTitle(state, actual);
            updateDetailTabTitles(state, actual);
            applyDefaultColumnLayout(state, actual);
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
                         !state->eventRows.empty() || !state->auditRows.empty());
            const std::wstring basisText = state->loadedStatisticBasis == "actual"
                ? L"实际输血量" : L"申请量对照";
            const std::wstring timeText = state->loadedEventTimeSource == "out" ? L"出库时间" :
                state->loadedEventTimeSource == "apply" ? L"申请时间" :
                state->loadedEventTimeSource == "check" ? L"血库审核时间" : L"配血时间";
            setStatus(state, L"查询完成 · " + basisText + L" / " + timeText + L" / " +
                             state->loadedCampus + L" · 事件 " +
                             std::to_wstring(state->totals.event_count) + L"，异常 " +
                             std::to_wstring(state->totals.issue_event_count) + L"，核查 " +
                             std::to_wstring(state->totals.audit_record_count) + L" · " +
                             std::to_wstring(state->totals.raw_record_count) + L" 条，" +
                             std::to_wstring(result->elapsedMs) + L" ms");
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
            if (state) {
                LRESULT result = 0;
                if (search::page_feedback_static_color(
                        state->feedback, reinterpret_cast<HDC>(wp),
                        reinterpret_cast<HWND>(lp), result)) return result;
            }
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
