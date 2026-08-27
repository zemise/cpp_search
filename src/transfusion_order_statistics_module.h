#pragma once

#ifdef _WIN32
#include "module_registry.h"

HWND create_transfusion_order_statistics_module(const ModuleContext& ctx);
#endif
