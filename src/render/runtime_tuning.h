#pragma once

#include "fly_profile.h"
#include "reward_balance.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <utility>

namespace flyarena {

enum class AppMode : uint32_t {
    Training = 0,
    Battle = 1
};

enum class TrainingSubmode : uint32_t {
    RandomTrainer = 0,
    ImportedOpponent = 1
};

struct PolicyMutationPermissions {
    bool red = false;
    bool blue = false;
};

constexpr PolicyMutationPermissions policy_mutation_permissions(
    AppMode mode,
    TrainingSubmode submode)
{
    if (mode != AppMode::Training)
        return {};
    return {
        true,
        submode == TrainingSubmode::RandomTrainer
    };
}

constexpr uint32_t kTrainingSpeedOptionCount = 6;

// A return value of 0 means MAX: no wall-clock target and no GPU-duty sleep.
// This deliberately lets MAX use all available compute.
constexpr float training_speed_multiplier(uint32_t option) {
    constexpr float values[kTrainingSpeedOptionCount] = {
        1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 0.0f};
    return values[option < kTrainingSpeedOptionCount ? option : 0];
}

// Battle and finite Training speeds retain pacing and the configured GPU-duty
// budget. Only Training MAX returns the unlimited sentinel.
constexpr float neural_pacing_multiplier(
    bool training_enabled,
    uint32_t option)
{
    return training_enabled
        ? training_speed_multiplier(option)
        : 1.0f;
}

constexpr double training_tick_target_wall_ms(
    double world_step_ms,
    uint32_t option)
{
    const float multiplier = training_speed_multiplier(option);
    return multiplier > 0.0f
        ? world_step_ms / static_cast<double>(multiplier)
        : 0.0;
}

enum class TrainingCommandKind : uint32_t {
    Load = 0,
    Reset = 1,
    RandomizeTrainer = 2
};

struct TrainingCommand {
    TrainingCommandKind kind = TrainingCommandKind::Load;
    uint32_t slot = 0; // 0 = red/left, 1 = blue/right
    std::string path;
};

struct TrainingSlotUiState {
    std::string checkpoint_path;
    std::string status;
};

struct FlypackCommand {
    uint32_t slot = 0;
    std::string path;
};

struct FlypackSlotUiState {
    std::string source_path;
    std::string status;
};

struct RuntimeTuning {
    // >1 lowers the empirically measured neutral envelope toward its center.
    // This is an arena actuator sensitivity control, not neural physiology.
    std::atomic<float> sensitivity{1.75f};

    std::atomic<float> forward_gain{1.35f};
    std::atomic<float> turn_gain{1.15f};

    // Product-loop mode is read by simulation and written by the UI. The
    // revision lets the simulation apply transition work exactly once.
    std::atomic<AppMode> app_mode{AppMode::Training};
    std::atomic<TrainingSubmode> training_submode{
        TrainingSubmode::RandomTrainer};
    std::atomic<uint64_t> mode_revision{0};
    std::atomic<uint64_t> restart_revision{0};
    std::atomic<bool> repeat_matches{false};
    std::atomic<uint64_t> episode_number{0};
    std::atomic<uint32_t> training_speed_option{0};
    std::atomic<float> measured_simulation_multiplier{0.0f};

    std::atomic<bool> paused{false};
    std::atomic<bool> quit_requested{false};

    RewardTuning reward_tuning() const {
        std::scoped_lock lock(training_mutex_);
        return reward_tuning_;
    }

    void set_reward_tuning(const RewardTuning& tuning) {
        std::scoped_lock lock(training_mutex_);
        reward_tuning_ = tuning;
        reward_tuning_.damage_penalty_scale = std::clamp(
            reward_tuning_.damage_penalty_scale, 0.25f, 2.0f);
        reward_tuning_.attack_success_scale = std::clamp(
            reward_tuning_.attack_success_scale, 0.25f, 2.0f);
        reward_tuning_.defense_success_scale = std::clamp(
            reward_tuning_.defense_success_scale, 0.25f, 2.0f);
        reward_tuning_.terminal_scale = std::clamp(
            reward_tuning_.terminal_scale, 0.25f, 2.0f);
        reward_tuning_.engagement_scale = std::clamp(
            reward_tuning_.engagement_scale, 0.25f, 2.0f);
    }

    void initialize_trainer(
        const FlyProfile& profile,
        uint64_t generation,
        const std::string& checkpoint_path)
    {
        std::scoped_lock lock(training_mutex_);
        trainer_profile_ = profile;
        trainer_generation_ = generation;
        trainer_checkpoint_path_ = checkpoint_path;
    }

    void initialize_imported_opponent(
        const std::string& checkpoint_path)
    {
        std::scoped_lock lock(training_mutex_);
        imported_opponent_checkpoint_path_ = checkpoint_path;
    }

    void activate_training_submode(TrainingSubmode submode) {
        std::scoped_lock lock(training_mutex_);
        training_submode.store(submode);
        if (submode == TrainingSubmode::RandomTrainer) {
            if (!trainer_checkpoint_path_.empty())
                training_slots_[1].checkpoint_path = trainer_checkpoint_path_;
            training_slots_[1].status = "Trainer ready";
        } else {
            if (!imported_opponent_checkpoint_path_.empty())
                training_slots_[1].checkpoint_path =
                    imported_opponent_checkpoint_path_;
            training_slots_[1].status = "Frozen opponent · read only";
        }
    }

