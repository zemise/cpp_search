#include "machine_picker_popup.h"

#ifdef _WIN32

#include "search_controller.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"

#include <commctrl.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <vector>

namespace search {
namespace {

constexpr int IDC_PICKER_ROOM = 6951;
constexpr int IDC_PICKER_MACHINE = 6952;
constexpr int IDC_PICKER_SEARCH = 6953;
constexpr UINT_PTR SEARCH_SUBCLASS = 6954;
constexpr const wchar_t* PICKER_CLASS = L"SharedMachinePickerPopup";

constexpr int PICKER_CLIENT_W = 644;
constexpr int PICKER_INITIAL_H = 224;
constexpr int PICKER_INPUT_X = 10;
constexpr int PICKER_SEARCH_LABEL_Y = 14;
constexpr int PICKER_SEARCH_Y = 10;
constexpr int PICKER_SEARCH_LABEL_W = 66;
constexpr int PICKER_ROOM_Y = 44;
constexpr int PICKER_LIST_Y = 76;
constexpr int PICKER_LIST_W = 618;
constexpr int PICKER_CODE_COL_W = 64;
constexpr int PICKER_PY_COL_W = 80;
constexpr int PICKER_GROUP_CODE_COL_W = 74;
constexpr int PICKER_GROUP_NAME_COL_W = 150;
constexpr int PICKER_SAMPLE_COL_W = 66;
constexpr int PICKER_COL_GAP = 8;
constexpr int PICKER_NAME_COL_W =
    PICKER_LIST_W - PICKER_CODE_COL_W - PICKER_GROUP_CODE_COL_W - PICKER_GROUP_NAME_COL_W -
    PICKER_SAMPLE_COL_W - PICKER_PY_COL_W - PICKER_COL_GAP;
constexpr int PICKER_COMBO_DROP_H = 180;
constexpr int PICKER_INITIAL_LIST_H = 92;
constexpr int PICKER_MIN_ROWS = 3;
constexpr int PICKER_MAX_ROWS = 8;
constexpr int PICKER_HEADER_H = 28;
constexpr int PICKER_BOTTOM_PAD = 10;
constexpr int PICKER_LIST_EXTRA_H = 6;

struct PickerState {
    MachinePickerPopupOptions options;
    HWND searchLabel = nullptr;
    HWND searchEdit = nullptr;
    HWND roomCombo = nullptr;
    HWND machineList = nullptr;
    std::vector<RoomOption> rooms;
    std::vector<MachineOption> allMachines;
    std::vector<MachineOption> machines;
    bool closePosted = false;
    bool syncingRoom = false;
    bool roomChosenByUser = false;
    bool refreshingMachines = false;
};

int S(HWND hwnd, int value) {
    return static_cast<int>(value * dpi_scale_factor(hwnd));
}

HWND makeStatic(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, h, parent, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
}

HWND makeEdit(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, x, y, w, h, parent,
                           win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

void setCell(HWND list, int row, int col, const std::string& text) {
    const auto wide = utf8_to_wide(text);
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

std::string selectedRoomCode(PickerState* ps) {
    if (!ps || !ps->roomCombo) return "";
    const int index = static_cast<int>(SendMessageW(ps->roomCombo, CB_GETCURSEL, 0, 0));
    if (ps->options.include_all_rooms) {
        if (index <= 0) return "";
        const int roomIndex = index - 1;
        if (roomIndex >= 0 && roomIndex < static_cast<int>(ps->rooms.size()))
            return ps->rooms[static_cast<size_t>(roomIndex)].room_code;
        return "";
    }
    if (index < 0 || index >= static_cast<int>(ps->rooms.size())) return "";
    return ps->rooms[static_cast<size_t>(index)].room_code;
}

std::string searchText(PickerState* ps) {
    if (!ps || !ps->searchEdit) return "";
    wchar_t buffer[128]{};
    GetWindowTextW(ps->searchEdit, buffer, 128);
    return trim(wide_to_utf8(buffer));
}

std::string lowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

bool machineMatchesSearch(const MachineOption& row, const std::string& needle) {
    const std::string q = lowerAscii(trim(needle));
    if (q.empty()) return true;
    const std::string code = lowerAscii(trim(row.mach_code));
    const std::string py = lowerAscii(trim(row.py_code));
    return code.find(q) != std::string::npos || py.find(q) != std::string::npos;
}

void selectRoomByCode(PickerState* ps, const std::string& roomCode) {
    if (!ps || !ps->roomCombo) return;
    const std::string target = trim(roomCode);
    ps->syncingRoom = true;
    if (target.empty() && ps->options.include_all_rooms) {
        SendMessageW(ps->roomCombo, CB_SETCURSEL, 0, 0);
        ps->syncingRoom = false;
        return;
    }
    for (int i = 0; i < static_cast<int>(ps->rooms.size()); ++i) {
        if (trim(ps->rooms[static_cast<size_t>(i)].room_code) == target) {
            SendMessageW(ps->roomCombo, CB_SETCURSEL, ps->options.include_all_rooms ? i + 1 : i, 0);
            ps->syncingRoom = false;
            return;
        }
    }
    ps->syncingRoom = false;
}

void syncRoomFromSelection(PickerState* ps) {
    if (!ps || !ps->machineList) return;
    const int sel = ListView_GetNextItem(ps->machineList, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= static_cast<int>(ps->machines.size())) return;
    selectRoomByCode(ps, ps->machines[static_cast<size_t>(sel)].room_code);
}

void applyFilter(PickerState* ps) {
    if (!ps) return;
    ps->machines.clear();
    const std::string keyword = searchText(ps);
    const std::string roomCode = trim(selectedRoomCode(ps));
    const bool restrictByRoom = keyword.empty() && ps->roomChosenByUser && !roomCode.empty();
    for (const auto& row : ps->allMachines) {
        if (restrictByRoom && trim(row.room_code) != roomCode) continue;
        if (machineMatchesSearch(row, keyword)) ps->machines.push_back(row);
    }
}

int listHeight(HWND hwnd, PickerState* ps) {
    const int visibleRows =
        std::clamp(static_cast<int>(ps ? ps->machines.size() : 0), PICKER_MIN_ROWS, PICKER_MAX_ROWS);
    const int rowHeight = S(hwnd, 22);
    return S(hwnd, PICKER_HEADER_H) + visibleRows * rowHeight + S(hwnd, PICKER_LIST_EXTRA_H);
}

void layoutPicker(HWND hwnd, PickerState* ps) {
    if (!hwnd || !ps || !ps->machineList) return;
    const int ix = S(hwnd, PICKER_INPUT_X);
    const int labelW = S(hwnd, PICKER_SEARCH_LABEL_W);
    if (ps->searchLabel)
        MoveWindow(ps->searchLabel, ix, S(hwnd, PICKER_SEARCH_LABEL_Y), labelW, S(hwnd, 18), TRUE);
    if (ps->searchEdit)
        MoveWindow(ps->searchEdit, ix + labelW, S(hwnd, PICKER_SEARCH_Y),
                   S(hwnd, PICKER_LIST_W - PICKER_SEARCH_LABEL_W), S(hwnd, 24), TRUE);
    if (ps->roomCombo)
        MoveWindow(ps->roomCombo, ix, S(hwnd, PICKER_ROOM_Y), S(hwnd, PICKER_LIST_W),
                   S(hwnd, PICKER_COMBO_DROP_H), TRUE);
    const int listH = listHeight(hwnd, ps);
    MoveWindow(ps->machineList, ix, S(hwnd, PICKER_LIST_Y), S(hwnd, PICKER_LIST_W), listH, TRUE);
    ListView_SetColumnWidth(ps->machineList, 0, S(hwnd, PICKER_CODE_COL_W));
    ListView_SetColumnWidth(ps->machineList, 1, S(hwnd, PICKER_NAME_COL_W));
    ListView_SetColumnWidth(ps->machineList, 2, S(hwnd, PICKER_GROUP_CODE_COL_W));
    ListView_SetColumnWidth(ps->machineList, 3, S(hwnd, PICKER_GROUP_NAME_COL_W));
    ListView_SetColumnWidth(ps->machineList, 4, S(hwnd, PICKER_SAMPLE_COL_W));
    ListView_SetColumnWidth(ps->machineList, 5, S(hwnd, PICKER_PY_COL_W));

    RECT cr{0, 0, S(hwnd, PICKER_CLIENT_W), S(hwnd, PICKER_LIST_Y) + listH + S(hwnd, PICKER_BOTTOM_PAD)};
    AdjustWindowRectEx(&cr, GetWindowLongW(hwnd, GWL_STYLE), FALSE, GetWindowLongW(hwnd, GWL_EXSTYLE));
    SetWindowPos(hwnd, nullptr, 0, 0, cr.right - cr.left, cr.bottom - cr.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void populateMachines(PickerState* ps) {
    if (!ps || !ps->machineList) return;
    applyFilter(ps);
    ps->refreshingMachines = true;
    ListView_DeleteAllItems(ps->machineList);
    const std::string current = trim(ps->options.current_mach_code);
    const std::string keyword = searchText(ps);
    int selected = -1;
    for (int i = 0; i < static_cast<int>(ps->machines.size()); ++i) {
        const auto& machine = ps->machines[static_cast<size_t>(i)];
        const auto code = utf8_to_wide(machine.mach_code);
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<wchar_t*>(code.c_str());
        ListView_InsertItem(ps->machineList, &item);
        setCell(ps->machineList, i, 1, machine.mach_name);
        setCell(ps->machineList, i, 2, machine.group_code);
        setCell(ps->machineList, i, 3, machine.group_name);
        setCell(ps->machineList, i, 4, machine.sample_name);
        setCell(ps->machineList, i, 5, machine.py_code);
        if (keyword.empty() && !current.empty() && machine.mach_code == current) selected = i;
    }
    if (selected < 0 && !ps->machines.empty()) selected = 0;
    if (selected >= 0) {
        ListView_SetItemState(ps->machineList, selected, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(ps->machineList, selected, FALSE);
    }
    ps->refreshingMachines = false;
    if (selected >= 0 && !keyword.empty()) syncRoomFromSelection(ps);
    layoutPicker(GetParent(ps->machineList), ps);
}

void reloadMachines(PickerState* ps) {
    populateMachines(ps);
}

void reloadRooms(PickerState* ps) {
    if (!ps || !ps->roomCombo) return;
    SendMessageW(ps->roomCombo, CB_RESETCONTENT, 0, 0);
    ps->rooms.clear();
    ps->allMachines.clear();
    std::string error;
    if (!load_report_machine_picker_room_options(ps->options.db_settings, ps->rooms, error)) {
        MessageBoxW(ps->options.owner, L"检验科室加载失败。", L"选择检验仪器", MB_ICONERROR);
    }
    if (!load_report_machine_picker_machine_options(ps->options.db_settings, "", ps->allMachines, error)) {
        MessageBoxW(ps->options.owner, L"检验仪器加载失败。", L"选择检验仪器", MB_ICONERROR);
    }
    if (ps->options.include_all_rooms) {
        SendMessageW(ps->roomCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"全部"));
    }
    const std::string currentRoom = trim(ps->options.current_room_code);
    int selected = ps->options.include_all_rooms ? 0 : -1;
    for (int i = 0; i < static_cast<int>(ps->rooms.size()); ++i) {
        const auto& room = ps->rooms[static_cast<size_t>(i)];
        const auto text = utf8_to_wide(room.room_name.empty() ? room.room_code : room.room_name);
        SendMessageW(ps->roomCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        if (!currentRoom.empty() && room.room_code == currentRoom)
            selected = ps->options.include_all_rooms ? i + 1 : i;
    }
    if (selected < 0 && !ps->rooms.empty()) selected = 0;
    if (selected >= 0) SendMessageW(ps->roomCombo, CB_SETCURSEL, selected, 0);
    reloadMachines(ps);
}

void acceptSelection(HWND hwnd, PickerState* ps) {
    if (!ps || !ps->machineList) return;
    const int index = ListView_GetNextItem(ps->machineList, -1, LVNI_SELECTED);
    if (index < 0 || index >= static_cast<int>(ps->machines.size())) return;
    if (ps->options.on_accept) {
        ps->options.on_accept(ps->machines[static_cast<size_t>(index)]);
    }
    DestroyWindow(hwnd);
}

void selectRow(PickerState* ps, int row) {
    if (!ps || !ps->machineList) return;
    const int count = ListView_GetItemCount(ps->machineList);
    if (count <= 0) return;
    row = std::clamp(row, 0, count - 1);
    ListView_SetItemState(ps->machineList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(ps->machineList, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(ps->machineList, row, FALSE);
    syncRoomFromSelection(ps);
}

void moveSelection(PickerState* ps, int delta) {
    if (!ps || !ps->machineList || delta == 0) return;
    int row = ListView_GetNextItem(ps->machineList, -1, LVNI_SELECTED);
    row = row < 0 ? 0 : row + delta;
    selectRow(ps, row);
}

LRESULT CALLBACK searchProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR sid, DWORD_PTR data) {
    auto* ps = reinterpret_cast<PickerState*>(data);
    switch (msg) {
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                HWND popup = GetParent(hwnd);
                if (IsWindow(popup)) {
                    DestroyWindow(popup);
                    return 0;
                }
            }
            if (wp == VK_UP || wp == VK_DOWN) {
                moveSelection(ps, wp == VK_UP ? -1 : 1);
                return 0;
            }
            if (wp == VK_RETURN) {
                HWND popup = GetParent(hwnd);
                if (IsWindow(popup)) {
                    acceptSelection(popup, ps);
                    return 0;
                }
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, searchProc, sid);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void postClose(HWND hwnd, PickerState* ps) {
    if (!ps || ps->closePosted) return;
    ps->closePosted = true;
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

LRESULT CALLBACK pickerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ps = reinterpret_cast<PickerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            ps = reinterpret_cast<PickerState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ps));
            ps->searchLabel = makeStatic(hwnd, L"检索内容", 0, 0, 10, 18);
            ps->searchEdit = makeEdit(hwnd, IDC_PICKER_SEARCH, 0, 0, 10, 24);
            SetWindowSubclass(ps->searchEdit, searchProc, SEARCH_SUBCLASS, reinterpret_cast<DWORD_PTR>(ps));
            ps->roomCombo = create_combo(hwnd, IDC_PICKER_ROOM, 0, 0, 10, PICKER_COMBO_DROP_H, false);
            ps->machineList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                              WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                              0, 0, 0, 0, hwnd, win32_control_id(IDC_PICKER_MACHINE),
                                              GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(ps->machineList,
                                              LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            addColumn(ps->machineList, 0, L"仪器", S(hwnd, PICKER_CODE_COL_W));
            addColumn(ps->machineList, 1, L"仪器名称", S(hwnd, PICKER_NAME_COL_W));
            addColumn(ps->machineList, 2, L"项目代码", S(hwnd, PICKER_GROUP_CODE_COL_W));
            addColumn(ps->machineList, 3, L"项目名称", S(hwnd, PICKER_GROUP_NAME_COL_W));
            addColumn(ps->machineList, 4, L"样本", S(hwnd, PICKER_SAMPLE_COL_W));
            addColumn(ps->machineList, 5, L"拼音码", S(hwnd, PICKER_PY_COL_W));
            if (ps->options.font) {
                SendMessageW(ps->searchLabel, WM_SETFONT, reinterpret_cast<WPARAM>(ps->options.font), TRUE);
                SendMessageW(ps->searchEdit, WM_SETFONT, reinterpret_cast<WPARAM>(ps->options.font), TRUE);
                SendMessageW(ps->roomCombo, WM_SETFONT, reinterpret_cast<WPARAM>(ps->options.font), TRUE);
                SendMessageW(ps->machineList, WM_SETFONT, reinterpret_cast<WPARAM>(ps->options.font), TRUE);
            }
            reloadRooms(ps);
            layoutPicker(hwnd, ps);
            if (ps->searchEdit) SetFocus(ps->searchEdit);
            return 0;
        }
        case WM_SIZE:
            layoutPicker(hwnd, ps);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_PICKER_ROOM && HIWORD(wp) == CBN_SELCHANGE) {
                if (ps && !ps->syncingRoom) {
                    ps->roomChosenByUser = true;
                    if (ps->searchEdit) SetWindowTextW(ps->searchEdit, L"");
                }
                reloadMachines(ps);
                return 0;
            }
            if (LOWORD(wp) == IDC_PICKER_SEARCH && HIWORD(wp) == EN_CHANGE) {
                if (ps && !ps->roomChosenByUser && searchText(ps).empty())
                    selectRoomByCode(ps, "");
                populateMachines(ps);
                return 0;
            }
            break;
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (nm && nm->idFrom == IDC_PICKER_MACHINE && nm->code == NM_DBLCLK) {
                acceptSelection(hwnd, ps);
                return 0;
            }
            if (nm && nm->idFrom == IDC_PICKER_MACHINE && nm->code == LVN_KEYDOWN) {
                auto* key = reinterpret_cast<NMLVKEYDOWN*>(lp);
                if (key->wVKey == VK_RETURN) {
                    acceptSelection(hwnd, ps);
                    return 0;
                }
            }
            if (nm && nm->idFrom == IDC_PICKER_MACHINE && nm->code == NM_CLICK) {
                if (ps && ps->searchEdit) SetFocus(ps->searchEdit);
                return 0;
            }
            if (nm && nm->idFrom == IDC_PICKER_MACHINE && nm->code == LVN_ITEMCHANGED) {
                auto* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                if (lv && ps && !ps->refreshingMachines && (lv->uNewState & LVIS_SELECTED))
                    syncRoomFromSelection(ps);
                return 0;
            }
            break;
        }
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) {
                postClose(hwnd, ps);
                return 0;
            }
            break;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_NCDESTROY:
            delete ps;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void registerPickerClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = pickerProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = PICKER_CLASS;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassExW(&wc);
    registered = true;
}

int clampInt(int value, int minValue, int maxValue) {
    if (maxValue < minValue) return minValue;
    return std::clamp(value, minValue, maxValue);
}

POINT defaultPopupPoint(HWND owner, HWND anchor, int width, int height) {
    POINT pt{CW_USEDEFAULT, CW_USEDEFAULT};
    RECT basis{};
    bool hasBasis = false;
    if (anchor && IsWindow(anchor)) {
        GetWindowRect(anchor, &basis);
        hasBasis = true;
        pt.x = basis.left;
        pt.y = basis.bottom + 4;
    } else if (owner && IsWindow(owner)) {
        GetWindowRect(owner, &basis);
        hasBasis = true;
        pt.x = basis.left + 80;
        pt.y = basis.top + 80;
    }
    if (!hasBasis) return pt;

    HMONITOR monitor = MonitorFromRect(&basis, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) return pt;

    const RECT& work = mi.rcWork;
    if (pt.x + width > work.right) {
        pt.x = work.right - width;
    }
    if (anchor && IsWindow(anchor) && pt.y + height > work.bottom) {
        pt.y = basis.top - height - 4;
    }
    pt.x = clampInt(pt.x, work.left, work.right - width);
    pt.y = clampInt(pt.y, work.top, work.bottom - height);
    return pt;
}

}  // namespace

void show_machine_picker_popup(const MachinePickerPopupOptions& options) {
    registerPickerClass();
    auto* ps = new PickerState;
    ps->options = options;
    const int w = options.anchor ? S(options.anchor, PICKER_CLIENT_W) : PICKER_CLIENT_W;
    const int h = options.anchor ? S(options.anchor, PICKER_INITIAL_H) : PICKER_INITIAL_H;
    const POINT pt = defaultPopupPoint(options.owner, options.anchor, w, h);
    HWND popup = CreateWindowExW(WS_EX_TOOLWINDOW, PICKER_CLASS, L"选择检验仪器",
                                 WS_POPUP | WS_CAPTION,
                                 pt.x, pt.y, w, h,
                                 options.owner, nullptr, GetModuleHandleW(nullptr), ps);
    if (!popup) delete ps;
    else {
        ShowWindow(popup, SW_SHOWNORMAL);
        UpdateWindow(popup);
    }
}

}  // namespace search

#endif
