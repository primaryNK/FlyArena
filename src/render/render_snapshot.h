#pragma once

#include "arena_sim.h"
#include "combat_event.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace flyarena {

constexpr size_t kKeyNeuronCount = 12;

enum class ArenaPhase : uint32_t {
    Booting = 0,
    Calibrating = 1,
    Live = 2,
    Complete = 3,
    Failed = 4
};

struct FlyRenderState {
    std::string name;
    FlyVisualLoadout visual;
    uint64_t completed_training_episodes = 0;

    Vec2 position{};
    float heading_rad = 0.0f;

    float forward = 0.0f;
    float turn = 0.0f;
    float forward_speed = 0.0f;
    float collision_radius_world = 0.030f;
    int32_t hp = 100;
    int32_t max_hp = 100;
    int32_t stamina = 100;
    int32_t max_stamina = 100;
    float stamina_continuous = 100.0f;
    bool stamina_recovery_eligible = false;
    float sword_relative_angle = -1.22f;
    float shield_relative_angle = 1.10f;
    float shield_deploy = 0.0f;
    float shield_center_distance_body_radii = 0.55f;
    bool shield_raised = false;
    bool sword_swinging = false;
    float parry_window_remaining_s = 0.0f;
    float last_wall_impact_speed = 0.0f;

    float visual_left = 0.0f;
    float visual_right = 0.0f;
    float opponent_bearing_rad = 0.0f;
    bool opponent_visible = false;

    float dn_left_hz = 0.0f;
    float dn_right_hz = 0.0f;
    float mn_left_hz = 0.0f;
    float mn_right_hz = 0.0f;

    uint32_t body_contacts = 0;
    uint32_t wall_contacts = 0;
    uint32_t hits_landed = 0;
    uint32_t blocks = 0;
    uint32_t parries = 0;
    uint32_t dodges = 0;

    // Twelve deterministic high-outdegree representatives selected from the
    // descending/motor pools. Brightness is recent per-neuron spike activity.
    std::array<float, kKeyNeuronCount> key_neuron_activity{};
};

struct ArenaRenderSnapshot {
    uint64_t sequence = 0;
    double sim_time_ms = 0.0;
    double world_step_ms = 50.0;

    ArenaPhase phase = ArenaPhase::Booting;
    std::string status = "Starting...";
    bool headless_max_training = false;

    FlyRenderState red;
    FlyRenderState blue;

    float pair_distance = 0.0f;

    double match_duration_ms = 60000.0;
    int32_t match_result = 0; // 0 live/none, 1 RED, 2 BLUE, 3 DRAW
    std::string result_reason;

    std::array<CombatEvent, 4> combat_events{};
    uint32_t combat_event_count = 0;

    double neural_compute_ms_last_tick = 0.0;
};

struct SnapshotSample {
    ArenaRenderSnapshot previous;
    ArenaRenderSnapshot current;
    std::chrono::steady_clock::time_point current_published_at{};
    bool valid = false;
};

class SnapshotBuffer {
public:
    void publish(const ArenaRenderSnapshot& snapshot) {
        std::scoped_lock lock(mutex_);

        if (!has_current_) {
            previous_ = snapshot;
            current_ = snapshot;
            has_current_ = true;
        } else {
            previous_ = current_;
            current_ = snapshot;
        }

        current_published_at_ = std::chrono::steady_clock::now();
    }

    SnapshotSample sample() const {
        std::scoped_lock lock(mutex_);

        SnapshotSample s;
        if (!has_current_)
            return s;

        s.previous = previous_;
        s.current = current_;
        s.current_published_at = current_published_at_;
        s.valid = true;
        return s;
    }

private:
    mutable std::mutex mutex_;
    ArenaRenderSnapshot previous_{};
    ArenaRenderSnapshot current_{};
    std::chrono::steady_clock::time_point current_published_at_{};
    bool has_current_ = false;
};

inline float lerp_float(float a, float b, float t) {
    return a + (b - a) * t;
}

inline float lerp_angle(float a, float b, float t) {
    float d = wrap_angle(b - a);
    return wrap_angle(a + d * t);
}

