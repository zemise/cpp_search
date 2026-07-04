#include "regular_report_module.h"
#include "regular_report_state.h"

#ifdef _WIN32

#include "app_settings_io.h"
#include "barcode_label_printing.h"
#include "main_app.h"
#include "log.h"
#include "resource.h"
#include "search_controller.h"
#include "search_splitter.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "trend_window.h"
#include "win32_control_id.h"

#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <exception>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// ============================================================================
// Aliases to extracted utility functions (local shorthand in anonymous ns)
// ============================================================================

// These aliases make the panel code readable without the "regular" prefix.
// They're only valid within this translation unit's anonymous namespace.

namespace {

inline int S(HWND h, int v) { return regularS(h, v); }
inline HWND makeStatic(HWND p, const wchar_t* t, int x, int y, int w, int h, DWORD s = SS_LEFT) {
    return regularMakeStatic(p, t, x, y, w, h, s);
}
inline HWND makeEdit(HWND p, const wchar_t* t, int x, int y, int w, int h, DWORD e = ES_AUTOHSCROLL) {
    return regularMakeEdit(p, t, x, y, w, h, e);
}
inline HWND makeDatePicker(HWND p, int x, int y, int w, int h,
                            const wchar_t* f, const SYSTEMTIME* v, int id = 0) {
    return regularMakeDatePicker(p, x, y, w, h, f, v, id);
}

// ============================================================================

// Forward decls for functions defined later in this file
void runReportQuery(RegularReportState* st, bool preserveState = false);
void querySelectedResults(RegularReportState* st, int selected);
void sortReportRowsByColumn(RegularReportState* st, int column);
void populateLeftPanelFromReport(RegularReportState* st, int selected);
void selectReportRow(RegularReportState* st, int index);
void selectAdjacentReportRow(RegularReportState* st, int delta);
bool hasSelectedReportRow(const RegularReportState* st);
int currentReportIndex(const RegularReportState* st);
void beginResultEdit(RegularReportState* st, int row);
void finishResultEdit(RegularReportState* st, bool commit, bool moveNext = false,
                      bool restoreListFocus = true);
bool inspectDateMatchesCurrentQuery(const RegularReportState* st);
void setInspectDateAndQuery(RegularReportState* st, SYSTEMTIME date, bool preserve = false);
LRESULT CALLBACK resultListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                UINT_PTR subclassId, DWORD_PTR data);

// ============================================================================
// Local helpers
// ============================================================================

HWND makeButton(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", text,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS,
                           x, y, w, h, parent, win32_control_id(id),
                           GetModuleHandleW(nullptr), nullptr);
}

HWND makeCombo(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    HWND combo = regularAddClipSiblings(search::create_combo(parent, 0, x, y, w, h, false));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
    return combo;
}

constexpr int REGULAR_TREND_DEFAULT_DAYS = 14;

std::string regularDateText(SYSTEMTIME st) {
    st = regularNormalizeDate(st);
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
    return buffer;
}

SYSTEMTIME regularReportTrendEndDate(const search::ReportRow& row) {
    SYSTEMTIME end{};
    if (regularParseDateTimeText(row.chk_date, end)) return regularNormalizeDate(end);
    return regularTodayDate();
}

// ============================================================================
// Machine picker
// ============================================================================

void acceptAndCloseMachinePicker(HWND hwnd, MachinePickerState* ps);

std::string selectedMachinePickerRoomCode(MachinePickerState* ps) {
    if (!ps || !ps->roomCombo) return "";
    const auto idx = static_cast<int>(SendMessageW(ps->roomCombo, CB_GETCURSEL, 0, 0));
    if (idx <= 0) return "";
    const int roomIndex = idx - 1;
    if (roomIndex < 0 || roomIndex >= static_cast<int>(ps->rooms.size())) return "";
    return ps->rooms[static_cast<size_t>(roomIndex)].room_code;
}

std::string machinePickerSearchText(MachinePickerState* ps) {
    if (!ps || !ps->searchEdit) return "";
    wchar_t buffer[128]{};
    GetWindowTextW(ps->searchEdit, buffer, 128);
    return search::trim(search::wide_to_utf8(buffer));
}

std::string lowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

bool machineMatchesSearch(const search::MachineOption& row, const std::string& needle) {
    const std::string q = lowerAscii(search::trim(needle));
    if (q.empty()) return true;
    const std::string code = lowerAscii(search::trim(row.mach_code));
    const std::string py = lowerAscii(search::trim(row.py_code));
    return code.find(q) != std::string::npos || py.find(q) != std::string::npos;
}

void selectMachinePickerRoomByCode(MachinePickerState* ps, const std::string& roomCode) {
    if (!ps || !ps->roomCombo) return;
    const std::string target = search::trim(roomCode);
    if (target.empty()) {
        ps->syncingRoom = true;
        SendMessageW(ps->roomCombo, CB_SETCURSEL, 0, 0);
        ps->syncingRoom = false;
        return;
    }
    for (int i = 0; i < static_cast<int>(ps->rooms.size()); ++i) {
        if (search::trim(ps->rooms[static_cast<size_t>(i)].room_code) == target) {
            ps->syncingRoom = true;
            SendMessageW(ps->roomCombo, CB_SETCURSEL, i + 1, 0);
            ps->syncingRoom = false;
            return;
        }
    }
}

void syncMachinePickerRoomFromSelection(MachinePickerState* ps) {
    if (!ps || !ps->machineList) return;
    const int sel = ListView_GetNextItem(ps->machineList, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= static_cast<int>(ps->machines.size())) return;
    selectMachinePickerRoomByCode(ps, ps->machines[static_cast<size_t>(sel)].room_code);
}

void applyMachinePickerFilter(MachinePickerState* ps) {
    if (!ps) return;
    ps->machines.clear();
    const std::string keyword = machinePickerSearchText(ps);
    const std::string roomCode = search::trim(selectedMachinePickerRoomCode(ps));
    const bool restrictByRoom = keyword.empty() && ps->roomChosenByUser && !roomCode.empty();
    for (const auto& row : ps->allMachines) {
        if (restrictByRoom && search::trim(row.room_code) != roomCode)
            continue;
        if (machineMatchesSearch(row, keyword))
            ps->machines.push_back(row);
    }
}

int machinePickerListHeight(HWND hwnd, MachinePickerState* ps) {
    const int vis = std::clamp(static_cast<int>(ps ? ps->machines.size() : 0),
                               REGULAR_MACHINE_PICKER_MIN_ROWS, REGULAR_MACHINE_PICKER_MAX_ROWS);
    const int fh = ps && ps->report ? regularFontLogicalHeight(hwnd, ps->report->ctx.uiFont) : 16;
    const int rh = S(hwnd, std::max(22, fh + 6));
    return S(hwnd, REGULAR_MACHINE_PICKER_HEADER_H) + vis * rh +
           S(hwnd, REGULAR_MACHINE_PICKER_LIST_EXTRA_H);
}

