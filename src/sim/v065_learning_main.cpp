#include "gpu_dual_brain.h"
#include "topology_v2.h"
#include "io_map.h"
#include "arena_sim.h"
#include "equipment_combat.h"
#include "render_snapshot.h"
#include "runtime_tuning.h"
#include "win32_renderer.h"
#include "plastic_readout.h"
#include "fly_profile.h"
#include "match_rules.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace flyarena;

namespace {

constexpr int kSimulationRestartRequested = 100;
constexpr Vec2 kRedSpawn{-0.62f, 0.0f};
constexpr Vec2 kBlueSpawn{0.62f, 0.0f};
constexpr float kRedSpawnHeading = 0.0f;
constexpr float kBlueSpawnHeading = 3.14159265358979323846f;
constexpr const char* kTrainerCheckpointPath =
    "data\\training\\Trainer_v065.flytrain";

struct Cli {
    std::string topology = "data\\cache\\banc_latest_v888_v3.farena";
    std::string io_map = "data\\io\\banc_v888_io_v061.fio";

    std::string red_name = "Ruby";
    std::string blue_name = "Azure";

    float duration_ms = 60000.0f;
    float world_step_ms = 50.0f;
    float gpu_budget = 0.40f;

    uint32_t visual_cap = 192;
    uint32_t body_side_cap = 64;
    uint32_t body_center_cap = 32;

    float tonic_body_hz = 2.0f;
    float visual_max_hz = 60.0f;
    float contact_body_hz = 45.0f;

    float baseline_warmup_ms = 500.0f;
    float baseline_sample_ms = 1500.0f;
    float neutral_validation_ms = 1000.0f;

    int render_fps = 120;

    float initial_sensitivity = 2.05f;
    float initial_forward_gain = 1.35f;
    float initial_turn_gain = 1.15f;

    bool learning = true;
    float learning_rate = 0.0016f;
    float exploration_sigma = 0.16f;

    std::string red_train =
        "data\\training\\Ruby_v065.flytrain";
    std::string blue_train =
        "data\\training\\Azure_v065.flytrain";

    // Optional persistent character packages. Empty keeps the legacy v0.6.5a
    // hard-coded loadout for command-line compatibility.
    std::string red_flypack;
    std::string blue_flypack;
};

struct Groups {
    std::vector<uint32_t> visual_left;
    std::vector<uint32_t> visual_right;
    std::vector<uint32_t> body_left;
    std::vector<uint32_t> body_right;
    std::vector<uint32_t> body_center;

    std::vector<uint32_t> dn_left;
    std::vector<uint32_t> dn_right;
    std::vector<uint32_t> mn_left;
    std::vector<uint32_t> mn_right;
};

struct KeyNeuronSet {
    std::array<uint32_t, kKeyNeuronCount> indices{};
    std::array<uint64_t, kKeyNeuronCount> root_ids{};
};

struct NeuralReadout {
    float dn_left_hz = 0.0f;
    float dn_right_hz = 0.0f;
    float mn_left_hz = 0.0f;
    float mn_right_hz = 0.0f;
};

struct NeutralEnvelope {
    double quantile = 0.90;

    double dn_mean_center = 0.0;
    double mn_mean_center = 0.0;
    double dn_forward_threshold = 0.0;
    double mn_forward_threshold = 0.0;

    double dn_diff_center = 0.0;
    double mn_diff_center = 0.0;
    double dn_turn_deadband = 0.0;
    double mn_turn_deadband = 0.0;

    double mn_left_threshold = 0.0;
    double mn_right_threshold = 0.0;

    // Independent combat-action normalization.
    double mn_left_action_center = 0.0;
    double mn_right_action_center = 0.0;
    double mn_left_action_scale = 1.0;
    double mn_right_action_scale = 1.0;

    double dn_forward_scale = 1.0;
    double mn_forward_scale = 1.0;
    double dn_turn_scale = 1.0;
    double mn_turn_scale = 1.0;
};

struct ValidationScore {
    double red_forward = 0.0;
    double blue_forward = 0.0;
    double red_turn_abs = 0.0;
    double blue_turn_abs = 0.0;
};

void select_portable_runtime_root() {
    const fs::path default_topology =
        fs::path("data") / "cache" / "banc_latest_v888_v3.farena";
    std::error_code ec;
    if (fs::exists(default_topology, ec))
        return;

    std::vector<wchar_t> executable_path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, executable_path.data(),
        static_cast<DWORD>(executable_path.size()));
    if (length == 0
        || static_cast<size_t>(length) >= executable_path.size())
        return;

    const fs::path executable(
        std::wstring(executable_path.data(), length));
    const fs::path candidate_root =
        executable.parent_path().parent_path();
    ec.clear();
    if (!fs::exists(candidate_root / default_topology, ec))
        return;

    fs::current_path(candidate_root, ec);
    if (!ec) {
        std::cout
            << "[INFO] Runtime root: "
            << candidate_root.string() << "\n";
    }
}

bool parse(int argc, char** argv, Cli& c) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];

        auto need = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << flag << "\n";
                return nullptr;
            }
            return argv[++i];
        };

        if (a == "--topology") {
            auto v = need("--topology"); if (!v) return false; c.topology = v;
        } else if (a == "--io-map") {
            auto v = need("--io-map"); if (!v) return false; c.io_map = v;
        } else if (a == "--red-name") {
            auto v = need("--red-name"); if (!v) return false; c.red_name = v;
        } else if (a == "--blue-name") {
            auto v = need("--blue-name"); if (!v) return false; c.blue_name = v;
        } else if (a == "--duration-ms") {
            auto v = need("--duration-ms"); if (!v) return false; c.duration_ms = std::stof(v);
        } else if (a == "--world-step-ms") {
            auto v = need("--world-step-ms"); if (!v) return false; c.world_step_ms = std::stof(v);
        } else if (a == "--gpu-budget") {
            auto v = need("--gpu-budget"); if (!v) return false;
            c.gpu_budget = std::clamp(
                std::stof(v) / 100.0f, 0.05f, 1.0f);
        } else if (a == "--render-fps") {
            auto v = need("--render-fps"); if (!v) return false;
            c.render_fps = std::stoi(v);
            if (c.render_fps != 0)
                c.render_fps = std::max(60, c.render_fps);
        } else if (a == "--sensitivity") {
            auto v = need("--sensitivity"); if (!v) return false;
            c.initial_sensitivity = std::clamp(
                std::stof(v), 0.75f, 4.0f);
        } else if (a == "--forward-gain") {
            auto v = need("--forward-gain"); if (!v) return false;
            c.initial_forward_gain = std::clamp(
                std::stof(v), 0.10f, 4.0f);
        } else if (a == "--turn-gain") {
            auto v = need("--turn-gain"); if (!v) return false;
            c.initial_turn_gain = std::clamp(
                std::stof(v), 0.10f, 4.0f);
        } else if (a == "--learning") {
            auto v = need("--learning"); if (!v) return false;
            c.learning = std::stoi(v) != 0;
        } else if (a == "--learning-rate") {
            auto v = need("--learning-rate"); if (!v) return false;
            c.learning_rate = std::clamp(
                std::stof(v), 0.00001f, 0.05f);
        } else if (a == "--exploration") {
            auto v = need("--exploration"); if (!v) return false;
            c.exploration_sigma = std::clamp(
                std::stof(v), 0.0f, 0.80f);
        } else if (a == "--red-train") {
            auto v = need("--red-train"); if (!v) return false;
            c.red_train = v;
        } else if (a == "--blue-train") {
            auto v = need("--blue-train"); if (!v) return false;
            c.blue_train = v;
        } else if (a == "--red-flypack") {
            auto v = need("--red-flypack"); if (!v) return false;
            c.red_flypack = v;
        } else if (a == "--blue-flypack") {
            auto v = need("--blue-flypack"); if (!v) return false;
            c.blue_flypack = v;
        } else {
            std::cerr << "Unknown option: " << a << "\n";
            return false;
        }
    }

    return c.duration_ms >= 0.0f &&
           c.world_step_ms > 0.0f;
}

std::vector<uint32_t> select_mask_group(
    const TopologyV2& topo,
    const std::vector<uint32_t>& masks,
    uint32_t bit,
    uint32_t cap)
{
    std::vector<uint32_t> proofread;
    std::vector<uint32_t> fallback;

    for (uint32_t i = 0; i < topo.neuron_count; ++i) {
        if ((masks[i] & bit) == 0) continue;
        fallback.push_back(i);

        if (topo.neuron_flags[i] & NeuronFlagBits::Proofread)
            proofread.push_back(i);
    }

    const auto& src = proofread.empty() ? fallback : proofread;
    if (src.empty() || cap == 0)
        return {};

    const uint32_t take =
        std::min<uint32_t>(
            cap, static_cast<uint32_t>(src.size()));

    std::vector<uint32_t> out;
    out.reserve(take);

    for (uint32_t k = 0; k < take; ++k) {
        const size_t p =
            static_cast<size_t>(
                (static_cast<uint64_t>(k) * src.size()) / take);
        out.push_back(src[std::min(p, src.size() - 1)]);
    }

    return out;
}

std::vector<uint32_t> full_readout_group(
    const IOMapV1& io,
    uint32_t bit)
{
    std::vector<uint32_t> out;
    for (uint32_t i = 0; i < io.neuron_count; ++i) {
        if (io.readout_mask[i] & bit)
            out.push_back(i);
    }
    return out;
}

void set_group_rate(
    std::vector<float>& rates,
    uint32_t brain,
    uint32_t n,
    const std::vector<uint32_t>& group,
    float hz)
{
    if (hz <= 0.0f) return;

    const size_t base =
        static_cast<size_t>(brain) * n;

    for (uint32_t i : group)
        rates[base + i] =
            std::max(rates[base + i], hz);
}

KeyNeuronSet select_key_neurons(
    const TopologyV2& topo,
    const Groups& g)
{
    KeyNeuronSet out;
    size_t cursor = 0;

    auto append_top =
        [&](const std::vector<uint32_t>& group, size_t count) {
            std::vector<uint32_t> ranked = group;
            std::stable_sort(
                ranked.begin(), ranked.end(),
                [&](uint32_t a, uint32_t b) {
                    const uint64_t da =
                        topo.offsets[a + 1] - topo.offsets[a];
                    const uint64_t db =
                        topo.offsets[b + 1] - topo.offsets[b];
                    if (da != db) return da > db;
                    return a < b;
                });

            const size_t take =
                std::min(count, ranked.size());
            for (size_t i = 0; i < take && cursor < kKeyNeuronCount; ++i) {
                const uint32_t index = ranked[i];
                out.indices[cursor] = index;
                out.root_ids[cursor] = topo.root_ids[index];
                ++cursor;
            }
        };

    // 3 high-outdegree representatives from each low-level output pool.
    append_top(g.dn_left, 3);
    append_top(g.dn_right, 3);
    append_top(g.mn_left, 3);
    append_top(g.mn_right, 3);

    return out;
}

std::array<float, kKeyNeuronCount> key_neuron_activity_for(
    const NeuralStepResult& step,
    const std::vector<uint32_t>& previous,
    uint32_t brain,
    uint32_t neuron_count,
    const KeyNeuronSet& keys,
    float window_ms)
{
    std::array<float, kKeyNeuronCount> activity{};
    if (window_ms <= 0.0f) return activity;

    const size_t base =
        static_cast<size_t>(brain) * neuron_count;
    const float seconds = window_ms / 1000.0f;

    for (size_t i = 0; i < kKeyNeuronCount; ++i) {
        const uint32_t index = keys.indices[i];
        const uint32_t delta =
            step.cumulative_neuron_spike_counts[base + index]
            - previous[base + index];
        const float hz =
            static_cast<float>(delta) / seconds;

        // Smooth visual brightness only; does not feed back into simulation.
        activity[i] =
            std::clamp(
                1.0f - std::exp(-hz / 32.0f),
                0.0f, 1.0f);
    }
    return activity;
}

