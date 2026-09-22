#include "create_fly_dialog.h"

#include "arena_sim.h"
#include "equipment_combat.h"
#include "fly_profile.h"

#include <algorithm>
#include <array>
#include <cwchar>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <vector>

#include <commdlg.h>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

namespace fs = std::filesystem;

namespace flyarena {
namespace {

constexpr int kIdName = 100;
constexpr int kIdSwordLength = 105;
constexpr int kIdWingSize = 107;
constexpr int kIdShieldMass = 110;
constexpr int kIdSave = 113;
constexpr int kIdCancel = 114;
constexpr int kIdColorBase = 115;
constexpr size_t kColorCount = 8;
constexpr int kIdSkinCardBase = 200;
constexpr size_t kPhysicalSliderCount = 3;
constexpr int kSkinCount = 6;

struct DialogState {
    struct ChildLayout {
        HWND window = nullptr;
        RECT rect{};
    };

    HWND window = nullptr;
    HWND name = nullptr;
    std::array<uint32_t, 4> skin_ids{{1, 0, 0, 0}};
    std::array<HWND, 24> skin_cards{};
    std::array<HWND, kPhysicalSliderCount> sliders{};
    std::array<HWND, kPhysicalSliderCount> slider_values{};
    HWND derived = nullptr;
    // Body, wings, sword, shield; primary then secondary for each.
    std::array<COLORREF, kColorCount> colors{{
        RGB(235, 95, 85), RGB(85, 125, 240),
        RGB(216, 244, 255), RGB(111, 183, 216),
        RGB(243, 245, 250), RGB(143, 164, 200),
        RGB(235, 175, 62), RGB(163, 58, 91)}};
    std::array<HWND, kColorCount> color_buttons{};
    bool saved = false;
    std::string saved_path;
    std::string error;
    int base_client_width = 1;
    int base_client_height = 1;
    std::vector<ChildLayout> child_layout;
    HBRUSH background_brush = nullptr;
};

struct LayoutTransform {
    float scale = 1.0f;
    int offset_x = 0;
    int offset_y = 0;
};

LayoutTransform layout_transform(const DialogState& state) {
    RECT client{};
    GetClientRect(state.window, &client);
    const float sx = static_cast<float>(client.right)
        / static_cast<float>(std::max(1, state.base_client_width));
    const float sy = static_cast<float>(client.bottom)
        / static_cast<float>(std::max(1, state.base_client_height));
    LayoutTransform result;
    result.scale = std::max(0.01f, std::min(sx, sy));
    result.offset_x = static_cast<int>((
        client.right - state.base_client_width * result.scale) * 0.5f);
    result.offset_y = static_cast<int>((
        client.bottom - state.base_client_height * result.scale) * 0.5f);
    return result;
}

BOOL CALLBACK capture_child_layout(HWND child, LPARAM parameter) {
    auto& state = *reinterpret_cast<DialogState*>(parameter);
    RECT rect{};
    GetWindowRect(child, &rect);
    MapWindowPoints(
        nullptr, state.window,
        reinterpret_cast<POINT*>(&rect), 2);
    state.child_layout.push_back({child, rect});
    return TRUE;
}

void capture_layout(DialogState& state) {
    RECT client{};
    GetClientRect(state.window, &client);
    state.base_client_width = std::max(1L, client.right);
    state.base_client_height = std::max(1L, client.bottom);
    state.child_layout.clear();
    EnumChildWindows(
        state.window, capture_child_layout,
        reinterpret_cast<LPARAM>(&state));
}

void layout_controls(DialogState& state) {
    if (state.child_layout.empty())
        return;
    const LayoutTransform transform = layout_transform(state);
    HDWP positions = BeginDeferWindowPos(
        static_cast<int>(state.child_layout.size()));
    for (const auto& child : state.child_layout) {
        const int x = transform.offset_x
            + static_cast<int>(child.rect.left * transform.scale);
        const int y = transform.offset_y
            + static_cast<int>(child.rect.top * transform.scale);
        const int width = std::max(1, static_cast<int>(
            (child.rect.right - child.rect.left) * transform.scale));
        const int height = std::max(1, static_cast<int>(
            (child.rect.bottom - child.rect.top) * transform.scale));
        positions = DeferWindowPos(
            positions, child.window, nullptr,
            x, y, width, height,
            SWP_NOACTIVATE | SWP_NOZORDER);
    }
    if (positions)
        EndDeferWindowPos(positions);
}

std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty())
        return {};
    const int count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()),
        nullptr, 0);
    if (count <= 0)
        return L"Invalid UTF-8";
    std::wstring out(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()),
        out.data(), count);
    return out;
}

std::string wide_to_utf8(const std::wstring& value) {
    if (value.empty())
        return {};
    const int count = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (count <= 0)
        return {};
    std::string out(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()),
        out.data(), count, nullptr, nullptr);
    return out;
}

std::wstring window_text(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring out(
        static_cast<size_t>(std::max(0, length)) + 1,
        L'\0');
    if (length > 0) {
        GetWindowTextW(control, out.data(), length + 1);
        out.resize(static_cast<size_t>(length));
    } else {
        out.clear();
    }
    return out;
}

