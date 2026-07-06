#include "barcode_module.h"

#ifdef _WIN32

#include "main_app.h"
#include "resource.h"
#include "regular_report_module.h"
#include "search_core.h"
#include "log.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"

#include <commctrl.h>
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const wchar_t* WND_CLASS = L"BarcodeModuleChild";
constexpr const wchar_t* LEGEND_CLASS = L"BarcodeStatusLegend";
constexpr const wchar_t* WINDOW_TITLE = L"已签收条码查询";
constexpr const wchar_t* PROP_STATE = L"BarcodeSt";
constexpr UINT WM_BARCODE_LOADED = WM_APP + 501;

constexpr int IDC_DATE_FIELD = 4101;
constexpr int IDC_START_DATE = 4102;
constexpr int IDC_END_DATE = 4103;
constexpr int IDC_BARCODE = 4104;
constexpr int IDC_NAME = 4105;
constexpr int IDC_REG_NO = 4106;
constexpr int IDC_MACHINE_STATUS = 4107;
constexpr int IDC_ROOM = 4108;
constexpr int IDC_NOT_CANCELED = 4110;
constexpr int IDC_CANCELED = 4111;
constexpr int IDC_QUERY = 4112;
constexpr int IDC_CANCEL_BARCODE = 4113;
constexpr int IDC_CANCEL_MEDICAL = 4114;
constexpr int IDC_REFRESH = 4115;
constexpr int IDC_CANCEL_REASON = 4116;
constexpr int IDC_EXPORT = 4118;
constexpr int IDC_LIST = 4120;
constexpr int IDC_STATUS = 4121;
constexpr int FIRST_DATA_COLUMN = 1;
constexpr int LAST_DATA_COLUMN = 27;
constexpr UINT IDM_COPY_CELL = 41201;
const COLORREF COLOR_NOT_MACHINE = RGB(0xFF, 0xFF, 0x54);
const COLORREF COLOR_LOADED_NOT_REVIEWED = RGB(0xFF, 0xFF, 0xFF);
const COLORREF COLOR_REVIEWED_NOT_SENT = RGB(0x6F, 0x94, 0xE6);
const COLORREF COLOR_SENT = RGB(0x99, 0xBB, 0x90);

struct BarcodeState {
    ModuleContext ctx;
    HWND dateField = nullptr;
    HWND startDate = nullptr;
    HWND endDate = nullptr;
    HWND barcode = nullptr;
    HWND name = nullptr;
    HWND regNo = nullptr;
    HWND machineStatus = nullptr;
    HWND room = nullptr;
    HWND notCanceled = nullptr;
    HWND canceled = nullptr;
    HWND query = nullptr;
    HWND cancelBarcode = nullptr;
    HWND cancelMedical = nullptr;
    HWND refresh = nullptr;
    HWND cancelReason = nullptr;
    HWND exportExcel = nullptr;
    HWND legend = nullptr;
    HWND list = nullptr;
    HWND status = nullptr;
    HBRUSH bgBrush = nullptr;
    std::vector<search::RoomOption> rooms;
    std::vector<search::BarcodeQueryRow> rows;
    int listSortColumn = -1;
    bool listSortAscending = true;
    std::thread bgThread;
};

struct BarcodeQueryResult {
    bool ok = false;
    std::string error;
    std::vector<search::BarcodeQueryRow> rows;
};

struct ListColumn {
    int index;
    const wchar_t* title;
    int width;
};

const ListColumn BARCODE_COLUMNS[] = {
    {0, L"", 24},
    {1, L"样本号", 58},
    {2, L"急诊", 44},
    {3, L"条形码", 104},
    {4, L"病人号", 90},
    {5, L"类型", 56},
    {6, L"姓名", 86},
    {7, L"性别", 48},
    {8, L"申请科室", 110},
    {9, L"床号", 70},
    {10, L"签收人", 100},
    {11, L"签收时间", 150},
    {12, L"医嘱内容", 230},
    {13, L"标本", 72},
    {14, L"检验者", 80},
    {15, L"审核者", 80},
    {16, L"费用", 76},
    {17, L"申请医生", 90},
    {18, L"状态", 66},
    {19, L"备注", 58},
    {20, L"原因", 58},
    {21, L"送检", 70},
    {22, L"送检时间", 140},
    {23, L"申请时间", 150},
    {24, L"取消时间", 140},
    {25, L"取消人", 82},
    {26, L"HZID", 70},
    {27, L"上机状态", 112},
};

