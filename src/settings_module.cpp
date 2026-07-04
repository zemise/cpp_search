#include "settings_module.h"

#ifdef _WIN32

#include "app_settings_io.h"
#include "barcode_label_printing.h"
#include "main_app.h"
#include "quality_control_settings_dialog.h"
#include "resource.h"
#include "search_controller.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "quick_machine_keys.h"
#include "update_config.h"
#include "win32_control_id.h"
#include <commctrl.h>
#include <windows.h>
#include <winspool.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace {

constexpr int IDC_SET_SERVER   = 5101;
constexpr int IDC_SET_USER     = 5103;
constexpr int IDC_SET_PASSWORD = 5104;
constexpr int IDC_SET_TEST     = 5106;
constexpr int IDC_SET_SAVE     = 5107;
constexpr int IDC_SET_CANCEL   = 5108;
constexpr int IDC_SET_INITIAL_DATABASE = 5109;
constexpr int IDC_SET_FONT_SIZE = 5110;
constexpr int IDC_SET_LIS_ABO_CODES = 5111;
constexpr int IDC_SET_LIS_RHD_CODES = 5112;
constexpr int IDC_SET_LIS_HGB_CODES = 5113;
constexpr int IDC_SET_LIS_PLT_CODES = 5114;
constexpr int IDC_SET_BARCODE_PRINTER = 5115;
constexpr int IDC_SET_QUICK_MACHINE_1 = 5116;
constexpr int IDC_SET_QUICK_MACHINE_2 = 5117;
constexpr int IDC_SET_QUICK_MACHINE_3 = 5118;
constexpr int IDC_SET_QUICK_MACHINE_PICK_1 = 5119;
constexpr int IDC_SET_QUICK_MACHINE_PICK_2 = 5120;
constexpr int IDC_SET_QUICK_MACHINE_PICK_3 = 5121;
constexpr int IDC_PICKER_ROOM = 5122;
constexpr int IDC_PICKER_MACHINE = 5123;
constexpr int IDC_SET_UPDATE_SOURCE = 5124;
constexpr int IDC_SET_UPDATE_MANIFEST_URL = 5125;
constexpr int IDC_SET_UPDATE_FOLDER = 5126;
constexpr int IDC_SET_UPDATE_AUTO_CHECK = 5128;
constexpr int IDC_SET_UPDATE_MANIFEST_LABEL = 5129;
constexpr int IDC_SET_UPDATE_FOLDER_LABEL = 5130;
constexpr int IDC_SET_LIS_DIRECT_ANTIGLOBULIN_CODES = 5131;
constexpr int IDC_SET_LIS_IRREGULAR_ANTIBODY_CODES = 5132;
constexpr int IDC_SET_ZEBRA_CHINESE_FONT = 5133;
constexpr int IDC_LABEL_SERVER = 5201;
constexpr int IDC_LABEL_INITIAL_DATABASE = 5202;
constexpr int IDC_LABEL_USER = 5203;
constexpr int IDC_LABEL_PASSWORD = 5204;
constexpr int IDC_LABEL_FONT_SIZE = 5205;
constexpr int IDC_LABEL_LIS_ABO_CODES = 5206;
constexpr int IDC_LABEL_LIS_RHD_CODES = 5207;
constexpr int IDC_LABEL_LIS_HGB_CODES = 5208;
constexpr int IDC_LABEL_LIS_PLT_CODES = 5209;
constexpr int IDC_LABEL_BARCODE_PRINTER = 5210;
constexpr int IDC_LABEL_QUICK_MACHINE_1 = 5211;
constexpr int IDC_LABEL_QUICK_MACHINE_2 = 5212;
constexpr int IDC_LABEL_QUICK_MACHINE_3 = 5213;
constexpr int IDC_LABEL_UPDATE_SOURCE = 5214;
constexpr int IDC_TEXT_LIS_HINT = 5215;
constexpr int IDC_TEXT_PAGE_TITLE = 5216;
constexpr int IDC_TEXT_PAGE_SUBTITLE = 5217;
constexpr int IDC_TEXT_SECTION_DATABASE = 5218;
constexpr int IDC_TEXT_SECTION_LIS = 5219;
constexpr int IDC_TEXT_SECTION_REPORT = 5220;
constexpr int IDC_TEXT_SECTION_UPDATE = 5221;
constexpr int IDC_TEXT_DATABASE_HINT = 5222;
constexpr int IDC_TEXT_REPORT_HINT = 5223;
constexpr int IDC_TEXT_UPDATE_HINT = 5224;
constexpr int IDC_TEXT_DATABASE_DISPLAY = 5225;
constexpr int IDC_TEXT_REPORT_PRINTER = 5226;
constexpr int IDC_TEXT_REPORT_QUICK = 5227;
constexpr int IDC_TEXT_SAVE_STATUS = 5228;
constexpr int IDC_SET_LIS_BLOOD_TYPE_MACHINES = 5229;
constexpr int IDC_SET_LIS_CBC_MACHINES = 5230;
constexpr int IDC_LABEL_LIS_BLOOD_TYPE_MACHINES = 5231;
constexpr int IDC_LABEL_LIS_CBC_MACHINES = 5232;
constexpr int IDC_SET_LIS_BLOOD_EXCLUDE_MACHINES = 5233;
constexpr int IDC_LABEL_LIS_BLOOD_EXCLUDE_MACHINES = 5234;
constexpr int IDC_TEXT_SECTION_QUALITY_CONTROL = 5235;
constexpr int IDC_TEXT_QUALITY_CONTROL_HINT = 5236;
constexpr int IDC_SETTINGS_TABS = 5237;
constexpr int IDC_LABEL_LIS_DIRECT_ANTIGLOBULIN_CODES = 5238;
constexpr int IDC_LABEL_LIS_IRREGULAR_ANTIBODY_CODES = 5239;
constexpr int IDC_LABEL_ZEBRA_CHINESE_FONT = 5240;

constexpr const wchar_t* WND_CLASS  = L"SettingsModuleChild";
constexpr const wchar_t* PICKER_CLASS = L"SettingsMachinePicker";
constexpr const wchar_t* WHITE_HOST_CLASS = L"SettingsWhiteHost";
constexpr const wchar_t* PROP_STATE = L"SettingsSt";
constexpr const wchar_t* WINDOW_TITLE = L"系统设置";
constexpr int PICKER_W = 640;
constexpr int PICKER_H = 360;
constexpr int PICKER_PAD = 12;
constexpr int PICKER_COMBO_H = 180;
constexpr int PICKER_LIST_Y = 48;
constexpr int PICKER_CODE_W = 70;
constexpr int PICKER_NAME_W = 170;
constexpr int PICKER_GROUP_CODE_W = 78;
constexpr int PICKER_GROUP_NAME_W = 150;
constexpr int PICKER_SAMPLE_W = 64;
constexpr int ALLOWED_FONT_SIZES[] = {9, 11, 12, 13};
constexpr COLORREF COLOR_PAGE_BG = RGB(0xF3, 0xF6, 0xFA);
constexpr COLORREF COLOR_CARD_BG = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF COLOR_CARD_BORDER = RGB(0xD8, 0xE0, 0xEA);
constexpr COLORREF COLOR_TEXT = RGB(0x1F, 0x29, 0x37);
constexpr COLORREF COLOR_MUTED_TEXT = RGB(0x6B, 0x72, 0x80);
constexpr COLORREF COLOR_ACCENT = RGB(0x25, 0x63, 0xEB);
constexpr COLORREF COLOR_SUCCESS = RGB(0x16, 0x7A, 0x3A);

struct SettingsState {
    ModuleContext ctx;
    search::AppSettings app;
    HFONT titleFont = nullptr;
    HFONT sectionFont = nullptr;
    HBRUSH pageBrush = nullptr;
    HBRUSH editBrush = nullptr;
    HWND updateAutoCheckHost = nullptr;
    HWND updateAutoCheck = nullptr;
    HWND qualityControlPanel = nullptr;
    HWND tabs = nullptr;
    int currentTab = 0;
    std::array<std::string, QUICK_MACHINE_COUNT> quickMachineCodes;
    std::array<std::string, QUICK_MACHINE_COUNT> quickMachineRoomCodes;
    std::array<std::wstring, QUICK_MACHINE_COUNT> quickMachineNames;
};

struct SettingsMachinePickerState {
    SettingsState* settings = nullptr;
    HWND owner = nullptr;
    HWND roomCombo = nullptr;
    HWND machineList = nullptr;
    int slot = 0;
    std::vector<search::RoomOption> rooms;
    std::vector<search::MachineOption> machines;
};

struct SettingsUpdateConfig {
    std::wstring sourceType;
    std::wstring manifestUrl;
    std::wstring folderPath;
};

