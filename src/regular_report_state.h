#pragma once

#ifdef _WIN32

#include "module_registry.h"
#include "search_app.h"
#include "search_core.h"
#include "window_task.h"

#include <windows.h>
#include <commctrl.h>
#include <gdiplus.h>

#include <memory>
#include <string>
#include <vector>

// ============================================================================
// IDs and dimensions (shared across all regular_report_* files)
// ============================================================================

constexpr const wchar_t* REGULAR_REPORT_WND_CLASS = L"RegularReportChild";
constexpr const wchar_t* REGULAR_REPORT_PICTURE_POPUP_CLASS = L"RegularReportPicturePopup";
constexpr const wchar_t* REGULAR_REPORT_WINDOW_TITLE = L"常规报告";
constexpr const wchar_t* REGULAR_REPORT_PROP_STATE = L"RegularReportSt";
constexpr const wchar_t* REGULAR_REPORT_PROP_DATE_FORMAT = L"RegularReportDateFormat";
constexpr const wchar_t* REGULAR_REPORT_BLANK_DATE_FORMAT = L" ";

constexpr UINT_PTR IDT_REPORT_AUTO_REFRESH = 6310;
constexpr UINT_PTR IDT_REPORT_INITIAL_QUICK_MACHINE = 6311;

constexpr int REGULAR_IDC_RESULT_LIST = 5201;
constexpr int REGULAR_IDC_REPORT_LIST = 5202;
constexpr int REGULAR_IDC_SPLITTER = 5204;
constexpr int REGULAR_IDC_LEFT_SCROLL = 5205;
constexpr int REGULAR_IDC_RIGHT_TAB = 5206;
constexpr int REGULAR_IDC_MIDDLE_TAB = 5207;
constexpr int REGULAR_IDC_MACHINE_PICKER_BUTTON = 5208;
constexpr int REGULAR_IDC_MACHINE_PICKER_ROOM = 5209;
constexpr int REGULAR_IDC_MACHINE_PICKER_MACH = 5210;
constexpr int REGULAR_IDC_MACHINE_PICKER_SEARCH = 5219;
constexpr int REGULAR_IDC_INSPECT_DATE = 5211;
constexpr int REGULAR_IDC_REPORT_FIRST_BUTTON = 5212;
constexpr int REGULAR_IDC_REPORT_LAST_BUTTON = 5213;
constexpr int REGULAR_IDC_REPORT_DATE_TODAY_BUTTON = 5214;
constexpr int REGULAR_IDC_REPORT_DATE_PREV_BUTTON = 5215;
constexpr int REGULAR_IDC_REPORT_DATE_NEXT_BUTTON = 5216;
constexpr int REGULAR_IDC_REPORT_AUTO_REFRESH_CHECK = 5217;
constexpr int REGULAR_IDC_REPORT_AUTO_REFRESH_SECONDS = 5218;
constexpr int REGULAR_IDC_BOTTOM_MACHINE_1 = 5401;
constexpr int REGULAR_IDC_BOTTOM_REFRESH = 5402;
constexpr int REGULAR_IDC_BOTTOM_MACHINE_2 = 5411;
constexpr int REGULAR_IDC_BOTTOM_MACHINE_3 = 5420;
constexpr int REGULAR_IDC_BOTTOM_GRAPH = 5424;
constexpr int REGULAR_IDC_BOTTOM_PREV_REPORT = 5408;
constexpr int REGULAR_IDC_BOTTOM_NEXT_REPORT = 5409;
constexpr int REGULAR_IDM_REPORT_PRINT_BARCODE = 5220;
constexpr int REGULAR_IDM_REPORT_PRINT_CHECKED_BARCODES = 5221;
constexpr int REGULAR_IDM_REPORT_TREND = 5222;
constexpr int REGULAR_IDC_BOTTOM_TREND = 5425;

constexpr int REGULAR_REPORT_COLUMN_COUNT = 30;
constexpr int REGULAR_RESULT_VALUE_COL = 5;
constexpr int REGULAR_RIGHT_REPORT_PRINT_COL = 8;

