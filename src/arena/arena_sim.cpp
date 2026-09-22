#include "arena_sim.h"

#include <algorithm>
#include <cmath>

namespace flyarena {

static float length(Vec2 v) {
    return std::sqrt(v.x * v.x + v.y * v.y);
}

static float dot(Vec2 a, Vec2 b) {
    return a.x * b.x + a.y * b.y;
}

static Vec2 mul(Vec2 v, float s) {
    return {v.x * s, v.y * s};
}

static Vec2 add(Vec2 a, Vec2 b) {
    return {a.x + b.x, a.y + b.y};
}

static Vec2 sub(Vec2 a, Vec2 b) {
    return {a.x - b.x, a.y - b.y};
}

float wrap_angle(float a) {
    constexpr float pi = 3.14159265358979323846f;
    constexpr float two_pi = 2.0f * pi;
    while (a > pi) a -= two_pi;
    while (a < -pi) a += two_pi;
    return a;
}


float apply_stamina_delta(
    FlyBodyState& body,
    float delta_points)
{
    const float before = std::clamp(
        static_cast<float>(body.stamina)
        + body.stamina_fractional_delta,
        0.0f,
        static_cast<float>(body.max_stamina));
    if (!std::isfinite(delta_points) || delta_points == 0.0f)
        return 0.0f;

    if ((body.stamina <= 0 && delta_points < 0.0f)
        || (body.stamina >= body.max_stamina && delta_points > 0.0f))
    {
        body.stamina_fractional_delta = 0.0f;
        return 0.0f;
    }

    body.stamina_fractional_delta += delta_points;

    const int32_t whole =
        static_cast<int32_t>(
            body.stamina_fractional_delta);

    if (whole == 0) {
        const float after = std::clamp(
            static_cast<float>(body.stamina)
            + body.stamina_fractional_delta,
            0.0f,
            static_cast<float>(body.max_stamina));
        const float actual = after - before;
        if (actual < 0.0f)
            body.stamina_spent_points += -actual;
        else
            body.stamina_recovered_points += actual;
        return actual;
    }

    body.stamina =
        std::clamp<int32_t>(
            body.stamina + whole,
            0,
            body.max_stamina);

    if (body.stamina == 0 || body.stamina == body.max_stamina) {
        body.stamina_fractional_delta = 0.0f;
    } else {
        body.stamina_fractional_delta -=
            static_cast<float>(whole);
    }

    const float after = std::clamp(
        static_cast<float>(body.stamina)
        + body.stamina_fractional_delta,
        0.0f,
        static_cast<float>(body.max_stamina));
    const float actual = after - before;
    if (actual < 0.0f)
        body.stamina_spent_points += -actual;
    else
        body.stamina_recovered_points += actual;
    return actual;
}

ArenaSense sense_other_and_wall(
    const FlyBodyState& self,
    const FlyBodyState& other,
    const ArenaConfig& cfg)
{
    ArenaSense s;

    const Vec2 d{
        other.position.x - self.position.x,
        other.position.y - self.position.y
    };
    const float dist = length(d);
    const float world_angle = std::atan2(d.y, d.x);
    const float bearing = wrap_angle(world_angle - self.heading_rad);

    s.opponent_distance = dist;
    s.opponent_bearing_rad = bearing;

    if (dist <= cfg.visual_range &&
        std::fabs(bearing) <= cfg.visual_half_fov_rad)
    {
        s.opponent_visible = true;

        const float proximity =
            std::clamp(
                1.0f - dist / cfg.visual_range,
                0.0f, 1.0f);

        const float angular =
            std::clamp(
                1.0f - std::fabs(bearing)
                    / cfg.visual_half_fov_rad,
                0.0f, 1.0f);

        // Coarse looming / relative-motion salience.
        // This alters only visual stimulus strength; it does not issue a chase
        // or turn command. Positive closing speed makes an approaching opponent
        // a stronger visual event.
        float closing_speed = 0.0f;
        if (dist > 1e-6f) {
            const Vec2 los{d.x / dist, d.y / dist};
            const Vec2 relative_velocity{
                other.velocity.x - self.velocity.x,
                other.velocity.y - self.velocity.y
            };
            closing_speed =
                std::max(
                    0.0f,
                    -dot(relative_velocity, los));
        }

        const float looming =
            std::clamp(
                closing_speed
                / std::max(0.1f, cfg.max_linear_speed),
                0.0f, 1.0f);

        // Peripheral vision remains weaker but non-zero. The old ±80-degree
        // proxy caused the opponent to disappear after a single pass.
        const float angular_gain =
            0.28f + 0.72f * angular;

        const float motion_gain =
            1.0f + 0.45f * looming;

        const float strength =
            std::clamp(
                std::sqrt(proximity)
                * angular_gain
                * motion_gain,
                0.0f, 1.0f);

        if (bearing >= 0.0f)
            s.visual_left = strength;
        else
            s.visual_right = strength;
    }

    const float contact_distance = 2.0f * cfg.fly_radius;
    if (dist <= contact_distance) {
        s.opponent_contact = true;

        if (std::fabs(bearing) < 0.45f) {
            s.body_center = 1.0f;
        } else if (bearing > 0.0f) {
            s.body_left = 1.0f;
        } else {
            s.body_right = 1.0f;
        }
    }

    const float radial = length(self.position);
    const float wall_distance =
        cfg.arena_radius - cfg.fly_radius - radial;

    if (wall_distance <= cfg.wall_sensor_margin) {
        s.wall_contact = wall_distance <= 0.0f;

        const float strength =
            std::clamp(
                1.0f - wall_distance / cfg.wall_sensor_margin,
                0.0f, 1.0f);

        if (radial > 1e-6f) {
            const float wall_world_angle =
                std::atan2(self.position.y, self.position.x);

            const float wall_bearing =
                wrap_angle(
                    wall_world_angle - self.heading_rad);

            constexpr float frontal_half_angle = 0.55f;

            if (std::fabs(wall_bearing) <= frontal_half_angle) {
                s.body_center =
                    std::max(s.body_center, strength);
            } else if (wall_bearing > 0.0f) {
                s.body_left =
                    std::max(s.body_left, strength);
            } else {
                s.body_right =
                    std::max(s.body_right, strength);
            }
        } else {
            s.body_center =
                std::max(s.body_center, strength);
        }
    }

    return s;
}

EquipmentMovementStats derive_equipment_movement_stats(
    const FlyEquipment& equipment,
    const ArenaConfig& cfg)
{
    const float wings = wing_size_scale(equipment);
    const float shield_mass_ratio =
        shield_mass(equipment) / kBaseShieldMass;

    EquipmentMovementStats result;
    result.wing_thrust_multiplier =
        (cfg.wing_thrust_base
         + cfg.wing_thrust_size_scale * wings)
        * std::clamp(
            equipment.wings.drive_speed_scale, 0.65f, 1.60f);
    result.wing_speed_multiplier =
        (cfg.wing_speed_base
         + cfg.wing_speed_size_scale * wings)
        * std::clamp(
            equipment.wings.drive_speed_scale, 0.65f, 1.60f);
    result.movement_stamina_multiplier =
        wings * wings
        * std::clamp(
            equipment.wings.stamina_cost_scale, 0.60f, 1.80f);
    result.shield_move_multiplier = std::clamp(
        1.0f
            - cfg.shield_move_penalty_per_mass
              * (shield_mass_ratio - 1.0f),
        cfg.shield_move_multiplier_min,
        cfg.shield_move_multiplier_max);
    result.shield_turn_multiplier = std::clamp(
        1.0f
            - cfg.shield_turn_penalty_per_mass
              * (shield_mass_ratio - 1.0f),
        cfg.shield_turn_multiplier_min,
        cfg.shield_turn_multiplier_max);
    return result;
}

void step_body(
    FlyBodyState& body,
    const ArenaControlFrame& control,
    const ArenaConfig& cfg,
    float dt_s)
{
    dt_s = std::max(0.0f, dt_s);

    body.stun_remaining_s = std::max(0.0f, body.stun_remaining_s - dt_s);
    body.weapon_contact_cooldown_s = std::max(0.0f, body.weapon_contact_cooldown_s - dt_s);
    body.dodge_cooldown_s = std::max(0.0f, body.dodge_cooldown_s - dt_s);
    body.parry_window_remaining_s = std::max(0.0f, body.parry_window_remaining_s - dt_s);

    const EquipmentMovementStats equipment =
        derive_equipment_movement_stats(body.equipment, cfg);

    float effective_forward = std::clamp(control.forward,0.0f,1.0f);
    if (body.stun_remaining_s > 0.0f) effective_forward *= 0.12f;

    const float drain =
        cfg.stamina_thrust_drain_per_s
        * equipment.movement_stamina_multiplier
        * effective_forward
        * dt_s;

    body.stamina_recovery_eligible = false;
    if (effective_forward > 0.10f) {
        const float actual = apply_stamina_delta(body, -drain);
        body.stamina_spent_movement_points += std::max(0.0f, -actual);
        body.stamina_recovery_blocked_movement_s += dt_s;
    } else if (!body.shield_raised) {
        body.stamina_recovery_eligible = true;
        body.stamina_recovery_eligible_s += dt_s;
        apply_stamina_delta(
            body,
            cfg.stamina_regen_per_s * dt_s);
    } else {
        body.stamina_recovery_blocked_shield_s += dt_s;
    }

    float stamina_factor=1.0f;
    if (body.stamina <= 0) {
        stamina_factor=cfg.exhausted_thrust_multiplier;
    } else if (body.stamina < 20) {
        stamina_factor=
            cfg.exhausted_thrust_multiplier
            +(1.0f-cfg.exhausted_thrust_multiplier)
              *(static_cast<float>(body.stamina)/20.0f);
    }

    // Neural forward output is thrust. Heading changes do not instantly rotate
    // the velocity vector, creating visible flight inertia / drift.
    const float thrust = effective_forward*cfg.thrust_accel*equipment.wing_thrust_multiplier*equipment.shield_move_multiplier*stamina_factor;

    const Vec2 heading{
        std::cos(body.heading_rad),
        std::sin(body.heading_rad)
    };

    body.velocity.x += heading.x * thrust * dt_s;
    body.velocity.y += heading.y * thrust * dt_s;

    const float linear_decay =
        std::exp(-cfg.linear_drag_per_s * dt_s);

    body.velocity.x *= linear_decay;
    body.velocity.y *= linear_decay;

    float speed = length(body.velocity);
    const float equipment_max_speed = cfg.max_linear_speed*equipment.wing_speed_multiplier*equipment.shield_move_multiplier;
    if (speed > equipment_max_speed && speed > 1e-6f) {
        const float s = equipment_max_speed / speed;
        body.velocity.x *= s;
        body.velocity.y *= s;
        speed = equipment_max_speed;
    }

    // Turn output is angular acceleration rather than direct angular speed.
    const float turn_input = (body.stun_remaining_s>0.0f) ? std::clamp(control.turn,-1.0f,1.0f)*0.18f : std::clamp(control.turn,-1.0f,1.0f);
    const float angular_accel =
        turn_input * cfg.turn_accel * equipment.shield_turn_multiplier;

    body.angular_velocity += angular_accel * dt_s;

    const float angular_decay =
        std::exp(-cfg.angular_drag_per_s * dt_s);

    body.angular_velocity *= angular_decay;
    body.angular_velocity =
        std::clamp(
            body.angular_velocity,
            -cfg.max_turn_rate * equipment.shield_turn_multiplier,
            cfg.max_turn_rate * equipment.shield_turn_multiplier);

    body.heading_rad =
        wrap_angle(
            body.heading_rad
            + body.angular_velocity * dt_s);

    const Vec2 old_position = body.position;

    Vec2 candidate{
        body.position.x + body.velocity.x * dt_s,
        body.position.y + body.velocity.y * dt_s
    };

    const float max_r =
        cfg.arena_radius - cfg.fly_radius;

    const float candidate_r =
        length(candidate);

    if (candidate_r > max_r &&
        candidate_r > 1e-6f)
    {
        const Vec2 n{
            candidate.x / candidate_r,
            candidate.y / candidate_r
        };

        // Positive means velocity points out of the legal circle.
        const float impact_speed =
            std::max(0.0f, dot(body.velocity, n));

        body.last_wall_impact_speed =
            std::max(
                body.last_wall_impact_speed,
                impact_speed);

        if (impact_speed > 0.0f) {
            // Reflect only the normal component; tangential velocity survives.
            const Vec2 normal_component =
                mul(n, impact_speed);

            body.velocity =
                sub(
                    body.velocity,
                    mul(
                        normal_component,
                        1.0f + cfg.wall_restitution));

            // A glancing wall hit also kicks angular velocity so the fly can
            // ricochet/spin instead of repeatedly presenting the same heading.
            const Vec2 tangent{-n.y, n.x};
            const float tangential_speed =
                dot(body.velocity, tangent);

            body.angular_velocity +=
                tangential_speed
                * cfg.wall_spin_kick;

            body.angular_velocity =
                std::clamp(
                    body.angular_velocity,
                    -cfg.max_turn_rate,
                    cfg.max_turn_rate);

            // Shield state is deliberately ignored here. The body hit the
            // arena wall, so HP loss is applied regardless of block state.
            const float damage_speed =
                std::max(
                    0.0f,
                    impact_speed
                    - cfg.wall_damage_speed_threshold);

            const float raw_damage =
                damage_speed
                * cfg.wall_damage_per_speed;

            const int32_t damage =
                std::clamp<int32_t>(
                    static_cast<int32_t>(
                        std::lround(raw_damage)),
                    0,
                    cfg.max_wall_damage_per_hit);

            if (damage > 0) {
                body.hp =
                    std::max<int32_t>(
                        0,
                        body.hp - damage);
                body.wall_damage_taken += damage;
            }

            ++body.wall_contacts;
        }

        // Place body exactly on the legal inner boundary.
        body.position =
            mul(n, max_r);

        // Move the reflected velocity for a small residual fraction of the
        // tick so the bounce is immediately visible instead of appearing to
        // freeze for one world step.
        const float residual_dt = dt_s * 0.30f;
        body.position.x += body.velocity.x * residual_dt;
        body.position.y += body.velocity.y * residual_dt;

        const float after_r = length(body.position);
        if (after_r > max_r && after_r > 1e-6f) {
            const float s = max_r / after_r;
            body.position.x *= s;
            body.position.y *= s;
        }
    } else {
        body.position = candidate;
    }

    const Vec2 actual_delta{
        body.position.x - old_position.x,
        body.position.y - old_position.y
    };

    body.distance_travelled += length(actual_delta);
    body.forward_speed = length(body.velocity);
    body.turn_rate = body.angular_velocity;
}

void resolve_pair_contact(
    FlyBodyState& a,
    FlyBodyState& b,
    const ArenaConfig& cfg)
{
    Vec2 d =
        sub(b.position, a.position);

    float dist = length(d);
    const float min_dist =
        2.0f * cfg.fly_radius;

    if (dist >= min_dist)
        return;

    if (dist < 1e-6f) {
        d = {1.0f, 0.0f};
        dist = 1.0f;
    }

    const Vec2 n{
        d.x / dist,
        d.y / dist
    };

    const Vec2 t{-n.y, n.x};

    // Separate overlap symmetrically.
    const float penetration =
        min_dist - dist;

    a.position.x -= n.x * penetration * 0.5f;
    a.position.y -= n.y * penetration * 0.5f;

    b.position.x += n.x * penetration * 0.5f;
    b.position.y += n.y * penetration * 0.5f;

    const Vec2 relative_velocity =
        sub(b.velocity, a.velocity);

    const float rel_normal =
        dot(relative_velocity, n);

    if (rel_normal < 0.0f) {
        // Equal-mass elastic-ish collision impulse.
        const float impulse =
            -(1.0f + cfg.body_restitution)
            * rel_normal
            * 0.5f;

        const Vec2 jn =
            mul(n, impulse);

        a.velocity =
            sub(a.velocity, jn);

        b.velocity =
            add(b.velocity, jn);

        // Small tangential exchange makes glancing collisions feel more like
        // two spinning tops clipping one another.
        const float rel_tangent =
            dot(relative_velocity, t);

        const float tangent_impulse =
            -rel_tangent
            * cfg.body_tangent_exchange
            * 0.5f;

        const Vec2 jt =
            mul(t, tangent_impulse);

        a.velocity =
            sub(a.velocity, jt);

        b.velocity =
            add(b.velocity, jt);

        a.angular_velocity -=
            rel_tangent * cfg.body_spin_kick;

        b.angular_velocity +=
            rel_tangent * cfg.body_spin_kick;

        a.angular_velocity =
            std::clamp(
                a.angular_velocity,
                -cfg.max_turn_rate,
                cfg.max_turn_rate);

        b.angular_velocity =
            std::clamp(
                b.angular_velocity,
                -cfg.max_turn_rate,
                cfg.max_turn_rate);
    }

    a.forward_speed = length(a.velocity);
    b.forward_speed = length(b.velocity);
    a.turn_rate = a.angular_velocity;
    b.turn_rate = b.angular_velocity;

    ++a.body_contacts;
    ++b.body_contacts;
}

} // namespace flyarena
