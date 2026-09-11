#include "app_settings.h"
#include "app_settings_io.h"
#include "search_app.h"
#include "search_controller.h"
#include "search_input_view_model.h"
#include "search_settings_dialog.h"
#include "search_core.h"
#include "search_text.h"
#include "search_ui_context.h"
#include "search_ui_events.h"
#include "search_ui_layout.h"
#include "search_ui_presenter.h"
#include "search_view_state.h"
#include "trend_window.h"
#include "resource.h"
#include "version.h"

#ifndef _WIN32
#include <iostream>
int main() {
    std::cout << "result_search GUI is only available on Windows.\n";
    return 0;
}
#else

#include <windows.h>
#include <commctrl.h>

#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace {

void enable_dpi_awareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;

    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
    auto set_process_dpi_awareness_context = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (set_process_dpi_awareness_context &&
        set_process_dpi_awareness_context(reinterpret_cast<HANDLE>(-4))) {
        return;
    }

    using SetProcessDPIAwareFn = BOOL(WINAPI*)();
    auto set_process_dpi_aware = reinterpret_cast<SetProcessDPIAwareFn>(
        GetProcAddress(user32, "SetProcessDPIAware"));
    if (set_process_dpi_aware) {
        set_process_dpi_aware();
    }
}

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
search::MainUiIds g_main_ui_ids{
    IDC_PATIENT_ID, IDC_BARCODE, IDC_NAME, IDC_PATIENT_NO, IDC_OPER, IDC_START, IDC_END,
    IDC_ROOM, IDC_MACH, IDC_GROUP, IDC_ITEM, IDC_PATIENT_TYPE, IDC_REPORT_STATUS,
    IDC_REPORTS, IDC_RESULTS, IDC_SPLITTER,
    IDC_SETTINGS, IDC_QUERY, IDC_TREND, IDC_EXPORT, IDC_PREVIEW, IDC_PRINT, IDC_EXIT, IDC_STATUS
};
search::UiContext g_ui_context;
search::MainUiHandles& g_ui = g_ui_context.handles;
bool g_dragging_splitter = false;

search::ViewState g_state;
int& g_font_size = g_state.settings.ui.font_size;
int& g_splitter_x = g_state.settings.ui.splitter_x;
std::vector<search::ReportRow>& g_report_rows = g_state.report_rows;
std::vector<search::ResultRow>& g_result_rows = g_state.result_rows;
search::QueryInput g_last_query_input;
bool g_has_last_query_input = false;
std::vector<search::RoomOption>& g_room_options = g_state.room_options;
std::vector<search::PatientTypeOption>& g_patient_type_options = g_state.patient_type_options;
std::vector<search::MachineOption>& g_machine_options = g_state.machine_options;
std::string& g_connection_string = g_state.connection_string;
search::DbSettings& g_db_settings = g_state.settings.db;

void set_text(HWND hwnd, const std::wstring& text) {
    SetWindowTextW(hwnd, text.c_str());
}

void set_status(const std::wstring& text) {
    search::set_status_text(g_ui, text);
}

void save_settings_to_ini();

LRESULT CALLBACK splitter_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_SETCURSOR:
            SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
            return TRUE;
        case WM_LBUTTONDOWN:
            g_dragging_splitter = true;
            SetCapture(hwnd);
            return 0;
        case WM_MOUSEMOVE:
            if (g_dragging_splitter && g_ui_context.main_window) {
                POINT pt{};
                GetCursorPos(&pt);
                ScreenToClient(g_ui_context.main_window, &pt);
                g_splitter_x = pt.x - 4;
                search::layout_main_window(g_ui_context.main_window, g_ui, g_splitter_x);
            }
            return 0;
        case WM_LBUTTONUP:
            if (g_dragging_splitter) {
                g_dragging_splitter = false;
                ReleaseCapture();
                save_settings_to_ini();
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void apply_font_to_window(HWND root) {
    if (!root || !g_ui_context.ui_font) {
        return;
    }
    EnumChildWindows(root, [](HWND child, LPARAM) -> BOOL {
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui_context.ui_font), TRUE);
        return TRUE;
    }, 0);
    InvalidateRect(root, nullptr, TRUE);
    UpdateWindow(root);
}

void load_settings() {
    g_state.ini_path = search::default_ini_path();
    g_state.settings = search::load_settings(g_state.ini_path);
    g_font_size = search::clamp_font_size(g_font_size);
}

void save_settings_to_ini() {
    search::save_settings(g_state.ini_path, g_state.settings);
}

