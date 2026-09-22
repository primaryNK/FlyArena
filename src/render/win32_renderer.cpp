#include "win32_renderer.h"
#ifdef FLYARENA_PRODUCT_UI
#include "create_fly_dialog.h"
#include "reward_tuning_dialog.h"
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")

namespace flyarena {

namespace {

template <typename T>
void safe_release(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    const int needed = MultiByteToWideChar(
        CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
        nullptr, 0);
    if (needed <= 0)
        return std::wstring(s.begin(), s.end());

    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
        out.data(), needed);
    return out;
}

std::string wide_to_utf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int needed = WideCharToMultiByte(
        CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
        nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
        return {};

    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
        out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring wrap_path_for_hud(const std::string& path) {
    std::wstring out = utf8_to_wide(path);
    try {
        const std::filesystem::path absolute(out);
        std::error_code ec;
        const std::filesystem::path relative =
            std::filesystem::relative(
                absolute, std::filesystem::current_path(), ec);
        if (!ec && !relative.empty()) {
            const std::wstring candidate = relative.wstring();
            if (candidate != L".."
                && candidate.rfind(L"..\\", 0) != 0
                && candidate.rfind(L"../", 0) != 0)
            {
                out = candidate;
            }
        }
    }
    catch (...) {
        // Display fallback only; never change the canonical save path.
    }

    // Give DirectWrite legal wrap points without changing the real path.
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] == L'\\' || out[i] == L'/') {
            out.insert(i + 1, 1, L'\x200b');
            ++i;
        }
    }
    return out;
}

float clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

D2D1_COLOR_F color(float r, float g, float b, float a = 1.0f) {
    return D2D1::ColorF(r, g, b, a);
}

enum class SoundEffect : size_t { Hit, Block, Parry, Dodge, Wall, Count };

void append_u16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xffu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xffu));
}

void append_u32(std::vector<uint8_t>& out, uint32_t value) {
    append_u16(out, static_cast<uint16_t>(value & 0xffffu));
    append_u16(out, static_cast<uint16_t>((value >> 16) & 0xffffu));
}

std::vector<uint8_t> make_sound_effect(SoundEffect effect) {
    constexpr uint32_t sample_rate = 22050;
    const float duration = effect == SoundEffect::Wall ? 0.48f : 0.20f;
    const uint32_t samples = static_cast<uint32_t>(sample_rate * duration);
    std::vector<int16_t> pcm(samples);
    uint32_t noise_state = 0x13579bdu;
    constexpr float two_pi = 6.28318530717958647692f;
    for (uint32_t i = 0; i < samples; ++i) {
        const float t = static_cast<float>(i) / sample_rate;
        const float x = t / duration;
        const float envelope = std::max(0.0f, 1.0f - x);
        noise_state = noise_state * 1664525u + 1013904223u;
        const float noise =
            (static_cast<float>((noise_state >> 9) & 0x7fffu) / 16383.5f) - 1.0f;
        float sample = 0.0f;
        switch (effect) {
        case SoundEffect::Hit:
            sample = 0.58f * noise * envelope * envelope
                + 0.42f * std::sin(two_pi * (210.0f - 90.0f * x) * t) * envelope;
            break;
        case SoundEffect::Block:
            sample = std::sin(two_pi * 115.0f * t) * envelope * envelope
                + 0.20f * noise * envelope;
            break;
        case SoundEffect::Parry:
            sample = (0.55f * std::sin(two_pi * 760.0f * t)
                + 0.35f * std::sin(two_pi * 1210.0f * t)) * envelope;
            break;
        case SoundEffect::Dodge:
            sample = 0.50f * noise * std::sin(3.14159265f * x)
                + 0.30f * std::sin(two_pi * (260.0f + 900.0f * x) * t) * envelope;
            break;
        case SoundEffect::Wall: {
            const float hum = 0.62f * std::sin(two_pi * 72.0f * t)
                + 0.38f * std::sin(two_pi * 79.0f * t);
            sample = hum * envelope * (0.72f + 0.28f * std::sin(two_pi * 7.0f * t));
            break;
        }
        case SoundEffect::Count:
            break;
        }
        pcm[i] = static_cast<int16_t>(
            std::clamp(sample * 14500.0f, -32767.0f, 32767.0f));
    }

    std::vector<uint8_t> wav;
    wav.reserve(44 + pcm.size() * sizeof(int16_t));
    const auto tag = [&](const char* value) {
        wav.insert(wav.end(), value, value + 4);
    };
    tag("RIFF"); append_u32(wav, 36u + samples * 2u); tag("WAVE");
    tag("fmt "); append_u32(wav, 16); append_u16(wav, 1); append_u16(wav, 1);
    append_u32(wav, sample_rate); append_u32(wav, sample_rate * 2u);
    append_u16(wav, 2); append_u16(wav, 16);
    tag("data"); append_u32(wav, samples * 2u);
    for (int16_t value : pcm)
        append_u16(wav, static_cast<uint16_t>(value));
    return wav;
}


constexpr float kVirtualWidth = 1280.0f;
constexpr float kVirtualHeight = 720.0f;

D2D1_MATRIX_3X2_F uniform_canvas_transform(
    float client_width,
    float client_height)
{
    const float scale =
        std::max(
            0.01f,
            std::min(
                client_width / kVirtualWidth,
                client_height / kVirtualHeight));

    const float offset_x =
        (client_width - kVirtualWidth * scale) * 0.5f;
    const float offset_y =
        (client_height - kVirtualHeight * scale) * 0.5f;

    D2D1_MATRIX_3X2_F m{};
    m._11 = scale;
    m._12 = 0.0f;
    m._21 = 0.0f;
    m._22 = scale;
    m._31 = offset_x;
    m._32 = offset_y;
    return m;
}

struct PreviewEvent {
    std::wstring label;
    D2D1_COLOR_F color{};
    std::chrono::steady_clock::time_point start{};
    float duration_s = 0.80f;
    float direction = 0.0f;
    bool has_world_position = false;
    float world_x = 0.0f;
    float world_y = 0.0f;
};

} // namespace

