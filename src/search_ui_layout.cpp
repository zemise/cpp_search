#include "search_ui_layout.h"
#include "search_ui_columns.h"
#include "win32_control_id.h"

#ifdef _WIN32

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <iterator>

namespace search {

float dpi_scale_factor(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static auto get_dpi_for_window = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (get_dpi_for_window) {
        return get_dpi_for_window(hwnd) / 96.0f;
    }

    HDC dc = GetDC(hwnd);
    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) {
        ReleaseDC(hwnd, dc);
    }
    return std::max(1, dpi) / 96.0f;
}

int measure_control_text_width(HWND parent, HWND control, int minimumLogicalWidth,
                               int horizontalPaddingLogical) {
    const auto scale = [parent](int value) {
        return static_cast<int>(value * dpi_scale_factor(parent));
    };
    if (!parent || !control) return scale(minimumLogicalWidth);

    wchar_t text[256]{};
    const int textLength = GetWindowTextW(control, text, static_cast<int>(std::size(text)));
    HDC dc = GetDC(parent);
    if (!dc) return scale(minimumLogicalWidth);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0));
    if (!font) font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ oldFont = SelectObject(dc, font);
    SIZE size{};
    GetTextExtentPoint32W(dc, text, textLength, &size);
    SelectObject(dc, oldFont);
    ReleaseDC(parent, dc);
    return std::max(scale(minimumLogicalWidth),
                    static_cast<int>(size.cx) + scale(horizontalPaddingLogical));
}

int clamp_font_size(int value) {
    return std::max(8, std::min(24, value));
}

HFONT create_ui_font(int pointSize) {
    NONCLIENTMETRICSW nm{};
    nm.cbSize = sizeof(nm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(nm), &nm, 0);
    LOGFONTW lf = nm.lfMessageFont;
    HDC screen = GetDC(nullptr);
    lf.lfHeight = -MulDiv(pointSize, GetDeviceCaps(screen, LOGPIXELSY), 72);
    ReleaseDC(nullptr, screen);
    return CreateFontIndirectW(&lf);
}

