#include "blood_module.h"

#ifdef _WIN32

#include "app_settings_io.h"
#include "main_app.h"
#include "resource.h"
#include "search_app.h"
#include "search_core.h"
#include "log.h"
#include "search_text.h"
#include "search_ui_columns.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"

#include <algorithm>
#include <commctrl.h>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

namespace {

constexpr int IDC_PATIENT_NO   = 6001;
constexpr int IDC_PATIENT_NAME = 6002;
constexpr int IDC_FORM_NO      = 6003;
constexpr int IDC_STATUS_COMBO = 6004;
constexpr int IDC_START_DATE   = 6005;
constexpr int IDC_END_DATE     = 6006;
constexpr int IDC_SEARCH       = 6007;
constexpr int IDC_LIST         = 6008;
constexpr int IDC_TABS         = 6009;
constexpr int IDC_HISTORY_LIST = 6010;
constexpr int BLOOD_TAB_HISTORY = 0;

constexpr int IDC_TOOL_AUDIT       = 6101;
constexpr int IDC_TOOL_CANCEL      = 6102;
constexpr int IDC_TOOL_REJECT      = 6103;
constexpr int IDC_TOOL_RESTORE     = 6104;
constexpr int IDC_TOOL_ARCHIVE     = 6105;
constexpr int IDC_TOOL_UNARCHIVE   = 6106;
constexpr int IDC_TOOL_DELETE      = 6107;
constexpr int IDC_TOOL_CROSS_MATCH = 6108;
constexpr int IDC_TOOL_PRINT       = 6109;
constexpr int IDC_TOOL_REFRESH     = 6110;
constexpr int IDC_TOOL_RESULT      = 6111;
constexpr int IDC_TOOL_CLOSE       = 6112;

constexpr const wchar_t* WND_CLASS  = L"BloodModuleChild";
constexpr const wchar_t* LIS_WND_CLASS = L"BloodLisResultWindow";
constexpr const wchar_t* PROP_STATE = L"BloodSt";
constexpr const wchar_t* PROP_LIS_STATE = L"BloodLisSt";
constexpr const wchar_t* WINDOW_TITLE = L"输血结果查询";
constexpr UINT WM_BLOOD_AUTO_QUERY = WM_APP + 61;
constexpr UINT WM_LIS_QUERY_DONE = WM_APP + 62;
constexpr UINT WM_LIS_RESULTS_DONE = WM_APP + 63;
constexpr UINT WM_LIS_SUMMARY_DONE = WM_APP + 64;
constexpr UINT_PTR SEARCH_EDIT_SUBCLASS = 6201;
constexpr int DEFAULT_DATE_RANGE_DAYS = 7;
constexpr int DEFAULT_LIS_DAYS = 14;
constexpr int MIN_LIS_DAYS = 7;
constexpr int MAX_LIS_DAYS = 365;

constexpr int IDC_LIS_DAYS = 6301;
constexpr int IDC_LIS_QUERY = 6302;
constexpr int IDC_LIS_REPORTS = 6303;
constexpr int IDC_LIS_RESULTS = 6304;
constexpr int IDC_LIS_QUERY_NAME = 6305;
constexpr int IDC_LIS_QUERY_ID = 6306;

enum LisReportColumn {
    LisReportSampleNo = 0,
    LisReportInspectTime = 1,
    LisReportGroupName = 2,
    LisReportBarcode = 3,
    LisReportRequester = 4,
    LisReportReviewer = 5,
    LisReportRoomCode = 6,
    LisReportMachineCode = 7,
};

enum class LisQueryMode {
    PatientNo,
    Name,
    SocialNo,
};

constexpr COLORREF COLOR_PAGE_BG = RGB(0xE8, 0xF8, 0xFF);
constexpr COLORREF COLOR_SEARCH_BUTTON = RGB(0xB2, 0xDC, 0xFC);
constexpr COLORREF COLOR_LEGEND_TEXT = RGB(0xEB, 0x55, 0x28);
constexpr COLORREF COLOR_EMERGENCY = RGB(0xEA, 0x33, 0x23);
constexpr COLORREF COLOR_REVIEWED = RGB(0xA6, 0xEB, 0x9A);
constexpr COLORREF COLOR_COMPLETED = RGB(0xFF, 0xFF, 0x54);
constexpr COLORREF COLOR_WHITE = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF COLOR_BLACK = RGB(0x00, 0x00, 0x00);
constexpr COLORREF COLOR_TRUSTED = RGB(0x16, 0x7A, 0x3A);
constexpr COLORREF COLOR_WARNING_TEXT = RGB(0xB4, 0x56, 0x00);

constexpr const char* STATUS_PENDING_TEXT = "未审核";
constexpr const char* STATUS_REVIEWED_TEXT = "已审核";
constexpr const char* STATUS_COMPLETED_TEXT = "已完结";
constexpr const char* EMERGENCY_TRAN_PROPERTY = "紧急(电话联系输血科)";

constexpr const wchar_t* STATUS_ALL_LABEL = L"全部";
constexpr const wchar_t* STATUS_PENDING_LABEL = L"未审核";
constexpr const wchar_t* STATUS_REVIEWED_LABEL = L"已审核";
constexpr const wchar_t* STATUS_COMPLETED_LABEL = L"已完结";

enum class LayoutArea {
    Toolbar,
    LeftSearch,
    RightSummary,
    DetailMain,
    DetailSide,
};

struct LayoutItem {
    HWND hwnd = nullptr;
    LayoutArea area = LayoutArea::Toolbar;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

struct DetailFields {
    HWND patientName = nullptr;
    HWND patientNo = nullptr;
    HWND patientType = nullptr;
    HWND sex = nullptr;
    HWND age = nullptr;
    HWND applyDept = nullptr;
    HWND patientDept = nullptr;
    HWND bedNo = nullptr;
    HWND urgency = nullptr;
    HWND applyTime = nullptr;
    HWND diagnosis = nullptr;
    HWND bloodProduct = nullptr;
    HWND transfusionHistory = nullptr;
    HWND reactionHistory = nullptr;
    HWND applyType = nullptr;
    HWND applyAbo = nullptr;
    HWND applyRh = nullptr;
};

struct BloodState {
    ModuleContext ctx;
    HWND patientNo = nullptr;
    HWND patientName = nullptr;
    HWND formNo = nullptr;
    HWND statusCombo = nullptr;
    HWND startDate = nullptr;
    HWND endDate = nullptr;
    HWND list = nullptr;
    HWND tabs = nullptr;
    HWND status = nullptr;
    HWND searchButton = nullptr;
    HWND legend = nullptr;
    HWND toolbarLine = nullptr;
    HWND searchBox = nullptr;
    HWND summaryBox = nullptr;
    HWND patientDetailBox = nullptr;
    HWND requestDetailBox = nullptr;
    HWND labBox = nullptr;
    HWND historyBox = nullptr;
    HWND historyList = nullptr;
    DetailFields detail;
    std::vector<LayoutItem> layout;
    std::vector<search::BloodRequestRow> rows;
    std::vector<search::BloodCrossMatchRow> historyRows;
    int selectedCellRow = -1;
    int selectedCellCol = -1;
    int activeTab = BLOOD_TAB_HISTORY;
    bool emergencyApplyType = false;
    HBRUSH bgBrush = nullptr;
    HBRUSH searchBrush = nullptr;
};

struct LisState {
    ModuleContext ctx;
    HWND patientNo = nullptr;
    HWND patientName = nullptr;
    HWND patientAge = nullptr;
    HWND patientSex = nullptr;
    HWND days = nullptr;
    HWND queryButton = nullptr;
    HWND queryNameButton = nullptr;
    HWND queryIdButton = nullptr;
    HWND labelPatientNo = nullptr;
    HWND labelPatientName = nullptr;
    HWND labelPatientAge = nullptr;
    HWND labelPatientSex = nullptr;
    HWND labelPatientSection = nullptr;
    HWND labelSummarySection = nullptr;
    HWND labelDays = nullptr;
    HWND labelDaysHint = nullptr;
    HWND labelDaysMin = nullptr;
    HWND labelDaysMax = nullptr;
    HWND labelReports = nullptr;
    HWND labelResults = nullptr;
    HWND identityHint = nullptr;
    HWND reports = nullptr;
    HWND results = nullptr;
    HWND status = nullptr;
    HBRUSH bgBrush = nullptr;
    COLORREF identityHintColor = COLOR_TRUSTED;
    HFONT summaryFont = nullptr;
    HFONT summaryBoldFont = nullptr;
    RECT summaryRect{};
    std::wstring summaryBloodValue;
    std::wstring summaryBloodDate;
    std::wstring summaryCbcValue;
    std::wstring summaryCbcDate;
    std::wstring summaryIrregularValue;
    std::wstring summaryIrregularDate;
    std::wstring summaryDirectAntiglobulinValue;
    std::wstring summaryDirectAntiglobulinDate;
    std::string patient_no;
    std::string patient_name;
    std::string patient_age;
    std::string patient_sex;
    std::vector<search::ReportRow> report_rows;
    std::vector<search::ResultRow> result_rows;
    std::vector<search::ResultRowTone> result_tones;  // precomputed for custom-draw
    int reportSortCol = -1;
    bool reportSortAscending = true;
    bool suppressReportSelection = false;
    int queryGeneration = 0;
    int resultGeneration = 0;
    // Cached LIS summary settings loaded once for this popup.
    std::string lis_abo_codes;
    std::string lis_rhd_codes;
    std::string lis_hgb_codes;
    std::string lis_plt_codes;
    std::string lis_irregular_antibody_codes;
    std::string lis_direct_antiglobulin_codes;
    std::string lis_blood_type_machines;
    std::string lis_cbc_machines;
    std::string lis_blood_exclude_machines;
    std::set<std::string> lis_blood_type_machine_pairs;
    std::set<std::string> lis_cbc_machine_pairs;
};


void layoutLisWindow(HWND hwnd, LisState* st);

struct LisQueryResult {
    int generation = 0;
    bool ok = false;
    LisQueryMode mode = LisQueryMode::PatientNo;
    bool phoneFiltered = false;
    bool phoneLookupAttempted = false;
    size_t socialNoPatientCount = 0;
    std::vector<search::ReportRow> reports;
    std::string error;
};

struct LisSummaryResult {
    int generation = 0;
    bool ok = false;
    search::LisSummary summary;
    std::string error;
};

struct LisResultsResult {
    int generation = 0;
    int reportIndex = -1;
    bool ok = false;
    std::vector<search::ResultRow> rows;
    std::string error;
};

RECT workAreaForWindow(HWND hwnd) {
    RECT fallback{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &fallback, 0);
    RECT probe = fallback;
    if (hwnd) {
        GetWindowRect(hwnd, &probe);
    }
    HMONITOR monitor = MonitorFromRect(&probe, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (monitor && GetMonitorInfoW(monitor, &mi)) {
        return mi.rcWork;
    }
    return fallback;
}

RECT centeredPopupRect(HWND ownerRoot, int desiredW, int desiredH, int minW, int minH) {
    const RECT work = workAreaForWindow(ownerRoot);
    const int workW = std::max(1, static_cast<int>(work.right - work.left));
    const int workH = std::max(1, static_cast<int>(work.bottom - work.top));
    const int maxW = std::max(minW, workW * 9 / 10);
    const int maxH = std::max(minH, workH * 9 / 10);
    const int popupW = std::min(desiredW, maxW);
    const int popupH = std::min(desiredH, maxH);

    RECT ownerRect{};
    if (!ownerRoot || !GetWindowRect(ownerRoot, &ownerRect)) {
        ownerRect = work;
    }
    int x = ownerRect.left + (ownerRect.right - ownerRect.left - popupW) / 2;
    int y = ownerRect.top + (ownerRect.bottom - ownerRect.top - popupH) / 2;
    x = std::max(static_cast<int>(work.left), std::min(x, static_cast<int>(work.right) - popupW));
    y = std::max(static_cast<int>(work.top), std::min(y, static_cast<int>(work.bottom) - popupH));
    return RECT{x, y, x + popupW, y + popupH};
}

bool isLeapYear(WORD year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

WORD daysInMonth(WORD year, WORD month) {
    static constexpr WORD kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && isLeapYear(year)) {
        return 29;
    }
    return kDays[month - 1];
}

SYSTEMTIME subtractDays(SYSTEMTIME date, int days) {
    while (days-- > 0) {
        if (date.wDay > 1) {
            --date.wDay;
            continue;
        }
        if (date.wMonth > 1) {
            --date.wMonth;
        } else {
            --date.wYear;
            date.wMonth = 12;
        }
        date.wDay = daysInMonth(date.wYear, date.wMonth);
    }
    return date;
}

void setDefaultDateRange(BloodState* st) {
    SYSTEMTIME today{};
    GetLocalTime(&today);
    today.wHour = today.wMinute = today.wSecond = today.wMilliseconds = 0;
    SYSTEMTIME start = subtractDays(today, DEFAULT_DATE_RANGE_DAYS);
    DateTime_SetSystemtime(st->startDate, GDT_VALID, &start);
    DateTime_SetSystemtime(st->endDate, GDT_VALID, &today);
}

void addLayout(BloodState* st, HWND hwnd, LayoutArea area, int x, int y, int w, int h) {
    if (hwnd) {
        st->layout.push_back({hwnd, area, x, y, w, h});
    }
}

HWND createStatic(HWND parent, const wchar_t* text, DWORD style, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | style,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND createValue(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
                           WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND createMultilineValue(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
                           WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND createToolButton(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                           x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

HWND createOwnerDrawButton(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                           x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

HFONT createScaledFont(HFONT base, double scale, LONG weight) {
    LOGFONTW lf{};
    HFONT source = base ? base : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    if (GetObjectW(source, sizeof(lf), &lf) != sizeof(lf)) {
        return nullptr;
    }
    lf.lfHeight = lf.lfHeight == 0 ? -14 : static_cast<LONG>(lf.lfHeight * scale);
    lf.lfWeight = weight;
    return CreateFontIndirectW(&lf);
}

void applyLisSummaryFonts(HWND hwnd, LisState* st) {
    if (!st) return;
    if (st->summaryFont) {
        DeleteObject(st->summaryFont);
        st->summaryFont = nullptr;
    }
    if (st->summaryBoldFont) {
        DeleteObject(st->summaryBoldFont);
        st->summaryBoldFont = nullptr;
    }
    st->summaryFont = createScaledFont(st->ctx.uiFont, 1.0, FW_NORMAL);
    st->summaryBoldFont = createScaledFont(st->ctx.uiFont, 1.0, FW_BOLD);
    layoutLisWindow(hwnd, st);
    InvalidateRect(hwnd, &st->summaryRect, TRUE);
}

void invalidateLisSummary(HWND hwnd, LisState* st) {
    if (!hwnd || !st) return;
    InvalidateRect(hwnd, &st->summaryRect, TRUE);
}

std::wstring normalizeCellText(const std::string& text) {
    std::wstring ws = search::utf8_to_wide(search::trim(text));
    auto visible = [](wchar_t ch) {
        return !std::iswspace(ch) && ch != L'\u3000';
    };
    ws.erase(ws.begin(), std::find_if(ws.begin(), ws.end(), visible));
    ws.erase(std::find_if(ws.rbegin(), ws.rend(), visible).base(), ws.end());
    return ws;
}

void insertEmptyRow(HWND list, int row) {
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = row;
    item.iSubItem = 0;
    item.pszText = const_cast<wchar_t*>(L"");
    ListView_InsertItem(list, &item);
}

void setCell(HWND list, int row, int col, const std::string& text) {
    std::wstring ws = normalizeCellText(text);
    ListView_SetItemText(list, row, col, const_cast<wchar_t*>(ws.c_str()));
}

// Lighter version for columns known to not need whitespace normalization (codes, dates, etc.)
void setCellUtf8(HWND list, int row, int col, const std::string& text) {
    const auto wide = search::utf8_to_wide(search::trim(text));
    ListView_SetItemText(list, row, col, const_cast<wchar_t*>(wide.c_str()));
}

std::string slashDateTimeMinute(const std::string& value) {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    if ((std::sscanf(value.c_str(), "%d-%d-%d %d:%d", &year, &month, &day, &hour, &minute) == 5 ||
         std::sscanf(value.c_str(), "%d/%d/%d %d:%d", &year, &month, &day, &hour, &minute) == 5) &&
        year > 0 && month > 0 && day > 0) {
        char buffer[24]{};
        std::snprintf(buffer, sizeof(buffer), "%d/%d/%d %d:%02d", year, month, day, hour, minute);
        return buffer;
    }
    return value;
}

void populateBloodHistory(BloodState* st, const std::string& patientNo) {
    if (!st || !st->historyList) return;
    st->historyRows.clear();
    ListView_DeleteAllItems(st->historyList);
    const std::string conn = search::wide_to_utf8(search::build_connection_string_w(st->ctx.dbSettings));
    if (conn.empty() || search::trim(patientNo).empty()) return;

    std::string error;
    if (!search::query_blood_crossmatch_history(conn, patientNo, st->historyRows, error)) {
        SetWindowTextW(st->status, L"输血历史查询失败。");
        return;
    }

    SendMessageW(st->historyList, WM_SETREDRAW, FALSE, 0);
    for (size_t i = 0; i < st->historyRows.size(); ++i) {
        const auto& r = st->historyRows[i];
        const int row = static_cast<int>(i);
        insertEmptyRow(st->historyList, row);
        setCellUtf8(st->historyList, row, 0, slashDateTimeMinute(r.blood_out_date));
        setCell(st->historyList, row, 1, r.blood_out_man);
        setCell(st->historyList, row, 2, r.blood_bag_no);
        setCellUtf8(st->historyList, row, 3, r.product_code);
        setCell(st->historyList, row, 4, r.blood_type);
        setCell(st->historyList, row, 5, r.rhd);
        setCell(st->historyList, row, 6, r.composition);
        setCellUtf8(st->historyList, row, 7, r.norm);
        setCell(st->historyList, row, 8, r.unit);
        setCell(st->historyList, row, 9, r.cross_method);
        setCell(st->historyList, row, 10, r.main_result);
        setCell(st->historyList, row, 11, r.second_result);
        setCellUtf8(st->historyList, row, 12, slashDateTimeMinute(r.match_date));
        setCell(st->historyList, row, 13, r.match_man);
        setCell(st->historyList, row, 14, r.source);
    }
    SendMessageW(st->historyList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(st->historyList, nullptr, TRUE);
}

LRESULT CALLBACK searchEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                UINT_PTR subclassId, DWORD_PTR refData) {
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        HWND parent = reinterpret_cast<HWND>(refData);
        SendMessageW(parent, WM_COMMAND, MAKEWPARAM(IDC_SEARCH, BN_CLICKED), 0);
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, searchEditProc, subclassId);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

COLORREF statusColor(const std::string& status) {
    const auto value = search::trim(status);
    if (value == STATUS_PENDING_TEXT) return COLOR_WHITE;
    if (value == STATUS_REVIEWED_TEXT) return COLOR_REVIEWED;
    if (value == STATUS_COMPLETED_TEXT) return COLOR_COMPLETED;
    return COLOR_WHITE;
}

bool isEmergencyTranProperty(const std::string& value) {
    return search::trim(value) == EMERGENCY_TRAN_PROPERTY;
}

COLORREF bloodCellColor(const search::BloodRequestRow& row, int col) {
    if (col == search::blood_request_columns::TranProperty && isEmergencyTranProperty(row.tran_property)) {
        return COLOR_EMERGENCY;
    }
    return statusColor(row.apply_status);
}

void setValue(HWND hwnd, const std::string& value) {
    if (!hwnd) return;
    std::wstring text = search::utf8_to_wide(value);
    SetWindowTextW(hwnd, text.c_str());
}

void setValue(HWND hwnd, const wchar_t* value) {
    if (!hwnd) return;
    SetWindowTextW(hwnd, value ? value : L"");
}

void setValueOrDefault(HWND hwnd, const std::string& value, const wchar_t* fallback) {
    if (search::trim(value).empty()) {
        setValue(hwnd, fallback);
        return;
    }
    setValue(hwnd, value);
}

HBRUSH detailValueBrush(BloodState* st, HDC hdc, HWND child) {
    if (child == st->detail.patientName) {
        SetTextColor(hdc, COLOR_EMERGENCY);
        SetBkColor(hdc, COLOR_WHITE);
        return reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    }
    if (child == st->detail.applyType) {
        SetTextColor(hdc, st->emergencyApplyType ? COLOR_EMERGENCY : COLOR_BLACK);
        SetBkColor(hdc, COLOR_WHITE);
        return reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    }
    return nullptr;
}

void updateDetail(BloodState* st, int selected) {
    if (selected < 0 || selected >= static_cast<int>(st->rows.size())) {
        setValue(st->detail.patientName, L"");
        setValue(st->detail.patientNo, L"");
        setValue(st->detail.patientType, L"");
        setValue(st->detail.sex, L"");
        setValue(st->detail.age, L"");
        setValue(st->detail.applyDept, L"");
        setValue(st->detail.patientDept, L"");
        setValue(st->detail.bedNo, L"");
        setValue(st->detail.urgency, L"");
        setValue(st->detail.applyTime, L"");
        setValue(st->detail.diagnosis, L"");
        setValue(st->detail.bloodProduct, L"");
        setValue(st->detail.transfusionHistory, L"");
        setValue(st->detail.reactionHistory, L"");
        setValue(st->detail.applyType, L"备血");
        st->emergencyApplyType = false;
        st->historyRows.clear();
        if (st->historyList) ListView_DeleteAllItems(st->historyList);
        setValue(st->detail.applyAbo, L"未知");
        setValue(st->detail.applyRh, L"未知");
        SetWindowTextW(st->status, L"请选择左侧申请记录。");
        return;
    }

    const auto& row = st->rows[static_cast<size_t>(selected)];
    setValue(st->detail.patientName, row.patient_name);
    setValue(st->detail.patientNo, row.patient_no);
    setValue(st->detail.patientType, row.patient_no_type);
    setValue(st->detail.sex, row.patient_sex);
    setValue(st->detail.age, row.patient_age);
    setValue(st->detail.applyDept, row.apply_dept);
    setValue(st->detail.patientDept, row.apply_dept);
    setValue(st->detail.bedNo, row.apply_bed_no);
    setValue(st->detail.urgency, row.urgency_level);
    setValue(st->detail.applyTime, row.apply_time);
    setValue(st->detail.diagnosis, L"");
    setValue(st->detail.bloodProduct, row.apply_composition);
    setValueOrDefault(st->detail.transfusionHistory, row.transfusion_history, L"无");
    setValue(st->detail.reactionHistory, row.reaction_history);
    setValue(st->detail.applyType, row.tran_property);
    st->emergencyApplyType = isEmergencyTranProperty(row.tran_property);
    setValue(st->detail.applyAbo, row.apply_abo);
    setValue(st->detail.applyRh, row.apply_rhd);
    InvalidateRect(st->detail.applyType, nullptr, TRUE);

    std::wstring status = L"已选择：";
    status += search::utf8_to_wide(row.patient_name);
    if (!row.apply_form_no.empty()) {
        status += L"  申请单号：";
        status += search::utf8_to_wide(row.apply_form_no);
    }
    SetWindowTextW(st->status, status.c_str());
    if (st->activeTab == BLOOD_TAB_HISTORY) {
        populateBloodHistory(st, row.patient_no);
    }
}

void runBloodQuery(BloodState* st) {
    if (search::build_connection_string_w(st->ctx.dbSettings).empty()) {
        MessageBoxW(nullptr, L"请先在“系统设置”中填写数据库连接信息。", L"缺少数据库设置", MB_ICONWARNING);
        return;
    }

    wchar_t buf[256]{};
    search::BloodQueryFilters f;
    f.connection_string = search::wide_to_utf8(search::build_connection_string_w(st->ctx.dbSettings));

    GetWindowTextW(st->patientNo, buf, 256);
    f.patient_no = search::trim(search::wide_to_utf8(buf));
    GetWindowTextW(st->patientName, buf, 256);
    f.patient_name = search::trim(search::wide_to_utf8(buf));
    GetWindowTextW(st->formNo, buf, 256);
    f.apply_form_no = search::trim(search::wide_to_utf8(buf));

    int sel = static_cast<int>(SendMessageW(st->statusCombo, CB_GETCURSEL, 0, 0));
    if (sel > 0) {
        wchar_t statusText[32]{};
        SendMessageW(st->statusCombo, CB_GETLBTEXT, static_cast<WPARAM>(sel),
                     reinterpret_cast<LPARAM>(statusText));
        f.apply_status = search::trim(search::wide_to_utf8(statusText));
    }

    SYSTEMTIME time{};
    if (DateTime_GetSystemtime(st->startDate, &time) == GDT_VALID) {
        char dbuf[16]{};
        std::snprintf(dbuf, sizeof(dbuf), "%04u-%02u-%02u", time.wYear, time.wMonth, time.wDay);
        f.start_date = dbuf;
    }
    if (DateTime_GetSystemtime(st->endDate, &time) == GDT_VALID) {
        char dbuf[16]{};
        std::snprintf(dbuf, sizeof(dbuf), "%04u-%02u-%02u", time.wYear, time.wMonth, time.wDay);
        f.end_date = dbuf;
    }

    st->rows.clear();
    st->selectedCellRow = -1;
    st->selectedCellCol = -1;
    ListView_DeleteAllItems(st->list);
    updateDetail(st, -1);
    SetWindowTextW(st->status, L"正在查询输血申请...");

    std::string error;
    if (!search::query_blood_requests(f, st->rows, error)) {
        SetWindowTextW(st->status, L"查询失败。");
        MessageBoxW(GetParent(st->list), search::utf8_to_wide(error).c_str(), L"查询失败", MB_ICONERROR);
        return;
    }

    for (size_t i = 0; i < st->rows.size(); i++) {
        const auto& r = st->rows[i];
        insertEmptyRow(st->list, static_cast<int>(i));
        setCell(st->list, static_cast<int>(i), search::blood_request_columns::TranProperty,     r.tran_property);
        setCell(st->list, static_cast<int>(i), search::blood_request_columns::PatientName,      r.patient_name);
        setCell(st->list, static_cast<int>(i), search::blood_request_columns::ApplyDept,        r.apply_dept);
        setCell(st->list, static_cast<int>(i), search::blood_request_columns::ApplyBedNo,       r.apply_bed_no);
        setCell(st->list, static_cast<int>(i), search::blood_request_columns::ApplyAbo,         r.apply_abo);
        setCell(st->list, static_cast<int>(i), search::blood_request_columns::ApplyRhd,         r.apply_rhd);
        setCell(st->list, static_cast<int>(i), search::blood_request_columns::ApplyComposition, r.apply_composition);
        setCell(st->list, static_cast<int>(i), search::blood_request_columns::PatientNo,        r.patient_no);
        setCell(st->list, static_cast<int>(i), search::blood_request_columns::ApplyFormNo,      r.apply_form_no);
        setCell(st->list, static_cast<int>(i), search::blood_request_columns::CheckDoctor,      r.check_doctor);
        setCell(st->list, static_cast<int>(i), search::blood_request_columns::CheckDate,        r.check_date);
    }

    wchar_t msg[128]{};
    std::swprintf(msg, 128, L"查询完成，共 %zu 条输血申请。", st->rows.size());
    SetWindowTextW(st->status, msg);
    if (!st->rows.empty()) {
        st->selectedCellRow = 0;
        st->selectedCellCol = 0;
        updateDetail(st, 0);
        InvalidateRect(st->list, nullptr, FALSE);
    }
}

void addSummaryField(BloodState* st, HWND parent, const wchar_t* label, HWND& value,
                     int x, int y, int labelW, int valueW, const wchar_t* initial = L"", int valueH = 28) {
    const int labelH = 22;
    HWND l = createStatic(parent, label, SS_RIGHT, 0, 0, 0, 0);
    value = createValue(parent, initial, 0, 0, 0, 0);
    addLayout(st, l, LayoutArea::RightSummary, x, y + 4, labelW, labelH);
    addLayout(st, value, LayoutArea::RightSummary, x + labelW + 6, y, valueW, valueH);
}

void addSearchLabel(BloodState* st, HWND parent, const wchar_t* label, int x, int y, int w, int h) {
    addLayout(st, createStatic(parent, label, SS_LEFT, 0, 0, 0, 0), LayoutArea::LeftSearch, x, y, w, h);
}

void createToolbar(HWND hwnd, BloodState* st) {
    struct ToolDef {
        int id;
        const wchar_t* text;
        int w;
        bool enabled;
    };
    const ToolDef tools[] = {
        {IDC_TOOL_AUDIT,       L"审核申请",     84, false},
        {IDC_TOOL_CANCEL,      L"取消审核",     84, false},
        {IDC_TOOL_REJECT,      L"驳回申请",     84, false},
        {IDC_TOOL_RESTORE,     L"驳回恢复",     84, false},
        {IDC_TOOL_ARCHIVE,     L"申请单归档",   96, false},
        {IDC_TOOL_UNARCHIVE,   L"取消归档",     84, false},
        {IDC_TOOL_DELETE,      L"删除配血",     84, false},
        {IDC_TOOL_CROSS_MATCH, L"交叉报废",     84, false},
        {IDC_TOOL_PRINT,       L"打印(F6)",     84, false},
        {IDC_TOOL_REFRESH,     L"刷新(R)",      84, true},
        {IDC_TOOL_RESULT,      L"查询检验结果", 112, true},
        {IDC_TOOL_CLOSE,       L"关闭(C)",      84, true},
    };

    int x = 12;
    for (const auto& tool : tools) {
        HWND btn = createToolButton(hwnd, tool.id, tool.text, 0, 0, 0, 0);
        EnableWindow(btn, tool.enabled ? TRUE : FALSE);
        addLayout(st, btn, LayoutArea::Toolbar, x, 10, tool.w, 42);
        x += tool.w + 10;
    }
    st->toolbarLine = createStatic(hwnd, L"", SS_ETCHEDHORZ, 0, 0, 0, 0);
}

void createBloodControls(HWND hwnd, BloodState* st) {
    const float s = search::dpi_scale_factor(hwnd);
    auto S = [s](int v) { return static_cast<int>(v * s); };

    createToolbar(hwnd, st);

    st->searchBox = search::create_groupbox(hwnd, L"信息检索", 0, 0, 0, 0);
    addSearchLabel(st, hwnd, L"病人编号：", 12, 28, 60, 24);
    st->patientNo = search::create_edit(hwnd, IDC_PATIENT_NO, 0, 0, 0, 0);
    SetWindowSubclass(st->patientNo, searchEditProc, SEARCH_EDIT_SUBCLASS, reinterpret_cast<DWORD_PTR>(hwnd));
    addLayout(st, st->patientNo, LayoutArea::LeftSearch, 76, 22, 250, 28);

    addSearchLabel(st, hwnd, L"病人姓名：", 12, 64, 60, 24);
    st->patientName = search::create_edit(hwnd, IDC_PATIENT_NAME, 0, 0, 0, 0);
    SetWindowSubclass(st->patientName, searchEditProc, SEARCH_EDIT_SUBCLASS, reinterpret_cast<DWORD_PTR>(hwnd));
    addLayout(st, st->patientName, LayoutArea::LeftSearch, 76, 58, 250, 28);

    addSearchLabel(st, hwnd, L"申请单号：", 12, 100, 60, 24);
    st->formNo = search::create_edit(hwnd, IDC_FORM_NO, 0, 0, 0, 0);
    SetWindowSubclass(st->formNo, searchEditProc, SEARCH_EDIT_SUBCLASS, reinterpret_cast<DWORD_PTR>(hwnd));
    addLayout(st, st->formNo, LayoutArea::LeftSearch, 76, 94, 250, 28);

    addSearchLabel(st, hwnd, L"申请状态：", 12, 136, 60, 24);
    st->statusCombo = search::create_combo(hwnd, IDC_STATUS_COMBO, 0, 0, 0, 0, false);
    addLayout(st, st->statusCombo, LayoutArea::LeftSearch, 76, 130, 250, 180);
    const wchar_t* statusItems[] = {
        STATUS_ALL_LABEL,
        STATUS_PENDING_LABEL,
        STATUS_REVIEWED_LABEL,
        STATUS_COMPLETED_LABEL,
    };
    for (const auto* item : statusItems) {
        SendMessageW(st->statusCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    }
    SendMessageW(st->statusCombo, CB_SETCURSEL, 0, 0);

    addSearchLabel(st, hwnd, L"申请日期：", 12, 172, 60, 24);
    st->startDate = search::create_date_picker(hwnd, IDC_START_DATE, 0, 0, 0, 0);
    st->endDate = search::create_date_picker(hwnd, IDC_END_DATE, 0, 0, 0, 0);
    addLayout(st, st->startDate, LayoutArea::LeftSearch, 76, 166, 120, 28);
    addLayout(st, st->endDate, LayoutArea::LeftSearch, 204, 166, 120, 28);

    st->searchButton = createOwnerDrawButton(hwnd, IDC_SEARCH, L"查找", 0, 0, 0, 0);
    addLayout(st, st->searchButton, LayoutArea::LeftSearch, 332, 22, 110, 172);

    st->legend = createStatic(hwnd, L"白色【未审核】，绿色【已审核】，黄色【已完结】", SS_CENTER, 0, 0, 0, 0);

    st->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, hwnd, win32_control_id(IDC_LIST), st->ctx.instance, nullptr);
    ListView_SetExtendedListViewStyle(st->list, LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    struct ListColumnDef {
        int id;
        const wchar_t* title;
        int width;
    };
    const ListColumnDef listColumns[] = {
        {search::blood_request_columns::TranProperty,     L"紧急程度", 68},
        {search::blood_request_columns::PatientName,      L"姓名",     80},
        {search::blood_request_columns::ApplyDept,        L"申请科室", 185},
        {search::blood_request_columns::ApplyBedNo,       L"床号",     45},
        {search::blood_request_columns::ApplyAbo,         L"申请ABO",  80},
        {search::blood_request_columns::ApplyRhd,         L"申请RHD",  80},
        {search::blood_request_columns::ApplyComposition, L"申请成分", 180},
        {search::blood_request_columns::PatientNo,        L"病人号",   120},
        {search::blood_request_columns::ApplyFormNo,      L"申请单号", 150},
        {search::blood_request_columns::CheckDoctor,      L"审核人",   90},
        {search::blood_request_columns::CheckDate,        L"审核时间", 140},
    };
    for (const auto& col : listColumns) {
        search::add_list_column(st->list, col.id, col.title, S(col.width));
    }

    st->summaryBox = search::create_groupbox(hwnd, L"", 0, 0, 0, 0);
    addSummaryField(st, hwnd, L"患者姓名：", st->detail.patientName, 16, 22, 104, 220, L"", 44);
    addSummaryField(st, hwnd, L"病人类型：", st->detail.patientType, 370, 22, 104, 210, L"");
    addSummaryField(st, hwnd, L"性别：", st->detail.sex, 720, 22, 70, 170, L"");
    addSummaryField(st, hwnd, L"年龄：", st->detail.age, 1040, 22, 70, 190, L"");
    addSummaryField(st, hwnd, L"申请科室：", st->detail.applyDept, 1290, 22, 104, 330);

    addSummaryField(st, hwnd, L"病人编号：", st->detail.patientNo, 16, 78, 104, 220);
    addSummaryField(st, hwnd, L"输血史：", st->detail.transfusionHistory, 370, 78, 104, 210, L"");
    addSummaryField(st, hwnd, L"反应史：", st->detail.reactionHistory, 720, 78, 70, 170, L"");
    addSummaryField(st, hwnd, L"床号：", st->detail.bedNo, 1040, 78, 70, 190);
    addSummaryField(st, hwnd, L"开单时间：", st->detail.applyTime, 1290, 78, 104, 330);

    addSummaryField(st, hwnd, L"申请类型：", st->detail.applyType, 16, 134, 104, 220, L"备血");
    addSummaryField(st, hwnd, L"紧急程度：", st->detail.urgency, 370, 134, 104, 210);
    addSummaryField(st, hwnd, L"申请ABO：", st->detail.applyAbo, 720, 134, 94, 146, L"未知");
    addSummaryField(st, hwnd, L"申请Rh(D)：", st->detail.applyRh, 1040, 134, 104, 156, L"未知");
    addSummaryField(st, hwnd, L"临床诊断：", st->detail.diagnosis, 1290, 134, 104, 330);

    HWND bloodLabel = createStatic(hwnd, L"输血品种：", SS_RIGHT, 0, 0, 0, 0);
    st->detail.bloodProduct = createValue(hwnd, L"", 0, 0, 0, 0);
    addLayout(st, bloodLabel, LayoutArea::RightSummary, 16, 190, 104, 24);
    addLayout(st, st->detail.bloodProduct, LayoutArea::RightSummary, 126, 184, 1598, 32);

    st->tabs = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, hwnd, win32_control_id(IDC_TABS), st->ctx.instance, nullptr);
    const wchar_t* tabTexts[] = {
        L"输血历史", L"用血申请", L"交叉配血", L"检测信息", L"收费信息", L"不良反应", L"输血效果", L"用血前评估"
    };
    for (int i = 0; i < static_cast<int>(sizeof(tabTexts) / sizeof(tabTexts[0])); ++i) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(tabTexts[i]);
        TabCtrl_InsertItem(st->tabs, i, &item);
    }
    TabCtrl_SetCurSel(st->tabs, BLOOD_TAB_HISTORY);

    st->patientDetailBox = search::create_groupbox(hwnd, L"患者详细信息", 0, 0, 0, 0);
    addLayout(st, createStatic(hwnd, L"身高(cm)：", SS_RIGHT, 0, 0, 0, 0), LayoutArea::DetailMain, 70, 48, 120, 24);
    addLayout(st, createValue(hwnd, L"0", 0, 0, 0, 0), LayoutArea::DetailMain, 198, 42, 170, 28);
    addLayout(st, createStatic(hwnd, L"体重(kg)：", SS_RIGHT, 0, 0, 0, 0), LayoutArea::DetailMain, 420, 48, 120, 24);
    addLayout(st, createValue(hwnd, L"0", 0, 0, 0, 0), LayoutArea::DetailMain, 548, 42, 180, 28);
    addLayout(st, createStatic(hwnd, L"身份证号：", SS_RIGHT, 0, 0, 0, 0), LayoutArea::DetailMain, 770, 48, 120, 24);
    addLayout(st, createValue(hwnd, L"", 0, 0, 0, 0), LayoutArea::DetailMain, 898, 42, 520, 28);

    st->requestDetailBox = search::create_groupbox(hwnd, L"用血申请信息", 0, 0, 0, 0);
    addLayout(st, createStatic(hwnd, L"预定输血时间：", SS_RIGHT, 0, 0, 0, 0), LayoutArea::DetailMain, 42, 154, 150, 24);
    addLayout(st, createValue(hwnd, L"", 0, 0, 0, 0), LayoutArea::DetailMain, 198, 148, 250, 28);
    addLayout(st, createStatic(hwnd, L"输血目的：", SS_RIGHT, 0, 0, 0, 0), LayoutArea::DetailMain, 590, 154, 120, 24);
    addLayout(st, createValue(hwnd, L"", 0, 0, 0, 0), LayoutArea::DetailMain, 718, 148, 680, 28);
    addLayout(st, createStatic(hwnd, L"输血同意书：", SS_RIGHT, 0, 0, 0, 0), LayoutArea::DetailMain, 42, 206, 150, 24);
    addLayout(st, createValue(hwnd, L"", 0, 0, 0, 0), LayoutArea::DetailMain, 198, 200, 250, 28);
    addLayout(st, createStatic(hwnd, L"手术名称：", SS_RIGHT, 0, 0, 0, 0), LayoutArea::DetailMain, 590, 206, 120, 24);
    addLayout(st, createValue(hwnd, L"", 0, 0, 0, 0), LayoutArea::DetailMain, 718, 200, 680, 28);
    addLayout(st, createStatic(hwnd, L"失血量(ML)：", SS_RIGHT, 0, 0, 0, 0), LayoutArea::DetailMain, 42, 258, 150, 24);
    addLayout(st, createValue(hwnd, L"", 0, 0, 0, 0), LayoutArea::DetailMain, 198, 252, 250, 28);
    addLayout(st, createStatic(hwnd, L"用血备注：", SS_RIGHT, 0, 0, 0, 0), LayoutArea::DetailMain, 590, 258, 120, 24);
    addLayout(st, createMultilineValue(hwnd, L"", 0, 0, 0, 0), LayoutArea::DetailMain, 718, 252, 680, 42);

    HWND componentList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        0, 0, 0, 0, hwnd, nullptr, st->ctx.instance, nullptr);
    ListView_SetExtendedListViewStyle(componentList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    search::add_list_column(componentList, 0, L"输血成份", S(380));
    search::add_list_column(componentList, 1, L"血量", S(170));
    search::add_list_column(componentList, 2, L"单位", S(110));
    search::add_list_column(componentList, 3, L"项目备注", S(270));
    addLayout(st, componentList, LayoutArea::DetailMain, 16, 326, 1400, 180);

    st->labBox = search::create_groupbox(hwnd, L"申请时的检测信息", 0, 0, 0, 0);
    const wchar_t* labs[] = {L"病人ABO血型：", L"Rh(D)血型：", L"不规则抗体", L"HGB", L"HCT", L"PLT", L"PT", L"APTT", L"ALT", L"HbsAg", L"FIB", L"抗HCV", L"HIV抗体", L"梅毒抗体"};
    const wchar_t* labVals[] = {L"未知", L"未知", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L""};
    for (int i = 0; i < 14; ++i) {
        const int y = 48 + i * 46;
        addLayout(st, createStatic(hwnd, labs[i], SS_RIGHT, 0, 0, 0, 0), LayoutArea::DetailSide, 20, y + 4, 120, 24);
        addLayout(st, createValue(hwnd, labVals[i], 0, 0, 0, 0), LayoutArea::DetailSide, 146, y, 150, 28);
    }

    st->historyBox = search::create_groupbox(hwnd, L"历史配血信息", 0, 0, 0, 0);
    st->historyList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, hwnd, win32_control_id(IDC_HISTORY_LIST), st->ctx.instance, nullptr);
    ListView_SetExtendedListViewStyle(st->historyList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    struct HistoryColumnDef {
        int id;
        const wchar_t* title;
        int width;
    };
    const HistoryColumnDef historyColumns[] = {
        {0, L"出库时间", 150},
        {1, L"出库人", 90},
        {2, L"血袋编号", 140},
        {3, L"产品码", 90},
        {4, L"血型", 70},
        {5, L"RH(D)", 70},
        {6, L"血液成分", 150},
        {7, L"血量", 70},
        {8, L"单位", 60},
        {9, L"配血方法", 150},
        {10, L"主侧结果", 150},
        {11, L"次侧结果", 150},
        {12, L"配血时间", 150},
        {13, L"配血者", 90},
        {14, L"血袋来源", 110},
    };
    for (const auto& col : historyColumns) {
        search::add_list_column(st->historyList, col.id, col.title, S(col.width));
    }
    ShowWindow(st->historyBox, SW_HIDE);
    ShowWindow(st->historyList, SW_HIDE);

    st->status = createStatic(hwnd, L"请输入条件后查询。", SS_LEFT, 0, 0, 0, 0);

    EnumChildWindows(hwnd, [](HWND child, LPARAM p) -> BOOL {
        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(p), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(st->ctx.uiFont));
}

void layoutBloodWindow(HWND hwnd, BloodState* st) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int clientW = rc.right - rc.left;
    const int clientH = rc.bottom - rc.top;
    const float s = search::dpi_scale_factor(hwnd);
    auto S = [s](int v) { return static_cast<int>(v * s); };

    const int toolbarH = S(64);
    const int margin = S(8);
    const int gap = S(8);
    const int leftW = (std::max)(S(420), (std::min)(S(448), clientW * 27 / 100));
    const int rightX = leftW + gap;
    const int rightW = (std::max)(S(500), clientW - rightX - margin);
    const int contentTop = toolbarH + gap;
    const int searchH = S(218);
    const int legendH = S(30);
    const int listTop = contentTop + searchH + legendH;
    const int listH = (std::max)(S(160), clientH - listTop - S(30));
    const int summaryH = S(238);
    const int tabH = S(42);
    const int tabsY = contentTop + summaryH + gap;
    const int detailY = tabsY + tabH - S(2);
    const int detailH = (std::max)(S(240), clientH - detailY - S(30));
    const int sideW = (std::min)(S(330), (std::max)(S(250), rightW / 7));
    const int mainW = (std::max)(S(420), rightW - sideW - gap);

    MoveWindow(st->toolbarLine, 0, toolbarH - S(1), clientW, S(1), TRUE);
    MoveWindow(st->searchBox, 0, contentTop, leftW, searchH, TRUE);
    MoveWindow(st->legend, 0, contentTop + searchH, leftW, legendH, TRUE);
    MoveWindow(st->list, 0, listTop, leftW, listH, TRUE);
    MoveWindow(st->summaryBox, rightX, contentTop, rightW, summaryH, TRUE);
    MoveWindow(st->tabs, rightX, tabsY, rightW, tabH, TRUE);
    const bool showHistory = st->activeTab == BLOOD_TAB_HISTORY;
    ShowWindow(st->patientDetailBox, showHistory ? SW_HIDE : SW_SHOW);
    ShowWindow(st->requestDetailBox, showHistory ? SW_HIDE : SW_SHOW);
    ShowWindow(st->labBox, showHistory ? SW_HIDE : SW_SHOW);
    ShowWindow(st->historyBox, showHistory ? SW_SHOW : SW_HIDE);
    ShowWindow(st->historyList, showHistory ? SW_SHOW : SW_HIDE);
    MoveWindow(st->patientDetailBox, rightX + S(10), detailY + S(8), mainW - S(20), S(102), TRUE);
    MoveWindow(st->requestDetailBox, rightX + S(10), detailY + S(120), mainW - S(20), S(198), TRUE);
    MoveWindow(st->labBox, rightX + mainW + gap, detailY + S(8), sideW, detailH - S(8), TRUE);
    MoveWindow(st->historyBox, rightX + S(10), detailY + S(8), rightW - S(20), detailH - S(8), TRUE);
    MoveWindow(st->historyList, rightX + S(20), detailY + S(40), rightW - S(40), detailH - S(48), TRUE);
    MoveWindow(st->status, margin, clientH - S(24), clientW - S(16), S(22), TRUE);

    for (const auto& item : st->layout) {
        int originX = 0;
        int originY = 0;
        int areaW = clientW;
        int areaH = toolbarH;
        switch (item.area) {
            case LayoutArea::Toolbar:
                originX = 0;
                originY = 0;
                areaW = clientW;
                areaH = toolbarH;
                break;
            case LayoutArea::LeftSearch:
                originX = 0;
                originY = contentTop;
                areaW = leftW;
                areaH = searchH;
                break;
            case LayoutArea::RightSummary:
                originX = rightX;
                originY = contentTop;
                areaW = rightW;
                areaH = summaryH;
                break;
            case LayoutArea::DetailMain:
                originX = rightX + S(10);
                originY = detailY + S(8);
                areaW = mainW - S(20);
                areaH = detailH;
                break;
            case LayoutArea::DetailSide:
                originX = rightX + mainW + gap;
                originY = detailY + S(8);
                areaW = sideW;
                areaH = detailH;
                break;
        }

        const int relX = S(item.x);
        const int availableW = areaW - relX - S(8);
        const bool tabHidden = showHistory && (item.area == LayoutArea::DetailMain || item.area == LayoutArea::DetailSide);
        if (tabHidden || (item.area == LayoutArea::DetailMain && availableW < S(50))) {
            ShowWindow(item.hwnd, SW_HIDE);
            continue;
        }
        ShowWindow(item.hwnd, SW_SHOW);

        const int x = originX + relX;
        const int y = originY + S(item.y);
        const int w = (std::min)(S(item.w), (std::max)(S(30), availableW));
        const int h = (std::min)(S(item.h), (std::max)(S(18), areaH - S(item.y) - S(8)));
        MoveWindow(item.hwnd, x, y, w, h, TRUE);
    }
}

std::string dateOnly(const std::string& value) {
    return value.size() > 10 ? value.substr(0, 10) : value;
}

std::string referenceRange(const search::ResultRow& row) {
    const auto lower = search::trim(row.downbound);
    const auto upper = search::trim(row.upbound);
    if (lower.empty()) {
        return upper;
    }
    if (upper.empty()) {
        return lower;
    }
    return lower + "~" + upper;
}

std::string normalizeNumericCode(const std::string& value) {
    std::string text = search::trim(value);
    const size_t dot = text.find('.');
    if (dot != std::string::npos) {
        text = text.substr(0, dot);
    }
    size_t first_digit = 0;
    while (first_digit + 1 < text.size() && text[first_digit] == '0') {
        ++first_digit;
    }
    text = text.substr(first_digit);
    return text;
}

std::set<std::string> parseRoomMachinePairs(const std::string& text) {
    std::set<std::string> pairs;
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t semi = text.find(';', pos);
        const std::string group = search::trim(text.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos));
        const size_t colon = group.find(':');
        if (colon != std::string::npos) {
            const std::string room = normalizeNumericCode(group.substr(0, colon));
            const std::string machines = group.substr(colon + 1);
            size_t machinePos = 0;
            while (machinePos <= machines.size()) {
                const size_t comma = machines.find(',', machinePos);
                const std::string machine = normalizeNumericCode(machines.substr(machinePos, comma == std::string::npos ? std::string::npos : comma - machinePos));
                if (!room.empty() && !machine.empty()) {
                    pairs.insert(room + ":" + machine);
                }
                if (comma == std::string::npos) break;
                machinePos = comma + 1;
            }
        }
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
    return pairs;
}

void countLisReportInstrumentMatches(const LisState* st, size_t& bloodTypeCount, size_t& cbcCount) {
    bloodTypeCount = 0;
    cbcCount = 0;
    if (!st) return;
    for (const auto& row : st->report_rows) {
        const std::string room = normalizeNumericCode(row.room_code);
        const std::string machine = normalizeNumericCode(row.mach_code);
        if (room.empty() || machine.empty()) continue;
        const std::string key = room + ":" + machine;
        if (st->lis_blood_type_machine_pairs.find(key) != st->lis_blood_type_machine_pairs.end()) {
            ++bloodTypeCount;
        } else if (st->lis_cbc_machine_pairs.find(key) != st->lis_cbc_machine_pairs.end()) {
            ++cbcCount;
        }
    }
}

std::string deviationText(const search::ResultRow& row) {
    switch (search::result_row_tone(row)) {
        case search::ResultRowTone::High: return "↑";
        case search::ResultRowTone::Low:  return "↓";
        default:                          return "";
    }
}

int selectedBloodRow(BloodState* st) {
    if (!st) return -1;
    if (st->selectedCellRow >= 0 && st->selectedCellRow < static_cast<int>(st->rows.size())) {
        return st->selectedCellRow;
    }
    const int selected = ListView_GetNextItem(st->list, -1, LVNI_SELECTED);
    if (selected >= 0 && selected < static_cast<int>(st->rows.size())) {
        return selected;
    }
    return -1;
}

void insertLisReportRow(HWND list, int index, const search::ReportRow& row) {
    insertEmptyRow(list, index);
    setCellUtf8(list, index, LisReportSampleNo, row.oper_no);
    setCellUtf8(list, index, LisReportInspectTime, dateOnly(row.chk_date));
    setCellUtf8(list, index, LisReportGroupName, row.group_name);
    setCellUtf8(list, index, LisReportBarcode, row.txm_no);
    setCellUtf8(list, index, LisReportRequester, row.requester);
    setCellUtf8(list, index, LisReportReviewer, row.reviewer);
    setCellUtf8(list, index, LisReportRoomCode, row.room_code);
    setCellUtf8(list, index, LisReportMachineCode, row.mach_code);
}

void insertLisResultRow(HWND list, int index, const search::ResultRow& row) {
    insertEmptyRow(list, index);
    setCellUtf8(list, index, 0, row.item_code);
    setCell(list, index, 1, row.item_name);
    setCell(list, index, 2, row.result);
    setCellUtf8(list, index, 3, deviationText(row));
    setCellUtf8(list, index, 4, referenceRange(row));
}

const std::string& lisReportSortValue(const search::ReportRow& row, int col) {
    switch (col) {
        case LisReportSampleNo: return row.oper_no;
        case LisReportInspectTime: return row.chk_date;
        case LisReportGroupName: return row.group_name;
        case LisReportBarcode: return row.txm_no;
        case LisReportRequester: return row.requester;
        case LisReportReviewer: return row.reviewer;
        case LisReportRoomCode: return row.room_code;
        case LisReportMachineCode: return row.mach_code;
        default: { static const std::string empty; return empty; }
    }
}

void sortLisReports(LisState* st) {
    if (!st || st->reportSortCol < 0) return;
    const int col = st->reportSortCol;
    const bool ascending = st->reportSortAscending;
    std::stable_sort(st->report_rows.begin(), st->report_rows.end(),
        [col, ascending](const search::ReportRow& lhs, const search::ReportRow& rhs) {
            const std::string& lv = lisReportSortValue(lhs, col);
            const std::string& rv = lisReportSortValue(rhs, col);
            if (lv == rv) {
                return lhs.rep_no < rhs.rep_no;
            }
            return ascending ? lv < rv : rv < lv;
        });
}

void presentLisReports(LisState* st) {
    SendMessageW(st->reports, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(st->reports);
    for (size_t i = 0; i < st->report_rows.size(); ++i) {
        insertLisReportRow(st->reports, static_cast<int>(i), st->report_rows[i]);
    }
    SendMessageW(st->reports, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(st->reports, nullptr, TRUE);
}

void presentLisResults(LisState* st) {
    SendMessageW(st->results, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(st->results);
    for (size_t i = 0; i < st->result_rows.size(); ++i) {
        insertLisResultRow(st->results, static_cast<int>(i), st->result_rows[i]);
    }
    SendMessageW(st->results, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(st->results, nullptr, TRUE);
}

struct LisSummaryParts {
    std::wstring bloodValue;
    std::wstring bloodDate;
    std::wstring cbcValue;
    std::wstring cbcDate;
    std::wstring irregularValue;
    std::wstring irregularDate;
    std::wstring directAntiglobulinValue;
    std::wstring directAntiglobulinDate;
};

std::wstring lisBloodValueText(const search::LisSummary& summary) {
    const std::string abo = search::trim(summary.abo);
    const std::string rhd = search::trim(summary.rhd);
    std::wstring text = L"血型：";
    if (abo.empty() && rhd.empty()) {
        return text + L"无";
    }

    if (!abo.empty()) {
        text += search::utf8_to_wide(abo);
    }
    if (!rhd.empty()) {
        if (!abo.empty()) text += L"，";
        text += L"RhD";
        text += search::utf8_to_wide(rhd);
    }
    return text;
}

std::wstring lisCbcValueText(const search::LisSummary& summary) {
    if (search::trim(summary.hgb).empty() && search::trim(summary.plt).empty()) {
        return L"血常规：无";
    }

    std::wstring text = L"Hb：";
    if (search::trim(summary.hgb).empty()) {
        text += L"无";
    } else {
        text += search::utf8_to_wide(search::trim(summary.hgb));
    }
    text += L"，PLT：";
    if (search::trim(summary.plt).empty()) {
        text += L"无";
    } else {
        text += search::utf8_to_wide(search::trim(summary.plt));
    }
    return text;
}

std::wstring lisSummaryValueText(const wchar_t* label, const std::string& value) {
    std::wstring text = label ? label : L"";
    const std::string trimmed = search::trim(value);
    if (trimmed.empty()) {
        text += L"无";
    } else {
        text += search::utf8_to_wide(trimmed);
    }
    return text;
}

LisSummaryParts lisSummaryParts(const search::LisSummary& summary) {
    LisSummaryParts parts;
    parts.bloodValue = lisBloodValueText(summary);
    if (!search::trim(summary.blood_type_date).empty()) {
        parts.bloodDate = search::utf8_to_wide(summary.blood_type_date) + L"  ";
    }
    parts.cbcValue = lisCbcValueText(summary);
    if (!search::trim(summary.cbc_date).empty()) {
        parts.cbcDate = search::utf8_to_wide(summary.cbc_date) + L"  ";
    }
    parts.irregularValue = lisSummaryValueText(L"不规则：", summary.irregular_antibody);
    if (!search::trim(summary.irregular_antibody_date).empty()) {
        parts.irregularDate = search::utf8_to_wide(summary.irregular_antibody_date) + L"  ";
    }
    parts.directAntiglobulinValue = lisSummaryValueText(L"直抗：", summary.direct_antiglobulin);
    if (!search::trim(summary.direct_antiglobulin_date).empty()) {
        parts.directAntiglobulinDate = search::utf8_to_wide(summary.direct_antiglobulin_date) + L"  ";
    }
    return parts;
}

HWND lisParentWindow(const LisState* st) {
    if (!st) return nullptr;
    if (st->reports) return GetParent(st->reports);
    if (st->status) return GetParent(st->status);
    return nullptr;
}

void setLisSummaryText(LisState* st, const LisSummaryParts& parts) {
    if (!st) return;
    st->summaryBloodValue = parts.bloodValue;
    st->summaryBloodDate = parts.bloodDate;
    st->summaryCbcValue = parts.cbcValue;
    st->summaryCbcDate = parts.cbcDate;
    st->summaryIrregularValue = parts.irregularValue;
    st->summaryIrregularDate = parts.irregularDate;
    st->summaryDirectAntiglobulinValue = parts.directAntiglobulinValue;
    st->summaryDirectAntiglobulinDate = parts.directAntiglobulinDate;
    invalidateLisSummary(lisParentWindow(st), st);
}

void setLisSummaryLoading(LisState* st) {
    if (!st) return;
    st->summaryBloodValue = L"正在读取最近检验摘要...";
    st->summaryBloodDate.clear();
    st->summaryCbcValue.clear();
    st->summaryCbcDate.clear();
    st->summaryIrregularValue.clear();
    st->summaryIrregularDate.clear();
    st->summaryDirectAntiglobulinValue.clear();
    st->summaryDirectAntiglobulinDate.clear();
    invalidateLisSummary(lisParentWindow(st), st);
}

int textPixelWidth(HDC hdc, HFONT font, const std::wstring& text) {
    if (!hdc || text.empty()) return 0;
    HFONT old = font ? static_cast<HFONT>(SelectObject(hdc, font)) : nullptr;
    SIZE size{};
    GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size);
    if (old) SelectObject(hdc, old);
    return size.cx;
}

void drawTextSingleLine(HDC hdc, HFONT font, const std::wstring& text, RECT rc) {
    if (!hdc || text.empty() || rc.right <= rc.left || rc.bottom <= rc.top) return;
    HFONT old = font ? static_cast<HFONT>(SelectObject(hdc, font)) : nullptr;
    DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (old) SelectObject(hdc, old);
}

void drawTextRunClipped(HDC hdc, HFONT font, const std::wstring& text, RECT rc, int& x, COLORREF color) {
    if (!hdc || text.empty() || x >= rc.right || rc.bottom <= rc.top) return;
    HFONT oldFont = font ? static_cast<HFONT>(SelectObject(hdc, font)) : nullptr;
    const COLORREF oldColor = SetTextColor(hdc, color);
    TEXTMETRICW tm{};
    GetTextMetricsW(hdc, &tm);
    const int textH = static_cast<int>(tm.tmHeight);
    const int rcH = static_cast<int>(rc.bottom - rc.top);
    const int y = static_cast<int>(rc.top) + (std::max)(0, (rcH - textH) / 2);
    RECT clip{x, rc.top, rc.right, rc.bottom};
    ExtTextOutW(hdc, x, y, ETO_CLIPPED, &clip, text.c_str(), static_cast<UINT>(text.size()), nullptr);
    SIZE size{};
    GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size);
    x += size.cx;
    SetTextColor(hdc, oldColor);
    if (oldFont) SelectObject(hdc, oldFont);
}

void drawValueWithAlert(HDC hdc, const LisState* st, const std::wstring& value,
                        RECT rc, size_t alertPos, size_t alertLen) {
    if (!hdc || !st || value.empty() || rc.right <= rc.left) return;
    if (alertPos == std::wstring::npos || alertLen == 0 || alertPos >= value.size()) {
        SetTextColor(hdc, COLOR_BLACK);
        drawTextSingleLine(hdc, st->summaryBoldFont, value, rc);
        return;
    }

    const size_t safeAlertLen = (std::min)(alertLen, value.size() - alertPos);
    int x = rc.left;
    drawTextRunClipped(hdc, st->summaryBoldFont, value.substr(0, alertPos), rc, x, COLOR_BLACK);
    drawTextRunClipped(hdc, st->summaryBoldFont, value.substr(alertPos, safeAlertLen), rc, x, COLOR_EMERGENCY);
    drawTextRunClipped(hdc, st->summaryBoldFont, value.substr(alertPos + safeAlertLen), rc, x, COLOR_BLACK);
}

std::wstring slashDateForSummary(const std::wstring& date) {
    if (date.size() < 10 || date[4] != L'-' || date[7] != L'-') return date;
    const std::wstring year = date.substr(0, 4);
    std::wstring month = date.substr(5, 2);
    std::wstring day = date.substr(8, 2);
    if (month.size() == 2 && month[0] == L'0') month.erase(month.begin());
    if (day.size() == 2 && day[0] == L'0') day.erase(day.begin());
    return year + L"/" + month + L"/" + day;
}

enum class LisSummaryAlert {
    None,
    RhDNegative,
    Positive
};

void drawLisSummaryLine(HDC hdc, const LisState* st, const std::wstring& date,
                        const std::wstring& value, RECT rc, LisSummaryAlert alert) {
    if (!hdc || !st) return;
    const std::wstring displayDate = slashDateForSummary(date);
    const int dateW = (std::max)(textPixelWidth(hdc, st->summaryFont, L"0000/00/00  "),
                                 textPixelWidth(hdc, st->summaryFont, displayDate));
    RECT dateRc = rc;
    dateRc.right = (std::min)(rc.right, rc.left + dateW);
    const COLORREF oldText = SetTextColor(hdc, RGB(0x68, 0x75, 0x80));
    drawTextSingleLine(hdc, st->summaryFont, displayDate, dateRc);

    RECT valueRc = rc;
    valueRc.left = dateRc.right;
    size_t alertPos = std::wstring::npos;
    size_t alertLen = 0;
    if (alert == LisSummaryAlert::RhDNegative) {
        alertPos = value.find(L"RhD阴性");
        alertLen = std::wstring(L"RhD阴性").size();
    } else if (alert == LisSummaryAlert::Positive) {
        alertPos = value.find(L"阳性");
        alertLen = std::wstring(L"阳性").size();
    }
    drawValueWithAlert(hdc, st, value, valueRc, alertPos, alertLen);
    SetTextColor(hdc, oldText);
}

void drawLisSummary(HWND hwnd, const LisState* st, HDC hdc) {
    if (!hwnd || !st || !hdc || st->summaryRect.right <= st->summaryRect.left) return;
    FillRect(hdc, &st->summaryRect, st->bgBrush ? st->bgBrush : GetSysColorBrush(COLOR_WINDOW));

    const float s = search::dpi_scale_factor(hwnd);
    auto S = [s](int v) { return static_cast<int>(v * s); };
    const int lineH = S(24);
    const int topY = st->summaryRect.top + S(1);
    const int rowGap = S(4);

    RECT bloodRc{st->summaryRect.left, topY, st->summaryRect.right, topY + lineH};
    RECT cbcRc{st->summaryRect.left, bloodRc.bottom + rowGap, st->summaryRect.right, bloodRc.bottom + rowGap + lineH};
    RECT irregularRc{st->summaryRect.left, cbcRc.bottom + rowGap, st->summaryRect.right, cbcRc.bottom + rowGap + lineH};
    RECT directAntiglobulinRc{st->summaryRect.left, irregularRc.bottom + rowGap, st->summaryRect.right, irregularRc.bottom + rowGap + lineH};

    const int oldBk = SetBkMode(hdc, TRANSPARENT);
    const COLORREF oldText = SetTextColor(hdc, COLOR_BLACK);
    drawLisSummaryLine(hdc, st, st->summaryBloodDate, st->summaryBloodValue, bloodRc, LisSummaryAlert::RhDNegative);
    drawLisSummaryLine(hdc, st, st->summaryCbcDate, st->summaryCbcValue, cbcRc, LisSummaryAlert::None);
    drawLisSummaryLine(hdc, st, st->summaryIrregularDate, st->summaryIrregularValue, irregularRc, LisSummaryAlert::Positive);
    drawLisSummaryLine(hdc, st, st->summaryDirectAntiglobulinDate, st->summaryDirectAntiglobulinValue, directAntiglobulinRc, LisSummaryAlert::Positive);
    SetTextColor(hdc, oldText);
    SetBkMode(hdc, oldBk);
}

std::string lisConnectionString(LisState* st) {
    return search::wide_to_utf8(search::build_connection_string_w(st->ctx.dbSettings));
}

int lisDaysValue(LisState* st) {
    if (!st || !st->days) return DEFAULT_LIS_DAYS;
    const int days = static_cast<int>(SendMessageW(st->days, TBM_GETPOS, 0, 0));
    return (std::min)(MAX_LIS_DAYS, (std::max)(MIN_LIS_DAYS, days));
}

void updateLisDaysHint(LisState* st) {
    if (!st || !st->labelDaysHint) return;
    SetWindowTextW(st->labelDaysHint, (std::to_wstring(lisDaysValue(st)) + L" 天").c_str());
}

void queryLisResults(LisState* st, int reportIndex) {
    st->result_rows.clear();
    ListView_DeleteAllItems(st->results);
    if (reportIndex < 0 || reportIndex >= static_cast<int>(st->report_rows.size())) {
        return;
    }

    const HWND hwnd = GetParent(st->results);
    const int generation = ++st->resultGeneration;
    const std::string conn = lisConnectionString(st);
    const std::string repNo = st->report_rows[static_cast<size_t>(reportIndex)].rep_no;

    std::thread([hwnd, conn, repNo, reportIndex, generation]() {
        auto* result = new LisResultsResult;
        result->generation = generation;
        result->reportIndex = reportIndex;
        result->ok = search::query_results(conn, repNo, result->rows, result->error);
        if (!PostMessageW(hwnd, WM_LIS_RESULTS_DONE, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }).detach();
}

void selectLisReport(LisState* st, int index) {
    st->suppressReportSelection = true;
    ListView_SetItemState(st->reports, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    if (index < 0 || index >= static_cast<int>(st->report_rows.size())) {
        st->suppressReportSelection = false;
        st->result_rows.clear();
        ListView_DeleteAllItems(st->results);
        return;
    }
    ListView_SetItemState(st->reports, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    st->suppressReportSelection = false;
    ListView_EnsureVisible(st->reports, index, FALSE);
    queryLisResults(st, index);
}

void setLisIdentityEnabled(LisState* st, bool enabled) {
    if (!st) return;
    HWND controls[] = {
        st->labelPatientNo,
        st->patientNo,
        st->labelPatientAge,
        st->patientAge,
        st->labelPatientSex,
        st->patientSex,
    };
    for (HWND control : controls) {
        if (control) {
            EnableWindow(control, enabled ? TRUE : FALSE);
            InvalidateRect(control, nullptr, TRUE);
        }
    }
}

void setLisIdentityHint(LisState* st, const wchar_t* text, COLORREF color) {
    if (!st || !st->identityHint) return;
    st->identityHintColor = color;
    SetWindowTextW(st->identityHint, text ? text : L"");
    InvalidateRect(st->identityHint, nullptr, TRUE);
}

void runLisQuery(LisState* st, LisQueryMode mode = LisQueryMode::PatientNo) {
    const bool byName = mode == LisQueryMode::Name;
    const bool bySocialNo = mode == LisQueryMode::SocialNo;
    setLisIdentityEnabled(st, mode == LisQueryMode::PatientNo);

    const auto conn = lisConnectionString(st);
    if (conn.empty()) {
        setLisIdentityEnabled(st, TRUE);
        MessageBoxW(nullptr, L"请先在“系统设置”中填写数据库连接信息。", L"缺少数据库设置", MB_ICONWARNING);
        return;
    }
    if (!byName && search::trim(st->patient_no).empty()) {
        setLisIdentityEnabled(st, TRUE);
        MessageBoxW(nullptr, L"当前输血申请没有病人号，无法查询检验结果。", L"缺少病人号", MB_ICONWARNING);
        return;
    }
    if (byName && search::trim(st->patient_name).empty()) {
        setLisIdentityEnabled(st, TRUE);
        MessageBoxW(nullptr, L"当前输血申请没有姓名，无法按名字查询检验结果。", L"缺少姓名", MB_ICONWARNING);
        return;
    }

    SYSTEMTIME today{};
    GetLocalTime(&today);
    today.wHour = today.wMinute = today.wSecond = today.wMilliseconds = 0;
    SYSTEMTIME start = subtractDays(today, lisDaysValue(st));

    char startBuf[16]{};
    char endBuf[16]{};
    std::snprintf(startBuf, sizeof(startBuf), "%04u-%02u-%02u", start.wYear, start.wMonth, start.wDay);
    std::snprintf(endBuf, sizeof(endBuf), "%04u-%02u-%02u", today.wYear, today.wMonth, today.wDay);

    search::QueryFilters filters;
    filters.connection_string = conn;
    filters.lis_abo_codes = st->lis_abo_codes;
    filters.lis_rhd_codes = st->lis_rhd_codes;
    filters.lis_hgb_codes = st->lis_hgb_codes;
    filters.lis_plt_codes = st->lis_plt_codes;
    filters.lis_irregular_antibody_codes = st->lis_irregular_antibody_codes;
    filters.lis_direct_antiglobulin_codes = st->lis_direct_antiglobulin_codes;
    filters.lis_blood_type_machines = st->lis_blood_type_machines;
    filters.lis_cbc_machines = st->lis_cbc_machines;
    filters.lis_blood_exclude_machines = st->lis_blood_exclude_machines;
    const bool phoneLookupAttempted = byName && !search::trim(st->patient_no).empty();
    if (byName) {
        filters.patient_name = st->patient_name;
        if (phoneLookupAttempted) {
            std::string phone;
            std::string phoneError;
            if (search::query_latest_report_phone_by_reg_no(conn, st->patient_no, phone, phoneError)
                && !search::trim(phone).empty()) {
                filters.patient_phone = phone;
            }
        }
    } else if (bySocialNo) {
        std::string idError;
        if (!search::query_inpatient_nos_by_social_no_from_reg_no(conn, st->patient_no, filters.patient_nos, idError)) {
            setLisIdentityEnabled(st, TRUE);
            MessageBoxW(nullptr, search::utf8_to_wide(idError).c_str(), L"按身份证查询失败", MB_ICONERROR);
            return;
        }
        if (filters.patient_nos.empty()) {
            setLisIdentityEnabled(st, TRUE);
            MessageBoxW(nullptr, L"未能从住院病人信息表中找到当前病人号对应的身份证号，无法按身份证查询。", L"未找到身份证", MB_ICONWARNING);
            return;
        }
    } else {
        filters.patient_no = st->patient_no;
    }
    filters.start_date = startBuf;
    filters.end_date = endBuf;
    filters.limit = 500;

    st->report_rows.clear();
    st->result_rows.clear();
    ListView_DeleteAllItems(st->reports);
    ListView_DeleteAllItems(st->results);
    setLisSummaryLoading(st);
    if (bySocialNo) {
        setLisIdentityHint(st, L"已按身份证匹配住院号", COLOR_TRUSTED);
        SetWindowTextW(st->status, L"正在按身份证查询检验结果...");
    } else if (byName && !search::trim(filters.patient_phone).empty()) {
        setLisIdentityHint(st, L"已按姓名 + 电话匹配", COLOR_TRUSTED);
        SetWindowTextW(st->status, L"正在按姓名和电话查询检验结果...");
    } else if (byName && !search::trim(st->patient_no).empty()) {
        setLisIdentityHint(st, L"仅按姓名查询，请谨慎", COLOR_WARNING_TEXT);
        SetWindowTextW(st->status, L"仅按姓名查询，正在查询检验结果...");
    } else {
        setLisIdentityHint(st, byName ? L"仅按姓名查询，可能存在重名" : L"已按病人号精确匹配", byName ? COLOR_WARNING_TEXT : COLOR_TRUSTED);
        SetWindowTextW(st->status, byName ? L"正在按名字查询检验结果..." : L"正在查询检验结果...");
    }

    EnableWindow(st->queryButton, FALSE);
    EnableWindow(st->queryNameButton, FALSE);
    EnableWindow(st->queryIdButton, FALSE);
    const HWND hwnd = GetParent(st->reports);
    const int generation = ++st->queryGeneration;
    const size_t socialNoPatientCount = filters.patient_nos.size();
    std::thread([hwnd, filters, mode, byName, phoneLookupAttempted, socialNoPatientCount, generation]() {
        auto* result = new LisQueryResult;
        result->generation = generation;
        result->mode = mode;
        result->phoneFiltered = byName && !search::trim(filters.patient_phone).empty();
        result->phoneLookupAttempted = phoneLookupAttempted;
        result->socialNoPatientCount = socialNoPatientCount;
        result->ok = search::query_blood_lis_reports(filters, result->reports, result->error);
        if (!PostMessageW(hwnd, WM_LIS_QUERY_DONE, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }).detach();
    std::thread([hwnd, filters, generation]() {
        auto* result = new LisSummaryResult;
        result->generation = generation;
        result->ok = search::query_lis_summary(filters, result->summary, result->error);
        if (!PostMessageW(hwnd, WM_LIS_SUMMARY_DONE, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }).detach();
}

void finishLisQuery(HWND hwnd, LisState* st, std::unique_ptr<LisQueryResult> result) {
    if (!st || result->generation != st->queryGeneration) return;
    EnableWindow(st->queryButton, TRUE);
    EnableWindow(st->queryNameButton, TRUE);
    EnableWindow(st->queryIdButton, TRUE);
    if (!result->ok) {
        SetWindowTextW(st->status, L"查询失败。");
        MessageBoxW(hwnd, search::utf8_to_wide(result->error).c_str(), L"查询检验结果失败", MB_ICONERROR);
        return;
    }

    st->report_rows = std::move(result->reports);
    if (search::trim(st->patient_age).empty() && !st->report_rows.empty()) {
        setValue(st->patientAge, st->report_rows.front().age);
    }
    if (search::trim(st->patient_sex).empty() && !st->report_rows.empty()) {
        setValue(st->patientSex, st->report_rows.front().sex);
    }
    sortLisReports(st);
    presentLisReports(st);
    size_t bloodTypeCount = 0;
    size_t cbcCount = 0;
    countLisReportInstrumentMatches(st, bloodTypeCount, cbcCount);
    wchar_t msg[192]{};
    const wchar_t* prefix = L"查询完成";
    if (result->mode == LisQueryMode::SocialNo) {
        std::swprintf(msg, 192, L"按身份证查询完成（匹配 %zu 个住院号），共 %zu 条组合项目。血型 %zu，血常规 %zu。",
                      result->socialNoPatientCount, st->report_rows.size(), bloodTypeCount, cbcCount);
        SetWindowTextW(st->status, msg);
        selectLisReport(st, st->report_rows.empty() ? -1 : 0);
        return;
    }
    if (result->mode == LisQueryMode::Name && result->phoneFiltered) {
        prefix = L"按姓名+电话查询完成";
    } else if (result->mode == LisQueryMode::Name && result->phoneLookupAttempted) {
        prefix = L"按名字查询完成（未获取到电话）";
    } else if (result->mode == LisQueryMode::Name) {
        prefix = L"按名字查询完成";
    }
    std::swprintf(msg, 192, L"%ls，共 %zu 条组合项目。血型 %zu，血常规 %zu。",
                  prefix, st->report_rows.size(), bloodTypeCount, cbcCount);
    SetWindowTextW(st->status, msg);
    selectLisReport(st, st->report_rows.empty() ? -1 : 0);
}

void finishLisSummary(HWND hwnd, LisState* st, std::unique_ptr<LisSummaryResult> result) {
    if (!st || result->generation != st->queryGeneration) return;
    if (result->ok) {
        setLisSummaryText(st, lisSummaryParts(result->summary));
    } else {
        st->summaryBloodValue = L"血型：失败";
        st->summaryBloodDate.clear();
        st->summaryCbcValue = L"血常规：失败";
        st->summaryCbcDate.clear();
        st->summaryIrregularValue = L"不规则：失败";
        st->summaryIrregularDate.clear();
        st->summaryDirectAntiglobulinValue = L"直抗：失败";
        st->summaryDirectAntiglobulinDate.clear();
        invalidateLisSummary(hwnd, st);
    }
}

void finishLisResults(HWND hwnd, LisState* st, std::unique_ptr<LisResultsResult> result) {
    if (!st || result->generation != st->resultGeneration) return;
    if (!result->ok) {
        MessageBoxW(hwnd, search::utf8_to_wide(result->error).c_str(), L"查询项目明细失败", MB_ICONERROR);
        return;
    }
    st->result_rows = std::move(result->rows);
    st->result_tones.clear();
    st->result_tones.reserve(st->result_rows.size());
    for (const auto& row : st->result_rows) {
        st->result_tones.push_back(search::result_row_tone(row));
    }
    presentLisResults(st);
}

void layoutLisWindow(HWND hwnd, LisState* st) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const float s = search::dpi_scale_factor(hwnd);
    auto S = [s](int v) { return static_cast<int>(v * s); };

    const int margin = S(14);
    const int gap = S(10);
    const int sidebarW = S(360);
    const int sidebarX = margin;
    const int sidebarRight = (std::min)(w - margin, sidebarX + sidebarW);
    const int labelW = S(68);
    const int editH = S(28);
    InvalidateRect(hwnd, nullptr, TRUE);

    const int sidePad = S(10);
    const int contentX = sidebarX + sidePad;
    const int contentRight = sidebarRight - sidePad;
    const int contentW = (std::max)(S(260), contentRight - contentX);
    int sideY = margin + S(2);
    const int fieldX = contentX + labelW + S(8);
    const int fieldW = (std::max)(S(120), contentRight - fieldX);
    auto moveSectionTitle = [&](HWND label) {
        MoveWindow(label, contentX, sideY, contentW, S(24), TRUE);
        sideY += S(26);
    };
    auto moveSidebarField = [&](HWND label, HWND field, int fieldHeight = 28) {
        MoveWindow(label, contentX, sideY + S(4), labelW, S(24), TRUE);
        MoveWindow(field, fieldX, sideY, fieldW, S(fieldHeight), TRUE);
        sideY += S(fieldHeight + 6);
    };
    moveSectionTitle(st->labelPatientSection);
    moveSidebarField(st->labelPatientNo, st->patientNo);
    moveSidebarField(st->labelPatientName, st->patientName);
    moveSidebarField(st->labelPatientAge, st->patientAge);
    moveSidebarField(st->labelPatientSex, st->patientSex);

    sideY += S(8);
    MoveWindow(st->labelSummarySection, contentX, sideY, S(90), S(24), TRUE);
    ShowWindow(st->identityHint, SW_SHOW);
    MoveWindow(st->identityHint, contentX + S(96), sideY + S(1), contentW - S(96), S(24), TRUE);
    sideY += S(26);
    st->summaryRect = RECT{
        contentX,
        sideY,
        contentRight,
        sideY + S(110)
    };
    InvalidateRect(hwnd, &st->summaryRect, TRUE);
    sideY = st->summaryRect.bottom + S(16);

    ShowWindow(st->labelDays, SW_SHOW);
    MoveWindow(st->labelDays, contentX, sideY, S(120), S(24), TRUE);
    MoveWindow(st->labelDaysHint, contentX + S(120), sideY, contentW - S(120), S(24), TRUE);
    sideY += S(26);

    const int rangeLabelW = S(28);
    const int sliderX = contentX + rangeLabelW + S(6);
    const int sliderW = (std::max)(S(140), contentW - rangeLabelW * 2 - S(12));
    MoveWindow(st->labelDaysMin, contentX, sideY + S(5), rangeLabelW, S(22), TRUE);
    MoveWindow(st->days, sliderX, sideY, sliderW, S(32), TRUE);
    MoveWindow(st->labelDaysMax, contentRight - rangeLabelW, sideY + S(5), rangeLabelW, S(22), TRUE);
    sideY += S(46);

    const int buttonW = contentW;
    const int buttonH = S(34);
    MoveWindow(st->queryButton, contentX, sideY, buttonW, buttonH, TRUE);
    sideY += S(38);
    MoveWindow(st->queryNameButton, contentX, sideY, buttonW, buttonH, TRUE);
    sideY += S(38);
    MoveWindow(st->queryIdButton, contentX, sideY, buttonW, buttonH, TRUE);
    sideY += S(42);

    const int labelTop = margin;
    const int top = labelTop + S(24);
    const int listsX = sidebarRight + gap;
    const int listsRight = w - margin;
    const int listsW = (std::max)(S(760), listsRight - listsX);
    const int leftW = (std::max)(S(360), listsW * 38 / 100);
    const int rightX = listsX + leftW + gap;
    const int listH = (std::max)(S(260), h - top - S(30));
    const int reportW = leftW;
    const int resultW = (std::max)(S(420), listsRight - rightX);
    MoveWindow(st->labelReports, listsX, labelTop, S(150), S(24), TRUE);
    MoveWindow(st->labelResults, rightX, labelTop, S(150), S(24), TRUE);
    MoveWindow(st->reports, listsX, top, reportW, listH, TRUE);
    MoveWindow(st->results, rightX, top, resultW, listH, TRUE);
    MoveWindow(st->status, S(8), h - S(24), w - S(16), S(22), TRUE);

    const int reportFixedW = S(54 + 100 + 150 + 105 + 56 + 70 + 70);
    ListView_SetColumnWidth(st->reports, LisReportSampleNo, S(54));
    ListView_SetColumnWidth(st->reports, LisReportInspectTime, S(100));
    ListView_SetColumnWidth(st->reports, LisReportGroupName, S(150));
    ListView_SetColumnWidth(st->reports, LisReportBarcode, S(105));
    ListView_SetColumnWidth(st->reports, LisReportRequester, S(56));
    ListView_SetColumnWidth(st->reports, LisReportReviewer, (std::max)(S(72), reportW - reportFixedW - S(8)));
    ListView_SetColumnWidth(st->reports, LisReportRoomCode, S(70));
    ListView_SetColumnWidth(st->reports, LisReportMachineCode, S(70));

    const int resultFixedW = S(92 + 145 + 86 + 62);
    ListView_SetColumnWidth(st->results, 0, S(92));
    ListView_SetColumnWidth(st->results, 1, S(145));
    ListView_SetColumnWidth(st->results, 2, S(86));
    ListView_SetColumnWidth(st->results, 3, S(62));
    ListView_SetColumnWidth(st->results, 4, (std::max)(S(140), resultW - resultFixedW - S(8)));
}

LRESULT CALLBACK lisWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<LisState*>(GetPropW(hwnd, PROP_LIS_STATE));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            st = reinterpret_cast<LisState*>(cs->lpCreateParams);
            if (!st) { LOG_ERROR("WM_CREATE: lpCreateParams is null (LisState)"); return -1; }
            SetPropW(hwnd, PROP_LIS_STATE, reinterpret_cast<HANDLE>(st));

            st->bgBrush = CreateSolidBrush(COLOR_PAGE_BG);
            st->labelPatientNo = createStatic(hwnd, L"病人号：", SS_RIGHT, 0, 0, 0, 0);
            st->labelPatientName = createStatic(hwnd, L"姓　名：", SS_RIGHT, 0, 0, 0, 0);
            st->labelPatientAge = createStatic(hwnd, L"年　龄：", SS_RIGHT, 0, 0, 0, 0);
            st->labelPatientSex = createStatic(hwnd, L"性　别：", SS_RIGHT, 0, 0, 0, 0);
            st->labelPatientSection = createStatic(hwnd, L"患者信息", SS_LEFT, 0, 0, 0, 0);
            st->labelSummarySection = createStatic(hwnd, L"摘要信息", SS_LEFT, 0, 0, 0, 0);
            st->labelDays = createStatic(hwnd, L"查询范围", SS_LEFT, 0, 0, 0, 0);
            st->labelDaysHint = createStatic(hwnd, L"14 天", SS_RIGHT, 0, 0, 0, 0);
            st->labelDaysMin = createStatic(hwnd, L"7", SS_LEFT, 0, 0, 0, 0);
            st->labelDaysMax = createStatic(hwnd, L"365", SS_RIGHT, 0, 0, 0, 0);
            st->labelReports = createStatic(hwnd, L"组合项目", SS_LEFT, 0, 0, 0, 0);
            st->labelResults = createStatic(hwnd, L"详情信息", SS_LEFT, 0, 0, 0, 0);
            st->identityHint = createStatic(hwnd, L"已按病人号精确匹配", SS_LEFT, 0, 0, 0, 0);

            st->patientNo = createValue(hwnd, L"", 0, 0, 0, 0);
            st->patientName = createValue(hwnd, L"", 0, 0, 0, 0);
            st->patientAge = createValue(hwnd, L"", 0, 0, 0, 0);
            st->patientSex = createValue(hwnd, L"", 0, 0, 0, 0);
            st->days = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
                                       0, 0, 0, 0, hwnd, win32_control_id(IDC_LIS_DAYS),
                                       GetModuleHandleW(nullptr), nullptr);
            SendMessageW(st->days, TBM_SETRANGE, TRUE, MAKELPARAM(MIN_LIS_DAYS, MAX_LIS_DAYS));
            SendMessageW(st->days, TBM_SETPAGESIZE, 0, 7);
            SendMessageW(st->days, TBM_SETLINESIZE, 0, 1);
            SendMessageW(st->days, TBM_SETPOS, TRUE, DEFAULT_LIS_DAYS);
            st->queryButton = CreateWindowExW(0, L"BUTTON", L"按病人号查询", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                              0, 0, 0, 0, hwnd, win32_control_id(IDC_LIS_QUERY), GetModuleHandleW(nullptr), nullptr);
            st->queryNameButton = CreateWindowExW(0, L"BUTTON", L"按名字查询", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                                  0, 0, 0, 0, hwnd, win32_control_id(IDC_LIS_QUERY_NAME), GetModuleHandleW(nullptr), nullptr);
            st->queryIdButton = CreateWindowExW(0, L"BUTTON", L"按身份证查询", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                                0, 0, 0, 0, hwnd, win32_control_id(IDC_LIS_QUERY_ID), GetModuleHandleW(nullptr), nullptr);
            setLisSummaryLoading(st);

            st->reports = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_LIS_REPORTS), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(st->reports, LVS_EX_GRIDLINES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            search::add_list_column(st->reports, LisReportSampleNo, L"样本号", 54);
            search::add_list_column(st->reports, LisReportInspectTime, L"检验时间", 100);
            search::add_list_column(st->reports, LisReportGroupName, L"组合项目", 150);
            search::add_list_column(st->reports, LisReportBarcode, L"条形码", 105);
            search::add_list_column(st->reports, LisReportRequester, L"检验者", 56);
            search::add_list_column(st->reports, LisReportReviewer, L"审核者", 72);
            search::add_list_column(st->reports, LisReportRoomCode, L"科室代码", 70);
            search::add_list_column(st->reports, LisReportMachineCode, L"仪器代码", 70);

            st->results = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_LIS_RESULTS), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(st->results, LVS_EX_GRIDLINES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            search::add_list_column(st->results, 0, L"项目代码", 92);
            search::add_list_column(st->results, 1, L"项目名称", 145);
            search::add_list_column(st->results, 2, L"结果", 86);
            search::add_list_column(st->results, 3, L"偏值", 62);
            search::add_list_column(st->results, 4, L"参考范围", 140);

            st->status = createStatic(hwnd, L"", SS_LEFT, 0, 0, 0, 0);
            setValue(st->patientNo, st->patient_no);
            setValue(st->patientName, st->patient_name);
            setValue(st->patientAge, st->patient_age);
            setValue(st->patientSex, st->patient_sex);

            EnumChildWindows(hwnd, [](HWND child, LPARAM p) -> BOOL {
                SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(p), TRUE);
                return TRUE;
            }, reinterpret_cast<LPARAM>(st->ctx.uiFont));

            applyLisSummaryFonts(hwnd, st);
            PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_LIS_QUERY, BN_CLICKED), 0);
            return 0;
        }
        case WM_SIZE:
            if (st) layoutLisWindow(hwnd, st);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            if (st) {
                drawLisSummary(hwnd, st, hdc);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc,
                     st && st->bgBrush ? st->bgBrush : GetSysColorBrush(COLOR_WINDOW));
            return 1;
        }
        case app::WM_APP_FONT_CHANGED:
            if (st && lp) {
                st->ctx.uiFont = reinterpret_cast<HFONT>(lp);
                search::apply_font_to_children(hwnd, st->ctx.uiFont);
                applyLisSummaryFonts(hwnd, st);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        case WM_HSCROLL:
            if (st && reinterpret_cast<HWND>(lp) == st->days) {
                updateLisDaysHint(st);
                return 0;
            }
            break;
        case WM_COMMAND:
            if (st && LOWORD(wp) == IDC_LIS_QUERY) {
                runLisQuery(st);
                return 0;
            }
            if (st && LOWORD(wp) == IDC_LIS_QUERY_NAME) {
                runLisQuery(st, LisQueryMode::Name);
                return 0;
            }
            if (st && LOWORD(wp) == IDC_LIS_QUERY_ID) {
                runLisQuery(st, LisQueryMode::SocialNo);
                return 0;
            }
            break;
        case WM_LIS_QUERY_DONE:
            if (st) {
                std::unique_ptr<LisQueryResult> result(reinterpret_cast<LisQueryResult*>(lp));
                finishLisQuery(hwnd, st, std::move(result));
            } else {
                delete reinterpret_cast<LisQueryResult*>(lp);
            }
            return 0;
        case WM_LIS_SUMMARY_DONE:
            if (st) {
                std::unique_ptr<LisSummaryResult> result(reinterpret_cast<LisSummaryResult*>(lp));
                finishLisSummary(hwnd, st, std::move(result));
            } else {
                delete reinterpret_cast<LisSummaryResult*>(lp);
            }
            return 0;
        case WM_LIS_RESULTS_DONE:
            if (st) {
                std::unique_ptr<LisResultsResult> result(reinterpret_cast<LisResultsResult*>(lp));
                finishLisResults(hwnd, st, std::move(result));
            } else {
                delete reinterpret_cast<LisResultsResult*>(lp);
            }
            return 0;
        case WM_NOTIFY: {
            if (!st) break;
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (nm->idFrom == IDC_LIS_REPORTS && nm->code == LVN_COLUMNCLICK) {
                auto* clicked = reinterpret_cast<NMLISTVIEW*>(lp);
                if (clicked->iSubItem >= LisReportSampleNo && clicked->iSubItem <= LisReportMachineCode) {
                    std::string selectedRepNo;
                    const int selected = ListView_GetNextItem(st->reports, -1, LVNI_SELECTED);
                    if (selected >= 0 && selected < static_cast<int>(st->report_rows.size())) {
                        selectedRepNo = st->report_rows[static_cast<size_t>(selected)].rep_no;
                    }

                    if (st->reportSortCol == clicked->iSubItem) {
                        st->reportSortAscending = !st->reportSortAscending;
                    } else {
                        st->reportSortCol = clicked->iSubItem;
                        st->reportSortAscending = true;
                    }
                    sortLisReports(st);
                    presentLisReports(st);

                    int selectIndex = st->report_rows.empty() ? -1 : 0;
                    if (!selectedRepNo.empty()) {
                        for (size_t i = 0; i < st->report_rows.size(); ++i) {
                            if (st->report_rows[i].rep_no == selectedRepNo) {
                                selectIndex = static_cast<int>(i);
                                break;
                            }
                        }
                    }
                    selectLisReport(st, selectIndex);
                }
                return 0;
            }
            if (nm->idFrom == IDC_LIS_REPORTS && nm->code == LVN_ITEMCHANGED) {
                if (st->suppressReportSelection) return 0;
                auto* changed = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((changed->uChanged & LVIF_STATE) &&
                    (changed->uNewState & LVIS_SELECTED) &&
                    !(changed->uOldState & LVIS_SELECTED)) {
                    queryLisResults(st, changed->iItem);
                }
                return 0;
            }
            if (nm->idFrom == IDC_LIS_RESULTS && nm->code == NM_CUSTOMDRAW) {
                auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    const int idx = static_cast<int>(cd->nmcd.dwItemSpec);
                    if (idx >= 0 && idx < static_cast<int>(st->result_rows.size())) {
                        const auto tone = st->result_tones[static_cast<size_t>(idx)];
                        if (tone == search::ResultRowTone::High) cd->clrText = RGB(220, 0, 0);
                        else if (tone == search::ResultRowTone::Low) cd->clrText = RGB(0, 0, 220);
                    }
                    return CDRF_NEWFONT;
                }
            }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            if (!st) break;
            HDC hdc = reinterpret_cast<HDC>(wp);
            HWND child = reinterpret_cast<HWND>(lp);
            SetBkColor(hdc, COLOR_PAGE_BG);
            if (child && child == st->identityHint) {
                SetTextColor(hdc, st->identityHintColor);
            } else if (child && (child == st->labelDaysHint || child == st->labelDaysMin || child == st->labelDaysMax)) {
                SetTextColor(hdc, RGB(0x68, 0x75, 0x80));
            } else if (child && (child == st->labelPatientSection || child == st->labelSummarySection)) {
                SetTextColor(hdc, RGB(0x16, 0x2B, 0x3A));
            } else {
                SetTextColor(hdc, child && !IsWindowEnabled(child) ? GetSysColor(COLOR_GRAYTEXT) : COLOR_BLACK);
            }
            return reinterpret_cast<LRESULT>(st->bgBrush);
        }
        case WM_CTLCOLOREDIT: {
            if (!st) break;
            HDC hdc = reinterpret_cast<HDC>(wp);
            SetBkColor(hdc, COLOR_WHITE);
            SetTextColor(hdc, COLOR_BLACK);
            return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
        }
        case WM_DESTROY:
            RemovePropW(hwnd, PROP_LIS_STATE);
            if (st && st->bgBrush) DeleteObject(st->bgBrush);
            if (st && st->summaryFont) DeleteObject(st->summaryFont);
            if (st && st->summaryBoldFont) DeleteObject(st->summaryBoldFont);
            delete st;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void showLisWindow(HWND owner, const ModuleContext& ctx, const search::BloodRequestRow& row) {
    HWND ownerRoot = owner ? GetAncestor(owner, GA_ROOT) : nullptr;
    if (!ownerRoot) ownerRoot = owner;

    if (search::trim(row.patient_no).empty()) {
        MessageBoxW(ownerRoot ? ownerRoot : owner, L"当前输血申请没有病人号，无法查询检验结果。", L"缺少病人号", MB_ICONWARNING);
        return;
    }

    static bool registered = false;
    if (!registered) {
        REGISTER_MDI_CHILD_CLASS(ctx.instance, lisWndProc, LIS_WND_CLASS, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        registered = true;
    }

    auto* st = new LisState;
    st->ctx = ctx;
    st->patient_no = row.patient_no;
    st->patient_name = row.patient_name;
    st->patient_age = row.patient_age;
    st->patient_sex = row.patient_sex;

    // Cache LIS summary settings once — they don't change during the popup session.
    {
        const auto appSettings = search::load_settings(search::default_ini_path());
        st->lis_abo_codes = search::wide_to_utf8(appSettings.lis.abo_codes);
        st->lis_rhd_codes = search::wide_to_utf8(appSettings.lis.rhd_codes);
        st->lis_hgb_codes = search::wide_to_utf8(appSettings.lis.hgb_codes);
        st->lis_plt_codes = search::wide_to_utf8(appSettings.lis.plt_codes);
        st->lis_irregular_antibody_codes = search::wide_to_utf8(appSettings.lis.irregular_antibody_codes);
        st->lis_direct_antiglobulin_codes = search::wide_to_utf8(appSettings.lis.direct_antiglobulin_codes);
        st->lis_blood_type_machines = search::wide_to_utf8(appSettings.lis.blood_type_machines);
        st->lis_cbc_machines = search::wide_to_utf8(appSettings.lis.cbc_machines);
        st->lis_blood_exclude_machines = search::wide_to_utf8(appSettings.lis.blood_lis_exclude_machines);
        st->lis_blood_type_machine_pairs = parseRoomMachinePairs(st->lis_blood_type_machines);
        st->lis_cbc_machine_pairs = parseRoomMachinePairs(st->lis_cbc_machines);
    }

    const RECT popupRect = centeredPopupRect(ownerRoot, 2240, 1580, 1100, 790);

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, LIS_WND_CLASS, L"LIS检验信息",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        popupRect.left, popupRect.top,
        popupRect.right - popupRect.left,
        popupRect.bottom - popupRect.top,
        ownerRoot ? ownerRoot : owner, nullptr, ctx.instance, st);
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    } else {
        delete st;
    }
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<BloodState*>(GetPropW(hwnd, PROP_STATE));

    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* mcs = reinterpret_cast<MDICREATESTRUCTW*>(cs->lpCreateParams);
            st = reinterpret_cast<BloodState*>(mcs->lParam);
            if (!st) { LOG_ERROR("WM_CREATE: lpCreateParams is null (BloodState)"); return -1; }
            SetPropW(hwnd, PROP_STATE, reinterpret_cast<HANDLE>(st));
            st->bgBrush = CreateSolidBrush(COLOR_PAGE_BG);
            st->searchBrush = CreateSolidBrush(COLOR_SEARCH_BUTTON);
            createBloodControls(hwnd, st);
            setDefaultDateRange(st);
            layoutBloodWindow(hwnd, st);
            PostMessageW(hwnd, WM_BLOOD_AUTO_QUERY, 0, 0);
            return 0;
        }
        case WM_BLOOD_AUTO_QUERY:
            if (st) runBloodQuery(st);
            return 0;
        case WM_SIZE:
            if (st) layoutBloodWindow(hwnd, st);
            return 0;
        case WM_ERASEBKGND: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc,
                     st && st->bgBrush ? st->bgBrush : GetSysColorBrush(COLOR_WINDOW));
            return 1;
        }
        case app::WM_APP_FONT_CHANGED:
            if (st && lp) {
                st->ctx.uiFont = reinterpret_cast<HFONT>(lp);
                search::apply_font_to_children(hwnd, st->ctx.uiFont);
                layoutBloodWindow(hwnd, st);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        case WM_COMMAND: {
            if (!st) break;
            const int id = LOWORD(wp);
            if (id == IDC_SEARCH || id == IDC_TOOL_REFRESH) {
                runBloodQuery(st);
                return 0;
            }
            if (id == IDC_TOOL_CLOSE) {
                SendMessageW(GetParent(hwnd), WM_MDIDESTROY, reinterpret_cast<WPARAM>(hwnd), 0);
                return 0;
            }
            if (id == IDC_TOOL_RESULT) {
                const int rowIndex = selectedBloodRow(st);
                if (rowIndex < 0) {
                    MessageBoxW(hwnd, L"请先选择一条输血申请记录。", L"查询检验结果", MB_ICONWARNING);
                    return 0;
                }
                showLisWindow(hwnd, st->ctx, st->rows[static_cast<size_t>(rowIndex)]);
                return 0;
            }
            break;
        }
        case WM_DRAWITEM: {
            if (!st) break;
            auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (draw && draw->CtlID == IDC_SEARCH) {
                FillRect(draw->hDC, &draw->rcItem, st->searchBrush);
                SetBkMode(draw->hDC, TRANSPARENT);
                SetTextColor(draw->hDC, COLOR_BLACK);

                RECT textRc = draw->rcItem;
                if (draw->itemState & ODS_SELECTED) {
                    OffsetRect(&textRc, 1, 1);
                }
                DrawTextW(draw->hDC, L"查找", -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                if (draw->itemState & ODS_FOCUS) {
                    RECT focusRc = draw->rcItem;
                    InflateRect(&focusRc, -3, -3);
                    DrawFocusRect(draw->hDC, &focusRc);
                }
                FrameRect(draw->hDC, &draw->rcItem, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
                return TRUE;
            }
            break;
        }
        case WM_NOTIFY: {
            if (!st) break;
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (nm->idFrom == IDC_LIST && nm->code == NM_CLICK) {
                auto* item = reinterpret_cast<NMITEMACTIVATE*>(lp);
                LVHITTESTINFO hit{};
                hit.pt = item->ptAction;
                const int hitRow = ListView_SubItemHitTest(st->list, &hit);
                if (hitRow >= 0 && hitRow < static_cast<int>(st->rows.size())) {
                    st->selectedCellRow = hitRow;
                    st->selectedCellCol = hit.iSubItem >= 0 ? hit.iSubItem : 0;
                    ListView_SetItemState(st->list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
                    updateDetail(st, hitRow);
                    InvalidateRect(st->list, nullptr, FALSE);
                }
                return 0;
            }
            if (nm->idFrom == IDC_LIST && nm->code == LVN_ITEMCHANGED) {
                auto* changed = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((changed->uChanged & LVIF_STATE) &&
                    (changed->uNewState & LVIS_SELECTED) &&
                    !(changed->uOldState & LVIS_SELECTED)) {
                    st->selectedCellRow = changed->iItem;
                    st->selectedCellCol = 0;
                    updateDetail(st, changed->iItem);
                }
                return 0;
            }
            if (nm->idFrom == IDC_LIST && nm->code == NM_CUSTOMDRAW) {
                auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) {
                    return CDRF_NOTIFYITEMDRAW;
                }
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    const int idx = static_cast<int>(cd->nmcd.dwItemSpec);
                    if (idx >= 0 && idx < static_cast<int>(st->rows.size())) {
                        cd->clrTextBk = statusColor(st->rows[static_cast<size_t>(idx)].apply_status);
                    }
                    return CDRF_NOTIFYSUBITEMDRAW;
                }
                if (cd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                    const int idx = static_cast<int>(cd->nmcd.dwItemSpec);
                    if (idx >= 0 && idx < static_cast<int>(st->rows.size())) {
                        if (idx == st->selectedCellRow && cd->iSubItem == st->selectedCellCol) {
                            cd->clrTextBk = GetSysColor(COLOR_HIGHLIGHT);
                            cd->clrText = GetSysColor(COLOR_HIGHLIGHTTEXT);
                        } else {
                            cd->clrTextBk = bloodCellColor(st->rows[static_cast<size_t>(idx)], cd->iSubItem);
                            cd->clrText = COLOR_BLACK;
                        }
                    }
                    return CDRF_NEWFONT;
                }
            }
            if (nm->idFrom == IDC_TABS && nm->code == TCN_SELCHANGE) {
                st->activeTab = TabCtrl_GetCurSel(st->tabs);
                if (st->activeTab == BLOOD_TAB_HISTORY) {
                    int selected = st->selectedCellRow;
                    if (selected < 0) {
                        selected = ListView_GetNextItem(st->list, -1, LVNI_SELECTED);
                    }
                    if (selected >= 0 && selected < static_cast<int>(st->rows.size())) {
                        populateBloodHistory(st, st->rows[static_cast<size_t>(selected)].patient_no);
                    } else {
                        st->historyRows.clear();
                        ListView_DeleteAllItems(st->historyList);
                    }
                }
                layoutBloodWindow(hwnd, st);
                return 0;
            }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            if (!st) break;
            HDC hdc = reinterpret_cast<HDC>(wp);
            HWND child = reinterpret_cast<HWND>(lp);
            if (HBRUSH brush = detailValueBrush(st, hdc, child)) {
                return reinterpret_cast<LRESULT>(brush);
            }
            if (child == st->legend) {
                SetTextColor(hdc, COLOR_LEGEND_TEXT);
            }
            SetBkColor(hdc, COLOR_PAGE_BG);
            return reinterpret_cast<LRESULT>(st->bgBrush);
        }
        case WM_CTLCOLOREDIT: {
            if (!st) break;
            HDC hdc = reinterpret_cast<HDC>(wp);
            HWND child = reinterpret_cast<HWND>(lp);
            if (HBRUSH brush = detailValueBrush(st, hdc, child)) {
                return reinterpret_cast<LRESULT>(brush);
            }
            break;
        }
        case WM_DESTROY:
            RemovePropW(hwnd, PROP_STATE);
            if (st) {
                if (st->bgBrush) DeleteObject(st->bgBrush);
                if (st->searchBrush) DeleteObject(st->searchBrush);
                delete st;
            }
            break;
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND create_blood_module(const ModuleContext& ctx) {
    if (HWND existing = activate_existing_mdi_child_by_title(ctx.mdiClient, WINDOW_TITLE)) {
        return existing;
    }

    REGISTER_MDI_CHILD_CLASS(ctx.instance, wndProc, WND_CLASS, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));

    auto* st = new BloodState;
    st->ctx = ctx;

    MDICREATESTRUCTW mcs{};
    mcs.szClass = WND_CLASS;
    mcs.szTitle = WINDOW_TITLE;
    mcs.hOwner = ctx.instance;
    mcs.x = mcs.y = mcs.cx = mcs.cy = CW_USEDEFAULT;

    mcs.lParam = reinterpret_cast<LPARAM>(st);
    HWND child = reinterpret_cast<HWND>(SendMessageW(ctx.mdiClient, WM_MDICREATE, 0,
        reinterpret_cast<LPARAM>(&mcs)));
    if (child) {
        SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    } else {
        delete st;
    }
    return child;
}

#endif
