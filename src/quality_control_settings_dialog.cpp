#include "quality_control_settings_dialog.h"

#ifdef _WIN32

#include "machine_picker_popup.h"
#include "quality_control_store.h"
#include "resource.h"
#include "search_core.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"

#include <commctrl.h>
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace {

constexpr const wchar_t* WND_CLASS = L"QualityControlSettingsDialog";
constexpr const wchar_t* PROP_STATE = L"QualityControlSettingsState";
constexpr COLORREF COLOR_PAGE_BG = RGB(0xF3, 0xF6, 0xFA);
constexpr COLORREF COLOR_CARD_BG = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF COLOR_CARD_BORDER = RGB(0xD8, 0xE0, 0xEA);
constexpr COLORREF COLOR_TEXT = RGB(0x1F, 0x29, 0x37);
constexpr COLORREF COLOR_MUTED_TEXT = RGB(0x6B, 0x72, 0x80);
constexpr COLORREF COLOR_ACCENT = RGB(0x25, 0x63, 0xEB);

enum ControlId {
    IDC_LIST = 6901,
    IDC_ENABLED = 6902,
    IDC_MACH_CODE = 6903,
    IDC_MACH_NAME = 6904,
    IDC_SAMPLE_NO = 6905,
    IDC_QC_NAME = 6906,
    IDC_LEVEL = 6907,
    IDC_ITEM_CODE = 6908,
    IDC_ITEM_NAME = 6909,
    IDC_TARGET_MEAN = 6910,
    IDC_TARGET_SD = 6911,
    IDC_NEW = 6912,
    IDC_SAVE = 6913,
    IDC_DELETE = 6914,
    IDC_CLOSE = 6915,
    IDC_PICK_MACHINE = 6916,
    IDC_ROOM_CODE = 6917,
    IDC_TITLE = 6918,
    IDC_SUBTITLE = 6919,
    IDC_LIST_TITLE = 6920,
    IDC_LIST_HINT = 6921,
    IDC_FORM_TITLE = 6922,
    IDC_FORM_HINT = 6923,
    IDC_LOT_NO = 6924,
    IDC_VALID_FROM = 6925,
    IDC_VALID_TO = 6926,
    IDC_LOT_NOTE = 6927,
    IDC_DELETE_LOT = 6928,
    IDC_READ_DATE = 6929,
    IDC_READ_ITEMS = 6930,
};

struct State {
    HFONT font = nullptr;
    HFONT titleFont = nullptr;
    HFONT sectionFont = nullptr;
    HBRUSH pageBrush = nullptr;
    HBRUSH cardBrush = nullptr;
    HBRUSH editBrush = nullptr;
    search::DbSettings dbSettings;
    bool embedded = false;
    HWND title = nullptr;
    HWND subtitle = nullptr;
    HWND listTitle = nullptr;
    HWND listHint = nullptr;
    HWND formTitle = nullptr;
    HWND formHint = nullptr;
    HWND list = nullptr;
    HWND enabled = nullptr;
    HWND roomCodeLabel = nullptr;
    HWND roomCode = nullptr;
    HWND machCodeLabel = nullptr;
    HWND machCode = nullptr;
    HWND pickMachine = nullptr;
    HWND machNameLabel = nullptr;
    HWND machName = nullptr;
    HWND sampleNoLabel = nullptr;
    HWND sampleNo = nullptr;
    HWND qcNameLabel = nullptr;
    HWND qcName = nullptr;
    HWND levelLabel = nullptr;
    HWND level = nullptr;
    HWND itemCodeLabel = nullptr;
    HWND itemCode = nullptr;
    HWND itemNameLabel = nullptr;
    HWND itemName = nullptr;
    HWND targetMeanLabel = nullptr;
    HWND targetMean = nullptr;
    HWND targetSdLabel = nullptr;
    HWND targetSd = nullptr;
    HWND lotNoLabel = nullptr;
    HWND lotNo = nullptr;
    HWND validFromLabel = nullptr;
    HWND validFrom = nullptr;
    HWND validToLabel = nullptr;
    HWND validTo = nullptr;
    HWND lotNoteLabel = nullptr;
    HWND lotNote = nullptr;
    HWND readDateLabel = nullptr;
    HWND readDate = nullptr;
    std::vector<qc::Config> rows;
    std::vector<qc::Lot> lots;
    int currentId = 0;
    int currentLotId = 0;
};

void loadForm(State* st, int index);

HFONT createDerivedFont(HFONT base, int pointDelta, LONG weight) {
    LOGFONTW lf{};
    if (!base || GetObjectW(base, sizeof(lf), &lf) != sizeof(lf)) {
        NONCLIENTMETRICSW nm{};
        nm.cbSize = sizeof(nm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(nm), &nm, 0);
        lf = nm.lfMessageFont;
    }
    HDC screen = GetDC(nullptr);
    const int dpi = screen ? GetDeviceCaps(screen, LOGPIXELSY) : 96;
    const int point = std::max(8, MulDiv(std::abs(lf.lfHeight), 72, dpi));
    lf.lfHeight = -MulDiv(std::max(8, point + pointDelta), dpi, 72);
    lf.lfWeight = weight;
    if (screen) ReleaseDC(nullptr, screen);
    return CreateFontIndirectW(&lf);
}

void drawRoundRect(HDC dc, const RECT& rc, int radius, COLORREF fill, COLORREF border) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

HWND text(HWND parent, int id, const wchar_t* value) {
    return CreateWindowExW(0, L"STATIC", value, WS_CHILD | WS_VISIBLE | SS_LEFT,
                           0, 0, 0, 0, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

HWND label(HWND parent, const wchar_t* text) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
                           0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND edit(HWND parent, int id) {
    return search::create_edit(parent, id, 0, 0, 10, 24);
}

HWND datePicker(HWND parent, int id, bool allowBlank) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | DTS_SHORTDATECENTURYFORMAT;
    if (allowBlank) style |= DTS_SHOWNONE;
    HWND hwnd = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"", style,
                                0, 0, 10, 24, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
    DateTime_SetFormat(hwnd, L"yyyy-MM-dd");
    if (allowBlank) DateTime_SetSystemtime(hwnd, GDT_NONE, nullptr);
    return hwnd;
}

HWND button(HWND parent, int id, const wchar_t* text) {
    return search::create_button(parent, id, text, 0, 0, 76, 28);
}

std::string textUtf8(HWND hwnd) {
    wchar_t buffer[256]{};
    GetWindowTextW(hwnd, buffer, 256);
    return search::wide_to_utf8(buffer);
}

void setText(HWND hwnd, const std::string& value) {
    SetWindowTextW(hwnd, search::utf8_to_wide(value).c_str());
}

bool parseDate(const std::string& value, SYSTEMTIME& out) {
    const std::string text = search::trim(value);
    if (text.size() < 10) return false;
    int y = 0;
    int m = 0;
    int d = 0;
    if (std::sscanf(text.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return false;
    if (y < 1900 || m < 1 || m > 12 || d < 1 || d > 31) return false;
    ZeroMemory(&out, sizeof(out));
    out.wYear = static_cast<WORD>(y);
    out.wMonth = static_cast<WORD>(m);
    out.wDay = static_cast<WORD>(d);
    return true;
}

std::string dateText(HWND hwnd) {
    if (!hwnd) return "";
    SYSTEMTIME st{};
    if (DateTime_GetSystemtime(hwnd, &st) != GDT_VALID) return "";
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
    return buffer;
}

void setDate(HWND hwnd, const std::string& value, bool allowBlank) {
    if (!hwnd) return;
    SYSTEMTIME st{};
    if (parseDate(value, st)) {
        DateTime_SetSystemtime(hwnd, GDT_VALID, &st);
    } else if (allowBlank) {
        DateTime_SetSystemtime(hwnd, GDT_NONE, nullptr);
    }
}

std::string todayDate() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buffer;
}

void setToday(HWND hwnd) {
    setDate(hwnd, todayDate(), false);
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

void populateList(State* st) {
    if (!st || !st->list) return;
    ListView_DeleteAllItems(st->list);
    std::string error;
    if (!qc::load_configs(st->rows, error)) {
        MessageBoxW(GetParent(st->list), search::utf8_to_wide(error).c_str(), L"质控品设置", MB_ICONERROR);
        return;
    }
    for (int i = 0; i < static_cast<int>(st->rows.size()); ++i) {
        const auto& row = st->rows[static_cast<size_t>(i)];
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        const auto enabled = row.enabled ? L"是" : L"否";
        item.pszText = const_cast<wchar_t*>(enabled);
        ListView_InsertItem(st->list, &item);
        setCell(st->list, i, 1, row.mach_code);
        setCell(st->list, i, 2, row.sample_no);
        setCell(st->list, i, 3, row.qc_name);
        setCell(st->list, i, 4, row.level);
        setCell(st->list, i, 5, row.item_code);
        setCell(st->list, i, 6, row.item_name);
        std::vector<qc::Lot> lots;
        std::string lotError;
        if (qc::load_lots_for_config(row.id, lots, lotError) && !lots.empty()) {
            const auto found = std::find_if(lots.begin(), lots.end(), [](const qc::Lot& lot) {
                return lot.enabled && search::trim(lot.valid_to).empty();
            });
            const qc::Lot& lot = found == lots.end() ? lots.front() : *found;
            setCell(st->list, i, 7, lot.lot_no);
            setCell(st->list, i, 8, lot.valid_from);
            setCell(st->list, i, 9, lot.valid_to.empty() ? "当前" : lot.valid_to);
        }
    }
}

void selectListRow(State* st, int index) {
    if (!st || !st->list || index < 0 || index >= static_cast<int>(st->rows.size())) return;
    ListView_SetItemState(st->list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(st->list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(st->list, index, FALSE);
    loadForm(st, index);
}

int findRowIndex(const State* st, const qc::Config& row) {
    if (!st) return -1;
    const std::string mach = search::trim(row.mach_code);
    const std::string sample = search::trim(row.sample_no);
    const std::string item = search::trim(row.item_code);
    const std::string level = search::trim(row.level);
    for (int i = 0; i < static_cast<int>(st->rows.size()); ++i) {
        const auto& current = st->rows[static_cast<size_t>(i)];
        if (row.id > 0 && current.id == row.id) return i;
        if (search::trim(current.mach_code) == mach &&
            search::trim(current.sample_no) == sample &&
            search::trim(current.item_code) == item &&
            search::trim(current.level) == level) {
            return i;
        }
    }
    return -1;
}

int findRowIndexById(const State* st, int id) {
    if (!st || id <= 0) return -1;
    for (int i = 0; i < static_cast<int>(st->rows.size()); ++i) {
        if (st->rows[static_cast<size_t>(i)].id == id) return i;
    }
    return -1;
}

void clearLotForm(State* st) {
    if (!st) return;
    st->currentLotId = 0;
    st->lots.clear();
    SetWindowTextW(st->lotNo, L"");
    setDate(st->validFrom, "", true);
    setDate(st->validTo, "", true);
    SetWindowTextW(st->targetMean, L"");
    SetWindowTextW(st->targetSd, L"");
    SetWindowTextW(st->lotNote, L"");
}

void showMachinePicker(HWND owner, State* st) {
    if (!st) return;
    search::MachinePickerPopupOptions options;
    options.owner = owner;
    options.anchor = st->pickMachine;
    options.font = st->font;
    options.db_settings = st->dbSettings;
    options.current_room_code = search::trim(textUtf8(st->roomCode));
    options.current_mach_code = search::trim(textUtf8(st->machCode));
    options.include_all_rooms = true;
    options.on_accept = [st](const search::MachineOption& machine) {
        setText(st->roomCode, machine.room_code);
        setText(st->machCode, machine.mach_code);
        setText(st->machName, machine.mach_name);
    };
    search::show_machine_picker_popup(options);
}

void clearForm(State* st) {
    st->currentId = 0;
    clearLotForm(st);
    SendMessageW(st->enabled, BM_SETCHECK, BST_CHECKED, 0);
    SetWindowTextW(st->roomCode, L"");
    SetWindowTextW(st->machCode, L"");
    SetWindowTextW(st->machName, L"");
    SetWindowTextW(st->sampleNo, L"");
    SetWindowTextW(st->qcName, L"");
    SetWindowTextW(st->level, L"");
    SetWindowTextW(st->itemCode, L"");
    SetWindowTextW(st->itemName, L"");
    SetWindowTextW(st->targetMean, L"");
    SetWindowTextW(st->targetSd, L"");
    setToday(st->readDate);
}

void loadLatestLot(State* st) {
    clearLotForm(st);
    if (!st || st->currentId <= 0) return;
    std::string error;
    if (!qc::load_lots_for_config(st->currentId, st->lots, error)) {
        MessageBoxW(st->list, search::utf8_to_wide(error).c_str(), L"质控品设置", MB_ICONERROR);
        return;
    }
    if (st->lots.empty()) return;
    auto found = std::find_if(st->lots.begin(), st->lots.end(), [](const qc::Lot& lot) {
        return lot.enabled && search::trim(lot.valid_to).empty();
    });
    if (found == st->lots.end()) found = st->lots.begin();
    st->currentLotId = found->id;
    setText(st->lotNo, found->lot_no);
    setDate(st->validFrom, found->valid_from, true);
    setDate(st->validTo, found->valid_to, true);
    setText(st->lotNote, found->note);
    if (dateText(st->readDate).empty()) setDate(st->readDate, found->valid_from.empty() ? todayDate() : found->valid_from, false);
}

void loadCurrentLotTarget(State* st) {
    if (!st || st->currentLotId <= 0 || st->currentId <= 0) return;
    std::vector<qc::LotItemTarget> targets;
    std::string error;
    if (!qc::load_lot_item_targets(st->currentLotId, targets, error)) {
        MessageBoxW(st->list, search::utf8_to_wide(error).c_str(), L"质控品设置", MB_ICONERROR);
        return;
    }
    auto found = std::find_if(targets.begin(), targets.end(), [st](const qc::LotItemTarget& target) {
        return target.sample_item_id == st->currentId;
    });
    if (found == targets.end()) return;
    setText(st->targetMean, found->target_mean);
    setText(st->targetSd, found->target_sd);
}

void loadForm(State* st, int index) {
    if (!st || index < 0 || index >= static_cast<int>(st->rows.size())) return;
    const auto& row = st->rows[static_cast<size_t>(index)];
    st->currentId = row.id;
    SendMessageW(st->enabled, BM_SETCHECK, row.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    setText(st->roomCode, row.room_code);
    setText(st->machCode, row.mach_code);
    setText(st->machName, row.mach_name);
    setText(st->sampleNo, row.sample_no);
    setText(st->qcName, row.qc_name);
    setText(st->level, row.level);
    setText(st->itemCode, row.item_code);
    setText(st->itemName, row.item_name);
    loadLatestLot(st);
    if (!row.target_mean.empty() || !row.target_sd.empty()) {
        setText(st->targetMean, row.target_mean);
        setText(st->targetSd, row.target_sd);
    } else {
        loadCurrentLotTarget(st);
    }
}

bool collectForm(State* st, qc::Config& row) {
    row.id = st->currentId;
    row.enabled = SendMessageW(st->enabled, BM_GETCHECK, 0, 0) == BST_CHECKED;
    row.room_code = search::trim(textUtf8(st->roomCode));
    row.mach_code = search::trim(textUtf8(st->machCode));
    row.mach_name = search::trim(textUtf8(st->machName));
    row.sample_no = search::trim(textUtf8(st->sampleNo));
    row.qc_name = search::trim(textUtf8(st->qcName));
    row.level = search::trim(textUtf8(st->level));
    row.item_code = search::trim(textUtf8(st->itemCode));
    row.item_name = search::trim(textUtf8(st->itemName));
    row.target_mean = search::trim(textUtf8(st->targetMean));
    row.target_sd = search::trim(textUtf8(st->targetSd));
    if (row.mach_code.empty() || row.sample_no.empty()) {
        MessageBoxW(st->list, L"仪器代码和样本号不能为空。", L"质控品设置", MB_ICONWARNING);
        return false;
    }
    return true;
}

bool collectLotForm(State* st, int configId, qc::Lot& row, bool& hasLot) {
    row.id = st->currentLotId;
    row.config_id = configId;
    row.enabled = true;
    row.lot_no = search::trim(textUtf8(st->lotNo));
    row.target_mean = search::trim(textUtf8(st->targetMean));
    row.target_sd = search::trim(textUtf8(st->targetSd));
    row.valid_from = dateText(st->validFrom);
    row.valid_to = dateText(st->validTo);
    row.note = search::trim(textUtf8(st->lotNote));
    const bool hasTarget = !row.target_mean.empty() || !row.target_sd.empty();
    hasLot = !row.lot_no.empty() || !row.valid_from.empty() || !row.valid_to.empty() || hasTarget;
    if (!hasLot) return true;
    if (row.lot_no.empty() || row.valid_from.empty()) {
        MessageBoxW(st->list, L"维护批号或靶值时，质控批号和开始日期不能为空。", L"质控品设置", MB_ICONWARNING);
        return false;
    }
    auto found = std::find_if(st->lots.begin(), st->lots.end(), [st](const qc::Lot& lot) {
        return lot.id == st->currentLotId;
    });
    if (found != st->lots.end() &&
        (search::trim(found->lot_no) != row.lot_no || search::trim(found->valid_from) != row.valid_from)) {
        row.id = 0;
    }
    return true;
}

bool collectSampleConfigForm(State* st, qc::SampleConfig& row) {
    row.enabled = SendMessageW(st->enabled, BM_GETCHECK, 0, 0) == BST_CHECKED;
    row.room_code = search::trim(textUtf8(st->roomCode));
    row.mach_code = search::trim(textUtf8(st->machCode));
    row.mach_name = search::trim(textUtf8(st->machName));
    row.sample_no = search::trim(textUtf8(st->sampleNo));
    if (row.mach_code.empty() || row.sample_no.empty()) {
        MessageBoxW(st->list, L"仪器代码和样本号不能为空。", L"质控品设置", MB_ICONWARNING);
        return false;
    }
    return true;
}

bool collectReadItemsBatchLot(State* st, qc::Lot& row, bool& hasLot) {
    row.id = st->currentLotId;
    row.enabled = true;
    row.lot_no = search::trim(textUtf8(st->lotNo));
    row.target_mean = search::trim(textUtf8(st->targetMean));
    row.target_sd = search::trim(textUtf8(st->targetSd));
    row.valid_from = dateText(st->validFrom);
    row.valid_to = dateText(st->validTo);
    row.note = search::trim(textUtf8(st->lotNote));
    const bool hasTarget = !row.target_mean.empty() || !row.target_sd.empty();
    hasLot = !row.lot_no.empty() || !row.valid_from.empty() || !row.valid_to.empty() || !row.note.empty() || hasTarget;
    if (!hasLot) return true;
    if (row.lot_no.empty() || row.valid_from.empty()) {
        MessageBoxW(st->list, L"批量写入批号、靶值或备注时，质控批号和开始日期不能为空。", L"质控品设置", MB_ICONWARNING);
        return false;
    }
    auto found = std::find_if(st->lots.begin(), st->lots.end(), [st](const qc::Lot& lot) {
        return lot.id == st->currentLotId;
    });
    if (found != st->lots.end() &&
        (search::trim(found->lot_no) != row.lot_no || search::trim(found->valid_from) != row.valid_from)) {
        row.id = 0;
    }
    return true;
}

void readItemsFromLis(HWND hwnd, State* st) {
    if (!st) return;
    qc::SampleConfig sample;
    if (!collectSampleConfigForm(st, sample)) return;
    const std::string batchQcName = search::trim(textUtf8(st->qcName));
    const std::string batchLevel = search::trim(textUtf8(st->level));
    qc::Lot batchLot;
    bool hasBatchLot = false;
    if (!collectReadItemsBatchLot(st, batchLot, hasBatchLot)) return;
    const std::string date = dateText(st->readDate);
    if (date.empty()) {
        MessageBoxW(hwnd, L"读取日期不能为空。", L"质控品设置", MB_ICONWARNING);
        return;
    }
    std::string error;
    if (!qc::save_sample_config(sample, error)) {
        MessageBoxW(hwnd, search::utf8_to_wide(error).c_str(), L"质控品设置", MB_ICONERROR);
        return;
    }
    search::QualityControlSampleItemsQuery query;
    query.connection_string = search::wide_to_utf8(search::build_connection_string_w(st->dbSettings));
    query.mach_code = sample.mach_code;
    query.sample_no = sample.sample_no;
    query.inspect_date = date;
    std::vector<search::QualityControlSampleItemRow> items;
    if (!search::query_quality_control_sample_items(query, items, error)) {
        MessageBoxW(hwnd, search::utf8_to_wide(error).c_str(), L"质控品设置", MB_ICONERROR);
        return;
    }
    int order = 0;
    int savedCount = 0;
    bool filledFirstItem = false;
    for (const auto& item : items) {
        if (search::trim(item.item_code).empty()) continue;
        qc::SampleItem row;
        row.sample_config_id = sample.id;
        row.enabled = true;
        row.item_code = item.item_code;
        row.item_name = item.item_name;
        row.item_eng = item.item_eng;
        row.unit = item.unit;
        row.qc_name = batchQcName;
        row.level = batchLevel;
        row.sort_order = order++;
        if (!qc::save_sample_item(row, error)) {
            MessageBoxW(hwnd, search::utf8_to_wide(error).c_str(), L"质控品设置", MB_ICONERROR);
            return;
        }
        if (hasBatchLot) {
            qc::Lot itemLot = batchLot;
            itemLot.config_id = row.id;
            itemLot.sample_config_id = 0;
            if (!qc::save_lot(itemLot, error)) {
                MessageBoxW(hwnd, search::utf8_to_wide(error).c_str(), L"质控品设置", MB_ICONERROR);
                return;
            }
        }
        if (!filledFirstItem) {
            setText(st->itemCode, row.item_code);
            setText(st->itemName, row.item_name);
            filledFirstItem = true;
        }
        ++savedCount;
    }
    populateList(st);
    if (!st->rows.empty()) {
        for (int i = 0; i < static_cast<int>(st->rows.size()); ++i) {
            if (st->rows[static_cast<size_t>(i)].sample_config_id == sample.id) {
                selectListRow(st, i);
                break;
            }
        }
    }
    MessageBoxW(hwnd, (L"已读取并保存项目数：" + std::to_wstring(savedCount)).c_str(), L"质控品设置", MB_ICONINFORMATION);
}

void layout(HWND hwnd, State* st) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const float scale = search::dpi_scale_factor(hwnd);
    auto S = [scale](int v) { return static_cast<int>(v * scale); };
    const int pad = S(24);
    const int cardPadX = S(22);
    const int top = S(86);
    const int clientW = std::max(0, static_cast<int>(rc.right - rc.left));
    const int gap = S(18);
    const int contentW = std::max(S(760), clientW - pad * 2);
    const int desiredFormW = S(640);
    const int listW = std::max(S(360), std::min(S(520), contentW - gap - desiredFormW));
    const int formW = std::max(S(360), contentW - listW - gap);
    const RECT listCard{pad, top, pad + listW, std::max(top + S(360), static_cast<int>(rc.bottom) - S(24))};
    const RECT formCard{listCard.right + gap, top, listCard.right + gap + formW, listCard.bottom};
    const int labelW = S(78);
    const int rowH = S(34);
    const int editH = S(26);
    const int formX = formCard.left + cardPadX;
    const int formInnerW = formCard.right - formCard.left - cardPadX * 2;
    if (st->embedded) {
        const int innerPad = S(0);
        const int embeddedGap = S(16);
        const int embeddedListW = std::max(S(320), std::min(S(420), clientW * 2 / 5));
        const int embeddedFormX = embeddedListW + embeddedGap;
        const int embeddedFormW = std::max(S(320), clientW - embeddedFormX);
        const int embeddedLabelW = S(78);
        const int embeddedRowH = S(32);
        const int embeddedEditH = S(26);
        const int embeddedColGap = S(18);
        const int embeddedColW = std::max(S(220), (embeddedFormW - embeddedColGap) / 2);
        const int embeddedLeftX = embeddedFormX;
        const int embeddedRightX = embeddedFormX + embeddedColW + embeddedColGap;
        const int embeddedEditW = std::max(S(110), embeddedColW - embeddedLabelW - S(10));
        ShowWindow(st->title, SW_HIDE);
        ShowWindow(st->subtitle, SW_HIDE);
        ShowWindow(st->listTitle, SW_HIDE);
        ShowWindow(st->listHint, SW_HIDE);
        ShowWindow(st->formTitle, SW_HIDE);
        ShowWindow(st->formHint, SW_HIDE);
        MoveWindow(st->list, innerPad, innerPad,
                   embeddedListW, std::max(S(160), static_cast<int>(rc.bottom - rc.top)), TRUE);
        const auto placeEmbedded = [&](int colX, int row, HWND labelHwnd, HWND ctrl) {
            const int y = innerPad + row * embeddedRowH;
            MoveWindow(labelHwnd, colX, y, embeddedLabelW, embeddedEditH, TRUE);
            MoveWindow(ctrl, colX + embeddedLabelW + S(10), y, embeddedEditW, embeddedEditH, TRUE);
        };
        MoveWindow(st->enabled, embeddedLeftX + embeddedLabelW + S(10), innerPad,
                   S(90), embeddedEditH, TRUE);
        placeEmbedded(embeddedLeftX, 1, st->roomCodeLabel, st->roomCode);
        {
            const int y = innerPad + 2 * embeddedRowH;
            const int pickW = S(76);
            const int editOnlyW = std::max(S(80), embeddedEditW - pickW - S(8));
            MoveWindow(st->machCodeLabel, embeddedLeftX, y, embeddedLabelW, embeddedEditH, TRUE);
            MoveWindow(st->machCode, embeddedLeftX + embeddedLabelW + S(10), y, editOnlyW, embeddedEditH, TRUE);
            MoveWindow(st->pickMachine, embeddedLeftX + embeddedLabelW + S(10) + editOnlyW + S(8),
                       y - S(1), pickW, embeddedEditH + S(2), TRUE);
        }
        placeEmbedded(embeddedLeftX, 3, st->machNameLabel, st->machName);
        placeEmbedded(embeddedLeftX, 4, st->sampleNoLabel, st->sampleNo);
        placeEmbedded(embeddedLeftX, 5, st->qcNameLabel, st->qcName);
        placeEmbedded(embeddedLeftX, 6, st->levelLabel, st->level);
        {
            const int y = innerPad + 7 * embeddedRowH;
            const int buttonW = S(88);
            const int buttonGap = S(8);
            const int dateW = std::max(S(110), embeddedEditW - buttonW - buttonGap);
            MoveWindow(st->readDateLabel, embeddedLeftX, y, embeddedLabelW, embeddedEditH, TRUE);
            MoveWindow(st->readDate, embeddedLeftX + embeddedLabelW + S(10), y, dateW, embeddedEditH, TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_READ_ITEMS),
                       embeddedLeftX + embeddedLabelW + S(10) + dateW + buttonGap,
                       y - S(1), buttonW, embeddedEditH + S(2), TRUE);
        }
        placeEmbedded(embeddedRightX, 0, st->itemCodeLabel, st->itemCode);
        placeEmbedded(embeddedRightX, 1, st->itemNameLabel, st->itemName);
        placeEmbedded(embeddedRightX, 2, st->lotNoLabel, st->lotNo);
        placeEmbedded(embeddedRightX, 3, st->validFromLabel, st->validFrom);
        placeEmbedded(embeddedRightX, 4, st->validToLabel, st->validTo);
        placeEmbedded(embeddedRightX, 5, st->targetMeanLabel, st->targetMean);
        placeEmbedded(embeddedRightX, 6, st->targetSdLabel, st->targetSd);
        placeEmbedded(embeddedRightX, 7, st->lotNoteLabel, st->lotNote);
        const int btnY = innerPad + 9 * embeddedRowH;
        MoveWindow(GetDlgItem(hwnd, IDC_NEW), embeddedFormX, btnY, S(76), S(30), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_SAVE), embeddedFormX + S(86), btnY, S(76), S(30), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_DELETE), embeddedFormX + S(172), btnY, S(76), S(30), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_DELETE_LOT), embeddedFormX + S(258), btnY, S(90), S(30), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_CLOSE), embeddedFormX + S(356), btnY, S(76), S(30), TRUE);
        ShowWindow(GetDlgItem(hwnd, IDC_CLOSE), SW_HIDE);
        return;
    }
    MoveWindow(st->title, pad, S(18), std::max(S(300), clientW - pad * 2), S(30), TRUE);
    MoveWindow(st->subtitle, pad, S(50), std::max(S(300), clientW - pad * 2), S(22), TRUE);
    MoveWindow(st->listTitle, listCard.left + cardPadX, listCard.top + S(16), listW - cardPadX * 2, S(22), TRUE);
    MoveWindow(st->listHint, listCard.left + cardPadX, listCard.top + S(42), listW - cardPadX * 2, S(22), TRUE);
    MoveWindow(st->list, listCard.left + cardPadX, listCard.top + S(74),
               listW - cardPadX * 2, listCard.bottom - listCard.top - S(92), TRUE);
    MoveWindow(st->formTitle, formCard.left + cardPadX, formCard.top + S(16), formW - cardPadX * 2, S(22), TRUE);
    MoveWindow(st->formHint, formCard.left + cardPadX, formCard.top + S(42), formW - cardPadX * 2, S(22), TRUE);
    const int formColGap = S(18);
    const int formColW = std::max(S(220), (formInnerW - formColGap) / 2);
    const int formLeftX = formX;
    const int formRightX = formX + formColW + formColGap;
    const int formEditW = std::max(S(110), formColW - labelW - S(10));
    const auto place = [&](int colX, int row, HWND labelHwnd, HWND ctrl) {
        const int y = formCard.top + S(74) + row * rowH;
        MoveWindow(labelHwnd, colX, y, labelW, editH, TRUE);
        MoveWindow(ctrl, colX + labelW + S(10), y, formEditW, editH, TRUE);
    };
    MoveWindow(st->enabled, formLeftX + labelW + S(10), formCard.top + S(74), S(90), editH, TRUE);
    place(formLeftX, 1, st->roomCodeLabel, st->roomCode);
    {
        const int y = formCard.top + S(74) + 2 * rowH;
        const int pickW = S(76);
        const int editOnlyW = std::max(S(80), formEditW - pickW - S(8));
        MoveWindow(st->machCodeLabel, formLeftX, y, labelW, editH, TRUE);
        MoveWindow(st->machCode, formLeftX + labelW + S(10), y, editOnlyW, editH, TRUE);
        MoveWindow(st->pickMachine, formLeftX + labelW + S(10) + editOnlyW + S(8), y - S(1), pickW, editH + S(2), TRUE);
    }
    place(formLeftX, 3, st->machNameLabel, st->machName);
    place(formLeftX, 4, st->sampleNoLabel, st->sampleNo);
    place(formLeftX, 5, st->qcNameLabel, st->qcName);
    place(formLeftX, 6, st->levelLabel, st->level);
    {
        const int y = formCard.top + S(74) + 7 * rowH;
        const int buttonW = S(88);
        const int buttonGap = S(8);
        const int dateW = std::max(S(110), formEditW - buttonW - buttonGap);
        MoveWindow(st->readDateLabel, formLeftX, y, labelW, editH, TRUE);
        MoveWindow(st->readDate, formLeftX + labelW + S(10), y, dateW, editH, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_READ_ITEMS),
                   formLeftX + labelW + S(10) + dateW + buttonGap,
                   y - S(1), buttonW, editH + S(2), TRUE);
    }
    place(formRightX, 0, st->itemCodeLabel, st->itemCode);
    place(formRightX, 1, st->itemNameLabel, st->itemName);
    place(formRightX, 2, st->lotNoLabel, st->lotNo);
    place(formRightX, 3, st->validFromLabel, st->validFrom);
    place(formRightX, 4, st->validToLabel, st->validTo);
    place(formRightX, 5, st->targetMeanLabel, st->targetMean);
    place(formRightX, 6, st->targetSdLabel, st->targetSd);
    place(formRightX, 7, st->lotNoteLabel, st->lotNote);
    const int btnY = formCard.bottom - S(52);
    MoveWindow(GetDlgItem(hwnd, IDC_NEW), formX, btnY, S(76), S(30), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_SAVE), formX + S(86), btnY, S(76), S(30), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_DELETE), formX + S(172), btnY, S(76), S(30), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_DELETE_LOT), formX + S(258), btnY, S(90), S(30), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_CLOSE), formCard.right - cardPadX - S(76), btnY, S(76), S(30), TRUE);
}

LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<State*>(GetPropW(hwnd, PROP_STATE));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            st = reinterpret_cast<State*>(cs->lpCreateParams);
            SetPropW(hwnd, PROP_STATE, reinterpret_cast<HANDLE>(st));
            st->titleFont = createDerivedFont(st->font, 5, FW_SEMIBOLD);
            st->sectionFont = createDerivedFont(st->font, 1, FW_SEMIBOLD);
            st->pageBrush = CreateSolidBrush(COLOR_PAGE_BG);
            st->cardBrush = CreateSolidBrush(COLOR_CARD_BG);
            st->editBrush = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
            st->title = text(hwnd, IDC_TITLE, L"质控品设置");
            st->subtitle = text(hwnd, IDC_SUBTITLE, L"维护仪器固定质控样本号、项目水平，以及质控批号、有效期、靶值和 SD。");
            st->listTitle = text(hwnd, IDC_LIST_TITLE, L"质控配置");
            st->listHint = text(hwnd, IDC_LIST_HINT, L"左侧显示本机已维护的质控规则。");
            st->formTitle = text(hwnd, IDC_FORM_TITLE, L"配置明细");
            st->formHint = text(hwnd, IDC_FORM_HINT, L"可手工录入，也可从 LIS 检验仪器列表选择。");
            st->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                       WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                       0, 0, 0, 0, hwnd, win32_control_id(IDC_LIST), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(st->list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            addColumn(st->list, 0, L"启用", 54);
            addColumn(st->list, 1, L"仪器", 90);
            addColumn(st->list, 2, L"样本号", 90);
            addColumn(st->list, 3, L"质控名称", 140);
            addColumn(st->list, 4, L"水平", 70);
            addColumn(st->list, 5, L"项目代码", 90);
            addColumn(st->list, 6, L"项目名称", 150);
            addColumn(st->list, 7, L"批号", 100);
            addColumn(st->list, 8, L"开始", 90);
            addColumn(st->list, 9, L"结束", 90);
            st->enabled = CreateWindowExW(0, L"BUTTON", L"启用", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                          0, 0, 0, 0, hwnd, win32_control_id(IDC_ENABLED), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(st->enabled, BM_SETCHECK, BST_CHECKED, 0);
            st->roomCodeLabel = label(hwnd, L"科室代码");
            st->roomCode = edit(hwnd, IDC_ROOM_CODE);
            st->machCodeLabel = label(hwnd, L"仪器代码");
            st->machCode = edit(hwnd, IDC_MACH_CODE);
            st->pickMachine = button(hwnd, IDC_PICK_MACHINE, L"选择...");
            st->machNameLabel = label(hwnd, L"仪器名称");
            st->machName = edit(hwnd, IDC_MACH_NAME);
            st->sampleNoLabel = label(hwnd, L"样本号");
            st->sampleNo = edit(hwnd, IDC_SAMPLE_NO);
            st->qcNameLabel = label(hwnd, L"质控名称");
            st->qcName = edit(hwnd, IDC_QC_NAME);
            st->levelLabel = label(hwnd, L"水平");
            st->level = edit(hwnd, IDC_LEVEL);
            st->itemCodeLabel = label(hwnd, L"项目代码");
            st->itemCode = edit(hwnd, IDC_ITEM_CODE);
            st->itemNameLabel = label(hwnd, L"项目名称");
            st->itemName = edit(hwnd, IDC_ITEM_NAME);
            st->readDateLabel = label(hwnd, L"读取日期");
            st->readDate = datePicker(hwnd, IDC_READ_DATE, false);
            setToday(st->readDate);
            st->lotNoLabel = label(hwnd, L"质控批号");
            st->lotNo = edit(hwnd, IDC_LOT_NO);
            st->validFromLabel = label(hwnd, L"开始日期");
            st->validFrom = datePicker(hwnd, IDC_VALID_FROM, true);
            st->validToLabel = label(hwnd, L"结束日期");
            st->validTo = datePicker(hwnd, IDC_VALID_TO, true);
            st->targetMeanLabel = label(hwnd, L"靶值");
            st->targetMean = edit(hwnd, IDC_TARGET_MEAN);
            st->targetSdLabel = label(hwnd, L"SD");
            st->targetSd = edit(hwnd, IDC_TARGET_SD);
            st->lotNoteLabel = label(hwnd, L"批号备注");
            st->lotNote = edit(hwnd, IDC_LOT_NOTE);
            button(hwnd, IDC_NEW, L"新增");
            button(hwnd, IDC_SAVE, L"保存");
            button(hwnd, IDC_DELETE, L"删除");
            button(hwnd, IDC_DELETE_LOT, L"删批号");
            button(hwnd, IDC_READ_ITEMS, L"读取项目");
            button(hwnd, IDC_CLOSE, L"关闭");
            search::apply_font_to_children(hwnd, st->font);
            SendMessageW(st->title, WM_SETFONT, reinterpret_cast<WPARAM>(st->titleFont), TRUE);
            SendMessageW(st->listTitle, WM_SETFONT, reinterpret_cast<WPARAM>(st->sectionFont), TRUE);
            SendMessageW(st->formTitle, WM_SETFONT, reinterpret_cast<WPARAM>(st->sectionFont), TRUE);
            populateList(st);
            layout(hwnd, st);
            return 0;
        }
        case WM_SIZE:
            layout(hwnd, st);
            return 0;
        case WM_ERASEBKGND: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc,
                     st && st->embedded
                         ? (st->cardBrush ? st->cardBrush : GetSysColorBrush(COLOR_WINDOW))
                         : (st && st->pageBrush ? st->pageBrush : GetSysColorBrush(COLOR_BTNFACE)));
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(dc, &rc, st && st->embedded
                              ? (st->cardBrush ? st->cardBrush : GetSysColorBrush(COLOR_WINDOW))
                              : (st && st->pageBrush ? st->pageBrush : GetSysColorBrush(COLOR_BTNFACE)));
            if (st && st->embedded) {
                EndPaint(hwnd, &ps);
                return 0;
            }
            const float scale = search::dpi_scale_factor(hwnd);
            auto S = [scale](int v) { return static_cast<int>(v * scale); };
            const int pad = S(24);
            const int top = S(86);
            const int gap = S(18);
            const int clientW = std::max(0, static_cast<int>(rc.right - rc.left));
            const int contentW = std::max(S(760), clientW - pad * 2);
            const int listW = std::max(S(400), std::min(S(580), (contentW - gap) * 3 / 5));
            const int formW = std::max(S(360), contentW - listW - gap);
            const RECT listCard{pad, top, pad + listW, std::max(top + S(360), static_cast<int>(rc.bottom) - S(24))};
            const RECT formCard{listCard.right + gap, top, listCard.right + gap + formW, listCard.bottom};
            drawRoundRect(dc, listCard, S(8), COLOR_CARD_BG, COLOR_CARD_BORDER);
            drawRoundRect(dc, formCard, S(8), COLOR_CARD_BG, COLOR_CARD_BORDER);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lp));
            SetBkMode(dc, TRANSPARENT);
            if (id == IDC_TITLE) {
                SetTextColor(dc, COLOR_TEXT);
            } else if (id == IDC_LIST_TITLE || id == IDC_FORM_TITLE) {
                SetTextColor(dc, COLOR_ACCENT);
            } else if (id == IDC_SUBTITLE || id == IDC_LIST_HINT || id == IDC_FORM_HINT) {
                SetTextColor(dc, COLOR_MUTED_TEXT);
            } else {
                SetTextColor(dc, COLOR_TEXT);
            }
            const bool pageText = id == IDC_TITLE || id == IDC_SUBTITLE;
            return reinterpret_cast<LRESULT>(pageText && st && st->pageBrush
                                                 ? st->pageBrush
                                                 : (st && st->cardBrush ? st->cardBrush : GetSysColorBrush(COLOR_WINDOW)));
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, RGB(0xFF, 0xFF, 0xFF));
            SetTextColor(dc, COLOR_TEXT);
            return reinterpret_cast<LRESULT>(st && st->editBrush ? st->editBrush : GetStockObject(WHITE_BRUSH));
        }
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (st && nm->idFrom == IDC_LIST && nm->code == NM_CUSTOMDRAW) {
                auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    const int row = static_cast<int>(cd->nmcd.dwItemSpec);
                    if (ListView_GetItemState(st->list, row, LVIS_SELECTED) & LVIS_SELECTED) {
                        cd->nmcd.uItemState &= ~CDIS_SELECTED;
                        cd->clrText = GetSysColor(COLOR_HIGHLIGHTTEXT);
                        cd->clrTextBk = GetSysColor(COLOR_HIGHLIGHT);
                        return CDRF_NOTIFYSUBITEMDRAW | CDRF_NEWFONT;
                    }
                    return CDRF_DODEFAULT;
                }
                if (cd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                    const int row = static_cast<int>(cd->nmcd.dwItemSpec);
                    if (ListView_GetItemState(st->list, row, LVIS_SELECTED) & LVIS_SELECTED) {
                        cd->nmcd.uItemState &= ~CDIS_SELECTED;
                        cd->clrText = GetSysColor(COLOR_HIGHLIGHTTEXT);
                        cd->clrTextBk = GetSysColor(COLOR_HIGHLIGHT);
                        return CDRF_NEWFONT;
                    }
                    return CDRF_DODEFAULT;
                }
            }
            if (st && nm->idFrom == IDC_LIST && nm->code == LVN_ITEMCHANGED) {
                auto* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED)) {
                    loadForm(st, lv->iItem);
                }
            }
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_CLOSE) {
                if (st && st->embedded) return 0;
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wp) == IDC_NEW) {
                clearForm(st);
                return 0;
            }
            if (LOWORD(wp) == IDC_PICK_MACHINE) {
                showMachinePicker(hwnd, st);
                return 0;
            }
            if (LOWORD(wp) == IDC_READ_ITEMS) {
                readItemsFromLis(hwnd, st);
                return 0;
            }
            if (LOWORD(wp) == IDC_SAVE) {
                qc::Config row;
                if (!collectForm(st, row)) return 0;
                const int originalId = row.id;
                qc::Lot lot;
                bool hasLot = false;
                if (!collectLotForm(st, row.id, lot, hasLot)) return 0;
                std::string error;
                if (!qc::save_config(row, error)) {
                    MessageBoxW(hwnd, search::utf8_to_wide(error).c_str(), L"质控品设置", MB_ICONERROR);
                    return 0;
                }
                lot.config_id = row.id;
                if (hasLot && !qc::save_lot(lot, error)) {
                    MessageBoxW(hwnd, search::utf8_to_wide(error).c_str(), L"质控品设置", MB_ICONERROR);
                    return 0;
                }
                populateList(st);
                int savedIndex = findRowIndexById(st, row.id > 0 ? row.id : originalId);
                if (savedIndex < 0) savedIndex = findRowIndex(st, row);
                if (savedIndex >= 0) {
                    selectListRow(st, savedIndex);
                } else {
                    clearForm(st);
                }
                return 0;
            }
            if (LOWORD(wp) == IDC_DELETE) {
                if (st->currentId <= 0) return 0;
                if (MessageBoxW(hwnd, L"确定删除当前质控品配置？", L"质控品设置", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
                std::string error;
                if (!qc::delete_config(st->currentId, error)) {
                    MessageBoxW(hwnd, search::utf8_to_wide(error).c_str(), L"质控品设置", MB_ICONERROR);
                    return 0;
                }
                populateList(st);
                clearForm(st);
                return 0;
            }
            if (LOWORD(wp) == IDC_DELETE_LOT) {
                if (st->currentLotId <= 0) return 0;
                if (MessageBoxW(hwnd, L"确定删除当前质控批号记录？", L"质控品设置", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
                std::string error;
                if (!qc::delete_lot(st->currentLotId, error)) {
                    MessageBoxW(hwnd, search::utf8_to_wide(error).c_str(), L"质控品设置", MB_ICONERROR);
                    return 0;
                }
                loadLatestLot(st);
                populateList(st);
                return 0;
            }
            break;
        case WM_DESTROY:
            if (st) {
                if (st->titleFont) DeleteObject(st->titleFont);
                if (st->sectionFont) DeleteObject(st->sectionFont);
                if (st->pageBrush) DeleteObject(st->pageBrush);
                if (st->cardBrush) DeleteObject(st->cardBrush);
                if (st->editBrush) DeleteObject(st->editBrush);
                RemovePropW(hwnd, PROP_STATE);
                delete st;
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void registerClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = WND_CLASS;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_APP));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassExW(&wc);
    registered = true;
}

}  // namespace

HWND create_quality_control_settings_panel(HWND parent, HFONT font, const search::DbSettings& db_settings) {
    registerClass();
    auto* st = new State;
    st->font = font;
    st->dbSettings = db_settings;
    st->embedded = true;
    HWND hwnd = CreateWindowExW(0, WND_CLASS, L"",
                                WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                0, 0, 10, 10,
                                parent, nullptr, GetModuleHandleW(nullptr), st);
    if (!hwnd) delete st;
    return hwnd;
}

void update_quality_control_settings_panel_db(HWND panel, const search::DbSettings& db_settings) {
    auto* st = panel ? reinterpret_cast<State*>(GetPropW(panel, PROP_STATE)) : nullptr;
    if (st) st->dbSettings = db_settings;
}

#endif
