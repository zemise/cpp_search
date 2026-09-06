#include "quality_control_module.h"

#ifdef _WIN32

#include "main_app.h"
#include "machine_picker_popup.h"
#include "quality_control_chart_renderer.h"
#include "quality_control_store.h"
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

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const wchar_t* WND_CLASS = L"QualityControlModuleChild";
constexpr const wchar_t* CHART_WND_CLASS = L"QualityControlChartWindow";
constexpr const wchar_t* CARD_WND_CLASS = L"QualityControlCardPane";
constexpr const wchar_t* IMPORT_WND_CLASS = L"QualityControlImportDialog";
constexpr const wchar_t* WINDOW_TITLE = L"质控分析";
constexpr const wchar_t* PROP_STATE = L"QualityControlSt";
constexpr const wchar_t* PROP_CHART = L"QualityControlChartData";
constexpr UINT WM_QC_QUERY_DONE = WM_APP + 0x681;

enum ControlId {
    IDC_START_DATE = 6801,
    IDC_END_DATE = 6802,
    IDC_MACH_CODE = 6803,
    IDC_QUERY = 6804,
    IDC_MACH_PICK = 6805,
    IDC_GROUPS = 6806,
    IDC_CARDS = 6807,
    IDC_DETAILS = 6808,
    IDC_STATUS = 6809,
    IDC_ITEM_CODE = 6810,
    IDC_LEVEL = 6811,
    IDC_EXPORT_EXCEL = 6812,
    IDC_LJ_CHART = 6813,
    IDC_CHART_LIST = 6814,
    IDC_STATUS_FILTER = 6815,
    IDC_SIDE_LIST = 6816,
    IDC_SIDE_DETAILS = 6817,
    IDC_IMPORT_QC = 6818,
    IDC_IMPORT_START_DATE = 6821,
    IDC_IMPORT_END_DATE = 6822,
    IDC_IMPORT_OK = 6823,
    IDC_IMPORT_CANCEL = 6824,
};

struct GroupRow {
    std::string key;
    std::string mach_code;
    std::string mach_name;
    std::string sample_no;
    std::string item_code;
    std::string item_name;
    std::string item_eng;
    std::string level;
    std::string qc_name;
    int count = 0;
    std::string latest_time;
    std::string latest_result;
    std::string lot_no;
    int numeric_count = 0;
    double sum = 0.0;
    double sum_square = 0.0;
    double qc_mean = 0.0;
    double qc_sd = 0.0;
    bool has_configured_mean = false;
    bool has_configured_sd = false;
    bool has_configured_stats = false;
    int evaluated_count = 0;
    int in_control_count = 0;
    int warning_count = 0;
    int out_of_control_count = 0;
};

struct CardLevelStatus {
    std::string level;
    std::string sample_no;
    std::string status;
    int count = 0;
    int evaluated_count = 0;
    int warning_count = 0;
    int out_of_control_count = 0;
    int group_index = -1;
};

struct CardRow {
    std::string key;
    std::string mach_code;
    std::string item_code;
    std::string item_name;
    std::string item_eng;
    std::string qc_name;
    std::string latest_time;
    std::string latest_result;
    std::string tester_name;
    std::string lot_no;
    double target_mean = 0.0;
    bool has_target_mean = false;
    bool target_uses_mean = false;
    int count = 0;
    int warning_count = 0;
    int out_of_control_count = 0;
    int evaluated_count = 0;
    int representative_group = -1;
    std::vector<CardLevelStatus> levels;
};

struct State {
    ModuleContext ctx;
    HWND startLabel = nullptr;
    HWND startDate = nullptr;
    HWND endLabel = nullptr;
    HWND endDate = nullptr;
    HWND machLabel = nullptr;
    HWND machCode = nullptr;
    HWND machPick = nullptr;
    HWND itemLabel = nullptr;
    HWND itemCode = nullptr;
    HWND levelLabel = nullptr;
    HWND level = nullptr;
    HWND statusFilterLabel = nullptr;
    HWND statusFilter = nullptr;
    HWND queryButton = nullptr;
    HWND refreshButton = nullptr;
    HWND chartButton = nullptr;
    HWND exportButton = nullptr;
    HWND groups = nullptr;
    HWND summary = nullptr;
    HWND cards = nullptr;
    HWND details = nullptr;
    HWND sideTitle = nullptr;
    HWND sideChartButton = nullptr;
    HWND sideDetailsButton = nullptr;
    HWND sideExportButton = nullptr;
    HWND sideList = nullptr;
    HWND status = nullptr;
    HBRUSH bgBrush = nullptr;
    bool busy = false;
    bool suppressSelectionNotify = false;
    bool detailsExpanded = false;
    int cardScrollY = 0;
    int selectedCard = -1;
    std::string selectedMachineCode;
    std::vector<qc::Result> rows;
    std::vector<GroupRow> groupsRows;
    std::vector<CardRow> cardsRows;
    int selectedGroup = -1;
};

struct QueryDone {
    bool ok = false;
    std::string error;
    std::vector<qc::Result> rows;
    int config_count = 0;
    int elapsed_ms = 0;
    bool from_cache = false;
    bool imported = false;
    std::string cached_at;
    std::string import_start_date;
    std::string import_end_date;
};

struct ImportDialogState {
    HWND startDate = nullptr;
    HWND endDate = nullptr;
    HFONT font = nullptr;
    bool done = false;
    bool accepted = false;
    std::string start;
    std::string end;
};

struct ChartWindowData {
    std::vector<qc::ChartSeries> series;
    HWND list = nullptr;
    HWND tooltip = nullptr;
    std::vector<qc::ChartHitPoint> hitPoints;
    int selectedIndex = -1;
    int hoverIndex = -1;
    int scrollY = 0;
    bool suppressListNotify = false;
    std::wstring tooltipText;
};

int S(HWND hwnd, int value) {
    return static_cast<int>(value * search::dpi_scale_factor(hwnd));
}

HWND label(HWND parent, const wchar_t* text) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
                           0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND edit(HWND parent, int id) {
    return search::create_edit(parent, id, 0, 0, 10, 24);
}

HWND button(HWND parent, int id, const wchar_t* text) {
    return search::create_button(parent, id, text, 0, 0, 80, 28);
}

HWND combo(HWND parent, int id) {
    return search::create_combo(parent, id, 0, 0, 10, 160, false);
}

HWND datePicker(HWND parent, int id) {
    HWND hwnd = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATECENTURYFORMAT,
                                0, 0, 120, 24, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
    DateTime_SetFormat(hwnd, L"yyyy-MM-dd");
    return hwnd;
}

std::string dateText(HWND hwnd) {
    SYSTEMTIME st{};
    if (DateTime_GetSystemtime(hwnd, &st) != GDT_VALID) return "";
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
    return buffer;
}

std::wstring editText(HWND hwnd) {
    wchar_t buffer[128]{};
    GetWindowTextW(hwnd, buffer, 128);
    return buffer;
}

std::string comboText(HWND hwnd) {
    wchar_t buffer[128]{};
    GetWindowTextW(hwnd, buffer, 128);
    return search::wide_to_utf8(buffer);
}

void setComboItems(HWND hwnd, const std::vector<const wchar_t*>& items) {
    SendMessageW(hwnd, CB_RESETCONTENT, 0, 0);
    for (const auto* item : items) {
        SendMessageW(hwnd, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    }
    SendMessageW(hwnd, CB_SETCURSEL, 0, 0);
}

int comboSelection(HWND hwnd) {
    return static_cast<int>(SendMessageW(hwnd, CB_GETCURSEL, 0, 0));
}

qc::Query buildQuery(State* st);

void setTodayRange(State* st) {
    SYSTEMTIME end{};
    GetLocalTime(&end);
    SYSTEMTIME start = end;
    FILETIME ft{};
    SystemTimeToFileTime(&start, &ft);
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    value.QuadPart -= static_cast<ULONGLONG>(29) * 24 * 60 * 60 * 10000000ULL;
    ft.dwLowDateTime = value.LowPart;
    ft.dwHighDateTime = value.HighPart;
    FileTimeToSystemTime(&ft, &start);
    DateTime_SetSystemtime(st->startDate, GDT_VALID, &start);
    DateTime_SetSystemtime(st->endDate, GDT_VALID, &end);
}

LRESULT CALLBACK importDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<ImportDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            st = reinterpret_cast<ImportDialogState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
            st->startDate = datePicker(hwnd, IDC_IMPORT_START_DATE);
            st->endDate = datePicker(hwnd, IDC_IMPORT_END_DATE);
            HWND startLabel = label(hwnd, L"开始日期");
            HWND endLabel = label(hwnd, L"结束日期");
            HWND ok = button(hwnd, IDC_IMPORT_OK, L"导入");
            HWND cancel = button(hwnd, IDC_IMPORT_CANCEL, L"取消");
            const int pad = S(hwnd, 18);
            const int labelW = S(hwnd, 74);
            const int rowH = S(hwnd, 26);
            const int gap = S(hwnd, 10);
            const int inputX = pad + labelW + S(hwnd, 12);
            const int inputW = S(hwnd, 166);
            const int firstY = S(hwnd, 22);
            const int secondY = firstY + rowH + gap;
            MoveWindow(startLabel, pad, firstY, labelW, rowH, TRUE);
            MoveWindow(st->startDate, inputX, firstY, inputW, rowH, TRUE);
            MoveWindow(endLabel, pad, secondY, labelW, rowH, TRUE);
            MoveWindow(st->endDate, inputX, secondY, inputW, rowH, TRUE);
            const int buttonW = S(hwnd, 78);
            const int buttonH = S(hwnd, 30);
            const int buttonGap = S(hwnd, 10);
            const int buttonY = secondY + rowH + S(hwnd, 18);
            const int cancelX = inputX + inputW - buttonW;
            const int okX = cancelX - buttonGap - buttonW;
            MoveWindow(ok, okX, buttonY, buttonW, buttonH, TRUE);
            MoveWindow(cancel, cancelX, buttonY, buttonW, buttonH, TRUE);
            SYSTEMTIME today{};
            GetLocalTime(&today);
            DateTime_SetSystemtime(st->startDate, GDT_VALID, &today);
            DateTime_SetSystemtime(st->endDate, GDT_VALID, &today);
            if (st->font) {
                EnumChildWindows(hwnd, [](HWND child, LPARAM font) -> BOOL {
                    SendMessageW(child, WM_SETFONT, font, TRUE);
                    return TRUE;
                }, reinterpret_cast<LPARAM>(st->font));
            }
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_IMPORT_OK) {
                st->start = dateText(st->startDate);
                st->end = dateText(st->endDate);
                if (st->start.empty() || st->end.empty() || st->end < st->start) {
                    MessageBoxW(hwnd, L"请选择有效的导入日期范围。", WINDOW_TITLE, MB_ICONWARNING);
                    return 0;
                }
                st->accepted = true;
                st->done = true;
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wp) == IDC_IMPORT_CANCEL) {
                st->done = true;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            if (st) st->done = true;
            DestroyWindow(hwnd);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ensureImportDialogClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = importDialogProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = IMPORT_WND_CLASS;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    registered = true;
}