LRESULT CALLBACK whiteHostProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc, GetSysColorBrush(COLOR_WINDOW));
            return 1;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, GetSysColor(COLOR_WINDOW));
            SetTextColor(dc, COLOR_TEXT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_COMMAND:
            if (HWND parent = GetParent(hwnd)) {
                return SendMessageW(parent, msg, wp, lp);
            }
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int quickMachineEditId(int slot) {
    return slot == 0 ? IDC_SET_QUICK_MACHINE_1 : (slot == 1 ? IDC_SET_QUICK_MACHINE_2 : IDC_SET_QUICK_MACHINE_3);
}

HFONT createDerivedFont(HFONT base, int pointDelta, LONG weight) {
    LOGFONTW lf{};
    if (base && GetObjectW(base, sizeof(lf), &lf) == sizeof(lf)) {
        // Use the active app face and adjust only size/weight for local hierarchy.
    } else {
        NONCLIENTMETRICSW nm{};
        nm.cbSize = sizeof(nm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(nm), &nm, 0);
        lf = nm.lfMessageFont;
    }
    HDC screen = GetDC(nullptr);
    const int dpi = screen ? GetDeviceCaps(screen, LOGPIXELSY) : 96;
    const int currentPoint = std::max(8, MulDiv(std::abs(lf.lfHeight), 72, dpi));
    lf.lfHeight = -MulDiv(std::max(8, currentPoint + pointDelta), dpi, 72);
    lf.lfWeight = weight;
    if (screen) ReleaseDC(nullptr, screen);
    return CreateFontIndirectW(&lf);
}

HWND createSettingLabel(HWND parent, int id, const wchar_t* text) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
                           0, 0, 10, 10, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

HWND createText(HWND parent, int id, const wchar_t* text) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                           0, 0, 10, 10, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
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

struct SettingsLayout {
    RECT tabs{};
    RECT content{};
    RECT database{};
    RECT lis{};
    RECT report{};
    RECT update{};
    RECT qualityControl{};
    int labelW = 0;
    int gap = 0;
    int editH = 0;
    int rowGap = 0;
};

SettingsLayout calculateLayout(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const float s = search::dpi_scale_factor(hwnd);
    auto S = [s](int v) { return static_cast<int>(v * s); };

    const int margin = S(24);
    const int top = S(86);
    const int bottom = std::max(top + S(360), static_cast<int>(rc.bottom) - S(76));
    const int left = margin;
    const int right = std::max(left + S(720), static_cast<int>(rc.right) - margin);

    SettingsLayout l{};
    l.labelW = S(96);
    l.gap = S(10);
    l.editH = S(26);
    l.rowGap = S(34);
    l.tabs = RECT{left, top, right, top + S(34)};
    l.content = RECT{left, l.tabs.bottom + S(10), right, bottom};
    l.database = l.content;
    l.report = l.content;
    l.lis = l.content;
    l.update = l.content;
    l.qualityControl = l.content;
    return l;
}

void layoutSettingsWindow(HWND hwnd);
void updateSourceFieldVisibility(HWND hwnd);

void moveChild(HWND hwnd, int id, int x, int y, int w, int h) {
    HWND child = GetDlgItem(hwnd, id);
    if (child) MoveWindow(child, x, y, w, h, TRUE);
}

void showChild(HWND hwnd, int id, bool show) {
    if (HWND child = GetDlgItem(hwnd, id)) ShowWindow(child, show ? SW_SHOW : SW_HIDE);
}

void updateSettingsPageVisibility(HWND hwnd, SettingsState* st) {
    const int tab = st ? st->currentTab : 0;
    const bool database = tab == 0;
    const bool lis = tab == 1;
    const bool report = tab == 2;
    const bool qc = tab == 3;
    const bool update = tab == 4;

    const int databaseIds[] = {IDC_TEXT_SECTION_DATABASE, IDC_TEXT_DATABASE_HINT, IDC_TEXT_DATABASE_DISPLAY,
                               IDC_LABEL_SERVER, IDC_SET_SERVER, IDC_LABEL_INITIAL_DATABASE, IDC_SET_INITIAL_DATABASE,
                               IDC_LABEL_USER, IDC_SET_USER, IDC_LABEL_PASSWORD, IDC_SET_PASSWORD,
                               IDC_LABEL_FONT_SIZE, IDC_SET_FONT_SIZE, IDC_SET_TEST};
    for (int id : databaseIds) showChild(hwnd, id, database);

    const int lisIds[] = {IDC_TEXT_SECTION_LIS, IDC_TEXT_LIS_HINT, IDC_LABEL_LIS_ABO_CODES, IDC_SET_LIS_ABO_CODES,
                          IDC_LABEL_LIS_RHD_CODES, IDC_SET_LIS_RHD_CODES, IDC_LABEL_LIS_HGB_CODES, IDC_SET_LIS_HGB_CODES,
                          IDC_LABEL_LIS_PLT_CODES, IDC_SET_LIS_PLT_CODES, IDC_LABEL_LIS_IRREGULAR_ANTIBODY_CODES,
                          IDC_SET_LIS_IRREGULAR_ANTIBODY_CODES, IDC_LABEL_LIS_DIRECT_ANTIGLOBULIN_CODES,
                          IDC_SET_LIS_DIRECT_ANTIGLOBULIN_CODES, IDC_LABEL_LIS_BLOOD_TYPE_MACHINES,
                          IDC_SET_LIS_BLOOD_TYPE_MACHINES, IDC_LABEL_LIS_CBC_MACHINES, IDC_SET_LIS_CBC_MACHINES,
                          IDC_LABEL_LIS_BLOOD_EXCLUDE_MACHINES, IDC_SET_LIS_BLOOD_EXCLUDE_MACHINES};
    for (int id : lisIds) showChild(hwnd, id, lis);

    const int reportIds[] = {IDC_TEXT_SECTION_REPORT, IDC_TEXT_REPORT_HINT, IDC_TEXT_REPORT_PRINTER, IDC_TEXT_REPORT_QUICK,
                             IDC_LABEL_BARCODE_PRINTER, IDC_SET_BARCODE_PRINTER, IDC_LABEL_ZEBRA_CHINESE_FONT,
                             IDC_SET_ZEBRA_CHINESE_FONT, IDC_LABEL_QUICK_MACHINE_1, IDC_SET_QUICK_MACHINE_1,
                             IDC_SET_QUICK_MACHINE_PICK_1, IDC_LABEL_QUICK_MACHINE_2, IDC_SET_QUICK_MACHINE_2,
                             IDC_SET_QUICK_MACHINE_PICK_2, IDC_LABEL_QUICK_MACHINE_3, IDC_SET_QUICK_MACHINE_3,
                             IDC_SET_QUICK_MACHINE_PICK_3};
    for (int id : reportIds) showChild(hwnd, id, report);

    const int qcIds[] = {IDC_TEXT_SECTION_QUALITY_CONTROL, IDC_TEXT_QUALITY_CONTROL_HINT};
    for (int id : qcIds) showChild(hwnd, id, qc);
    if (st && st->qualityControlPanel) ShowWindow(st->qualityControlPanel, qc ? SW_SHOW : SW_HIDE);

    const int updateIds[] = {IDC_TEXT_SECTION_UPDATE, IDC_TEXT_UPDATE_HINT, IDC_LABEL_UPDATE_SOURCE, IDC_SET_UPDATE_SOURCE,
                             IDC_SET_UPDATE_MANIFEST_LABEL, IDC_SET_UPDATE_MANIFEST_URL, IDC_SET_UPDATE_FOLDER_LABEL,
                             IDC_SET_UPDATE_FOLDER};
    for (int id : updateIds) showChild(hwnd, id, update);
    if (st && st->updateAutoCheckHost) ShowWindow(st->updateAutoCheckHost, update ? SW_SHOW : SW_HIDE);
    showChild(hwnd, IDC_SET_UPDATE_AUTO_CHECK, update);
}

void layoutSettingsWindow(HWND hwnd) {
    const float s = search::dpi_scale_factor(hwnd);
    auto S = [s](int v) { return static_cast<int>(v * s); };
    RECT rc{};
    GetClientRect(hwnd, &rc);
    auto* st = reinterpret_cast<SettingsState*>(GetPropW(hwnd, PROP_STATE));
    SettingsLayout l = calculateLayout(hwnd);
    const int cardPadX = S(22);
    const int titleY = S(18);
    const int sectionTitleY = S(16);
    const int hintY = S(42);
    const int formY = S(74);
    const int labelW = l.labelW;
    const int controlGap = l.gap;
    const int rowGap = l.rowGap;
    const int editH = l.editH;

    const auto placeSection = [&](int titleId, int hintId, const RECT& card) {
        moveChild(hwnd, titleId, card.left + cardPadX, card.top + sectionTitleY,
                  card.right - card.left - cardPadX * 2, S(22));
        moveChild(hwnd, hintId, card.left + cardPadX, card.top + hintY,
                  card.right - card.left - cardPadX * 2, S(22));
    };
    const auto placeRow = [&](const RECT& card, int row, int labelId, int ctrlId, int ctrlW = -1) {
        const int y = card.top + formY + row * rowGap;
        const int labelX = card.left + cardPadX;
        const int ctrlX = labelX + labelW + controlGap;
        const int maxW = card.right - cardPadX - ctrlX;
        moveChild(hwnd, labelId, labelX, y, labelW, editH);
        moveChild(hwnd, ctrlId, ctrlX, y, ctrlW > 0 ? std::min(ctrlW, maxW) : maxW, editH);
    };

    const int titleW = std::max(S(300), static_cast<int>(rc.right) - S(48));
    moveChild(hwnd, IDC_TEXT_PAGE_TITLE, S(24), titleY, titleW, S(30));
    moveChild(hwnd, IDC_TEXT_PAGE_SUBTITLE, S(24), S(50), titleW, S(22));
    if (st && st->tabs) MoveWindow(st->tabs, l.tabs.left, l.tabs.top, l.tabs.right - l.tabs.left, l.tabs.bottom - l.tabs.top, TRUE);

    placeSection(IDC_TEXT_SECTION_DATABASE, IDC_TEXT_DATABASE_HINT, l.database);
    placeRow(l.database, 0, IDC_LABEL_SERVER, IDC_SET_SERVER);
    placeRow(l.database, 1, IDC_LABEL_INITIAL_DATABASE, IDC_SET_INITIAL_DATABASE);
    placeRow(l.database, 2, IDC_LABEL_USER, IDC_SET_USER);
    placeRow(l.database, 3, IDC_LABEL_PASSWORD, IDC_SET_PASSWORD);
    moveChild(hwnd, IDC_SET_TEST,
              l.database.right - cardPadX - S(96),
              l.database.top + formY + 4 * rowGap, S(96), S(32));
    moveChild(hwnd, IDC_TEXT_DATABASE_DISPLAY,
              l.database.left + cardPadX, l.database.top + formY + 4 * rowGap + S(6),
              S(180), S(22));
    {
        const int y = l.database.top + formY + 5 * rowGap;
        const int labelX = l.database.left + cardPadX;
        const int ctrlX = labelX + labelW + controlGap;
        moveChild(hwnd, IDC_LABEL_FONT_SIZE, labelX, y, labelW, editH);
        moveChild(hwnd, IDC_SET_FONT_SIZE, ctrlX, y, S(132), S(128));
    }

    placeSection(IDC_TEXT_SECTION_LIS, IDC_TEXT_LIS_HINT, l.lis);
    placeRow(l.lis, 0, IDC_LABEL_LIS_ABO_CODES, IDC_SET_LIS_ABO_CODES);
    placeRow(l.lis, 1, IDC_LABEL_LIS_RHD_CODES, IDC_SET_LIS_RHD_CODES);
    placeRow(l.lis, 2, IDC_LABEL_LIS_HGB_CODES, IDC_SET_LIS_HGB_CODES);
    placeRow(l.lis, 3, IDC_LABEL_LIS_PLT_CODES, IDC_SET_LIS_PLT_CODES);
    placeRow(l.lis, 4, IDC_LABEL_LIS_IRREGULAR_ANTIBODY_CODES, IDC_SET_LIS_IRREGULAR_ANTIBODY_CODES);
    placeRow(l.lis, 5, IDC_LABEL_LIS_DIRECT_ANTIGLOBULIN_CODES, IDC_SET_LIS_DIRECT_ANTIGLOBULIN_CODES);
    placeRow(l.lis, 6, IDC_LABEL_LIS_BLOOD_TYPE_MACHINES, IDC_SET_LIS_BLOOD_TYPE_MACHINES);
    placeRow(l.lis, 7, IDC_LABEL_LIS_CBC_MACHINES, IDC_SET_LIS_CBC_MACHINES);
    placeRow(l.lis, 8, IDC_LABEL_LIS_BLOOD_EXCLUDE_MACHINES, IDC_SET_LIS_BLOOD_EXCLUDE_MACHINES);

    placeSection(IDC_TEXT_SECTION_REPORT, IDC_TEXT_REPORT_HINT, l.report);
    moveChild(hwnd, IDC_TEXT_REPORT_PRINTER,
              l.report.left + cardPadX, l.report.top + S(68),
              l.report.right - l.report.left - cardPadX * 2, S(22));
    {
        const int y = l.report.top + S(94);
        const int labelX = l.report.left + cardPadX;
        const int ctrlX = labelX + labelW + controlGap;
        moveChild(hwnd, IDC_LABEL_BARCODE_PRINTER, labelX, y, labelW, editH);
        moveChild(hwnd, IDC_SET_BARCODE_PRINTER, ctrlX, y, l.report.right - cardPadX - ctrlX, editH);
    }
    {
        const int y = l.report.top + S(128);
        const int labelX = l.report.left + cardPadX;
        const int ctrlX = labelX + labelW + controlGap;
        moveChild(hwnd, IDC_LABEL_ZEBRA_CHINESE_FONT, labelX, y, labelW, editH);
        moveChild(hwnd, IDC_SET_ZEBRA_CHINESE_FONT, ctrlX, y, S(180), S(160));
    }
    moveChild(hwnd, IDC_TEXT_REPORT_QUICK,
              l.report.left + cardPadX, l.report.top + S(168),
              l.report.right - l.report.left - cardPadX * 2, S(22));
    for (int i = 0; i < QUICK_MACHINE_COUNT; ++i) {
        const int labelId = i == 0 ? IDC_LABEL_QUICK_MACHINE_1 :
                            (i == 1 ? IDC_LABEL_QUICK_MACHINE_2 : IDC_LABEL_QUICK_MACHINE_3);
        const int pickId = i == 0 ? IDC_SET_QUICK_MACHINE_PICK_1 :
                           (i == 1 ? IDC_SET_QUICK_MACHINE_PICK_2 : IDC_SET_QUICK_MACHINE_PICK_3);
        const int y = l.report.top + S(194) + i * rowGap;
        const int labelX = l.report.left + cardPadX;
        const int ctrlX = labelX + labelW + controlGap;
        const int buttonW = S(38);
        moveChild(hwnd, labelId, labelX, y, labelW, editH);
        moveChild(hwnd, quickMachineEditId(i), ctrlX, y,
                  l.report.right - cardPadX - ctrlX - buttonW - S(8), editH);
        moveChild(hwnd, pickId, l.report.right - cardPadX - buttonW, y - S(1), buttonW, editH + S(2));
    }
    placeSection(IDC_TEXT_SECTION_UPDATE, IDC_TEXT_UPDATE_HINT, l.update);
    placeRow(l.update, 0, IDC_LABEL_UPDATE_SOURCE, IDC_SET_UPDATE_SOURCE, S(160));
    placeRow(l.update, 1, IDC_SET_UPDATE_MANIFEST_LABEL, IDC_SET_UPDATE_MANIFEST_URL);
    placeRow(l.update, 1, IDC_SET_UPDATE_FOLDER_LABEL, IDC_SET_UPDATE_FOLDER);
    if (st && st->updateAutoCheckHost && st->updateAutoCheck) {
        const int checkX = l.update.left + cardPadX + labelW + controlGap;
        const int checkY = l.update.top + formY + 2 * rowGap;
        MoveWindow(st->updateAutoCheckHost, checkX, checkY, S(180), editH, TRUE);
        MoveWindow(st->updateAutoCheck, 0, 0, S(180), editH, TRUE);
    } else {
        moveChild(hwnd, IDC_SET_UPDATE_AUTO_CHECK,
                  l.update.left + cardPadX + labelW + controlGap,
                  l.update.top + formY + 2 * rowGap, S(170), editH);
    }

    placeSection(IDC_TEXT_SECTION_QUALITY_CONTROL, IDC_TEXT_QUALITY_CONTROL_HINT, l.qualityControl);
    if (st && st->qualityControlPanel) {
        MoveWindow(st->qualityControlPanel,
                   l.qualityControl.left + cardPadX,
                   l.qualityControl.top + S(74),
                   l.qualityControl.right - l.qualityControl.left - cardPadX * 2,
                   l.qualityControl.bottom - l.qualityControl.top - S(96),
                   TRUE);
    }

    const int buttonY = std::max(S(96), static_cast<int>(rc.bottom) - S(24) - S(32));
    const int buttonH = S(32);
    const int buttonW = S(96);
    const int buttonGap = S(10);
    const int rightEdge = std::max(S(240), static_cast<int>(rc.right) - S(24));
    const int statusW = S(128);
    moveChild(hwnd, IDC_TEXT_SAVE_STATUS,
              rightEdge - buttonW * 2 - buttonGap - statusW - S(14),
              buttonY + S(6), statusW, S(22));
    moveChild(hwnd, IDC_SET_CANCEL, rightEdge - buttonW * 2 - buttonGap, buttonY, buttonW, buttonH);
    moveChild(hwnd, IDC_SET_SAVE, rightEdge - buttonW, buttonY, buttonW, buttonH);
    updateSettingsPageVisibility(hwnd, st);
    updateSourceFieldVisibility(hwnd);
}

bool isMutedTextId(int id) {
    return id == IDC_TEXT_PAGE_SUBTITLE || id == IDC_TEXT_DATABASE_HINT ||
           id == IDC_TEXT_LIS_HINT || id == IDC_TEXT_REPORT_HINT || id == IDC_TEXT_UPDATE_HINT ||
           id == IDC_TEXT_QUALITY_CONTROL_HINT;
}

bool isSectionTitleId(int id) {
    return id == IDC_TEXT_SECTION_DATABASE || id == IDC_TEXT_SECTION_LIS ||
           id == IDC_TEXT_SECTION_REPORT || id == IDC_TEXT_SECTION_UPDATE ||
           id == IDC_TEXT_SECTION_QUALITY_CONTROL ||
           id == IDC_TEXT_DATABASE_DISPLAY || id == IDC_TEXT_REPORT_PRINTER ||
           id == IDC_TEXT_REPORT_QUICK;
}

bool isReadOnlyEditId(int id) {
    return id == IDC_SET_QUICK_MACHINE_1 || id == IDC_SET_QUICK_MACHINE_2 ||
           id == IDC_SET_QUICK_MACHINE_3;
}

int normalizeSettingsFontSize(int value) {
    int best = ALLOWED_FONT_SIZES[0];
    int bestDist = value > best ? value - best : best - value;
    for (int size : ALLOWED_FONT_SIZES) {
        const int dist = value > size ? value - size : size - value;
        if (dist < bestDist || (dist == bestDist && size > best)) {
            best = size;
            bestDist = dist;
        }
    }
    return best;
}

std::wstring fontSizeLabel(int size) {
    switch (size) {
        case 9: return L"9 - 紧凑";
        case 11: return L"11 - 标准";
        case 12: return L"12 - 舒适";
        case 13: return L"13 - 大字";
        default: return std::to_wstring(size);
    }
}

void populateFontSizeCombo(HWND hwnd, int currentSize) {
    HWND combo = GetDlgItem(hwnd, IDC_SET_FONT_SIZE);
    if (!combo) return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    const int normalized = normalizeSettingsFontSize(currentSize);
    int selected = 0;
    for (int i = 0; i < static_cast<int>(sizeof(ALLOWED_FONT_SIZES) / sizeof(ALLOWED_FONT_SIZES[0])); ++i) {
        const int size = ALLOWED_FONT_SIZES[i];
        const std::wstring label = fontSizeLabel(size);
        const int index = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0,
                                                        reinterpret_cast<LPARAM>(label.c_str())));
        SendMessageW(combo, CB_SETITEMDATA, index, size);
        if (size == normalized) selected = index;
    }
    SendMessageW(combo, CB_SETCURSEL, selected, 0);
}

