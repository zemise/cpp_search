#include "immune_duplicate_statistics_module.h"

#ifdef _WIN32

#include "main_app.h"
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
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const wchar_t* WND_CLASS = L"ImmuneDuplicateStatisticsModuleChild";
constexpr const wchar_t* WINDOW_TITLE = L"免疫重复项目统计";
constexpr const wchar_t* PROP_STATE = L"ImmuneDuplicateStatisticsSt";
constexpr int IDM_COPY_CELL = 9101;
constexpr UINT WM_IMMUNE_DUPLICATE_LOADED = WM_APP + 0x574;

enum ControlId {
    IDC_START_TIME = 6901,
    IDC_END_TIME = 6902,
    IDC_QUERY = 6903,
    IDC_EXPORT = 6904,
    IDC_SUMMARY = 6905,
    IDC_DETAILS = 6906,
    IDC_STATUS = 6907,
};

struct ListColumn {
    const wchar_t* title;
    int width;
};

constexpr ListColumn SUMMARY_COLUMNS[] = {
    {L"基准条码数", 180},
    {L"基准患者数", 180},
    {L"疑似重复患者数", 220},
    {L"疑似重复条码数", 220},
    {L"疑似重复项目数", 220},
    {L"同条码重复数", 200},
    {L"跨条码重复数", 200},
};

constexpr ListColumn DETAIL_COLUMNS[] = {
    {L"病人号", 120},
    {L"姓名", 100},
    {L"类型", 80},
    {L"科室", 160},
    {L"床号", 70},
    {L"基准条码", 130},
    {L"基准样本号", 110},
    {L"基准医嘱", 260},
    {L"基准签收时间", 150},
    {L"重复条码", 130},
    {L"重复样本号", 110},
    {L"重复项目代码", 120},
    {L"重复项目名称", 180},
    {L"重复类别", 130},
    {L"结果", 120},
    {L"单位", 80},
    {L"重复签收时间", 150},
    {L"报告时间", 150},
    {L"审核", 60},
    {L"发送", 60},
    {L"重复关系", 90},
};

using Summary = search::ImmuneDuplicateStatSummary;
using DetailRow = search::ImmuneDuplicateStatDetailRow;

struct ImmuneDuplicateState {
    ModuleContext ctx;
    HWND startTimeLabel = nullptr;
    HWND endTimeLabel = nullptr;
    HWND startDate = nullptr;
    HWND endDate = nullptr;
    HWND query = nullptr;
    HWND exportCsv = nullptr;
    HWND summaryList = nullptr;
    HWND details = nullptr;
    HWND status = nullptr;
    HBRUSH bgBrush = nullptr;
    bool querying = false;
    int sortColumn = -1;
    bool sortAscending = true;
    Summary summary;
    std::vector<DetailRow> rows;
};

struct ImmuneDuplicateQueryResult {
    bool ok = false;
    std::string error;
    Summary summary;
    std::vector<DetailRow> rows;
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

void setToday(HWND hwnd, bool endOfDay) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    st.wHour = endOfDay ? 23 : 0;
    st.wMinute = endOfDay ? 59 : 0;
    st.wSecond = 0;
    st.wMilliseconds = 0;
    DateTime_SetSystemtime(hwnd, GDT_VALID, &st);
}