struct ArenaRenderer::Impl
    #ifdef FLYARENA_PRODUCT_UI
    : IDropTarget
    #endif
{
    SnapshotBuffer& snapshots;
    RuntimeTuning& tuning;
    RendererConfig config;

    HWND hwnd = nullptr;

    ID2D1Factory* d2d_factory = nullptr;
    IDWriteFactory* dwrite_factory = nullptr;
    ID2D1HwndRenderTarget* target = nullptr;

    ID2D1SolidColorBrush* bg_brush = nullptr;
    ID2D1SolidColorBrush* panel_brush = nullptr;
    ID2D1SolidColorBrush* grid_brush = nullptr;
    ID2D1SolidColorBrush* white_brush = nullptr;
    ID2D1SolidColorBrush* muted_brush = nullptr;
    ID2D1SolidColorBrush* red_brush = nullptr;
    ID2D1SolidColorBrush* blue_brush = nullptr;
    ID2D1SolidColorBrush* wing_brush = nullptr;
    ID2D1SolidColorBrush* arena_brush = nullptr;
    ID2D1SolidColorBrush* accent_brush = nullptr;
    ID2D1SolidColorBrush* outline_brush = nullptr;
    ID2D1SolidColorBrush* eye_brush = nullptr;
    ID2D1SolidColorBrush* eye_highlight_brush = nullptr;
    ID2D1SolidColorBrush* red_soft_brush = nullptr;
    ID2D1SolidColorBrush* blue_soft_brush = nullptr;
    ID2D1SolidColorBrush* amber_brush = nullptr;
    ID2D1SolidColorBrush* blade_brush = nullptr;
    ID2D1StrokeStyle* drop_stroke = nullptr;

    IDWriteTextFormat* title_format = nullptr;
    IDWriteTextFormat* name_format = nullptr;
    IDWriteTextFormat* hud_format = nullptr;
    IDWriteTextFormat* small_format = nullptr;
    IDWriteTextFormat* path_format = nullptr;
    IDWriteTextFormat* event_format = nullptr;

    std::deque<PreviewEvent> preview_events;
    float next_dodge_direction = 1.0f;
    uint64_t last_combat_event_id = 0;
    uint64_t last_wall_sound_sequence = 0;
    std::chrono::steady_clock::time_point last_wall_sound_at{};
    std::array<std::vector<uint8_t>,
        static_cast<size_t>(SoundEffect::Count)> sound_effects{};
    bool show_debug = false;
    std::wstring ui_notice;
    std::chrono::steady_clock::time_point ui_notice_started{};

    std::chrono::steady_clock::time_point fps_epoch =
        std::chrono::steady_clock::now();
    uint64_t fps_frames = 0;
    double measured_fps = 0.0;

    bool initialized = false;

    #ifdef FLYARENA_PRODUCT_UI
    LONG drop_target_refs = 1;
    bool ole_initialized = false;
    bool ole_drop_registered = false;
    int drag_hover_slot = -1;
    bool drag_hover_valid = false;
    std::wstring drag_hover_path;
    std::wstring drag_hover_reason;
    #endif

    Impl(
        SnapshotBuffer& s,
        RuntimeTuning& t,
        const RendererConfig& c)
        : snapshots(s), tuning(t), config(c)
    {
        for (size_t i = 0; i < sound_effects.size(); ++i) {
            sound_effects[i] = make_sound_effect(
                static_cast<SoundEffect>(i));
        }
    }

    ~Impl() {
        // SND_MEMORY playback is asynchronous, so stop it before releasing
        // the in-memory WAV buffers owned by this renderer.
        PlaySoundA(nullptr, nullptr, 0);
        #ifdef FLYARENA_PRODUCT_UI
        if (ole_drop_registered && hwnd)
            RevokeDragDrop(hwnd);
        if (ole_initialized)
            OleUninitialize();
        #endif
        discard_device_resources();
        safe_release(title_format);
        safe_release(name_format);
        safe_release(hud_format);
        safe_release(small_format);
        safe_release(path_format);
        safe_release(event_format);
        safe_release(drop_stroke);
        safe_release(dwrite_factory);
        safe_release(d2d_factory);
    }

    static LRESULT CALLBACK wnd_proc(
        HWND hwnd,
        UINT msg,
        WPARAM wparam,
        LPARAM lparam)
    {
        Impl* self = reinterpret_cast<Impl*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (msg == WM_NCCREATE) {
            const auto* cs =
                reinterpret_cast<CREATESTRUCTW*>(lparam);
            self = static_cast<Impl*>(cs->lpCreateParams);
            SetWindowLongPtrW(
                hwnd, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(self));
            self->hwnd = hwnd;
        }

        if (self)
            return self->handle_message(msg, wparam, lparam);

        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    LRESULT handle_message(
        UINT msg,
        WPARAM wparam,
        LPARAM lparam)
    {
        switch (msg) {
        case WM_DESTROY:
            #ifdef FLYARENA_PRODUCT_UI
            if (ole_drop_registered) {
                RevokeDragDrop(hwnd);
                ole_drop_registered = false;
            }
            #endif
            tuning.quit_requested.store(true);
            PostQuitMessage(0);
            return 0;

        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize.x = 720;
            info->ptMinTrackSize.y = 405;
            return 0;
        }

        case WM_DPICHANGED: {
            const auto* suggested =
                reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(
                hwnd, nullptr,
                suggested->left, suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        case WM_SIZE:
            if (target) {
                const UINT w = LOWORD(lparam);
                const UINT h = HIWORD(lparam);
                if (w > 0 && h > 0)
                    target->Resize(D2D1::SizeU(w, h));
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_KEYDOWN:
            handle_key(wparam);
            return 0;

        #ifdef FLYARENA_PRODUCT_UI
        case WM_DROPFILES:
            handle_drop(reinterpret_cast<HDROP>(wparam));
            return 0;

        case WM_LBUTTONUP:
            handle_click(
                static_cast<int>(static_cast<short>(LOWORD(lparam))),
                static_cast<int>(static_cast<short>(HIWORD(lparam))));
            return 0;
        #endif
        }

        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    #ifdef FLYARENA_PRODUCT_UI
    D2D1_POINT_2F client_to_virtual(int client_x, int client_y) const {
        RECT rect{};
        GetClientRect(hwnd, &rect);
        const D2D1_MATRIX_3X2_F transform = uniform_canvas_transform(
            static_cast<float>(std::max<LONG>(1, rect.right - rect.left)),
            static_cast<float>(std::max<LONG>(1, rect.bottom - rect.top)));
        return D2D1::Point2F(
            (static_cast<float>(client_x) - transform._31) / transform._11,
            (static_cast<float>(client_y) - transform._32) / transform._22);
    }

    static bool point_in(
        D2D1_POINT_2F point,
        const D2D1_RECT_F& rect)
    {
        return point.x >= rect.left && point.x <= rect.right
            && point.y >= rect.top && point.y <= rect.bottom;
    }

    void set_notice(const std::wstring& notice) {
        ui_notice = notice;
        ui_notice_started = std::chrono::steady_clock::now();
    }

    void set_mode(AppMode mode) {
        if (tuning.app_mode.load() == mode)
            return;
        if (mode == AppMode::Training
            && tuning.training_submode.load()
               == TrainingSubmode::RandomTrainer
            && tuning.trainer_generation() == 0)
        {
            tuning.queue_trainer_randomize();
            tuning.repeat_matches.store(true);
        }
        tuning.app_mode.store(mode);
        tuning.mode_revision.fetch_add(1);
        tuning.paused.store(false);
        tuning.restart_revision.fetch_add(1);
        set_notice(
            mode == AppMode::Training
            ? L"TRAINING · fresh match · exploration and plastic updates ON"
            : L"BATTLE · fresh match · exploration, updates, and writes OFF");
    }

    uint32_t drop_slot_at(POINT client_point) const {
        const D2D1_POINT_2F point =
            client_to_virtual(client_point.x, client_point.y);
        if (point_in(
                point, D2D1::RectF(18.0f, 82.0f, 266.0f, 648.0f)))
            return 0;
        if (point_in(
                point, D2D1::RectF(1014.0f, 82.0f, 1262.0f, 648.0f)))
            return 1;
        return 2;
    }

    bool validate_drop(
        const std::wstring& wide_path,
        uint32_t slot,
        bool& flypack,
        std::wstring& reason) const
    {
        std::filesystem::path path(wide_path);
        std::wstring extension = path.extension().wstring();
        std::transform(
            extension.begin(), extension.end(), extension.begin(),
            [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });

        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        flypack = extension == L".flypack";
        const bool flytrain = extension == L".flytrain";
        const uintmax_t maximum_bytes = flypack
            ? 64u * 1024u : 4u * 1024u * 1024u;
        if ((!flypack && !flytrain) || ec || size > maximum_bytes) {
            reason = L"Expected .flypack (64 KiB) or .flytrain (4 MiB)";
            return false;
        }
        if (slot > 1) {
            reason = L"Move over the dotted left or right HUD";
            return false;
        }
        if (slot == 1
            && tuning.app_mode.load() == AppMode::Training
            && tuning.training_submode.load()
               == TrainingSubmode::RandomTrainer)
        {
            reason = L"Random Trainer owns the right slot";
            return false;
        }
        reason.clear();
        return true;
    }

    void queue_drop_path(
        const std::wstring& wide_path,
        POINT client_point)
    {
        const uint32_t slot = drop_slot_at(client_point);
        bool flypack = false;
        std::wstring reason;
        if (!validate_drop(wide_path, slot, flypack, reason)) {
            set_notice(L"Rejected: " + reason);
            return;
        }

        if (flypack)
            tuning.queue_flypack_load(slot, wide_to_utf8(wide_path));
        else
            tuning.queue_training_load(slot, wide_to_utf8(wide_path));
        tuning.paused.store(false);
        tuning.restart_revision.fetch_add(1);
        set_notice(
            slot == 0
            ? (flypack
               ? L"LEFT .flypack queued · validating before fresh match"
               : L"LEFT .flytrain queued · starting a fresh match")
            : (flypack
               ? L"RIGHT .flypack queued · validating before fresh match"
               : L"RIGHT .flytrain queued · starting a fresh match"));
    }

    void handle_drop(HDROP drop) {
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        POINT client_point{};
        const BOOL has_client_point = DragQueryPoint(drop, &client_point);

        if (count != 1 || !has_client_point) {
            set_notice(L"Drop exactly one .flypack or .flytrain onto a fly HUD");
            DragFinish(drop);
            return;
        }

        const UINT chars = DragQueryFileW(drop, 0, nullptr, 0);
        std::wstring wide_path(static_cast<size_t>(chars) + 1, L'\0');
        DragQueryFileW(drop, 0, wide_path.data(), chars + 1);
        wide_path.resize(chars);
        DragFinish(drop);
        queue_drop_path(wide_path, client_point);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID iid,
        void** object) override
    {
        if (!object)
            return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IDropTarget) {
            *object = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(
            InterlockedIncrement(&drop_target_refs));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        // Lifetime is owned by ArenaRenderer. COM registration only borrows
        // this object while the window/message loop is alive.
        return static_cast<ULONG>(
            InterlockedDecrement(&drop_target_refs));
    }

    static bool path_from_data_object(
        IDataObject* data,
        std::wstring& path)
    {
        if (!data)
            return false;
        FORMATETC format{
            CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium{};
        if (FAILED(data->GetData(&format, &medium)))
            return false;

        const HDROP drop = reinterpret_cast<HDROP>(medium.hGlobal);
        const UINT count = DragQueryFileW(
            drop, 0xFFFFFFFF, nullptr, 0);
        bool ok = false;
        if (count == 1) {
            const UINT chars = DragQueryFileW(drop, 0, nullptr, 0);
            path.assign(static_cast<size_t>(chars) + 1, L'\0');
            DragQueryFileW(drop, 0, path.data(), chars + 1);
            path.resize(chars);
            ok = true;
        }
        ReleaseStgMedium(&medium);
        return ok;
    }

    void update_drag_hover(POINTL screen_point, DWORD* effect) {
        POINT client_point{screen_point.x, screen_point.y};
        ScreenToClient(hwnd, &client_point);
        const uint32_t slot = drop_slot_at(client_point);
        bool flypack = false;
        std::wstring reason;
        const bool valid = !drag_hover_path.empty()
            && validate_drop(
                drag_hover_path, slot, flypack, reason);

        drag_hover_slot = slot <= 1 ? static_cast<int>(slot) : -1;
        drag_hover_valid = valid;
        drag_hover_reason = reason;
        if (effect)
            *effect = valid ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        if (valid) {
            set_notice(
                flypack
                ? L"Release to import .flypack identity + equipment"
                : L"Release to replace this slot's learned checkpoint");
        } else if (!reason.empty()) {
            set_notice(L"Cannot drop: " + reason);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    HRESULT STDMETHODCALLTYPE DragEnter(
        IDataObject* data,
        DWORD,
        POINTL point,
        DWORD* effect) override
    {
        drag_hover_path.clear();
        path_from_data_object(data, drag_hover_path);
        update_drag_hover(point, effect);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragOver(
        DWORD,
        POINTL point,
        DWORD* effect) override
    {
        update_drag_hover(point, effect);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override {
        drag_hover_slot = -1;
        drag_hover_valid = false;
        drag_hover_path.clear();
        drag_hover_reason.clear();
        InvalidateRect(hwnd, nullptr, FALSE);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(
        IDataObject* data,
        DWORD,
        POINTL point,
        DWORD* effect) override
    {
        std::wstring path;
        if (!path_from_data_object(data, path))
            path = drag_hover_path;
        POINT client_point{point.x, point.y};
        ScreenToClient(hwnd, &client_point);

        bool flypack = false;
        std::wstring reason;
        const uint32_t slot = drop_slot_at(client_point);
        const bool valid = validate_drop(
            path, slot, flypack, reason);
        if (effect)
            *effect = valid ? DROPEFFECT_COPY : DROPEFFECT_NONE;

        drag_hover_slot = -1;
        drag_hover_valid = false;
        drag_hover_path.clear();
        drag_hover_reason.clear();
        if (valid)
            queue_drop_path(path, client_point);
        else
            set_notice(L"Rejected: " + reason);
        InvalidateRect(hwnd, nullptr, FALSE);
        return S_OK;
    }

    void request_training_reset(uint32_t slot) {
        if (tuning.app_mode.load() != AppMode::Training) {
            set_notice(L"Learning reset is available in TRAINING mode only");
            return;
        }
        if (slot == 1
            && tuning.training_submode.load()
               == TrainingSubmode::RandomTrainer)
        {
            set_notice(
                L"Use RANDOMIZE TRAINER to reset learning and generate a new loadout");
            return;
        }
        if (slot == 1) {
            set_notice(
                L"Imported opponent is frozen · checkpoint replacement is allowed, reset/write is not");
            return;
        }

        const TrainingSlotUiState state = tuning.training_slot(slot);
        const std::wstring message =
            L"Reset all learned weights and overwrite this checkpoint?\n\n"
            + utf8_to_wide(state.checkpoint_path);
        const bool was_paused = tuning.paused.exchange(true);
        if (MessageBoxW(
                hwnd, message.c_str(), L"Reset Fly Learning",
                MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
        {
            tuning.paused.store(was_paused);
            return;
        }

        tuning.queue_training_reset(slot);
        tuning.paused.store(false);
        tuning.restart_revision.fetch_add(1);
        set_notice(
            slot == 0
            ? L"LEFT learning reset queued · starting fresh"
            : L"RIGHT learning reset queued · starting fresh");
    }

    void handle_click(int client_x, int client_y) {
        const D2D1_POINT_2F point =
            client_to_virtual(client_x, client_y);
        const D2D1_RECT_F create_rect =
            D2D1::RectF(286.0f, 14.0f, 402.0f, 44.0f);
        const D2D1_RECT_F training_rect =
            D2D1::RectF(412.0f, 14.0f, 516.0f, 44.0f);
        const D2D1_RECT_F battle_rect =
            D2D1::RectF(526.0f, 14.0f, 620.0f, 44.0f);
        const D2D1_RECT_F random_trainer_rect =
            D2D1::RectF(286.0f, 50.0f, 424.0f, 78.0f);
        const D2D1_RECT_F imported_opponent_rect =
            D2D1::RectF(434.0f, 50.0f, 590.0f, 78.0f);
        const D2D1_RECT_F randomize_trainer_rect =
            D2D1::RectF(600.0f, 50.0f, 780.0f, 78.0f);
        const D2D1_RECT_F play_rect =
            D2D1::RectF(430.0f, 674.0f, 512.0f, 704.0f);
        const D2D1_RECT_F pause_rect =
            D2D1::RectF(522.0f, 674.0f, 610.0f, 704.0f);
        const D2D1_RECT_F repeat_rect =
            D2D1::RectF(620.0f, 674.0f, 718.0f, 704.0f);
        const D2D1_RECT_F reset_rect =
            D2D1::RectF(728.0f, 674.0f, 816.0f, 704.0f);
        const D2D1_RECT_F reward_rect =
            D2D1::RectF(826.0f, 674.0f, 956.0f, 704.0f);
        const D2D1_RECT_F red_learning_reset_rect =
            D2D1::RectF(36.0f, 613.0f, 248.0f, 640.0f);
        const D2D1_RECT_F blue_learning_reset_rect =
            D2D1::RectF(1032.0f, 613.0f, 1244.0f, 640.0f);

        if (point_in(point, create_rect)) {
            const bool was_paused = tuning.paused.exchange(true);
            std::string saved_path;
            std::string error;
            const bool saved = show_create_fly_dialog(
                hwnd, saved_path, error);
            if (!was_paused)
                tuning.paused.store(false);

            if (saved) {
                set_notice(
                    L"Saved data-only fly: "
                    + utf8_to_wide(saved_path));
            } else if (!error.empty()) {
                set_notice(L"Create Fly failed: " + utf8_to_wide(error));
            }
            return;
        }
        if (point_in(point, training_rect)) {
            set_mode(AppMode::Training);
            return;
        }
        if (point_in(point, battle_rect)) {
            set_mode(AppMode::Battle);
            return;
        }
        if (point_in(point, random_trainer_rect)) {
            const bool changed =
                tuning.training_submode.load()
                != TrainingSubmode::RandomTrainer;
            tuning.activate_training_submode(
                TrainingSubmode::RandomTrainer);
            if (tuning.trainer_generation() == 0
                && tuning.app_mode.load() == AppMode::Training)
                tuning.queue_trainer_randomize();
            if (tuning.app_mode.load() != AppMode::Training) {
                set_mode(AppMode::Training);
            } else if (changed) {
                tuning.paused.store(false);
                tuning.restart_revision.fetch_add(1);
            }
            tuning.repeat_matches.store(true);
            set_notice(
                L"RANDOM TRAINER · both flies learn · automatic repeat ON");
            return;
        }
        if (point_in(point, imported_opponent_rect)) {
            const bool changed =
                tuning.training_submode.load()
                != TrainingSubmode::ImportedOpponent;
            tuning.activate_training_submode(
                TrainingSubmode::ImportedOpponent);
            if (tuning.app_mode.load() != AppMode::Training) {
                set_mode(AppMode::Training);
            } else if (changed) {
                tuning.paused.store(false);
                tuning.restart_revision.fetch_add(1);
            }
            tuning.repeat_matches.store(true);
            set_notice(
                L"IMPORTED OPPONENT · left learns · right reads frozen policy · repeat ON");
            return;
        }
        if (point_in(point, randomize_trainer_rect)) {
            if (tuning.app_mode.load() != AppMode::Training
                || tuning.training_submode.load()
                   != TrainingSubmode::RandomTrainer)
            {
                set_notice(
                    L"Randomize Trainer is available in RANDOM TRAINER Training");
                return;
            }
            tuning.queue_trainer_randomize();
            tuning.paused.store(false);
            tuning.restart_revision.fetch_add(1);
            set_notice(
                L"Trainer learning reset + new legal loadout queued · user preserved");
            return;
        }
        for (uint32_t option = 0;
             option < kTrainingSpeedOptionCount;
             ++option)
        {
            const float left = 790.0f + 34.0f * option;
            if (!point_in(
                    point,
                    D2D1::RectF(left, 50.0f, left + 31.0f, 78.0f)))
            {
                continue;
            }
            if (tuning.app_mode.load() != AppMode::Training) {
                set_notice(L"Simulation speed controls are Training-only");
                return;
            }
            tuning.training_speed_option.store(option);
            set_notice(
                option == 5
                ? L"Training speed MAX · headless learning · no wall-clock/GPU-duty pacing"
                : (L"Training speed "
                   + std::to_wstring(
                       static_cast<int>(training_speed_multiplier(option)))
                   + L"x · neural/world state preserved"));
            return;
        }
        if (point_in(point, play_rect)) {
            const SnapshotSample sample = snapshots.sample();
            tuning.paused.store(false);
            if (sample.valid
                && (sample.current.phase == ArenaPhase::Complete
                    || sample.current.phase == ArenaPhase::Failed))
            {
                tuning.restart_revision.fetch_add(1);
            }
            set_notice(L"PLAY");
            return;
        }
        if (point_in(point, pause_rect)) {
            tuning.paused.store(true);
            set_notice(L"PAUSED · simulation time is stopped");
            return;
        }
        if (point_in(point, repeat_rect)) {
            const bool enabled = !tuning.repeat_matches.load();
            tuning.repeat_matches.store(enabled);
            set_notice(enabled
                ? L"REPEAT ON · next match starts automatically"
                : L"REPEAT OFF");
            return;
        }
        if (point_in(point, reset_rect)) {
            tuning.paused.store(false);
            tuning.restart_revision.fetch_add(1);
            set_notice(L"Current match cancelled · starting a fresh match");
            return;
        }
        if (point_in(point, reward_rect)) {
            if (tuning.app_mode.load() != AppMode::Training) {
                set_notice(L"Reward tuning is available in Training only");
                return;
            }
            const bool was_paused = tuning.paused.exchange(true);
            const bool applied = show_reward_tuning_dialog(hwnd, tuning);
            tuning.paused.store(was_paused);
            set_notice(
                applied
                ? L"Reward preferences applied · automatic ± balance remains active"
                : L"Reward preferences unchanged");
            return;
        }
        if (point_in(point, red_learning_reset_rect)) {
            request_training_reset(0);
            return;
        }
        if (point_in(point, blue_learning_reset_rect)) {
            request_training_reset(1);
            return;
        }
    }
    #endif

    void handle_key(WPARAM key) {
        switch (key) {
        case VK_ESCAPE:
            DestroyWindow(hwnd);
            break;
        case VK_SPACE:
            tuning.paused.store(!tuning.paused.load());
            break;

        case VK_F1: config.target_fps = 60; break;
        case VK_F2: config.target_fps = 120; break;
        case VK_F3: config.target_fps = 144; break;
        case VK_F4: config.target_fps = 0; break;
        case VK_F10: show_debug = !show_debug; break;

        #ifdef FLYARENA_PRODUCT_UI
        case '1': set_mode(AppMode::Training); break;
        case '2': set_mode(AppMode::Battle); break;
        case 'R':
            tuning.paused.store(false);
            tuning.restart_revision.fetch_add(1);
            set_notice(L"Current match cancelled · starting a fresh match");
            break;
        case 'L': {
            const bool enabled = !tuning.repeat_matches.load();
            tuning.repeat_matches.store(enabled);
            set_notice(enabled ? L"REPEAT ON" : L"REPEAT OFF");
            break;
        }
        case VK_RETURN: {
            tuning.paused.store(false);
            const SnapshotSample sample = snapshots.sample();
            if (sample.valid
                && (sample.current.phase == ArenaPhase::Complete
                    || sample.current.phase == ArenaPhase::Failed))
            {
                tuning.restart_revision.fetch_add(1);
            }
            set_notice(L"PLAY");
            break;
        }
        #endif

        case VK_OEM_PLUS:
        case VK_ADD:
            tuning.sensitivity.store(std::min(
                4.0f, tuning.sensitivity.load() + 0.10f));
            break;
        case VK_OEM_MINUS:
        case VK_SUBTRACT:
            tuning.sensitivity.store(std::max(
                0.75f, tuning.sensitivity.load() - 0.10f));
            break;

        case VK_OEM_6: // ]
            tuning.forward_gain.store(std::min(
                4.0f, tuning.forward_gain.load() + 0.10f));
            break;
        case VK_OEM_4: // [
            tuning.forward_gain.store(std::max(
                0.10f, tuning.forward_gain.load() - 0.10f));
            break;

        case VK_OEM_7: // '
            tuning.turn_gain.store(std::min(
                4.0f, tuning.turn_gain.load() + 0.10f));
            break;
        case VK_OEM_1: // ;
            tuning.turn_gain.store(std::max(
                0.10f, tuning.turn_gain.load() - 0.10f));
            break;

        // Presentation-only preview. No combat outcome is generated.
        case 'H': add_preview(L"HIT!", color(1.00f, 0.40f, 0.20f)); break;
        case 'B': add_preview(L"BLOCK!", color(0.35f, 0.75f, 1.00f)); break;
        case 'P': add_preview(L"PARRY!", color(1.00f, 0.85f, 0.20f)); break;
        case 'D': add_preview(L"DODGE!", color(0.55f, 1.00f, 0.55f)); break;
        }
    }

    void add_preview(
        const wchar_t* label,
        D2D1_COLOR_F c)
    {
        PreviewEvent e;
        e.label = label;
        e.color = c;
        e.start = std::chrono::steady_clock::now();

        if (e.label == L"DODGE!") {
            e.direction = next_dodge_direction;
            next_dodge_direction *= -1.0f;
            e.duration_s = 0.72f;
        }

        preview_events.push_back(e);
        while (preview_events.size() > 5)
            preview_events.pop_front();
    }

    void consume_combat_events(const ArenaRenderSnapshot& s) {
        for(uint32_t i=0;i<s.combat_event_count;++i){
            const CombatEvent& ce=s.combat_events[i];
            if(ce.event_id<=last_combat_event_id) continue;
            last_combat_event_id=ce.event_id;
            PreviewEvent e; e.label=utf8_to_wide(combat_event_label(ce.type)); e.start=std::chrono::steady_clock::now(); e.direction=ce.direction; e.has_world_position=true; e.world_x=ce.world_x; e.world_y=ce.world_y;
            SoundEffect sound = SoundEffect::Hit;
            switch(ce.type){case CombatEventType::Hit:e.color=color(1.00f,.40f,.20f);sound=SoundEffect::Hit;break;case CombatEventType::Block:e.color=color(.35f,.75f,1.00f);sound=SoundEffect::Block;break;case CombatEventType::Parry:e.color=color(1.00f,.85f,.20f);sound=SoundEffect::Parry;break;case CombatEventType::Dodge:e.color=color(.55f,1.00f,.55f);e.duration_s=.72f;sound=SoundEffect::Dodge;break;}
            play_sound(sound);
            preview_events.push_back(e); while(preview_events.size()>8) preview_events.pop_front();
        }
    }

    void play_sound(SoundEffect effect) {
        const auto& wav = sound_effects[static_cast<size_t>(effect)];
        if (!wav.empty()) {
            PlaySoundA(
                reinterpret_cast<LPCSTR>(wav.data()), nullptr,
                SND_ASYNC | SND_MEMORY | SND_NODEFAULT);
        }
    }

    void consume_wall_sound(const ArenaRenderSnapshot& s) {
        if (s.sequence <= last_wall_sound_sequence)
            return;
        last_wall_sound_sequence = s.sequence;
        const float impact = std::max(
            s.red.last_wall_impact_speed,
            s.blue.last_wall_impact_speed);
        if (impact < 0.20f)
            return;
        const auto now = std::chrono::steady_clock::now();
        if (last_wall_sound_at.time_since_epoch().count() != 0
            && std::chrono::duration<double>(
                now - last_wall_sound_at).count() < 0.18)
        {
            return;
        }
        last_wall_sound_at = now;
        play_sound(SoundEffect::Wall);
    }

    bool initialize(HINSTANCE instance) {
        // Prevent Windows bitmap-DPI virtualization from blurring the vector UI.
        // If the call is unavailable/denied, Direct2D still works normally.
        SetProcessDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        const wchar_t* class_name =
            L"FlyArenaV063Window";

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &Impl::wnd_proc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = class_name;

        if (!RegisterClassExW(&wc)) {
            const DWORD e = GetLastError();
            if (e != ERROR_CLASS_ALREADY_EXISTS)
                return false;
        }

        RECT r{0, 0, config.width, config.height};
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

        hwnd = CreateWindowExW(
            0,
            class_name,
            #ifdef FLYARENA_PRODUCT_UI
            L"FlyArena v0.6.7 - Full-loop Accelerated Training / Battle",
            #else
            L"FlyArena v0.6.4 - Physical Combat Arena",
            #endif
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            r.right - r.left,
            r.bottom - r.top,
            nullptr, nullptr,
            instance,
            this);

        if (!hwnd)
            return false;

        #ifdef FLYARENA_PRODUCT_UI
        DragAcceptFiles(hwnd, TRUE);
        #endif

        HRESULT hr = D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            &d2d_factory);
        if (FAILED(hr))
            return false;

        D2D1_STROKE_STYLE_PROPERTIES drop_props =
            D2D1::StrokeStyleProperties();
        drop_props.dashStyle = D2D1_DASH_STYLE_DASH;
        if (FAILED(d2d_factory->CreateStrokeStyle(
                drop_props, nullptr, 0, &drop_stroke)))
        {
            return false;
        }

        hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&dwrite_factory));
        if (FAILED(hr))
            return false;

        auto make_format = [&](float size, DWRITE_FONT_WEIGHT weight,
                               IDWriteTextFormat** out) {
            return dwrite_factory->CreateTextFormat(
                L"Segoe UI",
                nullptr,
                weight,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                size,
                L"ko-KR",
                out);
        };

        if (FAILED(make_format(24.0f, DWRITE_FONT_WEIGHT_BOLD, &title_format)))
            return false;
        if (FAILED(make_format(30.0f, DWRITE_FONT_WEIGHT_BOLD, &name_format)))
            return false;
        if (FAILED(make_format(16.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &hud_format)))
            return false;
        if (FAILED(make_format(13.0f, DWRITE_FONT_WEIGHT_NORMAL, &small_format)))
            return false;
        if (FAILED(make_format(10.0f, DWRITE_FONT_WEIGHT_NORMAL, &path_format)))
            return false;
        if (FAILED(make_format(46.0f, DWRITE_FONT_WEIGHT_BLACK, &event_format)))
            return false;

        title_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        event_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        event_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);

        initialized = true;
        return true;
    }

    bool create_device_resources() {
        if (target)
            return true;

        RECT rc{};
        GetClientRect(hwnd, &rc);
        const UINT w = std::max<LONG>(1, rc.right - rc.left);
        const UINT h = std::max<LONG>(1, rc.bottom - rc.top);

        const auto props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_HARDWARE,
            D2D1::PixelFormat(
                DXGI_FORMAT_UNKNOWN,
                D2D1_ALPHA_MODE_UNKNOWN));

        const auto hwnd_props =
            D2D1::HwndRenderTargetProperties(
                hwnd,
                D2D1::SizeU(w, h),
                D2D1_PRESENT_OPTIONS_IMMEDIATELY);

        HRESULT hr = d2d_factory->CreateHwndRenderTarget(
            props, hwnd_props, &target);
        if (FAILED(hr))
            return false;

        auto brush = [&](D2D1_COLOR_F c, ID2D1SolidColorBrush** out) {
            return target->CreateSolidColorBrush(c, out);
        };

        if (FAILED(brush(color(0.040f, 0.050f, 0.065f), &bg_brush))) return false;
        if (FAILED(brush(color(0.070f, 0.085f, 0.110f, 0.96f), &panel_brush))) return false;
        if (FAILED(brush(color(0.18f, 0.21f, 0.27f, 0.80f), &grid_brush))) return false;
        if (FAILED(brush(color(0.93f, 0.95f, 0.98f), &white_brush))) return false;
        if (FAILED(brush(color(0.58f, 0.64f, 0.72f), &muted_brush))) return false;
        if (FAILED(brush(color(0.95f, 0.20f, 0.23f), &red_brush))) return false;
        if (FAILED(brush(color(0.20f, 0.50f, 1.00f), &blue_brush))) return false;
        if (FAILED(brush(color(0.80f, 0.88f, 0.98f, 0.30f), &wing_brush))) return false;
        if (FAILED(brush(color(0.10f, 0.12f, 0.16f), &arena_brush))) return false;
        if (FAILED(brush(color(0.38f, 0.92f, 0.66f), &accent_brush))) return false;
        if (FAILED(brush(color(0.018f, 0.024f, 0.036f, 0.98f), &outline_brush))) return false;
        if (FAILED(brush(color(0.18f, 0.055f, 0.075f, 1.0f), &eye_brush))) return false;
        if (FAILED(brush(color(1.00f, 0.88f, 0.90f, 0.95f), &eye_highlight_brush))) return false;
        if (FAILED(brush(color(1.00f, 0.39f, 0.42f, 1.0f), &red_soft_brush))) return false;
        if (FAILED(brush(color(0.33f, 0.65f, 1.00f, 1.0f), &blue_soft_brush))) return false;
        if (FAILED(brush(color(0.96f, 0.68f, 0.20f, 1.0f), &amber_brush))) return false;
        if (FAILED(brush(color(0.92f, 0.95f, 1.00f, 1.0f), &blade_brush))) return false;

        // High-quality vector/text rendering. Coordinates are kept at 96-DPI
        // physical-pixel mapping because the process itself is DPI aware.
        target->SetDpi(96.0f, 96.0f);
        target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

        return true;
    }

    void discard_device_resources() {
        safe_release(bg_brush);
        safe_release(panel_brush);
        safe_release(grid_brush);
        safe_release(white_brush);
        safe_release(muted_brush);
        safe_release(red_brush);
        safe_release(blue_brush);
        safe_release(wing_brush);
        safe_release(arena_brush);
        safe_release(accent_brush);
        safe_release(outline_brush);
        safe_release(eye_brush);
        safe_release(eye_highlight_brush);
        safe_release(red_soft_brush);
        safe_release(blue_soft_brush);
        safe_release(amber_brush);
        safe_release(blade_brush);
        safe_release(target);
    }

    D2D1_POINT_2F world_to_screen(
        const Vec2& p,
        float cx,
        float cy,
        float radius_px) const
    {
        const float scale =
            radius_px / std::max(0.001f, config.arena_radius_world);
        return D2D1::Point2F(
            cx + p.x * scale,
            cy - p.y * scale);
    }

    void text(
        const std::wstring& s,
        IDWriteTextFormat* format,
        ID2D1Brush* brush,
        const D2D1_RECT_F& rect)
    {
        target->DrawText(
            s.data(),
            static_cast<UINT32>(s.size()),
            format,
            rect,
            brush,
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    #ifdef FLYARENA_PRODUCT_UI
    void draw_top_button(
        const wchar_t* label,
        const D2D1_RECT_F& rect,
        bool active)
    {
        target->FillRoundedRectangle(
            D2D1::RoundedRect(rect, 7.0f, 7.0f),
            active ? accent_brush : panel_brush);
        target->DrawRoundedRectangle(
            D2D1::RoundedRect(rect, 7.0f, 7.0f),
            active ? white_brush : grid_brush,
            active ? 1.8f : 1.0f);
        small_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        small_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        text(
            label, small_format,
            active ? outline_brush : white_brush,
            rect);
        small_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        small_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }
    #endif

    void draw_bar(
        const wchar_t* label,
        float value,
        float max_value,
        float x,
        float y,
        float w,
        float h,
        ID2D1Brush* fill)
    {
        wchar_t buf[128]{};
        swprintf_s(
            buf, L"%ls  %.2f",
            label, static_cast<double>(value));

        text(
            buf, small_format, muted_brush,
            D2D1::RectF(x, y - 17.0f, x + w, y));

        const auto bg = D2D1::RectF(x, y, x + w, y + h);
        target->FillRectangle(bg, grid_brush);

        const float t =
            clamp01(value / std::max(0.001f, max_value));
        const auto fg = D2D1::RectF(
            x, y, x + w * t, y + h);
        target->FillRectangle(fg, fill);
    }

    void draw_integer_bar(
        const wchar_t* label,
        int32_t value,
        int32_t max_value,
        float x,
        float y,
        float w,
        float h,
        ID2D1Brush* fill)
    {
        wchar_t buf[128]{};
        swprintf_s(
            buf, L"%ls  %d / %d",
            label, value, max_value);

        text(
            buf, small_format, muted_brush,
            D2D1::RectF(x, y - 17.0f, x + w, y));

        const auto bg =
            D2D1::RectF(x, y, x + w, y + h);
        target->FillRectangle(bg, grid_brush);

        const float t =
            clamp01(
                static_cast<float>(value)
                / static_cast<float>(
                    std::max<int32_t>(1, max_value)));

        const auto fg =
            D2D1::RectF(
                x, y,
                x + w * t,
                y + h);
        target->FillRectangle(fg, fill);
    }

    void draw_signed_bar(
        const wchar_t* label,
        float value,
        float x,
        float y,
        float w,
        float h,
        ID2D1Brush* positive,
        ID2D1Brush* negative)
    {
        wchar_t buf[128]{};
        swprintf_s(
            buf, L"%ls  %+.2f",
            label, static_cast<double>(value));

        text(
            buf, small_format, muted_brush,
            D2D1::RectF(x, y - 17.0f, x + w, y));

        const auto bg = D2D1::RectF(x, y, x + w, y + h);
        target->FillRectangle(bg, grid_brush);

        const float mid = x + w * 0.5f;
        target->DrawLine(
            D2D1::Point2F(mid, y - 2.0f),
            D2D1::Point2F(mid, y + h + 2.0f),
            muted_brush, 1.0f);

        const float v = std::clamp(value, -1.0f, 1.0f);
        if (v >= 0.0f) {
            target->FillRectangle(
                D2D1::RectF(
                    mid, y,
                    mid + (w * 0.5f) * v,
                    y + h),
                positive);
        } else {
            target->FillRectangle(
                D2D1::RectF(
                    mid + (w * 0.5f) * v,
                    y, mid, y + h),
                negative);
        }
    }

    void draw_fly_model(
        const FlyRenderState& f,
        bool red_team,
        D2D1_POINT_2F center,
        float model_scale,
        float heading_deg,
        double render_seconds,
        bool portrait)
    {
        ID2D1Brush* team_soft =
            red_team ? red_soft_brush : blue_soft_brush;

        // Built-in data-only skins. IDs are validated by FlyProfile, and the
        // same FlyVisualLoadout reaches both this world model and HUD portrait.
        auto skin_brush = [](
            uint32_t id,
            ID2D1Brush* neutral,
            ID2D1Brush* warm,
            ID2D1Brush* cool) -> ID2D1Brush*
        {
            if (id == 1) return warm;
            if (id == 2) return cool;
            return neutral;
        };
        ID2D1Brush* body_skin = skin_brush(
            f.visual.body_skin_id, amber_brush, red_brush, blue_brush);
        ID2D1Brush* body_soft_skin = skin_brush(
            f.visual.body_skin_id, muted_brush, red_soft_brush, blue_soft_brush);
        ID2D1Brush* wing_skin = skin_brush(
            f.visual.wing_skin_id, wing_brush, amber_brush, accent_brush);
        ID2D1Brush* sword_skin = skin_brush(
            f.visual.sword_skin_id, blade_brush, amber_brush, accent_brush);
        ID2D1Brush* shield_skin = skin_brush(
            f.visual.shield_skin_id, team_soft, red_soft_brush, blue_soft_brush);

        auto unpack_color = [](uint32_t rgb, float alpha = 1.0f) {
            return D2D1::ColorF(
                static_cast<float>((rgb >> 16) & 0xffu) / 255.0f,
                static_cast<float>((rgb >> 8) & 0xffu) / 255.0f,
                static_cast<float>(rgb & 0xffu) / 255.0f,
                alpha);
        };
        ID2D1SolidColorBrush* body_primary = nullptr;
        ID2D1SolidColorBrush* body_secondary = nullptr;
        ID2D1LinearGradientBrush* body_gradient = nullptr;
        ID2D1LinearGradientBrush* wing_gradient = nullptr;
        ID2D1LinearGradientBrush* sword_gradient = nullptr;
        ID2D1LinearGradientBrush* shield_gradient = nullptr;

        target->CreateSolidColorBrush(
            unpack_color(f.visual.body_primary_color_rgb), &body_primary);
        target->CreateSolidColorBrush(
            unpack_color(f.visual.body_secondary_color_rgb), &body_secondary);

        auto make_gradient = [&](uint32_t first_rgb, uint32_t second_rgb,
                                 D2D1_POINT_2F start, D2D1_POINT_2F end,
                                 ID2D1LinearGradientBrush** out) {
            const D2D1_GRADIENT_STOP stops[] = {
                {0.0f, unpack_color(first_rgb)},
                {1.0f, unpack_color(second_rgb)}
            };
            ID2D1GradientStopCollection* collection = nullptr;
            if (SUCCEEDED(target->CreateGradientStopCollection(
                    stops, 2, &collection)))
            {
                target->CreateLinearGradientBrush(
                    D2D1::LinearGradientBrushProperties(start, end),
                    collection, out);
            }
            safe_release(collection);
        };
        make_gradient(
            f.visual.body_primary_color_rgb,
            f.visual.body_secondary_color_rgb,
            D2D1::Point2F(34.0f, -12.0f),
            D2D1::Point2F(-42.0f, 12.0f), &body_gradient);
        make_gradient(
            f.visual.wing_primary_color_rgb,
            f.visual.wing_secondary_color_rgb,
            D2D1::Point2F(12.0f, 0.0f),
            D2D1::Point2F(-48.0f, 28.0f), &wing_gradient);
        make_gradient(
            f.visual.sword_primary_color_rgb,
            f.visual.sword_secondary_color_rgb,
            D2D1::Point2F(2.0f, 0.0f),
            D2D1::Point2F(108.0f, 0.0f), &sword_gradient);
        make_gradient(
            f.visual.shield_primary_color_rgb,
            f.visual.shield_secondary_color_rgb,
            D2D1::Point2F(5.0f, -20.0f),
            D2D1::Point2F(42.0f, 20.0f), &shield_gradient);

        if (body_gradient) body_skin = body_gradient;
        if (body_secondary) body_soft_skin = body_secondary;
        if (wing_gradient) wing_skin = wing_gradient;
        if (sword_gradient) sword_skin = sword_gradient;
        if (shield_gradient) shield_skin = shield_gradient;

        const float visual_body =
            std::clamp(f.visual.body_scale, 0.65f, 1.55f);
        const float base_scale = model_scale * visual_body;
        const float wing_scale =
            std::clamp(f.visual.wing_size_scale, 0.55f, 1.85f);
        const float sword_length =
            std::clamp(f.visual.sword_length_scale, 0.55f, 2.00f);
        const float sword_thickness =
            std::clamp(f.visual.sword_thickness_scale, 0.55f, 1.80f);
        const float shield_scale =
            std::clamp(f.visual.shield_size_scale, 0.60f, 1.80f);

        const float flap =
            std::sin(
                static_cast<float>(
                    render_seconds
                    * (portrait ? 5.5 : (12.0 + 22.0 * f.forward))))
            * (portrait ? 5.0f : (10.0f + 12.0f * f.forward));

        D2D1_MATRIX_3X2_F canvas{};
        target->GetTransform(&canvas);

        auto set_local =
            [&](float angle_deg, float sx = 1.0f, float sy = 1.0f) {
                target->SetTransform(
                    D2D1::Matrix3x2F::Scale(
                        base_scale * sx,
                        base_scale * sy)
                    * D2D1::Matrix3x2F::Rotation(angle_deg)
                    * D2D1::Matrix3x2F::Translation(center.x, center.y)
                    * canvas);
            };

        // Shadow.
        set_local(heading_deg);
        target->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(-2.0f, 7.0f), 31.0f, 16.0f),
            grid_brush);

        // Cosmetic wing silhouettes. All variants use the same canonical
        // wing_size_scale for rendering and never alter actuator physics.
        for (float side : {-1.0f, 1.0f}) {
            set_local(
                heading_deg + side * flap * 0.38f,
                wing_scale, wing_scale);

            if (f.visual.wing_skin_id == 1) {
                // Ruby: long swept blade-wing with a strong leading vein.
                const D2D1_ELLIPSE swept = D2D1::Ellipse(
                    D2D1::Point2F(-11.0f, side * 20.0f),
                    34.0f, 9.0f);
                target->FillEllipse(swept, wing_skin);
                target->DrawEllipse(swept, white_brush, 1.5f);
                target->DrawLine(
                    D2D1::Point2F(5.0f, side * 13.0f),
                    D2D1::Point2F(-36.0f, side * 21.0f),
                    outline_brush, 1.4f);
            } else if (f.visual.wing_skin_id == 2) {
                // Azure: split/scalloped double-lobe wing.
                const D2D1_ELLIPSE inner = D2D1::Ellipse(
                    D2D1::Point2F(0.0f, side * 16.0f),
                    22.0f, 10.0f);
                const D2D1_ELLIPSE outer = D2D1::Ellipse(
                    D2D1::Point2F(-19.0f, side * 27.0f),
                    19.0f, 8.0f);
                target->FillEllipse(inner, wing_skin);
                target->FillEllipse(outer, wing_skin);
                target->DrawEllipse(inner, white_brush, 1.3f);
                target->DrawEllipse(outer, white_brush, 1.3f);
            } else if (f.visual.wing_skin_id == 3) {
                const D2D1_ELLIPSE narrow = D2D1::Ellipse(
                    D2D1::Point2F(-13.0f, side * 18.0f), 35.0f, 6.5f);
                target->FillEllipse(narrow, wing_skin);
                target->DrawEllipse(narrow, outline_brush, 1.6f);
            } else if (f.visual.wing_skin_id == 4) {
                const D2D1_ELLIPSE inner = D2D1::Ellipse(
                    D2D1::Point2F(1.0f, side * 15.0f), 20.0f, 9.0f);
                const D2D1_ELLIPSE fan = D2D1::Ellipse(
                    D2D1::Point2F(-25.0f, side * 23.0f), 24.0f, 12.0f);
                target->FillEllipse(inner, wing_skin);
                target->FillEllipse(fan, wing_skin);
                target->DrawEllipse(fan, white_brush, 1.2f);
            } else if (f.visual.wing_skin_id == 5) {
                for (int feather = 0; feather < 3; ++feather) {
                    const D2D1_ELLIPSE plume = D2D1::Ellipse(
                        D2D1::Point2F(-9.0f - feather * 11.0f,
                            side * (15.0f + feather * 6.0f)),
                        18.0f, 6.0f);
                    target->FillEllipse(plume, wing_skin);
                    target->DrawEllipse(plume, white_brush, 1.0f);
                }
            } else {
                const D2D1_ELLIPSE rounded = D2D1::Ellipse(
                    D2D1::Point2F(-7.0f, side * 18.0f),
                    27.0f, 13.0f);
                target->FillEllipse(rounded, wing_skin);
                target->DrawEllipse(rounded, white_brush, 1.3f);
            }
        }

        set_local(heading_deg);

        // Actual articulated sword pose. Damage is only possible while the
        // simulation reports an active powered swing.
        if (f.visual.show_sword) {
            const float sword_deg = f.sword_relative_angle * 180.0f / 3.14159265358979323846f;
            set_local(heading_deg + sword_deg);
            const float length = 54.0f * sword_length;
            if (f.visual.sword_skin_id == 1) {
                // Ruby sabre: curved presentation around the same base/tip.
                const D2D1_POINT_2F base = D2D1::Point2F(2.0f, 0.0f);
                const D2D1_POINT_2F middle = D2D1::Point2F(length * 0.56f, -4.5f);
                const D2D1_POINT_2F tip = D2D1::Point2F(length, 0.0f);
                target->DrawLine(base, middle, outline_brush, 6.0f * sword_thickness);
                target->DrawLine(middle, tip, outline_brush, 5.0f * sword_thickness);
                target->DrawLine(base, middle, sword_skin, 3.2f * sword_thickness);
                target->DrawLine(middle, tip, sword_skin, 2.5f * sword_thickness);
            } else if (f.visual.sword_skin_id == 3) {
                target->DrawLine(D2D1::Point2F(2.0f,0.0f),D2D1::Point2F(length,0.0f),outline_brush,8.0f*sword_thickness);
                target->DrawLine(D2D1::Point2F(2.0f,0.0f),D2D1::Point2F(length,0.0f),sword_skin,4.6f*sword_thickness);
            } else if (f.visual.sword_skin_id == 4) {
                target->DrawLine(D2D1::Point2F(2.0f,-2.5f),D2D1::Point2F(length,0.0f),sword_skin,2.2f*sword_thickness);
                target->DrawLine(D2D1::Point2F(2.0f,2.5f),D2D1::Point2F(length,0.0f),sword_skin,2.2f*sword_thickness);
            } else if (f.visual.sword_skin_id == 5) {
                target->DrawLine(D2D1::Point2F(2.0f,0.0f),D2D1::Point2F(length,0.0f),sword_skin,2.0f*sword_thickness);
                target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(length-5.0f,0.0f),5.0f,5.0f),sword_skin,2.0f);
            } else {
                target->DrawLine(D2D1::Point2F(2.0f, 0.0f),D2D1::Point2F(length,0.0f),outline_brush,6.0f*sword_thickness);
                target->DrawLine(D2D1::Point2F(2.0f, 0.0f),D2D1::Point2F(length,0.0f),sword_skin,(f.visual.sword_skin_id == 2 ? 2.2f : 3.2f)*sword_thickness);
                if (f.visual.sword_skin_id == 2) {
                    // Azure spear-tip ornament; canonical collision reach ends
                    // at the same `length` coordinate.
                    target->DrawLine(D2D1::Point2F(length,0.0f),D2D1::Point2F(length-9.0f,-5.0f),sword_skin,2.2f);
                    target->DrawLine(D2D1::Point2F(length,0.0f),D2D1::Point2F(length-9.0f,5.0f),sword_skin,2.2f);
                }
            }
            target->DrawLine(D2D1::Point2F(5.0f,-8.0f),D2D1::Point2F(5.0f,8.0f),amber_brush,4.0f);
        }

        if (f.visual.show_shield) {
            const float shield_deg = f.shield_relative_angle * 180.0f / 3.14159265358979323846f;
            set_local(heading_deg + shield_deg);
            const float shield_offset =
                14.5f
                * std::max(
                    0.0f,
                    f.shield_center_distance_body_radii);
            const float shield_rx =
                (f.visual.shield_skin_id == 1 ? 16.0f :
                 f.visual.shield_skin_id == 2 ? 11.0f : 13.0f)
                * shield_scale;
            const float shield_ry =
                (f.visual.shield_skin_id == 1 ? 16.0f :
                 f.visual.shield_skin_id == 2 ? 20.0f : 17.0f)
                * shield_scale;
            const D2D1_ELLIPSE shield=D2D1::Ellipse(D2D1::Point2F(shield_offset,0.0f),shield_rx,shield_ry);
            target->FillEllipse(shield, shield_skin); target->DrawEllipse(shield, f.parry_window_remaining_s>0.0f?accent_brush:white_brush, f.parry_window_remaining_s>0.0f?4.0f:2.5f);
            target->DrawLine(D2D1::Point2F(17.0f,0.0f),D2D1::Point2F(39.0f,0.0f),outline_brush,2.0f);
            if (f.visual.shield_skin_id == 1) {
                target->DrawEllipse(
                    D2D1::Ellipse(D2D1::Point2F(shield_offset,0.0f),6.0f*shield_scale,6.0f*shield_scale),
                    outline_brush, 2.0f);
            } else if (f.visual.shield_skin_id == 2) {
                target->DrawLine(D2D1::Point2F(shield_offset,-shield_ry),D2D1::Point2F(shield_offset,shield_ry),outline_brush,2.0f);
            } else if (f.visual.shield_skin_id == 3) {
                target->DrawRectangle(
                    D2D1::RectF(shield_offset-shield_rx*0.7f,-shield_ry*0.7f,
                                shield_offset+shield_rx*0.7f,shield_ry*0.7f),
                    outline_brush,2.0f);
            } else if (f.visual.shield_skin_id == 4) {
                target->DrawLine(D2D1::Point2F(shield_offset-shield_rx,0.0f),D2D1::Point2F(shield_offset+shield_rx,0.0f),outline_brush,2.0f);
            } else if (f.visual.shield_skin_id == 5) {
                target->DrawEllipse(
                    D2D1::Ellipse(D2D1::Point2F(shield_offset,0.0f),shield_rx*0.55f,shield_ry*0.55f),
                    outline_brush,2.0f);
            }
        }

        set_local(heading_deg);

        // Cosmetic body silhouettes; body_scale remains presentation-only.
        const float abdomen_rx =
            f.visual.body_skin_id == 1 ? 22.0f :
            f.visual.body_skin_id == 2 ? 16.0f : 18.0f;
        const float abdomen_ry =
            f.visual.body_skin_id == 1 ? 11.0f :
            f.visual.body_skin_id == 2 ? 17.0f : 14.0f;
        const float thorax_rx =
            f.visual.body_skin_id == 1 ? 13.0f :
            f.visual.body_skin_id == 2 ? 18.0f : 15.0f;
        const float thorax_ry =
            f.visual.body_skin_id == 1 ? 17.0f :
            f.visual.body_skin_id == 2 ? 13.0f : 15.5f;
        const D2D1_ELLIPSE abdomen =
            D2D1::Ellipse(D2D1::Point2F(-17.0f, 0.0f), abdomen_rx, abdomen_ry);
        target->FillEllipse(abdomen, body_soft_skin);
        target->DrawEllipse(abdomen, outline_brush, 2.6f);

        const D2D1_ELLIPSE thorax =
            D2D1::Ellipse(D2D1::Point2F(1.0f, 0.0f), thorax_rx, thorax_ry);
        target->FillEllipse(thorax, body_skin);
        target->DrawEllipse(thorax, outline_brush, 2.6f);

        if (f.visual.body_skin_id == 1) {
            // Ruby tail prongs sharpen the silhouette without changing body
            // collision radius.
            target->DrawLine(D2D1::Point2F(-35.0f,-5.0f),D2D1::Point2F(-43.0f,0.0f),outline_brush,2.4f);
            target->DrawLine(D2D1::Point2F(-43.0f,0.0f),D2D1::Point2F(-35.0f,5.0f),outline_brush,2.4f);
        } else if (f.visual.body_skin_id == 2) {
            // Azure rear segment creates a compact segmented silhouette.
            const D2D1_ELLIPSE rear = D2D1::Ellipse(
                D2D1::Point2F(-31.0f,0.0f), 9.0f, 12.0f);
            target->FillEllipse(rear, body_soft_skin);
            target->DrawEllipse(rear, outline_brush, 2.2f);
        } else if (f.visual.body_skin_id == 3) {
            target->DrawRectangle(
                D2D1::RectF(-37.0f,-8.0f,-25.0f,8.0f),
                outline_brush,2.2f);
        } else if (f.visual.body_skin_id == 4) {
            target->DrawLine(D2D1::Point2F(-36.0f,-12.0f),D2D1::Point2F(-26.0f,-18.0f),body_secondary?body_secondary:outline_brush,3.0f);
            target->DrawLine(D2D1::Point2F(-36.0f,12.0f),D2D1::Point2F(-26.0f,18.0f),body_secondary?body_secondary:outline_brush,3.0f);
        } else if (f.visual.body_skin_id == 5) {
            target->DrawEllipse(
                D2D1::Ellipse(D2D1::Point2F(-17.0f,0.0f),10.0f,10.0f),
                body_primary?body_primary:white_brush,2.5f);
        }

        const D2D1_ELLIPSE head =
            D2D1::Ellipse(D2D1::Point2F(20.0f, 0.0f), 14.5f, 14.5f);
        target->FillEllipse(head, body_skin);
        target->DrawEllipse(head, outline_brush, 2.6f);

        for (int i = 0; i < 3; ++i) {
            const float sx = -27.0f + i * 8.0f;
            const float half =
                9.5f - std::fabs(static_cast<float>(i) - 1.0f) * 1.8f;
            target->DrawLine(
                D2D1::Point2F(sx, -half),
                D2D1::Point2F(sx, half),
                outline_brush, 2.2f);
        }

        for (float side : {-1.0f, 1.0f}) {
            const D2D1_ELLIPSE eye =
                D2D1::Ellipse(
                    D2D1::Point2F(25.0f, side * 6.5f),
                    6.2f, 5.3f);
            target->FillEllipse(eye, eye_brush);
            target->DrawEllipse(eye, outline_brush, 1.5f);
            target->FillEllipse(
                D2D1::Ellipse(
                    D2D1::Point2F(27.0f, side * 6.5f - 1.6f),
                    1.7f, 1.7f),
                eye_highlight_brush);
        }

        target->DrawLine(
            D2D1::Point2F(30.0f, -2.0f),
            D2D1::Point2F(32.0f, 2.0f),
            outline_brush, 1.5f);
        target->DrawLine(
            D2D1::Point2F(29.0f, -7.0f),
            D2D1::Point2F(37.0f, -12.0f),
            outline_brush, 1.8f);
        target->DrawLine(
            D2D1::Point2F(29.0f, 7.0f),
            D2D1::Point2F(37.0f, 12.0f),
            outline_brush, 1.8f);

        target->SetTransform(canvas);
        safe_release(body_gradient);
        safe_release(wing_gradient);
        safe_release(sword_gradient);
        safe_release(shield_gradient);
        safe_release(body_primary);
        safe_release(body_secondary);
    }

    void draw_neural_sparks(
        const FlyRenderState& f,
        bool red_team,
        float cx,
        float cy,
        float radius,
        double render_seconds)
    {
        ID2D1Brush* team = red_team ? red_brush : blue_brush;
        ID2D1Brush* soft = red_team ? red_soft_brush : blue_soft_brush;

        for (size_t i = 0; i < kKeyNeuronCount; ++i) {
            const float angle =
                -1.5707963f
                + static_cast<float>(i)
                  * 6.2831853f / static_cast<float>(kKeyNeuronCount);
            const float x = cx + std::cos(angle) * radius;
            const float y = cy + std::sin(angle) * radius;
            const float a = clamp01(f.key_neuron_activity[i]);
            const float pulse =
                0.5f + 0.5f * std::sin(
                    static_cast<float>(render_seconds * 10.0 + i * 0.85));
            const float rr = 3.2f + a * (4.5f + 2.0f * pulse);

            target->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(x, y), 3.0f, 3.0f),
                grid_brush);

            if (a > 0.02f) {
                target->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(x, y), rr, rr),
                    soft);
                target->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(x, y), 2.0f + 2.2f * a, 2.0f + 2.2f * a),
                    team);
                if (a > 0.45f) {
                    target->DrawLine(
                        D2D1::Point2F(x - rr - 2.0f, y),
                        D2D1::Point2F(x + rr + 2.0f, y),
                        white_brush, 1.0f + a);
                    target->DrawLine(
                        D2D1::Point2F(x, y - rr - 2.0f),
                        D2D1::Point2F(x, y + rr + 2.0f),
                        white_brush, 1.0f + a);
                }
            } else {
                target->DrawEllipse(
                    D2D1::Ellipse(D2D1::Point2F(x, y), 3.3f, 3.3f),
                    muted_brush, 1.0f);
            }
        }
    }

    void draw_hud(
        const FlyRenderState& f,
        bool red_team,
        float x,
        float y,
        float w,
        float h,
        double render_seconds)
    {
        ID2D1Brush* team = red_team ? red_brush : blue_brush;

        target->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(x, y, x + w, y + h),
                18.0f, 18.0f),
            panel_brush);

        const std::wstring name = utf8_to_wide(f.name);
        text(
            name, name_format, team,
            D2D1::RectF(x + 18, y + 14, x + w - 18, y + 54));

        #ifdef FLYARENA_PRODUCT_UI
        const bool training =
            tuning.app_mode.load() == AppMode::Training;
        const bool random_trainer = training
            && tuning.training_submode.load()
               == TrainingSubmode::RandomTrainer;
        const wchar_t* slot_badge = training
            ? (red_team
               ? L"LEARNING"
               : (random_trainer
                  ? L"TRAINER · LEARNING"
                  : L"OPPONENT · FROZEN"))
            : L"BATTLE · FROZEN";
        text(
            slot_badge, small_format,
            training ? accent_brush : muted_brush,
            D2D1::RectF(x + 18, y + 52, x + w - 18, y + 72));
        #endif

        draw_integer_bar(
            L"HP", f.hp, f.max_hp,
            x + 18, y + 82, w - 36, 13.0f, team);
        draw_bar(
            f.stamina_recovery_eligible ? L"Stamina · RECOVERING" : L"Stamina",
            f.stamina_continuous,
            static_cast<float>(f.max_stamina),
            x + 18, y + 116, w - 36, 9.0f, accent_brush);

        const float portrait_x = x + w * 0.50f;
        const float portrait_y = y + 238.0f;

        // Large customization portrait. This intentionally does not use world
        // collision scale: it is a character card preview. Equipment ratios and
        // skins come from the same FlyVisualLoadout used in the arena model.
        draw_neural_sparks(
            f, red_team, portrait_x, portrait_y, 92.0f, render_seconds);
        draw_fly_model(
            f, red_team,
            D2D1::Point2F(portrait_x, portrait_y),
            1.58f, 0.0f, render_seconds, true);

        text(
            L"KEY CNS ACTIVITY", small_format, muted_brush,
            D2D1::RectF(x + 18, y + 336, x + w - 18, y + 358));

        wchar_t match_info[256]{};
        swprintf_s(
            match_info,
            L"Speed %.2f  Opponent %ls\nHIT %u  BLOCK %u  PARRY %u  DODGE %u",
            static_cast<double>(f.forward_speed),
            f.opponent_visible ? L"VISIBLE" : L"LOST",
            f.hits_landed,f.blocks,f.parries,f.dodges);
        text(
            match_info, hud_format, white_brush,
            D2D1::RectF(x + 18, y + 370, x + w - 18, y + 426));

        if (show_debug) {
            wchar_t debug[320]{};
            swprintf_s(
                debug,
                L"DN %.2f / %.2f Hz\nMN %.2f / %.2f Hz\nThrust %.2f  Turn %+.2f",
                static_cast<double>(f.dn_left_hz),
                static_cast<double>(f.dn_right_hz),
                static_cast<double>(f.mn_left_hz),
                static_cast<double>(f.mn_right_hz),
                static_cast<double>(f.forward),
                static_cast<double>(f.turn));
            text(
                debug, small_format, muted_brush,
                D2D1::RectF(x + 18, y + 430, x + w - 18, y + 500));
        } else {
            text(
                L"Active dots = recent spikes in 12 high-outdegree\nrepresentatives from DN/MN output pools.",
                small_format, muted_brush,
                D2D1::RectF(x + 18, y + 430, x + w - 18, y + 476));
        }

        #ifdef FLYARENA_PRODUCT_UI
        const uint32_t slot = red_team ? 0u : 1u;
        const TrainingSlotUiState training_state =
            tuning.training_slot(slot);
        const FlypackSlotUiState flypack_state =
            tuning.flypack_slot(slot);
        const D2D1_ROUNDED_RECT panel_outline =
            D2D1::RoundedRect(
                D2D1::RectF(x + 3.0f, y + 3.0f,
                            x + w - 3.0f, y + h - 3.0f),
                16.0f, 16.0f);
        target->DrawRoundedRectangle(
            panel_outline, team, 1.6f, drop_stroke);
        if (drag_hover_slot == static_cast<int>(slot)) {
            ID2D1Brush* hover_brush =
                drag_hover_valid ? accent_brush : amber_brush;
            target->DrawRoundedRectangle(
                panel_outline, hover_brush, 4.5f);
            text(
                drag_hover_valid ? L"DROP VALID · RELEASE TO LOAD"
                                 : L"DROP REJECTED",
                small_format, hover_brush,
                D2D1::RectF(x + 18, y + 70, x + w - 18, y + 88));
        }

        const D2D1_RECT_F drop_rect =
            D2D1::RectF(x + 10.0f, y + 480.0f,
                        x + w - 10.0f, y + h - 10.0f);
        target->DrawRoundedRectangle(
            D2D1::RoundedRect(drop_rect, 8.0f, 8.0f),
            accent_brush, 1.4f, drop_stroke);

        const bool managed_trainer = !red_team
            && training
            && tuning.training_submode.load()
               == TrainingSubmode::RandomTrainer;
        const bool frozen_opponent = !red_team
            && training
            && tuning.training_submode.load()
               == TrainingSubmode::ImportedOpponent;
        const std::wstring drop_label = managed_trainer
            ? (L"MANAGED TRAINER GEN "
               + std::to_wstring(tuning.trainer_generation())
               + L" · " + utf8_to_wide(training_state.status))
            : (L"DROP FLYPACK / FLYTRAIN · "
               + utf8_to_wide(training_state.status));
        text(
            drop_label, small_format, accent_brush,
            D2D1::RectF(x + 18, y + 480, x + w - 18, y + 497));

        if (!managed_trainer) {
            text(
                L"Fly: " + wrap_path_for_hud(flypack_state.source_path)
                + L" · " + utf8_to_wide(flypack_state.status),
                path_format, muted_brush,
                D2D1::RectF(x + 18, y + 496, x + w - 18, y + 513));
        }

        text(
            (frozen_opponent ? L"Read only: " : L"Save: ")
            + wrap_path_for_hud(
                training_state.checkpoint_path),
            path_format, muted_brush,
            D2D1::RectF(x + 18, y + 513, x + w - 18, y + 539));

        draw_top_button(
            managed_trainer
                ? L"USE RANDOMIZE TRAINER ABOVE"
                : (frozen_opponent
                   ? L"FROZEN POLICY · NO WRITES"
                   : L"RESET THIS FLY LEARNING"),
            D2D1::RectF(
                x + 18, y + 539, x + w - 18, y + 558),
            false);
        #endif
    }

    void draw_fly(
        const FlyRenderState& f,
        bool red_team,
        float cx,
        float cy,
        float arena_radius_px,
        double render_seconds)
    {
        ID2D1Brush* team = red_team ? red_brush : blue_brush;
        const D2D1_POINT_2F p =
            world_to_screen(f.position, cx, cy, arena_radius_px);

        // Tie world render size directly to physical radius / arena radius.
        // This makes fly-to-arena scale mathematically invariant under window
        // resizing instead of depending on unrelated hardcoded pixel sizes.
        const float collision_radius_px =
            arena_radius_px
            * f.collision_radius_world
            / std::max(0.001f, config.arena_radius_world);
        const float model_scale =
            collision_radius_px / 14.5f;

        const float heading_deg =
            -f.heading_rad * 180.0f / 3.14159265358979323846f;

        draw_fly_model(
            f, red_team, p, model_scale, heading_deg,
            render_seconds, false);

        const std::wstring name = utf8_to_wide(f.name);
        text(
            name, small_format, team,
            D2D1::RectF(
                p.x - 54.0f, p.y - 30.0f,
                p.x + 54.0f, p.y - 10.0f));
    }

    void draw_preview_events(
        float width,
        float height,
        float arena_cx,
        float arena_cy,
        float arena_radius_px)
    {
        const auto now = std::chrono::steady_clock::now();

        while (!preview_events.empty()) {
            const float age =
                std::chrono::duration<float>(
                    now - preview_events.front().start).count();
            if (age <= preview_events.front().duration_s)
                break;
            preview_events.pop_front();
        }

        if (preview_events.empty())
            return;

        const PreviewEvent& e = preview_events.back();
        const float age =
            std::chrono::duration<float>(
                now - e.start).count();
        const float t =
            clamp01(age / e.duration_s);
        const D2D1_POINT_2F event_point =
            e.has_world_position
            ? world_to_screen(
                Vec2{e.world_x, e.world_y},
                arena_cx, arena_cy, arena_radius_px)
            : D2D1::Point2F(width * 0.5f, height * 0.50f);

        ID2D1SolidColorBrush* event_brush = nullptr;
        D2D1_COLOR_F c = e.color;
        c.a = 1.0f - t;
        target->CreateSolidColorBrush(c, &event_brush);

        if (event_brush) {
            if (e.label == L"DODGE!") {
                // Dodge should read as rapid lateral displacement rather than
                // another impact ring. The label shoots sideways and leaves
                // fading motion streaks/ghost markers behind it.
                const float dir =
                    (e.direction == 0.0f) ? 1.0f : e.direction;
                const float ease =
                    1.0f - (1.0f - t) * (1.0f - t);
                const float shift =
                    dir * (38.0f + 145.0f * ease);
                const float base_x = event_point.x;
                const float base_y = event_point.y;

                text(
                    e.label,
                    event_format,
                    event_brush,
                    D2D1::RectF(
                        base_x - 190.0f + shift,
                        base_y - 64.0f - 24.0f * t,
                        base_x + 190.0f + shift,
                        base_y + 32.0f - 24.0f * t));

                // Speed lines point in the dodge direction.
                for (int i = 0; i < 4; ++i) {
                    const float trail =
                        static_cast<float>(i) * 28.0f;
                    const float yoff =
                        (static_cast<float>(i) - 1.5f) * 13.0f;
                    const float x0 =
                        base_x + shift
                        - dir * (48.0f + trail);
                    const float x1 =
                        base_x + shift
                        - dir * (8.0f + trail * 0.35f);

                    target->DrawLine(
                        D2D1::Point2F(x0, base_y + yoff),
                        D2D1::Point2F(x1, base_y + yoff - 4.0f),
                        event_brush,
                        std::max(1.0f, 5.0f - static_cast<float>(i)));
                }

                // Three ghost circles create a visible "afterimage" path.
                for (int i = 1; i <= 3; ++i) {
                    const float ghost_x =
                        base_x + shift
                        - dir * (28.0f * static_cast<float>(i));
                    const float r =
                        18.0f - 3.0f * static_cast<float>(i);
                    target->DrawEllipse(
                        D2D1::Ellipse(
                            D2D1::Point2F(ghost_x, base_y),
                            r, r * 0.62f),
                        event_brush,
                        std::max(1.0f, 3.5f - 0.7f * static_cast<float>(i)));
                }
            } else {
                const float rise = 42.0f * t;
                text(
                    e.label,
                    event_format,
                    event_brush,
                    D2D1::RectF(
                        event_point.x - 150.0f,
                        event_point.y - 88.0f - rise,
                        event_point.x + 150.0f,
                        event_point.y - 6.0f - rise));

                target->DrawEllipse(
                    D2D1::Ellipse(
                        D2D1::Point2F(
                            event_point.x,
                            event_point.y),
                        22.0f + 55.0f * t,
                        22.0f + 55.0f * t),
                    event_brush,
                    4.0f * (1.0f - 0.6f * t));
            }

            event_brush->Release();
        }
    }

    void render_frame() {
        if (!create_device_resources())
            return;

        RECT rc{};
        GetClientRect(hwnd, &rc);

        const float client_width =
            static_cast<float>(
                std::max<LONG>(1, rc.right - rc.left));
        const float client_height =
            static_cast<float>(
                std::max<LONG>(1, rc.bottom - rc.top));

        const float width = kVirtualWidth;
        const float height = kVirtualHeight;

        const SnapshotSample sample = snapshots.sample();

        ArenaRenderSnapshot view{};
        float interp = 1.0f;

        if (sample.valid) {
            const auto now = std::chrono::steady_clock::now();
            const double elapsed_ms =
                std::chrono::duration<double, std::milli>(
                    now - sample.current_published_at).count();

            // One world-tick visual delay: at a new snapshot publication, draw
            // the prior state, then interpolate smoothly toward the new state.
            interp = static_cast<float>(std::clamp(
                elapsed_ms
                / std::max(1.0, sample.current.world_step_ms),
                0.0, 1.0));

            view = interpolate_snapshot(sample, interp);
        } else {
            view.phase = ArenaPhase::Booting;
            view.status = "Waiting for simulation...";
            view.red.name = "Ruby";
            view.blue.name = "Azure";
        }

        if (sample.valid) {
            consume_wall_sound(sample.current);
            // A simultaneous physical combat event is more important than
            // the wall hum and therefore gets the final playback slot.
            consume_combat_events(sample.current);
        }

        const auto now = std::chrono::steady_clock::now();
        const double render_seconds =
            std::chrono::duration<double>(
                now.time_since_epoch()).count();

        target->BeginDraw();
        target->SetTransform(D2D1::Matrix3x2F::Identity());
        target->Clear(color(0.026f, 0.032f, 0.045f));

        // Every visual element now lives in one 1280x720 virtual canvas.
        // One uniform transform scales the arena, UI, text, flies and VFX
        // together. Non-16:9 windows get harmless letterboxing instead of
        // per-element stretching.
        const D2D1_MATRIX_3X2_F canvas_transform =
            uniform_canvas_transform(
                client_width,
                client_height);

        target->SetTransform(canvas_transform);

        if (view.headless_max_training) {
            const float cx = width * 0.5f;
            const float cy = height * 0.54f;
            const float arena_radius_px = 255.0f;

            // Keep the only animated information computationally meaningful:
            // episode number, simulated time and achieved throughput. Models
            // remain at opposing spawn points rather than jumping at sparse
            // heartbeat updates.
            target->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(cx, cy),
                    arena_radius_px, arena_radius_px),
                arena_brush);
            target->DrawEllipse(
                D2D1::Ellipse(D2D1::Point2F(cx, cy),
                    arena_radius_px, arena_radius_px),
                grid_brush, 2.0f);

            FlyRenderState red_spawn = view.red;
            FlyRenderState blue_spawn = view.blue;
            red_spawn.position = {-0.62f, 0.0f};
            blue_spawn.position = {0.62f, 0.0f};
            red_spawn.heading_rad = 0.0f;
            blue_spawn.heading_rad = 3.14159265358979323846f;
            red_spawn.forward = blue_spawn.forward = 0.0f;
            red_spawn.sword_swinging = blue_spawn.sword_swinging = false;
            draw_fly(red_spawn, true, cx, cy, arena_radius_px, 0.0);
            draw_fly(blue_spawn, false, cx, cy, arena_radius_px, 0.0);

            wchar_t max_status[256]{};
            swprintf_s(
                max_status,
                L"MAX LEARNING  ·  EPISODE %llu  ·  SIM %.2fs  ·  %.1fx",
                static_cast<unsigned long long>(
                    tuning.episode_number.load()),
                view.sim_time_ms / 1000.0,
                static_cast<double>(
                    tuning.measured_simulation_multiplier.load()));
            text(max_status, title_format, white_brush,
                D2D1::RectF(180.0f, 74.0f, 1100.0f, 122.0f));
            text(L"HEADLESS: arena visuals frozen at spawn · simulation continues at hardware limit",
                hud_format, muted_brush,
                D2D1::RectF(210.0f, 126.0f, 1070.0f, 160.0f));

            constexpr const wchar_t* speed_labels[
                kTrainingSpeedOptionCount] = {
                    L"1x", L"2x", L"4x", L"8x", L"16x", L"MAX"};
            for (uint32_t option = 0;
                 option < kTrainingSpeedOptionCount; ++option)
            {
                const float left = 790.0f + 34.0f * option;
                draw_top_button(
                    speed_labels[option],
                    D2D1::RectF(left, 50.0f, left + 31.0f, 78.0f),
                    option == 5);
            }

            const HRESULT max_hr = target->EndDraw();
            if (max_hr == D2DERR_RECREATE_TARGET)
                discard_device_resources();
            ++fps_frames;
            const double fps_elapsed =
                std::chrono::duration<double>(now - fps_epoch).count();
            if (fps_elapsed >= 0.50) {
                measured_fps = static_cast<double>(fps_frames) / fps_elapsed;
                fps_frames = 0;
                fps_epoch = now;
            }
            return;
        }

        const float panel_w = 248.0f;
        const float margin = 18.0f;
        const float center_left = panel_w + margin * 2.0f;
        const float center_right = width - panel_w - margin * 2.0f;

        const float cx = (center_left + center_right) * 0.5f;
        const float cy = height * 0.525f;
        const float arena_radius_px =
            std::min(
                (center_right - center_left) * 0.455f,
                height * 0.365f);

        #ifdef FLYARENA_PRODUCT_UI
        // Product-loop controls. These are real hit targets in the same
        // virtual canvas, so resizing/letterboxing cannot desynchronize input.
        const AppMode app_mode = tuning.app_mode.load();
        draw_top_button(
            L"CREATE FLY",
            D2D1::RectF(286.0f, 14.0f, 402.0f, 44.0f),
            false);
        draw_top_button(
            L"TRAINING",
            D2D1::RectF(412.0f, 14.0f, 516.0f, 44.0f),
            app_mode == AppMode::Training);
        draw_top_button(
            L"BATTLE",
            D2D1::RectF(526.0f, 14.0f, 620.0f, 44.0f),
            app_mode == AppMode::Battle);
        if (app_mode == AppMode::Training) {
            draw_top_button(
                L"RANDOM TRAINER",
                D2D1::RectF(286.0f, 50.0f, 424.0f, 78.0f),
                tuning.training_submode.load()
                    == TrainingSubmode::RandomTrainer);
            draw_top_button(
                L"IMPORTED OPPONENT",
                D2D1::RectF(434.0f, 50.0f, 590.0f, 78.0f),
                tuning.training_submode.load()
                    == TrainingSubmode::ImportedOpponent);
            draw_top_button(
                L"RANDOMIZE TRAINER",
                D2D1::RectF(600.0f, 50.0f, 780.0f, 78.0f),
                false);
            constexpr const wchar_t* speed_labels[
                kTrainingSpeedOptionCount] = {
                    L"1x", L"2x", L"4x", L"8x", L"16x", L"MAX"};
            for (uint32_t option = 0;
                 option < kTrainingSpeedOptionCount;
                 ++option)
            {
                const float left = 790.0f + 34.0f * option;
                draw_top_button(
                    speed_labels[option],
                    D2D1::RectF(
                        left, 50.0f, left + 31.0f, 78.0f),
                    tuning.training_speed_option.load() == option);
            }
        }

        // Header.
        text(
            L"FLYARENA · NEURAL COMBAT",
            title_format,
            white_brush,
            D2D1::RectF(
                640.0f, 16.0f,
                center_right, 52.0f));
        #else
        text(
            L"FLYARENA  ·  NEURAL ARENA  ·  HD VECTOR",
            title_format,
            white_brush,
            D2D1::RectF(
                center_left, 16.0f,
                center_right, 52.0f));
        #endif

        wchar_t stats[320]{};
        const wchar_t* phase =
            view.phase == ArenaPhase::Calibrating ? L"CALIBRATING" :
            view.phase == ArenaPhase::Live ? L"LIVE" :
            view.phase == ArenaPhase::Complete ? L"COMPLETE" :
            view.phase == ArenaPhase::Failed ? L"FAILED" :
            L"BOOTING";

        if (config.target_fps > 0) {
            swprintf_s(
                stats,
                #ifdef FLYARENA_PRODUCT_UI
                L"%ls   episode %llu   sim %.2fs   actual %.1fx   render %.1f FPS / cap %d",
                phase,
                static_cast<unsigned long long>(tuning.episode_number.load()),
                #else
                L"%ls   sim %.2fs   render %.1f FPS / cap %d   interp %.2f",
                phase,
                #endif
                view.sim_time_ms / 1000.0,
                #ifdef FLYARENA_PRODUCT_UI
                static_cast<double>(
                    tuning.measured_simulation_multiplier.load()),
                #endif
                measured_fps,
                config.target_fps
                #ifndef FLYARENA_PRODUCT_UI
                , static_cast<double>(interp)
                #endif
                );
        } else {
            swprintf_s(
                stats,
                #ifdef FLYARENA_PRODUCT_UI
                L"%ls   episode %llu   sim %.2fs   actual %.1fx   render %.1f FPS / uncapped",
                phase,
                static_cast<unsigned long long>(tuning.episode_number.load()),
                #else
                L"%ls   sim %.2fs   render %.1f FPS / uncapped   interp %.2f",
                phase,
                #endif
                view.sim_time_ms / 1000.0,
                #ifdef FLYARENA_PRODUCT_UI
                static_cast<double>(
                    tuning.measured_simulation_multiplier.load()),
                #endif
                measured_fps
                #ifndef FLYARENA_PRODUCT_UI
                , static_cast<double>(interp)
                #endif
                );
        }

        text(
            stats,
            small_format,
            muted_brush,
            D2D1::RectF(
                center_left, 82.0f,
                center_right, 104.0f));

        const double remaining_ms =
            (view.match_duration_ms > 0.0)
            ? std::max(0.0, view.match_duration_ms - view.sim_time_ms)
            : 0.0;
        const int remaining_seconds =
            static_cast<int>(std::ceil(remaining_ms / 1000.0));
        wchar_t timer_text[64]{};
        swprintf_s(
            timer_text, L"%02d:%02d",
            remaining_seconds / 60,
            remaining_seconds % 60);
        text(
            timer_text, name_format, white_brush,
            D2D1::RectF(
                cx - 80.0f, 78.0f,
                cx + 80.0f, 116.0f));

        // Arena background.
        target->FillEllipse(
            D2D1::Ellipse(
                D2D1::Point2F(cx, cy),
                arena_radius_px,
                arena_radius_px),
            arena_brush);

        // Rings / crosshair.
        for (int i = 1; i <= 3; ++i) {
            const float r =
                arena_radius_px * (static_cast<float>(i) / 3.0f);
            target->DrawEllipse(
                D2D1::Ellipse(
                    D2D1::Point2F(cx, cy), r, r),
                grid_brush, 1.0f);
        }
        target->DrawLine(
            D2D1::Point2F(cx - arena_radius_px, cy),
            D2D1::Point2F(cx + arena_radius_px, cy),
            grid_brush, 1.0f);
        target->DrawLine(
            D2D1::Point2F(cx, cy - arena_radius_px),
            D2D1::Point2F(cx, cy + arena_radius_px),
            grid_brush, 1.0f);

        if (sample.valid) {
            draw_fly(
                view.red, true,
                cx, cy, arena_radius_px,
                render_seconds);
            draw_fly(
                view.blue, false,
                cx, cy, arena_radius_px,
                render_seconds);
        }

        // Pair distance.
        wchar_t dist[128]{};
        swprintf_s(
            dist,
            L"distance %.3f",
            static_cast<double>(view.pair_distance));
        text(
            dist,
            hud_format,
            white_brush,
            D2D1::RectF(
                cx - 100.0f,
                #ifdef FLYARENA_PRODUCT_UI
                cy + arena_radius_px + 2.0f,
                cx + 100.0f,
                cy + arena_radius_px + 22.0f));
                #else
                cy + arena_radius_px + 18.0f,
                cx + 100.0f,
                cy + arena_radius_px + 45.0f));
                #endif

        // Side HUDs.
        const float panel_h = 566.0f;
        draw_hud(
            view.red, true,
            margin, 82.0f,
            panel_w, panel_h, render_seconds);
        draw_hud(
            view.blue, false,
            width - margin - panel_w,
            82.0f,
            panel_w, panel_h, render_seconds);

        // Minimal controls. Developer parameters are hidden unless F10 is on.
        const wchar_t* footer =
            #ifdef FLYARENA_PRODUCT_UI
            L"";
            #else
            L"F1 60 · F2 120 · F3 144 · F4 uncapped · F10 debug · Space pause · H/B/P/D VFX test";
            #endif
        text(
            footer, small_format, muted_brush,
            D2D1::RectF(
                #ifdef FLYARENA_PRODUCT_UI
                center_left, height - 68.0f,
                center_right, height - 46.0f));
                #else
                center_left, height - 42.0f,
                center_right, height - 16.0f));
                #endif

        #ifdef FLYARENA_PRODUCT_UI
        draw_top_button(
            L"PLAY",
            D2D1::RectF(430.0f, 674.0f, 512.0f, 704.0f),
            !tuning.paused.load());
        draw_top_button(
            L"PAUSE",
            D2D1::RectF(522.0f, 674.0f, 610.0f, 704.0f),
            tuning.paused.load());
        draw_top_button(
            L"REPEAT",
            D2D1::RectF(620.0f, 674.0f, 718.0f, 704.0f),
            tuning.repeat_matches.load());
        draw_top_button(
            L"RESET",
            D2D1::RectF(728.0f, 674.0f, 816.0f, 704.0f),
            false);
        if (tuning.app_mode.load() == AppMode::Training) {
            draw_top_button(
                L"REWARD TUNING",
                D2D1::RectF(826.0f, 674.0f, 956.0f, 704.0f),
                false);
        }
        #endif

        if (show_debug) {
            wchar_t tuning_text[256]{};
            swprintf_s(
                tuning_text,
                L"Sensitivity %.2f   Forward gain %.2f   Turn gain %.2f",
                static_cast<double>(tuning.sensitivity.load()),
                static_cast<double>(tuning.forward_gain.load()),
                static_cast<double>(tuning.turn_gain.load()));
            text(
                tuning_text, small_format, accent_brush,
                D2D1::RectF(
                    center_left, height - 66.0f,
                    center_right, height - 44.0f));
        }

        #ifdef FLYARENA_PRODUCT_UI
        if (!ui_notice.empty()) {
            const double notice_age =
                std::chrono::duration<double>(
                    now - ui_notice_started).count();
            if (notice_age < 5.0) {
                text(
                    ui_notice, small_format, accent_brush,
                    D2D1::RectF(
                        center_left, height - 68.0f,
                        center_right, height - 45.0f));
            } else {
                ui_notice.clear();
            }
        }
        #endif

        if (tuning.paused.load()) {
            text(
                L"PAUSED",
                event_format,
                white_brush,
                D2D1::RectF(
                    cx - 180.0f, cy - 70.0f,
                    cx + 180.0f, cy + 70.0f));
        }

        if (view.phase == ArenaPhase::Complete && view.match_result != 0) {
            std::wstring result;
            if (view.match_result == 1)
                result = utf8_to_wide(view.red.name) + L" WINS!";
            else if (view.match_result == 2)
                result = utf8_to_wide(view.blue.name) + L" WINS!";
            else
                result = L"DRAW";

            text(
                result, event_format, white_brush,
                D2D1::RectF(
                    cx - 230.0f, cy - 84.0f,
                    cx + 230.0f, cy + 10.0f));

            const std::wstring reason =
                utf8_to_wide(view.result_reason);
            text(
                reason, hud_format, accent_brush,
                D2D1::RectF(
                    cx - 160.0f, cy + 8.0f,
                    cx + 160.0f, cy + 40.0f));
        }

        // Calibration / error status.
        if (view.phase != ArenaPhase::Live &&
            view.phase != ArenaPhase::Complete)
        {
            text(
                utf8_to_wide(view.status),
                hud_format,
                accent_brush,
                D2D1::RectF(
                    center_left,
                    116.0f,
                    center_right,
                    144.0f));
        }

        draw_preview_events(
            width, height,
            cx, cy, arena_radius_px);

        const HRESULT hr = target->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET)
            discard_device_resources();

        // FPS measurement.
        ++fps_frames;
        const double fps_elapsed =
            std::chrono::duration<double>(
                now - fps_epoch).count();
        if (fps_elapsed >= 0.50) {
            measured_fps =
                static_cast<double>(fps_frames) / fps_elapsed;
            fps_frames = 0;
            fps_epoch = now;
        }
    }

    void pace_frame(
        std::chrono::steady_clock::time_point frame_start)
    {
        const int fps =
            tuning.app_mode.load() == AppMode::Training
                && tuning.training_speed_option.load() == 5
            ? 4
            : config.target_fps;
        if (fps <= 0)
            return;

        const double target_s = 1.0 / static_cast<double>(fps);

        for (;;) {
            const auto now = std::chrono::steady_clock::now();
            const double elapsed =
                std::chrono::duration<double>(
                    now - frame_start).count();
            const double remaining = target_s - elapsed;

            if (remaining <= 0.0)
                break;

            if (remaining > 0.002) {
                std::this_thread::sleep_for(
                    std::chrono::duration<double>(
                        remaining - 0.001));
            } else {
                std::this_thread::yield();
            }
        }
    }

    int run(HINSTANCE instance, int show_cmd) {
        (void)show_cmd;

        timeBeginPeriod(1);

        #ifdef FLYARENA_PRODUCT_UI
        const HRESULT ole_result = OleInitialize(nullptr);
        ole_initialized = SUCCEEDED(ole_result);
        #endif

        if (!initialize(instance)) {
            #ifdef FLYARENA_PRODUCT_UI
            if (ole_initialized) {
                OleUninitialize();
                ole_initialized = false;
            }
            #endif
            timeEndPeriod(1);
            return 2;
        }

        #ifdef FLYARENA_PRODUCT_UI
        if (ole_initialized
            && SUCCEEDED(RegisterDragDrop(hwnd, this)))
        {
            ole_drop_registered = true;
        }
        #endif

        MSG msg{};
        bool running = true;

        while (running && !tuning.quit_requested.load()) {
            const auto frame_start =
                std::chrono::steady_clock::now();

            while (PeekMessageW(
                &msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT) {
                    running = false;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            if (!running)
                break;

            render_frame();
            pace_frame(frame_start);
        }

        #ifdef FLYARENA_PRODUCT_UI
        if (ole_drop_registered) {
            RevokeDragDrop(hwnd);
            ole_drop_registered = false;
        }
        if (ole_initialized) {
            OleUninitialize();
            ole_initialized = false;
        }
        #endif
        timeEndPeriod(1);
        return static_cast<int>(msg.wParam);
    }
};

ArenaRenderer::ArenaRenderer(
    SnapshotBuffer& snapshots,
    RuntimeTuning& tuning,
    const RendererConfig& config)
    : impl_(new Impl(snapshots, tuning, config))
{}

ArenaRenderer::~ArenaRenderer() {
    delete impl_;
}

int ArenaRenderer::run(
    HINSTANCE instance,
    int show_cmd)
{
    return impl_->run(instance, show_cmd);
}

} // namespace flyarena