float group_delta_hz(
    const std::vector<uint32_t>& current,
    const std::vector<uint32_t>& previous,
    uint32_t brain,
    uint32_t n,
    const std::vector<uint32_t>& group,
    float window_ms)
{
    if (group.empty() || window_ms <= 0.0f)
        return 0.0f;

    const size_t base =
        static_cast<size_t>(brain) * n;

    uint64_t delta = 0;
    for (uint32_t i : group) {
        delta += static_cast<uint32_t>(
            current[base + i] - previous[base + i]);
    }

    return static_cast<float>(
        static_cast<double>(delta)
        / group.size()
        / (window_ms / 1000.0));
}

NeuralReadout readout_for(
    const NeuralStepResult& step,
    const std::vector<uint32_t>& previous,
    uint32_t brain,
    uint32_t n,
    const Groups& g,
    float window_ms)
{
    NeuralReadout r;

    r.dn_left_hz = group_delta_hz(
        step.cumulative_neuron_spike_counts,
        previous, brain, n,
        g.dn_left, window_ms);

    r.dn_right_hz = group_delta_hz(
        step.cumulative_neuron_spike_counts,
        previous, brain, n,
        g.dn_right, window_ms);

    r.mn_left_hz = group_delta_hz(
        step.cumulative_neuron_spike_counts,
        previous, brain, n,
        g.mn_left, window_ms);

    r.mn_right_hz = group_delta_hz(
        step.cumulative_neuron_spike_counts,
        previous, brain, n,
        g.mn_right, window_ms);

    return r;
}

double quantile(
    std::vector<double> x,
    double q)
{
    if (x.empty())
        return 0.0;

    std::sort(x.begin(), x.end());
    q = std::clamp(q, 0.0, 1.0);

    const double p =
        q * static_cast<double>(x.size() - 1);

    const size_t lo =
        static_cast<size_t>(std::floor(p));
    const size_t hi =
        static_cast<size_t>(std::ceil(p));

    if (lo == hi)
        return x[lo];

    const double t =
        p - static_cast<double>(lo);

    return x[lo] * (1.0 - t) + x[hi] * t;
}

double signed_deadband(
    double value,
    double center,
    double half_width)
{
    const double d = value - center;
    const double a = std::fabs(d);

    if (a <= half_width)
        return 0.0;

    return std::copysign(
        a - half_width, d);
}

NeutralEnvelope make_neutral_envelope(
    const std::vector<NeuralReadout>& red,
    const std::vector<NeuralReadout>& blue,
    double q)
{
    std::vector<double> dn_means;
    std::vector<double> mn_means;
    std::vector<double> dn_diffs;
    std::vector<double> mn_diffs;
    std::vector<double> mn_lefts;
    std::vector<double> mn_rights;

    auto append =
        [&](const std::vector<NeuralReadout>& ws) {
            for (const auto& r : ws) {
                dn_means.push_back(
                    0.5 * (r.dn_left_hz + r.dn_right_hz));
                mn_means.push_back(
                    0.5 * (r.mn_left_hz + r.mn_right_hz));

                dn_diffs.push_back(
                    static_cast<double>(
                        r.dn_left_hz - r.dn_right_hz));
                mn_diffs.push_back(
                    static_cast<double>(
                        r.mn_left_hz - r.mn_right_hz));

                mn_lefts.push_back(r.mn_left_hz);
                mn_rights.push_back(r.mn_right_hz);
            }
        };

    append(red);
    append(blue);

    NeutralEnvelope e;
    e.quantile = q;

    e.dn_mean_center =
        quantile(dn_means, 0.50);
    e.mn_mean_center =
        quantile(mn_means, 0.50);

    e.dn_forward_threshold =
        quantile(dn_means, q);
    e.mn_forward_threshold =
        quantile(mn_means, q);

    e.dn_diff_center =
        quantile(dn_diffs, 0.50);
    e.mn_diff_center =
        quantile(mn_diffs, 0.50);

    std::vector<double> dn_abs_dev;
    std::vector<double> mn_abs_dev;

    for (double v : dn_diffs)
        dn_abs_dev.push_back(
            std::fabs(v - e.dn_diff_center));

    for (double v : mn_diffs)
        mn_abs_dev.push_back(
            std::fabs(v - e.mn_diff_center));

    e.dn_turn_deadband =
        quantile(dn_abs_dev, q);
    e.mn_turn_deadband =
        quantile(mn_abs_dev, q);

    e.mn_left_threshold =
        quantile(mn_lefts, q);
    e.mn_right_threshold =
        quantile(mn_rights, q);

    // Sword and shield each get their own neutral normalization.
    // Median -> q90 becomes the useful [roughly 0..1] excursion range.
    e.mn_left_action_center =
        quantile(mn_lefts, 0.50);
    e.mn_right_action_center =
        quantile(mn_rights, 0.50);

    const double left_q90 =
        quantile(mn_lefts, 0.90);
    const double right_q90 =
        quantile(mn_rights, 0.90);

    e.mn_left_action_scale =
        std::max(
            0.20,
            left_q90 - e.mn_left_action_center);
    e.mn_right_action_scale =
        std::max(
            0.20,
            right_q90 - e.mn_right_action_center);

    e.dn_forward_scale =
        std::max(
            0.25,
            e.dn_forward_threshold
            - e.dn_mean_center);

    e.mn_forward_scale =
        std::max(
            0.25,
            e.mn_forward_threshold
            - e.mn_mean_center);

    e.dn_turn_scale =
        std::max(0.25, e.dn_turn_deadband);

    e.mn_turn_scale =
        std::max(0.25, e.mn_turn_deadband);

    return e;
}

void apply_neutral_rates(
    std::vector<float>& rates,
    uint32_t brain,
    uint32_t n,
    const Groups& g,
    float tonic_hz)
{
    set_group_rate(
        rates, brain, n,
        g.body_left, tonic_hz);

    set_group_rate(
        rates, brain, n,
        g.body_right, tonic_hz);

    set_group_rate(
        rates, brain, n,
        g.body_center, tonic_hz * 0.6f);
}

PlasticFeatures make_plastic_features(
    const NeuralReadout& r,
    const NeutralEnvelope& e,
    const std::array<float, kKeyNeuronCount>& key_activity)
{
    PlasticFeatures f{};
    size_t i = 0;

    f[i++] = 1.0f;

    const double dn_mean =
        0.5 * static_cast<double>(
            r.dn_left_hz + r.dn_right_hz);

    const double mn_mean =
        0.5 * static_cast<double>(
            r.mn_left_hz + r.mn_right_hz);

    const double dn_diff =
        static_cast<double>(
            r.dn_left_hz - r.dn_right_hz);

    const double mn_diff =
        static_cast<double>(
            r.mn_left_hz - r.mn_right_hz);

    auto squash =
        [](double x) -> float {
            return static_cast<float>(
                std::tanh(x));
        };

    f[i++] =
        squash(
            (dn_mean - e.dn_mean_center)
            / std::max(0.25, e.dn_forward_scale));

    f[i++] =
        squash(
            (mn_mean - e.mn_mean_center)
            / std::max(0.25, e.mn_forward_scale));

    f[i++] =
        squash(
            (dn_diff - e.dn_diff_center)
            / std::max(0.25, e.dn_turn_scale));

    f[i++] =
        squash(
            (mn_diff - e.mn_diff_center)
            / std::max(0.25, e.mn_turn_scale));

    f[i++] =
        squash(
            (static_cast<double>(r.mn_left_hz)
             - e.mn_left_action_center)
            / e.mn_left_action_scale);

    f[i++] =
        squash(
            (static_cast<double>(r.mn_right_hz)
             - e.mn_right_action_center)
            / e.mn_right_action_scale);

    for (size_t k = 0;
         k < kKeyNeuronCount
         && i < kPlasticFeatureCount;
         ++k)
    {
        // Key-neuron activity is already normalized to [0,1].
        f[i++] =
            2.0f * key_activity[k] - 1.0f;
    }

    return f;
}

ArenaControlFrame brain_to_control(
    const NeuralReadout& r,
    const NeutralEnvelope& e,
    const RuntimeTuning& tuning)
{
    ArenaControlFrame c;

    const double sensitivity =
        std::clamp(
            static_cast<double>(
                tuning.sensitivity.load()),
            0.75, 4.0);

    // Sensitivity > 1 narrows the actuator gate while preserving the neutral
    // center measured in v0.6.2d. It does not alter LIF neural state.
    const double mn_threshold =
        e.mn_mean_center
        + (e.mn_forward_threshold - e.mn_mean_center)
          / sensitivity;

    const double dn_threshold =
        e.dn_mean_center
        + (e.dn_forward_threshold - e.dn_mean_center)
          / sensitivity;

    const double mn_deadband =
        e.mn_turn_deadband / sensitivity;

    const double dn_deadband =
        e.dn_turn_deadband / sensitivity;

    const double mn_mean =
        0.5 * static_cast<double>(
            r.mn_left_hz + r.mn_right_hz);

    const double dn_mean =
        0.5 * static_cast<double>(
            r.dn_left_hz + r.dn_right_hz);

    const double mn_excess =
        std::max(0.0, mn_mean - mn_threshold);

    const double dn_excess =
        std::max(0.0, dn_mean - dn_threshold);

    double forward =
        0.84 * std::tanh(
            mn_excess
            / std::max(
                0.25,
                e.mn_forward_scale / sensitivity))
        + 0.16 * std::tanh(
            dn_excess
            / std::max(
                0.25,
                e.dn_forward_scale / sensitivity));

    forward *=
        static_cast<double>(
            tuning.forward_gain.load());

    c.forward =
        static_cast<float>(
            std::clamp(forward, 0.0, 1.0));

    const double dn_diff =
        static_cast<double>(
            r.dn_left_hz - r.dn_right_hz);

    const double mn_diff =
        static_cast<double>(
            r.mn_left_hz - r.mn_right_hz);

    const double dn_turn_excess =
        signed_deadband(
            dn_diff,
            e.dn_diff_center,
            dn_deadband);

    const double mn_turn_excess =
        signed_deadband(
            mn_diff,
            e.mn_diff_center,
            mn_deadband);

    double turn =
        0.78 * std::tanh(
            dn_turn_excess
            / std::max(
                0.25,
                e.dn_turn_scale / sensitivity))
        + 0.22 * std::tanh(
            mn_turn_excess
            / std::max(
                0.25,
                e.mn_turn_scale / sensitivity));

    turn *=
        static_cast<double>(
            tuning.turn_gain.load());

    c.turn =
        static_cast<float>(
            std::clamp(turn, -1.0, 1.0));

    c.left_foreleg =
        static_cast<float>(
            std::clamp(
                (static_cast<double>(r.mn_left_hz)
                 - e.mn_left_threshold)
                / std::max(
                    0.5, e.mn_forward_scale),
                0.0, 1.0));

    c.right_foreleg =
        static_cast<float>(
            std::clamp(
                (static_cast<double>(r.mn_right_hz)
                 - e.mn_right_threshold)
                / std::max(
                    0.5, e.mn_forward_scale),
                0.0, 1.0));

    // Independent low-level equipment channels.
    // They are not aliases of the old foreleg gates.
    const double sword_raw =
        (static_cast<double>(r.mn_right_hz)
         - e.mn_right_action_center)
        / e.mn_right_action_scale;

    const double shield_raw =
        (static_cast<double>(r.mn_left_hz)
         - e.mn_left_action_center)
        / e.mn_left_action_scale;

    c.sword_drive =
        static_cast<float>(
            std::clamp(
                0.5 + 0.5 * std::tanh(sword_raw - 0.55),
                0.0, 1.0));

    c.shield_drive =
        static_cast<float>(
            std::clamp(
                0.5 + 0.5 * std::tanh(shield_raw - 0.55),
                0.0, 1.0));

    c.source_flags = 1u;
    return c;
}

