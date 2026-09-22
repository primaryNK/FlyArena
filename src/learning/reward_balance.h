#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace flyarena {

// User-facing reward preferences. These are gameplay/learning assumptions,
// not measured physiology. Automatic sign balancing is applied afterward.
struct RewardTuning {
    float damage_penalty_scale = 1.0f;
    float attack_success_scale = 1.0f;
    float defense_success_scale = 1.0f;
    float terminal_scale = 1.0f;
    float engagement_scale = 1.0f;
};

struct BalancedRewardSample {
    float raw_positive = 0.0f;
    float raw_negative_magnitude = 0.0f;
    float positive_scale = 1.0f;
    float negative_scale = 1.0f;
    float balanced_reward = 0.0f;
    bool clamped = false;
};

struct RewardBalanceDiagnostics {
    double cumulative_raw_positive = 0.0;
    double cumulative_raw_negative_magnitude = 0.0;
    double cumulative_balanced_reward = 0.0;
    float positive_ewma = 1.0f;
    float negative_ewma = 1.0f;
    uint64_t clamped_samples = 0;
};

class AutomaticRewardBalancer {
public:
    BalancedRewardSample apply(
        float positive,
        float negative_magnitude)
    {
        positive = std::max(0.0f, std::isfinite(positive) ? positive : 0.0f);
        negative_magnitude = std::max(
            0.0f,
            std::isfinite(negative_magnitude)
                ? negative_magnitude : 0.0f);

        constexpr float rate = 0.01f;
        positive_ewma_ = (1.0f - rate) * positive_ewma_ + rate * positive;
        negative_ewma_ =
            (1.0f - rate) * negative_ewma_ + rate * negative_magnitude;

        constexpr float budget_floor = 0.25f;
        const float positive_budget = std::max(budget_floor, positive_ewma_);
        const float negative_budget = std::max(budget_floor, negative_ewma_);
        const float common_budget =
            std::sqrt(positive_budget * negative_budget);

        BalancedRewardSample sample;
        sample.raw_positive = positive;
        sample.raw_negative_magnitude = negative_magnitude;
        sample.positive_scale = std::clamp(
            common_budget / positive_budget, 0.40f, 2.50f);
        sample.negative_scale = std::clamp(
            common_budget / negative_budget, 0.40f, 2.50f);

        const float unbounded =
            positive * sample.positive_scale
            - negative_magnitude * sample.negative_scale;
        sample.balanced_reward = std::clamp(unbounded, -30.0f, 30.0f);
        sample.clamped = sample.balanced_reward != unbounded;

        diagnostics_.cumulative_raw_positive += positive;
        diagnostics_.cumulative_raw_negative_magnitude += negative_magnitude;
        diagnostics_.cumulative_balanced_reward += sample.balanced_reward;
        diagnostics_.positive_ewma = positive_ewma_;
        diagnostics_.negative_ewma = negative_ewma_;
        if (sample.clamped)
            ++diagnostics_.clamped_samples;
        return sample;
    }

    RewardBalanceDiagnostics diagnostics() const { return diagnostics_; }

private:
    float positive_ewma_ = 1.0f;
    float negative_ewma_ = 1.0f;
    RewardBalanceDiagnostics diagnostics_{};
};

} // namespace flyarena
