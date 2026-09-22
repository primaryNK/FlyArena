#pragma once
#include "arena_sim.h"
#include "combat_event.h"
#include <cstdint>
#include <vector>
namespace flyarena {
struct CombatConfig {
    float sword_trigger_threshold = 0.62f;
    float sword_rearm_threshold = 0.26f;

    // Sword animation is deterministic: quick slash, then length-based return.
    float sword_ready_angle = -1.22f;
    float sword_strike_angle = 1.18f;
    float sword_swing_duration_s = 0.115f;
    float sword_recovery_base_s = 0.48f;
    float sword_min_hit_angular_speed = 5.5f;
    int32_t sword_hit_damage = 12;

    // Shield is a separate hold action.
    float shield_raise_threshold = 0.62f;
    float shield_hold_threshold = 0.42f;
    float shield_rearm_threshold = 0.28f;
    int32_t shield_min_stamina_to_raise = 7;
    float shield_hold_stamina_drain_per_s = 28.0f;
    float shield_raise_duration_s = 0.10f;
    float shield_lower_duration_s = 0.13f;
    float parry_window_s = 0.165f;

    // Defender success interrupts the attacker.
    float block_stun_s = 0.18f;
    float parry_stun_base_s = 0.55f;
    float parry_stun_speed_scale_s = 0.08f;
    float parry_stun_max_s = 1.15f;

    float hit_speed_threshold = 0.34f;
    float parry_relative_speed_threshold = 0.55f;
    float parry_shield_motion_threshold = 0.12f;
    float dodge_margin_world = 0.030f;
    float dodge_lateral_speed_threshold = 0.22f;
    float hit_impulse_scale = 0.18f;
    float parry_impulse_scale = 0.27f;
    float hit_cooldown_s = 0.16f;
    float block_cooldown_s = 0.10f;
    float dodge_cooldown_s = 0.28f;
};
void step_equipment(FlyBodyState&,const ArenaControlFrame&,const CombatConfig&,float);
void resolve_equipment_combat(FlyBodyState&,FlyBodyState&,float,const CombatConfig&,double,uint64_t&,std::vector<CombatEvent>&);
} // namespace flyarena