void apply_sensory_rates(
    std::vector<float>& rates,
    uint32_t brain,
    uint32_t n,
    const Groups& g,
    const ArenaSense& s,
    const Cli& cli)
{
    apply_neutral_rates(
        rates, brain, n,
        g, cli.tonic_body_hz);

    if (s.visual_left > 0.0f) {
        set_group_rate(
            rates, brain, n,
            g.visual_left,
            cli.visual_max_hz * s.visual_left);
    }

    if (s.visual_right > 0.0f) {
        set_group_rate(
            rates, brain, n,
            g.visual_right,
            cli.visual_max_hz * s.visual_right);
    }

    if (s.body_left > 0.0f) {
        set_group_rate(
            rates, brain, n,
            g.body_left,
            cli.tonic_body_hz
            + s.body_left
              * (cli.contact_body_hz
                 - cli.tonic_body_hz));
    }

    if (s.body_right > 0.0f) {
        set_group_rate(
            rates, brain, n,
            g.body_right,
            cli.tonic_body_hz
            + s.body_right
              * (cli.contact_body_hz
                 - cli.tonic_body_hz));
    }

    if (s.body_center > 0.0f) {
        set_group_rate(
            rates, brain, n,
            g.body_center,
            cli.tonic_body_hz
            + s.body_center
              * (cli.contact_body_hz
                 - cli.tonic_body_hz));
    }
}

float pair_distance(
    const FlyBodyState& a,
    const FlyBodyState& b)
{
    const float dx =
        b.position.x - a.position.x;
    const float dy =
        b.position.y - a.position.y;

    return std::sqrt(dx * dx + dy * dy);
}

ValidationScore validate_envelope(
    const NeutralEnvelope& e,
    const std::vector<NeuralReadout>& red,
    const std::vector<NeuralReadout>& blue)
{
    ValidationScore s;

    RuntimeTuning strict_neutral;
    strict_neutral.sensitivity.store(1.0f);
    strict_neutral.forward_gain.store(1.0f);
    strict_neutral.turn_gain.store(1.0f);

    for (const auto& r : red) {
        const auto c =
            brain_to_control(
                r, e, strict_neutral);

        s.red_forward += c.forward;
        s.red_turn_abs += std::fabs(c.turn);
    }

    for (const auto& r : blue) {
        const auto c =
            brain_to_control(
                r, e, strict_neutral);

        s.blue_forward += c.forward;
        s.blue_turn_abs += std::fabs(c.turn);
    }

    const double rn =
        static_cast<double>(
            std::max<size_t>(1, red.size()));

    const double bn =
        static_cast<double>(
            std::max<size_t>(1, blue.size()));

    s.red_forward /= rn;
    s.red_turn_abs /= rn;
    s.blue_forward /= bn;
    s.blue_turn_abs /= bn;

    return s;
}

ArenaRenderSnapshot make_snapshot(
    uint64_t sequence,
    double sim_time_ms,
    double world_step_ms,
    ArenaPhase phase,
    const std::string& status,
    const FlyBodyState& red,
    const FlyBodyState& blue,
    const ArenaSense& red_sense,
    const ArenaSense& blue_sense,
    const NeuralReadout& red_neural,
    const NeuralReadout& blue_neural,
    const ArenaControlFrame& red_control,
    const ArenaControlFrame& blue_control,
    float collision_radius_world,
    double match_duration_ms,
    double compute_ms)
{
    ArenaRenderSnapshot s;
    s.sequence = sequence;
    s.sim_time_ms = sim_time_ms;
    s.world_step_ms = world_step_ms;
    s.phase = phase;
    s.status = status;
    s.pair_distance = pair_distance(red, blue);
    s.match_duration_ms = match_duration_ms;
    s.neural_compute_ms_last_tick = compute_ms;

    auto fill =
        [&](FlyRenderState& out,
           const FlyBodyState& body,
           const ArenaSense& sense,
           const NeuralReadout& neural,
           const ArenaControlFrame& control)
    {
        out.name = body.identity.name;
        out.visual = visual_loadout_from_equipment(body.equipment, body.visual.body_scale);
        out.position = body.position;
        out.heading_rad = body.heading_rad;
        out.forward_speed = body.forward_speed;
        out.collision_radius_world = collision_radius_world;
        out.hp = body.hp;
        out.max_hp = body.max_hp;
        out.stamina = body.stamina;
        out.max_stamina = body.max_stamina;
        out.stamina_continuous = std::clamp(
            static_cast<float>(body.stamina)
            + body.stamina_fractional_delta,
            0.0f,
            static_cast<float>(body.max_stamina));
        out.stamina_recovery_eligible =
            body.stamina_recovery_eligible
            && out.stamina_continuous
               < static_cast<float>(body.max_stamina) - 0.001f;
        out.sword_relative_angle = body.sword_relative_angle;
        out.shield_relative_angle = body.shield_relative_angle;
        out.shield_deploy = body.shield_deploy;
        out.shield_center_distance_body_radii =
            shield_center_distance_world(
                body.equipment,
                collision_radius_world,
                body.shield_deploy)
            / std::max(0.001f, collision_radius_world);
        out.shield_raised = body.shield_raised;
        out.sword_swinging = body.sword_phase == SwordPhase::Swing;
        out.parry_window_remaining_s = body.parry_window_remaining_s;
        out.last_wall_impact_speed =
            body.last_wall_impact_speed;

        out.forward = control.forward;
        out.turn = control.turn;

        out.visual_left = sense.visual_left;
        out.visual_right = sense.visual_right;
        out.opponent_bearing_rad =
            sense.opponent_bearing_rad;
        out.opponent_visible =
            sense.opponent_visible;

        out.dn_left_hz = neural.dn_left_hz;
        out.dn_right_hz = neural.dn_right_hz;
        out.mn_left_hz = neural.mn_left_hz;
        out.mn_right_hz = neural.mn_right_hz;

        out.body_contacts = body.body_contacts;
        out.wall_contacts = body.wall_contacts;
        out.hits_landed = body.hits_landed;
        out.blocks = body.blocks;
        out.parries = body.parries;
        out.dodges = body.dodges;
    };

    fill(
        s.red, red,
        red_sense, red_neural, red_control);

    fill(
        s.blue, blue,
        blue_sense, blue_neural, blue_control);

    return s;
}

void publish_status(
    SnapshotBuffer& snapshots,
    uint64_t sequence,
    ArenaPhase phase,
    const std::string& status,
    const std::string& red_name,
    const std::string& blue_name,
    double world_step_ms)
{
    ArenaRenderSnapshot s;
    s.sequence = sequence;
    s.world_step_ms = world_step_ms;
    s.phase = phase;
    s.status = status;
    s.red.name = red_name;
    s.blue.name = blue_name;
    snapshots.publish(s);
}