void runQuery(HWND hwnd, BarcodeState* st);

std::string textOf(HWND hwnd) {
    wchar_t buf[512]{};
    GetWindowTextW(hwnd, buf, 512);
    return search::trim(search::wide_to_utf8(buf));
}

std::string comboText(HWND hwnd) {
    const int idx = static_cast<int>(SendMessageW(hwnd, CB_GETCURSEL, 0, 0));
    if (idx < 0) return "";
    wchar_t buf[256]{};
    SendMessageW(hwnd, CB_GETLBTEXT, static_cast<WPARAM>(idx), reinterpret_cast<LPARAM>(buf));
    return search::trim(search::wide_to_utf8(buf));
}

std::string dateText(HWND hwnd) {
    SYSTEMTIME st{};
    if (DateTime_GetSystemtime(hwnd, &st) != GDT_VALID) return "";
    char buf[20]{};
    sprintf_s(buf, "%04u-%02u-%02u %02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
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

HWND dateTimePicker(HWND parent, int id, int x, int y, int w, int h) {
    HWND hwnd = search::create_date_picker(parent, id, x, y, w, h);
    DateTime_SetFormat(hwnd, L"yyyy-MM-dd HH:mm");
    return hwnd;
}

void setStatus(BarcodeState* st, const std::wstring& text) {
    if (st && st->status) SetWindowTextW(st->status, text.c_str());
}

void addComboItem(HWND combo, const wchar_t* text) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
}

void fillStaticCombos(BarcodeState* st) {
    addComboItem(st->dateField, L"申请日期");
    addComboItem(st->dateField, L"签收日期");
    addComboItem(st->dateField, L"上机日期");
    SendMessageW(st->dateField, CB_SETCURSEL, 1, 0);

    const wchar_t* statuses[] = {L"全部", L"已签收未上机", L"已上机未审核", L"已审核未发送", L"发送完成"};
    for (const auto* text : statuses) addComboItem(st->machineStatus, text);
    SendMessageW(st->machineStatus, CB_SETCURSEL, 2, 0);
}

void loadRooms(BarcodeState* st) {
    SendMessageW(st->room, CB_RESETCONTENT, 0, 0);
    addComboItem(st->room, L"全部");
    st->rooms.clear();

    const auto conn = search::wide_to_utf8(search::build_connection_string_w(st->ctx.dbSettings));
    if (!conn.empty()) {
        std::string error;
        search::query_rooms(conn, st->rooms, error);
    }

    for (size_t i = 0; i < st->rooms.size(); ++i) {
        const auto label = search::utf8_to_wide(st->rooms[i].room_name);
        SendMessageW(st->room, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    SendMessageW(st->room, CB_SETCURSEL, 0, 0);
}

HWND label(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_RIGHT,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND leftLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND button(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                           x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

void addColumn(HWND list, int index, const wchar_t* title, int width) {
    search::add_list_column(list, index, title, width);
}

struct LegendItem {
    const wchar_t* text;
    COLORREF color;
    int x;
    int y;
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
            if (auto* st = reinterpret_cast<BarcodeState*>(GetPropW(GetParent(hwnd), PROP_STATE))) {
                font = st->ctx.uiFont;
            }
            HGDIOBJ oldFont = nullptr;
            if (font) oldFont = SelectObject(dc, font);

            const float s = search::dpi_scale_factor(hwnd);
            auto S = [s](int v) { return static_cast<int>(v * s); };
            const LegendItem items[] = {
                {L"已签收未上机", COLOR_NOT_MACHINE, 0, 2},
                {L"已上机未审核", COLOR_LOADED_NOT_REVIEWED, 116, 2},
                {L"已审核未发送", COLOR_REVIEWED_NOT_SENT, 248, 2},
                {L"发送完成", COLOR_SENT, 380, 2},
            };
            for (const auto& item : items) {
                RECT swatch{S(item.x), S(item.y + 3), S(item.x + 12), S(item.y + 15)};
                HBRUSH brush = CreateSolidBrush(item.color);
                FillRect(dc, &swatch, brush);
                DeleteObject(brush);
                FrameRect(dc, &swatch, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
                RECT textRc{S(item.x + 17), S(item.y), rc.right, S(item.y + 20)};
                DrawTextW(dc, item.text, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
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

std::string csvEscape(const std::string& text) {
    const bool quote = text.find_first_of(",\"\r\n") != std::string::npos;
    std::string out;
    out.reserve(text.size() + 2);
    if (quote) out.push_back('"');
    for (const char ch : text) {
        if (ch == '"') {
            out += "\"\"";
        } else {
            out.push_back(ch);
        }
    }
    if (quote) out.push_back('"');
    return out;
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

std::wstring defaultExportFilename(BarcodeState* st) {
    std::string filename = "已签收条码";
    const std::string dateField = sanitizeFilenamePart(comboText(st->dateField));
    const std::string start = sanitizeFilenamePart(dateText(st->startDate));
    const std::string end = sanitizeFilenamePart(dateText(st->endDate));
    if (!dateField.empty()) filename += "-" + dateField;
    if (!start.empty()) filename += "-" + start;
    if (!end.empty() && end != start) filename += "至" + end;
    filename += ".csv";
    return search::utf8_to_wide(filename);
}

bool chooseExportPath(HWND owner, BarcodeState* st, std::wstring& path) {
    wchar_t buffer[MAX_PATH]{};
    const std::wstring defaultName = defaultExportFilename(st);
    lstrcpynW(buffer, defaultName.c_str(), MAX_PATH);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"CSV 文件 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"csv";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return false;
    path = buffer;
    return true;
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

LRESULT CALLBACK searchEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                UINT_PTR subclassId, DWORD_PTR refData) {
    if (msg == WM_GETDLGCODE) {
        return DefSubclassProc(hwnd, msg, wp, lp) | DLGC_WANTALLKEYS;
    }
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

void createControls(HWND hwnd, BarcodeState* st) {
    const float s = search::dpi_scale_factor(hwnd);
    auto S = [s](int v) { return static_cast<int>(v * s); };
    registerLegendClass(GetModuleHandleW(nullptr));

    st->dateField = search::create_combo(hwnd, IDC_DATE_FIELD, S(8), S(8), S(88), S(160), false);
    st->startDate = dateTimePicker(hwnd, IDC_START_DATE, S(104), S(8), S(160), S(24));
    label(hwnd, L"至", S(268), S(11), S(20), S(22));
    st->endDate = dateTimePicker(hwnd, IDC_END_DATE, S(292), S(8), S(160), S(24));
    label(hwnd, L"条形码:", S(456), S(11), S(58), S(22));
    st->barcode = search::create_edit(hwnd, IDC_BARCODE, S(518), S(8), S(140), S(24));
    SetWindowSubclass(st->barcode, searchEditProc, 1, reinterpret_cast<DWORD_PTR>(hwnd));
    label(hwnd, L"姓  名:", S(662), S(11), S(58), S(22));
    st->name = search::create_edit(hwnd, IDC_NAME, S(724), S(8), S(96), S(24));
    SetWindowSubclass(st->name, searchEditProc, 2, reinterpret_cast<DWORD_PTR>(hwnd));
    label(hwnd, L"病人号:", S(824), S(11), S(58), S(22));
    st->regNo = search::create_edit(hwnd, IDC_REG_NO, S(886), S(8), S(124), S(24));
    SetWindowSubclass(st->regNo, searchEditProc, 3, reinterpret_cast<DWORD_PTR>(hwnd));
    label(hwnd, L"专业组", S(1014), S(11), S(46), S(22));
    st->room = search::create_combo(hwnd, IDC_ROOM, S(1064), S(8), S(100), S(160), false);
    label(hwnd, L"上机状态:", S(1168), S(11), S(72), S(22));
    st->machineStatus = search::create_combo(hwnd, IDC_MACHINE_STATUS, S(1244), S(8), S(120), S(160), false);

    st->notCanceled = CreateWindowExW(0, L"BUTTON", L"未取消签收", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
                                      S(10), S(42), S(98), S(24), hwnd, win32_control_id(IDC_NOT_CANCELED), GetModuleHandleW(nullptr), nullptr);
    st->canceled = CreateWindowExW(0, L"BUTTON", L"取消签收", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
                                   S(10), S(70), S(98), S(24), hwnd, win32_control_id(IDC_CANCELED), GetModuleHandleW(nullptr), nullptr);
    Button_SetCheck(st->notCanceled, BST_CHECKED);

    st->query = button(hwnd, IDC_QUERY, L"查    询", S(116), S(50), S(76), S(32));
    st->cancelBarcode = button(hwnd, IDC_CANCEL_BARCODE, L"取消条码签收", S(200), S(50), S(112), S(32));
    st->cancelMedical = button(hwnd, IDC_CANCEL_MEDICAL, L"取消医嘱签收", S(320), S(50), S(112), S(32));
    st->refresh = button(hwnd, IDC_REFRESH, L"刷    新", S(440), S(50), S(76), S(32));
    st->cancelReason = button(hwnd, IDC_CANCEL_REASON, L"取消原因限制", S(524), S(50), S(118), S(32));
    st->exportExcel = button(hwnd, IDC_EXPORT, L"导出Excel", S(650), S(50), S(100), S(32));
    EnableWindow(st->cancelBarcode, FALSE);
    EnableWindow(st->cancelMedical, FALSE);
    EnableWindow(st->cancelReason, FALSE);
    EnableWindow(st->exportExcel, FALSE);

    st->legend = CreateWindowExW(0, LEGEND_CLASS, L"", WS_CHILD | WS_VISIBLE,
                                 S(770), S(52), S(460), S(24), hwnd, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);

    st->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL,
                               S(4), S(120), S(1240), S(420), hwnd, win32_control_id(IDC_LIST), GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(st->list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    for (const auto& column : BARCODE_COLUMNS) {
        addColumn(st->list, column.index, column.title, S(column.width));
    }

    st->status = leftLabel(hwnd, L"", S(8), S(546), S(900), S(24));

    fillStaticCombos(st);
    setToday(st->startDate, false);
    setToday(st->endDate, true);
    loadRooms(st);
    search::apply_font_to_children(hwnd, st->ctx.uiFont);
}

void layout(HWND hwnd, BarcodeState* st) {
    if (!st) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const float s = search::dpi_scale_factor(hwnd);
    auto S = [s](int v) { return static_cast<int>(v * s); };
    const int listTop = S(120);
    const int statusH = S(24);
    const int clientW = static_cast<int>(rc.right);
    const int clientH = static_cast<int>(rc.bottom);
    MoveWindow(st->list, S(4), listTop,
               (std::max)(S(300), clientW - S(8)),
               (std::max)(S(160), clientH - listTop - statusH - S(8)), TRUE);
    MoveWindow(st->status, S(8), clientH - statusH - S(4),
               (std::max)(S(300), clientW - S(16)), statusH, TRUE);
}

void setCell(HWND list, int row, int col, const std::string& text) {
    const auto wide = search::utf8_to_wide(text);
    ListView_SetItemText(list, row, col, const_cast<wchar_t*>(wide.c_str()));
}

void insertRow(HWND list, int index, const search::BarcodeQueryRow& row) {
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = index;
    wchar_t first[] = L"";
    item.pszText = first;
    ListView_InsertItem(list, &item);
    const std::string* cells[] = {
        &row.sample_no,
        &row.emergency,
        &row.barcode,
        &row.reg_no,
        &row.type_name,
        &row.name,
        &row.sex,
        &row.dept_name,
        &row.bed_no,
        &row.receiver,
        &row.receive_time,
        &row.order_text,
        &row.sample_name,
        &row.tester,
        &row.reviewer,
        &row.fee,
        &row.request_doctor,
        &row.status,
        &row.note,
        &row.reason,
        &row.submitter,
        &row.submit_time,
        &row.request_time,
        &row.cancel_time,
        &row.cancel_operator,
        &row.hzid,
        &row.machine_status,
    };
    const int cellCount = static_cast<int>(sizeof(cells) / sizeof(cells[0]));
    for (int col = 1; col <= cellCount; ++col) {
        setCell(list, index, col, *cells[col - 1]);
    }
}

const std::string& barcodeSortValue(const search::BarcodeQueryRow& row, int col) {
    static const std::string empty;
    switch (col) {
        case 1: return row.sample_no;
        case 2: return row.emergency;
        case 3: return row.barcode;
        case 4: return row.reg_no;
        case 5: return row.type_name;
        case 6: return row.name;
        case 7: return row.sex;
        case 8: return row.dept_name;
        case 9: return row.bed_no;
        case 10: return row.receiver;
        case 11: return row.receive_time;
        case 12: return row.order_text;
        case 13: return row.sample_name;
        case 14: return row.tester;
        case 15: return row.reviewer;
        case 16: return row.fee;
        case 17: return row.request_doctor;
        case 18: return row.status;
        case 19: return row.note;
        case 20: return row.reason;
        case 21: return row.submitter;
        case 22: return row.submit_time;
        case 23: return row.request_time;
        case 24: return row.cancel_time;
        case 25: return row.cancel_operator;
        case 26: return row.hzid;
        case 27: return row.machine_status;
        default: return empty;
    }
}

bool parseDouble(const std::string& text, double& value) {
    const std::string trimmed = search::trim(text);
    if (trimmed.empty()) return false;
    char* end = nullptr;
    value = std::strtod(trimmed.c_str(), &end);
    return end && *end == '\0';
}

int compareBarcodeSortValue(const search::BarcodeQueryRow& a,
                            const search::BarcodeQueryRow& b,
                            int col) {
    const std::string left = search::trim(barcodeSortValue(a, col));
    const std::string right = search::trim(barcodeSortValue(b, col));
    if (col == 16) {
        double ln = 0.0, rn = 0.0;
        const bool lok = parseDouble(left, ln);
        const bool rok = parseDouble(right, rn);
        if (lok && rok) {
            if (ln < rn) return -1;
            if (ln > rn) return 1;
            return 0;
        }
        if (lok != rok) return lok ? -1 : 1;
    }
    if (left < right) return -1;
    if (left > right) return 1;
    return 0;
}

std::string barcodeRowKey(const search::BarcodeQueryRow& row) {
    return row.barcode + "|" + row.sample_no + "|" + row.reg_no + "|" + row.order_text;
}

void updateExportButton(BarcodeState* st) {
    if (!st || !st->exportExcel) return;
    EnableWindow(st->exportExcel, !st->rows.empty());
}

void sortBarcodeRowsForDisplay(BarcodeState* st) {
    if (!st || st->listSortColumn < FIRST_DATA_COLUMN || st->listSortColumn > LAST_DATA_COLUMN) return;
    const int col = st->listSortColumn;
    const bool ascending = st->listSortAscending;
    std::stable_sort(st->rows.begin(), st->rows.end(),
                     [col, ascending](const auto& a, const auto& b) {
                         const int cmp = compareBarcodeSortValue(a, b, col);
                         return ascending ? cmp < 0 : cmp > 0;
                     });
}

void presentRows(BarcodeState* st) {
    if (!st || !st->list) return;
    SendMessageW(st->list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(st->list);
    for (size_t i = 0; i < st->rows.size(); ++i) {
        insertRow(st->list, static_cast<int>(i), st->rows[i]);
    }
    SendMessageW(st->list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(st->list, nullptr, TRUE);
}

void sortRowsByColumn(BarcodeState* st, int col) {
    if (!st || !st->list || st->rows.empty() || col < FIRST_DATA_COLUMN || col > LAST_DATA_COLUMN) return;
    if (st->listSortColumn == col) {
        st->listSortAscending = !st->listSortAscending;
    } else {
        st->listSortColumn = col;
        st->listSortAscending = true;
    }

    std::string selectedKey;
    const int selected = ListView_GetNextItem(st->list, -1, LVNI_SELECTED);
    if (selected >= 0 && selected < static_cast<int>(st->rows.size())) {
        selectedKey = barcodeRowKey(st->rows[static_cast<size_t>(selected)]);
    }

    sortBarcodeRowsForDisplay(st);
    presentRows(st);

    if (!selectedKey.empty()) {
        for (int i = 0; i < static_cast<int>(st->rows.size()); ++i) {
            if (barcodeRowKey(st->rows[static_cast<size_t>(i)]) == selectedKey) {
                ListView_SetItemState(st->list, i, LVIS_SELECTED | LVIS_FOCUSED,
                                      LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(st->list, i, FALSE);
                break;
            }
        }
    }
}

void openRegularReportForRow(HWND owner, BarcodeState* st, int index) {
    if (!st || index < 0 || index >= static_cast<int>(st->rows.size())) return;
    const auto& row = st->rows[static_cast<size_t>(index)];
    if (search::trim(row.report_no).empty() || search::trim(row.machine_code).empty() ||
        search::trim(row.inspect_date).empty()) {
        MessageBoxW(owner, L"该条码为已签收未上机，无法跳转到常规报告。", WINDOW_TITLE, MB_ICONINFORMATION);
        return;
    }

    auto* target = new RegularReportOpenTarget;
    target->rep_no = search::trim(row.report_no);
    target->oper_no = search::trim(row.sample_no);
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

void showCellContextMenu(HWND hwnd, BarcodeState* st) {
    if (!st || !st->list) return;
    POINT screenPt{};
    GetCursorPos(&screenPt);
    POINT listPt = screenPt;
    ScreenToClient(st->list, &listPt);

    LVHITTESTINFO hit{};
    hit.pt = listPt;
    const int row = ListView_SubItemHitTest(st->list, &hit);
    if (row < 0 || row >= static_cast<int>(st->rows.size()) ||
        hit.iSubItem < FIRST_DATA_COLUMN || hit.iSubItem > LAST_DATA_COLUMN) {
        return;
    }

    ListView_SetItemState(st->list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(st->list, row, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, IDM_COPY_CELL, L"复制单元格");
    const UINT command = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                        screenPt.x, screenPt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (command != IDM_COPY_CELL) return;

    const std::wstring text = search::utf8_to_wide(
        barcodeSortValue(st->rows[static_cast<size_t>(row)], hit.iSubItem));
    if (copyTextToClipboard(hwnd, text)) {
        setStatus(st, L"已复制单元格：" + std::wstring(BARCODE_COLUMNS[hit.iSubItem].title));
    } else {
        setStatus(st, L"复制单元格失败。");
    }
}

search::BarcodeQueryFilters collectFilters(BarcodeState* st) {
    search::BarcodeQueryFilters f;
    f.connection_string = search::wide_to_utf8(search::build_connection_string_w(st->ctx.dbSettings));
    const auto dateField = comboText(st->dateField);
    f.date_field = dateField == "签收日期" ? "Receive" : dateField == "上机日期" ? "Machine" : "Apply";
    f.start_date = dateText(st->startDate);
    f.end_date = dateText(st->endDate);
    f.barcode = textOf(st->barcode);
    f.patient_name = textOf(st->name);
    f.reg_no = textOf(st->regNo);
    f.machine_status = comboText(st->machineStatus);
    f.canceled = Button_GetCheck(st->canceled) == BST_CHECKED;

    const int roomIdx = static_cast<int>(SendMessageW(st->room, CB_GETCURSEL, 0, 0));
    if (roomIdx > 0 && static_cast<size_t>(roomIdx - 1) < st->rooms.size()) {
        f.room_code = st->rooms[static_cast<size_t>(roomIdx - 1)].room_code;
    }

    return f;
}

void runQuery(HWND hwnd, BarcodeState* st) {
    if (search::build_connection_string_w(st->ctx.dbSettings).empty()) {
        MessageBoxW(hwnd, L"请先在系统设置中配置数据库连接。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }
    auto filters = collectFilters(st);
    setStatus(st, L"正在查询...");

    if (st->bgThread.joinable()) st->bgThread.join();
    st->bgThread = std::thread([hwnd, filters]() {
        try {
            auto* result = new BarcodeQueryResult;
            result->ok = search::query_barcodes(filters, result->rows, result->error);
            if (!PostMessageW(hwnd, WM_BARCODE_LOADED, 0, reinterpret_cast<LPARAM>(result))) {
                LOG_WARN("PostMessageW WM_BARCODE_LOADED failed");
                delete result;
            }
        } catch (...) {
            LOG_ERROR("Barcode query thread crashed");
        }
    });
}

void finishQuery(HWND hwnd, BarcodeState* st, std::unique_ptr<BarcodeQueryResult> result) {
    if (!result->ok) {
        setStatus(st, L"查询失败。");
        MessageBoxW(hwnd, search::utf8_to_wide(result->error).c_str(), WINDOW_TITLE, MB_ICONERROR);
        updateExportButton(st);
        return;
    }
    st->rows = std::move(result->rows);
    sortBarcodeRowsForDisplay(st);
    presentRows(st);
    updateExportButton(st);
    setStatus(st, L"查询完成，共 " + std::to_wstring(st->rows.size()) + L" 条。");
}

void exportRowsCsv(HWND hwnd, BarcodeState* st) {
    if (!st || st->rows.empty()) {
        MessageBoxW(hwnd, L"当前没有可导出的条码记录。", WINDOW_TITLE, MB_ICONINFORMATION);
        return;
    }

    std::wstring path;
    if (!chooseExportPath(hwnd, st, path)) return;

    FILE* file = nullptr;
#ifdef _MSC_VER
    _wfopen_s(&file, path.c_str(), L"wb");
#else
    file = _wfopen(path.c_str(), L"wb");
#endif
    if (!file) {
        MessageBoxW(hwnd, L"导出文件创建失败，请确认目标位置可写。", WINDOW_TITLE, MB_ICONERROR);
        return;
    }

    std::ostringstream csv;
    csv << "\xEF\xBB\xBF";
    bool firstColumn = true;
    for (const auto& column : BARCODE_COLUMNS) {
        if (column.index < FIRST_DATA_COLUMN || column.index > LAST_DATA_COLUMN) continue;
        if (!firstColumn) csv << ',';
        firstColumn = false;
        csv << csvEscape(search::wide_to_utf8(column.title));
    }
    csv << '\n';
    for (const auto& row : st->rows) {
        for (int col = FIRST_DATA_COLUMN; col <= LAST_DATA_COLUMN; ++col) {
            if (col > FIRST_DATA_COLUMN) csv << ',';
            csv << csvEscape(barcodeSortValue(row, col));
        }
        csv << '\n';
    }
    const std::string text = csv.str();
    fwrite(text.data(), 1, text.size(), file);
    fclose(file);

    setStatus(st, L"已导出条码记录：" + path);
    MessageBoxW(hwnd, (L"已导出条码记录：\n" + path).c_str(), WINDOW_TITLE, MB_ICONINFORMATION);
}

COLORREF rowColor(const search::BarcodeQueryRow& row) {
    if (row.machine_status == "已签收未上机") return COLOR_NOT_MACHINE;
    if (row.machine_status == "已审核未发送") return COLOR_REVIEWED_NOT_SENT;
    if (row.machine_status == "发送完成") return COLOR_SENT;
    return COLOR_LOADED_NOT_REVIEWED;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<BarcodeState*>(GetPropW(hwnd, PROP_STATE));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* mcs = reinterpret_cast<MDICREATESTRUCTW*>(cs->lpCreateParams);
            st = reinterpret_cast<BarcodeState*>(mcs->lParam);
            if (!st) {
                LOG_ERROR("WM_CREATE: lpCreateParams is null (BarcodeState)");
                return -1;
            }
            SetPropW(hwnd, PROP_STATE, reinterpret_cast<HANDLE>(st));
            st->bgBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
            createControls(hwnd, st);
            layout(hwnd, st);
            return 0;
        }
        case WM_SIZE:
            layout(hwnd, st);
            return 0;
        case app::WM_APP_FONT_CHANGED:
            if (st && lp) {
                st->ctx.uiFont = reinterpret_cast<HFONT>(lp);
                search::apply_font_to_children(hwnd, st->ctx.uiFont);
                layout(hwnd, st);
                if (st->legend) InvalidateRect(st->legend, nullptr, TRUE);
            }
            return 0;
        case WM_COMMAND:
            if (!st) break;
            switch (LOWORD(wp)) {
                case IDC_QUERY:
                case IDC_REFRESH:
                    runQuery(hwnd, st);
                    return 0;
                case IDC_EXPORT:
                    exportRowsCsv(hwnd, st);
                    return 0;
            }
            break;
        case WM_BARCODE_LOADED:
            if (st) {
                std::unique_ptr<BarcodeQueryResult> result(reinterpret_cast<BarcodeQueryResult*>(lp));
                finishQuery(hwnd, st, std::move(result));
            } else {
                delete reinterpret_cast<BarcodeQueryResult*>(lp);
            }
            return 0;
        case WM_NOTIFY:
            if (st) {
                auto* nm = reinterpret_cast<NMHDR*>(lp);
                if (nm->idFrom == IDC_LIST && nm->code == LVN_COLUMNCLICK) {
                    auto* clicked = reinterpret_cast<NMLISTVIEW*>(lp);
                    sortRowsByColumn(st, clicked->iSubItem);
                    return 0;
                }
                if (nm->idFrom == IDC_LIST && nm->code == NM_RCLICK) {
                    showCellContextMenu(hwnd, st);
                    return 0;
                }
                if (nm->idFrom == IDC_LIST && nm->code == NM_DBLCLK) {
                    auto* item = reinterpret_cast<NMITEMACTIVATE*>(lp);
                    openRegularReportForRow(hwnd, st, item ? item->iItem : -1);
                    return 0;
                }
                if (nm->idFrom == IDC_LIST && nm->code == NM_CUSTOMDRAW) {
                    auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                    if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                    if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                        const int idx = static_cast<int>(cd->nmcd.dwItemSpec);
                        if (idx >= 0 && idx < static_cast<int>(st->rows.size())) {
                            cd->clrTextBk = rowColor(st->rows[static_cast<size_t>(idx)]);
                            cd->clrText = RGB(0, 0, 0);
                        }
                        return CDRF_NEWFONT;
                    }
                }
            }
            break;
        case WM_CTLCOLORSTATIC:
            if (st) {
                SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_BTNFACE));
                return reinterpret_cast<LRESULT>(st->bgBrush);
            }
            break;
        case WM_DESTROY:
            RemovePropW(hwnd, PROP_STATE);
            if (st) {
                if (st->bgThread.joinable()) st->bgThread.join();
                if (st->bgBrush) DeleteObject(st->bgBrush);
                delete st;
            }
            break;
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND create_barcode_module(const ModuleContext& ctx) {
    if (HWND existing = activate_existing_mdi_child_by_title(ctx.mdiClient, WINDOW_TITLE)) {
        return existing;
    }

    REGISTER_MDI_CHILD_CLASS(ctx.instance, wndProc, WND_CLASS, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));

    auto* st = new BarcodeState;
    st->ctx = ctx;

    MDICREATESTRUCTW mcs{};
    mcs.szClass = WND_CLASS;
    mcs.szTitle = WINDOW_TITLE;
    mcs.hOwner = ctx.instance;
    mcs.x = mcs.y = mcs.cx = mcs.cy = CW_USEDEFAULT;

    mcs.lParam = reinterpret_cast<LPARAM>(st);
    HWND child = reinterpret_cast<HWND>(SendMessageW(ctx.mdiClient, WM_MDICREATE, 0, reinterpret_cast<LPARAM>(&mcs)));
    if (child) {
        SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    } else {
        delete st;
    }
    return child;
}

#endif