int selectedFontSize(HWND hwnd) {
    HWND combo = GetDlgItem(hwnd, IDC_SET_FONT_SIZE);
    const int index = combo ? static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0)) : CB_ERR;
    if (index != CB_ERR) {
        const LRESULT data = SendMessageW(combo, CB_GETITEMDATA, index, 0);
        if (data != CB_ERR) {
            return normalizeSettingsFontSize(static_cast<int>(data));
        }
    }
    return normalizeSettingsFontSize(11);
}

void broadcastSettingsChangedToChildren(HWND mdiClient) {
    if (!mdiClient) return;
    HWND child = GetWindow(mdiClient, GW_CHILD);
    while (child) {
        SendMessageW(child, app::WM_APP_SETTINGS_CHANGED, 0, 0);
        child = GetWindow(child, GW_HWNDNEXT);
    }
}

void setSaveStatus(HWND hwnd, const wchar_t* text) {
    HWND status = GetDlgItem(hwnd, IDC_TEXT_SAVE_STATUS);
    if (!status) return;
    SetWindowTextW(status, text ? text : L"");
    ShowWindow(status, text && text[0] ? SW_SHOW : SW_HIDE);
    InvalidateRect(status, nullptr, TRUE);
}

void setSavedStatusNow(HWND hwnd) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t text[64]{};
    swprintf(text, 64, L"已保存 %02u:%02u", now.wHour, now.wMinute);
    setSaveStatus(hwnd, text);
}