int simulation_thread_main(
    const Cli cli,
    SnapshotBuffer& snapshots,
    RuntimeTuning& tuning)
{
    uint64_t sequence = 1;
    const uint64_t restart_revision_at_start =
        tuning.restart_revision.load();

    auto restart_requested = [&]() {
        return tuning.restart_revision.load()
            != restart_revision_at_start;
    };

    // Process-lifetime neural session. Episodes reset arena bodies and reward
    // traces, but intentionally retain the expensive BANC topology, GPU
    // resources, calibrated envelope, and continuously evolving CNS state.
    static TopologyV2 topo;
    static IOMapV1 io;
    static Groups groups;
    static KeyNeuronSet key_neurons;
    static GpuDualBrain brain;
    static std::vector<float> rates;
    static std::vector<uint32_t> previous_counts;
    static NeutralEnvelope envelope;
    static ValidationScore neutral_score;
    static bool neural_session_initialized = false;
    const bool reused_resident_neural_session = neural_session_initialized;
    std::string err;

    if (!neural_session_initialized) {

    publish_status(
        snapshots, sequence++,
        ArenaPhase::Booting,
        "Loading BANC topology and IO map...",
        cli.red_name, cli.blue_name,
        cli.world_step_ms);

    if (!load_topology_v2(
            cli.topology, topo, err))
    {
        publish_status(
            snapshots, sequence++,
            ArenaPhase::Failed,
            "Topology load failed: " + err,
            cli.red_name, cli.blue_name,
            cli.world_step_ms);
        return 2;
    }

    if (!load_io_map_v1(
            cli.io_map, io, err))
    {
        publish_status(
            snapshots, sequence++,
            ArenaPhase::Failed,
            "IO map load failed: " + err,
            cli.red_name, cli.blue_name,
            cli.world_step_ms);
        return 3;
    }

    if (io.neuron_count != topo.neuron_count) {
        publish_status(
            snapshots, sequence++,
            ArenaPhase::Failed,
            "IO map / topology neuron-count mismatch.",
            cli.red_name, cli.blue_name,
            cli.world_step_ms);
        return 4;
    }

    groups.visual_left =
        select_mask_group(
            topo, io.sensory_mask,
            SensoryVisualLeft,
            cli.visual_cap);

    groups.visual_right =
        select_mask_group(
            topo, io.sensory_mask,
            SensoryVisualRight,
            cli.visual_cap);

    groups.body_left =
        select_mask_group(
            topo, io.sensory_mask,
            SensoryBodyLeft,
            cli.body_side_cap);

    groups.body_right =
        select_mask_group(
            topo, io.sensory_mask,
            SensoryBodyRight,
            cli.body_side_cap);

    groups.body_center =
        select_mask_group(
            topo, io.sensory_mask,
            SensoryBodyCenter,
            cli.body_center_cap);

    groups.dn_left =
        full_readout_group(
            io, ReadoutDescendingLeft);
    groups.dn_right =
        full_readout_group(
            io, ReadoutDescendingRight);
    groups.mn_left =
        full_readout_group(
            io, ReadoutMotorLeft);
    groups.mn_right =
        full_readout_group(
            io, ReadoutMotorRight);

    if (groups.visual_left.empty() ||
        groups.visual_right.empty() ||
        groups.body_left.empty() ||
        groups.body_right.empty() ||
        groups.dn_left.empty() ||
        groups.dn_right.empty() ||
        groups.mn_left.empty() ||
        groups.mn_right.empty())
    {
        publish_status(
            snapshots, sequence++,
            ArenaPhase::Failed,
            "A required sensory/readout group is empty.",
            cli.red_name, cli.blue_name,
            cli.world_step_ms);
        return 5;
    }

    key_neurons = select_key_neurons(topo, groups);

    publish_status(
        snapshots, sequence++,
        ArenaPhase::Calibrating,
        "Initializing two whole-CNS GPU states...",
        cli.red_name, cli.blue_name,
        cli.world_step_ms);

    NeuralParameters params;
    NeuralRunConfig run;

    constexpr float open_ended_neural_capacity_ms =
        24.0f * 60.0f * 60.0f * 1000.0f; // 24 hours

    run.duration_ms =
        cli.baseline_warmup_ms
        + cli.baseline_sample_ms
        + cli.neutral_validation_ms
        + open_ended_neural_capacity_ms;

    run.dt_ms = 1.0f;
    run.telemetry_bin_ms =
        cli.world_step_ms;
    run.chunk_sim_ms =
        cli.world_step_ms;

    run.gpu_duty_target =
        cli.gpu_budget;

    run.realtime_pacing = true;
    run.unlimited = false;
    run.streaming = true;
    run.weight_mode =
        NeuralWeightMode::SynapseCount;

    rates.assign(
        static_cast<size_t>(2) * topo.neuron_count,
        0.0f);

    apply_neutral_rates(
        rates, 0,
        topo.neuron_count,
        groups, cli.tonic_body_hz);

    apply_neutral_rates(
        rates, 1,
        topo.neuron_count,
        groups, cli.tonic_body_hz);

    if (!brain.initialize_rates(
            topo, params, run,
            rates, err))
    {
        publish_status(
            snapshots, sequence++,
            ArenaPhase::Failed,
            "GPU neural initialization failed: " + err,
            cli.red_name, cli.blue_name,
            cli.world_step_ms);
        return 6;
    }

    // Warm-up.
    publish_status(
        snapshots, sequence++,
        ArenaPhase::Calibrating,
        "Neural warm-up...",
        cli.red_name, cli.blue_name,
        cli.world_step_ms);

    NeuralStepResult warmup;

    if (!brain.advance(
            cli.baseline_warmup_ms,
            warmup, err))
    {
        publish_status(
            snapshots, sequence++,
            ArenaPhase::Failed,
            "Neural warm-up failed: " + err,
            cli.red_name, cli.blue_name,
            cli.world_step_ms);
        return 7;
    }

    if (restart_requested())
        return kSimulationRestartRequested;

    previous_counts = warmup.cumulative_neuron_spike_counts;

    std::vector<NeuralReadout> red_baseline;
    std::vector<NeuralReadout> blue_baseline;

    const uint32_t baseline_windows =
        static_cast<uint32_t>(
            std::lround(
                cli.baseline_sample_ms
                / cli.world_step_ms));

    publish_status(
        snapshots, sequence++,
        ArenaPhase::Calibrating,
        "Measuring neutral neural-noise distribution...",
        cli.red_name, cli.blue_name,
        cli.world_step_ms);

    for (uint32_t w = 0;
         w < baseline_windows
         && !tuning.quit_requested.load()
         && !restart_requested();
         ++w)
    {
        NeuralStepResult step;

        if (!brain.advance(
                cli.world_step_ms,
                step, err))
        {
            publish_status(
                snapshots, sequence++,
                ArenaPhase::Failed,
                "Baseline measurement failed: " + err,
                cli.red_name, cli.blue_name,
                cli.world_step_ms);
            return 8;
        }

        red_baseline.push_back(
            readout_for(
                step, previous_counts,
                0, topo.neuron_count,
                groups,
                cli.world_step_ms));

        blue_baseline.push_back(
            readout_for(
                step, previous_counts,
                1, topo.neuron_count,
                groups,
                cli.world_step_ms));

        previous_counts =
            step.cumulative_neuron_spike_counts;
    }

    if (tuning.quit_requested.load())
        return 0;
    if (restart_requested())
        return kSimulationRestartRequested;

    // Held-out validation.
    const uint32_t validation_windows =
        static_cast<uint32_t>(
            std::lround(
                cli.neutral_validation_ms
                / cli.world_step_ms));

    std::vector<NeuralReadout> red_validation;
    std::vector<NeuralReadout> blue_validation;

    publish_status(
        snapshots, sequence++,
        ArenaPhase::Calibrating,
        "Validating neutral controller envelope...",
        cli.red_name, cli.blue_name,
        cli.world_step_ms);

    for (uint32_t w = 0;
         w < validation_windows
         && !tuning.quit_requested.load()
         && !restart_requested();
         ++w)
    {
        NeuralStepResult step;

        if (!brain.advance(
                cli.world_step_ms,
                step, err))
        {
            publish_status(
                snapshots, sequence++,
                ArenaPhase::Failed,
                "Neutral validation failed: " + err,
                cli.red_name, cli.blue_name,
                cli.world_step_ms);
            return 9;
        }

        red_validation.push_back(
            readout_for(
                step, previous_counts,
                0, topo.neuron_count,
                groups,
                cli.world_step_ms));

        blue_validation.push_back(
            readout_for(
                step, previous_counts,
                1, topo.neuron_count,
                groups,
                cli.world_step_ms));

        previous_counts =
            step.cumulative_neuron_spike_counts;
    }

    if (tuning.quit_requested.load())
        return 0;
    if (restart_requested())
        return kSimulationRestartRequested;

    bool neutral_ok = false;

    for (double q :
         std::vector<double>{
             0.90, 0.95, 0.975, 0.99})
    {
        const auto candidate =
            make_neutral_envelope(
                red_baseline,
                blue_baseline,
                q);

        const auto score =
            validate_envelope(
                candidate,
                red_validation,
                blue_validation);

        envelope = candidate;
        neutral_score = score;

        const double max_forward =
            std::max(
                score.red_forward,
                score.blue_forward);

        const double max_turn =
            std::max(
                score.red_turn_abs,
                score.blue_turn_abs);

        if (max_forward <= 0.10 &&
            max_turn <= 0.15)
        {
            neutral_ok = true;
            break;
        }
    }

    if (!neutral_ok) {
        publish_status(
            snapshots, sequence++,
            ArenaPhase::Failed,
            "Neutral controller calibration did not pass.",
            cli.red_name, cli.blue_name,
            cli.world_step_ms);
        return 10;
    }
    neural_session_initialized = true;
    } else {
        publish_status(
            snapshots, sequence++,
            ArenaPhase::Booting,
            "Reusing resident CNS/GPU session...",
            cli.red_name, cli.blue_name,
            cli.world_step_ms);
    }

    // First real learning layer: BANC CNS remains fixed, while a small
    // stochastic readout from CNS activity to low-level actuators is plastic.
    PlasticReadoutConfig plastic_cfg;
    plastic_cfg.learning_rate =
        cli.learning_rate;
    plastic_cfg.exploration_sigma =
        cli.exploration_sigma;

    PlasticReadout red_policy(0x524544ULL);
    PlasticReadout blue_policy(0x424C5545ULL);

    red_policy.set_config(plastic_cfg);
    blue_policy.set_config(plastic_cfg);

    std::string red_train_path =
        tuning.training_slot(0).checkpoint_path;
    std::string blue_train_path =
        tuning.training_slot(1).checkpoint_path;
    if (red_train_path.empty()) red_train_path = cli.red_train;
    if (blue_train_path.empty()) blue_train_path = cli.blue_train;

    if (!red_policy.load(red_train_path, err)) {
        publish_status(
            snapshots, sequence++,
            ArenaPhase::Failed,
            "RED training checkpoint failed: " + err,
            cli.red_name, cli.blue_name,
            cli.world_step_ms);
        return 13;
    }

    if (!blue_policy.load(blue_train_path, err)) {
        publish_status(
            snapshots, sequence++,
            ArenaPhase::Failed,
            "BLUE training checkpoint failed: " + err,
            cli.red_name, cli.blue_name,
            cli.world_step_ms);
        return 14;
    }

    tuning.complete_training_command(
        0, true, red_train_path, "Loaded");
    tuning.complete_training_command(
        1, true, blue_train_path, "Loaded");

    TrainingCommand training_command;
    while (tuning.pop_training_command(training_command)) {
        const bool red_slot = training_command.slot == 0;
        PlasticReadout candidate(
            red_slot ? 0x524544ULL : 0x424C5545ULL);
        candidate.set_config(plastic_cfg);

        std::string command_error;
        if (training_command.kind
            == TrainingCommandKind::RandomizeTrainer)
        {
            const uint64_t generation =
                tuning.trainer_generation() + 1;
            const FlyProfile randomized =
                make_random_trainer_profile(generation);

            if (!candidate.save(
                    kTrainerCheckpointPath,
                    randomized.identity.name,
                    command_error))
            {
                tuning.complete_training_command(
                    1, false, {},
                    "RANDOMIZE ERROR: " + command_error);
                continue;
            }

            blue_policy = std::move(candidate);
            blue_train_path = kTrainerCheckpointPath;
            tuning.complete_trainer_randomize(
                randomized,
                generation,
                blue_train_path);
            continue;
        }

        if (training_command.kind == TrainingCommandKind::Load) {
            if (!candidate.load(training_command.path, command_error)) {
                tuning.complete_training_command(
                    training_command.slot, false, {},
                    "ERROR: " + command_error);
                continue;
            }

            if (red_slot) {
                red_policy = std::move(candidate);
                red_train_path = training_command.path;
            } else {
                blue_policy = std::move(candidate);
                blue_train_path = training_command.path;
            }
            tuning.complete_training_command(
                training_command.slot, true,
                training_command.path, "Imported · active");
            continue;
        }

        const std::string reset_path =
            training_command.path.empty()
            ? (red_slot ? red_train_path : blue_train_path)
            : training_command.path;
        const std::string fly_name = red_slot
            ? cli.red_name
            : (tuning.trainer_generation() > 0
               ? tuning.trainer_profile().identity.name
               : cli.blue_name);
        if (!candidate.save(reset_path, fly_name, command_error)) {
            tuning.complete_training_command(
                training_command.slot, false, {},
                "RESET ERROR: " + command_error);
            continue;
        }

        if (red_slot) {
            red_policy = std::move(candidate);
            red_train_path = reset_path;
        } else {
            blue_policy = std::move(candidate);
            blue_train_path = reset_path;
        }
        tuning.complete_training_command(
            training_command.slot, true,
            reset_path, "Learning reset · 0 steps");
    }

    FlyProfile red_profile;
    red_profile.identity.name = cli.red_name;
    red_profile.identity.uuid = "legacy-ruby-v065";
    red_profile.identity.lineage = "v0.6.7-default";
    red_profile.equipment.body_skin_id = 1;
    red_profile.equipment.wings.skin_id = 1;
    red_profile.equipment.sword.skin_id = 1;
    red_profile.equipment.shield.skin_id = 1;
    red_profile.equipment.primary_color_rgb = 0xEF333Bu;
    red_profile.equipment.secondary_color_rgb = 0xFF6470u;
    red_profile.equipment.wings.size_scale = 1.12f;
    red_profile.equipment.sword.length_scale = 1.30f;

    FlyProfile blue_profile;
    blue_profile.identity.name = cli.blue_name;
    blue_profile.identity.uuid = "legacy-azure-v065";
    blue_profile.identity.lineage = "v0.6.7-default";
    blue_profile.equipment.body_skin_id = 2;
    blue_profile.equipment.wings.skin_id = 2;
    blue_profile.equipment.sword.skin_id = 2;
    blue_profile.equipment.shield.skin_id = 2;
    blue_profile.equipment.primary_color_rgb = 0x3380FFu;
    blue_profile.equipment.secondary_color_rgb = 0x54A6FFu;
    blue_profile.equipment.wings.size_scale = 1.12f;
    blue_profile.equipment.sword.length_scale = 1.30f;

    auto load_profile = [&](
        const std::string& path,
        FlyProfile& profile,
        const char* slot) -> bool
    {
        if (path.empty())
            return true;

        std::vector<std::string> warnings;
        if (!load_flypack(path, profile, err, &warnings)) {
            publish_status(
                snapshots, sequence++,
                ArenaPhase::Failed,
                std::string(slot) + " flypack failed: " + err,
                cli.red_name, cli.blue_name,
                cli.world_step_ms);
            return false;
        }
        for (const std::string& warning : warnings) {
            std::cerr << "[WARN] " << slot
                      << " flypack: " << warning << "\n";
        }
        return true;
    };

    const TrainingSubmode episode_training_submode =
        tuning.training_submode.load();
    const PolicyMutationPermissions mutation_permissions =
        policy_mutation_permissions(
            tuning.app_mode.load(),
            episode_training_submode);
    const bool red_learning_enabled = mutation_permissions.red;
    const bool blue_learning_enabled = mutation_permissions.blue;

    if (!tuning.active_profile(0, red_profile)) {
        if (!load_profile(cli.red_flypack, red_profile, "RED"))
            return 15;
        tuning.complete_flypack_load(
            0, true, red_profile,
            cli.red_flypack.empty() ? "legacy-default" : cli.red_flypack,
            "Active");
    }

    if (episode_training_submode == TrainingSubmode::RandomTrainer
        && tuning.trainer_generation() > 0)
    {
        blue_profile = tuning.trainer_profile();
    } else if (!tuning.active_profile(1, blue_profile)) {
        if (!load_profile(cli.blue_flypack, blue_profile, "BLUE"))
            return 16;
        tuning.complete_flypack_load(
            1, true, blue_profile,
            cli.blue_flypack.empty() ? "legacy-default" : cli.blue_flypack,
            "Active");
    }

    FlypackCommand flypack_command;
    while (tuning.pop_flypack_command(flypack_command)) {
        FlyProfile candidate_profile;
        std::vector<std::string> warnings;
        std::string command_error;

        if (flypack_command.slot == 1
            && episode_training_submode
               == TrainingSubmode::RandomTrainer)
        {
            tuning.complete_flypack_load(
                1, false, blue_profile, {},
                "Rejected · managed Random Trainer slot");
            continue;
        }

        if (!load_flypack(
                flypack_command.path,
                candidate_profile,
                command_error,
                &warnings))
        {
            tuning.complete_flypack_load(
                flypack_command.slot, false,
                flypack_command.slot == 0 ? red_profile : blue_profile,
                {}, "ERROR: " + command_error);
            continue;
        }

        PlasticReadout candidate_policy(
            flypack_command.slot == 0
            ? 0x524544ULL : 0x424C5545ULL);
        candidate_policy.set_config(plastic_cfg);
        std::string referenced_checkpoint;
        if (candidate_profile.training.present) {
            referenced_checkpoint =
                (fs::path("data") / "training"
                 / candidate_profile.training.checkpoint_file).string();
            if (!candidate_policy.load(
                    referenced_checkpoint,
                    command_error))
            {
                tuning.complete_flypack_load(
                    flypack_command.slot, false,
                    flypack_command.slot == 0
                        ? red_profile : blue_profile,
                    {}, "CHECKPOINT ERROR: " + command_error);
                continue;
            }
        }

        if (flypack_command.slot == 0) {
            red_profile = candidate_profile;
            if (!referenced_checkpoint.empty()) {
                red_policy = std::move(candidate_policy);
                red_train_path = referenced_checkpoint;
                tuning.complete_training_command(
                    0, true, red_train_path,
                    "Flypack checkpoint · active");
            }
        } else {
            blue_profile = candidate_profile;
            if (!referenced_checkpoint.empty()) {
                blue_policy = std::move(candidate_policy);
                blue_train_path = referenced_checkpoint;
                tuning.complete_training_command(
                    1, true, blue_train_path,
                    red_learning_enabled
                        ? "Flypack checkpoint · frozen read only"
                        : "Flypack checkpoint · Battle read only");
            }
        }

        const std::string status = warnings.empty()
            ? "Imported · active"
            : "Imported · normalized to legal ranges";
        tuning.complete_flypack_load(
            flypack_command.slot, true,
            candidate_profile,
            flypack_command.path,
            status);
    }

    // Arena begins from the same continuously running CNS state. Identity,
    // skins, and all equipment physics now originate in one FlyProfile.
    FlyBodyState red;
    red.identity.name = red_profile.identity.name;
    red.identity.author = red_profile.identity.author;
    red.identity.lineage = red_profile.identity.lineage;
    red.equipment = red_profile.equipment;
    red.visual = visual_loadout_from_equipment(red.equipment, 1.00f);
    red.position = kRedSpawn;
    red.heading_rad = kRedSpawnHeading;

    FlyBodyState blue;
    blue.identity.name = blue_profile.identity.name;
    blue.identity.author = blue_profile.identity.author;
    blue.identity.lineage = blue_profile.identity.lineage;
    blue.equipment = blue_profile.equipment;
    blue.visual = visual_loadout_from_equipment(blue.equipment, 1.00f);
    blue.position = kBlueSpawn;
    blue.heading_rad = kBlueSpawnHeading;

    ArenaConfig arena;

    ArenaSense red_sense =
        sense_other_and_wall(
            red, blue, arena);
    ArenaSense blue_sense =
        sense_other_and_wall(
            blue, red, arena);

    NeuralReadout zero_neural{};
    ArenaControlFrame zero_control{};

    snapshots.publish(
        make_snapshot(
            sequence++,
            0.0,
            cli.world_step_ms,
            ArenaPhase::Live,
            "Arena live",
            red, blue,
            red_sense, blue_sense,
            zero_neural, zero_neural,
            zero_control, zero_control,
            arena.fly_radius,
            cli.duration_ms,
            0.0));

    fs::create_directories("results");

    std::ofstream telemetry(
        "results/v065_learning_telemetry.csv");

    telemetry
        << "time_ms,"
        << "red_x,red_y,red_heading,red_speed,red_hp,red_stamina,red_stamina_spent,red_stamina_spent_movement,red_stamina_spent_shield,red_stamina_recovered,red_stamina_recovery_eligible_s,red_stamina_recovery_blocked_movement_s,red_stamina_recovery_blocked_shield_s,red_damage_reward,red_learning_reward,red_reward_raw_positive,red_reward_raw_negative,red_reward_positive_scale,red_reward_negative_scale,red_reward_clamped,red_policy_l2,red_sword_drive,red_shield_drive,red_sword_phase,red_shield_raised,red_shield_deploy,red_sword_swings,red_shield_raises,red_sword_angle,red_shield_angle,red_wall_impact_speed,red_forward,red_turn,"
        << "red_visual_left,red_visual_right,red_opponent_bearing,red_opponent_visible,"
        << "red_dn_left_hz,red_dn_right_hz,"
        << "red_mn_left_hz,red_mn_right_hz,"
        << "blue_x,blue_y,blue_heading,blue_speed,blue_hp,blue_stamina,blue_stamina_spent,blue_stamina_spent_movement,blue_stamina_spent_shield,blue_stamina_recovered,blue_stamina_recovery_eligible_s,blue_stamina_recovery_blocked_movement_s,blue_stamina_recovery_blocked_shield_s,blue_damage_reward,blue_learning_reward,blue_reward_raw_positive,blue_reward_raw_negative,blue_reward_positive_scale,blue_reward_negative_scale,blue_reward_clamped,blue_policy_l2,blue_sword_drive,blue_shield_drive,blue_sword_phase,blue_shield_raised,blue_shield_deploy,blue_sword_swings,blue_shield_raises,blue_sword_angle,blue_shield_angle,blue_wall_impact_speed,blue_forward,blue_turn,"
        << "blue_visual_left,blue_visual_right,blue_opponent_bearing,blue_opponent_visible,"
        << "blue_dn_left_hz,blue_dn_right_hz,"
        << "blue_mn_left_hz,blue_mn_right_hz,"
        << "red_hits,red_blocks,red_parries,red_dodges,"
        << "blue_hits,blue_blocks,blue_parries,blue_dodges,"
        << "pair_distance,"
        << "sensitivity,forward_gain,turn_gain,"
        << "neural_compute_ms\n";

    int32_t match_result = 0;
    std::string result_reason;
    double match_end_time_ms = 0.0;
    CombatConfig combat;
    uint64_t next_combat_event_id = 1;
    uint64_t total_hit_events=0,total_block_events=0,total_parry_events=0,total_dodge_events=0;

    // Reward-ready contract for the future plastic learner:
    // every HP lost contributes exactly -1 reward.
    int64_t red_total_damage_reward = 0;
    int64_t blue_total_damage_reward = 0;
    AutomaticRewardBalancer red_reward_balancer;
    AutomaticRewardBalancer blue_reward_balancer;
    uint64_t training_ticks = 0;
    uint64_t battle_ticks = 0;
    uint64_t mode_switch_count = 0;
    uint64_t handled_mode_revision = tuning.mode_revision.load();

    auto save_cancelled_training = [&]() {
        if (training_ticks == 0)
            return;
        std::string checkpoint_error;
        if (red_learning_enabled
            && !red_policy.save(
                red_train_path,
                red.identity.name,
                checkpoint_error))
        {
            std::cerr
                << "[WARN] RED cancelled-match checkpoint save failed: "
                << checkpoint_error << "\n";
        }
        if (blue_learning_enabled
            && !blue_policy.save(
                blue_train_path,
                blue.identity.name,
                checkpoint_error))
        {
            std::cerr
                << "[WARN] BLUE cancelled-match checkpoint save failed: "
                << checkpoint_error << "\n";
        }
    };

    const bool open_ended =
        cli.duration_ms <= 0.0f;

    const uint64_t finite_ticks =
        open_ended
        ? 0ull
        : static_cast<uint64_t>(
            std::ceil(
                cli.duration_ms
                / cli.world_step_ms));

    auto next_max_heartbeat = std::chrono::steady_clock::now();

    for (uint64_t tick = 0;
         !tuning.quit_requested.load()
         && (open_ended || tick < finite_ticks);
         ++tick)
    {
        while (tuning.paused.load() &&
               !tuning.quit_requested.load()
               && !restart_requested())
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
        }

        if (tuning.quit_requested.load())
            break;
        if (restart_requested()) {
            save_cancelled_training();
            return kSimulationRestartRequested;
        }

        const auto tick_wall_started =
            std::chrono::steady_clock::now();

        const uint64_t mode_revision =
            tuning.mode_revision.load();
        if (mode_revision != handled_mode_revision) {
            red_policy.reset_eligibility();
            blue_policy.reset_eligibility();
            handled_mode_revision = mode_revision;
            ++mode_switch_count;
        }

        if (red_learning_enabled)
            ++training_ticks;
        else
            ++battle_ticks;

        red_sense =
            sense_other_and_wall(
                red, blue, arena);

        blue_sense =
            sense_other_and_wall(
                blue, red, arena);

        std::fill(
            rates.begin(),
            rates.end(),
            0.0f);

        apply_sensory_rates(
            rates, 0,
            topo.neuron_count,
            groups, red_sense, cli);

        apply_sensory_rates(
            rates, 1,
            topo.neuron_count,
            groups, blue_sense, cli);

        if (!brain.update_stimulation_rates(
                rates, err))
        {
            publish_status(
                snapshots, sequence++,
                ArenaPhase::Failed,
                "Sensory upload failed: " + err,
                cli.red_name, cli.blue_name,
                cli.world_step_ms);
            return 11;
        }

        NeuralStepResult step;

        const float requested_speed = red_learning_enabled
            ? training_speed_multiplier(
                tuning.training_speed_option.load())
            : 1.0f;
        if (!brain.advance(
                cli.world_step_ms,
                step, err,
                0.0f))
        {
            publish_status(
                snapshots, sequence++,
                ArenaPhase::Failed,
                "Neural step failed: " + err,
                cli.red_name, cli.blue_name,
                cli.world_step_ms);
            return 12;
        }

        const NeuralReadout red_neural =
            readout_for(
                step, previous_counts,
                0, topo.neuron_count,
                groups,
                cli.world_step_ms);

        const NeuralReadout blue_neural =
            readout_for(
                step, previous_counts,
                1, topo.neuron_count,
                groups,
                cli.world_step_ms);

        const auto red_key_activity =
            key_neuron_activity_for(
                step, previous_counts,
                0, topo.neuron_count,
                key_neurons, cli.world_step_ms);

        const auto blue_key_activity =
            key_neuron_activity_for(
                step, previous_counts,
                1, topo.neuron_count,
                key_neurons, cli.world_step_ms);

        const ArenaControlFrame red_base_control =
            brain_to_control(
                red_neural,
                envelope,
                tuning);

        const ArenaControlFrame blue_base_control =
            brain_to_control(
                blue_neural,
                envelope,
                tuning);

        const PlasticFeatures red_features =
            make_plastic_features(
                red_neural,
                envelope,
                red_key_activity);

        const PlasticFeatures blue_features =
            make_plastic_features(
                blue_neural,
                envelope,
                blue_key_activity);

        const ArenaControlFrame red_control =
            red_policy.act(
                red_base_control,
                red_features,
                red_learning_enabled);

        const ArenaControlFrame blue_control =
            blue_policy.act(
                blue_base_control,
                blue_features,
                blue_learning_enabled);

        const float dt_s =
            cli.world_step_ms / 1000.0f;

        // Neural/sensory control remains at the 50 ms world cadence, but
        // kinetic flight physics is substepped to prevent fast flies from
        // tunneling through one another or through the circular wall.
        constexpr float max_physics_substep_s = 0.005f;
        const uint32_t physics_steps =
            std::max<uint32_t>(
                1,
                static_cast<uint32_t>(
                    std::ceil(dt_s / max_physics_substep_s)));

        const float physics_dt =
            dt_s / static_cast<float>(physics_steps);

        red.last_wall_impact_speed = 0.0f;
        blue.last_wall_impact_speed = 0.0f;

        const int32_t red_hp_before_tick = red.hp;
        const int32_t blue_hp_before_tick = blue.hp;

        std::vector<CombatEvent> tick_combat_events;
        tick_combat_events.reserve(8);

        for (uint32_t p = 0; p < physics_steps; ++p) {
            step_equipment(red, red_control, combat, physics_dt);
            step_equipment(blue, blue_control, combat, physics_dt);

            step_body(red, red_control, arena, physics_dt);
            step_body(blue, blue_control, arena, physics_dt);
            resolve_pair_contact(red, blue, arena);

            const double substep_time_ms = static_cast<double>(tick)*cli.world_step_ms + static_cast<double>(p+1)*physics_dt*1000.0;
            resolve_equipment_combat(red, blue, arena.fly_radius, combat, substep_time_ms, next_combat_event_id, tick_combat_events);
            resolve_equipment_combat(blue, red, arena.fly_radius, combat, substep_time_ms, next_combat_event_id, tick_combat_events);
        }
        for(const auto& e:tick_combat_events){switch(e.type){case CombatEventType::Hit:++total_hit_events;break;case CombatEventType::Block:++total_block_events;break;case CombatEventType::Parry:++total_parry_events;break;case CombatEventType::Dodge:++total_dodge_events;break;}}

        const int32_t red_hp_lost =
            std::max<int32_t>(
                0,
                red_hp_before_tick - red.hp);

        const int32_t blue_hp_lost =
            std::max<int32_t>(
                0,
                blue_hp_before_tick - blue.hp);

        const int32_t red_damage_reward =
            -red_hp_lost;

        const int32_t blue_damage_reward =
            -blue_hp_lost;

        red_total_damage_reward += red_damage_reward;
        blue_total_damage_reward += blue_damage_reward;

        // Reward shaping:
        // - own HP loss is always bad (including wall damage)
        // - only self-caused combat success is positive; opponent wall crashes
        //   do NOT produce attack reward
        // - a small engagement term discourages learning a passive hiding
        //   policy while remaining much weaker than actual combat success
        const RewardTuning reward_tuning = tuning.reward_tuning();
        float red_positive_reward = 0.0f;
        float blue_positive_reward = 0.0f;
        float red_negative_reward =
            static_cast<float>(red_hp_lost)
            * reward_tuning.damage_penalty_scale;
        float blue_negative_reward =
            static_cast<float>(blue_hp_lost)
            * reward_tuning.damage_penalty_scale;

        for (const auto& e : tick_combat_events) {
            const bool red_actor =
                e.actor_name == red.identity.name;

            float bonus = 0.0f;

            switch (e.type) {
            case CombatEventType::Hit:
                bonus =
                    (2.0f
                     + 1.75f
                       * static_cast<float>(e.damage))
                    * reward_tuning.attack_success_scale;
                break;
            case CombatEventType::Block:
                bonus = 2.5f * reward_tuning.defense_success_scale;
                break;
            case CombatEventType::Parry:
                bonus = 8.0f * reward_tuning.defense_success_scale;
                break;
            case CombatEventType::Dodge:
                bonus = 2.0f * reward_tuning.defense_success_scale;
                break;
            }

            if (red_actor)
                red_positive_reward += bonus;
            else
                blue_positive_reward += bonus;
        }

        const float current_pair_distance =
            pair_distance(red, blue);

        if (red_sense.opponent_visible
            && current_pair_distance < 0.80f)
        {
            red_positive_reward +=
                0.010f * reward_tuning.engagement_scale;
        }

        if (blue_sense.opponent_visible
            && current_pair_distance < 0.80f)
        {
            blue_positive_reward +=
                0.010f * reward_tuning.engagement_scale;
        }

        const bool final_tick_by_time =
            !open_ended
            && (tick + 1 >= finite_ticks);

        const bool final_tick_by_ko =
            red.hp <= 0
            || blue.hp <= 0;

        if (final_tick_by_time
            || final_tick_by_ko)
        {
            const int32_t provisional_result =
                static_cast<int32_t>(
                    classify_match_result(red.hp, blue.hp));

            if (provisional_result == 1) {
                red_positive_reward +=
                    30.0f * reward_tuning.terminal_scale;
                blue_negative_reward +=
                    20.0f * reward_tuning.terminal_scale;
            } else if (provisional_result == 2) {
                blue_positive_reward +=
                    30.0f * reward_tuning.terminal_scale;
                red_negative_reward +=
                    20.0f * reward_tuning.terminal_scale;
            }
        }

        const BalancedRewardSample red_reward_sample =
            red_reward_balancer.apply(
                red_positive_reward,
                red_negative_reward);
        const BalancedRewardSample blue_reward_sample =
            blue_reward_balancer.apply(
                blue_positive_reward,
                blue_negative_reward);
        const float red_learning_reward =
            red_reward_sample.balanced_reward;
        const float blue_learning_reward =
            blue_reward_sample.balanced_reward;

        if (red_learning_enabled) {
            red_policy.learn(
                red_learning_reward);
        }
        if (blue_learning_enabled) {
            blue_policy.learn(
                blue_learning_reward);
        }

        previous_counts =
            step.cumulative_neuron_spike_counts;

        const double match_time_ms =
            static_cast<double>(tick + 1)
            * cli.world_step_ms;

        // Pace the complete world tick, not just the GPU dispatch. This makes
        // timer, movement, physics, learning, and rendering snapshots advance
        // at the selected 1x..16x simulation rate. MAX has no wall-clock wait.
        if (requested_speed > 0.0f) {
            const double target_tick_wall_ms =
                training_tick_target_wall_ms(
                    cli.world_step_ms,
                    tuning.training_speed_option.load());
            const double elapsed_before_pacing_ms =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now()
                    - tick_wall_started).count();
            const double remaining_ms =
                target_tick_wall_ms - elapsed_before_pacing_ms;
            if (remaining_ms > 0.0) {
                std::this_thread::sleep_for(
                    std::chrono::duration<double, std::milli>(remaining_ms));
            }
        }

        const double tick_wall_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now()
                - tick_wall_started).count();
        const float instant_multiplier = static_cast<float>(
            cli.world_step_ms / std::max(0.001, tick_wall_ms));
        const float previous_multiplier =
            tuning.measured_simulation_multiplier.load();
        tuning.measured_simulation_multiplier.store(
            previous_multiplier <= 0.0f
            ? instant_multiplier
            : previous_multiplier * 0.85f
              + instant_multiplier * 0.15f);

        const bool max_speed_tick = red_learning_enabled
            && tuning.training_speed_option.load() == 5;

        // MAX publishes by wall clock rather than tick count. The renderer
        // shows frozen spawn models, episode, simulated timer and measured
        // throughput while all available compute advances the hidden match.
        const auto heartbeat_now = std::chrono::steady_clock::now();
        const bool publish_max_heartbeat =
            max_speed_tick && heartbeat_now >= next_max_heartbeat;
        if (!max_speed_tick || publish_max_heartbeat) {
        auto snap =
            make_snapshot(
                sequence++,
                match_time_ms,
                cli.world_step_ms,
                ArenaPhase::Live,
                (max_speed_tick
                    ? "MAX HEADLESS LEARNING | UI HEARTBEAT ONLY"
                    : red_learning_enabled
                    ? (blue_learning_enabled
                       ? "Arena live | RANDOM TRAINER · BOTH LEARNING"
                       : "Arena live | IMPORTED OPPONENT · LEFT LEARNING")
                    : "Arena live | BATTLE · FROZEN"),
                red, blue,
                red_sense, blue_sense,
                red_neural, blue_neural,
                red_control, blue_control,
                arena.fly_radius,
                cli.duration_ms,
                step.gpu_compute_wall_ms);

        snap.red.key_neuron_activity = red_key_activity;
        snap.blue.key_neuron_activity = blue_key_activity;
        snap.headless_max_training = max_speed_tick;
        snap.combat_event_count = static_cast<uint32_t>(std::min<size_t>(snap.combat_events.size(), tick_combat_events.size()));
        if(snap.combat_event_count>0){size_t start=tick_combat_events.size()-snap.combat_event_count;for(uint32_t i=0;i<snap.combat_event_count;++i)snap.combat_events[i]=tick_combat_events[start+i];}
        snapshots.publish(snap);
        if (publish_max_heartbeat) {
            next_max_heartbeat = heartbeat_now
                + std::chrono::milliseconds(250);
        }
        }

        if (!max_speed_tick) {
        telemetry
            << std::fixed
            << std::setprecision(6)
            << match_time_ms << ','
            << red.position.x << ','
            << red.position.y << ','
            << red.heading_rad << ','
            << red.forward_speed << ','
            << red.hp << ','
            << red.stamina << ','
            << red.stamina_spent_points << ','
            << red.stamina_spent_movement_points << ','
            << red.stamina_spent_shield_points << ','
            << red.stamina_recovered_points << ','
            << red.stamina_recovery_eligible_s << ','
            << red.stamina_recovery_blocked_movement_s << ','
            << red.stamina_recovery_blocked_shield_s << ','
            << red_damage_reward << ','
            << red_learning_reward << ','
            << red_reward_sample.raw_positive << ','
            << red_reward_sample.raw_negative_magnitude << ','
            << red_reward_sample.positive_scale << ','
            << red_reward_sample.negative_scale << ','
            << (red_reward_sample.clamped ? 1 : 0) << ','
            << red_policy.diagnostics().weight_l2 << ','
            << red_control.sword_drive << ','
            << red_control.shield_drive << ','
            << static_cast<uint32_t>(red.sword_phase) << ','
            << (red.shield_raised ? 1 : 0) << ','
            << red.shield_deploy << ','
            << red.sword_swings << ','
            << red.shield_raises << ','
            << red.sword_relative_angle << ','
            << red.shield_relative_angle << ','
            << red.last_wall_impact_speed << ','
            << red_control.forward << ','
            << red_control.turn << ','
            << red_sense.visual_left << ','
            << red_sense.visual_right << ','
            << red_sense.opponent_bearing_rad << ','
            << (red_sense.opponent_visible ? 1 : 0) << ','
            << red_neural.dn_left_hz << ','
            << red_neural.dn_right_hz << ','
            << red_neural.mn_left_hz << ','
            << red_neural.mn_right_hz << ','
            << blue.position.x << ','
            << blue.position.y << ','
            << blue.heading_rad << ','
            << blue.forward_speed << ','
            << blue.hp << ','
            << blue.stamina << ','
            << blue.stamina_spent_points << ','
            << blue.stamina_spent_movement_points << ','
            << blue.stamina_spent_shield_points << ','
            << blue.stamina_recovered_points << ','
            << blue.stamina_recovery_eligible_s << ','
            << blue.stamina_recovery_blocked_movement_s << ','
            << blue.stamina_recovery_blocked_shield_s << ','
            << blue_damage_reward << ','
            << blue_learning_reward << ','
            << blue_reward_sample.raw_positive << ','
            << blue_reward_sample.raw_negative_magnitude << ','
            << blue_reward_sample.positive_scale << ','
            << blue_reward_sample.negative_scale << ','
            << (blue_reward_sample.clamped ? 1 : 0) << ','
            << blue_policy.diagnostics().weight_l2 << ','
            << blue_control.sword_drive << ','
            << blue_control.shield_drive << ','
            << static_cast<uint32_t>(blue.sword_phase) << ','
            << (blue.shield_raised ? 1 : 0) << ','
            << blue.shield_deploy << ','
            << blue.sword_swings << ','
            << blue.shield_raises << ','
            << blue.sword_relative_angle << ','
            << blue.shield_relative_angle << ','
            << blue.last_wall_impact_speed << ','
            << blue_control.forward << ','
            << blue_control.turn << ','
            << blue_sense.visual_left << ','
            << blue_sense.visual_right << ','
            << blue_sense.opponent_bearing_rad << ','
            << (blue_sense.opponent_visible ? 1 : 0) << ','
            << blue_neural.dn_left_hz << ','
            << blue_neural.dn_right_hz << ','
            << blue_neural.mn_left_hz << ','
            << blue_neural.mn_right_hz << ','
            << red.hits_landed << ',' << red.blocks << ',' << red.parries << ',' << red.dodges << ','
            << blue.hits_landed << ',' << blue.blocks << ',' << blue.parries << ',' << blue.dodges << ','
            << pair_distance(red, blue) << ','
            << tuning.sensitivity.load() << ','
            << tuning.forward_gain.load() << ','
            << tuning.turn_gain.load() << ','
            << step.gpu_compute_wall_ms
            << '\n';
        }

        if ((red_learning_enabled || blue_learning_enabled)
            && ((tick + 1) % (max_speed_tick ? 1000 : 100) == 0))
        {
            std::string checkpoint_error;
            if (red_learning_enabled
                && !red_policy.save(
                    red_train_path,
                    red.identity.name,
                    checkpoint_error))
            {
                std::cerr
                    << "[WARN] RED checkpoint save failed: "
                    << checkpoint_error << "\n";
            }

            if (blue_learning_enabled
                && !blue_policy.save(
                    blue_train_path,
                    blue.identity.name,
                    checkpoint_error))
            {
                std::cerr
                    << "[WARN] BLUE checkpoint save failed: "
                    << checkpoint_error << "\n";
            }
        }

        if (match_has_ko(red.hp, blue.hp)) {
            match_end_time_ms = match_time_ms;
            result_reason = "KO";
            if (red.hp <= 0 && blue.hp <= 0)
                match_result = 3;
            else if (blue.hp <= 0)
                match_result = 1;
            else
                match_result = 2;
            break;
        }
    }

    if (match_result == 0 && !tuning.quit_requested.load() && !open_ended) {
        match_end_time_ms = cli.duration_ms;
        result_reason = "TIME";
        match_result = static_cast<int32_t>(
            classify_match_result(red.hp, blue.hp));
    }

    const auto final_sample =
        snapshots.sample();

    ArenaRenderSnapshot done;

    if (final_sample.valid)
        done = final_sample.current;

    done.sequence = sequence++;
    done.phase = ArenaPhase::Complete;
    done.match_duration_ms = cli.duration_ms;
    done.match_result = match_result;
    done.result_reason = result_reason;

    if (match_result == 1)
        done.status = cli.red_name + " WINS!";
    else if (match_result == 2)
        done.status = cli.blue_name + " WINS!";
    else if (match_result == 3)
        done.status = "DRAW";
    else
        done.status = "MATCH ENDED";

    snapshots.publish(done);

    if (red_learning_enabled || blue_learning_enabled) {
        std::string checkpoint_error;

        if (red_learning_enabled
            && !red_policy.save(
                red_train_path,
                red.identity.name,
                checkpoint_error))
        {
            std::cerr
                << "[WARN] RED final checkpoint save failed: "
                << checkpoint_error << "\n";
        }

        if (blue_learning_enabled
            && !blue_policy.save(
                blue_train_path,
                blue.identity.name,
                checkpoint_error))
        {
            std::cerr
                << "[WARN] BLUE final checkpoint save failed: "
                << checkpoint_error << "\n";
        }
    }

    const EquipmentMovementStats red_equipment_stats =
        derive_equipment_movement_stats(red.equipment, arena);
    const EquipmentMovementStats blue_equipment_stats =
        derive_equipment_movement_stats(blue.equipment, arena);
    const RewardTuning final_reward_tuning = tuning.reward_tuning();
    const RewardBalanceDiagnostics red_reward_diagnostics =
        red_reward_balancer.diagnostics();
    const RewardBalanceDiagnostics blue_reward_diagnostics =
        blue_reward_balancer.diagnostics();

    std::ofstream summary(
        "results/v065_summary.txt");

    summary
        << std::fixed
        << std::setprecision(6);

    summary
        << "FlyArena v0.6.7 full-loop accelerated plastic-readout arena\n"
        << "combat_telemetry_schema_version=3\n"
        << "session_mode="
        << (open_ended ? "window_lifetime" : "finite") << "\n"
        << "episode_number=" << tuning.episode_number.load() << "\n"
        << "resident_neural_session_reused="
        << (reused_resident_neural_session ? 1 : 0) << "\n"
        << "max_training_headless="
        << (red_learning_enabled
            && tuning.training_speed_option.load() == 5 ? 1 : 0) << "\n"
        << "match_duration_ms="
        << cli.duration_ms << "\n"
        << "match_end_time_ms="
        << match_end_time_ms << "\n"
        << "match_result="
        << (match_result == 1 ? "RED" :
            match_result == 2 ? "BLUE" :
            match_result == 3 ? "DRAW" : "ABORTED") << "\n"
        << "result_reason="
        << (result_reason.empty() ? "ABORTED" : result_reason) << "\n"
        << "neutral_quantile="
        << envelope.quantile << "\n"
        << "neutral_red_forward="
        << neutral_score.red_forward << "\n"
        << "neutral_blue_forward="
        << neutral_score.blue_forward << "\n"
        << "neutral_red_turn_abs="
        << neutral_score.red_turn_abs << "\n"
        << "neutral_blue_turn_abs="
        << neutral_score.blue_turn_abs << "\n"
        << "red_distance_travelled="
        << red.distance_travelled << "\n"
        << "blue_distance_travelled="
        << blue.distance_travelled << "\n"
        << "red_final_hp="
        << red.hp << "\n"
        << "blue_final_hp="
        << blue.hp << "\n"
        << "red_wall_damage_taken="
        << red.wall_damage_taken << "\n"
        << "blue_wall_damage_taken="
        << blue.wall_damage_taken << "\n"
        << "red_wall_contacts="
        << red.wall_contacts << "\n"
        << "blue_wall_contacts="
        << blue.wall_contacts << "\n"
        << "red_body_contacts="
        << red.body_contacts << "\n"
        << "blue_body_contacts=" << blue.body_contacts << "\n"
        << "red_sword_swings=" << red.sword_swings << "\n"
        << "blue_sword_swings=" << blue.sword_swings << "\n"
        << "red_sword_attempts_without_hit="
        << (red.sword_swings > red.hits_landed
            ? red.sword_swings - red.hits_landed : 0) << "\n"
        << "blue_sword_attempts_without_hit="
        << (blue.sword_swings > blue.hits_landed
            ? blue.sword_swings - blue.hits_landed : 0) << "\n"
        << "red_shield_raises=" << red.shield_raises << "\n"
        << "blue_shield_raises=" << blue.shield_raises << "\n"
        << "red_shield_raises_without_defense="
        << (red.shield_raises > red.blocks + red.parries
            ? red.shield_raises - red.blocks - red.parries : 0) << "\n"
        << "blue_shield_raises_without_defense="
        << (blue.shield_raises > blue.blocks + blue.parries
            ? blue.shield_raises - blue.blocks - blue.parries : 0) << "\n"
        << "red_final_shield_deploy=" << red.shield_deploy << "\n"
        << "blue_final_shield_deploy=" << blue.shield_deploy << "\n"
        << "red_hits_landed=" << red.hits_landed << "\n"
        << "blue_hits_landed=" << blue.hits_landed << "\n"
        << "red_blocks=" << red.blocks << "\n"
        << "blue_blocks=" << blue.blocks << "\n"
        << "red_parries=" << red.parries << "\n"
        << "blue_parries=" << blue.parries << "\n"
        << "red_dodges=" << red.dodges << "\n"
        << "blue_dodges=" << blue.dodges << "\n"
        << "total_hit_events=" << total_hit_events << "\n"
        << "total_block_events=" << total_block_events << "\n"
        << "total_parry_events=" << total_parry_events << "\n"
        << "total_dodge_events=" << total_dodge_events << "\n"
        << "red_final_stamina=" << red.stamina << "\n"
        << "blue_final_stamina=" << blue.stamina << "\n"
        << "red_stamina_spent=" << red.stamina_spent_points << "\n"
        << "blue_stamina_spent=" << blue.stamina_spent_points << "\n"
        << "red_stamina_spent_movement=" << red.stamina_spent_movement_points << "\n"
        << "blue_stamina_spent_movement=" << blue.stamina_spent_movement_points << "\n"
        << "red_stamina_spent_shield=" << red.stamina_spent_shield_points << "\n"
        << "blue_stamina_spent_shield=" << blue.stamina_spent_shield_points << "\n"
        << "red_stamina_recovered=" << red.stamina_recovered_points << "\n"
        << "blue_stamina_recovered=" << blue.stamina_recovered_points << "\n"
        << "red_stamina_recovery_eligible_s=" << red.stamina_recovery_eligible_s << "\n"
        << "blue_stamina_recovery_eligible_s=" << blue.stamina_recovery_eligible_s << "\n"
        << "red_stamina_recovery_blocked_movement_s=" << red.stamina_recovery_blocked_movement_s << "\n"
        << "blue_stamina_recovery_blocked_movement_s=" << blue.stamina_recovery_blocked_movement_s << "\n"
        << "red_stamina_recovery_blocked_shield_s=" << red.stamina_recovery_blocked_shield_s << "\n"
        << "blue_stamina_recovery_blocked_shield_s=" << blue.stamina_recovery_blocked_shield_s << "\n"
        << "red_total_damage_reward=" << red_total_damage_reward << "\n"
        << "blue_total_damage_reward=" << blue_total_damage_reward << "\n"
        << "reward_rule=v3_user_preferences_plus_automatic_sign_balance\n"
        << "reward_damage_penalty_scale=" << final_reward_tuning.damage_penalty_scale << "\n"
        << "reward_attack_success_scale=" << final_reward_tuning.attack_success_scale << "\n"
        << "reward_defense_success_scale=" << final_reward_tuning.defense_success_scale << "\n"
        << "reward_terminal_scale=" << final_reward_tuning.terminal_scale << "\n"
        << "reward_engagement_scale=" << final_reward_tuning.engagement_scale << "\n"
        << "red_reward_raw_positive=" << red_reward_diagnostics.cumulative_raw_positive << "\n"
        << "red_reward_raw_negative=" << red_reward_diagnostics.cumulative_raw_negative_magnitude << "\n"
        << "red_reward_balanced_total=" << red_reward_diagnostics.cumulative_balanced_reward << "\n"
        << "red_reward_clamped_samples=" << red_reward_diagnostics.clamped_samples << "\n"
        << "blue_reward_raw_positive=" << blue_reward_diagnostics.cumulative_raw_positive << "\n"
        << "blue_reward_raw_negative=" << blue_reward_diagnostics.cumulative_raw_negative_magnitude << "\n"
        << "blue_reward_balanced_total=" << blue_reward_diagnostics.cumulative_balanced_reward << "\n"
        << "blue_reward_clamped_samples=" << blue_reward_diagnostics.clamped_samples << "\n"
        << "learning_enabled="
        << (red_learning_enabled ? 1 : 0) << "\n"
        << "red_policy_updates_enabled="
        << (red_learning_enabled ? 1 : 0) << "\n"
        << "blue_policy_updates_enabled="
        << (blue_learning_enabled ? 1 : 0) << "\n"
        << "training_submode="
        << (episode_training_submode == TrainingSubmode::RandomTrainer
            ? "random_trainer" : "imported_opponent") << "\n"
        << "trainer_generation=" << tuning.trainer_generation() << "\n"
        << "training_speed_option="
        << tuning.training_speed_option.load() << "\n"
        << "requested_training_multiplier="
        << training_speed_multiplier(
            tuning.training_speed_option.load()) << "\n"
        << "measured_simulation_multiplier="
        << tuning.measured_simulation_multiplier.load() << "\n"
        << "training_ticks=" << training_ticks << "\n"
        << "battle_ticks=" << battle_ticks << "\n"
        << "mode_switch_count=" << mode_switch_count << "\n"
        << "red_flypack="
        << (cli.red_flypack.empty() ? "legacy-default" : cli.red_flypack) << "\n"
        << "blue_flypack="
        << (episode_training_submode == TrainingSubmode::RandomTrainer
            && tuning.trainer_generation() > 0
            ? "generated-random-trainer"
            : (cli.blue_flypack.empty()
               ? "legacy-default" : cli.blue_flypack)) << "\n"
        << "red_fly_uuid=" << red_profile.identity.uuid << "\n"
        << "blue_fly_uuid=" << blue_profile.identity.uuid << "\n"
        << "red_body_skin_id=" << red.equipment.body_skin_id << "\n"
        << "blue_body_skin_id=" << blue.equipment.body_skin_id << "\n"
        << "red_wing_skin_id=" << red.equipment.wings.skin_id << "\n"
        << "blue_wing_skin_id=" << blue.equipment.wings.skin_id << "\n"
        << "red_sword_skin_id=" << red.equipment.sword.skin_id << "\n"
        << "blue_sword_skin_id=" << blue.equipment.sword.skin_id << "\n"
        << "red_shield_skin_id=" << red.equipment.shield.skin_id << "\n"
        << "blue_shield_skin_id=" << blue.equipment.shield.skin_id << "\n"
        << "red_primary_color_rgb=" << red.equipment.primary_color_rgb << "\n"
        << "red_secondary_color_rgb=" << red.equipment.secondary_color_rgb << "\n"
        << "red_wing_primary_color_rgb=" << red.equipment.wings.primary_color_rgb << "\n"
        << "red_wing_secondary_color_rgb=" << red.equipment.wings.secondary_color_rgb << "\n"
        << "red_sword_primary_color_rgb=" << red.equipment.sword.primary_color_rgb << "\n"
        << "red_sword_secondary_color_rgb=" << red.equipment.sword.secondary_color_rgb << "\n"
        << "red_shield_primary_color_rgb=" << red.equipment.shield.primary_color_rgb << "\n"
        << "red_shield_secondary_color_rgb=" << red.equipment.shield.secondary_color_rgb << "\n"
        << "blue_primary_color_rgb=" << blue.equipment.primary_color_rgb << "\n"
        << "blue_secondary_color_rgb=" << blue.equipment.secondary_color_rgb << "\n"
        << "blue_wing_primary_color_rgb=" << blue.equipment.wings.primary_color_rgb << "\n"
        << "blue_wing_secondary_color_rgb=" << blue.equipment.wings.secondary_color_rgb << "\n"
        << "blue_sword_primary_color_rgb=" << blue.equipment.sword.primary_color_rgb << "\n"
        << "blue_sword_secondary_color_rgb=" << blue.equipment.sword.secondary_color_rgb << "\n"
        << "blue_shield_primary_color_rgb=" << blue.equipment.shield.primary_color_rgb << "\n"
        << "blue_shield_secondary_color_rgb=" << blue.equipment.shield.secondary_color_rgb << "\n"
        << "red_sword_length_scale=" << red.equipment.sword.length_scale << "\n"
        << "blue_sword_length_scale=" << blue.equipment.sword.length_scale << "\n"
        << "red_sword_recovery_scale=" << red.equipment.sword.recovery_scale << "\n"
        << "blue_sword_recovery_scale=" << blue.equipment.sword.recovery_scale << "\n"
        << "red_wing_size_scale=" << red.equipment.wings.size_scale << "\n"
        << "blue_wing_size_scale=" << blue.equipment.wings.size_scale << "\n"
        << "red_wing_drive_speed_scale=" << red.equipment.wings.drive_speed_scale << "\n"
        << "blue_wing_drive_speed_scale=" << blue.equipment.wings.drive_speed_scale << "\n"
        << "red_wing_stamina_cost_scale=" << red.equipment.wings.stamina_cost_scale << "\n"
        << "blue_wing_stamina_cost_scale=" << blue.equipment.wings.stamina_cost_scale << "\n"
        << "red_shield_mass_scale=" << red.equipment.shield.mass_scale << "\n"
        << "blue_shield_mass_scale=" << blue.equipment.shield.mass_scale << "\n"
        << "red_shield_size_scale=" << red.equipment.shield.size_scale << "\n"
        << "blue_shield_size_scale=" << blue.equipment.shield.size_scale << "\n"
        << "red_shield_stamina_cost_scale=" << red.equipment.shield.stamina_cost_scale << "\n"
        << "blue_shield_stamina_cost_scale=" << blue.equipment.shield.stamina_cost_scale << "\n"
        << "red_equipment_thrust_multiplier=" << red_equipment_stats.wing_thrust_multiplier * red_equipment_stats.shield_move_multiplier << "\n"
        << "blue_equipment_thrust_multiplier=" << blue_equipment_stats.wing_thrust_multiplier * blue_equipment_stats.shield_move_multiplier << "\n"
        << "red_equipment_speed_multiplier=" << red_equipment_stats.wing_speed_multiplier * red_equipment_stats.shield_move_multiplier << "\n"
        << "blue_equipment_speed_multiplier=" << blue_equipment_stats.wing_speed_multiplier * blue_equipment_stats.shield_move_multiplier << "\n"
        << "red_equipment_turn_multiplier=" << red_equipment_stats.shield_turn_multiplier << "\n"
        << "blue_equipment_turn_multiplier=" << blue_equipment_stats.shield_turn_multiplier << "\n"
        << "red_movement_stamina_multiplier=" << red_equipment_stats.movement_stamina_multiplier << "\n"
        << "blue_movement_stamina_multiplier=" << blue_equipment_stats.movement_stamina_multiplier << "\n"
        << "red_train_file="
        << red_train_path << "\n"
        << "blue_train_file="
        << blue_train_path << "\n"
        << "red_training_steps="
        << red_policy.diagnostics().training_steps << "\n"
        << "blue_training_steps="
        << blue_policy.diagnostics().training_steps << "\n"
        << "red_cumulative_learning_reward="
        << red_policy.diagnostics().cumulative_reward << "\n"
        << "blue_cumulative_learning_reward="
        << blue_policy.diagnostics().cumulative_reward << "\n"
        << "red_policy_weight_l2="
        << red_policy.diagnostics().weight_l2 << "\n"
        << "blue_policy_weight_l2="
        << blue_policy.diagnostics().weight_l2 << "\n"
        << "final_pair_distance="
        << pair_distance(red, blue) << "\n"
        << "final_sensitivity="
        << tuning.sensitivity.load() << "\n"
        << "final_forward_gain="
        << tuning.forward_gain.load() << "\n"
        << "final_turn_gain="
        << tuning.turn_gain.load() << "\n"
        << "key_neuron_root_ids=";

    for (size_t i = 0; i < kKeyNeuronCount; ++i) {
        if (i) summary << ',';
        summary << key_neurons.root_ids[i];
    }
    summary << "\n";

    return 0;
}

} // namespace

