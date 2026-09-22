#pragma once

#include "fly_visual_loadout.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace flyarena {

struct SwordEquipment {
    float length_scale = 1.0f;
    // Independent gameplay recovery/cooldown tuning. Lower is faster.
    float recovery_scale = 1.0f;
    float mass_scale = 1.0f;
    float thickness_scale = 1.0f;
    uint32_t skin_id = 0;
    uint32_t primary_color_rgb = 0xF3F5FAu;
    uint32_t secondary_color_rgb = 0x8FA4C8u;
};
struct ShieldEquipment {
    float size_scale = 1.0f;
    float mass_scale = 1.0f;
    float stamina_cost_scale = 1.0f;
    uint32_t skin_id = 0;
    uint32_t primary_color_rgb = 0xEBAF3Eu;
    uint32_t secondary_color_rgb = 0xA33A5Bu;
};
struct WingEquipment {
    float size_scale = 1.0f;
    float drive_speed_scale = 1.0f;
    float stamina_cost_scale = 1.0f;
    uint32_t skin_id = 0;
    uint32_t primary_color_rgb = 0xD8F4FFu;
    uint32_t secondary_color_rgb = 0x6FB7D8u;
};
struct FlyEquipment {
    SwordEquipment sword;
    ShieldEquipment shield;
    WingEquipment wings;
    uint32_t body_skin_id = 0;
    uint32_t primary_color_rgb = 0xEBAF3Eu;   // body primary
    uint32_t secondary_color_rgb = 0xA33A5Bu; // body secondary
};

// FlyArena gameplay parameters, not measured biological constants.
constexpr float kBaseSwordLengthWorld = 0.125f;
constexpr float kBaseSwordMass = 0.12f;
constexpr float kBaseShieldRadiusWorld = 0.042f;
constexpr float kBaseShieldMass = 0.22f;

// Canonical gameplay wing hitbox, expressed in body-radius units. Each wing
// is a capsule from root to tip. These are tuned arena geometry, not measured
// Drosophila anatomy. Cosmetic skin details do not change this hitbox.
constexpr float kWingHitRootForwardBodyRadii = 0.40f;
constexpr float kWingHitRootLateralBodyRadii = 0.70f;
constexpr float kWingHitTipForwardBodyRadii = -2.65f;
constexpr float kWingHitTipLateralBodyRadii = 1.70f;
constexpr float kWingHitRadiusBodyRadii = 0.75f;

inline float sword_length_world(const FlyEquipment& e) {
    return kBaseSwordLengthWorld * std::clamp(e.sword.length_scale,0.60f,1.80f);
}
inline float sword_mass(const FlyEquipment& e) {
    return kBaseSwordMass * std::clamp(e.sword.mass_scale,0.60f,1.80f);
}
inline float sword_inertia(const FlyEquipment& e) {
    const float L=sword_length_world(e), m=sword_mass(e);
    return std::max(0.00005f,m*L*L/3.0f);
}
inline float shield_radius_world(const FlyEquipment& e) {
    return kBaseShieldRadiusWorld * std::clamp(e.shield.size_scale,0.65f,1.60f);
}
inline float shield_mass(const FlyEquipment& e) {
    return kBaseShieldMass * std::clamp(e.shield.mass_scale,0.60f,2.00f);
}
inline float smooth_shield_deploy(float deploy) {
    const float t = std::clamp(deploy, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
inline float shield_center_distance_world(
    const FlyEquipment& e,
    float body_radius_world,
    float deploy)
{
    const float radius = shield_radius_world(e);
    const float stowed = body_radius_world * 0.35f + radius * 0.15f;
    // Preserve the user-validated v0.6.4d fully raised collision center.
    const float deployed = body_radius_world + radius * 0.55f;
    return stowed
        + (deployed - stowed) * smooth_shield_deploy(deploy);
}
inline float shield_center_deploy_derivative_world(
    const FlyEquipment& e,
    float body_radius_world,
    float deploy)
{
    const float t = std::clamp(deploy, 0.0f, 1.0f);
    const float radius = shield_radius_world(e);
    const float stowed = body_radius_world * 0.35f + radius * 0.15f;
    const float deployed = body_radius_world + radius * 0.55f;
    return (deployed - stowed) * 6.0f * t * (1.0f - t);
}
inline float wing_size_scale(const FlyEquipment& e) {
    return std::clamp(e.wings.size_scale,0.65f,1.60f);
}
inline FlyVisualLoadout visual_loadout_from_equipment(const FlyEquipment& e,float body_scale=1.0f) {
    FlyVisualLoadout v;
    v.body_scale=body_scale;
    v.wing_size_scale=wing_size_scale(e);
    v.sword_length_scale=sword_length_world(e)/kBaseSwordLengthWorld;
    v.sword_thickness_scale=std::clamp(e.sword.thickness_scale,0.60f,1.80f);
    v.shield_size_scale=shield_radius_world(e)/kBaseShieldRadiusWorld;
    v.body_skin_id=e.body_skin_id; v.wing_skin_id=e.wings.skin_id;
    v.sword_skin_id=e.sword.skin_id; v.shield_skin_id=e.shield.skin_id;
    v.body_primary_color_rgb=e.primary_color_rgb;
    v.body_secondary_color_rgb=e.secondary_color_rgb;
    v.wing_primary_color_rgb=e.wings.primary_color_rgb;
    v.wing_secondary_color_rgb=e.wings.secondary_color_rgb;
    v.sword_primary_color_rgb=e.sword.primary_color_rgb;
    v.sword_secondary_color_rgb=e.sword.secondary_color_rgb;
    v.shield_primary_color_rgb=e.shield.primary_color_rgb;
    v.shield_secondary_color_rgb=e.shield.secondary_color_rgb;
    v.show_sword=true; v.show_shield=true;
    return v;
}

} // namespace flyarena
