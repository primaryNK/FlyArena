#include "fly_profile.h"
#include "plastic_readout.h"
#include "arena_sim.h"
#include "equipment_combat.h"
#include "match_rules.h"
#include "runtime_tuning.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using namespace flyarena;

namespace {

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::string read_all(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
}

FlyProfile sample_profile() {
    FlyProfile profile;
    profile.identity.name = "Test Fly";
    profile.identity.uuid = "test-fly-0001";
    profile.identity.author = "FlyArena tests";
    profile.identity.lineage = "round-trip";
    profile.equipment.body_skin_id = 2;
    profile.equipment.wings.skin_id = 1;
    profile.equipment.sword.skin_id = 2;
    profile.equipment.shield.skin_id = 1;
    profile.equipment.primary_color_rgb = 0x123456u;
    profile.equipment.secondary_color_rgb = 0xabcdefu;
    profile.equipment.wings.primary_color_rgb = 0x112233u;
    profile.equipment.wings.secondary_color_rgb = 0x445566u;
    profile.equipment.sword.primary_color_rgb = 0x778899u;
    profile.equipment.sword.secondary_color_rgb = 0xaabbccu;
    profile.equipment.shield.primary_color_rgb = 0x0a1b2cu;
    profile.equipment.shield.secondary_color_rgb = 0xddeeffu;
    profile.equipment.sword.length_scale = 1.45f;
    profile.equipment.sword.recovery_scale = 1.0f;
    profile.equipment.wings.size_scale = 1.22f;
    profile.equipment.wings.drive_speed_scale = 1.0f;
    profile.equipment.wings.stamina_cost_scale = 1.0f;
    profile.equipment.shield.mass_scale = 1.40f;
    profile.equipment.shield.size_scale = 1.0f;
    profile.equipment.shield.stamina_cost_scale = 1.0f;
    profile.training.present = true;
    profile.training.checkpoint_file = "Test_Fly.flytrain";
    profile.training.training_steps = 42;
    profile.statistics.episodes = 5;
    profile.statistics.wins = 2;
    profile.statistics.losses = 2;
    profile.statistics.draws = 1;
    return profile;
}

void test_flypack(const fs::path& directory) {
    const fs::path path = directory / "roundtrip.flypack";
    std::string error;
    FlyProfile profile = sample_profile();
    require(save_flypack(path.string(), profile, error), error.c_str());

    FlyProfile loaded;
    loaded.identity.name = "must survive failed loads";
    require(load_flypack(path.string(), loaded, error), error.c_str());
    require(loaded.identity.name == profile.identity.name, "flypack name did not round-trip");
    require(loaded.identity.uuid == profile.identity.uuid, "flypack UUID did not round-trip");
    require(std::fabs(loaded.equipment.sword.length_scale - 1.45f) < 1e-6f, "sword length did not round-trip");
    require(std::fabs(loaded.equipment.sword.recovery_scale - 1.0f) < 1e-6f, "v3 sword recovery link did not round-trip");
    require(std::fabs(loaded.equipment.wings.size_scale - 1.22f) < 1e-6f, "wing size did not round-trip");
    require(std::fabs(loaded.equipment.wings.drive_speed_scale - 1.0f) < 1e-6f, "v3 wing drive link did not round-trip");
    require(std::fabs(loaded.equipment.shield.stamina_cost_scale - 1.0f) < 1e-6f, "v3 shield stamina link did not round-trip");
    require(loaded.equipment.primary_color_rgb == 0x123456u
            && loaded.equipment.secondary_color_rgb == 0xabcdefu,
        "v4 body gradient colors did not round-trip");
    require(loaded.equipment.wings.primary_color_rgb == 0x112233u
            && loaded.equipment.wings.secondary_color_rgb == 0x445566u
            && loaded.equipment.sword.primary_color_rgb == 0x778899u
            && loaded.equipment.sword.secondary_color_rgb == 0xaabbccu
            && loaded.equipment.shield.primary_color_rgb == 0x0a1b2cu
            && loaded.equipment.shield.secondary_color_rgb == 0xddeeffu,
        "v4 component gradients did not round-trip");
    require(loaded.statistics.episodes == 5, "statistics did not round-trip");

    {
        std::ofstream append(path, std::ios::binary | std::ios::app);
        append << "future.optional_field \"ignored\"\n";
    }
    require(load_flypack(path.string(), loaded, error), "unknown optional field was not ignored");

    FlyProfile clamped = sample_profile();
    clamped.equipment.sword.length_scale = 99.0f;
    clamped.equipment.wings.size_scale = 0.01f;
    clamped.equipment.shield.mass_scale = 50.0f;
    clamped.equipment.body_skin_id = 999;
    std::vector<std::string> warnings;
    require(validate_and_normalize_fly_profile(clamped, error, &warnings), error.c_str());
    require(clamped.equipment.sword.length_scale == 1.80f, "sword length was not clamped");
    require(clamped.equipment.wings.size_scale == 0.65f, "wing size was not clamped");
    require(clamped.equipment.shield.mass_scale == 2.00f, "shield mass was not clamped");
    require(clamped.equipment.body_skin_id == kFlypackMaxBuiltInSkinId, "skin ID was not clamped");
    require(warnings.size() >= 4, "clamps were not reported");

    const fs::path legacy_path = directory / "legacy-v1.flypack";
    FlyProfile legacy = sample_profile();
    legacy.format_version = 1;
    require(save_flypack(legacy_path.string(), legacy, error), error.c_str());
    require(load_flypack(legacy_path.string(), loaded, error), error.c_str());
    require(loaded.format_version == 1, "v1 flypack was silently reinterpreted");
    require(loaded.equipment.sword.recovery_scale == 1.0f
            && loaded.equipment.wings.drive_speed_scale == 1.0f
            && loaded.equipment.wings.stamina_cost_scale == 1.0f
            && loaded.equipment.shield.stamina_cost_scale == 1.0f,
        "v1 flypack did not receive neutral v2 tuning defaults");
    require(loaded.equipment.primary_color_rgb == 0x3380FFu
            && loaded.equipment.secondary_color_rgb == 0x54A6FFu,
        "v1 flypack did not preserve its implied legacy palette");

    const fs::path v3_path = directory / "legacy-v3.flypack";
    FlyProfile v3 = sample_profile();
    v3.format_version = 3;
    require(save_flypack(v3_path.string(), v3, error), error.c_str());
    require(load_flypack(v3_path.string(), loaded, error), error.c_str());
    require(loaded.equipment.wings.primary_color_rgb
                == loaded.equipment.primary_color_rgb
            && loaded.equipment.sword.secondary_color_rgb
                == loaded.equipment.secondary_color_rgb
            && loaded.equipment.shield.primary_color_rgb
                == loaded.equipment.primary_color_rgb,
        "v3 shared gradient was not preserved across v4 components");

    FlyProfile invalid = sample_profile();
    invalid.training.checkpoint_file = "..\\escape.flytrain";
    require(!validate_and_normalize_fly_profile(invalid, error), "unsafe checkpoint path was accepted");

    invalid = sample_profile();
    invalid.equipment.wings.size_scale = std::numeric_limits<float>::quiet_NaN();
    require(!validate_and_normalize_fly_profile(invalid, error), "NaN equipment was accepted");

    const FlyProfile unchanged = loaded;
    {
        std::ofstream corrupt(path, std::ios::binary | std::ios::trunc);
        corrupt << "not a flypack\n";
    }
    require(!load_flypack(path.string(), loaded, error), "damaged flypack was accepted");
    require(loaded.identity.uuid == unchanged.identity.uuid, "failed load mutated the active profile");
}

void test_learning_freeze(const fs::path& directory) {
    PlasticReadout policy(0x1234);
    PlasticReadoutConfig config;
    config.exploration_sigma = 0.20f;
    policy.set_config(config);

    ArenaControlFrame base;
    base.forward = 0.4f;
    PlasticFeatures features{};
    features[0] = 1.0f;
    features[3] = 0.5f;

    (void)policy.act(base, features, true);
    policy.learn(10.0f);
    const PlasticReadoutDiagnostics learned = policy.diagnostics();
    require(learned.training_steps == 1, "learning step was not recorded");
    require(learned.weight_l2 > 0.0f, "positive reward did not change weights");

    std::string error;
    const fs::path before_path = directory / "before.flytrain";
    const fs::path after_path = directory / "after.flytrain";
    require(policy.save(before_path.string(), "Freeze Test", error), error.c_str());

    const ArenaControlFrame frozen_a = policy.act(base, features, false);
    const ArenaControlFrame frozen_b = policy.act(base, features, false);
    policy.learn(60.0f);
    require(frozen_a.forward == frozen_b.forward, "frozen policy used exploration noise");
    require(frozen_a.turn == frozen_b.turn, "frozen turn output was not deterministic");
    require(policy.diagnostics().training_steps == learned.training_steps, "frozen policy learned");
    require(policy.save(after_path.string(), "Freeze Test", error), error.c_str());
    require(read_all(before_path) == read_all(after_path), "frozen policy checkpoint changed");

    PlasticReadout restored(7);
    require(restored.load(before_path.string(), error), error.c_str());
    require(restored.diagnostics().training_steps == learned.training_steps, "checkpoint training steps did not load");
    require(std::fabs(restored.diagnostics().weight_l2 - learned.weight_l2) < 1e-6f, "checkpoint weights did not load");

    restored.reset_learning();
    require(restored.diagnostics().training_steps == 0, "learning reset kept training steps");
    require(restored.diagnostics().weight_l2 == 0.0f, "learning reset kept weights");
    require(restored.diagnostics().cumulative_reward == 0.0, "learning reset kept reward history");
}

void test_training_slot_commands() {
    AutomaticRewardBalancer reward_balancer;
    const BalancedRewardSample capped = reward_balancer.apply(1000.0f, 0.0f);
    require(capped.clamped && capped.balanced_reward <= 30.0f,
        "automatic reward balance allowed a runaway positive update");
    AutomaticRewardBalancer symmetric_balancer;
    BalancedRewardSample symmetric;
    for (int i = 0; i < 500; ++i)
        symmetric = symmetric_balancer.apply(2.0f, 2.0f);
    require(std::fabs(symmetric.positive_scale - symmetric.negative_scale) < 1e-4f
            && std::fabs(symmetric.balanced_reward) < 1e-4f,
        "automatic reward balance broke symmetric positive/negative budgets");

    require(training_speed_multiplier(0) == 1.0f
            && training_speed_multiplier(4) == 16.0f,
        "Training speed options changed their multiplier mapping");
    require(training_speed_multiplier(5) == 0.0f,
        "MAX Training speed retained a real-time pacing target");

    const auto random_permissions = policy_mutation_permissions(
        AppMode::Training, TrainingSubmode::RandomTrainer);
    require(random_permissions.red && random_permissions.blue,
        "Random Trainer did not grant learning to both slots");
    const auto imported_permissions = policy_mutation_permissions(
        AppMode::Training, TrainingSubmode::ImportedOpponent);
    require(imported_permissions.red && !imported_permissions.blue,
        "Imported Opponent did not freeze the opponent slot");
    const auto battle_permissions = policy_mutation_permissions(
        AppMode::Battle, TrainingSubmode::RandomTrainer);
    require(!battle_permissions.red && !battle_permissions.blue,
        "Battle granted policy mutation permission");

    RuntimeTuning tuning;
    tuning.initialize_training_slot(0, "data/training/left.flytrain");
    tuning.initialize_training_slot(1, "data/training/right.flytrain");

    tuning.queue_training_load(1, "imports/opponent.flytrain");
    TrainingCommand command;
    require(tuning.pop_training_command(command), "training load command was lost");
    require(command.kind == TrainingCommandKind::Load && command.slot == 1,
        "training load command targeted the wrong slot");
    require(command.path == "imports/opponent.flytrain",
        "training load command changed its source path");

    tuning.complete_training_command(
        1, true, command.path, "Imported · active");
    require(tuning.training_slot(1).checkpoint_path == command.path,
        "successful import did not become the active save path");

    tuning.queue_training_reset(1);
    require(tuning.pop_training_command(command), "training reset command was lost");
    require(command.kind == TrainingCommandKind::Reset && command.slot == 1,
        "training reset command targeted the wrong slot");
    require(command.path == "imports/opponent.flytrain",
        "training reset did not target the active checkpoint");

    FlyProfile first = make_random_trainer_profile(7);
    const FlyProfile repeated = make_random_trainer_profile(7);
    const FlyProfile next = make_random_trainer_profile(8);
    std::string error;
    require(validate_and_normalize_fly_profile(first, error),
        "generated Trainer profile was outside flypack limits");
    require(first.identity.name == "Trainer", "generated opponent was not named Trainer");
    require(first.identity.uuid == repeated.identity.uuid,
        "Trainer generation was not reproducible from its seed");
    require(first.identity.uuid != next.identity.uuid,
        "different Trainer generations reused an identity");

    tuning.initialize_imported_opponent("imports/opponent.flytrain");
    tuning.initialize_trainer(
        first, 7, "data/training/Trainer_v065.flytrain");
    require(tuning.trainer_generation() == 7,
        "Trainer generation was not retained between episodes");
    require(tuning.trainer_profile().identity.uuid == first.identity.uuid,
        "Trainer profile was not retained between episodes");
    tuning.activate_training_submode(TrainingSubmode::ImportedOpponent);
    require(tuning.training_slot(1).checkpoint_path
            == "imports/opponent.flytrain",
        "Imported Opponent did not restore its frozen checkpoint");
    tuning.activate_training_submode(TrainingSubmode::RandomTrainer);
    require(tuning.training_slot(1).checkpoint_path
            == "data/training/Trainer_v065.flytrain",
        "Random Trainer did not restore its separate checkpoint");

    tuning.queue_trainer_randomize();
    require(tuning.pop_training_command(command),
        "Trainer randomize command was lost");
    require(command.kind == TrainingCommandKind::RandomizeTrainer
            && command.slot == 1,
        "Trainer randomize command targeted the wrong slot");

    tuning.complete_trainer_randomize(
        next, 8, "data/training/Trainer_v065.flytrain");
    require(tuning.trainer_generation() == 8,
        "randomized Trainer generation was not committed");
    require(tuning.training_slot(1).checkpoint_path
            == "data/training/Trainer_v065.flytrain",
        "randomized Trainer lost its separate checkpoint path");

    tuning.queue_flypack_load(0, "imports/user.flypack");
    FlypackCommand flypack_command;
    require(tuning.pop_flypack_command(flypack_command),
        "flypack load command was lost");
    require(flypack_command.slot == 0
            && flypack_command.path == "imports/user.flypack",
        "flypack load command changed its target");
    FlyProfile active;
    require(!tuning.active_profile(0, active),
        "queued flypack mutated the active profile before validation");
    tuning.complete_flypack_load(
        0, true, first, flypack_command.path, "Imported · active");
    require(tuning.active_profile(0, active)
            && active.identity.uuid == first.identity.uuid,
        "validated flypack did not become the active profile");
}

void test_equipment_derivation() {
    ArenaConfig config;
    FlyEquipment light;
    light.shield.mass_scale = 0.60f;
    light.wings.size_scale = 0.65f;

    FlyEquipment heavy;
    heavy.shield.mass_scale = 2.00f;
    heavy.wings.size_scale = 1.60f;

    const EquipmentMovementStats light_stats =
        derive_equipment_movement_stats(light, config);
    const EquipmentMovementStats heavy_stats =
        derive_equipment_movement_stats(heavy, config);

    require(heavy_stats.wing_thrust_multiplier > light_stats.wing_thrust_multiplier,
        "larger wings did not increase thrust multiplier");
    require(heavy_stats.wing_speed_multiplier > light_stats.wing_speed_multiplier,
        "larger wings did not increase speed multiplier");
    require(heavy_stats.movement_stamina_multiplier > light_stats.movement_stamina_multiplier,
        "larger wings did not increase stamina burden");
    require(heavy_stats.shield_move_multiplier < light_stats.shield_move_multiplier,
        "heavy shield did not reduce movement response");
    require(heavy_stats.shield_turn_multiplier < light_stats.shield_turn_multiplier,
        "heavy shield did not reduce turn response");

    FlyEquipment efficient = heavy;
    efficient.wings.stamina_cost_scale = 0.60f;
    efficient.wings.drive_speed_scale = 1.60f;
    const EquipmentMovementStats efficient_stats =
        derive_equipment_movement_stats(efficient, config);
    require(efficient_stats.wing_speed_multiplier
            > heavy_stats.wing_speed_multiplier,
        "wing drive speed slider did not affect canonical movement physics");
    require(efficient_stats.movement_stamina_multiplier
            < heavy_stats.movement_stamina_multiplier,
        "wing stamina slider did not affect canonical stamina cost");

    const float short_recovery = 0.48f * 0.60f;
    const float long_recovery = 0.48f * 1.80f;
    require(long_recovery > short_recovery,
        "sword length was not inversely linked to repeated attack speed");
}

void test_v064d_combat_contract() {
    ArenaConfig arena;
    CombatConfig combat;

    FlyBodyState wall_test;
    wall_test.position = {arena.arena_radius - arena.fly_radius - 0.001f, 0.0f};
    wall_test.velocity = {1.0f, 0.0f};
    wall_test.shield_raised = true;
    const int32_t wall_hp_before = wall_test.hp;
    step_body(wall_test, ArenaControlFrame{}, arena, 0.005f);
    require(wall_test.hp < wall_hp_before, "raised shield blocked wall HP damage");
    require(wall_test.wall_damage_taken > 0, "wall damage telemetry was not recorded");

    FlyBodyState wing_test;
    wing_test.equipment.wings.size_scale = 1.60f;
    ArenaControlFrame thrust;
    thrust.forward = 1.0f;
    const int32_t stamina_before = wing_test.stamina;
    step_body(wing_test, thrust, arena, 0.25f);
    require(wing_test.stamina < stamina_before, "powered wing movement did not consume stamina");
    require(wing_test.stamina_spent_movement_points > 0.0f
            && wing_test.stamina_recovery_blocked_movement_s > 0.0f,
        "movement stamina spend/block telemetry was not recorded");

    FlyBodyState recovery_test;
    recovery_test.stamina = 50;
    step_body(recovery_test, ArenaControlFrame{}, arena, 0.25f);
    require(recovery_test.stamina_recovered_points > 0.0f
            && recovery_test.stamina_recovery_eligible_s > 0.0f
            && recovery_test.stamina_recovery_eligible,
        "gradual eligible stamina recovery was not recorded");

    FlyBodyState shield_test;
    shield_test.equipment.shield.mass_scale = 2.0f;
    ArenaControlFrame raise;
    raise.shield_drive = 1.0f;
    step_equipment(shield_test, raise, combat, 0.25f);
    require(shield_test.shield_raised, "shield neural drive did not raise shield");
    require(shield_test.stamina < shield_test.max_stamina, "raised shield did not consume stamina");
    require(shield_test.stamina_spent_shield_points > 0.0f,
        "shield stamina spend telemetry was not recorded");
    step_body(shield_test, ArenaControlFrame{}, arena, 0.25f);
    require(shield_test.stamina_recovery_blocked_shield_s > 0.0f
            && !shield_test.stamina_recovery_eligible,
        "raised shield did not block stamina recovery telemetry");

    FlyBodyState deploy_test;
    const float stowed_distance = shield_center_distance_world(
        deploy_test.equipment, arena.fly_radius, 0.0f);
    const float deployed_distance = shield_center_distance_world(
        deploy_test.equipment, arena.fly_radius, 1.0f);
    require(std::fabs(
                deployed_distance
                - (arena.fly_radius
                   + shield_radius_world(deploy_test.equipment) * 0.55f))
            < 1e-6f,
        "fully deployed shield changed the v0.6.4d collision center");
    step_equipment(deploy_test, raise, combat, 0.05f);
    require(deploy_test.shield_raised
            && deploy_test.shield_deploy > 0.0f
            && deploy_test.shield_deploy < 1.0f,
        "shield raise did not produce a continuous partial deployment");
    const float partial_distance = shield_center_distance_world(
        deploy_test.equipment, arena.fly_radius, deploy_test.shield_deploy);
    require(partial_distance > stowed_distance
            && partial_distance < deployed_distance,
        "shield deployment did not move canonical collision geometry outward");
    step_equipment(deploy_test, ArenaControlFrame{}, combat, 0.05f);
    require(!deploy_test.shield_raised
            && deploy_test.shield_deploy < 0.5f,
        "released shield did not move back toward its stowed position");

    auto make_attack = [&]() {
        FlyBodyState attacker;
        attacker.identity.name = "Attacker";
        attacker.position = {0.0f, 0.0f};
        attacker.heading_rad = 0.0f;
        attacker.sword_phase = SwordPhase::Swing;
        attacker.sword_relative_angle = 0.0f;
        attacker.sword_previous_relative_angle = 0.0f;
        attacker.sword_angular_velocity = 10.0f;
        attacker.sword_hit_active_this_step = true;
        return attacker;
    };

    uint64_t event_id = 1;
    std::vector<CombatEvent> events;
    FlyBodyState attacker = make_attack();
    FlyBodyState defender;
    defender.identity.name = "Defender";
    defender.position = {0.10f, -0.0135f};
    const int32_t hp_before = defender.hp;
    resolve_equipment_combat(
        attacker, defender, arena.fly_radius, combat, 1.0, event_id, events);
    require(defender.hp == hp_before - combat.sword_hit_damage, "active sword swing did not damage body");
    require(events.size() == 1 && events[0].type == CombatEventType::Hit, "active sword collision was not classified HIT");

    // Reach must come from the customized physical blade length. Keep the
    // defender's wings pointed away so this isolates sword reach from the new
    // wing capsules.
    events.clear();
    FlyBodyState short_attacker = make_attack();
    short_attacker.equipment.sword.length_scale = 0.60f;
    FlyBodyState reach_defender;
    reach_defender.identity.name = "ReachDefender";
    reach_defender.position = {0.18f, -0.0135f};
    reach_defender.heading_rad = 3.14159265f;
    resolve_equipment_combat(
        short_attacker, reach_defender, arena.fly_radius,
        combat, 1.1, event_id, events);
    require(reach_defender.hp == reach_defender.max_hp && events.empty(),
        "minimum-length sword reached beyond its canonical blade tip");

    events.clear();
    FlyBodyState long_attacker = make_attack();
    long_attacker.equipment.sword.length_scale = 1.80f;
    reach_defender = FlyBodyState{};
    reach_defender.identity.name = "ReachDefender";
    reach_defender.position = {0.18f, -0.0135f};
    reach_defender.heading_rad = 3.14159265f;
    resolve_equipment_combat(
        long_attacker, reach_defender, arena.fly_radius,
        combat, 1.2, event_id, events);
    require(reach_defender.hp == reach_defender.max_hp
                - combat.sword_hit_damage
            && events.size() == 1
            && events[0].type == CombatEventType::Hit,
        "maximum-length sword did not gain its canonical physical reach");

    // A blade that misses the circular body can hit a visible wing. The same
    // wing-size parameter must expand both rendering and collision geometry.
    events.clear();
    FlyBodyState small_wing_attacker = make_attack();
    FlyBodyState wing_defender;
    wing_defender.identity.name = "WingDefender";
    wing_defender.position = {0.17f, 0.055f};
    wing_defender.equipment.wings.size_scale = 0.65f;
    resolve_equipment_combat(
        small_wing_attacker, wing_defender, arena.fly_radius,
        combat, 1.3, event_id, events);
    require(wing_defender.hp == wing_defender.max_hp && events.empty(),
        "minimum-size wing hitbox exceeded its canonical silhouette");

    events.clear();
    FlyBodyState large_wing_attacker = make_attack();
    wing_defender = FlyBodyState{};
    wing_defender.identity.name = "WingDefender";
    wing_defender.position = {0.17f, 0.055f};
    wing_defender.equipment.wings.size_scale = 1.60f;
    resolve_equipment_combat(
        large_wing_attacker, wing_defender, arena.fly_radius,
        combat, 1.4, event_id, events);
    require(wing_defender.hp == wing_defender.max_hp
                - combat.sword_hit_damage
            && events.size() == 1
            && events[0].type == CombatEventType::Hit,
        "sword passed through the canonical large-wing hitbox");

    // The final powered substep remains hittable even though step_equipment
    // transitions the state to Recovery at the end of that same substep.
    FlyBodyState terminal_swing;
    terminal_swing.identity.name = "TerminalSwing";
    terminal_swing.sword_phase = SwordPhase::Swing;
    terminal_swing.sword_phase_time_s = combat.sword_swing_duration_s - 0.001f;
    ArenaControlFrame keep_swinging;
    keep_swinging.sword_drive = 1.0f;
    step_equipment(terminal_swing, keep_swinging, combat, 0.005f);
    require(terminal_swing.sword_phase == SwordPhase::Recovery
            && terminal_swing.sword_hit_active_this_step,
        "final sword arc lost hit validity during Swing-to-Recovery transition");

    events.clear();
    FlyBodyState swept_attacker = make_attack();
    swept_attacker.sword_previous_relative_angle = -0.70f;
    swept_attacker.sword_relative_angle = 0.70f;
    swept_attacker.sword_angular_velocity = 14.0f;
    FlyBodyState swept_defender;
    swept_defender.identity.name = "SweptDefender";
    swept_defender.position = {0.10f, -0.0135f};
    resolve_equipment_combat(
        swept_attacker, swept_defender, arena.fly_radius,
        combat, 1.5, event_id, events);
    require(swept_defender.hp == swept_defender.max_hp
                - combat.sword_hit_damage,
        "swept sword arc tunneled through the defender during Swing");

    events.clear();
    attacker = make_attack();
    defender = FlyBodyState{};
    defender.identity.name = "Defender";
    defender.position = {0.10f, -0.0135f};
    defender.heading_rad = 3.14159265f;
    defender.shield_raised = true;
    defender.shield_relative_angle = 0.12f;
    defender.parry_window_remaining_s = 0.0f;
    resolve_equipment_combat(
        attacker, defender, arena.fly_radius, combat, 2.0, event_id, events);
    require(defender.hp == defender.max_hp, "BLOCK allowed sword HP damage");
    require(events.size() == 1 && events[0].type == CombatEventType::Block, "shield interception was not classified BLOCK");
    require(std::fabs(events[0].world_x) > 0.01f, "BLOCK event lost its physical contact position");
    require(std::string(combat_event_label(events[0].type)) == "BLOCK!", "BLOCK event label regressed");
    const float neutral_block_stun = events[0].stun_seconds;

    events.clear();
    attacker = make_attack();
    defender = FlyBodyState{};
    defender.identity.name = "Defender";
    defender.position = {0.10f, -0.0135f};
    defender.heading_rad = 3.14159265f;
    defender.shield_raised = true;
    defender.shield_relative_angle = 0.12f;
    defender.equipment.shield.mass_scale = 2.0f;
    resolve_equipment_combat(
        attacker, defender, arena.fly_radius, combat, 2.5, event_id, events);
    require(events.size() == 1
            && events[0].type == CombatEventType::Block
            && events[0].stun_seconds > neutral_block_stun,
        "heavy shield did not increase physical BLOCK stun");

    events.clear();
    attacker = make_attack();
    defender = FlyBodyState{};
    defender.identity.name = "Defender";
    defender.position = {0.10f, -0.0135f};
    defender.heading_rad = 3.14159265f;
    defender.shield_raised = true;
    defender.shield_relative_angle = 0.12f;
    defender.shield_angular_velocity = 4.0f;
    defender.parry_window_remaining_s = combat.parry_window_s;
    resolve_equipment_combat(
        attacker, defender, arena.fly_radius, combat, 3.0, event_id, events);
    require(events.size() == 1 && events[0].type == CombatEventType::Parry, "timed moving shield was not classified PARRY");
    require(attacker.stun_remaining_s >= combat.parry_stun_base_s, "PARRY did not stun attacker");
    require(events[0].stun_seconds > neutral_block_stun,
        "PARRY stun was not longer than BLOCK stun");

    require(!match_time_expired(59999.0, 60000.0), "60-second match ended early");
    require(match_time_expired(60000.0, 60000.0), "60-second time limit did not expire");
    require(classify_match_result(80, 70) == MatchResult::Red, "higher RED HP did not win");
    require(classify_match_result(60, 90) == MatchResult::Blue, "higher BLUE HP did not win");
    require(classify_match_result(50, 50) == MatchResult::Draw, "equal HP was not a draw");
}

void test_skin_physics_invariance() {
    ArenaConfig config;
    FlyEquipment neutral;
    neutral.sword.length_scale = 1.37f;
    neutral.wings.size_scale = 1.28f;
    neutral.shield.mass_scale = 1.52f;
    neutral.shield.size_scale = 1.19f;

    FlyEquipment ruby = neutral;
    ruby.body_skin_id = 1;
    ruby.wings.skin_id = 1;
    ruby.sword.skin_id = 1;
    ruby.shield.skin_id = 1;

    FlyEquipment azure = neutral;
    azure.body_skin_id = 5;
    azure.wings.skin_id = 5;
    azure.sword.skin_id = 5;
    azure.shield.skin_id = 5;
    azure.primary_color_rgb = 0x010203u;
    azure.secondary_color_rgb = 0xfdfcfbu;
    azure.wings.primary_color_rgb = 0x102030u;
    azure.wings.secondary_color_rgb = 0x405060u;
    azure.sword.primary_color_rgb = 0x708090u;
    azure.sword.secondary_color_rgb = 0xa0b0c0u;
    azure.shield.primary_color_rgb = 0xd0e0f0u;
    azure.shield.secondary_color_rgb = 0x0f1e2du;

    const FlyVisualLoadout visual = visual_loadout_from_equipment(azure);
    require(visual.body_primary_color_rgb == azure.primary_color_rgb
            && visual.wing_secondary_color_rgb
                == azure.wings.secondary_color_rgb
            && visual.sword_primary_color_rgb
                == azure.sword.primary_color_rgb
            && visual.shield_secondary_color_rgb
                == azure.shield.secondary_color_rgb,
        "canonical equipment colors did not reach the in-game visual loadout");

    const EquipmentMovementStats expected =
        derive_equipment_movement_stats(neutral, config);

    for (const FlyEquipment* skin : {&ruby, &azure}) {
        const EquipmentMovementStats actual =
            derive_equipment_movement_stats(*skin, config);
        require(sword_length_world(*skin) == sword_length_world(neutral),
            "sword skin changed physical reach");
        require(shield_radius_world(*skin) == shield_radius_world(neutral),
            "shield skin changed collision radius");
        require(shield_mass(*skin) == shield_mass(neutral),
            "shield skin changed physical mass");
        require(actual.wing_thrust_multiplier == expected.wing_thrust_multiplier,
            "wing skin changed thrust multiplier");
        require(actual.wing_speed_multiplier == expected.wing_speed_multiplier,
            "wing skin changed speed multiplier");
        require(actual.shield_move_multiplier == expected.shield_move_multiplier,
            "skin changed movement multiplier");
        require(actual.shield_turn_multiplier == expected.shield_turn_multiplier,
            "skin changed turn multiplier");
    }
}

void test_training_speed_contract() {
    require(training_tick_target_wall_ms(50.0, 0) == 50.0,
        "1x did not pace the complete world tick at real time");
    require(training_tick_target_wall_ms(50.0, 1) == 25.0,
        "2x did not halve complete world-tick wall time");
    require(training_tick_target_wall_ms(50.0, 4) == 3.125,
        "16x did not target the complete simulation loop");
    require(training_tick_target_wall_ms(50.0, 5) == 0.0,
        "MAX retained a wall-clock limit");
}

void test_shipped_flypacks() {
    const fs::path root = fs::path(__FILE__).parent_path().parent_path();
    for (const char* name : {"Ruby.flypack", "Azure.flypack"}) {
        FlyProfile profile;
        std::string error;
        const fs::path path = root / "data" / "flies" / name;
        require(load_flypack(path.string(), profile, error), error.c_str());
        require(profile.format_version == 4,
            "shipped flypack was not upgraded to v4");
    }
}

} // namespace

int main() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path directory =
        fs::temp_directory_path() / ("flyarena-v065-tests-" + std::to_string(stamp));

    try {
        fs::create_directories(directory);
        test_flypack(directory);
        test_learning_freeze(directory);
        test_training_slot_commands();
        test_equipment_derivation();
        test_v064d_combat_contract();
        test_skin_physics_invariance();
        test_training_speed_contract();
        test_shipped_flypacks();
        fs::remove_all(directory);
        std::cout << "PASS: flypack v1-v4, full-loop speed, learning, swept combat, skin invariance\n";
        return 0;
    }
    catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << "\n";
        std::error_code ignored;
        fs::remove_all(directory, ignored);
        return 1;
    }
}