std::wstring readEdit(HWND hwnd, int id) {
    HWND ctrl = GetDlgItem(hwnd, id);
    int len = GetWindowTextLengthW(ctrl);
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(ctrl, text.data(), len + 1);
    text.resize(static_cast<size_t>(len));
    return text;
}

std::wstring readCombo(HWND hwnd, int id) {
    HWND combo = GetDlgItem(hwnd, id);
    const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (index >= 0) {
        const int len = static_cast<int>(SendMessageW(combo, CB_GETLBTEXTLEN, index, 0));
        if (len >= 0) {
            std::wstring text(static_cast<size_t>(len) + 1, L'\0');
            SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(text.data()));
            text.resize(static_cast<size_t>(len));
            return text;
        }
    }
    return readEdit(hwnd, id);
}

void selectComboText(HWND combo, const wchar_t* text, int fallback_index = 0) {
    const int found = static_cast<int>(SendMessageW(combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                                                    reinterpret_cast<LPARAM>(text)));
    SendMessageW(combo, CB_SETCURSEL, found >= 0 ? found : fallback_index, 0);
}

void populateUpdateSourceCombo(HWND hwnd) {
    HWND combo = GetDlgItem(hwnd, IDC_SET_UPDATE_SOURCE);
    if (!combo) return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(lis_update::kSourceFolderLabel));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(lis_update::kSourceHttpLabel));

    const std::wstring saved = search::load_module_str(
        lis_update::kConfigSection, L"SourceType", lis_update::kSourceFolder);
    selectComboText(combo,
                    saved == lis_update::kSourceHttp
                        ? lis_update::kSourceHttpLabel
                        : lis_update::kSourceFolderLabel,
                    0);
}

std::wstring selectedUpdateSourceType(HWND hwnd) {
    HWND combo = GetDlgItem(hwnd, IDC_SET_UPDATE_SOURCE);
    const int index = combo ? static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0)) : 0;
    return index == 1 ? lis_update::kSourceHttp : lis_update::kSourceFolder;
}

void updateSourceFieldVisibility(HWND hwnd) {
    auto* st = reinterpret_cast<SettingsState*>(GetPropW(hwnd, PROP_STATE));
    const bool update_page = st ? st->currentTab == 4 : true;
    const bool is_http = selectedUpdateSourceType(hwnd) == lis_update::kSourceHttp;
    ShowWindow(GetDlgItem(hwnd, IDC_SET_UPDATE_MANIFEST_LABEL), update_page && is_http ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hwnd, IDC_SET_UPDATE_MANIFEST_URL), update_page && is_http ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hwnd, IDC_SET_UPDATE_FOLDER_LABEL), update_page && !is_http ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hwnd, IDC_SET_UPDATE_FOLDER), update_page && !is_http ? SW_SHOW : SW_HIDE);
}

SettingsUpdateConfig collectUpdateConfig(HWND hwnd) {
    SettingsUpdateConfig cfg;
    cfg.sourceType = selectedUpdateSourceType(hwnd);
    cfg.manifestUrl = readEdit(hwnd, IDC_SET_UPDATE_MANIFEST_URL);
    cfg.folderPath = readEdit(hwnd, IDC_SET_UPDATE_FOLDER);
    return cfg;
}

void saveUpdateConfig(const SettingsUpdateConfig& cfg) {
    search::save_module_str(lis_update::kConfigSection, L"SourceType", cfg.sourceType);
    search::save_module_str(lis_update::kConfigSection, L"ManifestUrl", cfg.manifestUrl);
    search::save_module_str(lis_update::kConfigSection, L"FolderPath", cfg.folderPath);
}

std::vector<std::wstring> enumPrinterNames() {
    DWORD needed = 0;
    DWORD returned = 0;
    constexpr DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;
    EnumPrintersW(flags, nullptr, 4, nullptr, 0, &needed, &returned);
    if (needed == 0) return {};

    std::vector<BYTE> buffer(needed);
    if (!EnumPrintersW(flags, nullptr, 4, buffer.data(), needed, &needed, &returned)) {
        return {};
    }

    auto* info = reinterpret_cast<PRINTER_INFO_4W*>(buffer.data());
    std::vector<std::wstring> names;
    names.reserve(returned);
    for (DWORD i = 0; i < returned; ++i) {
        if (info[i].pPrinterName && info[i].pPrinterName[0]) {
            names.emplace_back(info[i].pPrinterName);
        }
    }
    return names;
}

void populatePrinterCombo(HWND hwnd) {
    HWND combo = GetDlgItem(hwnd, IDC_SET_BARCODE_PRINTER);
    if (!combo) return;

    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    const std::wstring configured = search::load_module_str(
        L"RegularReport", L"BarcodePrinterName", search::default_barcode_printer_name());
    int selected = -1;
    const auto printers = enumPrinterNames();
    for (const auto& printer : printers) {
        const int index = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0,
                                                        reinterpret_cast<LPARAM>(printer.c_str())));
        if (printer == configured) selected = index;
    }

    if (!configured.empty() && selected < 0) {
        selected = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0,
                                                 reinterpret_cast<LPARAM>(configured.c_str())));
    }
    if (selected >= 0) {
        SendMessageW(combo, CB_SETCURSEL, selected, 0);
    }
}

