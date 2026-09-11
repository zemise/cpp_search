#pragma once

#ifdef _WIN32

#include "module_registry.h"

#include <string>

constexpr UINT WM_BLOOD_OPEN_REQUEST = WM_APP + 65;

// WM_BLOOD_OPEN_REQUEST 接收方负责释放通过 LPARAM 传入的目标对象。
struct BloodRequestOpenTarget {
    std::string apply_form_no;
    std::string apply_time;
};

HWND create_blood_module(const ModuleContext& ctx);

#endif