constexpr UINT_PTR REGULAR_LEFT_PANEL_SUBCLASS = 6205;
constexpr UINT_PTR REGULAR_LEFT_CONTENT_SUBCLASS = 6206;
constexpr UINT_PTR REGULAR_RIGHT_PANEL_SUBCLASS = 6207;
constexpr UINT_PTR REGULAR_MIDDLE_PANEL_SUBCLASS = 6208;
constexpr UINT_PTR REGULAR_BOTTOM_PANEL_SUBCLASS = 6209;
constexpr UINT_PTR REGULAR_SAMPLE_INPUT_SUBCLASS = 6210;
constexpr UINT_PTR REGULAR_PICTURE_VIEW_SUBCLASS = 6211;
constexpr UINT_PTR REGULAR_PICTURE_VIEWPORT_SUBCLASS = 6212;
constexpr UINT_PTR REGULAR_RESULT_EDIT_SUBCLASS = 6213;
constexpr UINT_PTR REGULAR_RESULT_LIST_SUBCLASS = 6214;
constexpr UINT_PTR REGULAR_LEFT_TAB_SUBCLASS = 6215;
constexpr UINT_PTR REGULAR_MACHINE_PICKER_SEARCH_SUBCLASS = 6216;

constexpr int REGULAR_LEFT_CONTENT_HEIGHT = 875;
constexpr int REGULAR_LEFT_SCROLL_STEP = 36;
constexpr int REGULAR_LEFT_PANEL_MIN_W = 360;
constexpr int REGULAR_LEFT_PANEL_MAX_W = 360;
constexpr int REGULAR_PAD = 8;
constexpr int REGULAR_GAP = 6;
constexpr int REGULAR_SPLITTER_W = 8;
constexpr int REGULAR_TAB_H = 30;
constexpr int REGULAR_COMPACT_BUTTON_H = 28;
constexpr int REGULAR_BOTTOM_PANEL_H = 102;
constexpr int REGULAR_MIDDLE_TOOLBAR_Y = 32;
constexpr int REGULAR_MIDDLE_LIST_Y = 66;
constexpr int REGULAR_MIDDLE_LIST_BOTTOM_MARGIN = 88;
constexpr int REGULAR_MIDDLE_STATUS_BOTTOM = 22;
constexpr int REGULAR_MIDDLE_STATUS_H = 18;
constexpr int REGULAR_PICTURE_FIXED_W = 1560;
constexpr int REGULAR_PICTURE_FIXED_H = 1050;
constexpr int REGULAR_PICTURE_POPUP_DEFAULT_W = 980;
constexpr int REGULAR_PICTURE_POPUP_DEFAULT_H = 700;
constexpr int REGULAR_PICTURE_POPUP_MIN_W = 360;
constexpr int REGULAR_PICTURE_POPUP_MIN_H = 260;

constexpr int REGULAR_QUICK_MACHINE_COUNT = 3;
constexpr int REGULAR_RIGHT_TAB_Y = 56;
constexpr int REGULAR_RIGHT_SEARCH_LABEL_Y = 94;
constexpr int REGULAR_RIGHT_SEARCH_CONTROL_Y = 90;
constexpr int REGULAR_RIGHT_LIST_Y = 128;
constexpr int REGULAR_RIGHT_DATE_BUTTON_H = 28;
constexpr int REGULAR_RIGHT_DATE_BUTTON_W = 72;
constexpr int REGULAR_AUTO_REFRESH_DEFAULT_SECONDS = 10;
constexpr int REGULAR_AUTO_REFRESH_MIN_SECONDS = 5;
constexpr int REGULAR_AUTO_REFRESH_MAX_SECONDS = 3600;

constexpr const wchar_t* REGULAR_MACHINE_PICKER_CLASS = L"RegularReportMachinePicker";
constexpr int REGULAR_MACHINE_PICKER_CLIENT_W = 644;
constexpr int REGULAR_MACHINE_PICKER_INITIAL_H = 224;
constexpr int REGULAR_MACHINE_PICKER_INPUT_X = 10;
constexpr int REGULAR_MACHINE_PICKER_SEARCH_LABEL_Y = 14;
constexpr int REGULAR_MACHINE_PICKER_SEARCH_Y = 10;
constexpr int REGULAR_MACHINE_PICKER_SEARCH_LABEL_W = 66;
constexpr int REGULAR_MACHINE_PICKER_ROOM_Y = 44;
constexpr int REGULAR_MACHINE_PICKER_LIST_Y = 76;
constexpr int REGULAR_MACHINE_PICKER_LIST_W = 618;
constexpr int REGULAR_MACHINE_PICKER_CODE_COL_W = 64;
constexpr int REGULAR_MACHINE_PICKER_PY_COL_W = 80;
constexpr int REGULAR_MACHINE_PICKER_GROUP_CODE_COL_W = 74;
constexpr int REGULAR_MACHINE_PICKER_GROUP_NAME_COL_W = 150;
constexpr int REGULAR_MACHINE_PICKER_SAMPLE_COL_W = 66;
constexpr int REGULAR_MACHINE_PICKER_COL_GAP = 8;
constexpr int REGULAR_MACHINE_PICKER_NAME_COL_W =
    REGULAR_MACHINE_PICKER_LIST_W - REGULAR_MACHINE_PICKER_CODE_COL_W -
    REGULAR_MACHINE_PICKER_GROUP_CODE_COL_W - REGULAR_MACHINE_PICKER_GROUP_NAME_COL_W -
    REGULAR_MACHINE_PICKER_SAMPLE_COL_W - REGULAR_MACHINE_PICKER_PY_COL_W -
    REGULAR_MACHINE_PICKER_COL_GAP;
