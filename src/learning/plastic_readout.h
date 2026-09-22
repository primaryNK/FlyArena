#pragma once

#include "arena_control.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>

namespace flyarena {

constexpr size_t kPlasticFeatureCount = 19;
constexpr size_t kPlasticActionCount = 4;

using PlasticFeatures =
    std::array<float, kPlasticFeatureCount>;

struct PlasticReadoutConfig {
    float learning_rate = 0.0016f;
    float eligibility_decay = 0.90f;
    float exploration_sigma = 0.16f;
    float reward_baseline_rate = 0.015f;
    float max_abs_weight = 2.50f;

    float forward_residual_scale = 0.24f;
    float turn_residual_scale = 0.42f;
    float sword_residual_scale = 0.34f;
    float shield_residual_scale = 0.34f;
};

struct PlasticReadoutDiagnostics {
    uint64_t training_steps = 0;
    double cumulative_reward = 0.0;
    float reward_baseline = 0.0f;
    float weight_l2 = 0.0f;
};

class PlasticReadout {
public:
    explicit PlasticReadout(uint64_t seed = 1);

    void set_config(const PlasticReadoutConfig& config);
    const PlasticReadoutConfig& config() const { return config_; }

    // During learning this samples a small stochastic residual around the
    // current policy and updates an eligibility trace. In battle mode the
    // same learned mean policy is applied deterministically.
    ArenaControlFrame act(
        const ArenaControlFrame& base,
        const PlasticFeatures& features,
        bool learning_enabled);

    // Reward-modulated policy-gradient style update.
    void learn(float reward);

    void reset_eligibility();

    // Explicit user-requested learning reset. This clears only the plastic
    // readout state; BANC topology and neural state remain untouched.
    void reset_learning();

    bool load(
        const std::string& path,
        std::string& error);

    bool save(
        const std::string& path,
        const std::string& fly_name,
        std::string& error) const;

    PlasticReadoutDiagnostics diagnostics() const;

private:
    static float clamp01(float x);
    static float clamp11(float x);
    static float l2(
        const std::array<
            std::array<float, kPlasticFeatureCount>,
            kPlasticActionCount>& w);

    PlasticReadoutConfig config_{};

    std::array<
        std::array<float, kPlasticFeatureCount>,
        kPlasticActionCount> weights_{};

    std::array<
        std::array<float, kPlasticFeatureCount>,
        kPlasticActionCount> eligibility_{};

    std::mt19937_64 rng_;
    uint64_t initial_seed_ = 1;
    std::normal_distribution<float> normal_{0.0f, 1.0f};

    uint64_t training_steps_ = 0;
    double cumulative_reward_ = 0.0;
    float reward_baseline_ = 0.0f;
    bool last_action_was_learning_ = false;
};

} // namespace flyarena
