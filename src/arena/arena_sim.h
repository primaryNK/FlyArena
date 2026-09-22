#pragma once

#include "arena_control.h"
#include "fly_visual_loadout.h"
#include "fly_equipment.h"

#include <cmath>
#include <cstdint>
#include <string>

namespace flyarena {

struct FlyIdentity {
    std::string name;
    std::string author = "local";
    std::string lineage = "untrained-v063";
};

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

enum class SwordPhase : uint32_t { Ready=0, Swing=1, Recovery=2 };

struct FlyBodyState {
    FlyIdentity identity;
    FlyEquipment equipment;
    FlyVisualLoadout visual;
    Vec2 position;
    float heading_rad = 0.0f;

    // Kinetic flight state.
    Vec2 velocity{};
    float angular_velocity = 0.0f;

    // Derived/compatibility telemetry.
    float forward_speed = 0.0f;
    float turn_rate = 0.0f;

    // First health model. Weapons do not damage HP yet; v0.6.3d only applies
    // wall-impact damage so collision tuning can be observed before combat.
    int32_t hp = 100;
    int32_t max_hp = 100;
    // Discrete game-state stamina. Sub-point physics changes accumulate
    // internally until a whole stamina point is reached.
    int32_t stamina = 100;
    int32_t max_stamina = 100;
    float stamina_fractional_delta = 0.0f;
    float stamina_spent_points = 0.0f;
    float stamina_spent_movement_points = 0.0f;
    float stamina_spent_shield_points = 0.0f;
    float stamina_recovered_points = 0.0f;
    float stamina_recovery_eligible_s = 0.0f;
    float stamina_recovery_blocked_movement_s = 0.0f;
    float stamina_recovery_blocked_shield_s = 0.0f;
    bool stamina_recovery_eligible = false;

    // Articulated combat state.
    SwordPhase sword_phase = SwordPhase::Ready;
    float sword_relative_angle = -1.22f;
    float sword_previous_relative_angle = -1.22f;
    float sword_angular_velocity = 0.0f;
    bool sword_hit_active_this_step = false;
    float sword_phase_time_s = 0.0f;
    float previous_sword_drive = 0.0f;
    bool sword_trigger_armed = true;
    float sword_recovery_start_angle = -1.22f;

    float shield_relative_angle = 1.10f;
    float shield_angular_velocity = 0.0f;
    float previous_shield_drive = 0.0f;
    bool shield_raised = false;
    float shield_deploy = 0.0f;
    float shield_deploy_velocity = 0.0f;
    bool shield_trigger_armed = true;
    float parry_window_remaining_s = 0.0f;

    float stun_remaining_s = 0.0f;
    float weapon_contact_cooldown_s = 0.0f;
    float dodge_cooldown_s = 0.0f;
    float last_wall_impact_speed = 0.0f;

    float distance_travelled = 0.0f;
    int32_t wall_damage_taken = 0;
    uint32_t wall_contacts = 0;
    uint32_t body_contacts = 0;
    uint32_t sword_swings = 0;
    uint32_t shield_raises = 0;
    uint32_t hits_landed = 0;
    uint32_t blocks = 0;
    uint32_t parries = 0;
    uint32_t dodges = 0;
};

struct ArenaSense {
    float opponent_distance = 0.0f;
    float opponent_bearing_rad = 0.0f;

    float visual_left = 0.0f;
    float visual_right = 0.0f;

    float body_left = 0.0f;
    float body_right = 0.0f;
    float body_center = 0.0f;

    bool opponent_visible = false;
    bool opponent_contact = false;
    bool wall_contact = false;
};

struct ArenaConfig {
    float arena_radius = 0.90f;
    float fly_radius = 0.030f;
    float visual_range = 1.85f;
    float visual_half_fov_rad = 2.9670597f; // 170 degrees; 20-degree rear blind wedge
    float wall_sensor_margin = 0.10f;

    // "Flying spinning-top" body model.
    // Forward neural output is interpreted as thrust, not target ground speed.
    float thrust_accel = 4.8f;          // arena units / s^2
    float max_linear_speed = 1.45f;     // arena units / s
    float linear_drag_per_s = 1.05f;    // low drag -> visible inertia

    float turn_accel = 25.0f;           // rad / s^2
    float max_turn_rate = 8.5f;         // rad / s
    float angular_drag_per_s = 2.4f;    // retains angular inertia

    // Wing/stamina + heavy-shield locomotion model.
    float stamina_thrust_drain_per_s = 12.0f;
    float stamina_regen_per_s = 15.0f;
    float exhausted_thrust_multiplier = 0.35f;

    // Equipment balance values are gameplay assumptions, not measured fly
    // physiology. They are centralized so Create Fly can show the exact same
    // derived values consumed by physics.
    float wing_thrust_base = 0.72f;
    float wing_thrust_size_scale = 0.28f;
    float wing_speed_base = 0.70f;
    float wing_speed_size_scale = 0.30f;
    float shield_move_penalty_per_mass = 0.16f;
    float shield_move_multiplier_min = 0.68f;
    float shield_move_multiplier_max = 1.08f;
    float shield_turn_penalty_per_mass = 0.20f;
    float shield_turn_multiplier_min = 0.60f;
    float shield_turn_multiplier_max = 1.08f;

    // Collision energy retention.
    float wall_restitution = 0.84f;
    float body_restitution = 0.94f;
    float body_tangent_exchange = 0.10f;
    float wall_spin_kick = 1.25f;
    float body_spin_kick = 0.55f;

    // Wall HP damage: only normal impact speed above this threshold hurts.
    // Wall collision damage is unconditional with respect to shield state.
    // Raising a shield never blocks or reduces arena-wall damage.
    float wall_damage_speed_threshold = 0.22f;
    float wall_damage_per_speed = 30.0f;
    int32_t max_wall_damage_per_hit = 38;
};

struct EquipmentMovementStats {
    float wing_thrust_multiplier = 1.0f;
    float wing_speed_multiplier = 1.0f;
    float movement_stamina_multiplier = 1.0f;
    float shield_move_multiplier = 1.0f;
    float shield_turn_multiplier = 1.0f;
};

EquipmentMovementStats derive_equipment_movement_stats(
    const FlyEquipment& equipment,
    const ArenaConfig& cfg);

float wrap_angle(float a);

float apply_stamina_delta(
    FlyBodyState& body,
    float delta_points);

ArenaSense sense_other_and_wall(
    const FlyBodyState& self,
    const FlyBodyState& other,
    const ArenaConfig& cfg);

void step_body(
    FlyBodyState& body,
    const ArenaControlFrame& control,
    const ArenaConfig& cfg,
    float dt_s);

void resolve_pair_contact(
    FlyBodyState& a,
    FlyBodyState& b,
    const ArenaConfig& cfg);

} // namespace flyarena