int main(int argc, char** argv) {
    // A directly launched bin\FlyArena.exe normally inherits bin as its
    // working directory. Recover the portable repository root so default
    // data, training, results, and flypack paths keep working.
    select_portable_runtime_root();

    Cli cli;

    if (!parse(argc, argv, cli)) {
        std::cerr
            << "FlyArena v0.6.7 arguments invalid.\n";
        return 1;
    }

    SnapshotBuffer snapshots;
    RuntimeTuning tuning;

    tuning.sensitivity.store(
        cli.initial_sensitivity);
    tuning.forward_gain.store(
        cli.initial_forward_gain);
    tuning.turn_gain.store(
        cli.initial_turn_gain);
    tuning.app_mode.store(
        cli.learning ? AppMode::Training : AppMode::Battle);
    tuning.initialize_training_slot(0, cli.red_train);
    tuning.initialize_imported_opponent(cli.blue_train);
    if (cli.learning) {
        constexpr uint64_t initial_trainer_generation = 1;
        tuning.initialize_trainer(
            make_random_trainer_profile(initial_trainer_generation),
            initial_trainer_generation,
            kTrainerCheckpointPath);
        tuning.initialize_training_slot(1, kTrainerCheckpointPath);
        tuning.repeat_matches.store(true);
    } else {
        tuning.initialize_training_slot(1, cli.blue_train);
    }

    RendererConfig renderer_config;
    renderer_config.target_fps =
        cli.render_fps;
    renderer_config.arena_radius_world =
        0.90f;

    std::atomic<int> simulation_result{0};

    std::thread simulation_thread(
        [&]() {
            int final_result = 0;

            while (!tuning.quit_requested.load()) {
                tuning.episode_number.fetch_add(1);
                const int result =
                    simulation_thread_main(
                        cli,
                        snapshots,
                        tuning);

                if (tuning.quit_requested.load())
                    break;

                if (result == kSimulationRestartRequested)
                    continue;

                if (result != 0) {
                    final_result = result;
                    break;
                }

                // A completed match leaves the renderer and controls alive.
                // PLAY/RESET/mode-switch changes restart_revision. REPEAT
                // starts the next episode after a short result-screen delay.
                const uint64_t completed_revision =
                    tuning.restart_revision.load();
                const auto completed_at =
                    std::chrono::steady_clock::now();

                while (!tuning.quit_requested.load()
                       && tuning.restart_revision.load()
                          == completed_revision)
                {
                    const double complete_age_s =
                        std::chrono::duration<double>(
                            std::chrono::steady_clock::now()
                            - completed_at).count();

                    const bool max_learning =
                        tuning.app_mode.load() == AppMode::Training
                        && tuning.training_speed_option.load() == 5;
                    if (tuning.repeat_matches.load()
                        && !tuning.paused.load()
                        && (max_learning || complete_age_s >= 0.75))
                    {
                        tuning.restart_revision.fetch_add(1);
                        break;
                    }

                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(20));
                }
            }

            simulation_result.store(final_result);
        });

    ArenaRenderer renderer(
        snapshots,
        tuning,
        renderer_config);

    const int renderer_result =
        renderer.run(
            GetModuleHandleW(nullptr),
            SW_SHOWDEFAULT);

    tuning.quit_requested.store(true);

    if (simulation_thread.joinable())
        simulation_thread.join();

    const int sim_result =
        simulation_result.load();

    if (sim_result != 0)
        return sim_result;

    return renderer_result;
}
