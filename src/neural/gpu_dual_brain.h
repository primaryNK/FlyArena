#pragma once

#include "topology_v2.h"

#include <cstdint>
#include <string>
#include <vector>

namespace flyarena {

enum class NeuralWeightMode : uint32_t {
    SynapseCount = 0,
    NormalizedInput = 1
};

struct NeuralParameters {
    // Literature-backed baseline from Shiu et al. Nature 2024.
    float v_rest_mv = -52.0f;
    float v_reset_mv = -52.0f;
    float v_threshold_mv = -45.0f;
    float tau_membrane_ms = 20.0f;
    float tau_synapse_ms = 5.0f;
    float refractory_ms = 2.2f;
    float synaptic_delay_ms = 1.8f;
    float synapse_weight_mv = 0.275f;

    // FlyArena experimental normalized-weight mode:
    // norm == 1 means 100% of the target's anatomical input.
    // 7 mV equals the baseline Vrest -> Vthreshold gap.
    float norm_full_input_mv = 7.0f;

    // Monoamines are not well represented by a binary LIF sign model.
    // 1.0 preserves the Shiu-style excitatory fallback for DA/OA/5HT.
    float monoamine_scale = 1.0f;

    // Scientifically conservative default: no inferred sign => no output.
    bool unknown_nt_excitatory = false;
};

struct NeuralRunConfig {
    float duration_ms = 1000.0f;
    float dt_ms = 1.0f;
    float telemetry_bin_ms = 50.0f;

    uint32_t stim_count_per_brain = 128;
    float red_stim_rate_hz = 70.0f;
    float blue_stim_rate_hz = 50.0f;

    NeuralWeightMode weight_mode = NeuralWeightMode::SynapseCount;

    // Runtime policy. In default mode each compute chunk sleeps enough to:
    // 1) not run simulation faster than wall-clock time, and
    // 2) target no more than this compute duty fraction where feasible.
    float gpu_duty_target = 0.40f;
    bool realtime_pacing = true;
    bool unlimited = false;

    // Closed-loop sessions may advance beyond duration_ms without resetting
    // neural state. duration_ms still sizes aggregate bin storage; run()
    // remains finite. Streaming is capped by the uint32 step counter.
    bool streaming = false;

    float chunk_sim_ms = 50.0f;
};

struct NeuralRunResult {
    std::string adapter_name;
    uint64_t dedicated_vram_bytes = 0;

    uint32_t neuron_count = 0;
    uint64_t edge_count = 0;
    uint32_t delay_slots = 0;
    float effective_delay_ms = 0.0f;

    double gpu_compute_wall_ms = 0.0;
    double scheduled_sleep_ms = 0.0;
    double total_wall_ms = 0.0;
    double realtime_factor_compute_only = 0.0;
    double realtime_factor_paced = 0.0;

    std::vector<uint32_t> bin_spike_counts;     // [brain * bin_count + bin]
    std::vector<uint32_t> neuron_spike_counts;  // [brain * N + neuron]
    std::vector<float> final_membrane_mv;        // [brain * N + neuron]
    std::vector<float> final_synaptic_drive_mv; // [brain * N + neuron]
};


struct NeuralStepResult {
    uint32_t current_step = 0;
    float sim_time_ms = 0.0f;
    double gpu_compute_wall_ms = 0.0;
    double scheduled_sleep_ms = 0.0;

    // Cumulative since initialize(). Caller can subtract the previous snapshot.
    std::vector<uint32_t> cumulative_neuron_spike_counts; // [brain * N + neuron]
};

class GpuDualBrain {
public:
    GpuDualBrain();
    ~GpuDualBrain();

    GpuDualBrain(const GpuDualBrain&) = delete;
    GpuDualBrain& operator=(const GpuDualBrain&) = delete;

    // Backward-compatible static-mask entry point.
    bool initialize(
        const TopologyV2& topo,
        const NeuralParameters& params,
        const NeuralRunConfig& config,
        const std::vector<uint32_t>& stimulation_mask,
        std::string& err);

    // V0.6.2 dynamic sensory entry point. Each of the 2*N neurons carries its
    // own Poisson forcing rate in Hz. Zero means no forced sensory event.
    bool initialize_rates(
        const TopologyV2& topo,
        const NeuralParameters& params,
        const NeuralRunConfig& config,
        const std::vector<float>& stimulation_rate_hz,
        std::string& err);

    bool update_stimulation_rates(
        const std::vector<float>& stimulation_rate_hz,
        std::string& err);

    // Advance without resetting neural state. This is the closed-loop primitive.
    bool advance(
        float sim_duration_ms,
        NeuralStepResult& step,
        std::string& err,
        float realtime_multiplier = 1.0f);

    bool run(NeuralRunResult& result, std::string& err);

private:
    struct Impl;
    Impl* impl_;
};

} // namespace flyarena