void layoutMachinePicker(HWND hwnd, MachinePickerState* ps) {
    if (!hwnd || !ps || !ps->machineList) return;
    const int ix = S(hwnd, REGULAR_MACHINE_PICKER_INPUT_X);
    const int searchY = S(hwnd, REGULAR_MACHINE_PICKER_SEARCH_Y);
    const int labelW = S(hwnd, REGULAR_MACHINE_PICKER_SEARCH_LABEL_W);
    const int editX = ix + labelW;
    const int editW = S(hwnd, REGULAR_MACHINE_PICKER_LIST_W) - labelW;
    if (ps->searchEdit)
        MoveWindow(ps->searchEdit, editX, searchY, editW, S(hwnd, 24), TRUE);
    if (ps->roomCombo)
        MoveWindow(ps->roomCombo, ix, S(hwnd, REGULAR_MACHINE_PICKER_ROOM_Y),
                   S(hwnd, REGULAR_MACHINE_PICKER_LIST_W),
                   S(hwnd, REGULAR_MACHINE_PICKER_COMBO_DROP_H), TRUE);
    const int ly = S(hwnd, REGULAR_MACHINE_PICKER_LIST_Y);
    const int lw = S(hwnd, REGULAR_MACHINE_PICKER_LIST_W);
    const int lh = machinePickerListHeight(hwnd, ps);
    MoveWindow(ps->machineList, ix, ly, lw, lh, TRUE);
    ListView_SetColumnWidth(ps->machineList, 0, S(hwnd, REGULAR_MACHINE_PICKER_CODE_COL_W));
    ListView_SetColumnWidth(ps->machineList, 1, S(hwnd, REGULAR_MACHINE_PICKER_NAME_COL_W));
    ListView_SetColumnWidth(ps->machineList, 2, S(hwnd, REGULAR_MACHINE_PICKER_GROUP_CODE_COL_W));
    ListView_SetColumnWidth(ps->machineList, 3, S(hwnd, REGULAR_MACHINE_PICKER_GROUP_NAME_COL_W));
    ListView_SetColumnWidth(ps->machineList, 4, S(hwnd, REGULAR_MACHINE_PICKER_SAMPLE_COL_W));
    ListView_SetColumnWidth(ps->machineList, 5, S(hwnd, REGULAR_MACHINE_PICKER_PY_COL_W));

    RECT cr{0, 0, S(hwnd, REGULAR_MACHINE_PICKER_CLIENT_W),
            ly + lh + S(hwnd, REGULAR_MACHINE_PICKER_BOTTOM_PAD)};
    AdjustWindowRectEx(&cr, GetWindowLongW(hwnd, GWL_STYLE), FALSE,
                       GetWindowLongW(hwnd, GWL_EXSTYLE));
    SetWindowPos(hwnd, nullptr, 0, 0, cr.right - cr.left, cr.bottom - cr.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void populateMachinePickerRooms(MachinePickerState* ps) {
    if (!ps || !ps->roomCombo) return;
    regularComboReset(ps->roomCombo);
    regularComboAdd(ps->roomCombo, L"全部");
    for (const auto& row : ps->rooms) {
        regularComboAdd(ps->roomCombo, search::utf8_to_wide(row.room_name));
    }
    SendMessageW(ps->roomCombo, CB_SETCURSEL, 0, 0);
}

void populateMachinePickerMachines(MachinePickerState* ps) {
    if (!ps || !ps->machineList) return;
    applyMachinePickerFilter(ps);
    ps->refreshingMachines = true;
    ListView_DeleteAllItems(ps->machineList);
    const std::string curCode = ps->report ? search::trim(ps->report->selectedMachineCode) : "";
    const std::string keyword = machinePickerSearchText(ps);
    wchar_t curName[256]{};
    if (ps->report && ps->report->machineEdit)
        GetWindowTextW(ps->report->machineEdit, curName, 256);
    int sel = -1;
    for (int i = 0; i < static_cast<int>(ps->machines.size()); ++i) {
        const auto code = search::utf8_to_wide(ps->machines[static_cast<size_t>(i)].mach_code);
        const auto name = search::utf8_to_wide(ps->machines[static_cast<size_t>(i)].mach_name);
        const auto groupCode = search::utf8_to_wide(ps->machines[static_cast<size_t>(i)].group_code);
        const auto groupName = search::utf8_to_wide(ps->machines[static_cast<size_t>(i)].group_name);
        const auto sample = search::utf8_to_wide(ps->machines[static_cast<size_t>(i)].sample_name);
        const auto py = search::utf8_to_wide(ps->machines[static_cast<size_t>(i)].py_code);
        LVITEMW item{};
        item.mask = LVIF_TEXT; item.iItem = i; item.iSubItem = 0;
        item.pszText = const_cast<wchar_t*>(code.c_str());
        ListView_InsertItem(ps->machineList, &item);
        ListView_SetItemText(ps->machineList, i, 1, const_cast<wchar_t*>(name.c_str()));
        ListView_SetItemText(ps->machineList, i, 2, const_cast<wchar_t*>(groupCode.c_str()));
        ListView_SetItemText(ps->machineList, i, 3, const_cast<wchar_t*>(groupName.c_str()));
        ListView_SetItemText(ps->machineList, i, 4, const_cast<wchar_t*>(sample.c_str()));
        ListView_SetItemText(ps->machineList, i, 5, const_cast<wchar_t*>(py.c_str()));
        if (keyword.empty() && sel < 0 && !curCode.empty() &&
            search::trim(ps->machines[static_cast<size_t>(i)].mach_code) == curCode) sel = i;
        else if (keyword.empty() && sel < 0 && curName[0] && lstrcmpW(curName, name.c_str()) == 0) sel = i;
    }
    if (sel < 0 && !ps->machines.empty()) sel = 0;
    if (sel >= 0) {
        ListView_SetItemState(ps->machineList, sel, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(ps->machineList, sel, FALSE);
    }
    ps->refreshingMachines = false;
    if (sel >= 0 && !keyword.empty())
        syncMachinePickerRoomFromSelection(ps);
    layoutMachinePicker(ps->report ? ps->report->machinePickerPopup : nullptr, ps);
}

void reloadMachinePickerRooms(MachinePickerState* ps) {
    if (!ps || !ps->report || !ps->roomCombo) return;
    ps->rooms.clear();
    ps->allMachines.clear();
    const std::string cacheKey =
        search::wide_to_utf8(search::build_connection_string_w(ps->report->ctx.dbSettings));
    if (ps->report->machinePickerCacheConnectionString != cacheKey) {
        ps->report->machinePickerCacheLoaded = false;
        ps->report->cachedMachinePickerRooms.clear();
        ps->report->cachedMachinePickerMachines.clear();
        ps->report->machinePickerCacheConnectionString = cacheKey;
    }
    if (!ps->report->machinePickerCacheLoaded) {
        std::vector<search::RoomOption> rooms;
        std::vector<search::MachineOption> machines;
        std::string error;
        if (!search::load_report_machine_picker_room_options(ps->report->ctx.dbSettings, rooms, error)) {
            populateMachinePickerRooms(ps);
            populateMachinePickerMachines(ps);
            MessageBoxW(ps->report->machinePickerPopup ? ps->report->machinePickerPopup
                                                       : ps->report->leftContent,
                        L"检验科室加载失败。", L"常规报告", MB_ICONERROR);
            return;
        }
        if (!search::load_report_machine_picker_machine_options(ps->report->ctx.dbSettings, "", machines, error)) {
            populateMachinePickerRooms(ps);
            populateMachinePickerMachines(ps);
            MessageBoxW(ps->report->machinePickerPopup ? ps->report->machinePickerPopup
                                                       : ps->report->leftContent,
                        L"检验仪器加载失败。", L"常规报告", MB_ICONERROR);
            return;
        }
        ps->report->cachedMachinePickerRooms = std::move(rooms);
        ps->report->cachedMachinePickerMachines = std::move(machines);
        ps->report->machinePickerCacheConnectionString = cacheKey;
        ps->report->machinePickerCacheLoaded = true;
    }
    ps->rooms = ps->report->cachedMachinePickerRooms;
    ps->allMachines = ps->report->cachedMachinePickerMachines;
    if (ps->rooms.empty() && ps->allMachines.empty()) {
        populateMachinePickerRooms(ps);
        populateMachinePickerMachines(ps);
        return;
    }
    populateMachinePickerRooms(ps);
    populateMachinePickerMachines(ps);
}

void acceptMachinePicker(MachinePickerState* ps) {
    if (!ps || !ps->report || !ps->report->machineEdit || !ps->machineList) return;
    const int sel = ListView_GetNextItem(ps->machineList, -1, LVNI_SELECTED);
    if (sel >= 0 && sel < static_cast<int>(ps->machines.size())) {
        const auto& m = ps->machines[static_cast<size_t>(sel)];
        SetWindowTextW(ps->report->machineEdit, search::utf8_to_wide(m.mach_name).c_str());
        ps->report->selectedMachineCode = m.mach_code;
        ps->report->selectedRoomCode = m.room_code;
        regularUpdateQuickMachineButtonLabels(ps->report);
    }
}

void acceptAndCloseMachinePicker(HWND hwnd, MachinePickerState* ps) {
    RegularReportState* rpt = ps ? ps->report : nullptr;
    acceptMachinePicker(ps);
    DestroyWindow(hwnd);
    runReportQuery(rpt);
}

void selectMachinePickerRow(MachinePickerState* ps, int row) {
    if (!ps || !ps->machineList) return;
    const int count = ListView_GetItemCount(ps->machineList);
    if (count <= 0) return;
    row = std::clamp(row, 0, count - 1);
    ListView_SetItemState(ps->machineList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(ps->machineList, row, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(ps->machineList, row, FALSE);
    syncMachinePickerRoomFromSelection(ps);
    regularRedrawSelectedListRow(ps->machineList);
}

void moveMachinePickerSelection(MachinePickerState* ps, int delta) {
    if (!ps || !ps->machineList || delta == 0) return;
    int row = ListView_GetNextItem(ps->machineList, -1, LVNI_SELECTED);
    if (row < 0) row = 0;
    else row += delta;
    selectMachinePickerRow(ps, row);
}

LRESULT CALLBACK machinePickerSearchProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                         UINT_PTR sid, DWORD_PTR data) {
    auto* ps = reinterpret_cast<MachinePickerState*>(data);
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
                moveMachinePickerSelection(ps, wp == VK_UP ? -1 : 1);
                return 0;
            }
            if (wp == VK_RETURN) {
                HWND popup = GetParent(hwnd);
                if (IsWindow(popup)) {
                    acceptAndCloseMachinePicker(popup, ps);
                    return 0;
                }
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, machinePickerSearchProc, sid);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void postCloseMachinePicker(HWND hwnd, MachinePickerState* ps) {
    if (!ps || ps->closePosted) return;
    ps->closePosted = true;
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

LRESULT CALLBACK machinePickerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ps = reinterpret_cast<MachinePickerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
            break;
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            ps = reinterpret_cast<MachinePickerState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ps));
            if (!ps || !ps->report) return -1;
            ps->report->machinePickerPopup = hwnd;
            const int ix = S(hwnd, REGULAR_MACHINE_PICKER_INPUT_X);
            makeStatic(hwnd, L"检索内容", ix, S(hwnd, REGULAR_MACHINE_PICKER_SEARCH_LABEL_Y),
                       S(hwnd, REGULAR_MACHINE_PICKER_SEARCH_LABEL_W), S(hwnd, 18));
            ps->searchEdit = makeEdit(hwnd, L"",
                ix + S(hwnd, REGULAR_MACHINE_PICKER_SEARCH_LABEL_W),
                S(hwnd, REGULAR_MACHINE_PICKER_SEARCH_Y),
                S(hwnd, REGULAR_MACHINE_PICKER_LIST_W - REGULAR_MACHINE_PICKER_SEARCH_LABEL_W),
                S(hwnd, 24));
            SetWindowLongPtrW(ps->searchEdit, GWLP_ID, REGULAR_IDC_MACHINE_PICKER_SEARCH);
            SetWindowSubclass(ps->searchEdit, machinePickerSearchProc,
                              REGULAR_MACHINE_PICKER_SEARCH_SUBCLASS,
                              reinterpret_cast<DWORD_PTR>(ps));
            ps->roomCombo = search::create_combo(hwnd, REGULAR_IDC_MACHINE_PICKER_ROOM,
                ix, S(hwnd, REGULAR_MACHINE_PICKER_ROOM_Y),
                S(hwnd, REGULAR_MACHINE_PICKER_LIST_W),
                S(hwnd, REGULAR_MACHINE_PICKER_COMBO_DROP_H), false);
            ps->machineList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                ix, S(hwnd, REGULAR_MACHINE_PICKER_LIST_Y),
                S(hwnd, REGULAR_MACHINE_PICKER_LIST_W),
                S(hwnd, REGULAR_MACHINE_PICKER_INITIAL_LIST_H),
                hwnd, win32_control_id(REGULAR_IDC_MACHINE_PICKER_MACH),
                GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(ps->machineList,
                LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            search::add_list_column(ps->machineList, 0, L"仪器",
                                     S(hwnd, REGULAR_MACHINE_PICKER_CODE_COL_W));
            search::add_list_column(ps->machineList, 1, L"仪器名称",
                                     S(hwnd, REGULAR_MACHINE_PICKER_NAME_COL_W));
            search::add_list_column(ps->machineList, 2, L"项目代码",
                                     S(hwnd, REGULAR_MACHINE_PICKER_GROUP_CODE_COL_W));
            search::add_list_column(ps->machineList, 3, L"项目名称",
                                     S(hwnd, REGULAR_MACHINE_PICKER_GROUP_NAME_COL_W));
            search::add_list_column(ps->machineList, 4, L"样本",
                                     S(hwnd, REGULAR_MACHINE_PICKER_SAMPLE_COL_W));
            search::add_list_column(ps->machineList, 5, L"拼音码",
                                     S(hwnd, REGULAR_MACHINE_PICKER_PY_COL_W));
            regularApplyFont(hwnd, ps->report->ctx.uiFont);
            reloadMachinePickerRooms(ps);
            return 0;
        }
        case WM_COMMAND: {
            if (LOWORD(wp) == REGULAR_IDC_MACHINE_PICKER_ROOM && HIWORD(wp) == CBN_SELCHANGE) {
                if (ps && !ps->syncingRoom) {
                    ps->roomChosenByUser = true;
                    if (ps->searchEdit)
                        SetWindowTextW(ps->searchEdit, L"");
                }
                populateMachinePickerMachines(ps);
                return 0;
            }
            if (LOWORD(wp) == REGULAR_IDC_MACHINE_PICKER_SEARCH && HIWORD(wp) == EN_CHANGE) {
                if (ps && !ps->roomChosenByUser && machinePickerSearchText(ps).empty())
                    selectMachinePickerRoomByCode(ps, "");
                populateMachinePickerMachines(ps);
                return 0;
            }
            break;
        }
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (nm && nm->idFrom == REGULAR_IDC_MACHINE_PICKER_MACH &&
                nm->code == NM_KILLFOCUS) {
                regularRedrawSelectedListRow(ps ? ps->machineList : nullptr);
                return 0;
            }
            if (nm && nm->idFrom == REGULAR_IDC_MACHINE_PICKER_MACH &&
                nm->code == NM_CUSTOMDRAW) {
                auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    const int row = static_cast<int>(cd->nmcd.dwItemSpec);
                    if (regularCustomDrawListSelection(cd, ps ? ps->machineList : nullptr, row))
                        return CDRF_NOTIFYSUBITEMDRAW;
                    return CDRF_NOTIFYSUBITEMDRAW;
                }
                if (cd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                    const int row = static_cast<int>(cd->nmcd.dwItemSpec);
                    regularCustomDrawListSelection(cd, ps ? ps->machineList : nullptr, row);
                    return CDRF_DODEFAULT;
                }
            }
            if (nm && nm->idFrom == REGULAR_IDC_MACHINE_PICKER_MACH &&
                (nm->code == NM_RETURN || nm->code == NM_DBLCLK)) {
                acceptAndCloseMachinePicker(hwnd, ps);
                return 0;
            }
            if (nm && nm->idFrom == REGULAR_IDC_MACHINE_PICKER_MACH &&
                nm->code == NM_CLICK) {
                if (ps && ps->searchEdit)
                    SetFocus(ps->searchEdit);
                return 0;
            }
            if (nm && nm->idFrom == REGULAR_IDC_MACHINE_PICKER_MACH &&
                nm->code == LVN_ITEMCHANGED) {
                auto* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                if (lv && ps && !ps->refreshingMachines && (lv->uNewState & LVIS_SELECTED))
                    syncMachinePickerRoomFromSelection(ps);
                return 0;
            }
            break;
        }
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) { postCloseMachinePicker(hwnd, ps); return 0; }
            break;
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
        case WM_NCDESTROY:
            if (ps) {
                if (ps->report && ps->report->machinePickerPopup == hwnd)
                    ps->report->machinePickerPopup = nullptr;
                delete ps;
            }
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void registerMachinePickerClass(HINSTANCE instance) {
    static bool reg = false;
    if (reg) return;
    REGISTER_MDI_CHILD_CLASS(instance, machinePickerProc, REGULAR_MACHINE_PICKER_CLASS, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
}

void showMachinePicker(RegularReportState* st, HWND anchor) {
    if (!st || !anchor) return;
    if (IsWindow(st->machinePickerPopup)) {
        SetForegroundWindow(st->machinePickerPopup);
        auto* ps = reinterpret_cast<MachinePickerState*>(
            GetWindowLongPtrW(st->machinePickerPopup, GWLP_USERDATA));
        if (ps && ps->searchEdit)
            SetFocus(ps->searchEdit);
        return;
    }
    registerMachinePickerClass(st->ctx.instance);
    RECT ar{}; GetWindowRect(anchor, &ar);
    const int w = S(anchor, REGULAR_MACHINE_PICKER_CLIENT_W);
    const int h = S(anchor, REGULAR_MACHINE_PICKER_INITIAL_H);
    int x = ar.left, y = ar.bottom + S(anchor, 2);
    HMONITOR mon = MonitorFromRect(&ar, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{}; mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        x = std::min(x, static_cast<int>(mi.rcWork.right) - w);
        y = std::min(y, static_cast<int>(mi.rcWork.bottom) - h);
        x = std::max(x, static_cast<int>(mi.rcWork.left));
        y = std::max(y, static_cast<int>(mi.rcWork.top));
    }
    auto* ps = new MachinePickerState; ps->report = st;
    HWND popup = CreateWindowExW(WS_EX_TOOLWINDOW, REGULAR_MACHINE_PICKER_CLASS,
                                 L"选择检验仪器", WS_POPUP | WS_CAPTION,
                                 x, y, w, h,
                                 GetAncestor(st->leftContent, GA_ROOT),
                                 nullptr, st->ctx.instance, ps);
    if (!popup) { delete ps; return; }
    ShowWindow(popup, SW_SHOWNORMAL); UpdateWindow(popup);
    if (ps->searchEdit)
        SetFocus(ps->searchEdit);
}

// ============================================================================
// Left panel — scroll + subclass + creation
// ============================================================================

void clearLeftPanel(RegularReportState* st) {
    if (!st) return;
    for (HWND child : st->leftControls)
        if (IsWindow(child)) DestroyWindow(child);
    st->leftControls.clear();
    st->leftTabControls.clear();
    st->leftGroupFrames.clear();
    // Null out all left-side HWND members
    st->machineEdit = nullptr; st->machinePickerButton = nullptr; st->groupEdit = nullptr;
    st->sampleEdit = nullptr; st->reportNoEdit = nullptr; st->operNoEdit = nullptr;
    st->patientTypeCombo = nullptr; st->urgentCheck = nullptr; st->urgentLabel = nullptr;
    st->urgentEdit = nullptr; st->barcodeEdit = nullptr; st->regNoEdit = nullptr;
    st->patientNameEdit = nullptr; st->sexEdit = nullptr; st->ageEdit = nullptr;
    st->ageUnitCombo = nullptr; st->bedEdit = nullptr; st->phoneEdit = nullptr;
    st->deptEdit = nullptr; st->diagEdit = nullptr; st->reqDoctorEdit = nullptr;
    st->feeEdit = nullptr; st->testerEdit = nullptr; st->auditEdit = nullptr;
    st->noteCodeEdit = nullptr; st->noteEdit = nullptr;
    st->applyDatePicker = nullptr; st->receiveDatePicker = nullptr;
    st->machineDatePicker = nullptr; st->reportDatePicker = nullptr;
    st->inspectDatePicker = nullptr; st->collectDateEdit = nullptr;
}

void updateLeftScrollBar(RegularReportState* st) {
    if (!st || !st->leftPanel || !st->leftContent || !st->leftScrollBar) return;
    RECT rc{}; GetClientRect(st->leftPanel, &rc);
    const int page = std::max(1, static_cast<int>(rc.bottom - rc.top));
    const int contentH = S(st->leftPanel, std::max(REGULAR_LEFT_CONTENT_HEIGHT, st->leftContentHeight));
    const int maxScroll = std::max(0, contentH - page);
    st->leftScrollY = std::clamp(st->leftScrollY, 0, maxScroll);
    const int scrollW = GetSystemMetrics(SM_CXVSCROLL);
    const int contentW = std::max(0, static_cast<int>(rc.right - rc.left) - scrollW);
    MoveWindow(st->leftContent, 0, -st->leftScrollY, contentW, contentH, TRUE);
    SetWindowPos(st->leftScrollBar, HWND_TOP, contentW, 0, scrollW, page, SWP_SHOWWINDOW);
    SCROLLINFO si{};
    si.cbSize = sizeof(si); si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0; si.nMax = std::max(0, contentH - 1);
    si.nPage = static_cast<UINT>(page); si.nPos = st->leftScrollY;
    SetScrollInfo(st->leftScrollBar, SB_CTL, &si, TRUE);
    ShowWindow(st->leftScrollBar, contentH > page ? SW_SHOW : SW_HIDE);
}

void scrollLeftPanelTo(RegularReportState* st, int targetY) {
    if (!st || !st->leftPanel || !st->leftContent) return;
    RECT rc{}; GetClientRect(st->leftPanel, &rc);
    const int page = static_cast<int>(rc.bottom - rc.top);
    const int maxScroll = std::max(0, S(st->leftPanel,
        std::max(REGULAR_LEFT_CONTENT_HEIGHT, st->leftContentHeight)) - page);
    st->leftScrollY = std::clamp(targetY, 0, maxScroll);
    updateLeftScrollBar(st);
}

void drawLeftGroupFrames(HWND hwnd, RegularReportState* st, HDC dc) {
    RECT client{}; GetClientRect(hwnd, &client);
    FillRect(dc, &client, st && st->panelBrush ? st->panelBrush
                                                : GetSysColorBrush(COLOR_BTNFACE));
    if (!st) return;
    HBRUSH fb = GetSysColorBrush(COLOR_GRAYTEXT);
    HGDIOBJ oldF = nullptr;
    HFONT tf = st->groupTitleFont ? st->groupTitleFont : st->ctx.uiFont;
    if (tf) oldF = SelectObject(dc, tf);
    SetBkMode(dc, OPAQUE); SetBkColor(dc, RGB(0xEF, 0xEF, 0xEF)); SetTextColor(dc, RGB(0, 0, 0));
    for (const auto& f : st->leftGroupFrames) {
        RECT rc = f.rect;
        FrameRect(dc, &rc, fb);
        RECT tr = rc; tr.left += S(hwnd, 14);
        SIZE ts{};
        GetTextExtentPoint32W(dc, f.title, static_cast<int>(wcslen(f.title)), &ts);
        tr.top = rc.top - std::max(S(hwnd, 8), static_cast<int>(ts.cy) * 2 / 3);
        tr.right = std::min(rc.right - S(hwnd, 8), tr.left + ts.cx + S(hwnd, 24));
        tr.bottom = tr.top + std::max(S(hwnd, 20), static_cast<int>(ts.cy) + S(hwnd, 6));
        ExtTextOutW(dc, tr.left, tr.top, ETO_OPAQUE, &tr, L"", 0, nullptr);
        DrawTextW(dc, f.title, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    }
    if (oldF) SelectObject(dc, oldF);
}

LRESULT CALLBACK leftPanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                               UINT_PTR sid, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_VSCROLL: {
            if (!st) break;
            SCROLLINFO si{}; si.cbSize = sizeof(si); si.fMask = SIF_ALL;
            GetScrollInfo(st->leftScrollBar, SB_CTL, &si);
            int t = st->leftScrollY;
            switch (LOWORD(wp)) {
                case SB_LINEUP: t -= S(hwnd, REGULAR_LEFT_SCROLL_STEP); break;
                case SB_LINEDOWN: t += S(hwnd, REGULAR_LEFT_SCROLL_STEP); break;
                case SB_PAGEUP: t -= static_cast<int>(si.nPage); break;
                case SB_PAGEDOWN: t += static_cast<int>(si.nPage); break;
                case SB_THUMBTRACK: case SB_THUMBPOSITION: t = si.nTrackPos; break;
                case SB_TOP: t = 0; break;
                case SB_BOTTOM: t = si.nMax; break;
                default: return 0;
            }
            scrollLeftPanelTo(st, t); return 0;
        }
        case WM_MOUSEWHEEL:
            if (st) {
                const int d = GET_WHEEL_DELTA_WPARAM(wp);
                scrollLeftPanelTo(st, st->leftScrollY -
                    (d / WHEEL_DELTA) * S(hwnd, REGULAR_LEFT_SCROLL_STEP * 3));
                return 0;
            }
            break;
        case WM_NCDESTROY: RemoveWindowSubclass(hwnd, leftPanelProc, sid); break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK leftContentProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                 UINT_PTR sid, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_COMMAND:
            if (st && LOWORD(wp) == REGULAR_IDC_MACHINE_PICKER_BUTTON && HIWORD(wp) == BN_CLICKED) {
                showMachinePicker(st, reinterpret_cast<HWND>(lp)); return 0;
            }
            break;
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (st && nm && nm->idFrom == REGULAR_IDC_INSPECT_DATE &&
                nm->code == DTN_DATETIMECHANGE && !st->suppressInspectDateQuery &&
                !search::trim(st->selectedMachineCode).empty()) {
                runReportQuery(st, hasSelectedReportRow(st) && inspectDateMatchesCurrentQuery(st));
                return 0;
            }
            break;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps);
            drawLeftGroupFrames(hwnd, st, dc);
            EndPaint(hwnd, &ps); return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            HWND ctl = reinterpret_cast<HWND>(lp);
            if (GetPropW(ctl, L"RegularEmergencyLabel")) {
                SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(0xE6, 0, 0));
                return reinterpret_cast<LRESULT>(st ? st->panelBrush : nullptr);
            }
            if (GetPropW(ctl, L"RegularLeftLabel")) {
                SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(0x00, 0x00, 0xC4));
                return reinterpret_cast<LRESULT>(st ? st->panelBrush : nullptr);
            }
            break;
        }
        case WM_NCDESTROY: RemoveWindowSubclass(hwnd, leftContentProc, sid); break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// ============================================================================
// Right panel — summary paint + tab switching
// ============================================================================

void showRightInfoPage(RegularReportState* st) {
    if (!st || !st->rightTab) return;
    const bool show = TabCtrl_GetCurSel(st->rightTab) == 0;
    for (HWND c : st->rightInfoControls)
        if (IsWindow(c)) ShowWindow(c, show ? SW_SHOW : SW_HIDE);
    if (IsWindow(st->reportList)) ShowWindow(st->reportList, show ? SW_SHOW : SW_HIDE);
    InvalidateRect(st->rightPanel, nullptr, TRUE);
}

