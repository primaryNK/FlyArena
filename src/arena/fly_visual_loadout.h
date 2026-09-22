#pragma once

#include <cstdint>

namespace flyarena {

// Visual mirror of future canonical equipment/customization parameters.
//
// IMPORTANT:
// - These values are not the authoritative physics source yet.
// - V0.6.4+ should derive visual size from the same canonical sword/shield/wing
//   parameters used by physics, so appearance and collision geometry cannot
//   silently disagree.
struct FlyVisualLoadout {
    float body_scale = 1.0f;
    float wing_size_scale = 1.0f;
    float sword_length_scale = 1.0f;
    float sword_thickness_scale = 1.0f;
    float shield_size_scale = 1.0f;

    uint32_t body_skin_id = 0;
    uint32_t wing_skin_id = 0;
    uint32_t sword_skin_id = 0;
    uint32_t shield_skin_id = 0;

    // Data-only sRGB colors packed as 0xRRGGBB. Flypack v4 stores a separate
    // two-color gradient for every visible component. Cosmetics never affect
    // collision geometry or actuator physics.
    uint32_t body_primary_color_rgb = 0xEBAF3Eu;
    uint32_t body_secondary_color_rgb = 0xA33A5Bu;
    uint32_t wing_primary_color_rgb = 0xD8F4FFu;
    uint32_t wing_secondary_color_rgb = 0x6FB7D8u;
    uint32_t sword_primary_color_rgb = 0xF3F5FAu;
    uint32_t sword_secondary_color_rgb = 0x8FA4C8u;
    uint32_t shield_primary_color_rgb = 0xEBAF3Eu;
    uint32_t shield_secondary_color_rgb = 0xA33A5Bu;

    bool show_sword = true;
    bool show_shield = true;
};

} // namespace flyarena