constexpr int REGULAR_MACHINE_PICKER_COMBO_DROP_H = 180;
constexpr int REGULAR_MACHINE_PICKER_INITIAL_LIST_H = 92;
constexpr int REGULAR_MACHINE_PICKER_MIN_ROWS = 3;
constexpr int REGULAR_MACHINE_PICKER_MAX_ROWS = 8;
constexpr int REGULAR_MACHINE_PICKER_HEADER_H = 28;
constexpr int REGULAR_MACHINE_PICKER_BOTTOM_PAD = 10;
constexpr int REGULAR_MACHINE_PICKER_LIST_EXTRA_H = 6;

constexpr COLORREF REGULAR_COLOR_WHITE = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF REGULAR_COLOR_RESULT_SIDE_BG = RGB(0xF0, 0xF0, 0xF0);
constexpr COLORREF REGULAR_COLOR_REPORT_REVIEWED = RGB(0x6F, 0x94, 0xE6);
constexpr COLORREF REGULAR_COLOR_REPORT_SENT = RGB(0x98, 0xBB, 0x8F);
constexpr COLORREF REGULAR_COLOR_CRITICAL_PENDING = RGB(0xFA, 0xC0, 0xCB);
constexpr COLORREF REGULAR_COLOR_CRITICAL_FINAL = RGB(0xFF, 0xFF, 0x39);

// ============================================================================
// Shared structs
// ============================================================================

struct ColumnDef {
    int index;
    const wchar_t* title;
    int width;
};

struct ButtonDef {
    int id;
    const wchar_t* text;
};

// Drawn on leftContent instead of real GROUPBOX windows so sibling clipping can
// be enabled on the child controls without the group frame hiding them.
struct GroupFrame {
    RECT rect{};
    const wchar_t* title = L"";
};

struct RightHeaderLayout {
    RECT line1{};
    RECT line2{};
    int bottom = 0;
};

struct AgeDisplayParts {
    std::string value;
    std::string unit = "岁";
};

// ============================================================================
// Main state structures
// ============================================================================

struct RegularReportState;

struct MachinePickerState {
    RegularReportState* report = nullptr;
    HWND searchEdit = nullptr;
    HWND roomCombo = nullptr;
    HWND machineList = nullptr;
    std::vector<search::RoomOption> rooms;
    std::vector<search::MachineOption> allMachines;
    std::vector<search::MachineOption> machines;
    bool closePosted = false;
    bool syncingRoom = false;
    bool roomChosenByUser = false;
    bool refreshingMachines = false;
};

struct ReportLoadResult {
    int generation = 0;
    bool ok = false;
    bool preserveState = false;
    std::vector<search::ReportRow> rows;
    std::string connectionString;
    std::string queryDate;
    std::string error;
};

struct ResultLoadResult {
    int generation = 0;
    bool ok = false;
    std::vector<search::ResultRow> rows;
    std::string error;
};

struct PictureLoadResult {
    int generation = 0;
    bool ok = false;
    std::vector<unsigned char> picture;
    std::string error;
};

struct PicturePopupLoadResult {
    int generation = 0;
    bool ok = false;
    std::vector<unsigned char> picture;
    std::string error;
};

struct PicturePopupState {
    RegularReportState* owner = nullptr;
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    IStream* stream = nullptr;
    Gdiplus::Image* image = nullptr;
    ULONG_PTR gdiplusToken = 0;
    bool gdiplusReady = false;
    bool loading = false;
    app::WindowTask pictureTask;
    int generation = 0;
    std::string repNo;
    std::wstring status;
};