COLORREF report_row_background(const search::ReportRow& row) {
    switch (search::report_row_tone(row)) {
        case search::ReportRowTone::Printed:
            return RGB(0xFF, 0xFF, 0xFF);
        case search::ReportRowTone::Reviewed:
            return RGB(0x75, 0xFB, 0xFD);
        case search::ReportRowTone::Pending:
        default:
            return RGB(0xED, 0x6D, 0x52);
    }
}

COLORREF result_row_color(const search::ResultRow& row) {
    switch (search::result_row_tone(row)) {
        case search::ResultRowTone::High:
            return RGB(220, 0, 0);
        case search::ResultRowTone::Low:
            return RGB(0, 0, 220);
        case search::ResultRowTone::Default:
        default:
            return CLR_INVALID;
    }
}

void test_current_settings(HWND owner, const search::DbSettings& settings) {
    if (search::build_connection_string_w(settings).empty()) {
        MessageBoxW(owner, L"请先填写服务器、初始数据库和用户名。", L"测试连接", MB_ICONWARNING);
        return;
    }
    std::string error;
    if (search::test_database_connection(settings, error)) {
        MessageBoxW(owner, L"数据库连接成功。", L"测试连接", MB_ICONINFORMATION);
    } else {
        MessageBoxW(owner, search::utf8_to_wide(error).c_str(), L"数据库连接失败", MB_ICONERROR);
    }
}

void reload_room_options() {
    if (!g_ui.room) {
        return;
    }
    g_room_options.clear();

    std::string error;
    if (!search::load_room_options(g_db_settings, g_room_options, error)) {
        set_status(L"检验科室加载失败。");
        return;
    }
    search::populate_room_combo(g_ui.room, g_room_options);
}

void reload_patient_type_options() {
    if (!g_ui.patient_type) {
        return;
    }
    g_patient_type_options.clear();

    std::string error;
    if (!search::load_patient_type_options(g_db_settings, g_patient_type_options, error)) {
        set_status(L"病人类型加载失败。");
        return;
    }
    search::populate_patient_type_combo(g_ui.patient_type, g_patient_type_options);
}

void reload_machine_options() {
    if (!g_ui.mach) {
        return;
    }
    g_machine_options.clear();

    std::string error;
    if (!search::load_machine_options(g_db_settings, search::selected_room_code(g_ui.room, g_room_options), g_machine_options, error)) {
        set_status(L"检验仪器加载失败。");
        return;
    }
    search::populate_machine_combo(g_ui.mach, g_machine_options);
}

void show_settings(HWND owner) {
    search::SettingsDialogCallbacks callbacks;
    callbacks.on_test = [](HWND dialog, const search::DbSettings& settings) {
        test_current_settings(dialog, settings);
    };
    callbacks.on_save = [](const search::DbSettings& settings, int font_size) {
        g_db_settings = settings;
        g_font_size = font_size;
        save_settings_to_ini();
        if (g_ui_context.ui_font) {
            DeleteObject(g_ui_context.ui_font);
        }
        g_ui_context.ui_font = search::create_ui_font(g_font_size);
        apply_font_to_window(g_ui_context.main_window);
        reload_room_options();
        reload_patient_type_options();
        reload_machine_options();
        set_status(L"数据库设置已保存。");
    };
    search::show_settings_dialog(owner, g_ui_context.ui_font ? g_ui_context.ui_font : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                                 g_db_settings, g_font_size, callbacks);
}

void query_selected_results(int selected) {
    g_result_rows.clear();
    if (selected < 0 || selected >= static_cast<int>(g_report_rows.size())) {
        ListView_DeleteAllItems(g_ui.results);
        return;
    }
    std::string error;
    if (!search::load_result_rows(g_connection_string, g_report_rows[static_cast<size_t>(selected)].rep_no, g_result_rows, error)) {
        MessageBoxW(nullptr, search::utf8_to_wide(error).c_str(), L"查询项目明细失败", MB_ICONERROR);
        return;
    }
    search::present_result_rows(g_ui, g_result_rows);
}

void run_query() {
    search::QueryInput input = search::build_query_input(g_ui, g_state);

    if (search::build_connection_string_w(g_db_settings).empty()) {
        MessageBoxW(nullptr, L"请先在“设置”中填写数据库连接信息。", L"缺少数据库设置", MB_ICONWARNING);
        return;
    }

    search::clear_result_lists(g_ui);
    g_report_rows.clear();
    g_result_rows.clear();
    set_status(L"正在查询...");

    std::string error;
    if (!search::run_report_query(g_db_settings, input, g_report_rows, g_connection_string, error)) {
        set_status(L"查询失败");
        MessageBoxW(nullptr, search::utf8_to_wide(error).c_str(), L"查询失败", MB_ICONERROR);
        return;
    }

    g_last_query_input = input;
    g_has_last_query_input = true;
    search::present_report_rows(g_ui, g_report_rows);
    set_status(search::utf8_to_wide(search::make_query_count_status(g_report_rows.size())));
}

