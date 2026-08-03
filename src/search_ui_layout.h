#pragma once

#ifdef _WIN32

#include <windows.h>

namespace search {

struct MainUiIds {
    int patient_id;
    int barcode;
    int name;
    int patient_no;
    int oper;
    int start;
    int end;
    int room;
    int mach;
    int group;
    int item;
    int patient_type;
    int report_status;
    int reports;
    int results;
    int splitter;
    int settings;
    int query;
    int trend;
    int export_;
    int preview;
    int print;
    int exit;
    int status;
};

struct MainUiHandles {
    HWND patient_id = nullptr;
    HWND barcode = nullptr;
    HWND name = nullptr;
    HWND patient_no = nullptr;
    HWND oper = nullptr;
    HWND start = nullptr;
    HWND end = nullptr;
    HWND room = nullptr;
    HWND patient_type = nullptr;
    HWND report_status = nullptr;
    HWND mach = nullptr;
    HWND group = nullptr;
    HWND item = nullptr;
    HWND reports = nullptr;
    HWND results = nullptr;
    HWND splitter = nullptr;
    HWND status = nullptr;
    HWND group_test = nullptr;
    HWND group_patient = nullptr;
    HWND settings_button = nullptr;
    HWND query_button = nullptr;
    HWND trend_button = nullptr;
    HWND export_button = nullptr;
    HWND preview_button = nullptr;
    HWND print_button = nullptr;
    HWND exit_button = nullptr;
};

float dpi_scale_factor(HWND hwnd);
int measure_control_text_width(HWND parent, HWND control, int minimumLogicalWidth,
                               int horizontalPaddingLogical = 12);

int clamp_font_size(int value);
HFONT create_ui_font(int pointSize);
void apply_font_to_children(HWND root, HFONT font);

HWND create_label(HWND parent, const wchar_t* text, int x, int y, int w, int h);
HWND create_groupbox(HWND parent, const wchar_t* text, int x, int y, int w, int h);
HWND create_edit(HWND parent, int id, int x, int y, int w, int h);
HWND create_date_picker(HWND parent, int id, int x, int y, int w, int h);
HWND create_combo(HWND parent, int id, int x, int y, int w, int h, bool editable);
HWND create_password_edit(HWND parent, int id, int x, int y, int w, int h);
HWND create_button(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h);
void add_list_column(HWND list, int index, const wchar_t* title, int width);

void create_main_controls(HWND hwnd, HFONT font, const MainUiIds& ids, MainUiHandles& ui);
void layout_main_window(HWND hwnd, MainUiHandles& ui, int& splitter_x);

}  // namespace search

#define REGISTER_MDI_CHILD_CLASS(inst, wndProc, clsName, bg)                                               \
    do {                                                                                                  \
        static bool _registered = false;                                                                  \
        if (!_registered) {                                                                               \
            WNDCLASSEXW _wc{};                                                                            \
            _wc.cbSize = sizeof(_wc);                                                                     \
            _wc.lpfnWndProc = (wndProc);                                                                  \
            _wc.hInstance   = (inst);                                                                     \
            _wc.hIcon       = LoadIconW((inst), MAKEINTRESOURCEW(IDI_APP));                               \
            _wc.hCursor     = LoadCursor(nullptr, IDC_ARROW);                                             \
            _wc.hbrBackground = (bg);                                                                      \
            _wc.lpszClassName = (clsName);                                                                \
            RegisterClassExW(&_wc);                                                                        \
            _registered = true;                                                                           \
        }                                                                                                 \
    } while (0)

#endif