void populateZebraChineseFontCombo(HWND hwnd) {
    HWND combo = GetDlgItem(hwnd, IDC_SET_ZEBRA_CHINESE_FONT);
    if (!combo) return;

    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(search::zebra_simsun_font()));
    SendMessageW(combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(search::zebra_csong_font()));
    const std::wstring configured = search::configured_zebra_chinese_font();
    selectComboText(combo, configured.c_str(), 0);
}

search::DbSettings collectForm(HWND hwnd) {
    search::DbSettings s;
    s.server = readEdit(hwnd, IDC_SET_SERVER);
    s.initial_database = readEdit(hwnd, IDC_SET_INITIAL_DATABASE);
    s.user = readEdit(hwnd, IDC_SET_USER);
    s.password = readEdit(hwnd, IDC_SET_PASSWORD);
    return s;
}

std::string selectedPickerRoomCode(SettingsMachinePickerState* ps) {
    if (!ps || !ps->roomCombo) return "";
    const int index = static_cast<int>(SendMessageW(ps->roomCombo, CB_GETCURSEL, 0, 0));
    if (index < 0 || index >= static_cast<int>(ps->rooms.size())) return "";
    return ps->rooms[static_cast<size_t>(index)].room_code;
}

void populatePickerMachines(SettingsMachinePickerState* ps) {
    if (!ps || !ps->machineList) return;
    ListView_DeleteAllItems(ps->machineList);
    int selected = -1;
    const std::string current = ps->settings ? ps->settings->quickMachineCodes[static_cast<size_t>(ps->slot)] : "";
    for (int i = 0; i < static_cast<int>(ps->machines.size()); ++i) {
        const auto& machine = ps->machines[static_cast<size_t>(i)];
        const auto code = search::utf8_to_wide(machine.mach_code);
        const auto name = search::utf8_to_wide(machine.mach_name);
        const auto groupCode = search::utf8_to_wide(machine.group_code);
        const auto groupName = search::utf8_to_wide(machine.group_name);
        const auto sample = search::utf8_to_wide(machine.sample_name);
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<wchar_t*>(code.c_str());
        ListView_InsertItem(ps->machineList, &item);
        ListView_SetItemText(ps->machineList, i, 1, const_cast<wchar_t*>(name.c_str()));
        ListView_SetItemText(ps->machineList, i, 2, const_cast<wchar_t*>(groupCode.c_str()));
        ListView_SetItemText(ps->machineList, i, 3, const_cast<wchar_t*>(groupName.c_str()));
        ListView_SetItemText(ps->machineList, i, 4, const_cast<wchar_t*>(sample.c_str()));
        if (!current.empty() && machine.mach_code == current) selected = i;
    }
    if (selected < 0 && !ps->machines.empty()) selected = 0;
    if (selected >= 0) {
        ListView_SetItemState(ps->machineList, selected, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    }
}

void reloadPickerMachines(SettingsMachinePickerState* ps) {
    if (!ps || !ps->settings) return;
    ps->machines.clear();
    std::string error;
    if (!search::load_report_machine_picker_machine_options(collectForm(ps->owner), selectedPickerRoomCode(ps), ps->machines, error)) {
        MessageBoxW(ps->owner, L"检验仪器加载失败。", L"系统设置", MB_ICONERROR);
    }
    populatePickerMachines(ps);
}

void reloadPickerRooms(SettingsMachinePickerState* ps) {
    if (!ps || !ps->settings || !ps->roomCombo) return;
    SendMessageW(ps->roomCombo, CB_RESETCONTENT, 0, 0);
    ps->rooms.clear();
    std::string error;
    if (!search::load_report_machine_picker_room_options(collectForm(ps->owner), ps->rooms, error)) {
        MessageBoxW(ps->owner, L"检验科室加载失败。", L"系统设置", MB_ICONERROR);
    }
    const std::string currentRoom = ps->settings->quickMachineRoomCodes[static_cast<size_t>(ps->slot)];
    int selected = -1;
    for (int i = 0; i < static_cast<int>(ps->rooms.size()); ++i) {
        const auto text = search::utf8_to_wide(ps->rooms[static_cast<size_t>(i)].room_name);
        SendMessageW(ps->roomCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        if (!currentRoom.empty() && ps->rooms[static_cast<size_t>(i)].room_code == currentRoom) selected = i;
    }
    if (selected < 0 && !ps->rooms.empty()) selected = 0;
    if (selected >= 0) SendMessageW(ps->roomCombo, CB_SETCURSEL, selected, 0);
    reloadPickerMachines(ps);
}

void acceptSettingsMachinePicker(HWND hwnd, SettingsMachinePickerState* ps) {
    if (!ps || !ps->settings || !ps->machineList) return;
    const int index = ListView_GetNextItem(ps->machineList, -1, LVNI_SELECTED);
    if (index < 0 || index >= static_cast<int>(ps->machines.size())) return;
    const auto& machine = ps->machines[static_cast<size_t>(index)];
    const auto slot = static_cast<size_t>(ps->slot);
    ps->settings->quickMachineCodes[slot] = machine.mach_code;
    ps->settings->quickMachineRoomCodes[slot] = selectedPickerRoomCode(ps);
    ps->settings->quickMachineNames[slot] = search::utf8_to_wide(machine.mach_name);
    SetWindowTextW(GetDlgItem(ps->owner, quickMachineEditId(ps->slot)),
                   ps->settings->quickMachineNames[slot].c_str());
    DestroyWindow(hwnd);
}

LRESULT CALLBACK pickerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ps = reinterpret_cast<SettingsMachinePickerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            ps = reinterpret_cast<SettingsMachinePickerState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ps));
            if (!ps) return -1;
            const float scale = search::dpi_scale_factor(hwnd);
            auto S = [scale](int v) { return static_cast<int>(v * scale); };
            const int innerW = S(PICKER_W - PICKER_PAD * 2);
            ps->roomCombo = search::create_combo(hwnd, IDC_PICKER_ROOM, S(PICKER_PAD), S(10),
                                                 innerW, S(PICKER_COMBO_H), false);
            ps->machineList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                              WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                              S(PICKER_PAD), S(PICKER_LIST_Y),
                                              innerW, S(PICKER_H - PICKER_LIST_Y - PICKER_PAD),
                                              hwnd, win32_control_id(IDC_PICKER_MACHINE), GetModuleHandleW(nullptr),
                                              nullptr);
            ListView_SetExtendedListViewStyle(ps->machineList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            search::add_list_column(ps->machineList, 0, L"仪器", S(PICKER_CODE_W));
            search::add_list_column(ps->machineList, 1, L"仪器名称", S(PICKER_NAME_W));
            search::add_list_column(ps->machineList, 2, L"项目代码", S(PICKER_GROUP_CODE_W));
            search::add_list_column(ps->machineList, 3, L"项目名称", S(PICKER_GROUP_NAME_W));
            search::add_list_column(ps->machineList, 4, L"样本", S(PICKER_SAMPLE_W));
            if (ps->settings && ps->settings->ctx.uiFont) {
                EnumChildWindows(hwnd, [](HWND child, LPARAM font) -> BOOL {
                    SendMessageW(child, WM_SETFONT, font, TRUE);
                    return TRUE;
                }, reinterpret_cast<LPARAM>(ps->settings->ctx.uiFont));
            }
            reloadPickerRooms(ps);
            return 0;
        }
        case WM_SIZE:
            if (ps && ps->roomCombo && ps->machineList) {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                const float scale = search::dpi_scale_factor(hwnd);
                const int pad = static_cast<int>(PICKER_PAD * scale);
                const int listY = static_cast<int>(PICKER_LIST_Y * scale);
                const int innerW = std::max(120, static_cast<int>(rc.right - rc.left) - pad * 2);
                MoveWindow(ps->roomCombo, pad, static_cast<int>(10 * scale),
                           innerW, static_cast<int>(PICKER_COMBO_H * scale), TRUE);
                MoveWindow(ps->machineList, pad, listY, innerW,
                           std::max(80, static_cast<int>(rc.bottom - rc.top) - listY - pad), TRUE);
                ListView_SetColumnWidth(ps->machineList, 0, static_cast<int>(PICKER_CODE_W * scale));
                ListView_SetColumnWidth(ps->machineList, 1, static_cast<int>(PICKER_NAME_W * scale));
                ListView_SetColumnWidth(ps->machineList, 2, static_cast<int>(PICKER_GROUP_CODE_W * scale));
                ListView_SetColumnWidth(ps->machineList, 3, static_cast<int>(PICKER_GROUP_NAME_W * scale));
                ListView_SetColumnWidth(ps->machineList, 4,
                                        std::max(static_cast<int>(PICKER_SAMPLE_W * scale),
                                                 innerW - static_cast<int>((PICKER_CODE_W + PICKER_NAME_W +
                                                                           PICKER_GROUP_CODE_W + PICKER_GROUP_NAME_W +
                                                                           16) * scale)));
            }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_PICKER_ROOM && HIWORD(wp) == CBN_SELCHANGE) {
                reloadPickerMachines(ps);
                return 0;
            }
            break;
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (nm->idFrom == IDC_PICKER_MACHINE && nm->code == NM_DBLCLK) {
                acceptSettingsMachinePicker(hwnd, ps);
                return 0;
            }
            if (nm->idFrom == IDC_PICKER_MACHINE && nm->code == LVN_KEYDOWN) {
                auto* key = reinterpret_cast<NMLVKEYDOWN*>(lp);
                if (key->wVKey == VK_RETURN) {
                    acceptSettingsMachinePicker(hwnd, ps);
                    return 0;
                }
            }
            break;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_DESTROY:
            delete ps;
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void showSettingsMachinePicker(HWND owner, SettingsState* st, int slot, HWND anchor) {
    REGISTER_MDI_CHILD_CLASS(GetModuleHandleW(nullptr), pickerProc, PICKER_CLASS, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
    RECT rc{};
    GetWindowRect(anchor ? anchor : owner, &rc);
    auto* ps = new SettingsMachinePickerState;
    ps->settings = st;
    ps->owner = owner;
    ps->slot = slot;
    const float scale = search::dpi_scale_factor(owner);
    RECT popupRc{0, 0, static_cast<LONG>(PICKER_W * scale), static_cast<LONG>(PICKER_H * scale)};
    AdjustWindowRectEx(&popupRc, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_TOOLWINDOW);
    const int popupW = popupRc.right - popupRc.left;
    const int popupH = popupRc.bottom - popupRc.top;
    int popupX = rc.left;
    int popupY = rc.bottom + 2;
    HMONITOR monitor = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (monitor && GetMonitorInfoW(monitor, &mi)) {
        const RECT work = mi.rcWork;
        if (popupX + popupW > work.right) {
            popupX = std::max(static_cast<int>(work.left), static_cast<int>(rc.right) - popupW);
        }
        if (popupY + popupH > work.bottom) {
            popupY = std::max(static_cast<int>(work.top), static_cast<int>(rc.top) - popupH - 2);
        }
        popupX = std::max(static_cast<int>(work.left), std::min(popupX, static_cast<int>(work.right) - popupW));
        popupY = std::max(static_cast<int>(work.top), std::min(popupY, static_cast<int>(work.bottom) - popupH));
    }
    HWND popup = CreateWindowExW(WS_EX_TOOLWINDOW, PICKER_CLASS, L"选择检验仪器",
                                 WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                 popupX, popupY, popupW, popupH,
                                 owner, nullptr, GetModuleHandleW(nullptr), ps);
    ShowWindow(popup, SW_SHOW);
    SetForegroundWindow(popup);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<SettingsState*>(GetPropW(hwnd, PROP_STATE));

    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* mcs = reinterpret_cast<MDICREATESTRUCTW*>(cs->lpCreateParams);
            st = reinterpret_cast<SettingsState*>(mcs->lParam);
            SetPropW(hwnd, PROP_STATE, reinterpret_cast<HANDLE>(st));

            const float s = search::dpi_scale_factor(hwnd);
            auto S = [s](int v) { return static_cast<int>(v * s); };
            REGISTER_MDI_CHILD_CLASS(GetModuleHandleW(nullptr), whiteHostProc, WHITE_HOST_CLASS,
                                     GetSysColorBrush(COLOR_WINDOW));
            st->titleFont = createDerivedFont(st->ctx.uiFont, 7, FW_SEMIBOLD);
            st->sectionFont = createDerivedFont(st->ctx.uiFont, 1, FW_SEMIBOLD);
            st->pageBrush = CreateSolidBrush(COLOR_PAGE_BG);
            st->editBrush = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));

            createText(hwnd, IDC_TEXT_PAGE_TITLE, L"系统设置");
            createText(hwnd, IDC_TEXT_PAGE_SUBTITLE, L"配置数据库连接、显示字号、报告打印、快捷仪器和自动更新。");
            st->tabs = CreateWindowExW(0, WC_TABCONTROLW, L"",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                       0, 0, S(300), S(30), hwnd,
                                       win32_control_id(IDC_SETTINGS_TABS), GetModuleHandleW(nullptr), nullptr);
            const wchar_t* tabNames[] = {L"数据库与界面", L"LIS 摘要项目", L"常规报告打印", L"质控品设置", L"自动更新"};
            for (int i = 0; i < static_cast<int>(sizeof(tabNames) / sizeof(tabNames[0])); ++i) {
                TCITEMW item{};
                item.mask = TCIF_TEXT;
                item.pszText = const_cast<wchar_t*>(tabNames[i]);
                TabCtrl_InsertItem(st->tabs, i, &item);
            }
            createText(hwnd, IDC_TEXT_SECTION_DATABASE, L"数据库与界面");
            createText(hwnd, IDC_TEXT_DATABASE_HINT, L"连接 LIS 数据库，并设置主程序字号。");
            createText(hwnd, IDC_TEXT_DATABASE_DISPLAY, L"界面显示");
            createText(hwnd, IDC_TEXT_SECTION_LIS, L"LIS 摘要项目");
            createText(hwnd, IDC_TEXT_LIS_HINT, L"项目代码用分号分隔；仪器格式：ROOM:MACH1,MACH2；排除整科室用 ROOM:。");
            createText(hwnd, IDC_TEXT_SECTION_REPORT, L"常规报告打印");
            createText(hwnd, IDC_TEXT_REPORT_HINT, L"选择条码打印机，并设置底部 1 / 2 / 3 快捷检验仪器。");
            createText(hwnd, IDC_TEXT_REPORT_PRINTER, L"条码打印机");
            createText(hwnd, IDC_TEXT_REPORT_QUICK, L"快捷检验仪器");
            createText(hwnd, IDC_TEXT_SECTION_UPDATE, L"自动更新");
            createText(hwnd, IDC_TEXT_UPDATE_HINT, L"设置共享目录或 HTTP manifest，并控制启动后自动检查。");
            createText(hwnd, IDC_TEXT_SECTION_QUALITY_CONTROL, L"质控品设置");
            createText(hwnd, IDC_TEXT_QUALITY_CONTROL_HINT, L"维护某台检验仪器的固定质控样本号、项目、水平、靶值和 SD。");
            createText(hwnd, IDC_TEXT_SAVE_STATUS, L"");
            ShowWindow(GetDlgItem(hwnd, IDC_TEXT_SAVE_STATUS), SW_HIDE);

            createSettingLabel(hwnd, IDC_LABEL_SERVER, L"服务器");
            search::create_edit(hwnd, IDC_SET_SERVER, 0, 0, S(10), S(24));
            createSettingLabel(hwnd, IDC_LABEL_INITIAL_DATABASE, L"初始数据库");
            search::create_edit(hwnd, IDC_SET_INITIAL_DATABASE, 0, 0, S(10), S(24));
            createSettingLabel(hwnd, IDC_LABEL_USER, L"用户名");
            search::create_edit(hwnd, IDC_SET_USER, 0, 0, S(10), S(24));
            createSettingLabel(hwnd, IDC_LABEL_PASSWORD, L"密码");
            search::create_password_edit(hwnd, IDC_SET_PASSWORD, 0, 0, S(10), S(24));
            createSettingLabel(hwnd, IDC_LABEL_FONT_SIZE, L"字号");
            search::create_combo(hwnd, IDC_SET_FONT_SIZE, 0, 0, S(132), S(128), false);

            createSettingLabel(hwnd, IDC_LABEL_LIS_ABO_CODES, L"ABO 代码");
            search::create_edit(hwnd, IDC_SET_LIS_ABO_CODES, 0, 0, S(10), S(24));
            createSettingLabel(hwnd, IDC_LABEL_LIS_RHD_CODES, L"RhD 代码");
            search::create_edit(hwnd, IDC_SET_LIS_RHD_CODES, 0, 0, S(10), S(24));
            createSettingLabel(hwnd, IDC_LABEL_LIS_HGB_CODES, L"Hb 代码");
            search::create_edit(hwnd, IDC_SET_LIS_HGB_CODES, 0, 0, S(10), S(24));
            createSettingLabel(hwnd, IDC_LABEL_LIS_PLT_CODES, L"PLT 代码");
            search::create_edit(hwnd, IDC_SET_LIS_PLT_CODES, 0, 0, S(10), S(24));
            createSettingLabel(hwnd, IDC_LABEL_LIS_IRREGULAR_ANTIBODY_CODES, L"不规则代码");
            search::create_edit(hwnd, IDC_SET_LIS_IRREGULAR_ANTIBODY_CODES, 0, 0, S(10), S(24));
            createSettingLabel(hwnd, IDC_LABEL_LIS_DIRECT_ANTIGLOBULIN_CODES, L"直抗代码");
            search::create_edit(hwnd, IDC_SET_LIS_DIRECT_ANTIGLOBULIN_CODES, 0, 0, S(10), S(24));
            createSettingLabel(hwnd, IDC_LABEL_LIS_BLOOD_TYPE_MACHINES, L"血型仪器");
            search::create_edit(hwnd, IDC_SET_LIS_BLOOD_TYPE_MACHINES, 0, 0, S(10), S(24));
            createSettingLabel(hwnd, IDC_LABEL_LIS_CBC_MACHINES, L"血常规仪器");
            search::create_edit(hwnd, IDC_SET_LIS_CBC_MACHINES, 0, 0, S(10), S(24));
            createSettingLabel(hwnd, IDC_LABEL_LIS_BLOOD_EXCLUDE_MACHINES, L"输血排除仪器");
            search::create_edit(hwnd, IDC_SET_LIS_BLOOD_EXCLUDE_MACHINES, 0, 0, S(10), S(24));

            createSettingLabel(hwnd, IDC_LABEL_BARCODE_PRINTER, L"条码打印机");
            search::create_combo(hwnd, IDC_SET_BARCODE_PRINTER, 0, 0, S(10), S(220), false);
            createSettingLabel(hwnd, IDC_LABEL_ZEBRA_CHINESE_FONT, L"Zebra 字体");
            search::create_combo(hwnd, IDC_SET_ZEBRA_CHINESE_FONT, 0, 0, S(10), S(160), false);
            createSettingLabel(hwnd, IDC_LABEL_QUICK_MACHINE_1, L"快捷仪器 1");
            search::create_edit(hwnd, IDC_SET_QUICK_MACHINE_1, 0, 0, S(10), S(24));
            search::create_button(hwnd, IDC_SET_QUICK_MACHINE_PICK_1, L"...", 0, 0, S(40), S(28));
            createSettingLabel(hwnd, IDC_LABEL_QUICK_MACHINE_2, L"快捷仪器 2");
            search::create_edit(hwnd, IDC_SET_QUICK_MACHINE_2, 0, 0, S(10), S(24));
            search::create_button(hwnd, IDC_SET_QUICK_MACHINE_PICK_2, L"...", 0, 0, S(40), S(28));
            createSettingLabel(hwnd, IDC_LABEL_QUICK_MACHINE_3, L"快捷仪器 3");
            search::create_edit(hwnd, IDC_SET_QUICK_MACHINE_3, 0, 0, S(10), S(24));
            search::create_button(hwnd, IDC_SET_QUICK_MACHINE_PICK_3, L"...", 0, 0, S(40), S(28));

            createSettingLabel(hwnd, IDC_LABEL_UPDATE_SOURCE, L"更新源");
            search::create_combo(hwnd, IDC_SET_UPDATE_SOURCE, 0, 0, S(10), S(180), false);
            createSettingLabel(hwnd, IDC_SET_UPDATE_MANIFEST_LABEL, L"HTTP地址");
            search::create_edit(hwnd, IDC_SET_UPDATE_MANIFEST_URL, 0, 0, S(10), S(24));
            createSettingLabel(hwnd, IDC_SET_UPDATE_FOLDER_LABEL, L"共享目录");
            search::create_edit(hwnd, IDC_SET_UPDATE_FOLDER, 0, 0, S(10), S(24));
            st->updateAutoCheckHost = CreateWindowExW(
                0, WHITE_HOST_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                0, 0, S(180), S(24), hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            st->updateAutoCheck = CreateWindowExW(
                0, L"BUTTON", L"自动检查更新",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                0, 0, S(180), S(24), st->updateAutoCheckHost,
                win32_control_id(IDC_SET_UPDATE_AUTO_CHECK), GetModuleHandleW(nullptr), nullptr);

            search::create_button(hwnd, IDC_SET_TEST, L"测试连接", 0, 0, S(92), S(30));
            search::create_button(hwnd, IDC_SET_SAVE, L"保存", 0, 0, S(84), S(30));
            search::create_button(hwnd, IDC_SET_CANCEL, L"取消", 0, 0, S(84), S(30));
            st->qualityControlPanel = create_quality_control_settings_panel(hwnd, st->ctx.uiFont, st->app.db);

            auto& app = st->app;
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_SERVER), app.db.server.c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_INITIAL_DATABASE), app.db.initial_database.c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_USER), app.db.user.c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_PASSWORD), app.db.password.c_str());
            populateFontSizeCombo(hwnd, app.ui.font_size);
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_LIS_ABO_CODES), app.lis.abo_codes.c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_LIS_RHD_CODES), app.lis.rhd_codes.c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_LIS_HGB_CODES), app.lis.hgb_codes.c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_LIS_PLT_CODES), app.lis.plt_codes.c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_LIS_IRREGULAR_ANTIBODY_CODES), app.lis.irregular_antibody_codes.c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_LIS_DIRECT_ANTIGLOBULIN_CODES), app.lis.direct_antiglobulin_codes.c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_LIS_BLOOD_TYPE_MACHINES), app.lis.blood_type_machines.c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_LIS_CBC_MACHINES), app.lis.cbc_machines.c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_LIS_BLOOD_EXCLUDE_MACHINES), app.lis.blood_lis_exclude_machines.c_str());
            for (int i = 0; i < QUICK_MACHINE_COUNT; ++i) {
                st->quickMachineCodes[static_cast<size_t>(i)] =
                    search::wide_to_utf8(search::load_module_str(L"RegularReport", quick_machine_code_key(i), L""));
                st->quickMachineRoomCodes[static_cast<size_t>(i)] =
                    search::wide_to_utf8(search::load_module_str(L"RegularReport", quick_machine_room_key(i), L""));
                st->quickMachineNames[static_cast<size_t>(i)] =
                    search::load_module_str(L"RegularReport", quick_machine_name_key(i), L"");
                SetWindowTextW(GetDlgItem(hwnd, quickMachineEditId(i)),
                               st->quickMachineNames[static_cast<size_t>(i)].c_str());
                SendMessageW(GetDlgItem(hwnd, quickMachineEditId(i)), EM_SETREADONLY, TRUE, 0);
            }
            populatePrinterCombo(hwnd);
            populateZebraChineseFontCombo(hwnd);
            populateUpdateSourceCombo(hwnd);
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_UPDATE_MANIFEST_URL),
                           search::load_module_str(lis_update::kConfigSection, L"ManifestUrl",
                                                   lis_update::kDefaultGithubManifestUrl).c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_SET_UPDATE_FOLDER),
                           search::load_module_str(lis_update::kConfigSection, L"FolderPath", L"").c_str());
            updateSourceFieldVisibility(hwnd);
            SendMessageW(st->updateAutoCheck, BM_SETCHECK,
                         search::load_module_int(lis_update::kConfigSection, L"AutoCheck", 0)
                             ? BST_CHECKED
                             : BST_UNCHECKED,
                         0);

            EnumChildWindows(hwnd, [](HWND child, LPARAM param) -> BOOL {
                SendMessageW(child, WM_SETFONT, param, TRUE);
                return TRUE;
            }, reinterpret_cast<LPARAM>(st->ctx.uiFont));
            SendMessageW(GetDlgItem(hwnd, IDC_TEXT_PAGE_TITLE), WM_SETFONT,
                         reinterpret_cast<WPARAM>(st->titleFont), TRUE);
            SendMessageW(GetDlgItem(hwnd, IDC_TEXT_SECTION_DATABASE), WM_SETFONT,
                         reinterpret_cast<WPARAM>(st->sectionFont), TRUE);
            SendMessageW(GetDlgItem(hwnd, IDC_TEXT_SECTION_LIS), WM_SETFONT,
                         reinterpret_cast<WPARAM>(st->sectionFont), TRUE);
            SendMessageW(GetDlgItem(hwnd, IDC_TEXT_SECTION_REPORT), WM_SETFONT,
                         reinterpret_cast<WPARAM>(st->sectionFont), TRUE);
            SendMessageW(GetDlgItem(hwnd, IDC_TEXT_SECTION_UPDATE), WM_SETFONT,
                         reinterpret_cast<WPARAM>(st->sectionFont), TRUE);
            SendMessageW(GetDlgItem(hwnd, IDC_TEXT_SECTION_QUALITY_CONTROL), WM_SETFONT,
                         reinterpret_cast<WPARAM>(st->sectionFont), TRUE);
            SendMessageW(GetDlgItem(hwnd, IDC_TEXT_DATABASE_DISPLAY), WM_SETFONT,
                         reinterpret_cast<WPARAM>(st->sectionFont), TRUE);
            SendMessageW(GetDlgItem(hwnd, IDC_TEXT_REPORT_PRINTER), WM_SETFONT,
                         reinterpret_cast<WPARAM>(st->sectionFont), TRUE);
            SendMessageW(GetDlgItem(hwnd, IDC_TEXT_REPORT_QUICK), WM_SETFONT,
                         reinterpret_cast<WPARAM>(st->sectionFont), TRUE);
            layoutSettingsWindow(hwnd);
            return 0;
        }
        case WM_SIZE:
            layoutSettingsWindow(hwnd);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case WM_ERASEBKGND: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc, st && st->pageBrush ? st->pageBrush : GetSysColorBrush(COLOR_BTNFACE));
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(dc, &rc, st && st->pageBrush ? st->pageBrush : GetSysColorBrush(COLOR_BTNFACE));
            SettingsLayout l = calculateLayout(hwnd);
            const int radius = static_cast<int>(10 * search::dpi_scale_factor(hwnd));
            drawRoundRect(dc, l.content, radius, COLOR_CARD_BG, COLOR_CARD_BORDER);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lp));
            if (isReadOnlyEditId(id)) {
                SetBkMode(dc, OPAQUE);
                SetBkColor(dc, RGB(0xFF, 0xFF, 0xFF));
                SetTextColor(dc, COLOR_TEXT);
                return reinterpret_cast<LRESULT>(st && st->editBrush ? st->editBrush : GetStockObject(WHITE_BRUSH));
            }
            SetBkMode(dc, TRANSPARENT);
            if (id == IDC_TEXT_SAVE_STATUS) {
                SetTextColor(dc, COLOR_SUCCESS);
            } else if (id == IDC_TEXT_PAGE_TITLE || isSectionTitleId(id)) {
                SetTextColor(dc, id == IDC_TEXT_PAGE_TITLE ? COLOR_TEXT : COLOR_ACCENT);
            } else {
                SetTextColor(dc, isMutedTextId(id) ? COLOR_MUTED_TEXT : COLOR_TEXT);
            }
            return reinterpret_cast<LRESULT>(GetStockObject(HOLLOW_BRUSH));
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, RGB(0xFF, 0xFF, 0xFF));
            SetTextColor(dc, COLOR_TEXT);
            return reinterpret_cast<LRESULT>(st && st->editBrush ? st->editBrush : GetStockObject(WHITE_BRUSH));
        }
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (st && nm->idFrom == IDC_SETTINGS_TABS && nm->code == TCN_SELCHANGE) {
                st->currentTab = TabCtrl_GetCurSel(st->tabs);
                if (st->qualityControlPanel) {
                    update_quality_control_settings_panel_db(st->qualityControlPanel, collectForm(hwnd));
                }
                layoutSettingsWindow(hwnd);
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
            break;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == IDC_SET_UPDATE_SOURCE && HIWORD(wp) == CBN_SELCHANGE) {
                updateSourceFieldVisibility(hwnd);
                return 0;
            }
            if (id == IDC_SET_CANCEL) { DestroyWindow(hwnd); return 0; }
            if (id == IDC_SET_QUICK_MACHINE_PICK_1 || id == IDC_SET_QUICK_MACHINE_PICK_2 ||
                id == IDC_SET_QUICK_MACHINE_PICK_3) {
                const int slot = id == IDC_SET_QUICK_MACHINE_PICK_1 ? 0 :
                                 (id == IDC_SET_QUICK_MACHINE_PICK_2 ? 1 : 2);
                showSettingsMachinePicker(hwnd, st, slot, reinterpret_cast<HWND>(lp));
                return 0;
            }
            if (id == IDC_SET_TEST) {
                auto db = collectForm(hwnd);
                if (st && st->qualityControlPanel) {
                    update_quality_control_settings_panel_db(st->qualityControlPanel, db);
                }
                if (search::build_connection_string_w(db).empty()) {
                    MessageBoxW(hwnd, L"请先填写服务器、初始数据库和用户名。", L"测试连接", MB_ICONWARNING);
                } else {
                    std::string error;
                    if (search::test_database_connection(db, error))
                        MessageBoxW(hwnd, L"数据库连接成功。", L"测试连接", MB_ICONINFORMATION);
                    else
                        MessageBoxW(hwnd, search::utf8_to_wide(error).c_str(), L"数据库连接失败", MB_ICONERROR);
                }
                return 0;
            }
            if (id == IDC_SET_SAVE) {
                int oldFontSize = st->app.ui.font_size;
                if (st->ctx.appContext) {
                    auto* gctx = static_cast<app::Context*>(st->ctx.appContext);
                    oldFontSize = gctx->fontSize;
                }
                st->app.db = collectForm(hwnd);
                if (st->qualityControlPanel) {
                    update_quality_control_settings_panel_db(st->qualityControlPanel, st->app.db);
                }
                st->app.ui.font_size = selectedFontSize(hwnd);
                st->app.lis.abo_codes = readEdit(hwnd, IDC_SET_LIS_ABO_CODES);
                st->app.lis.rhd_codes = readEdit(hwnd, IDC_SET_LIS_RHD_CODES);
                st->app.lis.hgb_codes = readEdit(hwnd, IDC_SET_LIS_HGB_CODES);
                st->app.lis.plt_codes = readEdit(hwnd, IDC_SET_LIS_PLT_CODES);
                st->app.lis.irregular_antibody_codes = readEdit(hwnd, IDC_SET_LIS_IRREGULAR_ANTIBODY_CODES);
                st->app.lis.direct_antiglobulin_codes = readEdit(hwnd, IDC_SET_LIS_DIRECT_ANTIGLOBULIN_CODES);
                st->app.lis.blood_type_machines = readEdit(hwnd, IDC_SET_LIS_BLOOD_TYPE_MACHINES);
                st->app.lis.cbc_machines = readEdit(hwnd, IDC_SET_LIS_CBC_MACHINES);
                st->app.lis.blood_lis_exclude_machines = readEdit(hwnd, IDC_SET_LIS_BLOOD_EXCLUDE_MACHINES);
                if (!search::save_settings(search::default_ini_path(), st->app)) {
                    setSaveStatus(hwnd, L"");
                    MessageBoxW(hwnd, L"系统设置保存失败，请检查配置文件是否可写。", L"保存失败", MB_ICONERROR);
                    return 0;
                }
                search::save_module_str(L"RegularReport", L"BarcodePrinterName",
                                        readCombo(hwnd, IDC_SET_BARCODE_PRINTER));
                search::save_module_str(L"RegularReport", L"ZebraChineseFont",
                                        search::normalize_zebra_chinese_font(
                                            readCombo(hwnd, IDC_SET_ZEBRA_CHINESE_FONT)));
                for (int i = 0; i < QUICK_MACHINE_COUNT; ++i) {
                    search::save_module_str(L"RegularReport", quick_machine_code_key(i),
                                            search::utf8_to_wide(st->quickMachineCodes[static_cast<size_t>(i)]));
                    search::save_module_str(L"RegularReport", quick_machine_room_key(i),
                                            search::utf8_to_wide(st->quickMachineRoomCodes[static_cast<size_t>(i)]));
                    search::save_module_str(L"RegularReport", quick_machine_name_key(i),
                                            st->quickMachineNames[static_cast<size_t>(i)]);
                }
                saveUpdateConfig(collectUpdateConfig(hwnd));
                search::save_module_int(lis_update::kConfigSection, L"AutoCheck",
                                        SendMessageW(st->updateAutoCheck,
                                                     BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0);
                if (st->ctx.appContext) {
                    auto* gctx = static_cast<app::Context*>(st->ctx.appContext);
                    gctx->dbSettings = st->app.db;
                    gctx->fontSize = st->app.ui.font_size;
                    if (gctx->mainWindow && oldFontSize != st->app.ui.font_size) {
                        SendMessageW(gctx->mainWindow, app::WM_APP_SETTINGS_CHANGED, 0, 0);
                    } else {
                        broadcastSettingsChangedToChildren(st->ctx.mdiClient);
                    }
                }
                setSavedStatusNow(hwnd);
                return 0;
            }
            break;
        }
        case WM_DESTROY:
            RemovePropW(hwnd, PROP_STATE);
            if (st) {
                if (st->titleFont) DeleteObject(st->titleFont);
                if (st->sectionFont) DeleteObject(st->sectionFont);
                if (st->pageBrush) DeleteObject(st->pageBrush);
                if (st->editBrush) DeleteObject(st->editBrush);
            }
            delete st;
            break;
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND create_settings_module(const ModuleContext& ctx) {
    if (HWND existing = activate_existing_mdi_child_by_title(ctx.mdiClient, WINDOW_TITLE)) {
        return existing;
    }

    REGISTER_MDI_CHILD_CLASS(ctx.instance, wndProc, WND_CLASS, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));

    auto* st = new SettingsState;
    st->ctx = ctx;
    st->app = search::load_settings(search::default_ini_path());

    MDICREATESTRUCTW mcs{};
    mcs.szClass = WND_CLASS;
    mcs.szTitle = WINDOW_TITLE;
    mcs.hOwner = ctx.instance;
    mcs.x = mcs.y = mcs.cx = mcs.cy = CW_USEDEFAULT;

    mcs.lParam = reinterpret_cast<LPARAM>(st);
    HWND child = reinterpret_cast<HWND>(SendMessageW(ctx.mdiClient, WM_MDICREATE, 0,
        reinterpret_cast<LPARAM>(&mcs)));
    if (child) {
        SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    } else {
        delete st;
    }
    return child;
}

#endif