RECT importDialogRect(HWND owner, HWND anchor, int w, int h) {
    RECT anchorRc{};
    if (!anchor || !GetWindowRect(anchor, &anchorRc)) {
        GetWindowRect(owner, &anchorRc);
    }
    RECT work{};
    HMONITOR monitor = MonitorFromRect(&anchorRc, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (monitor && GetMonitorInfoW(monitor, &mi)) {
        work = mi.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    }
    const int margin = S(owner, 8);
    int x = anchorRc.right - w;
    int y = anchorRc.bottom + margin;
    if (y + h > work.bottom - margin) {
        y = anchorRc.top - h - margin;
    }
    const int minX = static_cast<int>(work.left) + margin;
    const int maxX = static_cast<int>(work.right) - margin - w;
    const int minY = static_cast<int>(work.top) + margin;
    const int maxY = static_cast<int>(work.bottom) - margin - h;
    x = std::max(minX, std::min(x, maxX));
    y = std::max(minY, std::min(y, maxY));
    return RECT{x, y, x + w, y + h};
}

bool showImportRangeDialog(HWND owner, State* moduleState, qc::Query& query) {
    ensureImportDialogClass();
    ImportDialogState dialog{};
    dialog.font = moduleState ? moduleState->ctx.uiFont : nullptr;
    const int w = S(owner, 310);
    const int h = S(owner, 192);
    const RECT rc = importDialogRect(owner, moduleState ? moduleState->refreshButton : nullptr, w, h);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, IMPORT_WND_CLASS, L"导入质控",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU,
                               rc.left, rc.top, w, h, owner, nullptr, GetModuleHandleW(nullptr), &dialog);
    if (!dlg) return false;
    EnableWindow(owner, FALSE);
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    MSG msg{};
    while (!dialog.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    if (!dialog.accepted) return false;
    query = buildQuery(moduleState);
    query.start_date = dialog.start;
    query.end_date = dialog.end;
    query.level.clear();
    query.item_code.clear();
    return true;
}

void setStatus(State* st, const std::wstring& text) {
    if (st && st->status) SetWindowTextW(st->status, text.c_str());
}

bool rowVisibleByStatus(const State* st, const qc::Result& row) {
    if (!st || !st->statusFilter) return true;
    switch (comboSelection(st->statusFilter)) {
        case 1:
            return row.qc_status == "warning" || row.qc_status == "out_of_control";
        case 2:
            return row.qc_status == "out_of_control";
        case 3:
            return row.qc_status == "warning";
        default:
            return true;
    }
}

bool hasVisibleRows(const State* st) {
    if (!st) return false;
    for (const auto& row : st->rows) {
        if (rowVisibleByStatus(st, row)) return true;
    }
    return false;
}

void updateExportButton(State* st) {
    if (!st) return;
    const BOOL enabled = !st->busy && hasVisibleRows(st);
    if (st->exportButton) EnableWindow(st->exportButton, enabled);
}

void updateChartButton(State* st) {
    if (!st) return;
    const BOOL enabled = !st->busy && !st->groupsRows.empty();
    if (st->chartButton) EnableWindow(st->chartButton, enabled);
}

void setCell(HWND list, int row, int col, const std::string& text) {
    const auto wide = search::utf8_to_wide(text);
    ListView_SetItemText(list, row, col, const_cast<wchar_t*>(wide.c_str()));
}

void addColumn(HWND list, int col, const wchar_t* text, int width) {
    LVCOLUMNW lvc{};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    lvc.pszText = const_cast<wchar_t*>(text);
    lvc.cx = width;
    lvc.iSubItem = col;
    ListView_InsertColumn(list, col, &lvc);
}

void setupList(HWND list, const std::vector<std::pair<const wchar_t*, int>>& cols) {
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    for (int i = 0; i < static_cast<int>(cols.size()); ++i) {
        addColumn(list, i, cols[static_cast<size_t>(i)].first, cols[static_cast<size_t>(i)].second);
    }
}

std::string formatNumber(double value, int digits = 2) {
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
    return buffer;
}

double groupMean(const GroupRow& group) {
    return group.numeric_count > 0 ? group.sum / group.numeric_count : 0.0;
}

double groupSd(const GroupRow& group) {
    if (group.numeric_count < 2) return 0.0;
    const double mean = groupMean(group);
    const double variance = (group.sum_square - group.numeric_count * mean * mean) / (group.numeric_count - 1);
    return variance > 0.0 ? std::sqrt(variance) : 0.0;
}

std::string groupKey(const std::string& machCode, const std::string& sampleNo, const std::string& itemCode,
                     const std::string& level, const std::string& qcName) {
    return machCode + "\x1f" + sampleNo + "\x1f" + itemCode + "\x1f" + level + "\x1f" + qcName;
}

std::string rowGroupKey(const qc::Result& row) {
    return groupKey(row.mach_code, row.sample_no, row.item_code, row.level, row.qc_name);
}

std::string qcStatusText(const std::string& status) {
    if (status == "out_of_control") return "失控";
    if (status == "warning") return "警告";
    if (status == "in_control") return "在控";
    return "";
}

std::string groupStatusText(const GroupRow& group) {
    if (group.count <= 0) return "无数据";
    if (group.out_of_control_count > 0) return "失控";
    if (group.warning_count > 0) return "警告";
    if (group.evaluated_count <= 0) return "未判定";
    return "在控";
}

std::string groupTitleText(const GroupRow& group) {
    std::string title = group.qc_name.empty() ? group.item_name : group.qc_name;
    if (title.empty()) title = group.item_code;
    if (!group.level.empty()) title += " / " + group.level;
    if (!group.sample_no.empty()) title += " / 样本号 " + group.sample_no;
    return title;
}

std::string itemTitleText(const std::string& itemEng, const std::string& itemName, const std::string& itemCode) {
    const std::string name = search::trim(itemName).empty() ? search::trim(itemCode) : search::trim(itemName);
    const std::string eng = search::trim(itemEng);
    if (!eng.empty() && !name.empty()) return "（" + eng + "）" + name;
    if (!name.empty()) return name;
    return eng;
}

std::string itemIdentity(const std::string& itemCode, const std::string& itemName, const std::string& qcName) {
    const std::string code = search::trim(itemCode);
    if (!code.empty()) return code;
    const std::string name = search::trim(itemName);
    if (!name.empty()) return name;
    return search::trim(qcName);
}

std::string cardKey(const GroupRow& group) {
    return group.mach_code + "\x1f" + itemIdentity(group.item_code, group.item_name, group.qc_name);
}

std::string rowCardKey(const qc::Result& row) {
    return row.mach_code + "\x1f" + itemIdentity(row.item_code, row.item_name, row.qc_name);
}

std::string cardStatusText(const CardRow& card) {
    if (card.count <= 0) return "无数据";
    if (card.out_of_control_count > 0) return "失控";
    if (card.warning_count > 0) return "警告";
    if (card.evaluated_count <= 0) return "未判定";
    return "在控";
}

std::string cardTargetText(const CardRow& card) {
    if (card.has_target_mean) return formatNumber(card.target_mean);
    if (card.target_uses_mean) return "均值 " + formatNumber(card.target_mean);
    return "均值";
}

std::string cardTitleText(const CardRow& card) {
    return itemTitleText(card.item_eng, card.item_name, card.item_code);
}

std::string selectedQcDate(State* st) {
    if (!st) return "";
    std::string date = search::trim(dateText(st->endDate));
    if (date.empty()) date = search::trim(dateText(st->startDate));
    return date;
}

bool datePartEquals(const std::string& value, const std::string& date) {
    const std::string trimmedValue = search::trim(value);
    const std::string trimmedDate = search::trim(date);
    if (trimmedValue.size() < 10 || trimmedDate.size() < 10) return false;
    return trimmedValue.compare(0, 10, trimmedDate, 0, 10) == 0;
}

bool rowInSelectedQcDate(State* st, const qc::Result& row) {
    const std::string date = selectedQcDate(st);
    if (date.empty()) return true;
    return datePartEquals(row.effective_time, date) ||
           datePartEquals(row.inspect_date, date) ||
           datePartEquals(row.report_date, date);
}

std::string pointDisplayTime(const qc::Result& row) {
    const std::string reportTime = search::trim(row.report_time);
    if (!reportTime.empty()) return reportTime;
    return row.effective_time;
}

std::string levelStatusText(const CardLevelStatus& level) {
    if (level.count <= 0) return "无数据";
    if (level.out_of_control_count > 0) return "失控";
    if (level.warning_count > 0) return "警告";
    if (level.evaluated_count <= 0) return "未判定";
    return "在控";
}

std::string joinRules(const std::vector<std::string>& rules) {
    std::string text;
    for (const auto& rule : rules) {
        if (!text.empty()) text += ", ";
        text += rule;
    }
    return text;
}

void appendRule(qc::Result& row, const std::string& rule) {
    std::string remaining = row.qc_rules;
    size_t start = 0;
    while (start <= remaining.size()) {
        const size_t comma = remaining.find(',', start);
        const std::string existing = search::trim(remaining.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (existing == rule) return;
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    if (!row.qc_rules.empty()) row.qc_rules += ", ";
    row.qc_rules += rule;
}

void markOutOfControl(qc::Result& row, const std::string& rule) {
    row.qc_status = "out_of_control";
    appendRule(row, rule);
}

void markWarning(qc::Result& row, const std::string& rule) {
    if (row.qc_status != "out_of_control") row.qc_status = "warning";
    appendRule(row, rule);
}

bool parseNumber(const std::string& text, double& value) {
    const std::string trimmed = search::trim(text);
    if (trimmed.empty()) return false;
    char* end = nullptr;
    value = std::strtod(trimmed.c_str(), &end);
    return end && *end == '\0';
}

bool configuredQcStats(const qc::Result& row, double& mean, double& sd) {
    return parseNumber(row.target_mean, mean) && parseNumber(row.target_sd, sd) && sd > 0.0;
}

bool configuredTargetMean(const qc::Result& row, double& mean) {
    return parseNumber(row.target_mean, mean);
}

bool configuredTargetSd(const qc::Result& row, double& sd) {
    return parseNumber(row.target_sd, sd) && sd > 0.0;
}

double groupQcMean(const GroupRow& group) {
    return group.has_configured_mean ? group.qc_mean : groupMean(group);
}

double groupQcSd(const GroupRow& group) {
    return group.has_configured_sd ? group.qc_sd : groupSd(group);
}

bool groupHasQcStats(const GroupRow& group) {
    if (group.has_configured_mean && groupQcSd(group) > 0.0) return true;
    return group.has_configured_stats || (group.numeric_count > 1 && groupSd(group) > 0.0);
}

std::string groupStatsSourceText(const GroupRow& group) {
    if (group.has_configured_mean) return "靶值";
    if (groupHasQcStats(group)) return "本期";
    return "";
}

void evaluateWestgardRules(State* st) {
    if (!st) return;
    std::map<std::string, const GroupRow*> groupsByKey;
    for (const auto& group : st->groupsRows) {
        groupsByKey[group.key] = &group;
    }

    for (auto& row : st->rows) {
        row.qc_mean = 0.0;
        row.qc_sd = 0.0;
        row.qc_z = 0.0;
        row.has_qc_stats = false;
        row.has_qc_z = false;
        row.qc_status.clear();
        row.qc_rules.clear();
        if (!row.has_numeric_value) continue;

        double mean = 0.0;
        double sd = 0.0;
        if (!configuredQcStats(row, mean, sd)) {
            const auto found = groupsByKey.find(rowGroupKey(row));
            if (found == groupsByKey.end()) continue;
            if (!groupHasQcStats(*found->second)) continue;
            mean = groupQcMean(*found->second);
            sd = groupQcSd(*found->second);
        }
        if (sd <= 0.0) continue;

        row.qc_mean = mean;
        row.qc_sd = sd;
        row.qc_z = (row.result_value - mean) / sd;
        row.has_qc_stats = true;
        row.has_qc_z = true;

        const double absZ = std::fabs(row.qc_z);
        std::vector<std::string> rules;
        if (absZ >= 3.0) {
            row.qc_status = "out_of_control";
            rules.push_back("1-3s");
        } else if (absZ >= 2.0) {
            row.qc_status = "warning";
            rules.push_back("1-2s");
        } else {
            row.qc_status = "in_control";
        }
        row.qc_rules = joinRules(rules);
    }

    std::map<std::string, std::vector<size_t>> rowsByGroup;
    for (size_t i = 0; i < st->rows.size(); ++i) {
        if (st->rows[i].has_qc_z) rowsByGroup[rowGroupKey(st->rows[i])].push_back(i);
    }
    for (auto& entry : rowsByGroup) {
        auto& indexes = entry.second;
        std::sort(indexes.begin(), indexes.end(), [st](size_t lhs, size_t rhs) {
            const auto& a = st->rows[lhs];
            const auto& b = st->rows[rhs];
            const std::string aTime = pointDisplayTime(a);
            const std::string bTime = pointDisplayTime(b);
            if (aTime != bTime) return aTime < bTime;
            if (a.source_rep_no != b.source_rep_no) return a.source_rep_no < b.source_rep_no;
            return a.source_entry_key < b.source_entry_key;
        });
        for (size_t i = 1; i < indexes.size(); ++i) {
            const qc::Result& prev = st->rows[indexes[i - 1]];
            qc::Result& current = st->rows[indexes[i]];
            const bool sameSide = (prev.qc_z >= 2.0 && current.qc_z >= 2.0) ||
                                  (prev.qc_z <= -2.0 && current.qc_z <= -2.0);
            if (sameSide) markOutOfControl(current, "2-2s");

            const bool oppositeSide = (prev.qc_z > 0.0 && current.qc_z < 0.0) ||
                                      (prev.qc_z < 0.0 && current.qc_z > 0.0);
            if (oppositeSide && std::fabs(current.qc_z - prev.qc_z) > 4.0) {
                markOutOfControl(current, "R-4s");
            }
        }
        for (size_t i = 3; i < indexes.size(); ++i) {
            const qc::Result& p0 = st->rows[indexes[i - 3]];
            const qc::Result& p1 = st->rows[indexes[i - 2]];
            const qc::Result& p2 = st->rows[indexes[i - 1]];
            qc::Result& current = st->rows[indexes[i]];
            const bool highSide = p0.qc_z >= 1.0 && p1.qc_z >= 1.0 && p2.qc_z >= 1.0 && current.qc_z >= 1.0;
            const bool lowSide = p0.qc_z <= -1.0 && p1.qc_z <= -1.0 && p2.qc_z <= -1.0 && current.qc_z <= -1.0;
            if (highSide || lowSide) markWarning(current, "4-1s");
        }
        for (size_t i = 9; i < indexes.size(); ++i) {
            bool highSide = true;
            bool lowSide = true;
            for (size_t offset = 0; offset < 10; ++offset) {
                const qc::Result& point = st->rows[indexes[i - offset]];
                highSide = highSide && point.qc_z > 0.0;
                lowSide = lowSide && point.qc_z < 0.0;
            }
            if (highSide || lowSide) markWarning(st->rows[indexes[i]], "10x");
        }
    }
}

int chartListHeight(const RECT& rc) {
    const int height = static_cast<int>(rc.bottom - rc.top);
    return std::clamp(height / 4, 150, 240);
}

int chartSingleHeightForWidth(int width) {
    const int preferred = static_cast<int>(width * 0.42);
    return std::clamp(preferred, 280, 420);
}

int chartGap() {
    return 12;
}

int chartContentHeight(const ChartWindowData* data, int singleHeight) {
    if (!data || data->series.empty()) return 0;
    return static_cast<int>(data->series.size()) * singleHeight +
           static_cast<int>(data->series.size() - 1) * chartGap();
}

int chartViewportHeight(const RECT& rc) {
    return std::max(0, static_cast<int>(rc.bottom - rc.top));
}

RECT chartAreaRect(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int listH = chartListHeight(rc);
    rc.bottom = std::max(rc.top + 220, rc.bottom - listH - 8);
    return rc;
}

int chartSingleHeightForRect(const RECT& rc) {
    return chartSingleHeightForWidth(std::max(0, static_cast<int>(rc.right - rc.left)));
}

int maxChartScroll(HWND hwnd, const ChartWindowData* data) {
    const RECT rc = chartAreaRect(hwnd);
    return std::max(0, chartContentHeight(data, chartSingleHeightForRect(rc)) - chartViewportHeight(rc));
}

void updateChartScrollBar(HWND hwnd, ChartWindowData* data) {
    if (!data) return;
    const int maxScroll = maxChartScroll(hwnd, data);
    data->scrollY = std::clamp(data->scrollY, 0, maxScroll);
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    const RECT rc = chartAreaRect(hwnd);
    si.nMax = std::max(0, chartContentHeight(data, chartSingleHeightForRect(rc)) - 1);
    si.nPage = static_cast<UINT>(std::max(1, chartViewportHeight(rc)));
    si.nPos = data->scrollY;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

RECT chartListRect(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int listH = chartListHeight(rc);
    rc.top = std::max(rc.top + 220, rc.bottom - listH);
    return rc;
}

std::string zText(const qc::ChartSeries& series, const qc::ChartPoint& point) {
    (void)series;
    if (!point.has_z) return "";
    return formatNumber(point.z);
}

const qc::ChartPoint* findChartPoint(const ChartWindowData* data, int index, const qc::ChartSeries** owner = nullptr) {
    if (!data || index < 0) return nullptr;
    for (const auto& series : data->series) {
        for (const auto& point : series.points) {
            if (static_cast<int>(point.point_index) == index) {
                if (owner) *owner = &series;
                return &point;
            }
        }
    }
    return nullptr;
}

void populateChartList(ChartWindowData* data) {
    if (!data || !data->list) return;
    SendMessageW(data->list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(data->list);
    int row = 0;
    for (const auto& series : data->series) {
        for (const auto& point : series.points) {
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = row;
            const auto time = search::utf8_to_wide(point.time);
            item.pszText = const_cast<wchar_t*>(time.c_str());
            ListView_InsertItem(data->list, &item);
            setCell(data->list, row, 1, point.sample_no);
            setCell(data->list, row, 2, point.rep_no);
            setCell(data->list, row, 3, point.level);
            setCell(data->list, row, 4, point.result_text);
            setCell(data->list, row, 5, point.unit);
            setCell(data->list, row, 6, zText(series, point));
            setCell(data->list, row, 7, qcStatusText(point.qc_status));
            setCell(data->list, row, 8, point.qc_rules);
            ++row;
        }
    }
    SendMessageW(data->list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(data->list, nullptr, TRUE);
}

int hitChartPoint(const ChartWindowData* data, int x, int y) {
    if (!data) return -1;
    POINT pt{x, y};
    for (const auto& hit : data->hitPoints) {
        if (PtInRect(&hit.bounds, pt)) return static_cast<int>(hit.point_index);
    }
    return -1;
}

std::wstring chartTooltipText(const ChartWindowData* data, int index) {
    const qc::ChartSeries* series = nullptr;
    const qc::ChartPoint* found = findChartPoint(data, index, &series);
    if (!found || !series) return L"";
    const auto& point = *found;
    std::wstring text = L"报告号：" + search::utf8_to_wide(point.rep_no);
    text += L"\n样本号：" + search::utf8_to_wide(point.sample_no);
    text += L"\n时间：" + search::utf8_to_wide(point.time);
    text += L"\n项目：" + search::utf8_to_wide(point.item_name);
    text += L"\n结果：" + search::utf8_to_wide(point.result_text);
    if (!point.unit.empty()) text += L" " + search::utf8_to_wide(point.unit);
    const std::string z = zText(*series, point);
    if (!z.empty()) text += L"\nZ值：" + search::utf8_to_wide(z);
    const std::string status = qcStatusText(point.qc_status);
    if (!status.empty()) text += L"\n状态：" + search::utf8_to_wide(status);
    if (!point.qc_rules.empty()) text += L"\n命中规则：" + search::utf8_to_wide(point.qc_rules);
    return text;
}

void updateChartTooltip(HWND hwnd, ChartWindowData* data, int index) {
    if (!data || !data->tooltip) return;
    if (index == data->hoverIndex) return;
    data->hoverIndex = index;
    data->tooltipText = chartTooltipText(data, index);
    TOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.hwnd = hwnd;
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.uId = reinterpret_cast<UINT_PTR>(hwnd);
    ti.lpszText = data->tooltipText.empty() ? const_cast<wchar_t*>(L"") : data->tooltipText.data();
    SendMessageW(data->tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
}

void selectChartPoint(HWND hwnd, ChartWindowData* data, int index) {
    if (!data || !findChartPoint(data, index)) return;
    data->selectedIndex = index;
    if (data->list) {
        data->suppressListNotify = true;
        ListView_SetItemState(data->list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemState(data->list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(data->list, index, FALSE);
        data->suppressListNotify = false;
    }
    RECT rc = chartAreaRect(hwnd);
    InvalidateRect(hwnd, &rc, FALSE);
}

void setChartScroll(HWND hwnd, ChartWindowData* data, int position) {
    if (!data) return;
    const int next = std::clamp(position, 0, maxChartScroll(hwnd, data));
    if (next == data->scrollY) return;
    data->scrollY = next;
    updateChartScrollBar(hwnd, data);
    RECT rc = chartAreaRect(hwnd);
    InvalidateRect(hwnd, &rc, FALSE);
}

void drawChartStack(HDC dc, const RECT& rect, ChartWindowData* data) {
    if (!data) return;
    HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(dc, &rect, bg);
    DeleteObject(bg);
    data->hitPoints.clear();

    const int singleHeight = chartSingleHeightForRect(rect);
    int top = rect.top - data->scrollY;
    for (const auto& series : data->series) {
        RECT chartRect{rect.left, top, rect.right, top + singleHeight};
        RECT intersection{};
        if (IntersectRect(&intersection, &rect, &chartRect)) {
            std::vector<qc::ChartHitPoint> localHits;
            qc::draw_levey_jennings_chart(dc, chartRect, series, data->selectedIndex, &localHits);
            data->hitPoints.insert(data->hitPoints.end(), localHits.begin(), localHits.end());
        }
        top += singleHeight + chartGap();
        if (top > rect.bottom + singleHeight) break;
    }
}

LRESULT CALLBACK chartWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* data = reinterpret_cast<ChartWindowData*>(GetPropW(hwnd, PROP_CHART));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            data = reinterpret_cast<ChartWindowData*>(cs->lpCreateParams);
            SetPropW(hwnd, PROP_CHART, reinterpret_cast<HANDLE>(data));
            data->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                         WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SINGLESEL,
                                         0, 0, 0, 0, hwnd, win32_control_id(IDC_CHART_LIST), GetModuleHandleW(nullptr), nullptr);
            setupList(data->list, {{L"时间", 140}, {L"样本号", 80}, {L"报告号", 90}, {L"水平", 60},
                                   {L"结果", 90}, {L"单位", 70}, {L"Z值", 70}, {L"状态", 70},
                                   {L"规则", 90}});
            populateChartList(data);
            data->tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (data->tooltip) {
                TOOLINFOW ti{};
                ti.cbSize = sizeof(ti);
                ti.hwnd = hwnd;
                ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
                ti.uId = reinterpret_cast<UINT_PTR>(hwnd);
                ti.lpszText = const_cast<wchar_t*>(L"");
                SendMessageW(data->tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
                SendMessageW(data->tooltip, TTM_SETMAXTIPWIDTH, 0, 360);
            }
            updateChartScrollBar(hwnd, data);
            return 0;
        }
        case WM_SIZE: {
            if (data && data->list) {
                RECT listRc = chartListRect(hwnd);
                MoveWindow(data->list, listRc.left + 8, listRc.top, listRc.right - listRc.left - 16,
                           listRc.bottom - listRc.top - 8, TRUE);
                RedrawWindow(data->list, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
                updateChartScrollBar(hwnd, data);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_VSCROLL: {
            if (!data) break;
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            int next = data->scrollY;
            switch (LOWORD(wp)) {
                case SB_LINEUP:
                    next -= 40;
                    break;
                case SB_LINEDOWN:
                    next += 40;
                    break;
                case SB_PAGEUP:
                    next -= static_cast<int>(si.nPage);
                    break;
                case SB_PAGEDOWN:
                    next += static_cast<int>(si.nPage);
                    break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION:
                    next = si.nTrackPos;
                    break;
                default:
                    break;
            }
            setChartScroll(hwnd, data, next);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (!data) break;
            const int delta = GET_WHEEL_DELTA_WPARAM(wp);
            setChartScroll(hwnd, data, data->scrollY - delta * 80 / WHEEL_DELTA);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            RECT rc = chartAreaRect(hwnd);
            HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
            RECT topBg{client.left, client.top, client.right, rc.top};
            RECT bottomBg{client.left, rc.bottom, client.right, client.bottom};
            if (topBg.bottom > topBg.top) FillRect(dc, &topBg, bg);
            if (bottomBg.bottom > bottomBg.top) FillRect(dc, &bottomBg, bg);
            DeleteObject(bg);
            if (data && rc.right > rc.left && rc.bottom > rc.top) {
                HDC memDc = CreateCompatibleDC(dc);
                HBITMAP bitmap = CreateCompatibleBitmap(dc, rc.right - rc.left, rc.bottom - rc.top);
                HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);
                RECT localRc{0, 0, rc.right - rc.left, rc.bottom - rc.top};
                drawChartStack(memDc, localRc, data);
                for (auto& hit : data->hitPoints) {
                    OffsetRect(&hit.bounds, rc.left, rc.top);
                }
                BitBlt(dc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, memDc, 0, 0, SRCCOPY);
                SelectObject(memDc, oldBitmap);
                DeleteObject(bitmap);
                DeleteDC(memDc);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!data) break;
            const int x = static_cast<short>(LOWORD(lp));
            const int y = static_cast<short>(HIWORD(lp));
            updateChartTooltip(hwnd, data, hitChartPoint(data, x, y));
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (!data) break;
            const int x = static_cast<short>(LOWORD(lp));
            const int y = static_cast<short>(HIWORD(lp));
            const int index = hitChartPoint(data, x, y);
            if (index >= 0) selectChartPoint(hwnd, data, index);
            return 0;
        }
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (data && nm && nm->idFrom == IDC_CHART_LIST && nm->code == LVN_ITEMCHANGED && !data->suppressListNotify) {
                auto* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED)) {
                    data->selectedIndex = lv->iItem;
                    RECT rc = chartAreaRect(hwnd);
                    InvalidateRect(hwnd, &rc, FALSE);
                }
                return 0;
            }
            break;
        }
        case WM_DESTROY:
            RemovePropW(hwnd, PROP_CHART);
            delete data;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ensureChartWindowClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = chartWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = CHART_WND_CLASS;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    registered = true;
}

void buildGroups(State* st, bool applyStatusFilter = true) {
    st->groupsRows.clear();
    std::map<std::string, size_t> index;
    for (const auto& row : st->rows) {
        if (applyStatusFilter && !rowVisibleByStatus(st, row)) continue;
        const std::string key = rowGroupKey(row);
        auto found = index.find(key);
        if (found == index.end()) {
            index[key] = st->groupsRows.size();
            GroupRow group;
            group.key = key;
            group.mach_code = row.mach_code;
            group.mach_name = row.mach_name;
            group.sample_no = row.sample_no;
            group.item_code = row.item_code;
            group.item_name = row.item_name;
            group.item_eng = row.item_eng;
            group.level = row.level;
            group.qc_name = row.qc_name;
            st->groupsRows.push_back(std::move(group));
            found = index.find(key);
        }
        auto& group = st->groupsRows[found->second];
        ++group.count;
        const std::string displayTime = pointDisplayTime(row);
        if (displayTime >= group.latest_time) {
            group.latest_time = displayTime;
            group.latest_result = row.result_text;
        }
        if (row.has_numeric_value) {
            ++group.numeric_count;
            group.sum += row.result_value;
            group.sum_square += row.result_value * row.result_value;
        }
        double configuredMean = 0.0;
        if (!group.has_configured_mean && configuredTargetMean(row, configuredMean)) {
            group.qc_mean = configuredMean;
            group.has_configured_mean = true;
            group.has_configured_stats = group.has_configured_mean && group.has_configured_sd;
        }
        double configuredSd = 0.0;
        if (!group.has_configured_sd && configuredTargetSd(row, configuredSd)) {
            group.qc_sd = configuredSd;
            group.has_configured_sd = true;
            group.has_configured_stats = group.has_configured_mean && group.has_configured_sd;
        }
        if (search::trim(group.lot_no).empty() && !search::trim(row.lot_no).empty()) {
            group.lot_no = row.lot_no;
        }
        if (!row.qc_status.empty()) {
            ++group.evaluated_count;
        }
        if (row.qc_status == "out_of_control") {
            ++group.out_of_control_count;
        } else if (row.qc_status == "warning") {
            ++group.warning_count;
        } else if (row.qc_status == "in_control") {
            ++group.in_control_count;
        }
    }
}

int levelSortRank(const std::string& level) {
    const std::string trimmed = search::trim(level);
    if (trimmed == "L1" || trimmed == "1") return 1;
    if (trimmed == "L2" || trimmed == "2") return 2;
    if (trimmed == "L3" || trimmed == "3") return 3;
    return 100;
}

void buildCards(State* st) {
    st->cardsRows.clear();
    std::map<std::string, size_t> cardIndex;
    std::map<std::string, std::pair<size_t, size_t>> levelIndex;
    for (int i = 0; i < static_cast<int>(st->groupsRows.size()); ++i) {
        const auto& group = st->groupsRows[static_cast<size_t>(i)];
        const std::string key = cardKey(group);
        auto found = cardIndex.find(key);
        if (found == cardIndex.end()) {
            cardIndex[key] = st->cardsRows.size();
            CardRow card;
            card.key = key;
            card.mach_code = group.mach_code;
            card.item_code = group.item_code;
            card.item_name = group.item_name;
            card.item_eng = group.item_eng;
            card.qc_name = group.qc_name;
            card.representative_group = i;
            st->cardsRows.push_back(std::move(card));
            found = cardIndex.find(key);
        }

        auto& card = st->cardsRows[found->second];
        CardLevelStatus level;
        level.level = group.level;
        level.sample_no = group.sample_no;
        level.status = "无数据";
        level.group_index = i;
        card.levels.push_back(std::move(level));
        levelIndex[group.key] = {found->second, card.levels.size() - 1};
    }

    for (const auto& row : st->rows) {
        if (!rowVisibleByStatus(st, row)) continue;
        if (!rowInSelectedQcDate(st, row)) continue;
        const auto cardFound = cardIndex.find(rowCardKey(row));
        if (cardFound == cardIndex.end()) continue;
        auto& card = st->cardsRows[cardFound->second];

        ++card.count;
        if (!row.qc_status.empty()) ++card.evaluated_count;
        if (row.qc_status == "out_of_control") {
            ++card.out_of_control_count;
        } else if (row.qc_status == "warning") {
            ++card.warning_count;
        }
        const std::string displayTime = pointDisplayTime(row);
        if (displayTime >= card.latest_time) {
            card.latest_time = displayTime;
            card.latest_result = row.result_text;
            card.tester_name = row.tester_name;
        }

        const auto levelFound = levelIndex.find(rowGroupKey(row));
        if (levelFound != levelIndex.end()) {
            auto& level = st->cardsRows[levelFound->second.first].levels[levelFound->second.second];
            ++level.count;
            if (!row.qc_status.empty()) ++level.evaluated_count;
            if (row.qc_status == "out_of_control") {
                ++level.out_of_control_count;
            } else if (row.qc_status == "warning") {
                ++level.warning_count;
            }
        }
        if (row.qc_status == "out_of_control") {
            const auto levelFoundForGroup = levelIndex.find(rowGroupKey(row));
            if (levelFoundForGroup != levelIndex.end()) {
                card.representative_group = st->cardsRows[levelFoundForGroup->second.first]
                                                .levels[levelFoundForGroup->second.second]
                                                .group_index;
            }
        }
    }

    for (auto& card : st->cardsRows) {
        for (auto& level : card.levels) {
            level.status = levelStatusText(level);
        }
        if (card.representative_group >= 0 &&
            card.representative_group < static_cast<int>(st->groupsRows.size())) {
            const auto& group = st->groupsRows[static_cast<size_t>(card.representative_group)];
            card.lot_no = group.lot_no;
            card.target_mean = groupQcMean(group);
            card.has_target_mean = group.has_configured_mean;
            card.target_uses_mean = !group.has_configured_mean && groupHasQcStats(group);
        }
        std::sort(card.levels.begin(), card.levels.end(), [](const CardLevelStatus& a, const CardLevelStatus& b) {
            const int ar = levelSortRank(a.level);
            const int br = levelSortRank(b.level);
            if (ar != br) return ar < br;
            if (a.level != b.level) return a.level < b.level;
            return a.sample_no < b.sample_no;
        });
    }
}

void populateGroups(State* st) {
    SendMessageW(st->groups, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(st->groups);
    for (int i = 0; i < static_cast<int>(st->groupsRows.size()); ++i) {
        const auto& row = st->groupsRows[static_cast<size_t>(i)];
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        const auto title = search::utf8_to_wide(row.item_name.empty() ? row.item_code : row.item_name);
        item.pszText = const_cast<wchar_t*>(title.c_str());
        ListView_InsertItem(st->groups, &item);
        setCell(st->groups, i, 1, row.level);
        setCell(st->groups, i, 2, row.sample_no);
        setCell(st->groups, i, 3, std::to_string(row.count));
        setCell(st->groups, i, 4, groupHasQcStats(row) ? formatNumber(groupQcMean(row)) : "");
        setCell(st->groups, i, 5, groupHasQcStats(row) ? formatNumber(groupQcSd(row)) : "");
        const double mean = groupQcMean(row);
        const double sd = groupQcSd(row);
        setCell(st->groups, i, 6, groupHasQcStats(row) && mean != 0.0 ? formatNumber(sd / mean * 100.0) : "");
        setCell(st->groups, i, 7, groupStatsSourceText(row));
        setCell(st->groups, i, 8, groupStatusText(row));
        setCell(st->groups, i, 9, row.out_of_control_count > 0 ? std::to_string(row.out_of_control_count) : "");
        setCell(st->groups, i, 10, row.warning_count > 0 ? std::to_string(row.warning_count) : "");
        setCell(st->groups, i, 11, row.latest_time);
    }
    SendMessageW(st->groups, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(st->groups, nullptr, TRUE);
}

void updateSummary(State* st) {
    if (!st || !st->summary) return;
    int outOfControlCards = 0;
    int warningCards = 0;
    int inControlCards = 0;
    int noDataCards = 0;
    int undecidedCards = 0;
    for (const auto& card : st->cardsRows) {
        const std::string status = cardStatusText(card);
        if (status == "失控") {
            ++outOfControlCards;
        } else if (status == "警告") {
            ++warningCards;
        } else if (status == "在控") {
            ++inControlCards;
        } else if (status == "无数据") {
            ++noDataCards;
        } else {
            ++undecidedCards;
        }
    }
    const std::wstring text = L"今日质控概览    项目 " + std::to_wstring(st->cardsRows.size()) +
                              L"    失控 " + std::to_wstring(outOfControlCards) +
                              L"    警告 " + std::to_wstring(warningCards) +
                              L"    在控 " + std::to_wstring(inControlCards) +
                              L"    无数据 " + std::to_wstring(noDataCards) +
                              L"    未判定 " + std::to_wstring(undecidedCards);
    SetWindowTextW(st->summary, text.c_str());
}

int cardGap(HWND hwnd) {
    return S(hwnd, 12);
}

int cardMinWidth(HWND hwnd) {
    return S(hwnd, 286);
}

int cardPreferredWidth(HWND hwnd) {
    return S(hwnd, 300);
}

int cardHeight(HWND hwnd) {
    return S(hwnd, 158);
}

int cardColumns(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int gap = cardGap(hwnd);
    const int width = std::max(1, static_cast<int>(rc.right - rc.left));
    return std::max(1, (width - gap) / (cardPreferredWidth(hwnd) + gap));
}

int cardActualWidth(HWND hwnd, int columns) {
    (void)columns;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int gap = cardGap(hwnd);
    const int width = std::max(1, static_cast<int>(rc.right - rc.left));
    const int available = std::max(1, width - gap * 2);
    if (available < cardMinWidth(hwnd)) return available;
    return std::min(cardPreferredWidth(hwnd), available);
}

int cardContentHeight(HWND hwnd, const State* st) {
    if (!st || st->cardsRows.empty()) return 0;
    const int columns = cardColumns(hwnd);
    const int rows = (static_cast<int>(st->cardsRows.size()) + columns - 1) / columns;
    return cardGap(hwnd) + rows * (cardHeight(hwnd) + cardGap(hwnd));
}

int maxCardScroll(HWND hwnd, const State* st) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    return std::max(0, cardContentHeight(hwnd, st) - std::max(0, static_cast<int>(rc.bottom - rc.top)));
}

void updateCardScrollBar(HWND hwnd, State* st) {
    if (!st) return;
    st->cardScrollY = std::clamp(st->cardScrollY, 0, maxCardScroll(hwnd, st));
    RECT rc{};
    GetClientRect(hwnd, &rc);
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = std::max(0, cardContentHeight(hwnd, st) - 1);
    si.nPage = static_cast<UINT>(std::max(1, static_cast<int>(rc.bottom - rc.top)));
    si.nPos = st->cardScrollY;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

RECT cardRectForIndex(HWND hwnd, const State* st, int index) {
    (void)st;
    const int gap = cardGap(hwnd);
    const int columns = cardColumns(hwnd);
    const int w = cardActualWidth(hwnd, columns);
    const int h = cardHeight(hwnd);
    const int row = index / columns;
    const int col = index % columns;
    const int x = gap + col * (w + gap);
    const int y = gap + row * (h + gap);
    return RECT{x, y, x + w, y + h};
}

COLORREF statusColor(const std::string& status) {
    if (status == "失控") return RGB(235, 64, 55);
    if (status == "警告") return RGB(255, 224, 82);
    if (status == "在控") return RGB(54, 202, 65);
    return RGB(205, 211, 218);
}

COLORREF cardStatusColor(const CardRow& card) {
    return statusColor(cardStatusText(card));
}

COLORREF cardTextColor(const CardRow& card) {
    const std::string status = cardStatusText(card);
    return status == "警告" || status == "未判定" || status == "无数据" ? RGB(35, 45, 58) : RGB(255, 255, 255);
}

void fillSolid(HDC dc, const RECT& rc, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rc, brush);
    DeleteObject(brush);
}

void drawCardText(HDC dc, RECT rc, const std::wstring& text, UINT format, COLORREF color) {
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &rc, format);
}

void drawStatusDot(HDC dc, int cx, int cy, int radius, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(235, 240, 235));
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Ellipse(dc, cx - radius, cy - radius, cx + radius + 1, cy + radius + 1);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void drawChartGlyph(HDC dc, const RECT& rc, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    const int left = rc.left + 3;
    const int right = rc.right - 3;
    const int top = rc.top + 4;
    const int bottom = rc.bottom - 4;
    MoveToEx(dc, left, bottom, nullptr);
    LineTo(dc, left + (right - left) / 3, top + (bottom - top) / 2);
    LineTo(dc, left + (right - left) * 2 / 3, bottom - (bottom - top) / 3);
    LineTo(dc, right, top);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawQualityCard(HDC dc, HWND hwnd, State* st, int index, RECT rc) {
    const auto& card = st->cardsRows[static_cast<size_t>(index)];
    const bool selected = index == st->selectedCard;
    fillSolid(dc, rc, selected ? RGB(18, 96, 168) : RGB(170, 185, 200));
    RECT inner{rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 2};
    fillSolid(dc, inner, RGB(255, 255, 255));

    RECT header{inner.left, inner.top, inner.right, inner.top + S(hwnd, 42)};
    fillSolid(dc, header, cardStatusColor(card));

    const COLORREF headerTextColor = cardTextColor(card);
    const int pad = S(hwnd, 8);
    RECT glyph{header.right - S(hwnd, 30), header.top + S(hwnd, 10), header.right - S(hwnd, 10), header.bottom - S(hwnd, 10)};
    drawChartGlyph(dc, glyph, headerTextColor);
    RECT title{header.left + pad, header.top + S(hwnd, 5), glyph.left - S(hwnd, 6), header.bottom - S(hwnd, 4)};
    std::wstring titleText = search::utf8_to_wide(cardTitleText(card));
    drawCardText(dc, title, titleText, DT_LEFT | DT_TOP | DT_END_ELLIPSIS | DT_WORDBREAK, headerTextColor);

    const COLORREF bodyText = RGB(42, 52, 60);
    RECT lot{inner.left + pad, inner.top + S(hwnd, 50), inner.right - S(hwnd, 76), inner.top + S(hwnd, 72)};
    drawCardText(dc, lot, L"批号: " + search::utf8_to_wide(card.lot_no),
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, bodyText);

    RECT result{inner.left + pad, inner.top + S(hwnd, 74), inner.right - S(hwnd, 76), inner.top + S(hwnd, 96)};
    drawCardText(dc, result, L"靶值: " + search::utf8_to_wide(cardTargetText(card)),
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, bodyText);

    RECT tester{inner.left + pad, inner.top + S(hwnd, 98), inner.right - S(hwnd, 76), inner.top + S(hwnd, 120)};
    drawCardText(dc, tester, L"操作人: " + search::utf8_to_wide(card.tester_name.empty() ? "-" : card.tester_name),
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, bodyText);

    RECT status{inner.left + pad, inner.bottom - S(hwnd, 24), inner.right - S(hwnd, 76), inner.bottom - S(hwnd, 4)};
    drawCardText(dc, status, L"状态: " + search::utf8_to_wide(cardStatusText(card)),
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, bodyText);

    const int dotX = inner.right - S(hwnd, 16);
    const int dotRadius = S(hwnd, 6);
    const int startY = inner.top + S(hwnd, 60);
    const int rowH = S(hwnd, 24);
    for (int i = 0; i < static_cast<int>(card.levels.size()) && i < 4; ++i) {
        const auto& level = card.levels[static_cast<size_t>(i)];
        const int y = startY + i * rowH;
        RECT levelText{inner.right - S(hwnd, 62), y - S(hwnd, 10), inner.right - S(hwnd, 22), y + S(hwnd, 10)};
        drawCardText(dc, levelText, search::utf8_to_wide(level.level.empty() ? "-" : level.level),
                     DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, bodyText);
        drawStatusDot(dc, dotX, y, dotRadius, statusColor(level.status));
    }
}

int hitQualityCard(HWND hwnd, State* st, int x, int y) {
    if (!st) return -1;
    POINT pt{x, y + st->cardScrollY};
    for (int i = 0; i < static_cast<int>(st->cardsRows.size()); ++i) {
        RECT rc = cardRectForIndex(hwnd, st, i);
        if (PtInRect(&rc, pt)) return i;
    }
    return -1;
}

int hitQualityCardHeader(HWND hwnd, State* st, int x, int y) {
    if (!st) return -1;
    POINT pt{x, y + st->cardScrollY};
    for (int i = 0; i < static_cast<int>(st->cardsRows.size()); ++i) {
        RECT rc = cardRectForIndex(hwnd, st, i);
        RECT header{rc.left + 2, rc.top + 2, rc.right - 2, rc.top + S(hwnd, 44)};
        if (PtInRect(&header, pt)) return i;
    }
    return -1;
}

bool rowInSelectedGroup(const State* st, const qc::Result& row) {
    if (st && st->selectedCard >= 0 && st->selectedCard < static_cast<int>(st->cardsRows.size())) {
        return rowVisibleByStatus(st, row) && rowCardKey(row) == st->cardsRows[static_cast<size_t>(st->selectedCard)].key;
    }
    if (!st || st->selectedGroup < 0 || st->selectedGroup >= static_cast<int>(st->groupsRows.size())) return true;
    const auto& group = st->groupsRows[static_cast<size_t>(st->selectedGroup)];
    return rowVisibleByStatus(st, row) && rowGroupKey(row) == group.key;
}

void populateCards(State* st) {
    if (!st || !st->cards) return;
    updateCardScrollBar(st->cards, st);
    InvalidateRect(st->cards, nullptr, FALSE);
}

void populateDetails(State* st) {
    SendMessageW(st->details, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(st->details);
    int out = 0;
    for (const auto& row : st->rows) {
        if (!rowVisibleByStatus(st, row)) continue;
        if (!rowInSelectedGroup(st, row)) continue;
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = out;
        const auto timeW = search::utf8_to_wide(pointDisplayTime(row));
        item.pszText = const_cast<wchar_t*>(timeW.c_str());
        ListView_InsertItem(st->details, &item);
        setCell(st->details, out, 1, row.sample_no);
        setCell(st->details, out, 2, row.source_rep_no);
        setCell(st->details, out, 3, row.mach_name.empty() ? row.mach_code : row.mach_name);
        setCell(st->details, out, 4, row.tester_name);
        setCell(st->details, out, 5, row.item_name);
        setCell(st->details, out, 6, row.level);
        setCell(st->details, out, 7, row.lot_no.empty() ? "-" : row.lot_no);
        setCell(st->details, out, 8, row.result_text);
        setCell(st->details, out, 9, row.unit);
        setCell(st->details, out, 10, qcStatusText(row.qc_status));
        setCell(st->details, out, 11, row.qc_rules);
        setCell(st->details, out, 12, row.has_qc_z ? formatNumber(row.qc_z) : "");
        setCell(st->details, out, 13, row.data_source.empty() ? "LIS导入" : row.data_source);
        ++out;
    }
    SendMessageW(st->details, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(st->details, nullptr, TRUE);
}

void updateSidePanel(State* st) {
    if (!st) return;
    if (st->sideList) {
        SendMessageW(st->sideList, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(st->sideList);
        for (int i = 0; i < static_cast<int>(st->cardsRows.size()); ++i) {
            const auto& card = st->cardsRows[static_cast<size_t>(i)];
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = i;
            const auto title = search::utf8_to_wide(cardTitleText(card));
            item.pszText = const_cast<wchar_t*>(title.c_str());
            ListView_InsertItem(st->sideList, &item);
            setCell(st->sideList, i, 1, cardStatusText(card));
        }
        SendMessageW(st->sideList, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(st->sideList, nullptr, TRUE);
        if (st->selectedCard >= 0 && st->selectedCard < static_cast<int>(st->cardsRows.size())) {
            st->suppressSelectionNotify = true;
            ListView_SetItemState(st->sideList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_SetItemState(st->sideList, st->selectedCard, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(st->sideList, st->selectedCard, FALSE);
            st->suppressSelectionNotify = false;
        }
    }

    const bool hasSelection = st->selectedCard >= 0 && st->selectedCard < static_cast<int>(st->cardsRows.size());
    if (st->sideTitle) SetWindowTextW(st->sideTitle, L"筛选条件");
    if (st->sideChartButton) EnableWindow(st->sideChartButton, hasSelection && !st->busy);
    if (st->sideDetailsButton) EnableWindow(st->sideDetailsButton, hasSelection && !st->busy);
    if (st->sideExportButton) EnableWindow(st->sideExportButton, hasSelection && !st->busy && hasVisibleRows(st));
}

std::vector<const qc::Result*> visibleDetails(State* st) {
    std::vector<const qc::Result*> rows;
    if (!st) return rows;
    for (const auto& row : st->rows) {
        if (!rowVisibleByStatus(st, row)) continue;
        if (rowInSelectedGroup(st, row)) rows.push_back(&row);
    }
    return rows;
}

void refreshVisibleResults(State* st) {
    if (!st) return;
    buildGroups(st);
    buildCards(st);
    if (st->selectedGroup >= static_cast<int>(st->groupsRows.size())) st->selectedGroup = -1;
    if (st->selectedCard >= static_cast<int>(st->cardsRows.size())) st->selectedCard = -1;
    populateGroups(st);
    updateSummary(st);
    populateCards(st);
    populateDetails(st);
    updateSidePanel(st);
    updateExportButton(st);
    updateChartButton(st);
}

void clearCurrentResults(State* st) {
    if (!st) return;
    st->rows.clear();
    st->groupsRows.clear();
    st->cardsRows.clear();
    st->selectedGroup = -1;
    st->selectedCard = -1;
    refreshVisibleResults(st);
}

const GroupRow* selectedOrFirstGroup(State* st) {
    if (!st || st->groupsRows.empty()) return nullptr;
    if (st->selectedCard >= 0 && st->selectedCard < static_cast<int>(st->cardsRows.size())) {
        const int group = st->cardsRows[static_cast<size_t>(st->selectedCard)].representative_group;
        if (group >= 0 && group < static_cast<int>(st->groupsRows.size())) {
            return &st->groupsRows[static_cast<size_t>(group)];
        }
    }
    if (st->selectedGroup >= 0 && st->selectedGroup < static_cast<int>(st->groupsRows.size())) {
        return &st->groupsRows[static_cast<size_t>(st->selectedGroup)];
    }
    return &st->groupsRows.front();
}

void selectGroup(State* st, int index) {
    if (!st || index < 0 || index >= static_cast<int>(st->groupsRows.size())) return;
    st->selectedGroup = index;
    st->selectedCard = -1;
    st->suppressSelectionNotify = true;
    if (st->groups) {
        ListView_SetItemState(st->groups, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemState(st->groups, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(st->groups, index, FALSE);
    }
    if (st->cards) {
        updateCardScrollBar(st->cards, st);
        InvalidateRect(st->cards, nullptr, FALSE);
    }
    st->suppressSelectionNotify = false;
    populateDetails(st);
    updateSidePanel(st);
    updateExportButton(st);
    updateChartButton(st);
}

void selectCard(State* st, int index) {
    if (!st || index < 0 || index >= static_cast<int>(st->cardsRows.size())) return;
    st->selectedCard = index;
    const int groupIndex = st->cardsRows[static_cast<size_t>(index)].representative_group;
    if (groupIndex >= 0 && groupIndex < static_cast<int>(st->groupsRows.size())) {
        st->selectedGroup = groupIndex;
    }
    st->suppressSelectionNotify = true;
    if (st->groups && st->selectedGroup >= 0) {
        ListView_SetItemState(st->groups, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemState(st->groups, st->selectedGroup, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(st->groups, st->selectedGroup, FALSE);
    }
    if (st->cards) {
        RECT client{};
        GetClientRect(st->cards, &client);
        RECT card = cardRectForIndex(st->cards, st, index);
        if (card.top < st->cardScrollY) {
            st->cardScrollY = card.top;
        } else if (card.bottom > st->cardScrollY + (client.bottom - client.top)) {
            st->cardScrollY = card.bottom - (client.bottom - client.top);
        }
        updateCardScrollBar(st->cards, st);
        InvalidateRect(st->cards, nullptr, FALSE);
    }
    st->suppressSelectionNotify = false;
    populateDetails(st);
    updateSidePanel(st);
    updateExportButton(st);
    updateChartButton(st);
}

bool rowInGroup(const GroupRow& group, const qc::Result& row) {
    return rowGroupKey(row) == group.key;
}

bool groupMatchesChartBase(const GroupRow& base, const GroupRow& candidate) {
    if (base.mach_code != candidate.mach_code) return false;
    const std::string baseName = search::trim(base.item_name);
    const std::string candidateName = search::trim(candidate.item_name);
    if (!baseName.empty() && !candidateName.empty()) return baseName == candidateName;
    return search::trim(base.item_code) == search::trim(candidate.item_code);
}

qc::ChartSeries buildChartSeries(State* st, const GroupRow& group, size_t& pointIndex) {
    qc::ChartSeries series;
    const std::string item = group.item_name.empty() ? group.item_code : group.item_name;
    series.title = item.empty() ? "Levey-Jennings 质控图" : item;
    if (!group.level.empty()) series.title += " " + group.level;
    if (!group.sample_no.empty()) series.title += " / 样本号 " + group.sample_no;
    series.subtitle = group.mach_name.empty() ? group.mach_code : group.mach_name;
    if (!group.qc_name.empty()) series.subtitle += " / " + group.qc_name;
    series.mean = groupQcMean(group);
    series.sd = groupQcSd(group);
    series.has_mean = groupHasQcStats(group);
    series.has_sd = groupHasQcStats(group) && series.sd > 0.0;
    const std::string source = groupStatsSourceText(group);
    if (!source.empty() && series.has_mean) {
        series.subtitle += " / 基准 " + source + " " + formatNumber(series.mean);
        if (series.has_sd) series.subtitle += " / SD " + formatNumber(series.sd);
    }
    for (const auto& row : st->rows) {
        if (!rowVisibleByStatus(st, row)) continue;
        if (!rowInGroup(group, row)) continue;
        qc::ChartPoint point;
        point.time = pointDisplayTime(row);
        point.sample_no = row.sample_no;
        point.rep_no = row.source_rep_no;
        point.item_name = row.item_name;
        point.level = row.level;
        point.result_text = row.result_text;
        point.unit = row.unit;
        point.qc_status = row.qc_status;
        point.qc_rules = row.qc_rules;
        point.value = row.result_value;
        point.z = row.qc_z;
        point.has_value = row.has_numeric_value;
        point.has_z = row.has_qc_z;
        if (series.unit.empty()) series.unit = row.unit;
        series.points.push_back(std::move(point));
    }
    std::sort(series.points.begin(), series.points.end(), [](const qc::ChartPoint& a, const qc::ChartPoint& b) {
        if (a.time != b.time) return a.time < b.time;
        return a.rep_no < b.rep_no;
    });
    for (auto& point : series.points) {
        point.point_index = pointIndex++;
    }
    return series;
}

std::vector<qc::ChartSeries> buildChartSeriesList(State* st, const GroupRow& base) {
    std::vector<const GroupRow*> groups;
    for (const auto& group : st->groupsRows) {
        if (groupMatchesChartBase(base, group)) groups.push_back(&group);
    }
    std::sort(groups.begin(), groups.end(), [](const GroupRow* a, const GroupRow* b) {
        if (a->level != b->level) return a->level < b->level;
        if (a->sample_no != b->sample_no) return a->sample_no < b->sample_no;
        return a->qc_name < b->qc_name;
    });

    std::vector<qc::ChartSeries> series;
    size_t pointIndex = 0;
    for (const auto* group : groups) {
        auto item = buildChartSeries(st, *group, pointIndex);
        if (!item.points.empty()) series.push_back(std::move(item));
    }
    return series;
}

RECT centeredChartWindowRect(HWND owner) {
    HMONITOR monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(monitor, &mi);
    const RECT work = mi.rcWork;
    const int workW = static_cast<int>(work.right - work.left);
    const int workH = static_cast<int>(work.bottom - work.top);
    const int maxW = std::max(640, workW - 40);
    const int maxH = std::max(520, workH - 40);
    const int minW = std::min(1280, maxW);
    const int minH = std::min(820, maxH);
    const int width = std::clamp(static_cast<int>(workW * 0.86), minW, maxW);
    const int height = std::clamp(static_cast<int>(workH * 0.84), minH, maxH);
    const int left = work.left + (workW - width) / 2;
    const int top = work.top + (workH - height) / 2;
    return RECT{left, top, left + width, top + height};
}

void openLeveyJenningsChart(HWND hwnd, State* st) {
    const GroupRow* group = selectedOrFirstGroup(st);
    if (!group) {
        MessageBoxW(hwnd, L"请先查询到质控结果。", WINDOW_TITLE, MB_ICONINFORMATION);
        return;
    }
    auto* data = new ChartWindowData;
    data->series = buildChartSeriesList(st, *group);
    ensureChartWindowClass();
    const RECT popup = centeredChartWindowRect(hwnd);
    HWND chart = CreateWindowExW(WS_EX_APPWINDOW, CHART_WND_CLASS, L"Levey-Jennings 质控图",
                                 WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL,
                                 popup.left, popup.top, popup.right - popup.left, popup.bottom - popup.top,
                                 hwnd, nullptr, GetModuleHandleW(nullptr), data);
    if (!chart) {
        delete data;
        MessageBoxW(hwnd, L"L-J 图窗口创建失败。", WINDOW_TITLE, MB_ICONERROR);
    }
}

LRESULT CALLBACK cardWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<State*>(GetPropW(GetParent(hwnd), PROP_STATE));
    switch (msg) {
        case WM_SIZE:
            updateCardScrollBar(hwnd, st);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_VSCROLL: {
            if (!st) break;
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            int next = st->cardScrollY;
            switch (LOWORD(wp)) {
                case SB_LINEUP:
                    next -= S(hwnd, 32);
                    break;
                case SB_LINEDOWN:
                    next += S(hwnd, 32);
                    break;
                case SB_PAGEUP:
                    next -= static_cast<int>(si.nPage);
                    break;
                case SB_PAGEDOWN:
                    next += static_cast<int>(si.nPage);
                    break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION:
                    next = si.nTrackPos;
                    break;
                default:
                    break;
            }
            st->cardScrollY = std::clamp(next, 0, maxCardScroll(hwnd, st));
            updateCardScrollBar(hwnd, st);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEWHEEL:
            if (st) {
                const int delta = GET_WHEEL_DELTA_WPARAM(wp);
                st->cardScrollY = std::clamp(st->cardScrollY - delta * S(hwnd, 72) / WHEEL_DELTA, 0, maxCardScroll(hwnd, st));
                updateCardScrollBar(hwnd, st);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEMOVE:
            SetCursor(LoadCursorW(nullptr, hitQualityCardHeader(hwnd, st, static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))) >= 0
                                               ? IDC_HAND
                                               : IDC_ARROW));
            return 0;
        case WM_LBUTTONDOWN:
            if (st) {
                SetFocus(hwnd);
                const int headerIndex = hitQualityCardHeader(hwnd, st, static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp)));
                if (headerIndex >= 0) {
                    selectCard(st, headerIndex);
                    openLeveyJenningsChart(GetParent(hwnd), st);
                } else {
                    const int index = hitQualityCard(hwnd, st, static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp)));
                    if (index >= 0) selectCard(st, index);
                }
            }
            return 0;
        case WM_SETCURSOR: {
            POINT pt{};
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            SetCursor(LoadCursorW(nullptr, hitQualityCardHeader(hwnd, st, pt.x, pt.y) >= 0 ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        case WM_LBUTTONDBLCLK:
            if (st) {
                const int headerIndex = hitQualityCardHeader(hwnd, st, static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp)));
                if (headerIndex >= 0) {
                    selectCard(st, headerIndex);
                    openLeveyJenningsChart(GetParent(hwnd), st);
                }
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            HDC memDc = CreateCompatibleDC(dc);
            const int clientW = std::max(1, static_cast<int>(client.right - client.left));
            const int clientH = std::max(1, static_cast<int>(client.bottom - client.top));
            HBITMAP bitmap = CreateCompatibleBitmap(dc, clientW, clientH);
            HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);
            fillSolid(memDc, client, RGB(246, 249, 252));
            SetBkMode(memDc, TRANSPARENT);
            HGDIOBJ oldFont = nullptr;
            if (st && st->ctx.uiFont) oldFont = SelectObject(memDc, st->ctx.uiFont);
            if (st && st->cardsRows.empty()) {
                RECT empty{client.left, client.top, client.right, client.bottom};
                drawCardText(memDc, empty, L"暂无质控概览。请选择仪器并点击“查询”或“导入质控”。",
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(90, 100, 112));
            } else if (st) {
                for (int i = 0; i < static_cast<int>(st->cardsRows.size()); ++i) {
                    RECT rc = cardRectForIndex(hwnd, st, i);
                    OffsetRect(&rc, 0, -st->cardScrollY);
                    RECT visible{};
                    if (IntersectRect(&visible, &client, &rc)) drawQualityCard(memDc, hwnd, st, i, rc);
                }
            }
            BitBlt(dc, 0, 0, clientW, clientH, memDc, 0, 0, SRCCOPY);
            if (oldFont) SelectObject(memDc, oldFont);
            SelectObject(memDc, oldBitmap);
            DeleteObject(bitmap);
            DeleteDC(memDc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ensureCardWindowClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSW wc{};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = cardWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = CARD_WND_CLASS;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
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

std::wstring defaultExportFilename(State* st) {
    std::string filename = "quality_control";
    const std::string mach = sanitizeFilenamePart(search::wide_to_utf8(editText(st->machCode)));
    if (!mach.empty()) filename += "-" + mach;
    const std::string start = sanitizeFilenamePart(dateText(st->startDate));
    const std::string end = sanitizeFilenamePart(dateText(st->endDate));
    if (!start.empty()) filename += "-" + start;
    if (!end.empty() && end != start) filename += "-" + end;
    filename += ".xlsx";
    return search::utf8_to_wide(filename);
}

bool chooseExportPath(HWND owner, State* st, std::wstring& path) {
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

void exportVisibleDetailsXlsx(HWND hwnd, State* st) {
    if (!st || st->busy) return;
    const auto rows = visibleDetails(st);
    if (rows.empty()) {
        MessageBoxW(hwnd, L"当前没有可导出的质控明细。", WINDOW_TITLE, MB_ICONINFORMATION);
        return;
    }

    std::wstring path;
    if (!chooseExportPath(hwnd, st, path)) return;

    const std::vector<std::string> headers = {
        "时间", "样本号", "报告号", "仪器", "检验者", "项目", "水平",
        "批号", "结果", "单位", "状态", "规则", "Z值", "来源"};
    std::string error;
    if (!search::write_xlsx_file(
            path, "质控明细", headers, rows.size(),
            [&rows](size_t row_index, size_t column) -> std::string {
                const auto& row = *rows[row_index];
                switch (column) {
                    case 0: return pointDisplayTime(row);
                    case 1: return row.sample_no;
                    case 2: return row.source_rep_no;
                    case 3: return row.mach_name.empty() ? row.mach_code : row.mach_name;
                    case 4: return row.tester_name;
                    case 5: return row.item_name;
                    case 6: return row.level;
                    case 7: return row.lot_no;
                    case 8: return row.result_text;
                    case 9: return row.unit;
                    case 10: return qcStatusText(row.qc_status);
                    case 11: return row.qc_rules;
                    case 12: return row.has_qc_z ? formatNumber(row.qc_z) : "";
                    case 13: return row.data_source.empty() ? "LIS导入" : row.data_source;
                    default: return {};
                }
            }, error)) {
        MessageBoxW(hwnd, L"导出文件创建失败，请确认目标位置可写。", WINDOW_TITLE, MB_ICONERROR);
        return;
    }
    setStatus(st, L"已导出质控明细：" + path);
    MessageBoxW(hwnd, (L"已导出质控明细：\n" + path).c_str(), WINDOW_TITLE, MB_ICONINFORMATION);
}

void resizeLayout(HWND hwnd, State* st) {
    if (!st) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int pad = S(hwnd, 10);
    const int statusH = S(hwnd, 24);
    const int summaryH = S(hwnd, 28);
    const int clientW = std::max(0, static_cast<int>(rc.right - rc.left));
    const int clientH = std::max(0, static_cast<int>(rc.bottom - rc.top));
    const int bottomH = st->detailsExpanded ? std::clamp(clientH / 4, S(hwnd, 150), S(hwnd, 220)) : 0;
    const int sideW = std::clamp(clientW / 5, S(hwnd, 340), S(hwnd, 390));
    const int sideLeft = rc.right - pad - sideW;
    const int sideInnerW = sideW - S(hwnd, 16);
    const int sideX = sideLeft + S(hwnd, 8);
    int sy = pad;
    MoveWindow(st->sideTitle, sideLeft, sy, sideW, S(hwnd, 26), TRUE);
    sy += S(hwnd, 30);
    MoveWindow(st->startLabel, sideX, sy, S(hwnd, 64), S(hwnd, 24), TRUE);
    MoveWindow(st->startDate, sideX + S(hwnd, 70), sy, S(hwnd, 120), S(hwnd, 24), TRUE);
    MoveWindow(st->endLabel, sideX + S(hwnd, 196), sy, S(hwnd, 20), S(hwnd, 24), TRUE);
    MoveWindow(st->endDate, sideX + S(hwnd, 220), sy, S(hwnd, 120), S(hwnd, 24), TRUE);
    sy += S(hwnd, 34);
    MoveWindow(st->machLabel, sideX, sy, S(hwnd, 50), S(hwnd, 24), TRUE);
    MoveWindow(st->machCode, sideX + S(hwnd, 56), sy, sideInnerW - S(hwnd, 96), S(hwnd, 24), TRUE);
    MoveWindow(st->machPick, sideX + sideInnerW - S(hwnd, 34), sy - S(hwnd, 2), S(hwnd, 34), S(hwnd, 28), TRUE);
    ShowWindow(st->itemLabel, SW_HIDE);
    ShowWindow(st->itemCode, SW_HIDE);
    sy += S(hwnd, 34);
    MoveWindow(st->levelLabel, sideX, sy, S(hwnd, 38), S(hwnd, 24), TRUE);
    MoveWindow(st->level, sideX + S(hwnd, 42), sy, S(hwnd, 76), S(hwnd, 160), TRUE);
    MoveWindow(st->statusFilterLabel, sideX + S(hwnd, 132), sy, S(hwnd, 38), S(hwnd, 24), TRUE);
    MoveWindow(st->statusFilter, sideX + S(hwnd, 174), sy, sideInnerW - S(hwnd, 174), S(hwnd, 180), TRUE);
    sy += S(hwnd, 34);
    const int queryW = (sideInnerW - S(hwnd, 8)) / 2;
    MoveWindow(st->queryButton, sideX, sy - S(hwnd, 2), queryW, S(hwnd, 30), TRUE);
    MoveWindow(st->refreshButton, sideX + queryW + S(hwnd, 8), sy - S(hwnd, 2),
               sideInnerW - queryW - S(hwnd, 8), S(hwnd, 30), TRUE);
    ShowWindow(st->chartButton, SW_HIDE);
    ShowWindow(st->exportButton, SW_HIDE);
    ShowWindow(st->groups, SW_HIDE);
    MoveWindow(st->summary, pad, pad, sideLeft - pad * 2, summaryH, TRUE);
    MoveWindow(st->cards, pad, pad + summaryH + S(hwnd, 6),
               sideLeft - pad * 2, rc.bottom - pad * 3 - bottomH - statusH - summaryH - S(hwnd, 6), TRUE);
    updateCardScrollBar(st->cards, st);
    sy += S(hwnd, 44);
    MoveWindow(st->sideChartButton, sideX, sy, S(hwnd, 72), S(hwnd, 28), TRUE);
    MoveWindow(st->sideDetailsButton, sideX + S(hwnd, 80), sy, S(hwnd, 72), S(hwnd, 28), TRUE);
    MoveWindow(st->sideExportButton, sideX + S(hwnd, 160), sy, S(hwnd, 82), S(hwnd, 28), TRUE);
    sy += S(hwnd, 38);
    MoveWindow(st->sideList, sideLeft, sy, sideW, std::max(0, static_cast<int>(rc.bottom - sy - statusH - pad)), TRUE);
    if (st->detailsExpanded) {
        ShowWindow(st->details, SW_SHOW);
        MoveWindow(st->details, pad, rc.bottom - bottomH - statusH - pad,
                   sideLeft - pad * 2, bottomH, TRUE);
    } else {
        ShowWindow(st->details, SW_HIDE);
        MoveWindow(st->details, pad, rc.bottom - statusH - pad, sideLeft - pad * 2, 0, TRUE);
    }
    MoveWindow(st->status, pad, rc.bottom - statusH, rc.right - pad * 2, statusH, TRUE);
}

qc::Query buildQuery(State* st) {
    qc::Query query;
    query.start_date = dateText(st->startDate);
    query.end_date = dateText(st->endDate);
    query.mach_code = search::trim(st->selectedMachineCode);
    query.item_code.clear();
    query.level = comboText(st->level);
    if (query.level == "全部") query.level.clear();
    return query;
}

bool configMatchesItem(const qc::Config& cfg, const search::QualityControlLisRow& item) {
    const std::string configured = search::trim(cfg.item_code);
    return configured.empty() || configured == search::trim(item.item_code);
}

bool rowMatchesQuery(const qc::Query& request, const qc::Config& cfg, const search::QualityControlLisRow& item) {
    if (!configMatchesItem(cfg, item)) return false;
    const std::string requestedItem = search::trim(request.item_code);
    if (!requestedItem.empty() && requestedItem != search::trim(item.item_code)) return false;
    return true;
}

bool configMatchesCachedItem(const qc::Config& cfg, const qc::Result& item) {
    const std::string configured = search::trim(cfg.item_code);
    return configured.empty() || configured == search::trim(item.item_code);
}

bool rowMatchesCachedQuery(const qc::Query& request, const qc::Config& cfg, const qc::Result& item) {
    if (!configMatchesCachedItem(cfg, item)) return false;
    const std::string requestedItem = search::trim(request.item_code);
    if (!requestedItem.empty() && requestedItem != search::trim(item.item_code)) return false;
    return true;
}

std::string datePart(const std::string& text) {
    const std::string trimmed = search::trim(text);
    return trimmed.size() >= 10 ? trimmed.substr(0, 10) : trimmed;
}

bool configInEffectiveDate(const qc::Config& cfg, const std::string& effectiveTime) {
    const std::string date = datePart(effectiveTime);
    const std::string from = search::trim(cfg.lot_valid_from);
    const std::string to = search::trim(cfg.lot_valid_to);
    if (from.empty()) return true;
    if (date.empty()) return false;
    return date >= from && (to.empty() || date <= to);
}

qc::Result toResult(const search::QualityControlLisRow& item, const qc::Config& cfg) {
    qc::Result row;
    row.source_rep_no = item.rep_no;
    row.source_entry_key = item.entry_id;
    row.room_code = item.room_code;
    row.mach_code = item.mach_code;
    row.mach_name = item.mach_name.empty() ? cfg.mach_name : item.mach_name;
    row.sample_no = item.sample_no;
    row.tester_name = item.tester_name;
    row.report_date = item.report_date;
    row.inspect_date = item.inspect_date;
    row.report_time = item.report_time;
    row.effective_time = item.effective_time;
    row.item_code = item.item_code;
    row.item_name = item.item_name.empty() ? cfg.item_name : item.item_name;
    row.item_eng = item.item_eng;
    row.result_text = item.result;
    row.has_numeric_value = parseNumber(item.result, row.result_value);
    row.unit = item.unit;
    row.normal = item.normal;
    row.qc_name = cfg.qc_name;
    row.level = cfg.level;
    row.lot_no = cfg.lot_no;
    row.lot_valid_from = cfg.lot_valid_from;
    row.lot_valid_to = cfg.lot_valid_to;
    row.target_mean = cfg.target_mean;
    row.target_sd = cfg.target_sd;
    return row;
}

qc::Result toRawCachedResult(const search::QualityControlLisRow& item) {
    qc::Result row;
    row.source_rep_no = item.rep_no;
    row.source_entry_key = item.entry_id;
    row.room_code = item.room_code;
    row.mach_code = item.mach_code;
    row.mach_name = item.mach_name;
    row.sample_no = item.sample_no;
    row.tester_name = item.tester_name;
    row.report_date = item.report_date;
    row.inspect_date = item.inspect_date;
    row.report_time = item.report_time;
    row.effective_time = item.effective_time;
    row.item_code = item.item_code;
    row.item_name = item.item_name;
    row.item_eng = item.item_eng;
    row.result_text = item.result;
    row.has_numeric_value = parseNumber(item.result, row.result_value);
    row.unit = item.unit;
    row.normal = item.normal;
    row.data_source = "LIS导入";
    return row;
}

qc::Result applyConfigToCachedResult(const qc::Result& item, const qc::Config& cfg, const std::string& source) {
    qc::Result row = item;
    row.mach_name = item.mach_name.empty() ? cfg.mach_name : item.mach_name;
    row.item_name = item.item_name.empty() ? cfg.item_name : item.item_name;
    row.qc_name = cfg.qc_name;
    row.level = cfg.level;
    row.lot_no = cfg.lot_no;
    row.lot_valid_from = cfg.lot_valid_from;
    row.lot_valid_to = cfg.lot_valid_to;
    row.target_mean = cfg.target_mean;
    row.target_sd = cfg.target_sd;
    row.qc_mean = 0.0;
    row.qc_sd = 0.0;
    row.qc_z = 0.0;
    row.has_qc_stats = false;
    row.has_qc_z = false;
    row.qc_status.clear();
    row.qc_rules.clear();
    row.data_source = source;
    return row;
}

void sortResults(std::vector<qc::Result>& rows) {
    std::sort(rows.begin(), rows.end(), [](const qc::Result& a, const qc::Result& b) {
        if (a.mach_code != b.mach_code) return a.mach_code < b.mach_code;
        if (a.item_code != b.item_code) return a.item_code < b.item_code;
        if (a.level != b.level) return a.level < b.level;
        const std::string aTime = pointDisplayTime(a);
        const std::string bTime = pointDisplayTime(b);
        if (aTime != bTime) return aTime < bTime;
        return a.source_rep_no < b.source_rep_no;
    });
}

void buildRowsFromCachedMirror(const qc::Query& request,
                               const std::vector<qc::Config>& active,
                               const std::vector<qc::Result>& cachedRows,
                               std::vector<qc::Result>& rows,
                               const std::string& source) {
    rows.clear();
    std::map<std::string, std::vector<qc::Config>> configsByMachineSample;
    for (const auto& cfg : active) {
        configsByMachineSample[search::trim(cfg.mach_code) + "\x1f" + search::trim(cfg.sample_no)].push_back(cfg);
    }
    std::set<std::string> emittedKeys;
    for (const auto& item : cachedRows) {
        const std::string key = search::trim(item.mach_code) + "\x1f" + search::trim(item.sample_no);
        const auto configIt = configsByMachineSample.find(key);
        if (configIt == configsByMachineSample.end()) continue;
        for (const auto& itemCfg : configIt->second) {
            if (!rowMatchesCachedQuery(request, itemCfg, item)) continue;
            if (!configInEffectiveDate(itemCfg, item.effective_time)) continue;
            const std::string uniqueKey = search::trim(item.source_rep_no) + "\x1f" + search::trim(item.source_entry_key) +
                                          "\x1f" + std::to_string(itemCfg.sample_item_id);
            if (!emittedKeys.insert(uniqueKey).second) break;
            rows.push_back(applyConfigToCachedResult(item, itemCfg, source));
            break;
        }
    }
    sortResults(rows);
}

bool queryLisRows(const ModuleContext& ctx, const qc::Query& request, bool importFromLis, std::vector<qc::Result>& rows,
                  int& configCount, bool& fromCache, std::string& cachedAt, std::string& error) {
    rows.clear();
    configCount = 0;
    fromCache = false;
    cachedAt.clear();
    std::vector<qc::Config> configs;
    if (!qc::load_analysis_configs(configs, error)) return false;

    std::vector<qc::Config> active;
    const std::string requestedMachine = search::trim(request.mach_code);
    const std::string requestedLevel = search::trim(request.level);
    for (const auto& cfg : configs) {
        if (!cfg.enabled) continue;
        if (search::trim(cfg.mach_code).empty() || search::trim(cfg.sample_no).empty()) continue;
        if (!requestedMachine.empty() && search::trim(cfg.mach_code) != requestedMachine) continue;
        if (!requestedLevel.empty() && search::trim(cfg.level) != requestedLevel) continue;
        active.push_back(cfg);
    }
    std::set<int> activeItems;
    for (const auto& cfg : active) activeItems.insert(cfg.sample_item_id);
    configCount = static_cast<int>(activeItems.size());
    if (active.empty()) return true;

    std::string sampleScope;
    std::string itemScope;
    std::vector<std::string> samples;
    std::vector<std::string> itemsForScope;
    for (const auto& cfg : active) {
        samples.push_back(search::trim(cfg.sample_no));
        itemsForScope.push_back(search::trim(cfg.item_code));
    }
    std::sort(samples.begin(), samples.end());
    samples.erase(std::unique(samples.begin(), samples.end()), samples.end());
    std::sort(itemsForScope.begin(), itemsForScope.end());
    itemsForScope.erase(std::unique(itemsForScope.begin(), itemsForScope.end()), itemsForScope.end());
    auto joinScope = [](const std::vector<std::string>& values) {
        std::string text;
        for (const auto& value : values) {
            if (search::trim(value).empty()) continue;
            if (!text.empty()) text += ";";
            text += search::trim(value);
        }
        return text;
    };
    sampleScope = joinScope(samples);
    itemScope = joinScope(itemsForScope);
    if (!importFromLis) {
        qc::CacheMeta meta;
        std::vector<qc::Result> cachedRows;
        if (!qc::load_result_cache(request, cachedRows, meta, error)) return false;
        buildRowsFromCachedMirror(request, active, cachedRows, rows, "本机缓存");
        fromCache = true;
        cachedAt = meta.refreshed_at.empty() ? meta.cached_at : meta.refreshed_at;
        return true;
    }

    const std::string connection = search::wide_to_utf8(search::build_connection_string_w(ctx.dbSettings));
    if (search::trim(connection).empty()) {
        error = "missing LIS database connection";
        return false;
    }

    std::map<std::string, std::vector<qc::Config>> configsByMachineSample;
    for (const auto& cfg : active) {
        configsByMachineSample[search::trim(cfg.mach_code) + "\x1f" + search::trim(cfg.sample_no)].push_back(cfg);
    }

    // Collect unique sample numbers for single batch query
    std::vector<std::string> batchSamples;
    {
        std::set<std::string> seen;
        for (const auto& cfg : active) {
            const std::string sn = search::trim(cfg.sample_no);
            if (!sn.empty() && seen.insert(sn).second) batchSamples.push_back(sn);
        }
    }

    search::QualityControlLisQuery lisQuery;
    lisQuery.connection_string = connection;
    lisQuery.start_date = request.start_date;
    lisQuery.end_date = request.end_date;
    lisQuery.mach_code = search::trim(request.mach_code);
    lisQuery.sample_nos = batchSamples;

    std::vector<search::QualityControlLisRow> items;
    if (!search::query_quality_control_lis_results(lisQuery, items, error)) return false;

    std::vector<qc::Result> rawRows;
    for (const auto& item : items) {
        rawRows.push_back(toRawCachedResult(item));
    }
    std::string cacheError;
    if (!qc::save_result_cache(request, sampleScope, itemScope, rawRows, cacheError)) {
        if (!cacheError.empty()) error = cacheError;
        return false;
    }

    std::map<std::string, std::vector<qc::Config>> configsForDisplay;
    for (const auto& cfg : active) {
        configsForDisplay[search::trim(cfg.mach_code) + "\x1f" + search::trim(cfg.sample_no)].push_back(cfg);
    }
    std::set<std::string> emittedKeys;
    for (const auto& item : rawRows) {
        const std::string key = search::trim(item.mach_code) + "\x1f" + search::trim(item.sample_no);
        const auto configIt = configsForDisplay.find(key);
        if (configIt == configsForDisplay.end()) continue;
        for (const auto& itemCfg : configIt->second) {
            if (!rowMatchesCachedQuery(request, itemCfg, item)) continue;
            if (!configInEffectiveDate(itemCfg, item.effective_time)) continue;
            const std::string uniqueKey = search::trim(item.source_rep_no) + "\x1f" + search::trim(item.source_entry_key) +
                                          "\x1f" + std::to_string(itemCfg.sample_item_id);
            if (!emittedKeys.insert(uniqueKey).second) break;
            rows.push_back(applyConfigToCachedResult(item, itemCfg, "LIS导入"));
            break;
        }
    }

    sortResults(rows);
    return true;
}

void showMachinePicker(HWND hwnd, State* st) {
    if (!st) return;
    search::MachinePickerPopupOptions options;
    options.owner = hwnd;
    options.anchor = st->machPick;
    options.font = st->ctx.uiFont;
    options.db_settings = st->ctx.dbSettings;
    options.current_mach_code = search::trim(st->selectedMachineCode);
    options.include_all_rooms = true;
    options.on_accept = [st](const search::MachineOption& machine) {
        st->selectedMachineCode = machine.mach_code;
        SetWindowTextW(st->machCode, search::utf8_to_wide(machine.mach_name.empty() ? machine.mach_code : machine.mach_name).c_str());
        clearCurrentResults(st);
        setStatus(st, L"已选择检验仪器，请点击“查询”或“导入质控”。");
    };
    search::show_machine_picker_popup(options);
}

void runQualityControlQuery(HWND hwnd, State* st, bool importFromLis, const qc::Query* importQuery = nullptr) {
    if (!st || st->busy) return;
    if (search::trim(st->selectedMachineCode).empty()) {
        setStatus(st, L"请先选择检验仪器，再查询质控结果。");
        MessageBoxW(hwnd, L"请先选择检验仪器。", WINDOW_TITLE, MB_ICONINFORMATION);
        return;
    }
    st->busy = true;
    EnableWindow(st->queryButton, FALSE);
    EnableWindow(st->refreshButton, FALSE);
    EnableWindow(st->exportButton, FALSE);
    EnableWindow(st->chartButton, FALSE);
    updateSidePanel(st);
    setStatus(st, importFromLis ? L"正在导入质控数据..." : L"正在查询质控结果...");
    const qc::Query query = importQuery ? *importQuery : buildQuery(st);
    const ModuleContext ctx = st->ctx;
    std::thread([hwnd, ctx, query, importFromLis]() {
        auto* done = new QueryDone;
        const auto started = std::chrono::steady_clock::now();
        done->imported = importFromLis;
        done->import_start_date = query.start_date;
        done->import_end_date = query.end_date;
        done->ok = queryLisRows(ctx, query, importFromLis, done->rows, done->config_count,
                                done->from_cache, done->cached_at, done->error);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
        done->elapsed_ms = static_cast<int>(elapsed);
        if (!PostMessageW(hwnd, WM_QC_QUERY_DONE, 0, reinterpret_cast<LPARAM>(done))) delete done;
    }).detach();
}

void openRegularReport(HWND hwnd, State* st, int visibleIndex) {
    const auto rows = visibleDetails(st);
    if (visibleIndex < 0 || visibleIndex >= static_cast<int>(rows.size())) return;
    const qc::Result* row = rows[static_cast<size_t>(visibleIndex)];
    if (search::trim(row->source_rep_no).empty() || search::trim(row->mach_code).empty()) {
        MessageBoxW(hwnd, L"当前质控记录缺少报告号或仪器代码，无法跳转。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }
    auto* target = new RegularReportOpenTarget;
    target->rep_no = row->source_rep_no;
    target->oper_no = row->sample_no;
    target->inspect_date = row->inspect_date.empty() ? row->report_date : row->inspect_date;
    target->mach_code = row->mach_code;
    target->mach_name = row->mach_name;
    target->room_code = row->room_code;
    HWND regular = create_regular_report_module(st->ctx);
    if (!regular || !PostMessageW(regular, WM_REGULAR_OPEN_REPORT, 0, reinterpret_cast<LPARAM>(target))) {
        delete target;
        MessageBoxW(hwnd, L"常规报告页面打开失败。", WINDOW_TITLE, MB_ICONERROR);
    }
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<State*>(GetPropW(hwnd, PROP_STATE));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* mcs = reinterpret_cast<MDICREATESTRUCTW*>(cs->lpCreateParams);
            st = reinterpret_cast<State*>(mcs->lParam);
            SetPropW(hwnd, PROP_STATE, reinterpret_cast<HANDLE>(st));
            st->bgBrush = CreateSolidBrush(RGB(0xF3, 0xF6, 0xFA));
            st->startLabel = label(hwnd, L"开始日期");
            st->startDate = datePicker(hwnd, IDC_START_DATE);
            st->endLabel = label(hwnd, L"至");
            st->endDate = datePicker(hwnd, IDC_END_DATE);
            st->machLabel = label(hwnd, L"仪器");
            st->machCode = edit(hwnd, IDC_MACH_CODE);
            SendMessageW(st->machCode, EM_SETREADONLY, TRUE, 0);
            st->machPick = button(hwnd, IDC_MACH_PICK, L"...");
            st->itemLabel = label(hwnd, L"项目代码");
            st->itemCode = edit(hwnd, IDC_ITEM_CODE);
            st->levelLabel = label(hwnd, L"水平");
            st->level = combo(hwnd, IDC_LEVEL);
            setComboItems(st->level, {L"全部", L"L1", L"L2", L"L3"});
            st->statusFilterLabel = label(hwnd, L"状态");
            st->statusFilter = combo(hwnd, IDC_STATUS_FILTER);
            setComboItems(st->statusFilter, {L"全部", L"警告+失控", L"仅失控", L"仅警告"});
            st->queryButton = button(hwnd, IDC_QUERY, L"查询");
            st->refreshButton = button(hwnd, IDC_IMPORT_QC, L"导入质控");
            st->chartButton = button(hwnd, IDC_LJ_CHART, L"L-J图");
            EnableWindow(st->chartButton, FALSE);
            st->exportButton = button(hwnd, IDC_EXPORT_EXCEL, L"导出Excel");
            EnableWindow(st->exportButton, FALSE);
            st->groups = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                         WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                         0, 0, 0, 0, hwnd, win32_control_id(IDC_GROUPS), GetModuleHandleW(nullptr), nullptr);
            st->summary = CreateWindowExW(0, L"STATIC", L"今日质控概览", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                                          0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            ensureCardWindowClass();
            st->cards = CreateWindowExW(WS_EX_CLIENTEDGE, CARD_WND_CLASS, L"",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_CLIPSIBLINGS,
                                        0, 0, 0, 0, hwnd, win32_control_id(IDC_CARDS), GetModuleHandleW(nullptr), nullptr);
            st->details = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                          WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                          0, 0, 0, 0, hwnd, win32_control_id(IDC_DETAILS), GetModuleHandleW(nullptr), nullptr);
            st->sideTitle = CreateWindowExW(0, L"STATIC", L"筛选条件", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                                            0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            st->sideChartButton = button(hwnd, IDC_LJ_CHART, L"L-J图");
            st->sideDetailsButton = button(hwnd, IDC_SIDE_DETAILS, L"明细");
            st->sideExportButton = button(hwnd, IDC_EXPORT_EXCEL, L"导出");
            EnableWindow(st->sideChartButton, FALSE);
            EnableWindow(st->sideDetailsButton, FALSE);
            EnableWindow(st->sideExportButton, FALSE);
            st->sideList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                           WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                           0, 0, 0, 0, hwnd, win32_control_id(IDC_SIDE_LIST), GetModuleHandleW(nullptr), nullptr);
            st->status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                         0, 0, 0, 0, hwnd, win32_control_id(IDC_STATUS), GetModuleHandleW(nullptr), nullptr);
            setupList(st->groups, {{L"项目", 150}, {L"水平", 70}, {L"样本号", 80}, {L"点数", 60},
                                   {L"均值", 70}, {L"SD", 70}, {L"CV%", 70}, {L"基准", 60},
                                   {L"状态", 70}, {L"失控", 56}, {L"警告", 56}, {L"最近时间", 140}});
            setupList(st->details, {{L"时间", 140}, {L"样本号", 90}, {L"报告号", 90}, {L"仪器", 160},
                                    {L"检验者", 90}, {L"项目", 180}, {L"水平", 60}, {L"批号", 90},
                                    {L"结果", 90}, {L"单位", 70}, {L"状态", 70}, {L"规则", 90},
                                    {L"Z值", 70}, {L"来源", 96}});
            setupList(st->sideList, {{L"项目", 480}, {L"状态", 90}});
            setTodayRange(st);
            search::apply_font_to_children(hwnd, st->ctx.uiFont);
            resizeLayout(hwnd, st);
            {
                std::string error;
                if (!qc::ensure_store(error)) {
                    setStatus(st, L"本地质控库初始化失败：" + search::utf8_to_wide(error));
                } else {
                    setStatus(st, L"请选择检验仪器后点击“查询”或“导入质控”。");
                }
            }
            return 0;
        }
        case WM_SIZE:
            resizeLayout(hwnd, st);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_QUERY) {
                runQualityControlQuery(hwnd, st, false);
                return 0;
            }
            if (LOWORD(wp) == IDC_IMPORT_QC) {
                qc::Query importQuery;
                if (showImportRangeDialog(hwnd, st, importQuery)) {
                    runQualityControlQuery(hwnd, st, true, &importQuery);
                }
                return 0;
            }
            if (LOWORD(wp) == IDC_MACH_PICK) {
                showMachinePicker(hwnd, st);
                return 0;
            }
            if (LOWORD(wp) == IDC_EXPORT_EXCEL) {
                exportVisibleDetailsXlsx(hwnd, st);
                return 0;
            }
            if (LOWORD(wp) == IDC_LJ_CHART) {
                openLeveyJenningsChart(hwnd, st);
                return 0;
            }
            if (LOWORD(wp) == IDC_SIDE_DETAILS) {
                st->detailsExpanded = !st->detailsExpanded;
                SetWindowTextW(st->sideDetailsButton, st->detailsExpanded ? L"收起" : L"明细");
                resizeLayout(hwnd, st);
                return 0;
            }
            if (LOWORD(wp) == IDC_STATUS_FILTER && HIWORD(wp) == CBN_SELCHANGE) {
                st->selectedGroup = -1;
                st->selectedCard = -1;
                refreshVisibleResults(st);
                const auto rows = visibleDetails(st);
                setStatus(st, L"已应用状态筛选：当前显示 " + std::to_wstring(rows.size()) + L" 条质控明细。");
                return 0;
            }
            break;
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (st && nm->idFrom == IDC_GROUPS && nm->code == LVN_ITEMCHANGED && !st->suppressSelectionNotify) {
                auto* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED)) {
                    selectGroup(st, lv->iItem);
                }
                return 0;
            }
            if (st && nm->idFrom == IDC_GROUPS && nm->code == NM_DBLCLK) {
                auto* item = reinterpret_cast<NMITEMACTIVATE*>(lp);
                selectGroup(st, item->iItem);
                openLeveyJenningsChart(hwnd, st);
                return 0;
            }
            if (st && nm->idFrom == IDC_SIDE_LIST && nm->code == LVN_ITEMCHANGED && !st->suppressSelectionNotify) {
                auto* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED)) {
                    selectCard(st, lv->iItem);
                }
                return 0;
            }
            if (st && nm->idFrom == IDC_DETAILS && nm->code == NM_DBLCLK) {
                auto* item = reinterpret_cast<NMITEMACTIVATE*>(lp);
                openRegularReport(hwnd, st, item->iItem);
                return 0;
            }
            break;
        }
        case WM_QC_QUERY_DONE: {
            std::unique_ptr<QueryDone> done(reinterpret_cast<QueryDone*>(lp));
            st->busy = false;
            EnableWindow(st->queryButton, TRUE);
            EnableWindow(st->refreshButton, TRUE);
            if (!done->ok) {
                updateSidePanel(st);
                updateExportButton(st);
                updateChartButton(st);
                setStatus(st, std::wstring(done->imported ? L"导入失败：" : L"查询失败：") +
                                  search::utf8_to_wide(done->error));
                MessageBoxW(hwnd, search::utf8_to_wide(done->error).c_str(), WINDOW_TITLE, MB_ICONERROR);
                return 0;
            }
            if (done->imported && !done->from_cache) {
                std::wstring status = L"已导入质控（" + search::utf8_to_wide(done->import_start_date) +
                                      L" 至 " + search::utf8_to_wide(done->import_end_date) + L"）：导入 " +
                                      std::to_wstring(done->rows.size()) + L" 条；启用配置 " +
                                      std::to_wstring(done->config_count) + L" 条；耗时 " +
                                      std::to_wstring(done->elapsed_ms) + L" ms。当前页面日期和卡片结果未改变，点击“查询”可读取本地数据。";
                if (done->config_count == 0) {
                    status = L"没有匹配的启用质控配置，请先在系统设置中维护仪器和固定样本号。";
                } else if (done->rows.empty()) {
                    status += L"；所选导入范围内没有匹配 LIS 结果。";
                }
                setStatus(st, status);
                updateSidePanel(st);
                updateExportButton(st);
                updateChartButton(st);
                return 0;
            }
            st->rows = std::move(done->rows);
            st->selectedGroup = -1;
            st->selectedCard = -1;
            buildGroups(st, false);
            evaluateWestgardRules(st);
            refreshVisibleResults(st);
            int warningCount = 0;
            int outOfControlCount = 0;
            int evaluatedCount = 0;
            for (const auto& row : st->rows) {
                if (!row.qc_status.empty()) ++evaluatedCount;
                if (row.qc_status == "warning") ++warningCount;
                if (row.qc_status == "out_of_control") ++outOfControlCount;
            }
            std::wstring sourceText;
            if (done->from_cache) {
                sourceText = L"本地查询";
                if (!done->cached_at.empty()) sourceText += L"（最近导入质控：" + search::utf8_to_wide(done->cached_at) + L"）";
            } else {
                sourceText = done->imported ? L"已导入质控" : L"LIS导入";
                if (done->imported) {
                    sourceText += L"（" + search::utf8_to_wide(done->import_start_date) +
                                  L" 至 " + search::utf8_to_wide(done->import_end_date) + L"）";
                }
            }
            std::wstring status = sourceText + L"：返回 " + std::to_wstring(st->rows.size()) +
                                  L" 条；启用配置 " + std::to_wstring(done->config_count) +
                                  L" 条；已判定 " + std::to_wstring(evaluatedCount) +
                                  L" 条；警告 " + std::to_wstring(warningCount) +
                                  L" 条；失控 " + std::to_wstring(outOfControlCount) +
                                  L" 条；耗时 " + std::to_wstring(done->elapsed_ms) + L" ms";
            if (done->config_count == 0) {
                status = L"没有匹配的启用质控配置，请先在系统设置中维护仪器和固定样本号。";
            } else if (done->from_cache && st->rows.empty() && done->cached_at.empty()) {
                status = L"本地查询暂无该范围质控数据，请先点击“导入质控”。";
            } else if (st->rows.empty()) {
                status += L"；当前日期范围内没有匹配 LIS 结果。";
            }
            setStatus(st, status);
            updateExportButton(st);
            updateChartButton(st);
            return 0;
        }
        case app::WM_APP_SETTINGS_CHANGED:
        case app::WM_APP_FONT_CHANGED:
            if (msg == app::WM_APP_FONT_CHANGED && lp) st->ctx.uiFont = reinterpret_cast<HFONT>(lp);
            search::apply_font_to_children(hwnd, st->ctx.uiFont);
            resizeLayout(hwnd, st);
            return 0;
        case WM_CTLCOLORSTATIC:
            SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
            return reinterpret_cast<LRESULT>(st ? st->bgBrush : nullptr);
        case WM_ERASEBKGND: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc, st && st->bgBrush ? st->bgBrush : GetSysColorBrush(COLOR_BTNFACE));
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
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);
    registered = true;
}

}  // namespace

HWND create_quality_control_module(const ModuleContext& ctx) {
    if (HWND existing = activate_existing_mdi_child_by_title(ctx.mdiClient, WINDOW_TITLE)) return existing;
    registerClass(ctx.instance);
    auto* st = new State;
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
