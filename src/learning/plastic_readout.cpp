#include "plastic_readout.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace flyarena {

namespace {

constexpr const char* kMagic =
    "FLYARENA_PLASTIC_READOUT_V1";

float dot(
    const std::array<float, kPlasticFeatureCount>& w,
    const PlasticFeatures& x)
{
    float s = 0.0f;
    for (size_t i = 0; i < kPlasticFeatureCount; ++i)
        s += w[i] * x[i];
    return s;
}

} // namespace

PlasticReadout::PlasticReadout(uint64_t seed)
    : rng_(seed), initial_seed_(seed)
{
}

void PlasticReadout::set_config(
    const PlasticReadoutConfig& config)
{
    config_ = config;
}

float PlasticReadout::clamp01(float x) {
    return std::clamp(x, 0.0f, 1.0f);
}

float PlasticReadout::clamp11(float x) {
    return std::clamp(x, -1.0f, 1.0f);
}

ArenaControlFrame PlasticReadout::act(
    const ArenaControlFrame& base,
    const PlasticFeatures& features,
    bool learning_enabled)
{
    ArenaControlFrame out = base;

    std::array<float, kPlasticActionCount> sampled{};

    for (size_t a = 0; a < kPlasticActionCount; ++a) {
        const float mean =
            std::tanh(dot(weights_[a], features));

        float standard_noise = 0.0f;
        if (learning_enabled
            && config_.exploration_sigma > 0.0f)
        {
            standard_noise = normal_(rng_);
        }

        sampled[a] =
            std::tanh(
                mean
                + config_.exploration_sigma
                  * standard_noise);

        if (learning_enabled) {
            const float sigma =
                std::max(
                    0.01f,
                    config_.exploration_sigma);

            // Gaussian-policy score-function trace:
            // d log pi / d w ~ epsilon/sigma * feature.
            for (size_t j = 0;
                 j < kPlasticFeatureCount;
                 ++j)
            {
                eligibility_[a][j] =
                    config_.eligibility_decay
                    * eligibility_[a][j]
                    + (standard_noise / sigma)
                      * features[j];
            }
        } else {
            for (size_t j = 0;
                 j < kPlasticFeatureCount;
                 ++j)
            {
                eligibility_[a][j] *=
                    config_.eligibility_decay;
            }
        }
    }

    out.forward =
        clamp01(
            base.forward
            + config_.forward_residual_scale
              * sampled[0]);

    out.turn =
        clamp11(
            base.turn
            + config_.turn_residual_scale
              * sampled[1]);

    out.sword_drive =
        clamp01(
            base.sword_drive
            + config_.sword_residual_scale
              * sampled[2]);

    out.shield_drive =
        clamp01(
            base.shield_drive
            + config_.shield_residual_scale
              * sampled[3]);

    last_action_was_learning_ =
        learning_enabled;

    return out;
}

void PlasticReadout::learn(float reward)
{
    if (!last_action_was_learning_)
        return;

    if (!std::isfinite(reward))
        return;

    reward =
        std::clamp(reward, -60.0f, 60.0f);

    const float advantage =
        reward - reward_baseline_;

    reward_baseline_ =
        (1.0f - config_.reward_baseline_rate)
        * reward_baseline_
        + config_.reward_baseline_rate
          * reward;

    for (size_t a = 0;
         a < kPlasticActionCount;
         ++a)
    {
        for (size_t j = 0;
             j < kPlasticFeatureCount;
             ++j)
        {
            weights_[a][j] +=
                config_.learning_rate
                * advantage
                * eligibility_[a][j];

            weights_[a][j] =
                std::clamp(
                    weights_[a][j],
                    -config_.max_abs_weight,
                    config_.max_abs_weight);
        }
    }

    cumulative_reward_ += reward;
    ++training_steps_;
}

void PlasticReadout::reset_eligibility()
{
    for (auto& action : eligibility_)
        action.fill(0.0f);
}