struct RegularReportState {
    ModuleContext ctx;
    HWND hwnd = nullptr;
    HWND leftPanel = nullptr;
    HWND leftContent = nullptr;
    HWND leftScrollBar = nullptr;
    HWND middlePanel = nullptr;
    HWND rightPanel = nullptr;
    HWND bottomPanel = nullptr;
    HWND splitter = nullptr;
    HWND middleTab = nullptr;
    HWND rightTab = nullptr;
    HWND rightSearchLabel = nullptr;
    HWND rightSearchEdit = nullptr;
    HWND rightSearchIndexButton = nullptr;
    HWND rightSearchUpButton = nullptr;
    HWND rightSearchDownButton = nullptr;
    HWND rightSearchMenuButton = nullptr;
    HWND rightDateTodayButton = nullptr;
    HWND rightDatePrevButton = nullptr;
    HWND rightDateNextButton = nullptr;
    HWND rightAutoRefreshCheck = nullptr;
    HWND rightAutoRefreshLabel = nullptr;
    HWND rightAutoRefreshEdit = nullptr;
    HWND rightAutoRefreshUnitLabel = nullptr;
    HWND machineEdit = nullptr;
    HWND machinePickerButton = nullptr;
    HWND machinePickerPopup = nullptr;
    HWND groupEdit = nullptr;
    HWND sampleEdit = nullptr;
    HWND reportNoEdit = nullptr;
    HWND operNoEdit = nullptr;
    HWND patientTypeCombo = nullptr;
    HWND urgentCheck = nullptr;
    HWND urgentLabel = nullptr;
    HWND urgentEdit = nullptr;
    HWND barcodeEdit = nullptr;
    HWND regNoEdit = nullptr;
    HWND patientNameEdit = nullptr;
    HWND sexEdit = nullptr;
    HWND ageEdit = nullptr;
    HWND ageUnitCombo = nullptr;
    HWND bedEdit = nullptr;
    HWND phoneEdit = nullptr;
    HWND deptEdit = nullptr;
    HWND diagEdit = nullptr;
    HWND reqDoctorEdit = nullptr;
    HWND feeEdit = nullptr;
    HWND testerEdit = nullptr;
    HWND auditEdit = nullptr;
    HWND noteCodeEdit = nullptr;
    HWND noteEdit = nullptr;
    HWND applyDatePicker = nullptr;
    HWND receiveDatePicker = nullptr;
    HWND machineDatePicker = nullptr;
    HWND reportDatePicker = nullptr;
    HWND inspectDatePicker = nullptr;
    HWND collectDateEdit = nullptr;
    HWND resultList = nullptr;
    HWND resultEdit = nullptr;
    HWND pictureViewport = nullptr;
    HWND pictureView = nullptr;
    HWND pictureHScroll = nullptr;
    HWND pictureVScroll = nullptr;
    HWND picturePopup = nullptr;
    HWND reportList = nullptr;
    HWND status = nullptr;
    int splitterX = 0;
    int pendingSplitterX = 0;
    int leftScrollY = 0;
    int pictureScrollX = 0;
    int pictureScrollY = 0;
    int resultEditRow = -1;
    int leftContentHeight = REGULAR_LEFT_CONTENT_HEIGHT;
    bool splitterUserSet = false;
    int reportQueryGeneration = 0;
    int resultQueryGeneration = 0;
    int pictureQueryGeneration = 0;
    int selectedReportIndex = -1;
    bool reportQueryLoading = false;
    bool resultQueryLoading = false;
    bool pictureQueryLoading = false;
    app::WindowTask reportQueryTask;
    app::WindowTask resultQueryTask;
    app::WindowTask pictureQueryTask;
    bool autoRefreshTimerActive = false;
    bool initialQuickMachineTimerActive = false;
    bool skipInitialQuickMachineLoad = false;
    bool suppressInspectDateQuery = false;
    bool suppressReportSelectionQuery = false;
    int reportSortColumn = -1;
    bool reportSortAscending = true;
    std::vector<HWND> leftControls;
    std::vector<HWND> leftTabControls;
    std::vector<GroupFrame> leftGroupFrames;
    std::vector<HWND> middleResultControls;
    std::vector<HWND> middlePictureControls;
    std::vector<HWND> rightInfoControls;
    HBRUSH bgBrush = nullptr;
    HBRUSH panelBrush = nullptr;
    HBRUSH blackBrush = nullptr;
    HFONT groupTitleFont = nullptr;
    std::string selectedMachineCode;
    std::string selectedRoomCode;
    bool machinePickerCacheLoaded = false;
    std::string machinePickerCacheConnectionString;
    std::vector<search::RoomOption> cachedMachinePickerRooms;
    std::vector<search::MachineOption> cachedMachinePickerMachines;
    std::string reportConnectionString;
    std::string reportQueryDate;
    bool pendingOpenReport = false;
    std::string pendingOpenRepNo;
    std::string pendingOpenOperNo;
    std::vector<search::ReportRow> reportRows;
    std::vector<search::ResultRow> resultRows;
    std::wstring pictureStatus;
    std::string pictureRepNo;
    IStream* pictureStream = nullptr;
    Gdiplus::Image* pictureImage = nullptr;
    ULONG_PTR gdiplusToken = 0;
    bool gdiplusReady = false;
    int contextReportIndex = -1;
};