LRESULT CALLBACK rightPanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                UINT_PTR sid, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps);
            RECT c{}; GetClientRect(hwnd, &c);
            FillRect(dc, &c, st && st->panelBrush ? st->panelBrush
                                                   : GetSysColorBrush(COLOR_BTNFACE));
            if (st) {
                const auto s1 = regularRightSummaryLine1(st);
                const auto s2 = regularRightSummaryLine2(st);
                const auto hdr = regularRightHeaderLayout(hwnd, st->ctx.uiFont,
                    c.right - c.left, s1, s2);
                HGDIOBJ of = st->ctx.uiFont ? SelectObject(dc, st->ctx.uiFont) : nullptr;
                SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(0, 0, 0xCC));
                RECT l1 = hdr.line1;
                DrawTextW(dc, s1.c_str(), -1, &l1, DT_WORDBREAK | DT_CENTER | DT_VCENTER);
                FillRect(dc, &hdr.line2,
                    st->blackBrush ? st->blackBrush
                                   : reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                SetTextColor(dc, RGB(0xFF, 0xFF, 0));
                RECT l2 = hdr.line2;
                DrawTextW(dc, s2.c_str(), -1, &l2, DT_WORDBREAK | DT_CENTER | DT_VCENTER);
                if (of) SelectObject(dc, of);
            }
            EndPaint(hwnd, &ps); return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            if (st && id == REGULAR_IDC_REPORT_FIRST_BUTTON) { selectReportRow(st, 0); return 0; }
            if (st && id == REGULAR_IDC_REPORT_LAST_BUTTON) {
                if (st->reportList) {
                    const int last = static_cast<int>(st->reportRows.size()) - 1;
                    ListView_SetItemState(st->reportList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
                    ListView_SetItemState(st->reportList, last, LVIS_SELECTED | LVIS_FOCUSED,
                                          LVIS_SELECTED | LVIS_FOCUSED);
                    ListView_EnsureVisible(st->reportList, last, FALSE);
                    SetFocus(st->reportList);
                }
                return 0;
            }
            if (st && id == REGULAR_IDC_REPORT_DATE_TODAY_BUTTON) {
                setInspectDateAndQuery(st, regularTodayDate(), true); return 0;
            }
            if (st && id == REGULAR_IDC_REPORT_DATE_PREV_BUTTON) {
                setInspectDateAndQuery(st, regularAddDays(
                    regularDatePickerSystemTime(st->inspectDatePicker), -1)); return 0;
            }
            if (st && id == REGULAR_IDC_REPORT_DATE_NEXT_BUTTON) {
                setInspectDateAndQuery(st, regularAddDays(
                    regularDatePickerSystemTime(st->inspectDatePicker), 1)); return 0;
            }
            if (st && id == REGULAR_IDC_REPORT_AUTO_REFRESH_CHECK && HIWORD(wp) == BN_CLICKED) {
                regularUpdateAutoRefreshTimer(st); return 0;
            }
            if (st && id == REGULAR_IDC_REPORT_AUTO_REFRESH_SECONDS && HIWORD(wp) == EN_CHANGE) {
                regularUpdateAutoRefreshTimer(st); return 0;
            }
            break;
        }
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (st && nm->idFrom == REGULAR_IDC_RIGHT_TAB && nm->code == TCN_SELCHANGE) {
                showRightInfoPage(st); return 0;
            }
            if (st && nm->idFrom == REGULAR_IDC_REPORT_LIST && nm->code == LVN_ITEMCHANGED) {
                auto* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED) &&
                    !(lv->uOldState & LVIS_SELECTED)) {
                    if (!st->suppressReportSelectionQuery) querySelectedResults(st, lv->iItem);
                    return 0;
                }
            }
            if (st && nm->idFrom == REGULAR_IDC_REPORT_LIST && nm->code == LVN_COLUMNCLICK) {
                auto* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                sortReportRowsByColumn(st, lv->iSubItem); return 0;
            }
            if (st && nm->idFrom == REGULAR_IDC_REPORT_LIST && nm->code == NM_RCLICK) {
                regularShowReportContextMenu(st, reinterpret_cast<NMITEMACTIVATE*>(lp));
                return 0;
            }
            if (st && nm->idFrom == REGULAR_IDC_REPORT_LIST && nm->code == NM_KILLFOCUS) {
                regularRedrawSelectedListRow(st->reportList); return 0;
            }
            if (nm->idFrom == REGULAR_IDC_REPORT_LIST && nm->code == NM_CUSTOMDRAW) {
                auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    const int row = static_cast<int>(cd->nmcd.dwItemSpec);
                    if (regularCustomDrawListSelection(cd, st ? st->reportList : nullptr, row))
                        return CDRF_NOTIFYSUBITEMDRAW;
                    return CDRF_NOTIFYSUBITEMDRAW;
                }
                if (cd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                    const int row = static_cast<int>(cd->nmcd.dwItemSpec);
                    if (!regularCustomDrawListSelection(cd, st ? st->reportList : nullptr, row)) {
                        cd->clrTextBk = cd->iSubItem == REGULAR_RIGHT_REPORT_PRINT_COL
                            ? regularReportPrintCellColor(st, row)
                            : regularReportRowColor(st, row);
                        cd->clrText = st && row >= 0 &&
                            row < static_cast<int>(st->reportRows.size()) &&
                            regularReportUsesEmergencyTextColor(st->reportRows[static_cast<size_t>(row)])
                            ? RGB(0xE6, 0, 0) : RGB(0, 0, 0);
                    }
                    return CDRF_NEWFONT;
                }
            }
            break;
        }
        case WM_NCDESTROY: RemoveWindowSubclass(hwnd, rightPanelProc, sid); break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// ============================================================================
// Middle panel — tab switching + picture scroll + result list NM_CUSTOMDRAW
// ============================================================================

void showMiddleResultPage(RegularReportState* st) {
    if (!st || !st->middleTab) return;
    const int page = TabCtrl_GetCurSel(st->middleTab);
    const bool showRes = page == 0, showPic = page == 1;
    for (HWND c : st->middleResultControls)
        if (IsWindow(c)) ShowWindow(c, showRes ? SW_SHOW : SW_HIDE);
    for (HWND c : st->middlePictureControls)
        if (IsWindow(c)) ShowWindow(c, showPic ? SW_SHOW : SW_HIDE);
    if (IsWindow(st->resultList)) ShowWindow(st->resultList, showRes ? SW_SHOW : SW_HIDE);
    if (IsWindow(st->status)) ShowWindow(st->status, showRes ? SW_SHOW : SW_HIDE);
    InvalidateRect(st->middlePanel, nullptr, TRUE);
    if (showPic) {
        regularUpdatePictureViewport(st);
        regularQuerySelectedPicture(st, st->selectedReportIndex);
    }
}

LRESULT CALLBACK middlePanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                 UINT_PTR sid, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (st && nm->idFrom == REGULAR_IDC_MIDDLE_TAB && nm->code == TCN_SELCHANGE) {
                finishResultEdit(st, false); showMiddleResultPage(st); return 0;
            }
            if (nm->idFrom == REGULAR_IDC_RESULT_LIST && nm->code == NM_CUSTOMDRAW) {
                auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    if (regularCustomDrawListSelection(cd, st ? st->resultList : nullptr,
                        static_cast<int>(cd->nmcd.dwItemSpec)))
                        return CDRF_NOTIFYSUBITEMDRAW;
                    return CDRF_NOTIFYSUBITEMDRAW;
                }
                if (cd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                    const int row = static_cast<int>(cd->nmcd.dwItemSpec);
                    if (regularCustomDrawListSelection(cd, st ? st->resultList : nullptr, row))
                        return CDRF_NEWFONT;
                    const auto idx = static_cast<size_t>(cd->nmcd.dwItemSpec);
                    if (st && idx < st->resultRows.size()) {
                        const auto& rd = st->resultRows[idx];
                        cd->clrTextBk = cd->iSubItem == REGULAR_RESULT_VALUE_COL
                            ? REGULAR_COLOR_WHITE
                            : (regularResultRowHasCriticalValue(rd)
                                ? REGULAR_COLOR_CRITICAL_FINAL
                                : REGULAR_COLOR_RESULT_SIDE_BG);
                        const COLORREF c = regularResultTextColor(rd);
                        if (c != CLR_INVALID) cd->clrText = c;
                    } else {
                        cd->clrTextBk = cd->iSubItem == REGULAR_RESULT_VALUE_COL
                            ? REGULAR_COLOR_WHITE : REGULAR_COLOR_RESULT_SIDE_BG;
                    }
                    return CDRF_NEWFONT;
                }
            }
            if (st && nm->idFrom == REGULAR_IDC_RESULT_LIST && nm->code == NM_KILLFOCUS) {
                regularRedrawSelectedListRow(st->resultList); return 0;
            }
            break;
        }
        case WM_HSCROLL:
            if (st && reinterpret_cast<HWND>(lp) == st->pictureHScroll) {
                SCROLLINFO si{}; si.cbSize = sizeof(si); si.fMask = SIF_ALL;
                GetScrollInfo(st->pictureHScroll, SB_CTL, &si);
                regularScrollPictureViewport(st,
                    regularScrollTargetFromCode(hwnd, LOWORD(wp), si, st->pictureScrollX),
                    st->pictureScrollY);
                return 0;
            }
            break;
        case WM_VSCROLL:
            if (st && reinterpret_cast<HWND>(lp) == st->pictureVScroll) {
                SCROLLINFO si{}; si.cbSize = sizeof(si); si.fMask = SIF_ALL;
                GetScrollInfo(st->pictureVScroll, SB_CTL, &si);
                regularScrollPictureViewport(st, st->pictureScrollX,
                    regularScrollTargetFromCode(hwnd, LOWORD(wp), si, st->pictureScrollY));
                return 0;
            }
            break;
        case WM_NCDESTROY: RemoveWindowSubclass(hwnd, middlePanelProc, sid); break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// ============================================================================
// Bottom panel proc
// ============================================================================

LRESULT CALLBACK bottomPanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                 UINT_PTR sid, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_COMMAND:
            if (st && LOWORD(wp) == REGULAR_IDC_BOTTOM_MACHINE_1) {
                regularApplyQuickMachine(st, 0); return 0;
            }
            if (st && LOWORD(wp) == REGULAR_IDC_BOTTOM_MACHINE_2) {
                regularApplyQuickMachine(st, 1); return 0;
            }
            if (st && LOWORD(wp) == REGULAR_IDC_BOTTOM_MACHINE_3) {
                regularApplyQuickMachine(st, 2); return 0;
            }
            if (st && LOWORD(wp) == REGULAR_IDC_BOTTOM_REFRESH) {
                if (search::trim(st->selectedMachineCode).empty())
                    MessageBoxW(st->hwnd, L"请先选择检验仪器。", L"常规报告", MB_ICONWARNING);
                else
                    runReportQuery(st, true);
                return 0;
            }
            if (st && LOWORD(wp) == REGULAR_IDC_BOTTOM_PREV_REPORT) {
                selectAdjacentReportRow(st, -1); return 0;
            }
            if (st && LOWORD(wp) == REGULAR_IDC_BOTTOM_NEXT_REPORT) {
                selectAdjacentReportRow(st, 1); return 0;
            }
            if (st && LOWORD(wp) == REGULAR_IDC_BOTTOM_GRAPH) {
                regularOpenPicturePopupForSelection(st); return 0;
            }
            if (st && LOWORD(wp) == REGULAR_IDC_BOTTOM_TREND) {
                const int index = currentReportIndex(st);
                if (index < 0) {
                    MessageBoxW(st->hwnd, L"请先选择一条报告记录。", L"趋势图提示", MB_ICONINFORMATION);
                } else {
                    st->contextReportIndex = index;
                    regularShowTrendForContext(st);
                }
                return 0;
            }
            break;
        case WM_NCDESTROY: RemoveWindowSubclass(hwnd, bottomPanelProc, sid); break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// ============================================================================
// Tab control helpers
// ============================================================================

bool focusLeftTabControl(RegularReportState* st, HWND cur, bool rev) {
    if (!st || st->leftTabControls.empty()) return false;
    auto it = std::find(st->leftTabControls.begin(), st->leftTabControls.end(), cur);
    if (it == st->leftTabControls.end()) return false;
    const int cnt = static_cast<int>(st->leftTabControls.size());
    int idx = static_cast<int>(std::distance(st->leftTabControls.begin(), it));
    for (int s = 0; s < cnt; ++s) {
        idx = rev ? (idx - 1 + cnt) % cnt : (idx + 1) % cnt;
        HWND t = st->leftTabControls[static_cast<size_t>(idx)];
        if (!IsWindow(t) || !IsWindowVisible(t) || !IsWindowEnabled(t)) continue;
        SetFocus(t); SendMessageW(t, EM_SETSEL, 0, -1); return true;
    }
    return false;
}

LRESULT CALLBACK leftTabControlProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                    UINT_PTR sid, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_GETDLGCODE:
            return DefSubclassProc(hwnd, msg, wp, lp) | DLGC_WANTTAB;
        case WM_KEYDOWN:
            if (wp == VK_TAB &&
                focusLeftTabControl(st, hwnd, (GetKeyState(VK_SHIFT) & 0x8000) != 0))
                return 0;
            break;
        case WM_NCDESTROY: RemoveWindowSubclass(hwnd, leftTabControlProc, sid); break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void registerLeftTabControls(RegularReportState* st, std::initializer_list<HWND> ctrls) {
    if (!st) return;
    for (HWND h : ctrls) {
        if (!h) continue;
        st->leftTabControls.push_back(h);
        SetWindowSubclass(h, leftTabControlProc, REGULAR_LEFT_TAB_SUBCLASS,
                          reinterpret_cast<DWORD_PTR>(st));
    }
}

// ============================================================================
// Sample input proc
// ============================================================================

LRESULT CALLBACK sampleInputProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                 UINT_PTR sid, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_GETDLGCODE:
            return DefSubclassProc(hwnd, msg, wp, lp) | DLGC_WANTALLKEYS;
        case WM_KEYDOWN:
            if (wp == VK_RETURN) { regularSelectReportRowBySampleInput(st); return 0; }
            break;
        case WM_CHAR:
            if (wp == VK_RETURN) return 0;
            break;
        case WM_NCDESTROY: RemoveWindowSubclass(hwnd, sampleInputProc, sid); break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// ============================================================================
// List helpers
// ============================================================================

void addColumns(HWND list, const ColumnDef* cols, int cnt, HWND scaleHost) {
    for (int i = 0; i < cnt; ++i)
        search::add_list_column(list, cols[i].index, cols[i].title,
                                 S(scaleHost, cols[i].width));
}

void insertTabs(HWND tab, const wchar_t* const* labels, int cnt) {
    TCITEMW item{}; item.mask = TCIF_TEXT;
    for (int i = 0; i < cnt; ++i) {
        item.pszText = const_cast<wchar_t*>(labels[i]);
        TabCtrl_InsertItem(tab, i, &item);
    }
    TabCtrl_SetCurSel(tab, 0);
}

void setCell(HWND list, int row, int col, const wchar_t* text) {
    ListView_SetItemText(list, row, col, const_cast<wchar_t*>(text));
}

void setCell(HWND list, int row, int col, const std::string& text) {
    setCell(list, row, col, search::utf8_to_wide(text).c_str());
}

void setCellIfChanged(HWND list, int row, int col, const std::wstring& text) {
    wchar_t cur[2048]{};
    ListView_GetItemText(list, row, col, cur, static_cast<int>(std::size(cur)));
    if (text != cur) setCell(list, row, col, text.c_str());
}

// ============================================================================
// Column seeding
// ============================================================================

void seedLists(RegularReportState* st) {
    const ColumnDef resCols[] = {
        {0, L"", 32}, {1, L"", 44}, {2, L"组合项目", 82}, {3, L"英文", 72},
        {4, L"项目名称", 132}, {5, L"结果", 96}, {6, L"偏", 36},
        {7, L"参考区间", 112}, {8, L"单位", 76}, {9, L"说明", 96},
    };
    addColumns(st->resultList, resCols,
               static_cast<int>(sizeof(resCols) / sizeof(resCols[0])), st->resultList);

    const ColumnDef rptCols[] = {
        {0, L"标签", 26}, {1, L"样本号", 58}, {2, L"姓名", 70}, {3, L"性别", 36},
        {4, L"年龄", 66}, {5, L"医嘱内容", 110}, {6, L"科室代码", 78},
        {7, L"床号", 46}, {8, L"打印", 64}, {9, L"病人类型", 84},
        {10, L"检验者", 92}, {11, L"项目名称", 120}, {12, L"验单号", 96},
        {13, L"审核", 58}, {14, L"确认", 58}, {15, L"条形码", 96},
        {16, L"检验仪器", 92}, {17, L"标本", 58}, {18, L"备注", 120},
        {19, L"审核", 82}, {20, L"开单日期", 128}, {21, L"签收时间", 128},
        {22, L"检验日期", 128}, {23, L"报告时间", 128}, {24, L"费用", 72},
        {25, L"医生代号", 82}, {26, L"临床诊断", 150}, {27, L"病人号", 96},
        {28, L"上机时间", 128}, {29, L"电话", 110},
    };
    addColumns(st->reportList, rptCols,
               static_cast<int>(sizeof(rptCols) / sizeof(rptCols[0])), st->reportList);
}

// ============================================================================
// Panel creation
// ============================================================================