HWND create_label(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_RIGHT,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND create_groupbox(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND create_edit(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                           x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

HWND create_date_picker(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowExW(0, DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATEFORMAT,
                           x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

HWND create_combo(HWND parent, int id, int x, int y, int w, int h, bool editable) {
    const DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                        (editable ? CBS_DROPDOWN : CBS_DROPDOWNLIST);
    return CreateWindowExW(0, L"COMBOBOX", L"", style,
                           x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

HWND create_password_edit(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
                           x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

HWND create_button(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                           x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

void add_list_column(HWND list, int index, const wchar_t* title, int width) {
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.pszText = const_cast<wchar_t*>(title);
    col.cx = width;
    col.iSubItem = index;
    ListView_InsertColumn(list, index, &col);
}

void create_main_controls(HWND hwnd, HFONT font, const MainUiIds& ids, MainUiHandles& ui) {
    const float s = dpi_scale_factor(hwnd);
    auto S = [s](int v) { return static_cast<int>(v * s); };

    int y = S(16);
    create_label(hwnd, L"诊疗卡号", S(8), y, S(72), S(22)); ui.patient_id = create_edit(hwnd, ids.patient_id, S(88), y, S(120), S(24)); y += S(28);
    create_label(hwnd, L"条码号", S(8), y, S(72), S(22)); ui.barcode = create_edit(hwnd, ids.barcode, S(88), y, S(120), S(24)); y += S(28);
    create_label(hwnd, L"病人姓名", S(8), y, S(72), S(22)); ui.name = create_edit(hwnd, ids.name, S(88), y, S(120), S(24)); y += S(28);
    create_label(hwnd, L"病人号", S(8), y, S(72), S(22)); ui.patient_no = create_edit(hwnd, ids.patient_no, S(88), y, S(120), S(24)); y += S(28);
    create_label(hwnd, L"样本号", S(8), y, S(72), S(22)); ui.oper = create_edit(hwnd, ids.oper, S(88), y, S(120), S(24)); y += S(28);
    create_label(hwnd, L"开始日期", S(8), y, S(72), S(22)); ui.start = create_date_picker(hwnd, ids.start, S(88), y, S(120), S(24)); y += S(28);
    create_label(hwnd, L"结束日期", S(8), y, S(72), S(22)); ui.end = create_date_picker(hwnd, ids.end, S(88), y, S(120), S(24)); y += S(28);
    ui.group_test = create_groupbox(hwnd, L"检验条件", S(8), y - S(2), S(202), S(78));
    create_label(hwnd, L"检验科室", S(14), y + S(18), S(66), S(22)); ui.room = create_combo(hwnd, ids.room, S(88), y + S(16), S(120), S(180), false);
    create_label(hwnd, L"检验仪器", S(14), y + S(46), S(66), S(22)); ui.mach = create_combo(hwnd, ids.mach, S(88), y + S(44), S(120), S(180), false);
    y += S(84);
    ui.group_patient = create_groupbox(hwnd, L"病人条件", S(8), y - S(2), S(202), S(78));
    create_label(hwnd, L"病人类型", S(14), y + S(18), S(66), S(22)); ui.patient_type = create_combo(hwnd, ids.patient_type, S(88), y + S(16), S(120), S(180), false);
    create_label(hwnd, L"报告状态", S(14), y + S(46), S(66), S(22)); ui.report_status = create_combo(hwnd, ids.report_status, S(88), y + S(44), S(120), S(180), false);
    y += S(84);
    create_label(hwnd, L"组合项目", S(8), y, S(72), S(22)); ui.group = create_edit(hwnd, ids.group, S(88), y, S(120), S(24)); y += S(28);
    create_label(hwnd, L"项目代码", S(8), y, S(72), S(22)); ui.item = create_edit(hwnd, ids.item, S(88), y, S(120), S(24));

    const int reports_x = S(220);
    const int list_top = S(8);
    const int reports_width = S(820);
    const int list_height = S(685);
    const int results_x = S(1048);
    const int results_width = S(410);
    const int buttons_y = S(714);

    ui.reports = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                 WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                 reports_x, list_top, reports_width, list_height,
                                 hwnd, win32_control_id(ids.reports), GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(ui.reports, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    add_list_column(ui.reports, report_columns::SampleNo, L"样本号", S(70));
    add_list_column(ui.reports, report_columns::Name, L"姓名", S(80));
    add_list_column(ui.reports, report_columns::Barcode, L"条码号", S(110));
    add_list_column(ui.reports, report_columns::ReportTime, L"上机时间", S(130));
    add_list_column(ui.reports, report_columns::Sex, L"性别", S(50));
    add_list_column(ui.reports, report_columns::Age, L"年龄", S(60));
    add_list_column(ui.reports, report_columns::Bed, L"床号", S(60));
    add_list_column(ui.reports, report_columns::PatientType, L"病人类型", S(70));
    add_list_column(ui.reports, report_columns::Requester, L"检验者", S(80));
    add_list_column(ui.reports, report_columns::Reviewer, L"审核者", S(80));
    add_list_column(ui.reports, report_columns::GroupName, L"项目名称", S(110));
    add_list_column(ui.reports, report_columns::ReviewStatus, L"审核", S(60));
    add_list_column(ui.reports, report_columns::ConfirmStatus, L"确认", S(60));
    add_list_column(ui.reports, report_columns::PrintStatus, L"打印", S(60));
    add_list_column(ui.reports, report_columns::SelfServicePrintStatus, L"自助机", S(60));

    ui.splitter = CreateWindowExW(0, L"LISWorkbenchSplitter", L"",
                                  WS_CHILD | WS_VISIBLE,
                                  S(1040), list_top, S(8), list_height,
                                  hwnd, win32_control_id(ids.splitter), GetModuleHandleW(nullptr), nullptr);

    ui.results = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                 WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                 results_x, list_top, results_width, list_height,
                                 hwnd, win32_control_id(ids.results), GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(ui.results, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    add_list_column(ui.results, result_columns::ItemName, L"项目名称", S(150));
    add_list_column(ui.results, result_columns::Result, L"结果", S(80));
    add_list_column(ui.results, result_columns::LowerBound, L"下限", S(70));
    add_list_column(ui.results, result_columns::UpperBound, L"上限", S(70));
    add_list_column(ui.results, result_columns::Unit, L"单位", S(70));
    add_list_column(ui.results, result_columns::EnglishName, L"英文名称", S(80));

    ui.settings_button = create_button(hwnd, ids.settings, L"设置", S(620), buttons_y, S(92), S(32));
    ui.query_button = create_button(hwnd, ids.query, L"查询(&Q)", S(720), buttons_y, S(92), S(32));
    ui.trend_button = create_button(hwnd, ids.trend, L"趋势图(&T)", S(820), buttons_y, S(92), S(32));
    ui.export_button = create_button(hwnd, ids.export_, L"导出(&E)", S(920), buttons_y, S(92), S(32));
    ui.preview_button = create_button(hwnd, ids.preview, L"预览(&V)", S(1020), buttons_y, S(92), S(32));
    ui.print_button = create_button(hwnd, ids.print, L"打印(&P)", S(1120), buttons_y, S(92), S(32));
    ui.exit_button = create_button(hwnd, ids.exit, L"退出(&X)", S(1220), buttons_y, S(92), S(32));
    ui.status = CreateWindowExW(0, L"STATIC", L"请输入条件后查询。", WS_CHILD | WS_VISIBLE,
                                S(8), buttons_y + S(2), S(600), S(28), hwnd, win32_control_id(ids.status), GetModuleHandleW(nullptr), nullptr);

    EnumChildWindows(hwnd, [](HWND child, LPARAM param) -> BOOL {
        SendMessageW(child, WM_SETFONT, param, TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(font));
}

void layout_main_window(HWND hwnd, MainUiHandles& ui, int& splitter_x) {
    if (!hwnd || !ui.reports || !ui.results) {
        return;
    }

    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int client_w = rc.right - rc.left;
    const int client_h = rc.bottom - rc.top;

    const float s = dpi_scale_factor(hwnd);
    auto S = [s](int v) { return static_cast<int>(v * s); };

    const int margin = S(8);
    const int left_x = S(8);
    const int left_input_x = S(88);
    const int left_input_w = S(120);
    const int left_panel_w = S(202);
    const int edit_h = S(24);
    const int row_gap = S(28);
    const int group_h = S(78);
    const int button_w = S(92);
    const int button_h = S(32);
    const int button_gap = S(8);
    const int status_w = S(600);
    const int bottom_margin = S(10);
    const int splitter_w = S(8);
    const int reports_x = S(220);
    const int min_reports_w = S(520);
    const int min_results_w = S(330);
    const int default_results_w = S(410);

    const int buttons_y = client_h - bottom_margin - button_h;
    const int status_y = buttons_y + 2;  // fine-tuned baseline offset
    const int list_top = S(8);
    const int list_bottom = buttons_y - S(12);
    const int list_h = std::max(S(120), list_bottom - list_top);
    const int min_splitter_x = reports_x + min_reports_w;
    const int max_splitter_x = std::max(min_splitter_x, client_w - margin - min_results_w - splitter_w);
    if (splitter_x <= 0) {
        splitter_x = client_w - margin - default_results_w - splitter_w;
    }
    splitter_x = std::max(min_splitter_x, std::min(max_splitter_x, splitter_x));
    const int reports_w = splitter_x - reports_x;
    const int results_x = splitter_x + splitter_w;
    const int results_w = std::max(min_results_w, client_w - margin - results_x);

    SendMessageW(ui.reports, WM_SETREDRAW, FALSE, 0);
    SendMessageW(ui.results, WM_SETREDRAW, FALSE, 0);

    HDWP hdwp = BeginDeferWindowPos(26);
    #define DWP(h, x, y, w, hh) if (h) hdwp = DeferWindowPos(hdwp, h, nullptr, x, y, w, hh, SWP_NOZORDER | SWP_NOCOPYBITS)

    int y = S(16);
    DWP(ui.patient_id, left_input_x, y, left_input_w, edit_h); y += row_gap;
    DWP(ui.barcode, left_input_x, y, left_input_w, edit_h); y += row_gap;
    DWP(ui.name, left_input_x, y, left_input_w, edit_h); y += row_gap;
    DWP(ui.patient_no, left_input_x, y, left_input_w, edit_h); y += row_gap;
    DWP(ui.oper, left_input_x, y, left_input_w, edit_h); y += row_gap;
    DWP(ui.start, left_input_x, y, left_input_w, edit_h); y += row_gap;
    DWP(ui.end, left_input_x, y, left_input_w, edit_h); y += row_gap;

    DWP(ui.group_test, left_x, y - S(2), left_panel_w, group_h);
    DWP(ui.room, left_input_x, y + S(16), left_input_w, edit_h + S(200));
    DWP(ui.mach, left_input_x, y + S(44), left_input_w, edit_h + S(200));
    y += S(84);

    DWP(ui.group_patient, left_x, y - S(2), left_panel_w, group_h);
    DWP(ui.patient_type, left_input_x, y + S(16), left_input_w, edit_h + S(200));
    DWP(ui.report_status, left_input_x, y + S(44), left_input_w, edit_h + S(200));
    y += S(84);

    DWP(ui.group, left_input_x, y, left_input_w, edit_h); y += row_gap;
    DWP(ui.item, left_input_x, y, left_input_w, edit_h);

    DWP(ui.reports, reports_x, list_top, reports_w, list_h);
    DWP(ui.splitter, splitter_x, list_top, splitter_w, list_h);
    DWP(ui.results, results_x, list_top, results_w, list_h);

    const int buttons_start_x = client_w - margin - (button_w * 7 + button_gap * 6);
    DWP(ui.settings_button, buttons_start_x, buttons_y, button_w, button_h);
    DWP(ui.query_button, buttons_start_x + (button_w + button_gap) * 1, buttons_y, button_w, button_h);
    DWP(ui.trend_button, buttons_start_x + (button_w + button_gap) * 2, buttons_y, button_w, button_h);
    DWP(ui.export_button, buttons_start_x + (button_w + button_gap) * 3, buttons_y, button_w, button_h);
    DWP(ui.preview_button, buttons_start_x + (button_w + button_gap) * 4, buttons_y, button_w, button_h);
    DWP(ui.print_button, buttons_start_x + (button_w + button_gap) * 5, buttons_y, button_w, button_h);
    DWP(ui.exit_button, buttons_start_x + (button_w + button_gap) * 6, buttons_y, button_w, button_h);
    DWP(ui.status, margin, status_y, std::min(status_w, buttons_start_x - margin - S(8)), S(28));

    #undef DWP
    EndDeferWindowPos(hdwp);

    SendMessageW(ui.reports, WM_SETREDRAW, TRUE, 0);
    SendMessageW(ui.results, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(ui.reports, nullptr, FALSE);
    InvalidateRect(ui.results, nullptr, FALSE);
}

void apply_font_to_children(HWND root, HFONT font) {
    if (!root || !font) return;
    SendMessageW(root, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    EnumChildWindows(root, [](HWND child, LPARAM p) -> BOOL {
        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(p), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(font));
}

}  // namespace search

#endif