// ============================================================================
// Forward declarations for cross-file functions
// ============================================================================

// utils
int regularS(HWND hwnd, int value);
HWND regularMakeStatic(HWND parent, const wchar_t* text, int x, int y, int w, int h, DWORD style = SS_LEFT);
HWND regularMakeEdit(HWND parent, const wchar_t* text, int x, int y, int w, int h, DWORD extra = ES_AUTOHSCROLL);
HWND regularMakeButton(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h);
HWND regularMakeCombo(HWND parent, const wchar_t* text, int x, int y, int w, int h);
HWND regularMakeDatePicker(HWND parent, int x, int y, int w, int h, const wchar_t* format, const SYSTEMTIME* value, int id = 0);
HWND regularAddClipSiblings(HWND hwnd);

SYSTEMTIME regularTodayDate();
SYSTEMTIME regularNormalizeDate(SYSTEMTIME st);
SYSTEMTIME regularAddDays(SYSTEMTIME st, int days);

std::string regularDatePickerValue(HWND hwnd);
SYSTEMTIME regularDatePickerSystemTime(HWND hwnd);
std::string regularSlashDate(const std::string& value);
std::string regularSlashDateTimeMinute(const std::string& value);
bool regularParseDateTimeText(const std::string& value, SYSTEMTIME& out);

void regularSetControlText(HWND hwnd, const std::string& text);
std::wstring regularWindowText(HWND hwnd);
void regularSetComboSingleText(HWND hwnd, const std::string& text);
AgeDisplayParts regularSplitAgeDisplayText(const std::string& text);
void regularFillAgeUnitCombo(HWND combo, const std::string& selectedUnit = "岁");
void regularSetDatePickerValue(HWND hwnd, const std::string& text);
void regularClearDatePickerValue(HWND hwnd);
void regularSetControlsEnabled(bool enabled, std::initializer_list<HWND> controls);

void regularApplyFont(HWND hwnd, HFONT font);
HFONT regularCreateBoldFont(HFONT base);
void regularRefreshLeftGroupTitleFont(RegularReportState* st);
int regularTextLogicalWidth(HWND hwnd, HFONT font, const wchar_t* text);
int regularFontLogicalHeight(HWND hwnd, HFONT font);
int regularWrappedTextHeightPx(HWND hwnd, HFONT font, const wchar_t* text, int widthPx, int minHeightPx);
RightHeaderLayout regularRightHeaderLayout(HWND hwnd, HFONT font, int panelWidthPx,
                                            const std::wstring& summaryLine1,
                                            const std::wstring& summaryLine2);
int regularLeftLabelWidth(HWND hwnd, HFONT font);
int regularRightLabelWidth(HWND hwnd, HFONT font);

// combos
void regularComboReset(HWND combo);
void regularComboAdd(HWND combo, const std::wstring& text);
void regularComboSelectFirst(HWND combo);

std::wstring regularRightSummaryLine1(const RegularReportState* st);
std::wstring regularRightSummaryLine2(const RegularReportState* st);

const wchar_t* regularQuickMachineCodeKey(int slot);
const wchar_t* regularQuickMachineNameKey(int slot);
const wchar_t* regularQuickMachineRoomKey(int slot);
bool regularQuickMachineMatchesCurrent(const RegularReportState* st, int slot);
void regularUpdateQuickMachineButtonLabels(RegularReportState* st);
bool regularIsAutoRefreshChecked(const RegularReportState* st);
int regularAutoRefreshSeconds(const RegularReportState* st);
void regularStopAutoRefreshTimer(RegularReportState* st);
void regularUpdateAutoRefreshTimer(RegularReportState* st);

