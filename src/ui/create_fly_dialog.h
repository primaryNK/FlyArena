#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace flyarena {

// Modal v1 editor. Saving produces only a validated data-only .flypack.
bool show_create_fly_dialog(
    HWND owner,
    std::string& saved_path,
    std::string& error);

} // namespace flyarena
