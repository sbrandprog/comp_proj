#pragma once
#include "wa.h"

#define WA_ACCEL_PROP_NAME L"wa_accel"

WA_API bool wa_accel_set(HWND hw, HACCEL ha);
WA_API HACCEL wa_accel_get(HWND hw);
WA_API void wa_accel_remove(HWND hw);

WA_API bool wa_accel_load(HWND hw, size_t accels_size, const ACCEL * accels);
WA_API void wa_accel_unload(HWND hw);