void show_trend(HWND owner) {
    if (search::build_connection_string_w(g_db_settings).empty()) {
        MessageBoxW(owner, L"请先在“设置”中填写数据库连接信息。", L"缺少数据库设置", MB_ICONWARNING);
        return;
    }
    if (!g_has_last_query_input || g_report_rows.empty()) {
        MessageBoxW(owner, L"请先查询出同一病人的结果后再打开趋势图。", L"趋势图提示", MB_ICONWARNING);
        return;
    }

    search::QueryInput input = g_last_query_input;
    if (search::trim(input.patient_name).empty() && search::trim(input.patient_no).empty()) {
        MessageBoxW(owner, L"上次查询未使用病人姓名或病人号，请先按病人姓名或病人号查询后再打开趋势图。", L"趋势图提示", MB_ICONWARNING);
        return;
    }
    search::show_trend_window(owner,
                              g_ui_context.ui_font ? g_ui_context.ui_font : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                              g_db_settings,
                              input);
}

void create_ui(HWND hwnd) {
    HFONT font = g_ui_context.ui_font ? g_ui_context.ui_font : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    search::create_main_controls(hwnd, font, g_main_ui_ids, g_ui);
    search::set_date_picker_today(g_ui.start);
    search::set_date_picker_today(g_ui.end);
    search::initialize_report_status_combo(g_ui.report_status);
    reload_room_options();
    reload_patient_type_options();
    reload_machine_options();
    search::layout_main_window(hwnd, g_ui, g_splitter_x);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_CREATE:
            create_ui(hwnd);
            return 0;
        case WM_SIZE:
            search::layout_main_window(hwnd, g_ui, g_splitter_x);
            return 0;
        case WM_DPICHANGED: {
            auto* rect = reinterpret_cast<RECT*>(lparam);
            SetWindowPos(hwnd, nullptr, rect->left, rect->top,
                         rect->right - rect->left, rect->bottom - rect->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_COMMAND: {
            search::CommandEventHandlers handlers;
            handlers.on_room_changed = [] { reload_machine_options(); };
            handlers.on_query = [] { run_query(); };
            handlers.on_show_trend = [](HWND owner) { show_trend(owner); };
            handlers.on_show_settings = [](HWND owner) { show_settings(owner); };
            handlers.on_unimplemented_action = [](HWND owner) {
                MessageBoxW(owner, L"该功能暂未实现。", L"提示", MB_ICONINFORMATION);
            };
            handlers.on_exit = [](HWND owner) { DestroyWindow(owner); };
            if (search::handle_command(hwnd, wparam, g_main_ui_ids, handlers)) {
                return 0;
            }
            break;
        }
        case WM_NOTIFY: {
            search::NotifyEventHandlers handlers;
            handlers.on_report_selected = [](int index) { query_selected_results(index); };
            handlers.report_row_background = [](const search::ReportRow& row) { return report_row_background(row); };
            handlers.result_row_color = [](const search::ResultRow& row) { return result_row_color(row); };
            handlers.report_rows = &g_report_rows;
            handlers.result_rows = &g_result_rows;
            LRESULT result = 0;
            if (search::handle_notify(lparam, g_main_ui_ids, handlers, result)) {
                return result;
            }
            break;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    enable_dpi_awareness();
    load_settings();
    g_ui_context.ui_font = search::create_ui_font(g_font_size);
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_DATE_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"LISWorkbenchQueryWindow";
    RegisterClassExW(&wc);

    WNDCLASSEXW splitter_wc{};
    splitter_wc.cbSize = sizeof(splitter_wc);
    splitter_wc.lpfnWndProc = splitter_proc;
    splitter_wc.hInstance = instance;
    splitter_wc.hCursor = LoadCursor(nullptr, IDC_SIZEWE);
    splitter_wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_3DSHADOW + 1);
    splitter_wc.lpszClassName = L"LISWorkbenchSplitter";
    RegisterClassExW(&splitter_wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, search::kAppTitle,
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1480, 760,
                                nullptr, nullptr, instance, nullptr);
    g_ui_context.main_window = hwnd;
    ShowWindow(hwnd, SW_MAXIMIZE);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_ui_context.ui_font) {
        DeleteObject(g_ui_context.ui_font);
        g_ui_context.ui_font = nullptr;
    }
    return static_cast<int>(msg.wParam);
}

#endif