void PlasticReadout::reset_learning()
{
    for (auto& action : weights_)
        action.fill(0.0f);
    reset_eligibility();
    training_steps_ = 0;
    cumulative_reward_ = 0.0;
    reward_baseline_ = 0.0f;
    last_action_was_learning_ = false;
    rng_.seed(initial_seed_);
}

float PlasticReadout::l2(
    const std::array<
        std::array<float, kPlasticFeatureCount>,
        kPlasticActionCount>& w)
{
    double sum = 0.0;
    for (const auto& action : w)
        for (float x : action)
            sum += static_cast<double>(x) * x;

    return static_cast<float>(
        std::sqrt(sum));
}

PlasticReadoutDiagnostics
PlasticReadout::diagnostics() const
{
    PlasticReadoutDiagnostics d;
    d.training_steps = training_steps_;
    d.cumulative_reward = cumulative_reward_;
    d.reward_baseline = reward_baseline_;
    d.weight_l2 = l2(weights_);
    return d;
}

bool PlasticReadout::save(
    const std::string& path,
    const std::string& fly_name,
    std::string& error) const
{
    error.clear();

    try {
        const fs::path p(path);
        if (p.has_parent_path())
            fs::create_directories(p.parent_path());

        std::ofstream out(path);
        if (!out) {
            error = "Could not open checkpoint for writing: "
                + path;
            return false;
        }

        out
            << kMagic << '\n'
            << "fly_name " << fly_name << '\n'
            << "training_steps "
            << training_steps_ << '\n'
            << "cumulative_reward "
            << std::setprecision(17)
            << cumulative_reward_ << '\n'
            << "reward_baseline "
            << reward_baseline_ << '\n'
            << "feature_count "
            << kPlasticFeatureCount << '\n'
            << "action_count "
            << kPlasticActionCount << '\n'
            << "weights\n";

        out << std::setprecision(9);

        for (size_t a = 0;
             a < kPlasticActionCount;
             ++a)
        {
            for (size_t j = 0;
                 j < kPlasticFeatureCount;
                 ++j)
            {
                if (j) out << ' ';
                out << weights_[a][j];
            }
            out << '\n';
        }

        return true;
    }
    catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool PlasticReadout::load(
    const std::string& path,
    std::string& error)
{
    error.clear();

    std::ifstream in(path);
    if (!in) {
        // Missing checkpoint is a valid fresh-training condition.
        return true;
    }

    std::string line;
    if (!std::getline(in, line)
        || line != kMagic)
    {
        error =
            "Unsupported or damaged training checkpoint: "
            + path;
        return false;
    }

    std::string key;

    std::getline(in, line); // fly_name metadata

    if (!(in >> key >> training_steps_)
        || key != "training_steps")
    {
        error = "Missing training_steps in " + path;
        return false;
    }

    if (!(in >> key >> cumulative_reward_)
        || key != "cumulative_reward")
    {
        error = "Missing cumulative_reward in " + path;
        return false;
    }

    if (!(in >> key >> reward_baseline_)
        || key != "reward_baseline")
    {
        error = "Missing reward_baseline in " + path;
        return false;
    }

    size_t feature_count = 0;
    size_t action_count = 0;

    if (!(in >> key >> feature_count)
        || key != "feature_count"
        || feature_count != kPlasticFeatureCount)
    {
        error = "Feature-count mismatch in " + path;
        return false;
    }

    if (!(in >> key >> action_count)
        || key != "action_count"
        || action_count != kPlasticActionCount)
    {
        error = "Action-count mismatch in " + path;
        return false;
    }

    if (!(in >> key) || key != "weights") {
        error = "Missing weights block in " + path;
        return false;
    }

    for (size_t a = 0;
         a < kPlasticActionCount;
         ++a)
    {
        for (size_t j = 0;
             j < kPlasticFeatureCount;
             ++j)
        {
            if (!(in >> weights_[a][j])) {
                error =
                    "Truncated weights in " + path;
                return false;
            }
        }
    }

    reset_eligibility();

    // Continue exploration from a different stream after a loaded run.
    rng_.seed(
        0x9e3779b97f4a7c15ULL
        ^ training_steps_);

    return true;
}

} // namespace flyarena