// panels
void regularCreateControls(HWND hwnd, RegularReportState* st);
void regularLayout(HWND hwnd, RegularReportState* st);
void regularClearLeftPanel(RegularReportState* st);
void regularSeedLists(RegularReportState* st);

// picture
void regularClearPictureView(RegularReportState* st, const std::wstring& status);
void regularQuerySelectedPicture(RegularReportState* st, int selected);
void regularUpdatePictureViewport(RegularReportState* st);
void regularOpenPicturePopupForSelection(RegularReportState* st);
void regularScrollPictureViewport(RegularReportState* st, int targetX, int targetY);

// queries
void regularRunReportQuery(RegularReportState* st, bool preserveState = false);
void regularRunAutoRefreshQuery(RegularReportState* st);
void regularQuerySelectedResults(RegularReportState* st, int selected);

// data
void regularPopulateLeftPanelFromReport(RegularReportState* st, int selected);
void regularSortReportRowsByColumn(RegularReportState* st, int column);
void regularBeginResultEdit(RegularReportState* st, int row);
void regularFinishResultEdit(RegularReportState* st, bool commit, bool moveNext = false,
                              bool restoreListFocus = true);
void regularSelectReportRow(RegularReportState* st, int index);
void regularSelectAdjacentReportRow(RegularReportState* st, int delta);
bool regularHasSelectedReportRow(const RegularReportState* st);
int regularCurrentReportIndex(const RegularReportState* st);
void regularShowReportContextMenu(RegularReportState* st, const NMITEMACTIVATE* item);
std::vector<int> regularCheckedReportIndexes(const RegularReportState* st);
void regularClearReportChecks(RegularReportState* st);
const search::ReportRow* regularContextReportRow(const RegularReportState* st);

// barcode
std::wstring regularPrintBarcodeForContext(RegularReportState* st);
std::wstring regularPrintCheckedBarcodes(RegularReportState* st);
void regularShowTrendForContext(RegularReportState* st);

// queries
search::QueryInput regularBuildReportQueryInput(RegularReportState* st);
bool regularInspectDateMatchesCurrentQuery(const RegularReportState* st);
void regularSetInspectDateAndQuery(RegularReportState* st, SYSTEMTIME date,
                                   bool preserveWhenPossible = false);
void regularApplyQuickMachine(RegularReportState* st, int slot);
void regularFinishReportQuery(RegularReportState* st, HWND hwnd,
                               std::unique_ptr<ReportLoadResult> result);
void regularFinishResultQuery(RegularReportState* st, HWND hwnd,
                               std::unique_ptr<ResultLoadResult> result);
void regularFinishPictureQuery(RegularReportState* st, HWND hwnd,
                                std::unique_ptr<PictureLoadResult> result);
void regularPresentResultRows(RegularReportState* st);
void regularPresentReportRows(RegularReportState* st,
                               const std::vector<search::ReportRow>* previousRows = nullptr);

// color helpers (used by panels' NM_CUSTOMDRAW)
bool regularReportIsSent(const search::ReportRow& report);
bool regularReportIsReviewed(const search::ReportRow& report);
bool regularReportIsCriticalReport(const search::ReportRow& report);
bool regularReportUsesEmergencyTextColor(const search::ReportRow& report);
bool regularReportHasBarcodeEmergencyLabel(const search::ReportRow& report);
COLORREF regularReportRowColor(const RegularReportState* st, int row);
COLORREF regularReportPrintCellColor(const RegularReportState* st, int row);
COLORREF regularResultTextColor(const search::ResultRow& row);
bool regularResultRowHasCriticalValue(const search::ResultRow& row);
bool regularListViewRowSelected(HWND list, int row);
bool regularCustomDrawListSelection(NMLVCUSTOMDRAW* cd, HWND list, int row);
void regularRedrawSelectedListRow(HWND list);

// picture sub-procs (defined in picture.cpp, used by panels)
LRESULT CALLBACK pictureViewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                 UINT_PTR subclassId, DWORD_PTR data);
LRESULT CALLBACK pictureViewportProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                     UINT_PTR subclassId, DWORD_PTR data);
int regularScrollTargetFromCode(HWND hwnd, int code, const SCROLLINFO& si, int current);

// sample input
void regularSelectReportRowBySampleInput(RegularReportState* st);

#endif
