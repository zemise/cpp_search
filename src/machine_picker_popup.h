#pragma once

#ifdef _WIN32

#include "app_settings.h"
#include "search_core.h"

#include <functional>
#include <string>

#include <windows.h>

namespace search {

struct MachinePickerPopupOptions {
    HWND owner = nullptr;
    HWND anchor = nullptr;
    HFONT font = nullptr;
    DbSettings db_settings;
    std::string current_room_code;
    std::string current_mach_code;
    bool include_all_rooms = true;
    std::function<void(const MachineOption&)> on_accept;
};

void show_machine_picker_popup(const MachinePickerPopupOptions& options);

}  // namespace search

#endif