inline FlyRenderState interpolate_fly(
    const FlyRenderState& a,
    const FlyRenderState& b,
    float t)
{
    FlyRenderState o = b;
    o.visual.body_scale =
        lerp_float(a.visual.body_scale, b.visual.body_scale, t);
    o.visual.wing_size_scale =
        lerp_float(a.visual.wing_size_scale, b.visual.wing_size_scale, t);
    o.visual.sword_length_scale =
        lerp_float(a.visual.sword_length_scale, b.visual.sword_length_scale, t);
    o.visual.sword_thickness_scale =
        lerp_float(a.visual.sword_thickness_scale, b.visual.sword_thickness_scale, t);
    o.visual.shield_size_scale =
        lerp_float(a.visual.shield_size_scale, b.visual.shield_size_scale, t);

    o.position.x = lerp_float(a.position.x, b.position.x, t);
    o.position.y = lerp_float(a.position.y, b.position.y, t);
    o.heading_rad = lerp_angle(a.heading_rad, b.heading_rad, t);

    o.forward = lerp_float(a.forward, b.forward, t);
    o.turn = lerp_float(a.turn, b.turn, t);
    o.forward_speed = lerp_float(a.forward_speed, b.forward_speed, t);
    o.collision_radius_world = b.collision_radius_world;
    // Integer stamina remains authoritative. The continuous value exposes the
    // simulator's real sub-point accumulator for progressive HUD feedback.
    o.stamina = b.stamina;
    o.max_stamina = b.max_stamina;
    o.stamina_continuous = lerp_float(
        a.stamina_continuous, b.stamina_continuous, t);
    o.stamina_recovery_eligible = b.stamina_recovery_eligible;
    o.sword_relative_angle = lerp_angle(a.sword_relative_angle,b.sword_relative_angle,t);
    o.shield_relative_angle = lerp_angle(a.shield_relative_angle,b.shield_relative_angle,t);
    o.shield_deploy = lerp_float(a.shield_deploy,b.shield_deploy,t);
    o.shield_center_distance_body_radii = lerp_float(
        a.shield_center_distance_body_radii,
        b.shield_center_distance_body_radii,
        t);
    o.shield_raised = b.shield_raised;
    o.sword_swinging = b.sword_swinging;
    o.parry_window_remaining_s = lerp_float(a.parry_window_remaining_s,b.parry_window_remaining_s,t);
    for (size_t i = 0; i < kKeyNeuronCount; ++i) {
        o.key_neuron_activity[i] =
            lerp_float(
                a.key_neuron_activity[i],
                b.key_neuron_activity[i],
                t);
    }
    // HP is a discrete game-state value. Never interpolate it into decimals.
    o.hp = b.hp;
    o.max_hp = b.max_hp;
    o.last_wall_impact_speed =
        lerp_float(
            a.last_wall_impact_speed,
            b.last_wall_impact_speed,
            t);

    o.visual_left = lerp_float(a.visual_left, b.visual_left, t);
    o.visual_right = lerp_float(a.visual_right, b.visual_right, t);
    o.opponent_bearing_rad =
        lerp_angle(
            a.opponent_bearing_rad,
            b.opponent_bearing_rad,
            t);
    o.opponent_visible = b.opponent_visible;

    o.dn_left_hz = lerp_float(a.dn_left_hz, b.dn_left_hz, t);
    o.dn_right_hz = lerp_float(a.dn_right_hz, b.dn_right_hz, t);
    o.mn_left_hz = lerp_float(a.mn_left_hz, b.mn_left_hz, t);
    o.mn_right_hz = lerp_float(a.mn_right_hz, b.mn_right_hz, t);
    return o;
}

inline ArenaRenderSnapshot interpolate_snapshot(
    const SnapshotSample& sample,
    float t)
{
    ArenaRenderSnapshot o = sample.current;
    o.red = interpolate_fly(sample.previous.red, sample.current.red, t);
    o.blue = interpolate_fly(sample.previous.blue, sample.current.blue, t);
    o.pair_distance = lerp_float(
        sample.previous.pair_distance,
        sample.current.pair_distance,
        t);
    return o;
}

} // namespace flyarena
