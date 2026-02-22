#pragma once

#include "defines.h"
#include "strings/kname.h"

typedef void (*PFN_texture_browser_texture_selected_callback)(kname asset_name, kname package_name);
typedef void (*PFN_texture_browser_cancel_callback)(void);

KAPI void texture_browser_open(PFN_texture_browser_texture_selected_callback selected_callback, PFN_texture_browser_cancel_callback cancel_callback);
