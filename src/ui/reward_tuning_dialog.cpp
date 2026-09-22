#include "reward_tuning_dialog.h"

#include "runtime_tuning.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

namespace flyarena {
namespace {

constexpr int kSliderBase = 300;
constexpr int kApply = 400;
constexpr int kCancel = 401;

struct RewardDialogState {
    HWND window = nullptr;
    RuntimeTuning* runtime = nullptr;
    RewardTuning values{};
    std::array<HWND, 5> sliders{};
    std::array<HWND, 5> labels{};
    bool applied = false;
};

HWND control(
    HWND parent,
    const wchar_t* class_name,
    const wchar_t* text,
    DWORD style,
    int x, int y, int width, int height,
    int id)
{
    HWND result = CreateWindowExW(
        0, class_name, text, style | WS_CHILD | WS_VISIBLE,
        x, y, width, height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(
        result, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return result;
}

void refresh(RewardDialogState& state) {
    float* destinations[] = {
        &state.values.damage_penalty_scale,
        &state.values.attack_success_scale,
        &state.values.defense_success_scale,
        &state.values.terminal_scale,
        &state.values.engagement_scale
    };
    for (size_t i = 0; i < state.sliders.size(); ++i) {
        *destinations[i] = static_cast<float>(
            SendMessageW(state.sliders[i], TBM_GETPOS, 0, 0)) / 100.0f;
        wchar_t value[32]{};
        swprintf_s(value, L"%.2f", static_cast<double>(*destinations[i]));
        SetWindowTextW(state.labels[i], value);
    }
}

void create_controls(RewardDialogState& state) {
    control(
        state.window, L"STATIC",
        L"TRAINING REWARD PREFERENCES  (gameplay assumptions)",
        SS_LEFT, 18, 18, 500, 22, 0);
    control(
        state.window, L"STATIC",
        L"Raw positive and negative reward budgets are automatically EWMA-balanced.\r\n"
        L"Final per-tick reward is capped to ±30 to prevent runaway updates.",
        SS_LEFT, 18, 42, 500, 42, 0);

    const wchar_t* names[] = {
        L"Own HP loss penalty",
        L"HIT / damage success",
        L"BLOCK / PARRY / DODGE success",
        L"Victory / defeat",
        L"Close-engagement shaping"
    };
    const float initial[] = {
        state.values.damage_penalty_scale,
        state.values.attack_success_scale,
        state.values.defense_success_scale,
        state.values.terminal_scale,
        state.values.engagement_scale
    };
    for (size_t i = 0; i < state.sliders.size(); ++i) {
        const int y = 96 + static_cast<int>(i) * 58;
        control(
            state.window, L"STATIC", names[i], SS_LEFT,
            18, y + 5, 215, 22, 0);
        state.sliders[i] = control(
            state.window, TRACKBAR_CLASSW, L"",
            WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ,
            235, y, 220, 36, kSliderBase + static_cast<int>(i));
        SendMessageW(
            state.sliders[i], TBM_SETRANGE, TRUE, MAKELPARAM(25, 200));
        SendMessageW(
            state.sliders[i], TBM_SETPOS, TRUE,
            static_cast<LPARAM>(std::lround(initial[i] * 100.0f)));
        state.labels[i] = control(
            state.window, L"STATIC", L"1.00", SS_RIGHT,
            462, y + 5, 48, 22, 0);
    }

    control(
        state.window, L"BUTTON", L"Apply",
        WS_TABSTOP | BS_DEFPUSHBUTTON,
        330, 397, 85, 32, kApply);
    control(
        state.window, L"BUTTON", L"Cancel",
        WS_TABSTOP | BS_PUSHBUTTON,
        425, 397, 85, 32, kCancel);
    refresh(state);
}

LRESULT CALLBACK reward_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam)
{
    auto* state = reinterpret_cast<RewardDialogState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state = static_cast<RewardDialogState*>(create->lpCreateParams);
        state->window = window;
        SetWindowLongPtrW(
            window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(state));
    }
    if (!state)
        return DefWindowProcW(window, message, wparam, lparam);

    switch (message) {
    case WM_CREATE:
        create_controls(*state);
        return 0;
    case WM_HSCROLL:
        refresh(*state);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == kApply && HIWORD(wparam) == BN_CLICKED) {
            refresh(*state);
            state->runtime->set_reward_tuning(state->values);
            state->applied = true;
            DestroyWindow(window);
            return 0;
        }
        if (LOWORD(wparam) == kCancel && HIWORD(wparam) == BN_CLICKED) {
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

bool show_reward_tuning_dialog(HWND owner, RuntimeTuning& runtime) {
    INITCOMMONCONTROLSEX common{};
    common.dwSize = sizeof(common);
    common.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&common);

    constexpr const wchar_t* class_name =
        L"FlyArenaRewardTuningDialogV1";
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = reward_proc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    cls.lpszClassName = class_name;
    if (!RegisterClassExW(&cls)
        && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return false;
    }

    RewardDialogState state;
    state.runtime = &runtime;
    state.values = runtime.reward_tuning();
    RECT owner_rect{};
    GetWindowRect(owner, &owner_rect);
    constexpr int width = 550;
    constexpr int height = 490;
    const int x = owner_rect.left
        + std::max(0L, (owner_rect.right - owner_rect.left - width) / 2);
    const int y = owner_rect.top
        + std::max(0L, (owner_rect.bottom - owner_rect.top - height) / 2);

    HWND window = CreateWindowExW(
        WS_EX_DLGMODALFRAME, class_name,
        L"Training Reward Tuning — automatic ± balance",
        WS_CAPTION | WS_SYSMENU | WS_POPUP,
        x, y, width, height,
        owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!window)
        return false;

    EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    MSG message{};
    while (IsWindow(window)) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0)
            break;
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    return state.applied;
}

} // namespace flyarena
