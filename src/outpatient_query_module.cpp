#include "outpatient_query_module.h"

#ifdef _WIN32

#include "main_app.h"
#include "resource.h"
#include "search_core.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"

#include <commctrl.h>
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const wchar_t* WND_CLASS = L"OutpatientQueryModuleChild";
constexpr const wchar_t* WINDOW_TITLE = L"门诊查询";
constexpr const wchar_t* PROP_STATE = L"OutpatientQuerySt";
constexpr UINT WM_OUTPATIENT_QUERY_LOADED = WM_APP + 0x581;
constexpr COLORREF COLOR_PENDING_BARCODE = RGB(0xFF, 0xFF, 0x54);
constexpr COLORREF COLOR_WHITE = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF COLOR_BLACK = RGB(0x00, 0x00, 0x00);
constexpr UINT IDM_COPY_CELL = 6951;

enum ControlId {
    IDC_START_TIME = 6901,
    IDC_END_TIME = 6902,
    IDC_LAB_DEPARTMENT = 6903,
    IDC_OUTPATIENT_NO = 6904,
    IDC_PATIENT_NAME = 6905,
    IDC_ID_CARD = 6906,
    IDC_QUERY = 6907,
    IDC_LIST = 6908,
    IDC_STATUS = 6909,
    IDC_INCLUDE_NON_LAB = 6910,
};

struct ListColumn {
    const wchar_t* title;
    int width;
};

constexpr ListColumn COLUMNS[] = {
    {L"门诊号", 110},
    {L"发票号", 120},
    {L"卡号", 120},
    {L"姓名", 90},
    {L"性别", 52},
    {L"年龄", 64},
    {L"项目名称", 230},
    {L"申请科室", 120},
    {L"单价", 80},
    {L"总数", 70},
    {L"单位", 70},
    {L"金额", 86},
    {L"收费时间", 150},
    {L"条码号", 120},
    {L"标本类型", 110},
    {L"条码打印时间", 150},
};

struct OutpatientQueryState {
    ModuleContext ctx;
    HWND startLabel = nullptr;
    HWND startDate = nullptr;
    HWND endLabel = nullptr;
    HWND endDate = nullptr;
    HWND labDepartmentLabel = nullptr;
    HWND labDepartment = nullptr;
    HWND includeNonLab = nullptr;
    HWND outpatientNoLabel = nullptr;
    HWND outpatientNo = nullptr;
    HWND patientNameLabel = nullptr;
    HWND patientName = nullptr;
    HWND idCardLabel = nullptr;
    HWND idCard = nullptr;
    HWND query = nullptr;
    HWND list = nullptr;
    HWND status = nullptr;
    HBRUSH bgBrush = nullptr;
    bool querying = false;
    int sortColumn = 12;
    bool sortAscending = false;
    std::vector<search::OutpatientChargeRow> rows;
};

struct QueryResult {
    bool ok = false;
    std::string error;
    std::vector<search::OutpatientChargeRow> rows;
};

int S(HWND hwnd, int value) {
    return static_cast<int>(value * search::dpi_scale_factor(hwnd));
}