void createLeftPanel(HWND parent, RegularReportState* st) {
    HWND p = st->leftContent;
    auto add = [&](HWND h) { st->leftControls.push_back(h); return h; };
    auto x = [&](int v) { return S(parent, v); };
    auto y = [&](int v) { return S(parent, v); };
    RECT prc{};
    if (st->leftPanel) GetClientRect(st->leftPanel, &prc);
    const int scrollW = GetSystemMetrics(SM_CXVSCROLL);
    const float sc = std::max(0.1f, search::dpi_scale_factor(parent));
    const int panelPxW = prc.right > prc.left
        ? static_cast<int>(prc.right - prc.left) : S(parent, REGULAR_LEFT_PANEL_MIN_W);
    const int panelLogicalW = static_cast<int>(std::max(0, panelPxW - scrollW) / sc);
    const int gx = 8, innerPad = 6;
    const int lw = regularLeftLabelWidth(parent, st->ctx.uiFont);
    const int gw = std::max(240, panelLogicalW - gx - 8);
    const int gr = gx + gw - innerPad;
    const int ix = std::max(gx + innerPad + lw + 4, gx + innerPad + 54);
    const int lx = std::max(gx + innerPad, ix - lw - 4);
    const int fiw = std::max(120, gr - ix);
    const int rew = 72, rex = gr - rew;
    const int nrew = 48, nrex = gr - nrew;
    const int rlw = regularRightLabelWidth(parent, st->ctx.uiFont);
    const int rlx = rex - rlw - 4, nrlx = nrex - rlw - 4;
    const int lsw = std::max(42, rlx - ix - 8);
    const int nlsw = std::max(42, nrlx - ix - 8);
    const int fh = regularFontLogicalHeight(parent, st->ctx.uiFont);
    const int lh = std::max(24, fh + 4), eh = std::max(22, fh + 8);
    const int bh = std::max(26, fh + 10), cih = std::max(18, fh + 6);
    const int rs = std::max(34, eh + 10);
    const int gtp = std::max(14, fh), gbp = 4;
    const int gg = std::max(6, fh / 3 + 2);

    auto fitW = [&](int px, int req, int minW = 20) {
        return std::max(minW, std::min(req, gr - px));
    };
    auto grp = [&](const wchar_t* t, int px, int py, int w, int h) {
        st->leftGroupFrames.push_back({RECT{x(px), y(py), x(px + w), y(py + h)}, t});
    };
    auto lbl = [&](const wchar_t* t, int px, int py, int w, int h = 24,
                   DWORD s = SS_RIGHT | SS_CENTERIMAGE | SS_ENDELLIPSIS) {
        HWND hw = makeStatic(p, t, x(px), y(py), x(fitW(px, w, 16)), x(std::max(h, lh)), s | SS_CENTERIMAGE);
        SetPropW(hw, L"RegularLeftLabel", reinterpret_cast<HANDLE>(1)); add(hw); return hw;
    };
    auto edt = [&](const wchar_t* t, int px, int py, int w, int h = 24, DWORD e = ES_AUTOHSCROLL) {
        bool ml = (e & ES_MULTILINE) != 0;
        HWND hw = makeEdit(p, t, x(px), y(py), x(fitW(px, w, 24)), x(ml ? std::max(h, eh) : eh), e | ES_CENTER);
        add(hw); return hw;
    };
    auto btn = [&](const wchar_t* t, int px, int py, int w, int h, int id = 0) {
        HWND hw = makeButton(p, id, t, x(px), y(py), x(fitW(px, w, 22)), x(std::max(h, bh)));
        add(hw); return hw;
    };
    auto cbo = [&](const wchar_t* t, int px, int py, int w, int h) {
        HWND hw = makeCombo(p, t, x(px), y(py), x(fitW(px, w, 42)), x(std::max(h, cih * 5)));
        SendMessageW(hw, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), x(cih));
        SendMessageW(hw, CB_SETITEMHEIGHT, 0, x(cih));
        add(hw); return hw;
    };
    auto cbx = [&](int px, int py, int w, int h) {
        HWND hw = CreateWindowExW(0, L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_CHECKBOX | BS_RIGHTBUTTON,
            x(px), y(py), x(fitW(px, w, 16)), x(std::max(h, eh)),
            p, nullptr, GetModuleHandleW(nullptr), nullptr);
        add(hw); return hw;
    };
    auto dtp = [&](int px, int py, int w, int h, const wchar_t* fmt, const SYSTEMTIME* v, int id = 0) {
        HWND hw = makeDatePicker(p, x(px), y(py), x(fitW(px, w, 80)), x(std::max(h, eh)), fmt, v, id);
        add(hw); return hw;
    };

    const int fgtp = std::max(10, fh * 2 / 3 + 2);
    int gy = fgtp;
    auto rowY = [&](int i) { return gy + gtp + i * rs; };
    auto gh = [&](int rows) { return gtp + std::max(0, rows - 1) * rs + eh + gbp; };

    // 标本信息
    const int sRows = 7, sH = gh(sRows);
    grp(L"标本信息", gx, gy, gw, sH);
    st->selectedMachineCode.clear(); st->selectedRoomCode.clear();
    lbl(L"检验仪器", lx, rowY(0), lw);
    const int pbw = 34, pbx = gr - pbw;
    st->machineEdit = edt(L"", ix, rowY(0) - 2, std::max(80, pbx - ix - 6), eh, ES_CENTER | ES_READONLY);
    st->machinePickerButton = btn(L"...", pbx, rowY(0) - 2, pbw, bh, REGULAR_IDC_MACHINE_PICKER_BUTTON);
    lbl(L"组合项目", lx, rowY(1), lw); st->groupEdit = edt(L"", ix, rowY(1) - 2, fiw, eh, ES_CENTER | ES_READONLY);
    lbl(L"标本", lx, rowY(2), lw); st->sampleEdit = edt(L"", ix, rowY(2) - 2, fiw, eh, ES_CENTER | ES_READONLY);
    lbl(L"检验单号", lx, rowY(3), lw); st->reportNoEdit = edt(L"", ix, rowY(3) - 2, nlsw, eh, ES_CENTER | ES_READONLY);
    lbl(L"样本号", nrlx, rowY(3), rlw); st->operNoEdit = edt(L"", nrex, rowY(3) - 2, nrew, eh, ES_CENTER);
    SetWindowSubclass(st->operNoEdit, sampleInputProc, REGULAR_SAMPLE_INPUT_SUBCLASS, reinterpret_cast<DWORD_PTR>(st));
    lbl(L"病人类型", lx, rowY(4), lw);
    const int ucw = 20, ucg = -12, ucx = nrlx - ucw - ucg;
    const int ptw = std::max(42, ucx - ix - 6);
    st->patientTypeCombo = cbo(L"", ix, rowY(4) - 2, ptw, cih * 5);
    st->urgentCheck = cbx(ucx, rowY(4) - 2, ucw, eh + 2);
    st->urgentLabel = lbl(L"急诊", nrlx, rowY(4), rlw);
    SetPropW(st->urgentLabel, L"RegularEmergencyLabel", reinterpret_cast<HANDLE>(1));
    st->urgentEdit = edt(L"", nrex, rowY(4) - 2, nrew, eh);
    lbl(L"条形码", lx, rowY(5), lw); st->barcodeEdit = edt(L"", ix, rowY(5) - 2, fiw, eh, ES_CENTER);
    lbl(L"病人号", lx, rowY(6), lw); st->regNoEdit = edt(L"", ix, rowY(6) - 2, fiw, eh);

    // 病人信息
    gy += sH + gg;
    const int pRows = 6, pH = gh(pRows);
    grp(L"病人信息", gx, gy, gw, pH);
    lbl(L"姓名", lx, rowY(0), lw); st->patientNameEdit = edt(L"", ix, rowY(0) - 2, lsw + 6, eh);
    lbl(L"性别", nrlx, rowY(0), rlw); st->sexEdit = edt(L"", nrex, rowY(0) - 2, nrew, eh);
    lbl(L"年龄", lx, rowY(1), lw);
    const int auw = 52, aux = std::max(ix + 54, nrlx - auw - 4);
    st->ageEdit = edt(L"", ix, rowY(1) - 2, std::max(48, aux - ix - 4), eh);
    st->ageUnitCombo = cbo(L"岁", aux, rowY(1) - 2, auw, cih * 5);
    regularFillAgeUnitCombo(st->ageUnitCombo);
    lbl(L"床号", nrlx, rowY(1), rlw); st->bedEdit = edt(L"", nrex, rowY(1) - 2, nrew, eh);
    lbl(L"电话", lx, rowY(2), lw); st->phoneEdit = edt(L"", ix, rowY(2) - 2, fiw, eh, ES_CENTER);
    lbl(L"临床科室", lx, rowY(3), lw); st->deptEdit = edt(L"", ix, rowY(3) - 2, fiw, eh);
    lbl(L"临床诊断", lx, rowY(4), lw); st->diagEdit = edt(L"", ix, rowY(4) - 2, fiw, eh, ES_CENTER);
    lbl(L"申请医生", lx, rowY(5), lw); st->reqDoctorEdit = edt(L"", ix, rowY(5) - 2, lsw + 6, eh, ES_CENTER);
    lbl(L"费用", rlx, rowY(5), rlw); st->feeEdit = edt(L"", rex, rowY(5) - 2, rew, eh, ES_CENTER);

    // 验单信息
    gy += pH + gg;
    const int oRows = 8, oH = gh(oRows);
    grp(L"验单信息", gx, gy, gw, oH);
    lbl(L"检验者", lx, rowY(0), lw); st->testerEdit = edt(L"", ix, rowY(0) - 2, lsw + 6, eh, ES_CENTER | ES_READONLY);
    lbl(L"审核", rlx, rowY(0), rlw); st->auditEdit = edt(L"", rex, rowY(0) - 2, rew, eh, ES_CENTER | ES_READONLY);
    lbl(L"备注", lx, rowY(1), lw); st->noteCodeEdit = edt(L"", ix, rowY(1) - 2, 42, eh);
    st->noteEdit = edt(L"", ix + 48, rowY(1) - 2, std::max(80, gr - ix - 48), eh);
    lbl(L"申请日期", lx, rowY(2), lw); st->applyDatePicker = dtp(ix, rowY(2) - 2, fiw, eh, L"yyyy-MM-dd HH:mm", nullptr);
    lbl(L"签收时间", lx, rowY(3), lw); st->receiveDatePicker = dtp(ix, rowY(3) - 2, fiw, eh, L"yyyy-MM-dd HH:mm:ss", nullptr);
    lbl(L"上机时间", lx, rowY(4), lw); st->machineDatePicker = dtp(ix, rowY(4) - 2, fiw, eh, L"yyyy-MM-dd HH:mm", nullptr);
    lbl(L"报告时间", lx, rowY(5), lw); st->reportDatePicker = dtp(ix, rowY(5) - 2, fiw, eh, L"yyyy-MM-dd HH:mm", nullptr);
    lbl(L"检验日期", lx, rowY(6), lw);
    SYSTEMTIME idate = regularTodayDate();
    st->inspectDatePicker = dtp(ix, rowY(6) - 2, fiw, eh, L"yyyy-MM-dd", &idate, REGULAR_IDC_INSPECT_DATE);
    lbl(L"采集日期", lx, rowY(7), lw); st->collectDateEdit = edt(L"", ix, rowY(7) - 2, fiw, eh, ES_CENTER | ES_READONLY);
    regularSetControlsEnabled(false, {st->applyDatePicker, st->receiveDatePicker,
        st->machineDatePicker, st->reportDatePicker, st->collectDateEdit});

    registerLeftTabControls(st, {
        st->machinePickerButton, st->urgentCheck, st->urgentEdit,
        st->operNoEdit, st->barcodeEdit, st->regNoEdit, st->patientNameEdit,
        st->sexEdit, st->ageEdit, st->ageUnitCombo, st->bedEdit,
        st->phoneEdit, st->deptEdit, st->diagEdit, st->reqDoctorEdit,
        st->feeEdit, st->noteCodeEdit, st->noteEdit,
        st->applyDatePicker, st->receiveDatePicker, st->machineDatePicker,
        st->reportDatePicker, st->inspectDatePicker,
    });
}

void createMiddlePanel(HWND parent, RegularReportState* st) {
    HWND p = st->middlePanel;
    auto aR = [&](HWND h) { st->middleResultControls.push_back(h); return h; };
    auto aP = [&](HWND h) { st->middlePictureControls.push_back(h); return h; };

    st->middleTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        S(parent, REGULAR_PAD), S(parent, 0), S(parent, 520), S(parent, REGULAR_TAB_H),
        p, win32_control_id(REGULAR_IDC_MIDDLE_TAB), GetModuleHandleW(nullptr), nullptr);
    const wchar_t* mTabs[] = {L"项目结果", L"结果图"};
    insertTabs(st->middleTab, mTabs, static_cast<int>(sizeof(mTabs) / sizeof(mTabs[0])));

    st->resultList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        S(parent, REGULAR_PAD), S(parent, REGULAR_MIDDLE_LIST_Y),
        S(parent, 520), S(parent, 350),
        p, win32_control_id(REGULAR_IDC_RESULT_LIST), GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(st->resultList,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    SetWindowSubclass(st->resultList, resultListProc, REGULAR_RESULT_LIST_SUBCLASS,
                      reinterpret_cast<DWORD_PTR>(st));
    aR(st->resultList);
    st->status = makeStatic(p, L"结果列表右键功能：项目复制；参数设置。[项目总数：7]",
        S(parent, REGULAR_PAD), S(parent, 424), S(parent, 520),
        S(parent, REGULAR_MIDDLE_STATUS_H));
    aR(st->status);

    st->pictureViewport = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
        WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        S(parent, REGULAR_PAD), S(parent, REGULAR_MIDDLE_LIST_Y),
        S(parent, 520), S(parent, 350),
        p, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(st->pictureViewport, pictureViewportProc, REGULAR_PICTURE_VIEWPORT_SUBCLASS,
                      reinterpret_cast<DWORD_PTR>(st));
    st->pictureView = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, S(parent, REGULAR_PICTURE_FIXED_W), S(parent, REGULAR_PICTURE_FIXED_H),
        st->pictureViewport, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(st->pictureView, pictureViewProc, REGULAR_PICTURE_VIEW_SUBCLASS,
                      reinterpret_cast<DWORD_PTR>(st));
    st->pictureHScroll = CreateWindowExW(0, L"SCROLLBAR", L"",
        WS_CHILD | SBS_HORZ,
        S(parent, REGULAR_PAD), S(parent, REGULAR_MIDDLE_LIST_Y + 350),
        S(parent, 520), S(parent, GetSystemMetrics(SM_CYHSCROLL)),
        p, nullptr, GetModuleHandleW(nullptr), nullptr);
    st->pictureVScroll = CreateWindowExW(0, L"SCROLLBAR", L"",
        WS_CHILD | SBS_VERT,
        S(parent, REGULAR_PAD + 520), S(parent, REGULAR_MIDDLE_LIST_Y),
        S(parent, GetSystemMetrics(SM_CXVSCROLL)), S(parent, 350),
        p, nullptr, GetModuleHandleW(nullptr), nullptr);
    aP(st->pictureViewport); aP(st->pictureHScroll); aP(st->pictureVScroll);
    regularUpdatePictureViewport(st);
    showMiddleResultPage(st);
}

