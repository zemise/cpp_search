#include "query_module.h"

#ifdef _WIN32

#include "app_settings_io.h"
#include "log.h"
#include "resource.h"
#include "search_app.h"
#include "search_controller.h"
#include "search_input_view_model.h"
#include "search_splitter.h"
#include "search_text.h"
#include "search_ui_context.h"
#include "search_ui_columns.h"
#include "search_ui_events.h"
#include "search_ui_layout.h"
#include "search_ui_presenter.h"
#include "search_view_state.h"
#include "regular_report_module.h"
#include "trend_window.h"
#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <thread>

#pragma comment(lib, "comctl32.lib")

namespace {

constexpr int IDC_PATIENT_ID = 1002;
constexpr int IDC_BARCODE = 1003;
constexpr int IDC_NAME = 1004;
constexpr int IDC_PATIENT_NO = 1005;
constexpr int IDC_OPER = 1006;
constexpr int IDC_START = 1007;
constexpr int IDC_END = 1008;
constexpr int IDC_ROOM = 1009;
constexpr int IDC_MACH = 1010;
constexpr int IDC_GROUP = 1011;
constexpr int IDC_ITEM = 1012;
constexpr int IDC_PATIENT_TYPE = 1013;
constexpr int IDC_REPORT_STATUS = 1014;
constexpr int IDC_REPORTS = 2001;
constexpr int IDC_RESULTS = 2002;
constexpr int IDC_SPLITTER = 2003;
constexpr int IDC_QUERY = 3001;
constexpr int IDC_EXIT = 3002;
constexpr int IDC_EXPORT = 3003;
constexpr int IDC_PREVIEW = 3004;
constexpr int IDC_PRINT = 3005;
constexpr int IDC_SETTINGS = 3006;
constexpr int IDC_TREND = 3007;
constexpr int IDC_STATUS = 4001;

constexpr const wchar_t* WND_CLASS   = L"QueryModuleChild";
constexpr const wchar_t* PROP_STATE  = L"QuerySt";
constexpr const wchar_t* WINDOW_TITLE = L"检验结果查询";
constexpr UINT WM_QUERY_LOADED = WM_APP + 71;
constexpr UINT WM_QUERY_RESULT_LOADED = WM_APP + 72;

struct QueryState {
    ModuleContext ctx;
    search::MainUiIds ids{IDC_PATIENT_ID, IDC_BARCODE, IDC_NAME, IDC_PATIENT_NO, IDC_OPER,
        IDC_START, IDC_END, IDC_ROOM, IDC_MACH, IDC_GROUP, IDC_ITEM, IDC_PATIENT_TYPE,
        IDC_REPORT_STATUS, IDC_REPORTS, IDC_RESULTS, IDC_SPLITTER,
        IDC_SETTINGS, IDC_QUERY, IDC_TREND, IDC_EXPORT, IDC_PREVIEW, IDC_PRINT, IDC_EXIT, IDC_STATUS};
    search::MainUiHandles ui;
    HFONT uiFont = nullptr;
    search::ViewState viewState;
    search::QueryInput lastQueryInput;
    bool hasLastQueryInput = false;
    int pendingSplitterX = 0;
    int queryGeneration = 0;
    int resultGeneration = 0;
    int reportSortColumn = -1;
    bool reportSortAscending = true;
    bool queryLoading = false;
    std::thread bgThread;
    std::thread bgResultThread;
};

struct QueryLoadResult {
    int generation = 0;
    bool ok = false;
    search::QueryInput input;
    std::vector<search::ReportRow> rows;
    std::string connection_string;
    std::string error;
};

struct ResultLoadResult {
    int generation = 0;
    bool ok = false;
    std::vector<search::ResultRow> rows;
    std::string error;
};

search::DbSettings& db(QueryState* q)  { return q->viewState.settings.db; }
int& fontSize(QueryState* q)          { return q->viewState.settings.ui.font_size; }
int& splitterX(QueryState* q)         { return q->viewState.settings.ui.splitter_x; }
auto& reportRows(QueryState* q)       { return q->viewState.report_rows; }
auto& resultRows(QueryState* q)       { return q->viewState.result_rows; }
auto& roomOpts(QueryState* q)         { return q->viewState.room_options; }
auto& patientTypeOpts(QueryState* q)  { return q->viewState.patient_type_options; }
auto& machineOpts(QueryState* q)      { return q->viewState.machine_options; }
auto& connStr(QueryState* q)          { return q->viewState.connection_string; }

void querySelectedResults(QueryState* q, int selected);

void setStatus(QueryState* q, const std::wstring& text) {
    search::set_status_text(q->ui, text);
}

COLORREF reportRowBg(const search::ReportRow& row) {
    switch (search::report_row_tone(row)) {
        case search::ReportRowTone::Printed:  return RGB(0xFF, 0xFF, 0xFF);
        case search::ReportRowTone::Reviewed: return RGB(0x75, 0xFB, 0xFD);
        default:                              return RGB(0xED, 0x6D, 0x52);
    }
}

COLORREF resultRowColor(const search::ResultRow& row) {
    switch (search::result_row_tone(row)) {
        case search::ResultRowTone::High: return RGB(220, 0, 0);
        case search::ResultRowTone::Low:  return RGB(0, 0, 220);
        default:                          return CLR_INVALID;
    }
}

std::string reportSortValue(const search::ReportRow& row, int col) {
    switch (col) {
        case search::report_columns::SampleNo: return row.oper_no;
        case search::report_columns::Name: return row.name;
        case search::report_columns::Barcode: return row.txm_no;
        case search::report_columns::ReportTime: return row.chk_date;
        case search::report_columns::Sex: return row.sex;
        case search::report_columns::Age: return row.age;
        case search::report_columns::Bed: return row.bed_code;
        case search::report_columns::PatientType: return row.patient_type;
        case search::report_columns::Requester: return row.requester;
        case search::report_columns::Reviewer: return row.reviewer;
        case search::report_columns::GroupName: return row.group_name;
        case search::report_columns::ReviewStatus: return search::display_conf(row.conf);
        case search::report_columns::ConfirmStatus: return search::display_chk_flag(row.chk_flag);
        case search::report_columns::PrintStatus: return search::display_binary_print_flag(row.zymz_print);
        case search::report_columns::SelfServicePrintStatus:
            return search::display_binary_print_flag(row.zzj_print);
        default: return row.id;
    }
}

bool parseSortNumber(const std::string& value, double& out) {
    const std::string trimmed = search::trim(value);
    if (trimmed.empty()) return false;
    char* end = nullptr;
    out = std::strtod(trimmed.c_str(), &end);
    return end && *end == '\0';
}

int compareReportRows(const search::ReportRow& left, const search::ReportRow& right, int col) {
    const std::string lv = search::trim(reportSortValue(left, col));
    const std::string rv = search::trim(reportSortValue(right, col));
    double ln = 0;
    double rn = 0;
    if (parseSortNumber(lv, ln) && parseSortNumber(rv, rn) && ln != rn)
        return ln < rn ? -1 : 1;
    if (lv != rv) return lv < rv ? -1 : 1;
    const std::string lid = search::trim(left.id);
    const std::string rid = search::trim(right.id);
    return lid < rid ? -1 : (lid == rid ? 0 : 1);
}

void sortReportRowsByColumn(QueryState* q, int col) {
    if (!q || !q->ui.reports || reportRows(q).empty() || col < 0) return;
    if (q->reportSortColumn == col) q->reportSortAscending = !q->reportSortAscending;
    else {
        q->reportSortColumn = col;
        q->reportSortAscending = true;
    }
    const bool asc = q->reportSortAscending;

    std::string selectedId;
    const int selected = ListView_GetNextItem(q->ui.reports, -1, LVNI_SELECTED);
    if (selected >= 0 && selected < static_cast<int>(reportRows(q).size()))
        selectedId = search::trim(reportRows(q)[static_cast<size_t>(selected)].id);

    std::stable_sort(reportRows(q).begin(), reportRows(q).end(),
        [col, asc](const auto& left, const auto& right) {
            const int cmp = compareReportRows(left, right, col);
            return asc ? cmp < 0 : cmp > 0;
        });

    search::present_report_rows(q->ui, reportRows(q));
    if (!selectedId.empty()) {
        for (int i = 0; i < static_cast<int>(reportRows(q).size()); ++i) {
            if (search::trim(reportRows(q)[static_cast<size_t>(i)].id) == selectedId) {
                ListView_SetItemState(q->ui.reports, i, LVIS_SELECTED | LVIS_FOCUSED,
                                      LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(q->ui.reports, i, FALSE);
                return;
            }
        }
    }
    querySelectedResults(q, -1);
}

void reloadRoomOptions(QueryState* q) {
    if (!q->ui.room) return;
    roomOpts(q).clear();
    std::string error;
    if (!search::load_room_options(db(q), roomOpts(q), error)) {
        setStatus(q, L"检验科室加载失败。");
        return;
    }
    search::populate_room_combo(q->ui.room, roomOpts(q));
}

void reloadPatientTypeOptions(QueryState* q) {
    if (!q->ui.patient_type) return;
    patientTypeOpts(q).clear();
    std::string error;
    if (!search::load_patient_type_options(db(q), patientTypeOpts(q), error)) {
        setStatus(q, L"病人类型加载失败。");
        return;
    }
    search::populate_patient_type_combo(q->ui.patient_type, patientTypeOpts(q));
}

void reloadMachineOptions(QueryState* q) {
    if (!q->ui.mach) return;
    machineOpts(q).clear();
    std::string error;
    if (!search::load_machine_options(db(q), search::selected_room_code(q->ui.room, roomOpts(q)), machineOpts(q), error)) {
        setStatus(q, L"检验仪器加载失败。");
        return;
    }
    search::populate_machine_combo(q->ui.mach, machineOpts(q));
}

void runQuery(QueryState* q) {
    auto input = search::build_query_input(q->ui, q->viewState);
    input.skip_order_text = true;
    if (search::build_connection_string_w(db(q)).empty()) {
        MessageBoxW(nullptr, L"请先在“设置”中填写数据库连接信息。", L"缺少数据库设置", MB_ICONWARNING);
        return;
    }
    search::clear_result_lists(q->ui);
    reportRows(q).clear();
    resultRows(q).clear();
    setStatus(q, L"正在查询...");

    q->queryLoading = true;
    const int generation = ++q->queryGeneration;
    const search::DbSettings settings = db(q);
    const HWND hwnd = GetParent(q->ui.reports);
    EnableWindow(q->ui.query_button, FALSE);
    if (q->bgThread.joinable()) q->bgThread.join();
    q->bgThread = std::thread([hwnd, settings, input, generation]() {
        try {
            auto* result = new QueryLoadResult;
            result->generation = generation;
            result->input = input;
            result->ok = search::run_report_query(settings, input, result->rows, result->connection_string, result->error);
            if (!PostMessageW(hwnd, WM_QUERY_LOADED, 0, reinterpret_cast<LPARAM>(result))) {
                LOG_WARN("PostMessageW WM_QUERY_LOADED failed");
                delete result;
            }
        } catch (...) {
            LOG_ERROR("Background query thread crashed");
        }
    });
}

void finishRunQuery(QueryState* q, HWND hwnd, std::unique_ptr<QueryLoadResult> result) {
    if (result->generation != q->queryGeneration) return;
    q->queryLoading = false;
    EnableWindow(q->ui.query_button, TRUE);
    if (!result->ok) {
        setStatus(q, L"查询失败");
        MessageBoxW(hwnd, search::utf8_to_wide(result->error).c_str(), L"查询失败", MB_ICONERROR);
        return;
    }
    q->lastQueryInput = result->input;
    q->hasLastQueryInput = true;
    q->reportSortColumn = -1;
    q->reportSortAscending = true;
    reportRows(q) = std::move(result->rows);
    connStr(q) = std::move(result->connection_string);
    search::present_report_rows(q->ui, reportRows(q));
    setStatus(q, search::utf8_to_wide(search::make_query_count_status(reportRows(q).size())));
}

void querySelectedResults(QueryState* q, int selected) {
    resultRows(q).clear();
    if (selected < 0 || selected >= static_cast<int>(reportRows(q).size())) {
        ListView_DeleteAllItems(q->ui.results);
        return;
    }
    ListView_DeleteAllItems(q->ui.results);
    setStatus(q, L"正在查询项目明细...");
    const int generation = ++q->resultGeneration;
    const std::string connection = connStr(q);
    const std::string repNo = reportRows(q)[static_cast<size_t>(selected)].rep_no;
    const HWND hwnd = GetParent(q->ui.results);
    if (q->bgResultThread.joinable()) q->bgResultThread.join();
    q->bgResultThread = std::thread([hwnd, connection, repNo, generation]() {
        auto* result = new ResultLoadResult;
        result->generation = generation;
        result->ok = search::load_result_rows(connection, repNo, result->rows, result->error);
        if (!PostMessageW(hwnd, WM_QUERY_RESULT_LOADED, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    });
}

void finishQuerySelectedResults(QueryState* q, HWND hwnd, std::unique_ptr<ResultLoadResult> result) {
    if (result->generation != q->resultGeneration) return;
    if (!result->ok) {
        MessageBoxW(hwnd, search::utf8_to_wide(result->error).c_str(), L"查询项目明细失败", MB_ICONERROR);
        return;
    }
    resultRows(q) = std::move(result->rows);
    search::present_result_rows(q->ui, resultRows(q));
    setStatus(q, search::utf8_to_wide(search::make_query_count_status(reportRows(q).size())));
}

void showTrend(HWND owner, QueryState* q) {
    if (search::build_connection_string_w(db(q)).empty()) {
        MessageBoxW(owner, L"请先在“设置”中填写数据库连接信息。", L"缺少数据库设置", MB_ICONWARNING);
        return;
    }
    if (!q->hasLastQueryInput || reportRows(q).empty()) {
        MessageBoxW(owner, L"请先查询出同一病人的结果后再打开趋势图。", L"趋势图提示", MB_ICONWARNING);
        return;
    }
    auto input = q->lastQueryInput;
    if (search::trim(input.patient_name).empty() && search::trim(input.patient_no).empty()) {
        MessageBoxW(owner, L"上次查询未使用病人姓名或病人号，请先按病人姓名或病人号查询后再打开趋势图。", L"趋势图提示", MB_ICONWARNING);
        return;
    }
    search::show_trend_window(owner, q->uiFont ? q->uiFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                              db(q), input);
}

void openRegularReportForRow(HWND owner, QueryState* q, int index) {
    if (!q || index < 0 || index >= static_cast<int>(reportRows(q).size())) return;
    const auto& row = reportRows(q)[static_cast<size_t>(index)];
    if (search::trim(row.rep_no).empty() || search::trim(row.mach_code).empty()) {
        MessageBoxW(owner,
                    L"当前报告缺少报告号或检验仪器代码，无法跳转到常规报告。",
                    L"常规报告", MB_ICONWARNING);
        return;
    }
    auto* target = new RegularReportOpenTarget;
    target->rep_no = search::trim(row.rep_no);
    target->oper_no = search::trim(row.oper_no);
    target->inspect_date = search::trim(row.inspect_date).empty() ? row.chk_date : row.inspect_date;
    target->mach_code = search::trim(row.mach_code);
    target->mach_name = search::trim(row.mach_name);
    target->room_code = search::trim(row.room_code);

    ModuleContext ctx = q->ctx;
    ctx.dbSettings = db(q);
    ctx.uiFont = q->uiFont;
    ctx.fontSize = fontSize(q);
    HWND regular = create_regular_report_module(ctx);
    if (!regular || !PostMessageW(regular, WM_REGULAR_OPEN_REPORT, 0, reinterpret_cast<LPARAM>(target))) {
        delete target;
        MessageBoxW(owner, L"常规报告页面打开失败。", L"常规报告", MB_ICONERROR);
    }
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* q = reinterpret_cast<QueryState*>(GetPropW(hwnd, PROP_STATE));

    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* mcs = reinterpret_cast<MDICREATESTRUCTW*>(cs->lpCreateParams);
            q = reinterpret_cast<QueryState*>(mcs->lParam);
            if (!q) {
                LOG_ERROR("WM_CREATE: lpCreateParams is null");
                return -1;
            }
            SetPropW(hwnd, PROP_STATE, reinterpret_cast<HANDLE>(q));

            q->uiFont = search::create_ui_font(fontSize(q));
            HFONT font = q->uiFont ? q->uiFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            search::register_splitter_class(q->ctx.instance);
            search::create_main_controls(hwnd, font, q->ids, q->ui);
            ShowWindow(q->ui.settings_button, SW_HIDE);
            search::set_date_picker_today(q->ui.start);
            search::set_date_picker_today(q->ui.end);
            search::initialize_report_status_combo(q->ui.report_status);
            reloadRoomOptions(q);
            reloadPatientTypeOptions(q);
            reloadMachineOptions(q);
            q->pendingSplitterX = search::load_module_int(L"Query", L"SplitterX", 0);
            int initX = 0;
            search::layout_main_window(hwnd, q->ui, initX);
            return 0;
        }
        case WM_SIZE: {
            if (!q) return 0;
            if (q->pendingSplitterX > 0 && IsZoomed(hwnd)) {
                splitterX(q) = q->pendingSplitterX;
                q->pendingSplitterX = 0;
            }
            int x = splitterX(q);
            search::layout_main_window(hwnd, q->ui, x);
            splitterX(q) = x;
            return 0;
        }
        case search::WM_SPLITTER_DRAG:
            if (q && reinterpret_cast<HWND>(lp) == q->ui.splitter) {
                splitterX(q) = static_cast<int>(wp);
                search::layout_main_window(hwnd, q->ui, splitterX(q));
            }
            return 0;
        case search::WM_SPLITTER_RELEASED:
            if (q && reinterpret_cast<HWND>(lp) == q->ui.splitter) {
                splitterX(q) = static_cast<int>(wp);
                search::layout_main_window(hwnd, q->ui, splitterX(q));
                search::save_module_int(L"Query", L"SplitterX", splitterX(q));
            }
            return 0;
        case WM_DPICHANGED: {
            auto* rect = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd, nullptr, rect->left, rect->top,
                         rect->right - rect->left, rect->bottom - rect->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_COMMAND: {
            if (!q) break;
            search::CommandEventHandlers handlers;
            handlers.on_room_changed = [q] { reloadMachineOptions(q); };
            handlers.on_query = [q] { runQuery(q); };
            handlers.on_show_trend = [q](HWND owner) { showTrend(owner, q); };
            handlers.on_unimplemented_action = [](HWND owner) {
                MessageBoxW(owner, L"该功能暂未实现。", L"提示", MB_ICONINFORMATION);
            };
            handlers.on_exit = [](HWND owner) { DestroyWindow(owner); };
            if (search::handle_command(hwnd, wp, q->ids, handlers)) return 0;
            break;
        }
        case WM_QUERY_LOADED:
            if (q) {
                std::unique_ptr<QueryLoadResult> result(reinterpret_cast<QueryLoadResult*>(lp));
                finishRunQuery(q, hwnd, std::move(result));
            } else {
                delete reinterpret_cast<QueryLoadResult*>(lp);
            }
            return 0;
        case WM_QUERY_RESULT_LOADED:
            if (q) {
                std::unique_ptr<ResultLoadResult> result(reinterpret_cast<ResultLoadResult*>(lp));
                finishQuerySelectedResults(q, hwnd, std::move(result));
            } else {
                delete reinterpret_cast<ResultLoadResult*>(lp);
            }
            return 0;
        case WM_NOTIFY: {
            if (!q) break;
            search::NotifyEventHandlers handlers;
            handlers.on_report_selected = [q](int i) { querySelectedResults(q, i); };
            handlers.on_report_activated = [q, hwnd](int i) { openRegularReportForRow(hwnd, q, i); };
            handlers.on_report_column_clicked = [q](int col) { sortReportRowsByColumn(q, col); };
            handlers.report_row_background = [](const search::ReportRow& r) { return reportRowBg(r); };
            handlers.result_row_color = [](const search::ResultRow& r) { return resultRowColor(r); };
            handlers.report_rows = &reportRows(q);
            handlers.result_rows = &resultRows(q);
            LRESULT result = 0;
            if (search::handle_notify(lp, q->ids, handlers, result)) return result;
            break;
        }
        case WM_DESTROY: {
            if (q->bgThread.joinable()) q->bgThread.join();
            if (q->bgResultThread.joinable()) q->bgResultThread.join();
            delete q;
            RemovePropW(hwnd, PROP_STATE);
            break;
        }
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND create_query_module(const ModuleContext& ctx) {
    if (HWND existing = activate_existing_mdi_child_by_title(ctx.mdiClient, WINDOW_TITLE)) {
        return existing;
    }

    static bool classesRegistered = false;
    if (!classesRegistered) {
        REGISTER_MDI_CHILD_CLASS(ctx.instance, wndProc, WND_CLASS, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
        search::register_splitter_class(ctx.instance);
        classesRegistered = true;
    }

    auto* q = new QueryState;
    q->ctx = ctx;
    q->viewState.settings.db = ctx.dbSettings;
    fontSize(q) = search::clamp_font_size(ctx.fontSize);

    MDICREATESTRUCTW mcs{};
    mcs.szClass = WND_CLASS;
    mcs.szTitle = WINDOW_TITLE;
    mcs.hOwner = ctx.instance;
    mcs.x = mcs.y = mcs.cx = mcs.cy = CW_USEDEFAULT;

    mcs.lParam = reinterpret_cast<LPARAM>(q);
    HWND child = reinterpret_cast<HWND>(SendMessageW(ctx.mdiClient, WM_MDICREATE, 0,
        reinterpret_cast<LPARAM>(&mcs)));
    if (child) {
        SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    } else {
        delete q;
    }
    return child;
}

#endif
