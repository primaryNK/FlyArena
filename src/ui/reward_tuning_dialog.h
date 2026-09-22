#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace flyarena {

struct RuntimeTuning;

bool show_reward_tuning_dialog(HWND owner, RuntimeTuning& runtime);

} // namespace flyarena