void createRightPanel(HWND parent, RegularReportState* st) {
    HWND p = st->rightPanel;
    auto aI = [&](HWND h) { st->rightInfoControls.push_back(h); return h; };
    st->rightTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        S(parent, REGULAR_PAD), S(parent, REGULAR_RIGHT_TAB_Y),
        S(parent, 576), S(parent, REGULAR_TAB_H),
        p, win32_control_id(REGULAR_IDC_RIGHT_TAB), GetModuleHandleW(nullptr), nullptr);
    const wchar_t* tabs[] = {L"信息列表", L"结果比较"};
    insertTabs(st->rightTab, tabs, static_cast<int>(sizeof(tabs) / sizeof(tabs[0])));

    st->rightSearchLabel = aI(makeStatic(p, L"按姓名查",
        S(parent, REGULAR_PAD), S(parent, REGULAR_RIGHT_SEARCH_LABEL_Y),
        S(parent, 70), S(parent, 24)));
    st->rightSearchEdit = aI(makeEdit(p, L"",
        S(parent, 82), S(parent, REGULAR_RIGHT_SEARCH_CONTROL_Y),
        S(parent, 84), S(parent, 26)));
    st->rightSearchIndexButton = aI(makeButton(p, 0, L"1",
        S(parent, 174), S(parent, REGULAR_RIGHT_SEARCH_CONTROL_Y),
        S(parent, 38), S(parent, REGULAR_COMPACT_BUTTON_H)));
    st->rightSearchUpButton = aI(makeButton(p, REGULAR_IDC_REPORT_FIRST_BUTTON, L"⇧",
        S(parent, 220), S(parent, REGULAR_RIGHT_SEARCH_CONTROL_Y),
        S(parent, 38), S(parent, REGULAR_COMPACT_BUTTON_H)));
    st->rightSearchDownButton = aI(makeButton(p, REGULAR_IDC_REPORT_LAST_BUTTON, L"⇩",
        S(parent, 266), S(parent, REGULAR_RIGHT_SEARCH_CONTROL_Y),
        S(parent, 38), S(parent, REGULAR_COMPACT_BUTTON_H)));
    st->rightSearchMenuButton = aI(makeButton(p, 0, L"▼",
        S(parent, 548), S(parent, REGULAR_RIGHT_SEARCH_CONTROL_Y),
        S(parent, 36), S(parent, REGULAR_COMPACT_BUTTON_H)));

    st->reportList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        S(parent, REGULAR_PAD), S(parent, REGULAR_RIGHT_LIST_Y),
        S(parent, 576), S(parent, 588),
        p, win32_control_id(REGULAR_IDC_REPORT_LIST), GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(st->reportList,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_CHECKBOXES);

    st->rightDateTodayButton = aI(makeButton(p, REGULAR_IDC_REPORT_DATE_TODAY_BUTTON, L"今天",
        S(parent, REGULAR_PAD), S(parent, REGULAR_RIGHT_LIST_Y + 596),
        S(parent, REGULAR_RIGHT_DATE_BUTTON_W), S(parent, REGULAR_RIGHT_DATE_BUTTON_H)));
    st->rightDatePrevButton = aI(makeButton(p, REGULAR_IDC_REPORT_DATE_PREV_BUTTON, L"前一天",
        S(parent, REGULAR_PAD + REGULAR_RIGHT_DATE_BUTTON_W + 8),
        S(parent, REGULAR_RIGHT_LIST_Y + 596),
        S(parent, REGULAR_RIGHT_DATE_BUTTON_W), S(parent, REGULAR_RIGHT_DATE_BUTTON_H)));
    st->rightDateNextButton = aI(makeButton(p, REGULAR_IDC_REPORT_DATE_NEXT_BUTTON, L"后一天",
        S(parent, REGULAR_PAD + (REGULAR_RIGHT_DATE_BUTTON_W + 8) * 2),
        S(parent, REGULAR_RIGHT_LIST_Y + 596),
        S(parent, REGULAR_RIGHT_DATE_BUTTON_W), S(parent, REGULAR_RIGHT_DATE_BUTTON_H)));

    st->rightAutoRefreshCheck = aI(CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | BS_AUTOCHECKBOX,
        S(parent, REGULAR_PAD + (REGULAR_RIGHT_DATE_BUTTON_W + 8) * 3 + 6),
        S(parent, REGULAR_RIGHT_LIST_Y + 600), S(parent, 18), S(parent, 20),
        p, win32_control_id(REGULAR_IDC_REPORT_AUTO_REFRESH_CHECK),
        GetModuleHandleW(nullptr), nullptr));
    st->rightAutoRefreshLabel = aI(makeStatic(p, L"自动刷新",
        S(parent, REGULAR_PAD + (REGULAR_RIGHT_DATE_BUTTON_W + 8) * 3 + 26),
        S(parent, REGULAR_RIGHT_LIST_Y + 598), S(parent, 66), S(parent, 24),
        SS_LEFT | SS_CENTERIMAGE));
    st->rightAutoRefreshEdit = aI(makeEdit(p, L"10",
        S(parent, REGULAR_PAD + (REGULAR_RIGHT_DATE_BUTTON_W + 8) * 3 + 88),
        S(parent, REGULAR_RIGHT_LIST_Y + 598), S(parent, 38), S(parent, 24),
        ES_CENTER | ES_NUMBER));
    st->rightAutoRefreshUnitLabel = aI(makeStatic(p, L"秒",
        S(parent, REGULAR_PAD + (REGULAR_RIGHT_DATE_BUTTON_W + 8) * 3 + 130),
        S(parent, REGULAR_RIGHT_LIST_Y + 598), S(parent, 24), S(parent, 24),
        SS_LEFT | SS_CENTERIMAGE));
    showRightInfoPage(st);
}

void createBottomPanel(HWND parent, RegularReportState* st) {
    HWND p = st->bottomPanel;
    const ButtonDef row1[] = {
        {REGULAR_IDC_BOTTOM_MACHINE_1, L"1"}, {REGULAR_IDC_BOTTOM_REFRESH, L"⟳ 刷新(F5)"},
        {5403, L"▣ 保存(F1)"}, {5404, L"✓ 审核(F3)"}, {5405, L"预览(V)"}, {5406, L"打印(F4)"},
        {5407, L"✕ 删除(D)"}, {REGULAR_IDC_BOTTOM_PREV_REPORT, L"⇧ 上一个"},
        {REGULAR_IDC_BOTTOM_NEXT_REPORT, L"⇩ 下一个"}, {5410, L"审核打印"},
    };
    const ButtonDef row2[] = {
        {REGULAR_IDC_BOTTOM_MACHINE_2, L"2"}, {5412, L"批审核"}, {5413, L"批取消"},
        {5414, L"批录入"}, {5415, L"批调整"}, {5416, L"批打印"}, {5417, L"批删除"},
        {5418, L"医嘱"}, {5419, L"汇总(F6)"},
    };
    const ButtonDef row3[] = {
        {REGULAR_IDC_BOTTOM_MACHINE_3, L"3"}, {5421, L"追踪(Z)"}, {5422, L"计算(F8)"},
        {5423, L"合并(U)"}, {REGULAR_IDC_BOTTOM_GRAPH, L"图形(T)"}, {REGULAR_IDC_BOTTOM_TREND, L"趋势图"},
        {5426, L"日统计"}, {5427, L"设置"}, {5428, L"审核规则"}, {5429, L"批修改"},
    };
    auto cr = [&](const ButtonDef* row, int cnt, int y) {
        int x = S(parent, REGULAR_PAD);
        for (int i = 0; i < cnt; ++i) {
            int w = (i == 0) ? S(parent, 42) : S(parent, 98);
            makeButton(p, row[i].id, row[i].text, x, S(parent, y), w,
                       S(parent, REGULAR_COMPACT_BUTTON_H));
            x += w + S(parent, REGULAR_PAD);
        }
    };
    cr(row1, static_cast<int>(sizeof(row1) / sizeof(row1[0])), 4);
    cr(row2, static_cast<int>(sizeof(row2) / sizeof(row2[0])), 36);
    cr(row3, static_cast<int>(sizeof(row3) / sizeof(row3[0])), 68);
    regularUpdateQuickMachineButtonLabels(st);
}

// ============================================================================
// createControls + layout
// ============================================================================

void createControls(HWND hwnd, RegularReportState* st) {
    st->leftPanel = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(st->leftPanel, leftPanelProc, REGULAR_LEFT_PANEL_SUBCLASS,
                      reinterpret_cast<DWORD_PTR>(st));
    st->leftContent = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 0, 0, st->leftPanel, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(st->leftContent, leftContentProc, REGULAR_LEFT_CONTENT_SUBCLASS,
                      reinterpret_cast<DWORD_PTR>(st));
    st->leftScrollBar = CreateWindowExW(0, L"SCROLLBAR", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SBS_VERT,
        0, 0, 0, 0, st->leftPanel, win32_control_id(REGULAR_IDC_LEFT_SCROLL),
        GetModuleHandleW(nullptr), nullptr);
    st->middlePanel = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(st->middlePanel, middlePanelProc, REGULAR_MIDDLE_PANEL_SUBCLASS,
                      reinterpret_cast<DWORD_PTR>(st));
    st->rightPanel = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(st->rightPanel, rightPanelProc, REGULAR_RIGHT_PANEL_SUBCLASS,
                      reinterpret_cast<DWORD_PTR>(st));
    st->bottomPanel = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(st->bottomPanel, bottomPanelProc, REGULAR_BOTTOM_PANEL_SUBCLASS,
                      reinterpret_cast<DWORD_PTR>(st));
    st->splitter = search::create_splitter(hwnd, REGULAR_IDC_SPLITTER, 0, 0, 0, 0, st->ctx.instance);
    createLeftPanel(hwnd, st);
    createMiddlePanel(hwnd, st);
    createRightPanel(hwnd, st);
    createBottomPanel(hwnd, st);
    seedLists(st);
    regularApplyFont(hwnd, st->ctx.uiFont);
    regularRefreshLeftGroupTitleFont(st);
    updateLeftScrollBar(st);
}

void layout(HWND hwnd, RegularReportState* st) {
    if (!st) return;
    RECT rc{}; GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    const int bottomH = S(hwnd, REGULAR_BOTTOM_PANEL_H);
    const int gap = S(hwnd, REGULAR_GAP), splitterW = S(hwnd, REGULAR_SPLITTER_W);
    const int leftW = std::min(std::max(S(hwnd, REGULAR_LEFT_PANEL_MIN_W), w * 21 / 100),
                               S(hwnd, REGULAR_LEFT_PANEL_MAX_W));
    const int topH = std::max(S(hwnd, 420), h - bottomH - gap);
    const int centerX = leftW + gap;
    const int minCW = S(hwnd, 460), minRW = S(hwnd, 360);
    const int availW = std::max(S(hwnd, 760), w - centerX - gap);
    const int defCW = std::max(S(hwnd, 500), availW * 46 / 100);
    const int minSX = centerX + minCW;
    int maxSX = w - gap - minRW - splitterW - gap;
    if (maxSX < minSX) maxSX = minSX;
    if (!st->splitterUserSet) st->splitterX = centerX + defCW;
    st->splitterX = std::clamp(st->splitterX, minSX, maxSX);

    const int centerW = st->splitterX - centerX;
    const int rightX = st->splitterX + splitterW + gap;
    const int rightW = std::max(S(hwnd, 200), w - rightX - gap);

    MoveWindow(st->leftPanel, 0, 0, leftW, h, TRUE);
    scrollLeftPanelTo(st, st->leftScrollY);
    MoveWindow(st->middlePanel, centerX, 0, centerW, topH, TRUE);
    MoveWindow(st->splitter, st->splitterX, 0, splitterW, topH, TRUE);
    MoveWindow(st->rightPanel, rightX, 0, rightW, topH, TRUE);
    MoveWindow(st->bottomPanel, centerX, topH + gap, w - centerX - gap, bottomH, TRUE);

    MoveWindow(st->middleTab, S(hwnd, REGULAR_PAD), S(hwnd, 0),
               centerW - S(hwnd, REGULAR_PAD * 2), S(hwnd, REGULAR_TAB_H), TRUE);
    MoveWindow(st->resultList, S(hwnd, REGULAR_PAD), S(hwnd, REGULAR_MIDDLE_LIST_Y),
               centerW - S(hwnd, REGULAR_PAD * 2),
               topH - S(hwnd, REGULAR_MIDDLE_LIST_BOTTOM_MARGIN), TRUE);
    const int phX = S(hwnd, REGULAR_PAD), phY = S(hwnd, REGULAR_MIDDLE_LIST_Y);
    const int phW = centerW - S(hwnd, REGULAR_PAD * 2);
    const int phH = topH - S(hwnd, REGULAR_MIDDLE_LIST_BOTTOM_MARGIN);
    const int pscW = GetSystemMetrics(SM_CXVSCROLL), pscH = GetSystemMetrics(SM_CYHSCROLL);
    const int pcW = S(hwnd, REGULAR_PICTURE_FIXED_W), pcH = S(hwnd, REGULAR_PICTURE_FIXED_H);
    bool nv = pcH > phH, nh = pcW > (phW - (nv ? pscW : 0));
    nv = pcH > (phH - (nh ? pscH : 0));
    const int pvW = std::max(1, phW - (nv ? pscW : 0));
    const int pvH = std::max(1, phH - (nh ? pscH : 0));
    MoveWindow(st->pictureViewport, phX, phY, pvW, pvH, TRUE);
    MoveWindow(st->pictureHScroll, phX, phY + pvH, pvW, pscH, TRUE);
    MoveWindow(st->pictureVScroll, phX + pvW, phY, pscW, pvH, TRUE);
    ShowWindow(st->pictureHScroll, nh ? SW_SHOW : SW_HIDE);
    ShowWindow(st->pictureVScroll, nv ? SW_SHOW : SW_HIDE);
    regularUpdatePictureViewport(st);
    MoveWindow(st->status, S(hwnd, REGULAR_PAD), topH - S(hwnd, REGULAR_MIDDLE_STATUS_BOTTOM),
               centerW - S(hwnd, REGULAR_PAD * 2), S(hwnd, REGULAR_MIDDLE_STATUS_H), TRUE);

    const int riX = S(hwnd, REGULAR_PAD);
    const int riW = std::max(S(hwnd, 80), rightW - S(hwnd, REGULAR_PAD * 2));
    const auto rhdr = regularRightHeaderLayout(hwnd, st->ctx.uiFont, rightW,
        regularRightSummaryLine1(st), regularRightSummaryLine2(st));
    const int rtY = rhdr.bottom + S(hwnd, 6);
    const int rslY = rtY + S(hwnd, REGULAR_RIGHT_SEARCH_LABEL_Y - REGULAR_RIGHT_TAB_Y);
    const int rscY = rtY + S(hwnd, REGULAR_RIGHT_SEARCH_CONTROL_Y - REGULAR_RIGHT_TAB_Y);
    const int rlY = rtY + S(hwnd, REGULAR_RIGHT_LIST_Y - REGULAR_RIGHT_TAB_Y);
    MoveWindow(st->rightTab, riX, rtY, riW, S(hwnd, REGULAR_TAB_H), TRUE);
    MoveWindow(st->rightSearchLabel, S(hwnd, REGULAR_PAD), rslY, S(hwnd, 70), S(hwnd, 24), TRUE);
    MoveWindow(st->rightSearchEdit, S(hwnd, 82), rscY, S(hwnd, 84), S(hwnd, 26), TRUE);
    MoveWindow(st->rightSearchIndexButton, S(hwnd, 174), rscY, S(hwnd, 38),
               S(hwnd, REGULAR_COMPACT_BUTTON_H), TRUE);
    MoveWindow(st->rightSearchUpButton, S(hwnd, 220), rscY, S(hwnd, 38),
               S(hwnd, REGULAR_COMPACT_BUTTON_H), TRUE);
    MoveWindow(st->rightSearchDownButton, S(hwnd, 266), rscY, S(hwnd, 38),
               S(hwnd, REGULAR_COMPACT_BUTTON_H), TRUE);
    MoveWindow(st->rightSearchMenuButton, rightW - S(hwnd, REGULAR_PAD + 36), rscY,
               S(hwnd, 36), S(hwnd, REGULAR_COMPACT_BUTTON_H), TRUE);

    const int rdbY = topH - S(hwnd, REGULAR_PAD + REGULAR_RIGHT_DATE_BUTTON_H);
    const int rdbG = S(hwnd, 8), rdbW = S(hwnd, REGULAR_RIGHT_DATE_BUTTON_W);
    const int arX = riX + (rdbW + rdbG) * 3 + S(hwnd, 6);
    MoveWindow(st->reportList, riX, rlY, riW,
               std::max(S(hwnd, 80), rdbY - rlY - S(hwnd, REGULAR_GAP)), TRUE);
    MoveWindow(st->rightDateTodayButton, riX, rdbY, rdbW,
               S(hwnd, REGULAR_RIGHT_DATE_BUTTON_H), TRUE);
    MoveWindow(st->rightDatePrevButton, riX + rdbW + rdbG, rdbY, rdbW,
               S(hwnd, REGULAR_RIGHT_DATE_BUTTON_H), TRUE);
    MoveWindow(st->rightDateNextButton, riX + (rdbW + rdbG) * 2, rdbY, rdbW,
               S(hwnd, REGULAR_RIGHT_DATE_BUTTON_H), TRUE);
    MoveWindow(st->rightAutoRefreshCheck, arX, rdbY + S(hwnd, 4), S(hwnd, 18),
               S(hwnd, 20), TRUE);
    MoveWindow(st->rightAutoRefreshLabel, arX + S(hwnd, 20), rdbY + S(hwnd, 2),
               S(hwnd, 66), S(hwnd, 24), TRUE);
    MoveWindow(st->rightAutoRefreshEdit, arX + S(hwnd, 82), rdbY + S(hwnd, 2),
               S(hwnd, 38), S(hwnd, 24), TRUE);
    MoveWindow(st->rightAutoRefreshUnitLabel, arX + S(hwnd, 124), rdbY + S(hwnd, 2),
               S(hwnd, 24), S(hwnd, 24), TRUE);
    InvalidateRect(st->rightPanel, nullptr, TRUE);
}

// ============================================================================
// Query orchestration
// ============================================================================

search::QueryInput buildReportQueryInput(RegularReportState* st) {
    search::QueryInput input;
    if (!st) return input;
    input.start_date = regularDatePickerValue(st->inspectDatePicker);
    input.end_date = input.start_date;
    input.room_code = st->selectedRoomCode;
    input.mach_code = st->selectedMachineCode;
    input.limit = 0;
    return input;
}

void runReportQuery(RegularReportState* st, bool preserveState) {
    if (!st || search::trim(st->selectedMachineCode).empty()) return;
    if (search::build_connection_string_w(st->ctx.dbSettings).empty()) {
        MessageBoxW(st->hwnd, L"请先在\"设置\"中填写数据库连接信息。", L"缺少数据库设置", MB_ICONWARNING);
        return;
    }
    if (!preserveState) {
        ListView_DeleteAllItems(st->reportList);
        ListView_DeleteAllItems(st->resultList);
        st->reportRows.clear(); st->resultRows.clear();
        InvalidateRect(st->rightPanel, nullptr, TRUE);
        st->pictureQueryLoading = false; st->pictureRepNo.clear();
        ++st->pictureQueryGeneration;
        regularClearPictureView(st, L"");
        st->selectedReportIndex = -1; st->contextReportIndex = -1;
    }
    SetWindowTextW(st->status, preserveState ? L"正在刷新样本列表..." : L"正在查询样本列表...");
    st->reportQueryLoading = true;
    const int gen = ++st->reportQueryGeneration;
    const auto settings = st->ctx.dbSettings;
    const auto input = buildReportQueryInput(st);
    const std::string qd = input.start_date;
    const HWND hwnd = st->hwnd;

    std::thread([hwnd, settings, input, gen, preserveState, qd]() {
        auto* r = new ReportLoadResult;
        r->generation = gen; r->preserveState = preserveState; r->queryDate = qd;
        r->ok = search::run_report_query(settings, input, r->rows, r->connectionString, r->error);
        if (!PostMessageW(hwnd, WM_REGULAR_REPORTS_LOADED, 0, reinterpret_cast<LPARAM>(r)))
            delete r;
    }).detach();
}