    FlyProfile trainer_profile() const {
        std::scoped_lock lock(training_mutex_);
        return trainer_profile_;
    }

    uint64_t trainer_generation() const {
        std::scoped_lock lock(training_mutex_);
        return trainer_generation_;
    }

    void initialize_training_slot(
        uint32_t slot,
        const std::string& checkpoint_path)
    {
        if (slot >= training_slots_.size())
            return;
        std::scoped_lock lock(training_mutex_);
        training_slots_[slot].checkpoint_path = checkpoint_path;
        training_slots_[slot].status = "Ready";
    }

    TrainingSlotUiState training_slot(uint32_t slot) const {
        std::scoped_lock lock(training_mutex_);
        if (slot >= training_slots_.size())
            return {};
        return training_slots_[slot];
    }

    bool active_profile(uint32_t slot, FlyProfile& profile) const {
        std::scoped_lock lock(training_mutex_);
        if (slot >= active_profiles_.size() || !has_active_profile_[slot])
            return false;
        profile = active_profiles_[slot];
        return true;
    }

    FlypackSlotUiState flypack_slot(uint32_t slot) const {
        std::scoped_lock lock(training_mutex_);
        if (slot >= flypack_slots_.size())
            return {};
        return flypack_slots_[slot];
    }

    void queue_flypack_load(uint32_t slot, const std::string& path) {
        if (slot >= flypack_slots_.size())
            return;
        std::scoped_lock lock(training_mutex_);
        flypack_slots_[slot].status = "Flypack load queued";
        flypack_commands_.push_back({slot, path});
    }

    bool pop_flypack_command(FlypackCommand& command) {
        std::scoped_lock lock(training_mutex_);
        if (flypack_commands_.empty())
            return false;
        command = std::move(flypack_commands_.front());
        flypack_commands_.pop_front();
        return true;
    }

    void complete_flypack_load(
        uint32_t slot,
        bool success,
        const FlyProfile& profile,
        const std::string& source_path,
        const std::string& status)
    {
        if (slot >= flypack_slots_.size())
            return;
        std::scoped_lock lock(training_mutex_);
        if (success) {
            active_profiles_[slot] = profile;
            has_active_profile_[slot] = true;
            flypack_slots_[slot].source_path = source_path;
        }
        flypack_slots_[slot].status = status;
    }

    void queue_training_load(
        uint32_t slot,
        const std::string& source_path)
    {
        if (slot >= training_slots_.size())
            return;
        std::scoped_lock lock(training_mutex_);
        training_slots_[slot].status = "Load queued";
        training_commands_.push_back(
            {TrainingCommandKind::Load, slot, source_path});
    }

    void queue_training_reset(uint32_t slot) {
        if (slot >= training_slots_.size())
            return;
        std::scoped_lock lock(training_mutex_);
        training_slots_[slot].status = "Reset queued";
        training_commands_.push_back(
            {TrainingCommandKind::Reset, slot,
             training_slots_[slot].checkpoint_path});
    }

    void queue_trainer_randomize() {
        std::scoped_lock lock(training_mutex_);
        training_slots_[1].status = "Randomize queued";
        training_commands_.push_back(
            {TrainingCommandKind::RandomizeTrainer, 1, {}});
    }

    void complete_trainer_randomize(
        const FlyProfile& profile,
        uint64_t generation,
        const std::string& checkpoint_path)
    {
        std::scoped_lock lock(training_mutex_);
        trainer_profile_ = profile;
        trainer_generation_ = generation;
        trainer_checkpoint_path_ = checkpoint_path;
        training_slots_[1].checkpoint_path = checkpoint_path;
        training_slots_[1].status = "Fresh randomized Trainer · 0 steps";
    }

    bool pop_training_command(TrainingCommand& command) {
        std::scoped_lock lock(training_mutex_);
        if (training_commands_.empty())
            return false;
        command = std::move(training_commands_.front());
        training_commands_.pop_front();
        return true;
    }

    void complete_training_command(
        uint32_t slot,
        bool success,
        const std::string& checkpoint_path,
        const std::string& status)
    {
        if (slot >= training_slots_.size())
            return;
        std::scoped_lock lock(training_mutex_);
        if (success && !checkpoint_path.empty()) {
            training_slots_[slot].checkpoint_path = checkpoint_path;
            if (slot == 1
                && training_submode.load()
                   == TrainingSubmode::ImportedOpponent)
            {
                imported_opponent_checkpoint_path_ = checkpoint_path;
            }
        }
        training_slots_[slot].status = status;
    }

private:
    mutable std::mutex training_mutex_;
    std::array<TrainingSlotUiState, 2> training_slots_{};
    std::deque<TrainingCommand> training_commands_;
    std::array<FlypackSlotUiState, 2> flypack_slots_{};
    std::array<FlyProfile, 2> active_profiles_{};
    std::array<bool, 2> has_active_profile_{{false, false}};
    std::deque<FlypackCommand> flypack_commands_;
    FlyProfile trainer_profile_{};
    uint64_t trainer_generation_ = 0;
    std::string trainer_checkpoint_path_;
    std::string imported_opponent_checkpoint_path_;
    RewardTuning reward_tuning_{};
};

} // namespace flyarena