std::string dateTimeText(HWND hwnd) {
    SYSTEMTIME st{};
    if (DateTime_GetSystemtime(hwnd, &st) != GDT_VALID) return "";
    char buf[32]{};
    sprintf_s(buf, "%04u-%02u-%02u %02u:%02u:%02u",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

void setStatus(ImmuneDuplicateState* st, const std::wstring& text) {
    if (st && st->status) SetWindowTextW(st->status, text.c_str());
}

void setCellUtf8(HWND list, int row, int col, const std::string& text) {
    const auto wide = search::utf8_to_wide(text);
    ListView_SetItemText(list, row, col, const_cast<wchar_t*>(wide.c_str()));
}

void initList(HWND list, const ListColumn* columns, int count) {
    for (int i = 0; i < count; ++i) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.fmt = LVCFMT_LEFT;
        col.cx = columns[i].width;
        col.pszText = const_cast<wchar_t*>(columns[i].title);
        ListView_InsertColumn(list, i, &col);
    }
}

void setSummaryValue(HWND list, int col, int value) {
    const auto text = std::to_wstring(value);
    ListView_SetItemText(list, 0, col, const_cast<wchar_t*>(text.c_str()));
}

int summaryValue(const Summary& summary, int col) {
    switch (col) {
        case 0: return summary.base_barcode_count;
        case 1: return summary.base_patient_count;
        case 2: return summary.duplicate_patient_count;
        case 3: return summary.duplicate_barcode_count;
        case 4: return summary.duplicate_item_count;
        case 5: return summary.same_barcode_duplicate_count;
        case 6: return summary.cross_barcode_duplicate_count;
        default: return 0;
    }
}

void populateSummary(ImmuneDuplicateState* st) {
    if (!st || !st->summaryList) return;
    ListView_DeleteAllItems(st->summaryList);
    const std::wstring firstValue = std::to_wstring(summaryValue(st->summary, 0));
    LVITEMW valueItem{};
    valueItem.mask = LVIF_TEXT;
    valueItem.iItem = 0;
    valueItem.pszText = const_cast<wchar_t*>(firstValue.c_str());
    ListView_InsertItem(st->summaryList, &valueItem);
    for (int i = 1; i < static_cast<int>(std::size(SUMMARY_COLUMNS)); ++i) {
        setSummaryValue(st->summaryList, i, summaryValue(st->summary, i));
    }
}

std::string cellValue(const DetailRow& row, int col) {
    switch (col) {
        case 0: return row.patient_no;
        case 1: return row.name;
        case 2: return row.type_name;
        case 3: return row.department;
        case 4: return row.bed_no;
        case 5: return row.base_barcode;
        case 6: return row.base_sample_no;
        case 7: return row.base_order_text;
        case 8: return row.base_sign_time;
        case 9: return row.duplicate_barcode;
        case 10: return row.duplicate_sample_no;
        case 11: return row.duplicate_item_code;
        case 12: return row.duplicate_item_name;
        case 13: return row.duplicate_category;
        case 14: return row.result;
        case 15: return row.unit;
        case 16: return row.duplicate_sign_time;
        case 17: return row.report_time;
        case 18: return row.reviewed;
        case 19: return row.sent;
        case 20: return row.relation;
        default: return "";
    }
}

void populateDetails(ImmuneDuplicateState* st) {
    if (!st || !st->details) return;
    SendMessageW(st->details, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(st->details);
    for (int i = 0; i < static_cast<int>(st->rows.size()); ++i) {
        const auto& row = st->rows[static_cast<size_t>(i)];
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        const auto patientNo = search::utf8_to_wide(row.patient_no);
        item.pszText = const_cast<wchar_t*>(patientNo.c_str());
        ListView_InsertItem(st->details, &item);
        for (int col = 1; col < static_cast<int>(std::size(DETAIL_COLUMNS)); ++col) {
            setCellUtf8(st->details, i, col, cellValue(row, col));
        }
    }
    SendMessageW(st->details, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(st->details, nullptr, TRUE);
}

void sortRows(ImmuneDuplicateState* st, int column, bool toggle) {
    if (!st) return;
    if (toggle && st->sortColumn == column) {
        st->sortAscending = !st->sortAscending;
    } else {
        st->sortColumn = column;
        st->sortAscending = true;
    }
    const int col = st->sortColumn;
    const bool asc = st->sortAscending;
    std::stable_sort(st->rows.begin(), st->rows.end(), [col, asc](const auto& a, const auto& b) {
        const auto av = cellValue(a, col);
        const auto bv = cellValue(b, col);
        return asc ? av < bv : av > bv;
    });
}

bool copyTextToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) {
        CloseClipboard();
        return false;
    }
    void* ptr = GlobalLock(mem);
    if (!ptr) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }
    memcpy(ptr, text.c_str(), bytes);
    GlobalUnlock(mem);
    if (!SetClipboardData(CF_UNICODETEXT, mem)) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

void showCellContextMenu(HWND hwnd, ImmuneDuplicateState* st) {
    if (!st || !st->details || st->rows.empty()) return;
    POINT screenPt{};
    GetCursorPos(&screenPt);
    POINT listPt = screenPt;
    ScreenToClient(st->details, &listPt);

    LVHITTESTINFO hit{};
    hit.pt = listPt;
    const int row = ListView_SubItemHitTest(st->details, &hit);
    if (row < 0 || hit.iSubItem < 0 || hit.iSubItem >= static_cast<int>(std::size(DETAIL_COLUMNS)) ||
        row >= static_cast<int>(st->rows.size())) {
        return;
    }

    ListView_SetItemState(st->details, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(st->details, row, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);

    const std::wstring text = search::utf8_to_wide(
        cellValue(st->rows[static_cast<size_t>(row)], hit.iSubItem));
    const std::wstring menuLabel = search::copy_menu_label(text);

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, IDM_COPY_CELL, menuLabel.c_str());
    const UINT command = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                        screenPt.x, screenPt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (command != IDM_COPY_CELL) return;

    if (copyTextToClipboard(hwnd, text)) {
        setStatus(st, L"已复制单元格：" + std::wstring(DETAIL_COLUMNS[hit.iSubItem].title));
    } else {
        setStatus(st, L"复制单元格失败。");
    }
}

void resizeLayout(HWND hwnd, ImmuneDuplicateState* st) {
    if (!st) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const int pad = S(hwnd, 10);
    const int labelGap = S(hwnd, 6);
    const int controlGap = S(hwnd, 8);
    const int groupGap = S(hwnd, 18);
    const int controlH = S(hwnd, 25);
    const int labelYOffset = S(hwnd, 2);
    const int firstRow = S(hwnd, 9);
    const int secondRow = S(hwnd, 42);
    const int topH = S(hwnd, 78);
    const int summaryH = S(hwnd, 72);

    int x = pad;
    int labelW = search::measure_control_text_width(hwnd, st->startTimeLabel, 72);
    MoveWindow(st->startTimeLabel, x, firstRow + labelYOffset, labelW, controlH, TRUE);
    x += labelW + labelGap;
    MoveWindow(st->startDate, x, firstRow, S(hwnd, 172), controlH, TRUE);
    x += S(hwnd, 172) + controlGap;
    labelW = search::measure_control_text_width(hwnd, st->endTimeLabel, 20);
    MoveWindow(st->endTimeLabel, x, firstRow + labelYOffset, labelW, controlH, TRUE);
    x += labelW + controlGap;
    MoveWindow(st->endDate, x, firstRow, S(hwnd, 172), controlH, TRUE);

    x = pad;
    MoveWindow(st->query, x, secondRow - S(hwnd, 1), S(hwnd, 64), S(hwnd, 27), TRUE);
    x += S(hwnd, 64) + controlGap;
    MoveWindow(st->exportCsv, x, secondRow - S(hwnd, 1), S(hwnd, 88), S(hwnd, 27), TRUE);
    x += S(hwnd, 88) + groupGap;
    MoveWindow(st->status, x, secondRow + labelYOffset,
               (std::max)(0, w - x - pad), controlH, TRUE);

    MoveWindow(st->summaryList, pad, topH, (std::max)(0, w - pad * 2), summaryH, TRUE);
    MoveWindow(st->details, pad, topH + summaryH + pad,
               (std::max)(0, w - pad * 2),
               (std::max)(S(hwnd, 100), h - topH - summaryH - pad * 2), TRUE);
}

void runQuery(HWND hwnd, ImmuneDuplicateState* st) {
    if (!st || st->querying) return;
    const auto connection = search::build_connection_string_w(st->ctx.dbSettings);
    if (connection.empty()) {
        MessageBoxW(hwnd, L"请先在系统设置中配置数据库连接。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }

    search::ImmuneDuplicateStatQuery query;
    query.connection_string = search::wide_to_utf8(connection);
    query.start_time = dateTimeText(st->startDate);
    query.end_time = dateTimeText(st->endDate);
    if (query.start_time > query.end_time) {
        MessageBoxW(hwnd, L"签收开始时间不能晚于结束时间。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }

    st->querying = true;
    EnableWindow(st->query, FALSE);
    EnableWindow(st->exportCsv, FALSE);
    setStatus(st, L"正在查询免疫重复项目...");

    std::thread([hwnd, query]() {
        auto* result = new ImmuneDuplicateQueryResult();
        result->ok = search::query_immune_duplicate_statistics(
            query, result->summary, result->rows, result->error);
        if (!PostMessageW(hwnd, WM_IMMUNE_DUPLICATE_LOADED, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }).detach();
}

void exportCsv(HWND hwnd, ImmuneDuplicateState* st) {
    if (!st || st->rows.empty()) {
        setStatus(st, L"当前没有可导出的明细。");
        return;
    }
    MessageBoxW(hwnd, L"当前阶段暂未接入明细导出。", WINDOW_TITLE, MB_ICONINFORMATION);
}

void openRegularReportForRow(HWND owner, ImmuneDuplicateState* st, int index) {
    if (!st || index < 0 || index >= static_cast<int>(st->rows.size())) return;
    const auto& row = st->rows[static_cast<size_t>(index)];
    if (search::trim(row.report_no).empty() || search::trim(row.machine_code).empty() ||
        search::trim(row.inspect_date).empty()) {
        MessageBoxW(owner, L"当前明细缺少报告号、仪器或日期，无法跳转到常规报告。", WINDOW_TITLE, MB_ICONINFORMATION);
        return;
    }

    auto* target = new RegularReportOpenTarget;
    target->rep_no = search::trim(row.report_no);
    target->oper_no = search::trim(row.duplicate_sample_no);
    target->inspect_date = search::trim(row.inspect_date);
    target->mach_code = search::trim(row.machine_code);
    target->mach_name = search::trim(row.machine_name);
    target->room_code = search::trim(row.room_code);

    HWND regular = create_regular_report_module(st->ctx);
    if (!regular || !PostMessageW(regular, WM_REGULAR_OPEN_REPORT, 0, reinterpret_cast<LPARAM>(target))) {
        delete target;
        MessageBoxW(owner, L"常规报告页面打开失败。", WINDOW_TITLE, MB_ICONERROR);
    }
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<ImmuneDuplicateState*>(GetPropW(hwnd, PROP_STATE));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* mcs = reinterpret_cast<MDICREATESTRUCTW*>(cs->lpCreateParams);
            st = reinterpret_cast<ImmuneDuplicateState*>(mcs->lParam);
            SetPropW(hwnd, PROP_STATE, st);
            st->bgBrush = CreateSolidBrush(RGB(0xF0, 0xF0, 0xF0));

            st->startTimeLabel = label(hwnd, L"签收时间：", 0, 0, 0, 0);
            st->startDate = dateTimePicker(hwnd, IDC_START_TIME, S(hwnd, 92), S(hwnd, 10), S(hwnd, 172), S(hwnd, 24));
            st->endTimeLabel = label(hwnd, L"至", 0, 0, 0, 0, SS_CENTER);
            st->endDate = dateTimePicker(hwnd, IDC_END_TIME, S(hwnd, 300), S(hwnd, 10), S(hwnd, 172), S(hwnd, 24));
            setToday(st->startDate, false);
            setToday(st->endDate, true);
            st->query = search::create_button(hwnd, IDC_QUERY, L"查询", S(hwnd, 486), S(hwnd, 9), S(hwnd, 56), S(hwnd, 26));
            st->exportCsv = search::create_button(hwnd, IDC_EXPORT, L"导出CSV", S(hwnd, 550), S(hwnd, 9), S(hwnd, 82), S(hwnd, 26));
            EnableWindow(st->exportCsv, FALSE);
            st->status = label(hwnd, L"请选择签收时间后查询。", S(hwnd, 646), S(hwnd, 12), S(hwnd, 520), S(hwnd, 24), SS_LEFT);

            st->summaryList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_SUMMARY), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(st->summaryList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            initList(st->summaryList, SUMMARY_COLUMNS, static_cast<int>(std::size(SUMMARY_COLUMNS)));

            st->details = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_DETAILS), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(st->details, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            initList(st->details, DETAIL_COLUMNS, static_cast<int>(std::size(DETAIL_COLUMNS)));

            search::apply_font_to_children(hwnd, st->ctx.uiFont);
            populateSummary(st);
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
            break;
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (st && nm->idFrom == IDC_DETAILS && nm->code == LVN_COLUMNCLICK) {
                auto* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                sortRows(st, lv->iSubItem, true);
                populateDetails(st);
                return 0;
            }
            if (st && nm->idFrom == IDC_DETAILS && nm->code == NM_RCLICK) {
                showCellContextMenu(hwnd, st);
                return 0;
            }
            if (st && nm->idFrom == IDC_DETAILS && nm->code == NM_DBLCLK) {
                auto* item = reinterpret_cast<NMITEMACTIVATE*>(lp);
                openRegularReportForRow(hwnd, st, item->iItem);
                return 0;
            }
            break;
        }
        case WM_IMMUNE_DUPLICATE_LOADED: {
            std::unique_ptr<ImmuneDuplicateQueryResult> result(
                reinterpret_cast<ImmuneDuplicateQueryResult*>(lp));
            if (!st) return 0;
            st->querying = false;
            EnableWindow(st->query, TRUE);
            EnableWindow(st->exportCsv, FALSE);
            if (!result->ok) {
                setStatus(st, L"查询失败：" + search::utf8_to_wide(result->error));
                MessageBoxW(hwnd, search::utf8_to_wide(result->error).c_str(), WINDOW_TITLE, MB_ICONERROR);
                return 0;
            }

            st->summary = result->summary;
            st->rows = std::move(result->rows);
            st->sortColumn = -1;
            st->sortAscending = true;
            populateSummary(st);
            populateDetails(st);

            wchar_t status[320]{};
            if (st->summary.base_barcode_count == 0) {
                swprintf(status, std::size(status), L"该时间段未找到输血常规检查条码。");
            } else if (st->summary.duplicate_item_count == 0) {
                swprintf(status, std::size(status),
                         L"已找到 %d 个基准条码、%d 次住院，未发现疑似重复项目。",
                         st->summary.base_barcode_count, st->summary.base_patient_count);
            } else {
                swprintf(status, std::size(status),
                         L"查询完成：基准条码 %d，疑似重复住院 %d，重复项目 %d。",
                         st->summary.base_barcode_count,
                         st->summary.duplicate_patient_count,
                         st->summary.duplicate_item_count);
            }
            std::wstring status_text(status);
            if (st->summary.missing_base_barcode_count > 0) {
                status_text += L" 无条码基准申请 " +
                               std::to_wstring(st->summary.missing_base_barcode_count) + L" 条。";
            }
            if (st->summary.unmatched_inpatient_count > 0) {
                status_text += L" 未匹配住院信息 " +
                               std::to_wstring(st->summary.unmatched_inpatient_count) + L" 次。";
            }
            setStatus(st, status_text);
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
                if (st->bgBrush) DeleteObject(st->bgBrush);
                RemovePropW(hwnd, PROP_STATE);
                delete st;
            }
            return 0;
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND create_immune_duplicate_statistics_module(const ModuleContext& ctx) {
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

    auto* st = new ImmuneDuplicateState();
    st->ctx = ctx;

    MDICREATESTRUCTW mcs{};
    mcs.szTitle = WINDOW_TITLE;
    mcs.szClass = WND_CLASS;
    mcs.hOwner = ctx.instance;
    mcs.x = CW_USEDEFAULT;
    mcs.y = CW_USEDEFAULT;
    mcs.cx = CW_USEDEFAULT;
    mcs.cy = CW_USEDEFAULT;
    mcs.lParam = reinterpret_cast<LPARAM>(st);
    HWND child = reinterpret_cast<HWND>(SendMessageW(ctx.mdiClient, WM_MDICREATE, 0, reinterpret_cast<LPARAM>(&mcs)));
    if (!child) {
        delete st;
        MessageBoxW(ctx.mdiClient, L"免疫重复项目统计窗口创建失败。", WINDOW_TITLE, MB_ICONERROR);
        return nullptr;
    }
    SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    return child;
}

#endif
