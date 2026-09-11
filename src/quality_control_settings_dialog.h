#pragma once

#ifdef _WIN32

#include "app_settings.h"

#include <windows.h>

HWND create_quality_control_settings_panel(HWND parent, HFONT font, const search::DbSettings& db_settings);
void update_quality_control_settings_panel_db(HWND panel, const search::DbSettings& db_settings);

#endif