float slider_value(HWND control) {
    return static_cast<float>(
        SendMessageW(control, TBM_GETPOS, 0, 0)) / 100.0f;
}

HWND make_control(
    HWND parent,
    const wchar_t* class_name,
    const wchar_t* text,
    DWORD style,
    int x, int y, int width, int height,
    int id)
{
    HWND control = CreateWindowExW(
        (style & WS_BORDER) ? WS_EX_CLIENTEDGE : 0,
        class_name, text, style | WS_CHILD | WS_VISIBLE,
        x, y, width, height,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(
        control, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return control;
}

void label(HWND parent, const wchar_t* text, int y) {
    make_control(
        parent, L"STATIC", text,
        SS_LEFT,
        20, y + 4, 150, 22, 0);
}

FlyEquipment equipment_from_controls(const DialogState& state) {
    FlyEquipment equipment;
    equipment.body_skin_id = state.skin_ids[0];
    equipment.wings.skin_id = state.skin_ids[1];
    equipment.sword.skin_id = state.skin_ids[2];
    equipment.shield.skin_id = state.skin_ids[3];
    equipment.sword.length_scale = slider_value(state.sliders[0]);
    // V3 editor exposes exactly three linked trade-offs. The remaining v2
    // fields stay neutral so users cannot independently buy speed/efficiency.
    equipment.sword.recovery_scale = 1.0f;
    equipment.wings.size_scale = slider_value(state.sliders[1]);
    equipment.wings.drive_speed_scale = 1.0f;
    equipment.wings.stamina_cost_scale = 1.0f;
    equipment.shield.mass_scale = slider_value(state.sliders[2]);
    equipment.shield.size_scale = 1.0f;
    equipment.shield.stamina_cost_scale = 1.0f;
    auto packed_rgb = [](COLORREF color) {
        return (static_cast<uint32_t>(GetRValue(color)) << 16)
            | (static_cast<uint32_t>(GetGValue(color)) << 8)
            | static_cast<uint32_t>(GetBValue(color));
    };
    equipment.primary_color_rgb = packed_rgb(state.colors[0]);
    equipment.secondary_color_rgb = packed_rgb(state.colors[1]);
    equipment.wings.primary_color_rgb = packed_rgb(state.colors[2]);
    equipment.wings.secondary_color_rgb = packed_rgb(state.colors[3]);
    equipment.sword.primary_color_rgb = packed_rgb(state.colors[4]);
    equipment.sword.secondary_color_rgb = packed_rgb(state.colors[5]);
    equipment.shield.primary_color_rgb = packed_rgb(state.colors[6]);
    equipment.shield.secondary_color_rgb = packed_rgb(state.colors[7]);
    return equipment;
}

void update_derived(DialogState& state) {
    const FlyEquipment equipment = equipment_from_controls(state);
    for (size_t i = 0; i < state.sliders.size(); ++i) {
        wchar_t value[32]{};
        swprintf_s(
            value, L"%.2f",
            static_cast<double>(slider_value(state.sliders[i])));
        SetWindowTextW(state.slider_values[i], value);
    }
    const ArenaConfig arena;
    const CombatConfig combat;
    const EquipmentMovementStats movement =
        derive_equipment_movement_stats(equipment, arena);
    const float shield_mass_ratio =
        shield_mass(equipment) / kBaseShieldMass;

    wchar_t text[768]{};
    swprintf_s(
        text,
        L"DERIVED GAMEPLAY VALUES\r\n"
        L"Sword reach       %.3f world\r\n"
        L"Sword recovery    %.3f s\r\n"
        L"Wing size/speed   %.2f\r\n"
        L"Thrust multiplier %.3f\r\n"
        L"Top-speed mult.   %.3f\r\n"
        L"Move stamina mult %.3f\r\n"
        L"Shield drain      %.1f / s\r\n"
        L"Movement mult.    %.3f\r\n"
        L"Turn multiplier   %.3f\r\n"
        L"Parry stun base   %.3f s",
        static_cast<double>(sword_length_world(equipment)),
        static_cast<double>(
            combat.sword_recovery_base_s
            * std::clamp(equipment.sword.length_scale, 0.60f, 1.80f)
            * std::clamp(equipment.sword.recovery_scale, 0.60f, 1.80f)),
        static_cast<double>(equipment.wings.size_scale),
        static_cast<double>(
            movement.wing_thrust_multiplier
            * movement.shield_move_multiplier),
        static_cast<double>(
            movement.wing_speed_multiplier
            * movement.shield_move_multiplier),
        static_cast<double>(movement.movement_stamina_multiplier),
        static_cast<double>(
            combat.shield_hold_stamina_drain_per_s
            * shield_mass_ratio
            * equipment.shield.stamina_cost_scale),
        static_cast<double>(movement.shield_move_multiplier),
        static_cast<double>(movement.shield_turn_multiplier),
        static_cast<double>(combat.parry_stun_base_s * shield_mass_ratio));
    SetWindowTextW(state.derived, text);
    InvalidateRect(state.window, nullptr, FALSE);
}

COLORREF skin_color(int id, COLORREF neutral, COLORREF warm, COLORREF cool) {
    if (id == 1) return warm;
    if (id == 2) return cool;
    return neutral;
}

void paint_preview(DialogState& state, HDC dc) {
    const int saved_dc = SaveDC(dc);
    const LayoutTransform transform = layout_transform(state);
    SetGraphicsMode(dc, GM_ADVANCED);
    XFORM world{
        transform.scale, 0.0f, 0.0f, transform.scale,
        static_cast<float>(transform.offset_x),
        static_cast<float>(transform.offset_y)};
    SetWorldTransform(dc, &world);

    RECT frame{540, 25, 1010, 275};
    HBRUSH background = CreateSolidBrush(RGB(25, 30, 42));
    FillRect(dc, &frame, background);
    DeleteObject(background);
    FrameRect(dc, &frame, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));

    const FlyEquipment equipment = equipment_from_controls(state);
    const int body_id = static_cast<int>(equipment.body_skin_id);
    const int wing_id = static_cast<int>(equipment.wings.skin_id);
    const int sword_id = static_cast<int>(equipment.sword.skin_id);
    const int shield_id = static_cast<int>(equipment.shield.skin_id);

    const COLORREF body_color = state.colors[0];
    const COLORREF body_color_2 = state.colors[1];
    const COLORREF wing_color = state.colors[2];
    const COLORREF wing_color_2 = state.colors[3];
    const COLORREF sword_color = state.colors[4];
    const COLORREF sword_color_2 = state.colors[5];
    const COLORREF shield_color = state.colors[6];
    const COLORREF shield_color_2 = state.colors[7];

    const int cx = 745;
    const int cy = 145;
    const int wing_w = static_cast<int>(45.0f * wing_size_scale(equipment));
    HBRUSH wing = CreateSolidBrush(wing_color);
    HBRUSH wing_secondary = CreateSolidBrush(wing_color_2);
    HBRUSH body = CreateSolidBrush(body_color);
    HBRUSH body_secondary = CreateSolidBrush(body_color_2);
    HBRUSH shield = CreateSolidBrush(shield_color);
    HBRUSH shield_secondary = CreateSolidBrush(shield_color_2);
    HPEN outline = CreatePen(PS_SOLID, 3, RGB(8, 10, 15));
    HPEN sword = CreatePen(PS_SOLID, 5, sword_color);
    HPEN sword_secondary = CreatePen(PS_SOLID, 3, sword_color_2);
    HGDIOBJ old_brush = SelectObject(dc, wing);
    HGDIOBJ old_pen = SelectObject(dc, outline);
    if (wing_id == 1) {
        Ellipse(dc, cx - 58, cy - wing_w, cx + 12, cy - 12);
        Ellipse(dc, cx - 58, cy + 12, cx + 12, cy + wing_w);
        MoveToEx(dc, cx + 4, cy - 16, nullptr);
        LineTo(dc, cx - 48, cy - wing_w / 2);
        MoveToEx(dc, cx + 4, cy + 16, nullptr);
        LineTo(dc, cx - 48, cy + wing_w / 2);
    } else if (wing_id == 2) {
        Ellipse(dc, cx - 38, cy - wing_w, cx + 12, cy - 8);
        Ellipse(dc, cx - 58, cy - wing_w - 8, cx - 16, cy - 20);
        Ellipse(dc, cx - 38, cy + 8, cx + 12, cy + wing_w);
        Ellipse(dc, cx - 58, cy + 20, cx - 16, cy + wing_w + 8);
    } else if (wing_id == 3) {
        Ellipse(dc, cx - 64, cy - wing_w / 2 - 9, cx + 10, cy - wing_w / 2 + 3);
        Ellipse(dc, cx - 64, cy + wing_w / 2 - 3, cx + 10, cy + wing_w / 2 + 9);
    } else if (wing_id == 4) {
        Ellipse(dc, cx - 42, cy - wing_w, cx + 12, cy - 10);
        Ellipse(dc, cx - 70, cy - wing_w, cx - 14, cy - 16);
        Ellipse(dc, cx - 42, cy + 10, cx + 12, cy + wing_w);
        Ellipse(dc, cx - 70, cy + 16, cx - 14, cy + wing_w);
    } else if (wing_id == 5) {
        for (int feather = 0; feather < 3; ++feather) {
            Ellipse(dc, cx - 42 - feather * 13, cy - 18 - feather * 8,
                    cx + 4 - feather * 13, cy - 7 - feather * 8);
            Ellipse(dc, cx - 42 - feather * 13, cy + 7 + feather * 8,
                    cx + 4 - feather * 13, cy + 18 + feather * 8);
        }
    } else {
        Ellipse(dc, cx - 45, cy - wing_w, cx + 10, cy - 5);
        Ellipse(dc, cx - 45, cy + 5, cx + 10, cy + wing_w);
    }
    // The lightweight GDI preview shows the same component endpoints as the
    // arena gradient with a secondary inner lobe.
    SelectObject(dc, wing_secondary);
    Ellipse(dc, cx - 36, cy - wing_w + 8, cx - 3, cy - 10);
    Ellipse(dc, cx - 36, cy + 10, cx - 3, cy + wing_w - 8);
    // Match the arena model's canonical abdomen/thorax/head proportions.
    constexpr float model = 1.45f;
    const int abdomen_rx = static_cast<int>((body_id == 1 ? 22.0f
        : body_id == 2 ? 16.0f : 18.0f) * model);
    const int abdomen_ry = static_cast<int>((body_id == 1 ? 11.0f
        : body_id == 2 ? 17.0f : 14.0f) * model);
    const int thorax_rx = static_cast<int>((body_id == 1 ? 13.0f
        : body_id == 2 ? 18.0f : 15.0f) * model);
    const int thorax_ry = static_cast<int>((body_id == 1 ? 17.0f
        : body_id == 2 ? 13.0f : 15.5f) * model);
    const int abdomen_x = cx - static_cast<int>(17.0f * model);
    const int thorax_x = cx + static_cast<int>(1.0f * model);
    const int head_x = cx + static_cast<int>(20.0f * model);
    const int head_r = static_cast<int>(14.5f * model);

    SelectObject(dc, body_secondary);
    Ellipse(dc, abdomen_x - abdomen_rx, cy - abdomen_ry,
        abdomen_x + abdomen_rx, cy + abdomen_ry);
    SelectObject(dc, body);
    Ellipse(dc, thorax_x - thorax_rx, cy - thorax_ry,
        thorax_x + thorax_rx, cy + thorax_ry);
    Ellipse(dc, head_x - head_r, cy - head_r,
        head_x + head_r, cy + head_r);
    if (body_id == 1) {
        MoveToEx(dc, cx - static_cast<int>(35.0f * model),
            cy - static_cast<int>(5.0f * model), nullptr);
        LineTo(dc, cx - static_cast<int>(43.0f * model), cy);
        LineTo(dc, cx - static_cast<int>(35.0f * model),
            cy + static_cast<int>(5.0f * model));
    } else if (body_id == 2) {
        SelectObject(dc, body_secondary);
        const int rear_x = cx - static_cast<int>(31.0f * model);
        Ellipse(dc, rear_x - static_cast<int>(9.0f * model),
            cy - static_cast<int>(12.0f * model),
            rear_x + static_cast<int>(9.0f * model),
            cy + static_cast<int>(12.0f * model));
    } else if (body_id == 3) {
        Rectangle(dc, cx - static_cast<int>(37.0f * model),
            cy - static_cast<int>(8.0f * model),
            cx - static_cast<int>(25.0f * model),
            cy + static_cast<int>(8.0f * model));
    } else if (body_id == 4) {
        MoveToEx(dc, cx - static_cast<int>(36.0f * model),
            cy - static_cast<int>(12.0f * model), nullptr);
        LineTo(dc, cx - static_cast<int>(26.0f * model),
            cy - static_cast<int>(18.0f * model));
        MoveToEx(dc, cx - static_cast<int>(36.0f * model),
            cy + static_cast<int>(12.0f * model), nullptr);
        LineTo(dc, cx - static_cast<int>(26.0f * model),
            cy + static_cast<int>(18.0f * model));
    } else if (body_id == 5) {
        Ellipse(dc, abdomen_x - static_cast<int>(10.0f * model),
            cy - static_cast<int>(10.0f * model),
            abdomen_x + static_cast<int>(10.0f * model),
            cy + static_cast<int>(10.0f * model));
    }
    SelectObject(dc, GetStockObject(BLACK_BRUSH));
    Ellipse(dc, head_x + 3, cy - 12, head_x + 12, cy - 4);
    Ellipse(dc, head_x + 3, cy + 4, head_x + 12, cy + 12);
    SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    SelectObject(dc, sword);
    const float sword_angle = -1.22f;
    const float sword_length = 54.0f * model * std::clamp(
        equipment.sword.length_scale, 0.60f, 1.80f);
    const int sword_base_x = cx + static_cast<int>(2.0f * model);
    const int sword_base_y = cy;
    const int sword_tip_x = cx + static_cast<int>(
        std::cos(sword_angle) * sword_length);
    const int sword_tip_y = cy + static_cast<int>(
        std::sin(sword_angle) * sword_length);
    const int sword_mid_x = (sword_base_x + sword_tip_x) / 2;
    const int sword_mid_y = (sword_base_y + sword_tip_y) / 2;
    if (sword_id == 1) {
        MoveToEx(dc, sword_base_x, sword_base_y, nullptr);
        LineTo(dc, sword_mid_x - 5, sword_mid_y - 2);
        LineTo(dc, sword_tip_x, sword_tip_y);
    } else if (sword_id == 3) {
        HPEN heavy_sword = CreatePen(PS_SOLID, 9, sword_color);
        SelectObject(dc, heavy_sword);
        MoveToEx(dc, sword_base_x, sword_base_y, nullptr);
        LineTo(dc, sword_tip_x, sword_tip_y);
        SelectObject(dc, sword);
        DeleteObject(heavy_sword);
    } else if (sword_id == 4) {
        MoveToEx(dc, sword_base_x - 3, sword_base_y - 2, nullptr);
        LineTo(dc, sword_tip_x, sword_tip_y);
        MoveToEx(dc, sword_base_x + 3, sword_base_y + 2, nullptr);
        LineTo(dc, sword_tip_x, sword_tip_y);
    } else {
        MoveToEx(dc, sword_base_x, sword_base_y, nullptr);
        LineTo(dc, sword_tip_x, sword_tip_y);
        if (sword_id == 2) {
            LineTo(dc, sword_tip_x - 11, sword_tip_y - 3);
            MoveToEx(dc, sword_tip_x, sword_tip_y, nullptr);
            LineTo(dc, sword_tip_x + 2, sword_tip_y + 11);
        }
        if (sword_id == 5)
            Ellipse(dc, sword_tip_x - 7, sword_tip_y - 7,
                sword_tip_x + 7, sword_tip_y + 7);
    }
    SelectObject(dc, sword_secondary);
    MoveToEx(dc, sword_mid_x, sword_mid_y, nullptr);
    LineTo(dc, sword_tip_x, sword_tip_y);
    SelectObject(dc, outline);
    SelectObject(dc, shield);
    const float shield_scale = model * std::clamp(
        equipment.shield.size_scale, 0.65f, 1.60f);
    const int shield_rx = static_cast<int>((shield_id == 1 ? 16.0f
        : shield_id == 2 ? 11.0f : 13.0f) * shield_scale);
    const int shield_ry = static_cast<int>((shield_id == 1 ? 16.0f
        : shield_id == 2 ? 20.0f : 17.0f) * shield_scale);
    const float shield_angle = 1.10f;
    const float shield_offset = 8.2f * model;
    const int shield_center_x = cx + static_cast<int>(
        std::cos(shield_angle) * shield_offset);
    const int shield_center_y = cy + static_cast<int>(
        std::sin(shield_angle) * shield_offset);
    Ellipse(
        dc,
        shield_center_x - shield_rx,
        shield_center_y - shield_ry,
        shield_center_x + shield_rx,
        shield_center_y + shield_ry);
    SelectObject(dc, shield_secondary);
    Ellipse(
        dc,
        shield_center_x - shield_rx / 2,
        shield_center_y - shield_ry / 2,
        shield_center_x + shield_rx / 2,
        shield_center_y + shield_ry / 2);
    if (shield_id == 1) {
        Ellipse(dc, shield_center_x - 7, shield_center_y - 7,
            shield_center_x + 7, shield_center_y + 7);
    } else if (shield_id == 2) {
        MoveToEx(dc, shield_center_x, shield_center_y - shield_ry, nullptr);
        LineTo(dc, shield_center_x, shield_center_y + shield_ry);
    } else if (shield_id == 3) {
        Rectangle(dc, shield_center_x - shield_rx / 2,
            shield_center_y - shield_ry / 2,
            shield_center_x + shield_rx / 2,
            shield_center_y + shield_ry / 2);
    } else if (shield_id == 4) {
        MoveToEx(dc, shield_center_x - shield_rx, shield_center_y, nullptr);
        LineTo(dc, shield_center_x + shield_rx, shield_center_y);
    } else if (shield_id == 5) {
        Ellipse(dc, shield_center_x - shield_rx / 2,
            shield_center_y - shield_ry / 2,
            shield_center_x + shield_rx / 2,
            shield_center_y + shield_ry / 2);
    }

    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(sword_secondary);
    DeleteObject(sword);
    DeleteObject(outline);
    DeleteObject(shield_secondary);
    DeleteObject(shield);
    DeleteObject(body);
    DeleteObject(body_secondary);
    DeleteObject(wing_secondary);
    DeleteObject(wing);
    RestoreDC(dc, saved_dc);
}

void paint_skin_card(
    const DialogState& state,
    const DRAWITEMSTRUCT& item)
{
    const int offset = static_cast<int>(item.CtlID) - kIdSkinCardBase;
    if (offset < 0 || offset >= 4 * kSkinCount)
        return;
    const int category = offset / kSkinCount;
    const int skin = offset % kSkinCount;
    const bool selected = state.skin_ids[static_cast<size_t>(category)]
        == static_cast<uint32_t>(skin);

    HDC dc = item.hDC;
    RECT rect = item.rcItem;
    HBRUSH background = CreateSolidBrush(
        selected ? RGB(45, 92, 105) : RGB(35, 39, 48));
    FillRect(dc, &rect, background);
    DeleteObject(background);

    HPEN border = CreatePen(
        PS_SOLID, selected ? 3 : 1,
        selected ? RGB(90, 225, 205) : RGB(115, 125, 140));
    HGDIOBJ old_pen = SelectObject(dc, border);
    HGDIOBJ old_brush = SelectObject(
        dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);

    const COLORREF tint = skin_color(
        skin, RGB(205, 210, 220), RGB(235, 80, 90), RGB(75, 145, 245));
    HPEN shape_pen = CreatePen(PS_SOLID, 3, tint);
    SelectObject(dc, shape_pen);
    const int x = (rect.left + rect.right) / 2;
    const int y = (rect.top + rect.bottom) / 2;
    if (category == 0) {
        if (skin == 0) Ellipse(dc, x - 15, y - 10, x + 16, y + 10);
        if (skin == 1) {
            Ellipse(dc, x - 17, y - 8, x + 10, y + 8);
            MoveToEx(dc, x - 17, y - 7, nullptr);
            LineTo(dc, x - 25, y);
            LineTo(dc, x - 17, y + 7);
        }
        if (skin == 2) {
            Ellipse(dc, x - 18, y - 11, x + 2, y + 11);
            Ellipse(dc, x + 1, y - 9, x + 19, y + 9);
        }
        if (skin == 3) { Rectangle(dc, x - 15, y - 9, x + 15, y + 9); }
        if (skin == 4) { Ellipse(dc, x - 17, y - 7, x + 17, y + 7); MoveToEx(dc,x-8,y-11,nullptr); LineTo(dc,x+8,y-11); }
        if (skin == 5) { Ellipse(dc, x - 14, y - 14, x + 14, y + 14); }
    } else if (category == 1) {
        Ellipse(dc, x - 18, y - 17, x + 8, y - 1);
        Ellipse(dc, x - 18, y + 1, x + 8, y + 17);
        if (skin == 1) {
            MoveToEx(dc, x - 15, y - 8, nullptr);
            LineTo(dc, x + 15, y);
            LineTo(dc, x - 15, y + 8);
        } else if (skin == 2) {
            Ellipse(dc, x - 27, y - 18, x - 9, y - 5);
            Ellipse(dc, x - 27, y + 5, x - 9, y + 18);
        }
    } else if (category == 2) {
        MoveToEx(dc, x - 20, y + 12, nullptr);
        LineTo(dc, x + 18, y - 12);
        if (skin == 1) {
            MoveToEx(dc, x - 8, y + 4, nullptr);
            LineTo(dc, x + 5, y - 14);
        } else if (skin == 2) {
            MoveToEx(dc, x + 18, y - 12, nullptr);
            LineTo(dc, x + 9, y - 15);
            MoveToEx(dc, x + 18, y - 12, nullptr);
            LineTo(dc, x + 14, y - 4);
        }
        if (skin == 3) { MoveToEx(dc,x-12,y+9,nullptr); LineTo(dc,x+16,y+9); }
        if (skin == 4) { MoveToEx(dc,x-14,y+11,nullptr); LineTo(dc,x+15,y-11); MoveToEx(dc,x-10,y+14,nullptr); LineTo(dc,x+18,y-8); }
        if (skin == 5) { MoveToEx(dc,x-17,y,nullptr); LineTo(dc,x+18,y); }
    } else {
        if (skin == 0) Ellipse(dc, x - 16, y - 16, x + 16, y + 16);
        if (skin == 1) {
            Ellipse(dc, x - 17, y - 17, x + 17, y + 17);
            Ellipse(dc, x - 5, y - 5, x + 5, y + 5);
        }
        if (skin == 2) {
            Ellipse(dc, x - 12, y - 19, x + 12, y + 19);
            MoveToEx(dc, x, y - 18, nullptr);
            LineTo(dc, x, y + 18);
        }
        if (skin == 3) Rectangle(dc, x - 15, y - 15, x + 15, y + 15);
        if (skin == 4) { Ellipse(dc,x-17,y-12,x+17,y+12); MoveToEx(dc,x-16,y,nullptr); LineTo(dc,x+16,y); }
        if (skin == 5) { Ellipse(dc,x-15,y-15,x+15,y+15); Ellipse(dc,x-9,y-9,x+9,y+9); }
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(235, 238, 245));
    const wchar_t* names[] = {L"Classic", L"Ruby", L"Azure", L"Onyx", L"Aurora", L"Royal"};
    RECT text_rect = rect;
    text_rect.top = rect.bottom - 17;
    DrawTextW(
        dc, names[skin], -1, &text_rect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(shape_pen);
    DeleteObject(border);
}

void paint_color_button(
    const DialogState& state,
    const DRAWITEMSTRUCT& item)
{
    const size_t index = static_cast<size_t>(item.CtlID - kIdColorBase);
    if (index >= state.colors.size())
        return;
    HBRUSH fill = CreateSolidBrush(state.colors[index]);
    FillRect(item.hDC, &item.rcItem, fill);
    DeleteObject(fill);
    FrameRect(
        item.hDC, &item.rcItem,
        reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    SetBkMode(item.hDC, TRANSPARENT);
    const int luminance = GetRValue(state.colors[index]) * 299
        + GetGValue(state.colors[index]) * 587
        + GetBValue(state.colors[index]) * 114;
    SetTextColor(item.hDC, luminance > 140000 ? RGB(20, 24, 30) : RGB(245, 247, 250));
    RECT text_rect = item.rcItem;
    DrawTextW(
        item.hDC, (index % 2) == 0 ? L"PRIMARY" : L"SECONDARY", -1,
        &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void choose_color(DialogState& state, size_t index) {
    static COLORREF custom_colors[16]{};
    CHOOSECOLORW picker{};
    picker.lStructSize = sizeof(picker);
    picker.hwndOwner = state.window;
    picker.rgbResult = state.colors[index];
    picker.lpCustColors = custom_colors;
    picker.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&picker)) {
        state.colors[index] = picker.rgbResult;
        InvalidateRect(state.color_buttons[index], nullptr, TRUE);
        update_derived(state);
    }
}

std::string make_uuid() {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    std::ostringstream out;
    out << "fly-" << std::hex << std::setfill('0')
        << std::setw(16) << static_cast<uint64_t>(counter.QuadPart)
        << '-' << std::setw(16) << GetTickCount64();
    return out.str();
}

void save_profile(DialogState& state) {
    FlyProfile profile;
    profile.identity.name = wide_to_utf8(window_text(state.name));
    profile.identity.uuid = make_uuid();
    profile.identity.author = "local";
    profile.identity.lineage = "create-fly-ui-v4-component-gradients";
    profile.equipment = equipment_from_controls(state);

    std::string validation_error;
    std::vector<std::string> warnings;
    if (!validate_and_normalize_fly_profile(
            profile, validation_error, &warnings))
    {
        MessageBoxW(
            state.window,
            utf8_to_wide(validation_error).c_str(),
            L"Cannot create fly",
            MB_OK | MB_ICONERROR);
        return;
    }

    wchar_t filename[MAX_PATH] = L"NewFly.flypack";
    std::wstring initial_directory;
    try {
        initial_directory = (fs::current_path() / "data" / "flies").wstring();
    } catch (...) {
        initial_directory.clear();
    }

    const wchar_t filter[] =
        L"FlyArena fly package (*.flypack)\0*.flypack\0All files (*.*)\0*.*\0\0";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = state.window;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = filename;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrInitialDir =
        initial_directory.empty() ? nullptr : initial_directory.c_str();
    dialog.lpstrDefExt = L"flypack";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&dialog))
        return;

    const std::string path = wide_to_utf8(filename);
    std::string save_error;
    if (!save_flypack(path, profile, save_error)) {
        MessageBoxW(
            state.window,
            utf8_to_wide(save_error).c_str(),
            L"Could not save fly",
            MB_OK | MB_ICONERROR);
        return;
    }

    state.saved = true;
    state.saved_path = path;
    DestroyWindow(state.window);
}

void create_controls(DialogState& state) {
    HWND parent = state.window;
    label(parent, L"Name", 24);
    state.name = make_control(
        parent, L"EDIT", L"New Fly",
        WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        170, 22, 330, 25, kIdName);

    make_control(
        parent, L"STATIC",
        L"PHYSICAL / GAMEPLAY ASSUMPTIONS  (separate from skins)",
        SS_LEFT,
        20, 58, 480, 22, 0);

    const wchar_t* value_labels[] = {
        L"SWORD · length / inverse attack speed",
        L"WINGS · movement speed / stamina burden",
        L"SHIELD · stun power / movement + stamina burden"
    };
    const int minimums[] = {60, 65, 60};
    const int maximums[] = {180, 160, 200};
    const int defaults[] = {130, 112, 100};
    const int value_ids[] = {
        kIdSwordLength, kIdWingSize, kIdShieldMass
    };
    for (size_t i = 0; i < kPhysicalSliderCount; ++i) {
        const int y = 84 + static_cast<int>(i) * 72;
        make_control(
            parent, L"STATIC", value_labels[i], SS_LEFT,
            20, y, 430, 22, 0);
        state.sliders[i] = make_control(
            parent, TRACKBAR_CLASSW, L"",
            WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ,
            20, y + 24, 430, 34, value_ids[i]);
        SendMessageW(
            state.sliders[i], TBM_SETRANGE, TRUE,
            MAKELPARAM(minimums[i], maximums[i]));
        SendMessageW(
            state.sliders[i], TBM_SETPOS, TRUE, defaults[i]);
        state.slider_values[i] = make_control(
            parent, L"STATIC", L"1.00", SS_RIGHT,
            455, y + 29, 50, 22, 0);
    }

    make_control(
        parent, L"STATIC",
        L"COSMETIC SKINS  (silhouette only — no physics changes)",
        SS_LEFT,
        540, 288, 450, 22, 0);
    const wchar_t* skin_labels[] = {
        L"BODY", L"WINGS", L"SWORD", L"SHIELD"};
    for (int category = 0; category < 4; ++category) {
        const int y = 316 + category * 68;
        make_control(
            parent, L"STATIC", skin_labels[category], SS_LEFT,
            540, y + 18, 60, 22, 0);
        for (int skin = 0; skin < kSkinCount; ++skin) {
            const int index = category * kSkinCount + skin;
            state.skin_cards[static_cast<size_t>(index)] = make_control(
                parent, L"BUTTON", L"",
                WS_TABSTOP | BS_OWNERDRAW,
                603 + skin * 65, y, 61, 56,
                kIdSkinCardBase + index);
        }
    }

    make_control(
        parent, L"STATIC", L"SEPARATE TWO-COLOR GRADIENTS  (cosmetic only)",
        SS_LEFT, 540, 590, 440, 22, 0);
    const wchar_t* color_labels[] = {
        L"BODY", L"WINGS", L"SWORD", L"SHIELD"};
    for (size_t component = 0; component < 4; ++component) {
        const int y = 618 + static_cast<int>(component) * 42;
        make_control(
            parent, L"STATIC", color_labels[component], SS_LEFT,
            540, y + 7, 70, 22, 0);
        for (size_t endpoint = 0; endpoint < 2; ++endpoint) {
            const size_t index = component * 2 + endpoint;
            state.color_buttons[index] = make_control(
                parent, L"BUTTON", L"", WS_TABSTOP | BS_OWNERDRAW,
                615 + static_cast<int>(endpoint) * 145, y, 135, 34,
                kIdColorBase + static_cast<int>(index));
        }
    }

    state.derived = make_control(
        parent, L"STATIC", L"",
        SS_LEFT,
        20, 315, 500, 300, 0);

    make_control(
        parent, L"BUTTON", L"Save .flypack",
        WS_TABSTOP | BS_DEFPUSHBUTTON,
        760, 795, 135, 34, kIdSave);
    make_control(
        parent, L"BUTTON", L"Cancel",
        WS_TABSTOP | BS_PUSHBUTTON,
        905, 795, 90, 34, kIdCancel);

    update_derived(state);
}

LRESULT CALLBACK dialog_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam)
{
    auto* state = reinterpret_cast<DialogState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state = static_cast<DialogState*>(create->lpCreateParams);
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
        capture_layout(*state);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize.x = 780;
        info->ptMinTrackSize.y = 700;
        return 0;
    }
    case WM_SIZE:
        layout_controls(*state);
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(window, &client);
        FillRect(
            reinterpret_cast<HDC>(wparam), &client,
            state->background_brush);
        return 1;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC control_dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(control_dc, RGB(226, 232, 242));
        SetBkMode(control_dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(state->background_brush);
    }
    case WM_CTLCOLOREDIT: {
        HDC control_dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(control_dc, RGB(235, 240, 248));
        SetBkColor(control_dc, RGB(35, 42, 55));
        return reinterpret_cast<LRESULT>(state->background_brush);
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        const int notification = HIWORD(wparam);
        if (id >= kIdSkinCardBase
            && id < kIdSkinCardBase + 4 * kSkinCount
            && notification == BN_CLICKED)
        {
            const int offset = id - kIdSkinCardBase;
            const int category = offset / kSkinCount;
            state->skin_ids[static_cast<size_t>(category)] =
                static_cast<uint32_t>(offset % kSkinCount);
            for (int skin = 0; skin < kSkinCount; ++skin) {
                InvalidateRect(
                    state->skin_cards[
                        static_cast<size_t>(category * kSkinCount + skin)],
                    nullptr, TRUE);
            }
            update_derived(*state);
            return 0;
        }
        if (id >= kIdColorBase
            && id < kIdColorBase + static_cast<int>(kColorCount)
            && notification == BN_CLICKED)
        {
            choose_color(*state, static_cast<size_t>(id - kIdColorBase));
            return 0;
        }
        if (id == kIdSave && notification == BN_CLICKED) {
            save_profile(*state);
            return 0;
        }
        if (id == kIdCancel && notification == BN_CLICKED) {
            DestroyWindow(window);
            return 0;
        }
        if (notification == EN_CHANGE || notification == CBN_SELCHANGE) {
            update_derived(*state);
            return 0;
        }
        break;
    }
    case WM_HSCROLL:
        update_derived(*state);
        return 0;
    case WM_DRAWITEM:
        if (reinterpret_cast<const DRAWITEMSTRUCT*>(lparam)->CtlID
                >= kIdColorBase
            && reinterpret_cast<const DRAWITEMSTRUCT*>(lparam)->CtlID
                < kIdColorBase + static_cast<int>(kColorCount))
        {
            paint_color_button(
                *state,
                *reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
        } else {
            paint_skin_card(
                *state,
                *reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
        }
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        paint_preview(*state, dc);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

bool show_create_fly_dialog(
    HWND owner,
    std::string& saved_path,
    std::string& error)
{
    saved_path.clear();
    error.clear();

    INITCOMMONCONTROLSEX common_controls{};
    common_controls.dwSize = sizeof(common_controls);
    common_controls.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&common_controls);

    const wchar_t* class_name = L"FlyArenaCreateFlyDialogV1";
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = dialog_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground =
        reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    window_class.lpszClassName = class_name;
    if (!RegisterClassExW(&window_class)) {
        const DWORD code = GetLastError();
        if (code != ERROR_CLASS_ALREADY_EXISTS) {
            error = "Could not register Create Fly window class";
            return false;
        }
    }

    DialogState state;
    state.background_brush = CreateSolidBrush(RGB(20, 25, 35));
    RECT owner_rect{};
    GetWindowRect(owner, &owner_rect);
    const int width = 1040;
    const int height = 880;
    const int x = owner_rect.left
        + std::max(0L, (owner_rect.right - owner_rect.left - width) / 2);
    const int y = owner_rect.top
        + std::max(0L, (owner_rect.bottom - owner_rect.top - height) / 2);

    HWND window = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        class_name,
        L"Create Fly — data-only .flypack",
        WS_CAPTION | WS_SYSMENU | WS_POPUP
            | WS_THICKFRAME | WS_MAXIMIZEBOX,
        x, y, width, height,
        owner, nullptr,
        GetModuleHandleW(nullptr),
        &state);
    if (!window) {
        DeleteObject(state.background_brush);
        error = "Could not create Create Fly window";
        return false;
    }

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

    DeleteObject(state.background_brush);

    saved_path = state.saved_path;
    error = state.error;
    return state.saved;
}

} // namespace flyarena