HWND label(HWND parent, const wchar_t* text, int x, int y, int w, int h, DWORD align = SS_RIGHT) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | align,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND edit(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                           x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

HWND dateTimePicker(HWND parent, int id, int x, int y, int w, int h) {
    HWND hwnd = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATECENTURYFORMAT,
                                x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
    DateTime_SetFormat(hwnd, L"yyyy-MM-dd HH:mm");
    return hwnd;
}

std::string dateTimeText(HWND hwnd) {
    SYSTEMTIME st{};
    if (DateTime_GetSystemtime(hwnd, &st) != GDT_VALID) return "";
    char buf[32]{};
    sprintf_s(buf, "%04u-%02u-%02u %02u:%02u:%02u",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
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

std::wstring controlText(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    if (len > 0) {
        GetWindowTextW(hwnd, text.data(), len + 1);
    }
    text.resize(static_cast<size_t>(len));
    return text;
}

std::wstring comboText(HWND combo) {
    const int idx = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (idx < 0) return {};
    wchar_t buf[64]{};
    SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(idx), reinterpret_cast<LPARAM>(buf));
    return buf;
}

void setStatus(OutpatientQueryState* st, const std::wstring& text) {
    if (st && st->status) SetWindowTextW(st->status, text.c_str());
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
    void* locked = GlobalLock(mem);
    if (!locked) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }
    std::memcpy(locked, text.c_str(), bytes);
    GlobalUnlock(mem);
    if (!SetClipboardData(CF_UNICODETEXT, mem)) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

void setCellUtf8(HWND list, int row, int col, const std::string& text) {
    const auto wide = search::utf8_to_wide(text);
    ListView_SetItemText(list, row, col, const_cast<wchar_t*>(wide.c_str()));
}

COLORREF rowBackColor(const search::OutpatientChargeRow& row) {
    return search::trim(row.barcode) == "未生成" ? COLOR_PENDING_BARCODE : COLOR_WHITE;
}

void initList(HWND list) {
    for (int i = 0; i < static_cast<int>(std::size(COLUMNS)); ++i) {
        search::add_list_column(list, i, COLUMNS[i].title, COLUMNS[i].width);
    }
}

std::string sortValue(const search::OutpatientChargeRow& row, int col) {
    switch (col) {
        case 0: return row.outpatient_no;
        case 1: return row.invoice_no;
        case 2: return row.card_no;
        case 3: return row.name;
        case 4: return row.sex;
        case 5: return row.age;
        case 6: return row.item_name;
        case 7: return row.application_department;
        case 8: return row.unit_price;
        case 9: return row.quantity;
        case 10: return row.unit;
        case 11: return row.amount;
        case 12: return row.charge_time;
        case 13: return row.barcode;
        case 14: return row.sample_name;
        case 15: return row.barcode_print_time;
        default: return row.charge_time;
    }
}

void sortRows(OutpatientQueryState* st, int column, bool toggle) {
    if (!st) return;
    if (toggle && st->sortColumn == column) {
        st->sortAscending = !st->sortAscending;
    } else {
        st->sortColumn = column;
        st->sortAscending = column != 12;
    }
    const int col = st->sortColumn;
    const bool asc = st->sortAscending;
    std::stable_sort(st->rows.begin(), st->rows.end(), [col, asc](const auto& a, const auto& b) {
        const auto av = sortValue(a, col);
        const auto bv = sortValue(b, col);
        return asc ? av < bv : av > bv;
    });
}

void populateList(OutpatientQueryState* st) {
    if (!st || !st->list) return;
    SendMessageW(st->list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(st->list);
    for (int i = 0; i < static_cast<int>(st->rows.size()); ++i) {
        const auto& row = st->rows[static_cast<size_t>(i)];
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        const auto outpatientNo = search::utf8_to_wide(row.outpatient_no);
        item.pszText = const_cast<wchar_t*>(outpatientNo.c_str());
        ListView_InsertItem(st->list, &item);
        setCellUtf8(st->list, i, 1, row.invoice_no);
        setCellUtf8(st->list, i, 2, row.card_no);
        setCellUtf8(st->list, i, 3, row.name);
        setCellUtf8(st->list, i, 4, row.sex);
        setCellUtf8(st->list, i, 5, row.age);
        setCellUtf8(st->list, i, 6, row.item_name);
        setCellUtf8(st->list, i, 7, row.application_department);
        setCellUtf8(st->list, i, 8, row.unit_price);
        setCellUtf8(st->list, i, 9, row.quantity);
        setCellUtf8(st->list, i, 10, row.unit);
        setCellUtf8(st->list, i, 11, row.amount);
        setCellUtf8(st->list, i, 12, row.charge_time);
        setCellUtf8(st->list, i, 13, row.barcode);
        setCellUtf8(st->list, i, 14, row.sample_name);
        setCellUtf8(st->list, i, 15, row.barcode_print_time);
    }
    SendMessageW(st->list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(st->list, nullptr, TRUE);
}

void showCellContextMenu(HWND hwnd, OutpatientQueryState* st) {
    if (!st || !st->list) return;
    POINT screenPt{};
    GetCursorPos(&screenPt);
    POINT listPt = screenPt;
    ScreenToClient(st->list, &listPt);

    LVHITTESTINFO hit{};
    hit.pt = listPt;
    const int row = ListView_SubItemHitTest(st->list, &hit);
    if (row < 0 || hit.iSubItem < 0 || hit.iSubItem >= static_cast<int>(std::size(COLUMNS)) ||
        row >= static_cast<int>(st->rows.size())) {
        return;
    }

    ListView_SetItemState(st->list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(st->list, row, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);

    const std::wstring text = search::utf8_to_wide(
        sortValue(st->rows[static_cast<size_t>(row)], hit.iSubItem));
    const std::wstring menuLabel = search::copy_menu_label(text);

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, IDM_COPY_CELL, menuLabel.c_str());
    const UINT command = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                        screenPt.x, screenPt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (command != IDM_COPY_CELL) return;

    if (copyTextToClipboard(hwnd, text)) {
        setStatus(st, L"已复制单元格：" + std::wstring(COLUMNS[hit.iSubItem].title));
    } else {
        setStatus(st, L"复制单元格失败。");
    }
}

void resizeLayout(HWND hwnd, OutpatientQueryState* st) {
    if (!st) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const int pad = S(hwnd, 10);
    const int labelYPad = S(hwnd, 2);
    const int row1 = S(hwnd, 10);
    const int row2 = S(hwnd, 42);
    const int listY = S(hwnd, 82);
    const int editH = S(hwnd, 24);
    const int btnH = S(hwnd, 26);
    const int labelGap = S(hwnd, 6);
    const int groupGap = S(hwnd, 14);

    int x = pad;
    int lw = search::measure_control_text_width(hwnd, st->startLabel, 76);
    MoveWindow(st->startLabel, x, row1 + labelYPad, lw, editH, TRUE); x += lw + labelGap;
    MoveWindow(st->startDate, x, row1, S(hwnd, 160), editH, TRUE); x += S(hwnd, 160) + groupGap;
    lw = search::measure_control_text_width(hwnd, st->endLabel, 24);
    MoveWindow(st->endLabel, x, row1 + labelYPad, lw, editH, TRUE); x += lw + labelGap;
    MoveWindow(st->endDate, x, row1, S(hwnd, 160), editH, TRUE); x += S(hwnd, 160) + groupGap;
    lw = search::measure_control_text_width(hwnd, st->labDepartmentLabel, 48);
    MoveWindow(st->labDepartmentLabel, x, row1 + labelYPad, lw, editH, TRUE); x += lw + labelGap;
    MoveWindow(st->labDepartment, x, row1, S(hwnd, 88), S(hwnd, 160), TRUE); x += S(hwnd, 88) + groupGap;
    MoveWindow(st->includeNonLab, x, row1 + S(hwnd, 2), S(hwnd, 130), editH, TRUE);

    x = pad;
    lw = search::measure_control_text_width(hwnd, st->outpatientNoLabel, 58);
    MoveWindow(st->outpatientNoLabel, x, row2 + labelYPad, lw, editH, TRUE); x += lw + labelGap;
    MoveWindow(st->outpatientNo, x, row2, S(hwnd, 160), editH, TRUE); x += S(hwnd, 160) + groupGap;
    lw = search::measure_control_text_width(hwnd, st->patientNameLabel, 42);
    MoveWindow(st->patientNameLabel, x, row2 + labelYPad, lw, editH, TRUE); x += lw + labelGap;
    MoveWindow(st->patientName, x, row2, S(hwnd, 140), editH, TRUE); x += S(hwnd, 140) + groupGap;
    lw = search::measure_control_text_width(hwnd, st->idCardLabel, 58);
    MoveWindow(st->idCardLabel, x, row2 + labelYPad, lw, editH, TRUE); x += lw + labelGap;
    MoveWindow(st->idCard, x, row2, S(hwnd, 220), editH, TRUE); x += S(hwnd, 220) + groupGap;
    MoveWindow(st->query, x, row2 - S(hwnd, 1), S(hwnd, 70), btnH, TRUE); x += S(hwnd, 70) + groupGap;
    MoveWindow(st->status, x, row2 + labelYPad,
               (std::max)(S(hwnd, 120), w - x - pad), editH, TRUE);

    MoveWindow(st->list, pad, listY, w - pad * 2, (std::max)(S(hwnd, 80), h - listY - pad), TRUE);
}

void runQuery(HWND hwnd, OutpatientQueryState* st) {
    if (!st || st->querying) return;
    const auto connection = search::build_connection_string_w(st->ctx.dbSettings);
    if (connection.empty()) {
        MessageBoxW(hwnd, L"请先在系统设置中配置数据库连接。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }

    search::OutpatientChargeQuery query;
    query.connection_string = search::wide_to_utf8(connection);
    query.start_time = dateTimeText(st->startDate);
    query.end_time = dateTimeText(st->endDate);
    query.lab_department = search::wide_to_utf8(comboText(st->labDepartment));
    query.include_non_lab = SendMessageW(st->includeNonLab, BM_GETCHECK, 0, 0) == BST_CHECKED;
    query.outpatient_no = search::wide_to_utf8(controlText(st->outpatientNo));
    query.patient_name = search::wide_to_utf8(controlText(st->patientName));
    query.id_card = search::wide_to_utf8(controlText(st->idCard));

    st->querying = true;
    EnableWindow(st->query, FALSE);
    setStatus(st, L"正在查询门诊收费明细...");

    std::thread([hwnd, query]() {
        auto* result = new QueryResult();
        result->ok = search::query_outpatient_charges(query, result->rows, result->error);
        if (!PostMessageW(hwnd, WM_OUTPATIENT_QUERY_LOADED, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }).detach();
}

LRESULT CALLBACK searchEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                UINT_PTR subclassId, DWORD_PTR refData) {
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        HWND parent = reinterpret_cast<HWND>(refData);
        SendMessageW(parent, WM_COMMAND, MAKEWPARAM(IDC_QUERY, BN_CLICKED), 0);
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, searchEditProc, subclassId);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<OutpatientQueryState*>(GetPropW(hwnd, PROP_STATE));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* mcs = reinterpret_cast<MDICREATESTRUCTW*>(cs->lpCreateParams);
            st = reinterpret_cast<OutpatientQueryState*>(mcs->lParam);
            SetPropW(hwnd, PROP_STATE, st);
            st->bgBrush = CreateSolidBrush(RGB(0xF0, 0xF0, 0xF0));

            st->startLabel = label(hwnd, L"收费时间：", 0, 0, 0, 0);
            st->startDate = dateTimePicker(hwnd, IDC_START_TIME, S(hwnd, 92), S(hwnd, 10), S(hwnd, 172), S(hwnd, 24));
            st->endLabel = label(hwnd, L"至", 0, 0, 0, 0, SS_CENTER);
            st->endDate = dateTimePicker(hwnd, IDC_END_TIME, S(hwnd, 300), S(hwnd, 10), S(hwnd, 172), S(hwnd, 24));
            setToday(st->startDate, false);
            setToday(st->endDate, true);

            st->labDepartmentLabel = label(hwnd, L"院区：", 0, 0, 0, 0);
            st->labDepartment = CreateWindowExW(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                S(hwnd, 538), S(hwnd, 10), S(hwnd, 82), S(hwnd, 160),
                hwnd, win32_control_id(IDC_LAB_DEPARTMENT), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(st->labDepartment, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"全部"));
            SendMessageW(st->labDepartment, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"老院"));
            SendMessageW(st->labDepartment, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"新院"));
            SendMessageW(st->labDepartment, CB_SETCURSEL, 0, 0);
            st->includeNonLab = CreateWindowExW(0, L"BUTTON", L"包含非检验科",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_INCLUDE_NON_LAB), GetModuleHandleW(nullptr), nullptr);

            st->outpatientNoLabel = label(hwnd, L"门诊号：", 0, 0, 0, 0);
            st->outpatientNo = edit(hwnd, IDC_OUTPATIENT_NO, S(hwnd, 592), S(hwnd, 10), S(hwnd, 84), S(hwnd, 24));
            SetWindowSubclass(st->outpatientNo, searchEditProc, 1, reinterpret_cast<DWORD_PTR>(hwnd));
            st->patientNameLabel = label(hwnd, L"姓名：", 0, 0, 0, 0);
            st->patientName = edit(hwnd, IDC_PATIENT_NAME, S(hwnd, 730), S(hwnd, 10), S(hwnd, 78), S(hwnd, 24));
            SetWindowSubclass(st->patientName, searchEditProc, 2, reinterpret_cast<DWORD_PTR>(hwnd));
            st->idCardLabel = label(hwnd, L"身份证：", 0, 0, 0, 0);
            st->idCard = edit(hwnd, IDC_ID_CARD, S(hwnd, 878), S(hwnd, 10), S(hwnd, 132), S(hwnd, 24));
            SetWindowSubclass(st->idCard, searchEditProc, 3, reinterpret_cast<DWORD_PTR>(hwnd));
            st->query = search::create_button(hwnd, IDC_QUERY, L"查询", S(hwnd, 1020), S(hwnd, 9), S(hwnd, 70), S(hwnd, 26));
            st->status = label(hwnd, L"请选择收费时间后查询。", S(hwnd, 1102), S(hwnd, 12), S(hwnd, 420), S(hwnd, 24), SS_LEFT);

            st->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_LIST), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(st->list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            initList(st->list);

            search::apply_font_to_children(hwnd, st->ctx.uiFont);
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
            break;
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (st && nm->idFrom == IDC_LIST && nm->code == LVN_COLUMNCLICK) {
                auto* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                sortRows(st, lv->iSubItem, true);
                populateList(st);
                return 0;
            }
            if (st && nm->idFrom == IDC_LIST && nm->code == NM_RCLICK) {
                showCellContextMenu(hwnd, st);
                return 0;
            }
            if (st && nm->idFrom == IDC_LIST && nm->code == NM_CUSTOMDRAW) {
                auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    const int idx = static_cast<int>(cd->nmcd.dwItemSpec);
                    if (idx >= 0 && idx < static_cast<int>(st->rows.size())) {
                        if (ListView_GetItemState(st->list, idx, LVIS_SELECTED) & LVIS_SELECTED) {
                            cd->clrTextBk = GetSysColor(COLOR_HIGHLIGHT);
                            cd->clrText = GetSysColor(COLOR_HIGHLIGHTTEXT);
                        } else {
                            cd->clrTextBk = rowBackColor(st->rows[static_cast<size_t>(idx)]);
                            cd->clrText = COLOR_BLACK;
                        }
                    }
                    return CDRF_NEWFONT;
                }
            }
            break;
        }
        case WM_OUTPATIENT_QUERY_LOADED: {
            std::unique_ptr<QueryResult> result(reinterpret_cast<QueryResult*>(lp));
            if (!st) return 0;
            st->querying = false;
            EnableWindow(st->query, TRUE);
            if (!result->ok) {
                setStatus(st, L"查询失败：" + search::utf8_to_wide(result->error));
                MessageBoxW(hwnd, search::utf8_to_wide(result->error).c_str(), WINDOW_TITLE, MB_ICONERROR);
                return 0;
            }
            st->rows = std::move(result->rows);
            st->sortColumn = 12;
            st->sortAscending = false;
            sortRows(st, 12, false);
            populateList(st);
            setStatus(st, L"查询完成：" + std::to_wstring(st->rows.size()) + L" 条。");
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
            break;
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
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP),
                                               IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);
    registered = true;
}

}  // namespace

HWND create_outpatient_query_module(const ModuleContext& ctx) {
    if (HWND existing = activate_existing_mdi_child_by_title(ctx.mdiClient, WINDOW_TITLE)) {
        return existing;
    }

    registerClass(ctx.instance);
    auto* st = new OutpatientQueryState();
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
        MessageBoxW(ctx.mdiClient, L"无法打开门诊查询窗口。", WINDOW_TITLE, MB_ICONERROR);
        return nullptr;
    }
    SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    return child;
}

#endif