void runAutoRefreshQuery(RegularReportState* st) {
    if (!st || st->reportQueryLoading) return;
    if (search::trim(st->selectedMachineCode).empty()) return;
    if (search::build_connection_string_w(st->ctx.dbSettings).empty()) return;
    runReportQuery(st, true);
}

bool inspectDateMatchesCurrentQuery(const RegularReportState* st) {
    if (!st) return false;
    const std::string d = regularDatePickerValue(st->inspectDatePicker);
    return !d.empty() && d == st->reportQueryDate;
}

void setInspectDateAndQuery(RegularReportState* st, SYSTEMTIME date, bool preserve) {
    if (!st || !st->inspectDatePicker) return;
    date = regularNormalizeDate(date);
    st->suppressInspectDateQuery = true;
    DateTime_SetSystemtime(st->inspectDatePicker, GDT_VALID, &date);
    st->suppressInspectDateQuery = false;
    if (!search::trim(st->selectedMachineCode).empty())
        runReportQuery(st, preserve && hasSelectedReportRow(st));
}

// ============================================================================
// Selection helpers
// ============================================================================

void selectReportRow(RegularReportState* st, int index) {
    if (!st || !st->reportList || index < 0 ||
        index >= static_cast<int>(st->reportRows.size())) return;
    ListView_SetItemState(st->reportList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(st->reportList, index, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(st->reportList, index, FALSE);
    SetFocus(st->reportList);
}

void selectAdjacentReportRow(RegularReportState* st, int delta) {
    if (!st || !st->reportList || st->reportRows.empty()) return;
    int cur = ListView_GetNextItem(st->reportList, -1, LVNI_SELECTED);
    if (cur < 0) cur = st->selectedReportIndex;
    if (cur < 0 || cur >= static_cast<int>(st->reportRows.size()))
        cur = delta > 0 ? -1 : static_cast<int>(st->reportRows.size());
    const int next = std::max(0, std::min(
        static_cast<int>(st->reportRows.size()) - 1, cur + delta));
    if (next != cur) selectReportRow(st, next);
}

int currentReportIndex(const RegularReportState* st) {
    if (!st) return -1;
    if (st->reportList) {
        const int sel = ListView_GetNextItem(st->reportList, -1, LVNI_SELECTED);
        if (sel >= 0 && sel < static_cast<int>(st->reportRows.size())) return sel;
    }
    if (st->selectedReportIndex >= 0 &&
        st->selectedReportIndex < static_cast<int>(st->reportRows.size()))
        return st->selectedReportIndex;
    return -1;
}

bool hasSelectedReportRow(const RegularReportState* st) {
    return st && st->reportList && ListView_GetNextItem(st->reportList, -1, LVNI_SELECTED) >= 0;
}

// ============================================================================
// Data display — report rows
// ============================================================================

std::array<std::wstring, REGULAR_REPORT_COLUMN_COUNT> reportDisplayValues(
    const search::ReportRow& d) {
    auto w = [](const std::string& s) { return search::utf8_to_wide(s); };
    std::array<std::wstring, REGULAR_REPORT_COLUMN_COUNT> v{};
    v[0] = regularReportHasBarcodeEmergencyLabel(d) ? L"急" : L"";
    v[1] = w(d.oper_no); v[2] = w(d.name); v[3] = w(d.sex); v[4] = w(d.age);
    v[5] = w(d.order_text); v[6] = w(d.dept_name); v[7] = w(d.bed_code);
    v[8] = w(search::display_binary_print_flag(d.zymz_print));
    v[9] = w(d.patient_type); v[10] = w(d.requester); v[11] = w(d.group_name);
    v[12] = w(d.rep_no); v[13] = w(d.chk_flag); v[14] = w(d.conf);
    v[15] = w(d.txm_no); v[16] = w(d.group_name); v[17] = w(d.sample_name);
    v[18] = w(d.note); v[19] = w(d.dean_oper);
    v[20] = w(regularSlashDateTimeMinute(d.chk_date));
    v[21] = w(regularSlashDateTimeMinute(d.collection_time));
    v[22] = w(regularSlashDate(d.inspect_date));
    v[23] = w(regularSlashDateTimeMinute(d.rep_time));
    v[24] = w(d.fee); v[25] = w(d.req_doctor); v[26] = w(d.diag_name);
    v[27] = w(d.reg_no); v[28] = w(d.create_time); v[29] = w(d.patient_phone);
    return v;
}

bool reportDisplayEqual(const search::ReportRow& l, const search::ReportRow& r) {
    return reportDisplayValues(l) == reportDisplayValues(r);
}

void insertReportRow(HWND list, int row, const search::ReportRow& d) {
    const auto vals = reportDisplayValues(d);
    LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = row;
    item.pszText = const_cast<wchar_t*>(vals[0].c_str());
    ListView_InsertItem(list, &item);
    for (int c = 1; c < REGULAR_REPORT_COLUMN_COUNT; ++c)
        setCell(list, row, c, vals[static_cast<size_t>(c)].c_str());
}

void updateReportRowCells(HWND list, int row, const search::ReportRow& d) {
    const auto vals = reportDisplayValues(d);
    for (int c = 0; c < REGULAR_REPORT_COLUMN_COUNT; ++c)
        setCellIfChanged(list, row, c, vals[static_cast<size_t>(c)]);
}

bool sameReportOrder(const std::vector<search::ReportRow>& oldR,
                     const std::vector<search::ReportRow>& newR) {
    if (oldR.size() != newR.size()) return false;
    for (size_t i = 0; i < oldR.size(); ++i)
        if (search::trim(oldR[i].id) != search::trim(newR[i].id)) return false;
    return true;
}

// Sort helpers
std::string reportSortValue(const search::ReportRow& r, int col) {
    switch (col) {
        case 0: return r.barcode_jz_flag; case 1: return r.oper_no;
        case 2: return r.name; case 3: return r.sex; case 4: return r.age;
        case 5: return r.order_text; case 6: return r.dept_name;
        case 7: return r.bed_code; case 8: return r.zymz_print;
        case 9: return r.patient_type; case 10: return r.requester;
        case 11: return r.group_name; case 12: return r.rep_no;
        case 13: return r.chk_flag; case 14: return r.conf; case 15: return r.txm_no;
        case 16: return r.group_name; case 17: return r.sample_name;
        case 18: return r.note; case 19: return r.dean_oper;
        case 20: return r.chk_date; case 21: return r.collection_time;
        case 22: return r.inspect_date; case 23: return r.rep_time;
        case 24: return r.fee; case 25: return r.req_doctor;
        case 26: return r.diag_name; case 27: return r.reg_no;
        case 28: return r.create_time; case 29: return r.patient_phone;
        default: return r.id;
    }
}

bool parseSortNumber(const std::string& v, double& out) {
    const std::string t = search::trim(v);
    if (t.empty()) return false;
    char* e = nullptr; out = std::strtod(t.c_str(), &e);
    return e && *e == '\0';
}

int compareReportSortValue(const search::ReportRow& a, const search::ReportRow& b, int col) {
    const std::string lv = search::trim(reportSortValue(a, col));
    const std::string rv = search::trim(reportSortValue(b, col));
    double ln = 0, rn = 0;
    if (parseSortNumber(lv, ln) && parseSortNumber(rv, rn) && ln != rn)
        return ln < rn ? -1 : 1;
    if (lv != rv) return lv < rv ? -1 : 1;
    return search::trim(a.id) < search::trim(b.id) ? -1 :
           (search::trim(a.id) == search::trim(b.id) ? 0 : 1);
}

bool reportSampleLess(const search::ReportRow& a, const search::ReportRow& b) {
    const std::string l = search::trim(a.oper_no), r = search::trim(b.oper_no);
    char *le = nullptr, *re = nullptr;
    const long lval = std::strtol(l.c_str(), &le, 10);
    const long rval = std::strtol(r.c_str(), &re, 10);
    if (le && *le == '\0' && !l.empty() && re && *re == '\0' && !r.empty() && lval != rval)
        return lval < rval;
    if (l != r) return l < r;
    return search::trim(a.id) < search::trim(b.id);
}

void sortReportRowsForDisplay(RegularReportState* st, std::vector<search::ReportRow>& rows,
                              bool preserveSort) {
    if (preserveSort && st && st->reportSortColumn >= 0) {
        const int col = st->reportSortColumn;
        const bool asc = st->reportSortAscending;
        std::stable_sort(rows.begin(), rows.end(),
            [col, asc](const auto& a, const auto& b) {
                int c = compareReportSortValue(a, b, col);
                return asc ? c < 0 : c > 0;
            });
        return;
    }
    std::stable_sort(rows.begin(), rows.end(), reportSampleLess);
}

bool containsId(const std::vector<std::string>& ids, const std::string& id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void presentReportRows(RegularReportState* st,
                       const std::vector<search::ReportRow>* prev = nullptr) {
    if (!st || !st->reportList) return;
    if (prev && sameReportOrder(*prev, st->reportRows)) {
        SendMessageW(st->reportList, WM_SETREDRAW, FALSE, 0);
        for (size_t i = 0; i < st->reportRows.size(); ++i)
            updateReportRowCells(st->reportList, static_cast<int>(i), st->reportRows[i]);
        SendMessageW(st->reportList, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(st->reportList, nullptr, TRUE);
        InvalidateRect(st->rightPanel, nullptr, TRUE);
        return;
    }
    SendMessageW(st->reportList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(st->reportList);
    for (size_t i = 0; i < st->reportRows.size(); ++i)
        insertReportRow(st->reportList, static_cast<int>(i), st->reportRows[i]);
    SendMessageW(st->reportList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(st->reportList, nullptr, TRUE);
    InvalidateRect(st->rightPanel, nullptr, TRUE);
}

void sortReportRowsByColumn(RegularReportState* st, int col) {
    if (!st || !st->reportList || st->reportRows.empty() || col < 0) return;
    if (st->reportSortColumn == col) st->reportSortAscending = !st->reportSortAscending;
    else { st->reportSortColumn = col; st->reportSortAscending = true; }
    const bool asc = st->reportSortAscending;

    std::string selId;
    const int sel = ListView_GetNextItem(st->reportList, -1, LVNI_SELECTED);
    if (sel >= 0 && sel < static_cast<int>(st->reportRows.size()))
        selId = st->reportRows[static_cast<size_t>(sel)].id;

    std::vector<std::string> chkIds;
    for (int idx : regularCheckedReportIndexes(st))
        chkIds.push_back(st->reportRows[static_cast<size_t>(idx)].id);

    std::stable_sort(st->reportRows.begin(), st->reportRows.end(),
        [col, asc](const auto& a, const auto& b) {
            int c = compareReportSortValue(a, b, col); return asc ? c < 0 : c > 0;
        });

    st->suppressReportSelectionQuery = true;
    presentReportRows(st);
    int rest = -1;
    for (int i = 0; i < static_cast<int>(st->reportRows.size()); ++i) {
        if (containsId(chkIds, st->reportRows[static_cast<size_t>(i)].id))
            ListView_SetCheckState(st->reportList, i, TRUE);
        if (!selId.empty() && st->reportRows[static_cast<size_t>(i)].id == selId) rest = i;
    }
    if (rest >= 0) {
        selectReportRow(st, rest); st->selectedReportIndex = rest;
        populateLeftPanelFromReport(st, rest);
    } else {
        st->selectedReportIndex = -1; populateLeftPanelFromReport(st, -1);
    }
    st->contextReportIndex = -1;
    st->suppressReportSelectionQuery = false;
}

// ============================================================================
// Result row display
// ============================================================================

std::string referenceRange(const search::ResultRow& r) {
    const std::string d = search::trim(r.downbound), u = search::trim(r.upbound);
    if (!d.empty() && !u.empty()) return d + "-" + u;
    return d.empty() ? u : d;
}

const wchar_t* deviationMarker(const search::ResultRow& r) {
    const std::string v = search::trim(r.normal);
    if (v == "1") return L"↑";
    if (v == "5") return L"↓";
    return L"";
}

void insertResultRow(HWND list, int row, const search::ResultRow& d,
                     const std::string& groupName) {
    LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = row;
    item.pszText = const_cast<wchar_t*>(L"");
    ListView_InsertItem(list, &item);
    wchar_t no[16]{}; wsprintfW(no, L"%d", row + 1);
    setCell(list, row, 1, no);
    setCell(list, row, 2, groupName);
    setCell(list, row, 3, d.item_eng);
    setCell(list, row, 4, d.item_name);
    setCell(list, row, 5, d.result);
    setCell(list, row, 6, deviationMarker(d));
    setCell(list, row, 7, referenceRange(d));
    setCell(list, row, 8, d.unit);
    setCell(list, row, 9, L"");
}

void presentResultRows(RegularReportState* st) {
    if (!st || !st->resultList) return;
    finishResultEdit(st, false);
    SendMessageW(st->resultList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(st->resultList);
    std::string prevGroup;
    for (size_t i = 0; i < st->resultRows.size(); ++i) {
        const std::string gn = search::trim(st->resultRows[i].group_name);
        const bool same = i > 0 && !gn.empty() && gn == prevGroup;
        insertResultRow(st->resultList, static_cast<int>(i),
                        st->resultRows[i], same ? "" : st->resultRows[i].group_name);
        prevGroup = gn;
    }
    SendMessageW(st->resultList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(st->resultList, nullptr, TRUE);
}

// ============================================================================
// Left panel population from report
// ============================================================================

void populateLeftPanelFromReport(RegularReportState* st, int sel) {
    auto clr = [&]() {
        regularSetControlText(st->groupEdit, "");  regularSetControlText(st->sampleEdit, "");
        regularSetControlText(st->reportNoEdit, "");  regularSetControlText(st->operNoEdit, "");
        regularSetComboSingleText(st->patientTypeCombo, "");
        if (st->urgentCheck) SendMessageW(st->urgentCheck, BM_SETCHECK, BST_UNCHECKED, 0);
        regularSetControlText(st->urgentEdit, "");  regularSetControlText(st->barcodeEdit, "");
        regularSetControlText(st->regNoEdit, "");  regularSetControlText(st->patientNameEdit, "");
        regularSetControlText(st->sexEdit, "");  regularSetControlText(st->ageEdit, "");
        regularFillAgeUnitCombo(st->ageUnitCombo);
        regularSetControlText(st->bedEdit, "");  regularSetControlText(st->phoneEdit, "");
        regularSetControlText(st->deptEdit, "");  regularSetControlText(st->diagEdit, "");
        regularSetControlText(st->reqDoctorEdit, "");  regularSetControlText(st->feeEdit, "");
        regularSetControlText(st->testerEdit, "");  regularSetControlText(st->auditEdit, "");
        regularSetControlText(st->noteCodeEdit, "");  regularSetControlText(st->noteEdit, "");
        regularClearDatePickerValue(st->applyDatePicker);
        regularClearDatePickerValue(st->receiveDatePicker);
        regularClearDatePickerValue(st->machineDatePicker);
        regularClearDatePickerValue(st->reportDatePicker);
        regularSetControlText(st->collectDateEdit, "");
    };
    if (!st || sel < 0 || sel >= static_cast<int>(st->reportRows.size())) { if (st) clr(); return; }
    const auto& r = st->reportRows[static_cast<size_t>(sel)];
    regularSetControlText(st->groupEdit, r.group_name);
    regularSetControlText(st->sampleEdit, r.sample_name);
    regularSetControlText(st->reportNoEdit, r.rep_no);
    regularSetControlText(st->operNoEdit, r.oper_no);
    regularSetComboSingleText(st->patientTypeCombo, r.patient_type);
    if (st->urgentCheck) SendMessageW(st->urgentCheck, BM_SETCHECK,
        regularReportUsesEmergencyTextColor(r) ? BST_CHECKED : BST_UNCHECKED, 0);
    regularSetControlText(st->urgentEdit, "");
    regularSetControlText(st->barcodeEdit, r.txm_no);
    regularSetControlText(st->regNoEdit, r.reg_no);
    regularSetControlText(st->patientNameEdit, r.name);
    regularSetControlText(st->sexEdit, r.sex);
    const auto age = regularSplitAgeDisplayText(r.age);
    regularSetControlText(st->ageEdit, age.value);
    regularFillAgeUnitCombo(st->ageUnitCombo, age.unit);
    regularSetControlText(st->bedEdit, r.bed_code);
    regularSetControlText(st->phoneEdit, r.patient_phone);
    regularSetControlText(st->deptEdit, r.dept_name);
    regularSetControlText(st->diagEdit, r.diag_name);
    regularSetControlText(st->reqDoctorEdit, r.req_doctor);
    regularSetControlText(st->feeEdit, r.fee);
    regularSetControlText(st->testerEdit, r.requester);
    regularSetControlText(st->auditEdit, r.dean_oper);
    regularSetControlText(st->noteCodeEdit, "");
    regularSetControlText(st->noteEdit, r.note);
    regularSetDatePickerValue(st->applyDatePicker, r.chk_date);
    regularSetDatePickerValue(st->receiveDatePicker, r.collection_time);
    regularSetDatePickerValue(st->machineDatePicker, r.create_time);
    regularSetDatePickerValue(st->reportDatePicker, r.rep_time);
    st->suppressInspectDateQuery = true;
    regularSetDatePickerValue(st->inspectDatePicker, r.inspect_date);
    st->suppressInspectDateQuery = false;
    regularSetControlText(st->collectDateEdit, "");
}

// ============================================================================
// Result editing
// ============================================================================

void finishResultEdit(RegularReportState* st, bool commit, bool moveNext, bool restore) {
    if (!st || !st->resultEdit) return;
    const HWND ed = st->resultEdit;
    const int row = st->resultEditRow;
    st->resultEdit = nullptr; st->resultEditRow = -1;
    if (commit && row >= 0 && row < static_cast<int>(st->resultRows.size())) {
        wchar_t buf[512]{};
        GetWindowTextW(ed, buf, static_cast<int>(std::size(buf)));
        st->resultRows[static_cast<size_t>(row)].result = search::wide_to_utf8(buf);
        setCell(st->resultList, row, REGULAR_RESULT_VALUE_COL, buf);
        ListView_RedrawItems(st->resultList, row, row);
    }
    DestroyWindow(ed);
    if (restore && st->resultList) SetFocus(st->resultList);
    if (commit && moveNext && row + 1 < static_cast<int>(st->resultRows.size())) {
        selectReportRow(st, row + 1);
        beginResultEdit(st, row + 1);
    }
}

LRESULT CALLBACK resultEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                UINT_PTR sid, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_GETDLGCODE:
            return DefSubclassProc(hwnd, msg, wp, lp) | DLGC_WANTALLKEYS;
        case WM_KEYDOWN:
            if (wp == VK_RETURN) { finishResultEdit(st, true, true); return 0; }
            if (wp == VK_ESCAPE) { finishResultEdit(st, false); return 0; }
            break;
        case WM_KILLFOCUS: finishResultEdit(st, false, false, false); return 0;
        case WM_NCDESTROY: RemoveWindowSubclass(hwnd, resultEditProc, sid); break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void beginResultEdit(RegularReportState* st, int row) {
    if (!st || !st->resultList || row < 0 ||
        row >= static_cast<int>(st->resultRows.size())) return;
    finishResultEdit(st, false);
    RECT rc{};
    if (!ListView_GetSubItemRect(st->resultList, row, REGULAR_RESULT_VALUE_COL,
                                  LVIR_BOUNDS, &rc)) return;
    RECT cl{}; GetClientRect(st->resultList, &cl);
    if (rc.right <= cl.left || rc.left >= cl.right ||
        rc.bottom <= cl.top || rc.top >= cl.bottom) return;
    rc.left = std::max(rc.left, cl.left); rc.right = std::min(rc.right, cl.right);
    InflateRect(&rc, -1, -1);
    const std::wstring txt =
        search::utf8_to_wide(st->resultRows[static_cast<size_t>(row)].result);
    st->resultEdit = CreateWindowExW(0, L"EDIT", txt.c_str(),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        rc.left, rc.top, std::max(24, static_cast<int>(rc.right - rc.left)),
        std::max(20, static_cast<int>(rc.bottom - rc.top)),
        st->resultList, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!st->resultEdit) return;
    st->resultEditRow = row;
    SendMessageW(st->resultEdit, WM_SETFONT, reinterpret_cast<WPARAM>(st->ctx.uiFont), TRUE);
    SetWindowSubclass(st->resultEdit, resultEditProc, REGULAR_RESULT_EDIT_SUBCLASS,
                      reinterpret_cast<DWORD_PTR>(st));
    SendMessageW(st->resultEdit, EM_SETSEL, 0, -1);
    SetFocus(st->resultEdit);
}

bool beginResultEditFromPoint(RegularReportState* st, POINT pt) {
    if (!st || !st->resultList) return false;
    LVHITTESTINFO hit{}; hit.pt = pt;
    const int row = ListView_SubItemHitTest(st->resultList, &hit);
    if (row >= 0 && hit.iSubItem == REGULAR_RESULT_VALUE_COL) {
        ListView_SetItemState(st->resultList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemState(st->resultList, row, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        beginResultEdit(st, row); return true;
    } else {
        finishResultEdit(st, false);
    }
    return false;
}

LRESULT CALLBACK resultListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                UINT_PTR sid, DWORD_PTR data) {
    auto* st = reinterpret_cast<RegularReportState*>(data);
    switch (msg) {
        case WM_LBUTTONDOWN: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (beginResultEditFromPoint(st, pt)) return 0;
            break;
        }
        case WM_HSCROLL: case WM_VSCROLL: case WM_MOUSEWHEEL:
            finishResultEdit(st, false); break;
        case WM_NCDESTROY: RemoveWindowSubclass(hwnd, resultListProc, sid); break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// ============================================================================
// Sample input
// ============================================================================

std::string normalizeSampleNo(std::string v) {
    v = search::trim(std::move(v));
    if (v.empty()) return v;
    if (!std::all_of(v.begin(), v.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return v;
    auto nz = v.find_first_not_of('0');
    return nz == std::string::npos ? "0" : v.substr(nz);
}

int findReportIndexBySampleNo(const RegularReportState* st, const std::string& input) {
    if (!st) return -1;
    const std::string exact = search::trim(input);
    if (exact.empty()) return -1;
    const std::string norm = normalizeSampleNo(exact);
    for (int i = 0; i < static_cast<int>(st->reportRows.size()); ++i) {
        const std::string s = search::trim(st->reportRows[static_cast<size_t>(i)].oper_no);
        if (s == exact || normalizeSampleNo(s) == norm) return i;
    }
    return -1;
}

int findReportIndexByRepNo(const RegularReportState* st, const std::string& repNo) {
    if (!st) return -1;
    const std::string target = search::trim(repNo);
    if (target.empty()) return -1;
    for (int i = 0; i < static_cast<int>(st->reportRows.size()); ++i) {
        if (search::trim(st->reportRows[static_cast<size_t>(i)].rep_no) == target) return i;
    }
    return -1;
}

// ============================================================================
// Query results dispatch
// ============================================================================

void querySelectedResults(RegularReportState* st, int sel) {
    if (!st || sel < 0 || sel >= static_cast<int>(st->reportRows.size())) {
        if (st) finishResultEdit(st, false);
        if (st && st->resultList) ListView_DeleteAllItems(st->resultList);
        if (st) {
            populateLeftPanelFromReport(st, -1);
            st->resultRows.clear(); st->selectedReportIndex = -1;
            st->contextReportIndex = -1; st->resultQueryLoading = false;
            st->pictureQueryLoading = false; st->pictureRepNo.clear();
            ++st->pictureQueryGeneration;
            regularClearPictureView(st, L"");
        }
        return;
    }
    populateLeftPanelFromReport(st, sel);
    st->pictureQueryLoading = false; st->pictureRepNo.clear();
    ++st->pictureQueryGeneration;
    regularClearPictureView(st, L"");
    regularQuerySelectedPicture(st, sel);
    if (st->resultQueryLoading && st->selectedReportIndex == sel) return;
    finishResultEdit(st, false);
    ListView_DeleteAllItems(st->resultList);
    st->resultRows.clear(); st->selectedReportIndex = sel;
    st->resultQueryLoading = true;
    SetWindowTextW(st->status, L"正在查询项目明细...");
    const int gen = ++st->resultQueryGeneration;
    const HWND hwnd = st->hwnd;
    const std::string conn = st->reportConnectionString;
    const std::string repNo = st->reportRows[static_cast<size_t>(sel)].rep_no;
    std::thread([hwnd, conn, repNo, gen]() {
        auto* r = new ResultLoadResult; r->generation = gen;
        r->ok = search::load_result_rows(conn, repNo, r->rows, r->error);
        if (!PostMessageW(hwnd, WM_REGULAR_RESULTS_LOADED, 0, reinterpret_cast<LPARAM>(r)))
            delete r;
    }).detach();
}

// ============================================================================
// Query completion
// ============================================================================

void finishReportQuery(RegularReportState* st, HWND hwnd,
                       std::unique_ptr<ReportLoadResult> result) {
    if (!st || !result || result->generation != st->reportQueryGeneration) return;
    st->reportQueryLoading = false;
    if (!result->ok) {
        SetWindowTextW(st->status, L"样本列表查询失败");
        MessageBoxW(hwnd, search::utf8_to_wide(result->error).c_str(), L"查询失败", MB_ICONERROR);
        return;
    }
    const bool ps = result->preserveState;
    const std::vector<search::ReportRow> prev = st->reportRows;
    std::string selId;
    const int sel = st->reportList ? ListView_GetNextItem(st->reportList, -1, LVNI_SELECTED) : -1;
    if (sel >= 0 && sel < static_cast<int>(prev.size()))
        selId = search::trim(prev[static_cast<size_t>(sel)].id);

    std::vector<std::string> chkIds;
    if (ps) {
        for (int idx : regularCheckedReportIndexes(st))
            if (idx >= 0 && idx < static_cast<int>(prev.size()))
                chkIds.push_back(search::trim(prev[static_cast<size_t>(idx)].id));
    }
    const int top = ps && st->reportList ? ListView_GetTopIndex(st->reportList) : -1;

    sortReportRowsForDisplay(st, result->rows, ps);
    st->reportRows = std::move(result->rows);
    st->reportConnectionString = std::move(result->connectionString);
    st->reportQueryDate = std::move(result->queryDate);

    st->suppressReportSelectionQuery = true;
    presentReportRows(st, ps ? &prev : nullptr);
    int rest = -1;
    int pendingTarget = -1;
    if (ps) {
        for (int i = 0; i < static_cast<int>(st->reportRows.size()); ++i) {
            if (containsId(chkIds, st->reportRows[static_cast<size_t>(i)].id))
                ListView_SetCheckState(st->reportList, i, TRUE);
            if (!selId.empty() &&
                st->reportRows[static_cast<size_t>(i)].id == selId) rest = i;
        }
        if (top >= 0 && top < ListView_GetItemCount(st->reportList))
            ListView_EnsureVisible(st->reportList, top, FALSE);
    }
    if (st->pendingOpenReport) {
        pendingTarget = findReportIndexByRepNo(st, st->pendingOpenRepNo);
        if (pendingTarget < 0 && !st->pendingOpenOperNo.empty()) {
            pendingTarget = findReportIndexBySampleNo(st, st->pendingOpenOperNo);
        }
        rest = pendingTarget;
    }
    if (rest >= 0) selectReportRow(st, rest);
    st->suppressReportSelectionQuery = false;

    if (st->pendingOpenReport) {
        st->pendingOpenReport = false;
        if (pendingTarget >= 0) {
            st->selectedReportIndex = pendingTarget;
            populateLeftPanelFromReport(st, pendingTarget);
            querySelectedResults(st, pendingTarget);
        } else {
            querySelectedResults(st, -1);
            MessageBoxW(hwnd, L"已打开常规报告，但未在当前日期和仪器下找到目标报告。",
                        L"常规报告", MB_ICONINFORMATION);
        }
        st->pendingOpenRepNo.clear();
        st->pendingOpenOperNo.clear();
    } else if (ps) {
        if (rest >= 0) {
            st->selectedReportIndex = rest;
            populateLeftPanelFromReport(st, rest);
            querySelectedResults(st, rest);
        } else {
            querySelectedResults(st, -1);
        }
    } else if (!st->reportRows.empty()) {
        selectReportRow(st, 0);
        querySelectedResults(st, 0);
    } else {
        querySelectedResults(st, -1);
    }
    SetWindowTextW(st->status,
        search::utf8_to_wide(search::make_query_count_status(st->reportRows.size())).c_str());
}

void finishResultQuery(RegularReportState* st, HWND hwnd,
                       std::unique_ptr<ResultLoadResult> result) {
    if (!st || !result || result->generation != st->resultQueryGeneration) return;
    st->resultQueryLoading = false;
    if (!result->ok) {
        MessageBoxW(hwnd, search::utf8_to_wide(result->error).c_str(),
                    L"查询项目明细失败", MB_ICONERROR);
        return;
    }
    finishResultEdit(st, false);
    st->resultRows = std::move(result->rows);
    presentResultRows(st);
    SetWindowTextW(st->status,
        search::utf8_to_wide(search::make_query_count_status(st->reportRows.size())).c_str());
}

// ============================================================================
// Barcode printing
// ============================================================================

const search::ReportRow* contextReportRow(const RegularReportState* st) {
    if (!st || st->contextReportIndex < 0 ||
        st->contextReportIndex >= static_cast<int>(st->reportRows.size())) return nullptr;
    return &st->reportRows[static_cast<size_t>(st->contextReportIndex)];
}

std::string barcodeGroupNameForReport(RegularReportState* st, int idx, std::string& err) {
    err.clear();
    if (!st || idx < 0 || idx >= static_cast<int>(st->reportRows.size())) {
        err = "invalid report row"; return "";
    }
    return search::trim(st->reportRows[static_cast<size_t>(idx)].group_name);
}

search::BarcodeLabelPayload barcodePayloadForReport(const search::ReportRow& r,
                                                    const std::string& gn) {
    search::BarcodeLabelPayload p;
    p.sample_no = r.oper_no; p.test_item = gn; p.barcode_value = r.txm_no;
    p.patient_name = r.name; p.specimen_type = r.sample_name;
    p.department = r.dept_name; p.patient_id = r.reg_no;
    p.timestamp = regularSlashDate(r.chk_date);
    return p;
}

}  // namespace

// ============================================================================
// Exported wrappers (regular* functions declared in state.h)
// ============================================================================

bool applyQuickMachineSlot(RegularReportState* st, int slot, bool showMissingMessage) {
    if (!st || slot < 0 || slot >= REGULAR_QUICK_MACHINE_COUNT) return false;
    const std::wstring name = search::load_module_str(
        L"RegularReport", regularQuickMachineNameKey(slot), L"");
    const std::wstring code = search::load_module_str(
        L"RegularReport", regularQuickMachineCodeKey(slot), L"");
    const std::wstring room = search::load_module_str(
        L"RegularReport", regularQuickMachineRoomKey(slot), L"");
    if (search::trim(search::wide_to_utf8(code)).empty()) {
        if (showMissingMessage)
            MessageBoxW(st->hwnd, L"请先在系统设置中配置该快捷检验仪器。", L"常规报告", MB_ICONINFORMATION);
        return false;
    }
    const std::string nextCode = search::wide_to_utf8(code);
    const std::string nextRoom = search::wide_to_utf8(room);
    const bool sameMachine = search::trim(st->selectedMachineCode) == search::trim(nextCode) &&
                             search::trim(st->selectedRoomCode) == search::trim(nextRoom);
    SetWindowTextW(st->machineEdit, name.empty() ? code.c_str() : name.c_str());
    st->selectedMachineCode = nextCode;
    st->selectedRoomCode = nextRoom;
    regularUpdateQuickMachineButtonLabels(st);
    runReportQuery(st, sameMachine && hasSelectedReportRow(st));
    return true;
}

void regularApplyQuickMachine(RegularReportState* st, int slot) {
    applyQuickMachineSlot(st, slot, true);
}

void regularOpenReportTarget(RegularReportState* st, const RegularReportOpenTarget& target) {
    if (!st) return;
    const std::string machCode = search::trim(target.mach_code);
    const std::string repNo = search::trim(target.rep_no);
    if (machCode.empty() || repNo.empty()) {
        MessageBoxW(st->hwnd,
                    L"目标报告缺少检验仪器或报告号，无法跳转到常规报告。",
                    L"常规报告", MB_ICONWARNING);
        return;
    }

    SYSTEMTIME date{};
    const std::string dateText = search::trim(target.inspect_date);
    if (!regularParseDateTimeText(dateText, date)) {
        MessageBoxW(st->hwnd,
                    L"目标报告缺少有效检验日期，无法跳转到常规报告。",
                    L"常规报告", MB_ICONWARNING);
        return;
    }

    st->selectedMachineCode = machCode;
    st->selectedRoomCode = search::trim(target.room_code);
    st->pendingOpenReport = true;
    st->pendingOpenRepNo = repNo;
    st->pendingOpenOperNo = search::trim(target.oper_no);

    const std::wstring machineText = search::utf8_to_wide(
        search::trim(target.mach_name).empty() ? machCode : search::trim(target.mach_name));
    SetWindowTextW(st->machineEdit, machineText.c_str());
    regularUpdateQuickMachineButtonLabels(st);

    st->suppressInspectDateQuery = true;
    DateTime_SetSystemtime(st->inspectDatePicker, GDT_VALID, &date);
    st->suppressInspectDateQuery = false;
    SetWindowTextW(st->status, L"正在跳转到目标报告...");
    runReportQuery(st, false);
}

void regularShowReportContextMenu(RegularReportState* st, const NMITEMACTIVATE* item) {
    if (!st || !st->reportList || !item || item->iItem < 0 ||
        item->iItem >= static_cast<int>(st->reportRows.size())) return;

    ListView_SetItemState(st->reportList, item->iItem,
                          LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    st->contextReportIndex = item->iItem;
    POINT pt = item->ptAction;
    ClientToScreen(st->reportList, &pt);
    if (pt.x == 0 && pt.y == 0) GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, REGULAR_IDM_REPORT_PRINT_BARCODE, L"打印条码");
    AppendMenuW(menu,
                regularCheckedReportIndexes(st).empty() ? (MF_STRING | MF_GRAYED) : MF_STRING,
                REGULAR_IDM_REPORT_PRINT_CHECKED_BARCODES, L"打印勾选条码");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, REGULAR_IDM_REPORT_TREND, L"趋势图");
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                   pt.x, pt.y, 0, st->hwnd, nullptr);
    DestroyMenu(menu);
}

std::vector<int> regularCheckedReportIndexes(const RegularReportState* st) {
    std::vector<int> indexes;
    if (!st || !st->reportList) return indexes;
    const int count = ListView_GetItemCount(st->reportList);
    for (int i = 0; i < count && i < static_cast<int>(st->reportRows.size()); ++i) {
        if (ListView_GetCheckState(st->reportList, i)) indexes.push_back(i);
    }
    return indexes;
}

void regularClearReportChecks(RegularReportState* st) {
    if (!st || !st->reportList) return;
    const int count = ListView_GetItemCount(st->reportList);
    for (int i = 0; i < count; ++i) ListView_SetCheckState(st->reportList, i, FALSE);
}

std::wstring regularPrintBarcodeForContext(RegularReportState* st) {
    const search::ReportRow* row = contextReportRow(st);
    if (!row) return L"请先右键选择一条报告记录。";

    std::string groupError;
    const std::string barcodeGroupName =
        barcodeGroupNameForReport(st, st->contextReportIndex, groupError);
    if (!groupError.empty()) {
        std::wstring msg = L"打印条码失败：组合项目查询失败。";
        msg += L"\n" + search::utf8_to_wide(groupError);
        return msg;
    }
    const auto payload = barcodePayloadForReport(*row, barcodeGroupName);
    const std::wstring details = search::barcode_label_details(payload);

    if (!search::barcode_label_printing_available()) {
        std::wstring msg = L"打印条码功能不可用：构建时未找到 LabelPrint 项目。\n\n";
        msg += details;
        return msg;
    }
    try {
        const std::wstring printerName = search::configured_barcode_printer_name();
        search::print_barcode_label(payload, printerName);
        std::wstring msg = L"打印条码已发送。\n打印机：";
        msg += printerName + L"\n\n" + details;
        return msg;
    } catch (const std::exception& ex) {
        std::wstring msg = L"打印条码失败：";
        msg += search::utf8_to_wide(ex.what());
        msg += L"\n打印机：" + search::configured_barcode_printer_name();
        msg += L"\n请在系统设置页重新选择条码打印机。\n\n" + details;
        return msg;
    }
}

std::wstring regularPrintCheckedBarcodes(RegularReportState* st) {
    const std::vector<int> indexes = regularCheckedReportIndexes(st);
    if (indexes.empty()) return L"请先勾选需要打印条码的报告记录。";
    if (!search::barcode_label_printing_available()) {
        regularClearReportChecks(st);
        return L"打印条码功能不可用：构建时未找到 LabelPrint 项目。";
    }
    const std::wstring printerName = search::configured_barcode_printer_name();
    int sent = 0;
    try {
        for (int idx : indexes) {
            std::string groupError;
            const std::string gn = barcodeGroupNameForReport(st, idx, groupError);
            if (!groupError.empty())
                throw std::runtime_error("组合项目查询失败: " + groupError);
            search::print_barcode_label(
                barcodePayloadForReport(st->reportRows[static_cast<size_t>(idx)], gn),
                printerName);
            ++sent;
        }
        regularClearReportChecks(st);
        return L"勾选条码已发送。\n打印机：" + printerName +
               L"\n数量：" + std::to_wstring(sent);
    } catch (const std::exception& ex) {
        std::wstring msg = L"批量打印条码失败：";
        msg += search::utf8_to_wide(ex.what());
        msg += L"\n打印机：" + printerName;
        msg += L"\n已发送：" + std::to_wstring(sent) + L" / " +
               std::to_wstring(indexes.size());
        regularClearReportChecks(st);
        return msg;
    }
}

void regularShowTrendForContext(RegularReportState* st) {
    const search::ReportRow* row = contextReportRow(st);
    if (!st || !row) {
        MessageBoxW(st ? st->hwnd : nullptr, L"请先右键选择一条报告记录。",
                    L"趋势图提示", MB_ICONINFORMATION);
        return;
    }
    if (search::build_connection_string_w(st->ctx.dbSettings).empty()) {
        MessageBoxW(st->hwnd, L"请先在“设置”中填写数据库连接信息。",
                    L"缺少数据库设置", MB_ICONWARNING);
        return;
    }
    const std::string patientId = search::trim(row->reg_no);
    const std::string patientName = search::trim(row->name);
    if (patientId.empty() && patientName.empty()) {
        MessageBoxW(st->hwnd, L"当前报告缺少病人号和姓名，无法打开趋势图。",
                    L"趋势图提示", MB_ICONWARNING);
        return;
    }

    SYSTEMTIME endDate = regularReportTrendEndDate(*row);
    SYSTEMTIME startDate = regularAddDays(endDate, -REGULAR_TREND_DEFAULT_DAYS + 1);

    search::QueryInput input;
    input.patient_id = patientId;
    input.patient_name = patientName;
    input.room_code = st->selectedRoomCode;
    input.mach_code = st->selectedMachineCode;
    input.start_date = regularDateText(startDate);
    input.end_date = regularDateText(endDate);
    input.limit = 0;

    if (patientId.empty()) {
        MessageBoxW(st->hwnd,
                    L"当前报告缺少病人号，将按姓名查询趋势，可能包含同名病人结果。",
                    L"趋势图提示", MB_ICONINFORMATION);
    }
    search::show_trend_window(st->hwnd,
                              st->ctx.uiFont ? st->ctx.uiFont :
                                  static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                              st->ctx.dbSettings, input);
}

void regularSelectReportRowBySampleInput(RegularReportState* st) {
    if (!st || !st->operNoEdit) return;
    const std::string input = search::wide_to_utf8(regularWindowText(st->operNoEdit));
    const int index = findReportIndexBySampleNo(st, input);
    if (index < 0) {
        MessageBoxW(st->hwnd, L"当前右侧列表中未找到该样本号。", L"常规报告", MB_ICONINFORMATION);
        return;
    }
    const int current = st->reportList ?
        ListView_GetNextItem(st->reportList, -1, LVNI_SELECTED) : -1;
    selectReportRow(st, index);
    if (current == index) querySelectedResults(st, index);
}

namespace {

// ============================================================================
// wndProc
// ============================================================================

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<RegularReportState*>(
        GetPropW(hwnd, REGULAR_REPORT_PROP_STATE));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* mcs = reinterpret_cast<MDICREATESTRUCTW*>(cs->lpCreateParams);
            st = reinterpret_cast<RegularReportState*>(mcs->lParam);
            if (!st) {
                LOG_ERROR("WM_CREATE: lpCreateParams is null (RegularReportState)");
                return -1;
            }
            SetPropW(hwnd, REGULAR_REPORT_PROP_STATE, reinterpret_cast<HANDLE>(st));
            st->hwnd = hwnd;
            st->bgBrush = CreateSolidBrush(RGB(0xB8, 0xB8, 0xB8));
            st->panelBrush = CreateSolidBrush(RGB(0xEF, 0xEF, 0xEF));
            st->blackBrush = CreateSolidBrush(RGB(0, 0, 0));
            st->pendingSplitterX = search::load_module_int(L"RegularReport", L"SplitterX", 0);
            createControls(hwnd, st);
            layout(hwnd, st);
            st->initialQuickMachineTimerActive =
                SetTimer(hwnd, IDT_REPORT_INITIAL_QUICK_MACHINE, 1, nullptr) != 0;
            return 0;
        }

        case WM_REGULAR_REPORTS_LOADED:
            if (st) finishReportQuery(st, hwnd,
                std::unique_ptr<ReportLoadResult>(reinterpret_cast<ReportLoadResult*>(lp)));
            else delete reinterpret_cast<ReportLoadResult*>(lp);
            return 0;

        case WM_REGULAR_OPEN_REPORT: {
            if (st && st->initialQuickMachineTimerActive) {
                KillTimer(hwnd, IDT_REPORT_INITIAL_QUICK_MACHINE);
                st->initialQuickMachineTimerActive = false;
            }
            if (st) st->skipInitialQuickMachineLoad = true;
            std::unique_ptr<RegularReportOpenTarget> target(
                reinterpret_cast<RegularReportOpenTarget*>(lp));
            if (st && target) regularOpenReportTarget(st, *target);
            return 0;
        }

        case WM_REGULAR_RESULTS_LOADED:
            if (st) finishResultQuery(st, hwnd,
                std::unique_ptr<ResultLoadResult>(reinterpret_cast<ResultLoadResult*>(lp)));
            else delete reinterpret_cast<ResultLoadResult*>(lp);
            return 0;

        case WM_REGULAR_PICTURE_LOADED:
            if (st) {
                std::unique_ptr<PictureLoadResult> r(
                    reinterpret_cast<PictureLoadResult*>(lp));
                if (r && r->generation == st->pictureQueryGeneration) {
                    st->pictureQueryLoading = false;
                    if (!r->ok)
                        regularClearPictureView(st,
                            L"图像查询失败：" + search::utf8_to_wide(r->error));
                    else {
                        delete st->pictureImage; st->pictureImage = nullptr;
                        if (st->pictureStream) { st->pictureStream->Release(); st->pictureStream = nullptr; }
                        if (!r->picture.empty()) {
                            if (!st->gdiplusReady) {
                                Gdiplus::GdiplusStartupInput inp;
                                st->gdiplusReady = Gdiplus::GdiplusStartup(
                                    &st->gdiplusToken, &inp, nullptr) == Gdiplus::Ok;
                            }
                            if (st->gdiplusReady) {
                                HGLOBAL m = GlobalAlloc(GMEM_MOVEABLE, r->picture.size());
                                if (m) {
                                    void* d = GlobalLock(m);
                                    if (d) {
                                        std::memcpy(d, r->picture.data(), r->picture.size());
                                        GlobalUnlock(m);
                                        if (CreateStreamOnHGlobal(m, TRUE, &st->pictureStream) == S_OK &&
                                            st->pictureStream) {
                                            st->pictureImage = Gdiplus::Image::FromStream(
                                                st->pictureStream, FALSE);
                                            if (!st->pictureImage ||
                                                st->pictureImage->GetLastStatus() != Gdiplus::Ok) {
                                                delete st->pictureImage; st->pictureImage = nullptr;
                                                st->pictureStream->Release(); st->pictureStream = nullptr;
                                            }
                                        }
                                    } else GlobalFree(m);
                                }
                            }
                        }
                        if (!st->pictureImage && !r->picture.empty())
                            st->pictureStatus = L"图像解码失败";
                        if (IsWindow(st->pictureView))
                            InvalidateRect(st->pictureView, nullptr, TRUE);
                    }
                }
            } else delete reinterpret_cast<PictureLoadResult*>(lp);
            return 0;

        case WM_SIZE:
            if (st && st->pendingSplitterX > 0 && IsZoomed(hwnd)) {
                st->splitterX = st->pendingSplitterX; st->splitterUserSet = true;
                st->pendingSplitterX = 0;
            }
            layout(hwnd, st); return 0;

        case WM_TIMER:
            if (st && wp == IDT_REPORT_INITIAL_QUICK_MACHINE) {
                KillTimer(hwnd, IDT_REPORT_INITIAL_QUICK_MACHINE);
                st->initialQuickMachineTimerActive = false;
                if (!st->skipInitialQuickMachineLoad &&
                    search::trim(st->selectedMachineCode).empty()) {
                    applyQuickMachineSlot(st, 0, false);
                }
                return 0;
            }
            if (st && wp == IDT_REPORT_AUTO_REFRESH) {
                if (regularIsAutoRefreshChecked(st)) runAutoRefreshQuery(st);
                else regularStopAutoRefreshTimer(st);
                return 0;
            }
            break;

        case search::WM_SPLITTER_DRAG:
            if (st && reinterpret_cast<HWND>(lp) == st->splitter) {
                st->splitterX = static_cast<int>(wp); st->splitterUserSet = true;
                layout(hwnd, st);
            }
            return 0;

        case search::WM_SPLITTER_RELEASED:
            if (st && reinterpret_cast<HWND>(lp) == st->splitter) {
                st->splitterX = static_cast<int>(wp); st->splitterUserSet = true;
                layout(hwnd, st);
                search::save_module_int(L"RegularReport", L"SplitterX", st->splitterX);
            }
            return 0;

        case WM_COMMAND:
            if (LOWORD(wp) == REGULAR_IDM_REPORT_PRINT_BARCODE) {
                MessageBoxW(hwnd, regularPrintBarcodeForContext(st).c_str(),
                            L"常规报告", MB_ICONINFORMATION);
                return 0;
            }
            if (LOWORD(wp) == REGULAR_IDM_REPORT_PRINT_CHECKED_BARCODES) {
                MessageBoxW(hwnd, regularPrintCheckedBarcodes(st).c_str(),
                            L"常规报告", MB_ICONINFORMATION);
                return 0;
            }
            if (LOWORD(wp) == REGULAR_IDM_REPORT_TREND) {
                regularShowTrendForContext(st);
                return 0;
            }
            break;

        case app::WM_APP_FONT_CHANGED:
            if (st && lp) {
                if (IsWindow(st->machinePickerPopup)) DestroyWindow(st->machinePickerPopup);
                st->ctx.uiFont = reinterpret_cast<HFONT>(lp);
                clearLeftPanel(st); createControls(hwnd, st);
                regularApplyFont(hwnd, st->ctx.uiFont);
                regularRefreshLeftGroupTitleFont(st);
                layout(hwnd, st);
            }
            return 0;

        case app::WM_APP_SETTINGS_CHANGED:
            if (st) regularUpdateQuickMachineButtonLabels(st);
            return 0;

        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            HWND ctl = reinterpret_cast<HWND>(lp);
            if (GetPropW(ctl, L"RegularEmergencyLabel")) {
                SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(0xE6, 0, 0));
                return reinterpret_cast<LRESULT>(st ? st->panelBrush : nullptr);
            }
            if (GetPropW(ctl, L"RegularLeftLabel")) {
                SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(0x00, 0x00, 0xC4));
                return reinterpret_cast<LRESULT>(st ? st->panelBrush : nullptr);
            }
            SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(0, 0, 0xCC));
            return reinterpret_cast<LRESULT>(st ? st->panelBrush : nullptr);
        }

        case WM_CTLCOLORDLG:
            return reinterpret_cast<LRESULT>(st ? st->bgBrush : nullptr);

        case WM_DESTROY:
            RemovePropW(hwnd, REGULAR_REPORT_PROP_STATE);
            if (st) {
                if (st->initialQuickMachineTimerActive) {
                    KillTimer(hwnd, IDT_REPORT_INITIAL_QUICK_MACHINE);
                    st->initialQuickMachineTimerActive = false;
                }
                regularStopAutoRefreshTimer(st);
                finishResultEdit(st, false);
                if (IsWindow(st->machinePickerPopup)) DestroyWindow(st->machinePickerPopup);
                if (IsWindow(st->picturePopup)) DestroyWindow(st->picturePopup);
                delete st->pictureImage; st->pictureImage = nullptr;
                if (st->pictureStream) { st->pictureStream->Release(); st->pictureStream = nullptr; }
                if (st->gdiplusReady) {
                    Gdiplus::GdiplusShutdown(st->gdiplusToken);
                    st->gdiplusReady = false; st->gdiplusToken = 0;
                }
                if (st->bgBrush) DeleteObject(st->bgBrush);
                if (st->panelBrush) DeleteObject(st->panelBrush);
                if (st->blackBrush) DeleteObject(st->blackBrush);
                if (st->groupTitleFont) DeleteObject(st->groupTitleFont);
                delete st;
            }
            break;
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

}  // namespace

// ============================================================================
// Public entry point
// ============================================================================

HWND create_regular_report_module(const ModuleContext& ctx) {
    if (HWND ex = activate_existing_mdi_child_by_title(
            ctx.mdiClient, REGULAR_REPORT_WINDOW_TITLE))
        return ex;

    static bool reg = false;
    if (!reg) {
        REGISTER_MDI_CHILD_CLASS(ctx.instance, wndProc, REGULAR_REPORT_WND_CLASS, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
        reg = true;
    }

    auto* st = new RegularReportState; st->ctx = ctx;
    MDICREATESTRUCTW mcs{};
    mcs.szClass = REGULAR_REPORT_WND_CLASS;
    mcs.szTitle = REGULAR_REPORT_WINDOW_TITLE;
    mcs.hOwner = ctx.instance;
    mcs.x = mcs.y = mcs.cx = mcs.cy = CW_USEDEFAULT;

    mcs.lParam = reinterpret_cast<LPARAM>(st);
    HWND child = reinterpret_cast<HWND>(
        SendMessageW(ctx.mdiClient, WM_MDICREATE, 0, reinterpret_cast<LPARAM>(&mcs)));
    if (child) SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    else { delete st; }
    return child;
}

#endif
