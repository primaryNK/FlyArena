#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl.h>

#include "gpu_dual_brain.h"
#include "gpu_pacing.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace flyarena {
namespace {

constexpr uint32_t kBrains = 2;
constexpr uint32_t kThreads = 128;
constexpr uint32_t kAccumulatorScale = 1000; // fixed-point units per mV

bool failed(HRESULT hr, const char* what, std::string& err) {
    if (SUCCEEDED(hr)) return false;
    std::ostringstream ss;
    ss << what << " failed, HRESULT=0x" << std::hex << static_cast<uint32_t>(hr);
    err = ss.str();
    return true;
}

struct Buffer {
    ComPtr<ID3D12Resource> resource;
    uint64_t bytes = 0;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
};

uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

bool create_buffer(
    ID3D12Device* device,
    uint64_t bytes,
    D3D12_HEAP_TYPE heap_type,
    D3D12_RESOURCE_FLAGS flags,
    D3D12_RESOURCE_STATES initial_state,
    Buffer& out,
    std::string& err)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = heap_type;

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = std::max<uint64_t>(bytes, 4);
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags = flags;

    if (failed(device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd, initial_state, nullptr,
        IID_PPV_ARGS(&out.resource)), "CreateCommittedResource", err)) {
        return false;
    }
    out.bytes = bytes;
    out.state = initial_state;
    return true;
}

D3D12_RESOURCE_BARRIER transition_barrier(
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = resource;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return b;
}

D3D12_RESOURCE_BARRIER global_uav_barrier() {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = nullptr;
    return b;
}

const char* kShader = R"HLSL(
cbuffer SimConstants : register(b0)
{
    uint NeuronCount;
    uint DelaySlots;
    uint WriteSlot;
    uint BinIndex;

    uint BinCount;
    uint StepIndex;
    uint WeightMode;
    uint AccumulatorScale;

    float DtMs;
    float TauMembraneMs;
    float TauSynapseMs;
    float RefractoryMs;

    float VRestMv;
    float VResetMv;
    float VThresholdMv;
    float SynapseWeightMv;

    float NormFullInputMv;
    float RedStimRateHz;
    float BlueStimRateHz;
    float MonoamineScale;

    uint UnknownExcitatory;
    uint PaddingU0;
    uint PaddingU1;
    uint PaddingU2;
};

StructuredBuffer<uint> Offsets       : register(t0);
StructuredBuffer<uint> Targets       : register(t1);
StructuredBuffer<uint> SynapseCounts : register(t2);
StructuredBuffer<float> Norms        : register(t3);
StructuredBuffer<uint> NtClass       : register(t4);
StructuredBuffer<float> StimRateHz    : register(t5);

RWStructuredBuffer<float> MembraneMv       : register(u0);
RWStructuredBuffer<float> SynapticDriveMv  : register(u1);
RWStructuredBuffer<float> RefractoryLeftMs : register(u2);
RWStructuredBuffer<int>   Accumulator       : register(u3);
RWStructuredBuffer<uint>  SpikeRing         : register(u4);
RWStructuredBuffer<uint>  BinSpikeCounts    : register(u5);
RWStructuredBuffer<uint>  NeuronSpikeCounts : register(u6);

float nt_scale(uint nt)
{
    // FlyArena v0.6.0 baseline policy:
    // ACh excitatory.
    // GABA + glutamate inhibitory (Shiu et al. baseline).
    // Histamine inhibitory for fast visual transmission.
    // DA/OA/5HT/tyramine are reduced to an excitatory LIF fallback;
    // this is explicitly an approximation, not receptor-resolved biology.
    if (nt == 1) return 1.0;                    // acetylcholine
    if (nt == 2) return MonoamineScale;         // dopamine
    if (nt == 3) return -1.0;                   // GABA
    if (nt == 4) return -1.0;                   // glutamate baseline
    if (nt == 5) return -1.0;                   // histamine
    if (nt == 6) return MonoamineScale;         // octopamine
    if (nt == 7) return MonoamineScale;         // serotonin
    if (nt == 8) return MonoamineScale;         // tyramine
    return UnknownExcitatory != 0 ? 1.0 : 0.0;  // unknown
}

uint hash32(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

float random01(uint x)
{
    return float(hash32(x) & 0x00ffffff) / 16777216.0;
}

[numthreads(128, 1, 1)]
void CSPropagate(uint3 tid : SV_DispatchThreadID)
{
    const uint total = NeuronCount * 2;
    const uint globalIndex = tid.x;
    if (globalIndex >= total) return;

    const uint brain = globalIndex / NeuronCount;
    const uint neuron = globalIndex - brain * NeuronCount;

    const uint ringIndex =
        (brain * DelaySlots + WriteSlot) * NeuronCount + neuron;

    if (SpikeRing[ringIndex] == 0) return;

    const float signScale = nt_scale(NtClass[neuron]);
    if (signScale == 0.0) return;

    const uint begin = Offsets[neuron];
    const uint end = Offsets[neuron + 1];

    for (uint e = begin; e < end; ++e)
    {
        const uint target = Targets[e];
        const uint targetGlobal = brain * NeuronCount + target;

        float impulseMv;
        if (WeightMode == 0)
        {
            impulseMv = float(SynapseCounts[e]) * SynapseWeightMv;
        }
        else
        {
            impulseMv = Norms[e] * NormFullInputMv;
        }

        const float signedMv = impulseMv * signScale;
        const int fixedImpulse = int(round(signedMv * float(AccumulatorScale)));

        if (fixedImpulse != 0)
        {
            InterlockedAdd(Accumulator[targetGlobal], fixedImpulse);
        }
    }
}

[numthreads(128, 1, 1)]
void CSUpdate(uint3 tid : SV_DispatchThreadID)
{
    const uint total = NeuronCount * 2;
    const uint globalIndex = tid.x;
    if (globalIndex >= total) return;

    const uint brain = globalIndex / NeuronCount;
    const uint neuron = globalIndex - brain * NeuronCount;
    const uint ringIndex =
        (brain * DelaySlots + WriteSlot) * NeuronCount + neuron;

    const int incomingFixed = Accumulator[globalIndex];
    Accumulator[globalIndex] = 0;

    float v = MembraneMv[globalIndex];
    float g = SynapticDriveMv[globalIndex]
            + float(incomingFixed) / float(AccumulatorScale);
    float refractory = RefractoryLeftMs[globalIndex];

    bool spiked = false;

    if (refractory > 0.0)
    {
        refractory = max(0.0, refractory - DtMs);
        v = VResetMv;
    }
    else
    {
        bool forcedStim = false;
        const float rateHz = max(0.0, StimRateHz[globalIndex]);
        if (rateHz > 0.0)
        {
            const float p = saturate(rateHz * DtMs / 1000.0);
            const uint seed =
                neuron * 747796405u
                + StepIndex * 2891336453u
                + brain * 277803737u
                + 0x9e3779b9u;
            forcedStim = random01(seed) < p;
        }

        if (forcedStim)
        {
            spiked = true;
        }
        else
        {
            v += DtMs * (g - (v - VRestMv)) / TauMembraneMs;
            if (v >= VThresholdMv)
                spiked = true;
        }

        if (spiked)
        {
            v = VResetMv;
            refractory = RefractoryMs;
        }
    }

    // First-order synaptic-drive decay used by the published LIF baseline.
    g *= exp(-DtMs / TauSynapseMs);

    MembraneMv[globalIndex] = v;
    SynapticDriveMv[globalIndex] = g;
    RefractoryLeftMs[globalIndex] = refractory;
    SpikeRing[ringIndex] = spiked ? 1u : 0u;

    if (spiked)
    {
        InterlockedAdd(
            BinSpikeCounts[brain * BinCount + BinIndex], 1u);
        InterlockedAdd(NeuronSpikeCounts[globalIndex], 1u);
    }
}
)HLSL";

struct alignas(256) SimConstants {
    uint32_t neuron_count;
    uint32_t delay_slots;
    uint32_t write_slot;
    uint32_t bin_index;

    uint32_t bin_count;
    uint32_t step_index;
    uint32_t weight_mode;
    uint32_t accumulator_scale;

    float dt_ms;
    float tau_membrane_ms;
    float tau_synapse_ms;
    float refractory_ms;

    float v_rest_mv;
    float v_reset_mv;
    float v_threshold_mv;
    float synapse_weight_mv;

    float norm_full_input_mv;
    float red_stim_rate_hz;
    float blue_stim_rate_hz;
    float monoamine_scale;

    uint32_t unknown_excitatory;
    uint32_t padding_u0;
    uint32_t padding_u1;
    uint32_t padding_u2;

    uint32_t padding[40]{};
};
static_assert(sizeof(SimConstants) == 256, "SimConstants must be one CBV alignment unit");

} // namespace

struct GpuDualBrain::Impl {
    NeuralParameters params;
    NeuralRunConfig config;

    uint32_t neuron_count = 0;
    uint64_t edge_count = 0;
    uint32_t total_neurons = 0;
    uint32_t delay_slots = 0;
    uint32_t bin_count = 0;
    uint32_t total_steps = 0;
    uint32_t chunk_steps = 0;
    uint32_t current_step = 0;
    double pending_auxiliary_wall_ms = 0.0;

    std::string adapter_name;
    uint64_t dedicated_vram = 0;

    ComPtr<IDXGIFactory6> factory;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    HANDLE fence_event = nullptr;
    uint64_t fence_value = 0;

    ComPtr<ID3D12RootSignature> root_signature;
    ComPtr<ID3D12PipelineState> pso_propagate;
    ComPtr<ID3D12PipelineState> pso_update;

    Buffer offsets;
    Buffer targets;
    Buffer counts;
    Buffer norms;
    Buffer nt_class;
    Buffer stim_rate;

    Buffer membrane;
    Buffer syn_drive;
    Buffer refractory;
    Buffer accumulator;
    Buffer spike_ring;
    Buffer bin_stats;
    Buffer neuron_spike_counts;

    Buffer constants_upload;
    uint8_t* constants_mapped = nullptr;

    ~Impl() {
        if (constants_upload.resource && constants_mapped) {
            constants_upload.resource->Unmap(0, nullptr);
            constants_mapped = nullptr;
        }
        if (fence_event) CloseHandle(fence_event);
    }

    bool wait(std::string& err) {
        ++fence_value;
        if (failed(queue->Signal(fence.Get(), fence_value), "Signal", err)) return false;
        if (fence->GetCompletedValue() < fence_value) {
            if (failed(
                fence->SetEventOnCompletion(fence_value, fence_event),
                "SetEventOnCompletion", err)) return false;
            WaitForSingleObject(fence_event, INFINITE);
        }
        return true;
    }

    bool initialize_device(std::string& err) {
        if (failed(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)),
                   "CreateDXGIFactory2", err)) return false;

        for (UINT i = 0;; ++i) {
            ComPtr<IDXGIAdapter1> candidate;
            HRESULT hr = factory->EnumAdapterByGpuPreference(
                i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&candidate));
            if (hr == DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(hr)) continue;

            DXGI_ADAPTER_DESC1 desc{};
            candidate->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

            if (SUCCEEDED(D3D12CreateDevice(
                candidate.Get(), D3D_FEATURE_LEVEL_12_0,
                __uuidof(ID3D12Device), nullptr))) {
                adapter = candidate;
                dedicated_vram = desc.DedicatedVideoMemory;

                char name[256]{};
                WideCharToMultiByte(
                    CP_UTF8, 0, desc.Description, -1,
                    name, 255, nullptr, nullptr);
                adapter_name = name;
                break;
            }
        }

        if (!adapter) {
            err = "No D3D12 hardware adapter found.";
            return false;
        }

        if (failed(D3D12CreateDevice(
            adapter.Get(), D3D_FEATURE_LEVEL_12_0,
            IID_PPV_ARGS(&device)), "D3D12CreateDevice", err)) return false;

        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        if (failed(device->CreateCommandQueue(
            &qd, IID_PPV_ARGS(&queue)), "CreateCommandQueue", err)) return false;
        if (failed(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_COMPUTE,
            IID_PPV_ARGS(&allocator)), "CreateCommandAllocator", err)) return false;
        if (failed(device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
            allocator.Get(), nullptr,
            IID_PPV_ARGS(&list)), "CreateCommandList", err)) return false;
        if (failed(list->Close(), "Initial CommandList Close", err)) return false;

        if (failed(device->CreateFence(
            0, D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&fence)), "CreateFence", err)) return false;

        fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!fence_event) {
            err = "CreateEvent failed.";
            return false;
        }
        return true;
    }

    bool create_pipeline(std::string& err) {
        std::vector<D3D12_ROOT_PARAMETER> rp(14);

        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rp[0].Descriptor.ShaderRegister = 0;
        rp[0].Descriptor.RegisterSpace = 0;
        rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        for (uint32_t i = 0; i < 6; ++i) {
            rp[1 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            rp[1 + i].Descriptor.ShaderRegister = i;
            rp[1 + i].Descriptor.RegisterSpace = 0;
            rp[1 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
        for (uint32_t i = 0; i < 7; ++i) {
            rp[7 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
            rp[7 + i].Descriptor.ShaderRegister = i;
            rp[7 + i].Descriptor.RegisterSpace = 0;
            rp[7 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }

        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = static_cast<UINT>(rp.size());
        rsd.pParameters = rp.data();
        rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> serialized;
        ComPtr<ID3DBlob> sig_error;
        HRESULT hr = D3D12SerializeRootSignature(
            &rsd, D3D_ROOT_SIGNATURE_VERSION_1,
            &serialized, &sig_error);
        if (FAILED(hr)) {
            if (sig_error) {
                err.assign(
                    static_cast<const char*>(sig_error->GetBufferPointer()),
                    sig_error->GetBufferSize());
            } else {
                failed(hr, "D3D12SerializeRootSignature", err);
            }
            return false;
        }

        if (failed(device->CreateRootSignature(
            0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
            IID_PPV_ARGS(&root_signature)), "CreateRootSignature", err)) {
            return false;
        }

        auto compile = [&](const char* entry, ComPtr<ID3DBlob>& blob) -> bool {
            ComPtr<ID3DBlob> errors;
            UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
            HRESULT chr = D3DCompile(
                kShader, std::strlen(kShader),
                "FlyArenaV060", nullptr, nullptr,
                entry, "cs_5_1", flags, 0,
                &blob, &errors);
            if (FAILED(chr)) {
                if (errors) {
                    err.assign(
                        static_cast<const char*>(errors->GetBufferPointer()),
                        errors->GetBufferSize());
                } else {
                    failed(chr, "D3DCompile", err);
                }
                return false;
            }
            return true;
        };

        ComPtr<ID3DBlob> propagate_blob, update_blob;
        if (!compile("CSPropagate", propagate_blob)) return false;
        if (!compile("CSUpdate", update_blob)) return false;

        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = root_signature.Get();

        pd.CS = {
            propagate_blob->GetBufferPointer(),
            propagate_blob->GetBufferSize()
        };
        if (failed(device->CreateComputePipelineState(
            &pd, IID_PPV_ARGS(&pso_propagate)),
            "CreateComputePipelineState(propagate)", err)) return false;

        pd.CS = {
            update_blob->GetBufferPointer(),
            update_blob->GetBufferSize()
        };
        if (failed(device->CreateComputePipelineState(
            &pd, IID_PPV_ARGS(&pso_update)),
            "CreateComputePipelineState(update)", err)) return false;

        return true;
    }

    bool upload_one(
        const void* data,
        uint64_t bytes,
        D3D12_RESOURCE_FLAGS flags,
        D3D12_RESOURCE_STATES final_state,
        Buffer& out,
        std::string& err)
    {
        if (!create_buffer(
            device.Get(), bytes, D3D12_HEAP_TYPE_DEFAULT,
            flags, D3D12_RESOURCE_STATE_COPY_DEST,
            out, err)) return false;

        Buffer upload;
        if (!create_buffer(
            device.Get(), bytes, D3D12_HEAP_TYPE_UPLOAD,
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            upload, err)) return false;

        void* mapped = nullptr;
        D3D12_RANGE no_read{0, 0};
        if (failed(upload.resource->Map(
            0, &no_read, &mapped), "Map(upload)", err)) return false;
        std::memcpy(mapped, data, static_cast<size_t>(bytes));
        upload.resource->Unmap(0, nullptr);

        if (failed(allocator->Reset(), "Allocator Reset(upload)", err)) return false;
        if (failed(list->Reset(
            allocator.Get(), nullptr), "CommandList Reset(upload)", err)) return false;

        list->CopyBufferRegion(
            out.resource.Get(), 0, upload.resource.Get(), 0, bytes);

        auto barrier = transition_barrier(
            out.resource.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            final_state);
        list->ResourceBarrier(1, &barrier);
        out.state = final_state;

        if (failed(list->Close(), "CommandList Close(upload)", err)) return false;
        ID3D12CommandList* lists[] = { list.Get() };
        queue->ExecuteCommandLists(1, lists);
        return wait(err);
    }

    template <typename T>
    bool upload_vector(
        const std::vector<T>& values,
        D3D12_RESOURCE_FLAGS flags,
        D3D12_RESOURCE_STATES final_state,
        Buffer& out,
        std::string& err)
    {
        return upload_one(
            values.data(),
            static_cast<uint64_t>(values.size()) * sizeof(T),
            flags, final_state, out, err);
    }

    bool create_constant_buffer(std::string& err) {
        const uint64_t bytes =
            static_cast<uint64_t>(chunk_steps) * sizeof(SimConstants);

        if (!create_buffer(
            device.Get(), bytes,
            D3D12_HEAP_TYPE_UPLOAD,
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            constants_upload, err)) return false;

        D3D12_RANGE no_read{0, 0};
        void* mapped = nullptr;
        if (failed(constants_upload.resource->Map(
            0, &no_read, &mapped),
            "Map(constants)", err)) return false;
        constants_mapped = static_cast<uint8_t*>(mapped);
        return true;
    }

    void set_resource_roots() {
        list->SetComputeRootShaderResourceView(
            1, offsets.resource->GetGPUVirtualAddress());
        list->SetComputeRootShaderResourceView(
            2, targets.resource->GetGPUVirtualAddress());
        list->SetComputeRootShaderResourceView(
            3, counts.resource->GetGPUVirtualAddress());
        list->SetComputeRootShaderResourceView(
            4, norms.resource->GetGPUVirtualAddress());
        list->SetComputeRootShaderResourceView(
            5, nt_class.resource->GetGPUVirtualAddress());
        list->SetComputeRootShaderResourceView(
            6, stim_rate.resource->GetGPUVirtualAddress());

        list->SetComputeRootUnorderedAccessView(
            7, membrane.resource->GetGPUVirtualAddress());
        list->SetComputeRootUnorderedAccessView(
            8, syn_drive.resource->GetGPUVirtualAddress());
        list->SetComputeRootUnorderedAccessView(
            9, refractory.resource->GetGPUVirtualAddress());
        list->SetComputeRootUnorderedAccessView(
            10, accumulator.resource->GetGPUVirtualAddress());
        list->SetComputeRootUnorderedAccessView(
            11, spike_ring.resource->GetGPUVirtualAddress());
        list->SetComputeRootUnorderedAccessView(
            12, bin_stats.resource->GetGPUVirtualAddress());
        list->SetComputeRootUnorderedAccessView(
            13, neuron_spike_counts.resource->GetGPUVirtualAddress());
    }

    SimConstants make_constants(uint32_t global_step) const {
        SimConstants c{};
        c.neuron_count = neuron_count;
        c.delay_slots = delay_slots;
        c.write_slot = global_step % delay_slots;

        const float time_ms = static_cast<float>(global_step) * config.dt_ms;
        c.bin_index = std::min<uint32_t>(
            bin_count - 1,
            static_cast<uint32_t>(time_ms / config.telemetry_bin_ms));

        c.bin_count = bin_count;
        c.step_index = global_step;
        c.weight_mode = static_cast<uint32_t>(config.weight_mode);
        c.accumulator_scale = kAccumulatorScale;

        c.dt_ms = config.dt_ms;
        c.tau_membrane_ms = params.tau_membrane_ms;
        c.tau_synapse_ms = params.tau_synapse_ms;
        c.refractory_ms = params.refractory_ms;

        c.v_rest_mv = params.v_rest_mv;
        c.v_reset_mv = params.v_reset_mv;
        c.v_threshold_mv = params.v_threshold_mv;
        c.synapse_weight_mv = params.synapse_weight_mv;

        c.norm_full_input_mv = params.norm_full_input_mv;
        c.red_stim_rate_hz = config.red_stim_rate_hz;
        c.blue_stim_rate_hz = config.blue_stim_rate_hz;
        c.monoamine_scale = params.monoamine_scale;

        c.unknown_excitatory = params.unknown_nt_excitatory ? 1u : 0u;
        return c;
    }

    bool dispatch_chunk(
        uint32_t first_step,
        uint32_t steps,
        double& compute_ms,
        std::string& err)
    {
        for (uint32_t local = 0; local < steps; ++local) {
            SimConstants c = make_constants(first_step + local);
            std::memcpy(
                constants_mapped + static_cast<size_t>(local) * sizeof(SimConstants),
                &c, sizeof(c));
        }

        if (failed(allocator->Reset(), "Allocator Reset(run)", err)) return false;
        if (failed(list->Reset(
            allocator.Get(), nullptr), "CommandList Reset(run)", err)) return false;

        list->SetComputeRootSignature(root_signature.Get());
        set_resource_roots();

        const uint32_t groups =
            (total_neurons + kThreads - 1) / kThreads;

        for (uint32_t local = 0; local < steps; ++local) {
            const D3D12_GPU_VIRTUAL_ADDRESS cbv =
                constants_upload.resource->GetGPUVirtualAddress()
                + static_cast<uint64_t>(local) * sizeof(SimConstants);
            list->SetComputeRootConstantBufferView(0, cbv);

            list->SetPipelineState(pso_propagate.Get());
            list->Dispatch(groups, 1, 1);

            auto barrier = global_uav_barrier();
            list->ResourceBarrier(1, &barrier);

            list->SetPipelineState(pso_update.Get());
            list->Dispatch(groups, 1, 1);

            barrier = global_uav_barrier();
            list->ResourceBarrier(1, &barrier);
        }

        if (failed(list->Close(), "CommandList Close(run)", err)) return false;

        auto start = std::chrono::steady_clock::now();
        ID3D12CommandList* lists[] = { list.Get() };
        queue->ExecuteCommandLists(1, lists);
        if (!wait(err)) return false;
        auto stop = std::chrono::steady_clock::now();

        compute_ms = std::chrono::duration<double, std::milli>(
            stop - start).count();
        return true;
    }

    template <typename T>
    bool readback(
        Buffer& src,
        size_t count,
        std::vector<T>& out,
        std::string& err)
    {
        const uint64_t bytes = static_cast<uint64_t>(count) * sizeof(T);
        if (bytes > src.bytes) {
            err = "Readback request exceeds source buffer.";
            return false;
        }

        Buffer rb;
        if (!create_buffer(
            device.Get(), bytes,
            D3D12_HEAP_TYPE_READBACK,
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_COPY_DEST,
            rb, err)) return false;

        if (failed(allocator->Reset(), "Allocator Reset(readback)", err)) return false;
        if (failed(list->Reset(
            allocator.Get(), nullptr), "CommandList Reset(readback)", err)) return false;

        auto to_copy = transition_barrier(
            src.resource.Get(), src.state,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1, &to_copy);

        list->CopyBufferRegion(
            rb.resource.Get(), 0,
            src.resource.Get(), 0, bytes);

        auto back = transition_barrier(
            src.resource.Get(),
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            src.state);
        list->ResourceBarrier(1, &back);

        if (failed(list->Close(), "CommandList Close(readback)", err)) return false;
        ID3D12CommandList* lists[] = { list.Get() };
        queue->ExecuteCommandLists(1, lists);
        if (!wait(err)) return false;

        void* mapped = nullptr;
        D3D12_RANGE read_range{0, static_cast<SIZE_T>(bytes)};
        if (failed(rb.resource->Map(
            0, &read_range, &mapped), "Map(readback)", err)) return false;

        out.resize(count);
        std::memcpy(out.data(), mapped, static_cast<size_t>(bytes));

        D3D12_RANGE no_write{0, 0};
        rb.resource->Unmap(0, &no_write);
        return true;
    }
};

GpuDualBrain::GpuDualBrain() : impl_(new Impl()) {}
GpuDualBrain::~GpuDualBrain() { delete impl_; }

bool GpuDualBrain::initialize_rates(
    const TopologyV2& topo,
    const NeuralParameters& params,
    const NeuralRunConfig& config,
    const std::vector<float>& stimulation_rate_hz,
    std::string& err)
{
    auto& g = *impl_;
    g.params = params;
    g.config = config;
    g.neuron_count = topo.neuron_count;
    g.edge_count = topo.edge_count;

    if (g.neuron_count == 0 || g.edge_count == 0) {
        err = "Topology is empty.";
        return false;
    }
    if (topo.edge_count > std::numeric_limits<uint32_t>::max()) {
        err = "V0.6.0 GPU CSR currently requires edge_count <= UINT32_MAX.";
        return false;
    }
    if (config.dt_ms <= 0.0f ||
        config.duration_ms <= 0.0f ||
        config.telemetry_bin_ms <= 0.0f) {
        err = "Invalid simulation timing parameters.";
        return false;
    }

    const uint64_t total_neurons64 =
        static_cast<uint64_t>(kBrains) * g.neuron_count;
    if (total_neurons64 > std::numeric_limits<uint32_t>::max()) {
        err = "Two-brain neuron state exceeds uint32 indexing.";
        return false;
    }
    g.total_neurons = static_cast<uint32_t>(total_neurons64);

    g.delay_slots = std::max<uint32_t>(
        1, static_cast<uint32_t>(
            std::lround(params.synaptic_delay_ms / config.dt_ms)));

    g.total_steps = std::max<uint32_t>(
        1, static_cast<uint32_t>(
            std::ceil(config.duration_ms / config.dt_ms)));

    g.bin_count = std::max<uint32_t>(
        1, static_cast<uint32_t>(
            std::ceil(config.duration_ms / config.telemetry_bin_ms)));

    g.chunk_steps = std::max<uint32_t>(
        1, static_cast<uint32_t>(
            std::ceil(config.chunk_sim_ms / config.dt_ms)));

    if (stimulation_rate_hz.size() != g.total_neurons) {
        err = "Stimulation-rate vector must contain exactly 2*N entries.";
        return false;
    }

    if (!g.initialize_device(err)) return false;
    if (!g.create_pipeline(err)) return false;

    // HLSL 5.1 root descriptors are easiest with 32-bit offsets.
    std::vector<uint32_t> offsets_u32(topo.offsets.size());
    for (size_t i = 0; i < topo.offsets.size(); ++i) {
        if (topo.offsets[i] > std::numeric_limits<uint32_t>::max()) {
            err = "CSR offset exceeds uint32 range.";
            return false;
        }
        offsets_u32[i] = static_cast<uint32_t>(topo.offsets[i]);
    }

    std::vector<uint32_t> nt_u32(topo.nt_class.begin(), topo.nt_class.end());

    const auto srv_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    const auto uav_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    const auto uav_flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if (!g.upload_vector(offsets_u32, D3D12_RESOURCE_FLAG_NONE, srv_state, g.offsets, err)) return false;
    if (!g.upload_vector(topo.targets, D3D12_RESOURCE_FLAG_NONE, srv_state, g.targets, err)) return false;
    if (!g.upload_vector(topo.synapse_count, D3D12_RESOURCE_FLAG_NONE, srv_state, g.counts, err)) return false;
    if (!g.upload_vector(topo.norm, D3D12_RESOURCE_FLAG_NONE, srv_state, g.norms, err)) return false;
    if (!g.upload_vector(nt_u32, D3D12_RESOURCE_FLAG_NONE, srv_state, g.nt_class, err)) return false;
    if (!g.upload_vector(stimulation_rate_hz, D3D12_RESOURCE_FLAG_NONE, srv_state, g.stim_rate, err)) return false;

    std::vector<float> initial_v(g.total_neurons, params.v_rest_mv);
    std::vector<float> zeros_f(g.total_neurons, 0.0f);
    std::vector<int32_t> zeros_i(g.total_neurons, 0);
    std::vector<uint32_t> zeros_u(g.total_neurons, 0);

    const uint64_t ring_count64 =
        static_cast<uint64_t>(kBrains)
        * g.delay_slots * g.neuron_count;
    if (ring_count64 > std::numeric_limits<size_t>::max()) {
        err = "Spike ring too large.";
        return false;
    }
    std::vector<uint32_t> spike_ring_init(
        static_cast<size_t>(ring_count64), 0);

    std::vector<uint32_t> bin_init(
        static_cast<size_t>(kBrains) * g.bin_count, 0);

    if (!g.upload_vector(initial_v, uav_flags, uav_state, g.membrane, err)) return false;
    if (!g.upload_vector(zeros_f, uav_flags, uav_state, g.syn_drive, err)) return false;
    if (!g.upload_vector(zeros_f, uav_flags, uav_state, g.refractory, err)) return false;
    if (!g.upload_vector(zeros_i, uav_flags, uav_state, g.accumulator, err)) return false;
    if (!g.upload_vector(spike_ring_init, uav_flags, uav_state, g.spike_ring, err)) return false;
    if (!g.upload_vector(bin_init, uav_flags, uav_state, g.bin_stats, err)) return false;
    if (!g.upload_vector(zeros_u, uav_flags, uav_state, g.neuron_spike_counts, err)) return false;

    if (!g.create_constant_buffer(err)) return false;
    g.current_step = 0;
    return true;
}

bool GpuDualBrain::initialize(
    const TopologyV2& topo,
    const NeuralParameters& params,
    const NeuralRunConfig& config,
    const std::vector<uint32_t>& stimulation_mask,
    std::string& err)
{
    const uint64_t total64 = static_cast<uint64_t>(topo.neuron_count) * 2ull;
    if (stimulation_mask.size() != total64) {
        err = "Stimulation mask must contain exactly 2*N entries.";
        return false;
    }
    std::vector<float> rates(stimulation_mask.size(), 0.0f);
    for (size_t i = 0; i < stimulation_mask.size(); ++i) {
        if (stimulation_mask[i] == 0) continue;
        const bool red = i < topo.neuron_count;
        rates[i] = red ? config.red_stim_rate_hz : config.blue_stim_rate_hz;
    }
    return initialize_rates(topo, params, config, rates, err);
}

bool GpuDualBrain::update_stimulation_rates(
    const std::vector<float>& stimulation_rate_hz,
    std::string& err)
{
    auto& g = *impl_;
    if (stimulation_rate_hz.size() != g.total_neurons) {
        err = "Stimulation-rate update must contain exactly 2*N entries.";
        return false;
    }
    const auto srv_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    const auto started = std::chrono::steady_clock::now();
    const bool uploaded = g.upload_vector(
        stimulation_rate_hz,
        D3D12_RESOURCE_FLAG_NONE,
        srv_state,
        g.stim_rate,
        err);
    if (uploaded) {
        g.pending_auxiliary_wall_ms +=
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
    }
    return uploaded;
}

bool GpuDualBrain::advance(
    float sim_duration_ms,
    NeuralStepResult& step,
    std::string& err,
    float realtime_multiplier)
{
    auto& g = *impl_;
    if (sim_duration_ms <= 0.0f) {
        err = "advance() duration must be positive.";
        return false;
    }
    if (!g.config.streaming && g.current_step >= g.total_steps) {
        err = "advance() called after configured simulation duration.";
        return false;
    }

    uint32_t requested = std::max<uint32_t>(
        1, static_cast<uint32_t>(std::lround(sim_duration_ms / g.config.dt_ms)));
    if (g.config.streaming) {
        if (requested > std::numeric_limits<uint32_t>::max() - g.current_step) {
            err = "streaming neural step counter reached its uint32 limit.";
            return false;
        }
    } else {
        requested = std::min<uint32_t>(
            requested, g.total_steps - g.current_step);
    }

    double compute_total = 0.0;
    double sleep_total = 0.0;
    const double upload_wall_ms = g.pending_auxiliary_wall_ms;
    g.pending_auxiliary_wall_ms = 0.0;
    uint32_t remaining = requested;
    while (remaining > 0) {
        const uint32_t steps = std::min<uint32_t>(g.chunk_steps, remaining);
        double compute_ms = 0.0;
        if (!g.dispatch_chunk(g.current_step, steps, compute_ms, err)) return false;
        compute_total += compute_ms;

        g.current_step += steps;
        remaining -= steps;
    }

    const auto readback_started = std::chrono::steady_clock::now();
    if (!g.readback(
        g.neuron_spike_counts,
        g.total_neurons,
        step.cumulative_neuron_spike_counts,
        err)) return false;
    const double readback_wall_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - readback_started).count();

    const double auxiliary_total = upload_wall_ms + readback_wall_ms;
    const double active_total = compute_total + auxiliary_total;
    const double pacing_simulated_ms = g.config.realtime_pacing
        ? static_cast<double>(requested) * g.config.dt_ms
        : 0.0;
    const double sleep_ms = gpu_pacing_sleep_ms(
        active_total,
        pacing_simulated_ms,
        static_cast<double>(realtime_multiplier),
        static_cast<double>(g.config.gpu_duty_target),
        g.config.unlimited);
    if (sleep_ms > 0.0) {
        std::this_thread::sleep_for(
            std::chrono::duration<double, std::milli>(sleep_ms));
        sleep_total += sleep_ms;
    }

    step.current_step = g.current_step;
    step.sim_time_ms = static_cast<float>(g.current_step) * g.config.dt_ms;
    step.gpu_compute_wall_ms = compute_total;
    step.gpu_auxiliary_wall_ms = auxiliary_total;
    step.gpu_active_wall_ms = active_total;
    step.scheduled_sleep_ms = sleep_total;
    return true;
}

bool GpuDualBrain::run(NeuralRunResult& result, std::string& err) {
    auto& g = *impl_;
    const auto total_start = std::chrono::steady_clock::now();

    double compute_total = 0.0;
    double sleep_total = 0.0;

    for (uint32_t first = 0; first < g.total_steps;) {
        const uint32_t steps =
            std::min<uint32_t>(g.chunk_steps, g.total_steps - first);

        double compute_ms = 0.0;
        if (!g.dispatch_chunk(first, steps, compute_ms, err)) return false;
        compute_total += compute_ms;

        if (!g.config.unlimited) {
            const double sim_ms =
                static_cast<double>(steps) * g.config.dt_ms;

            double target_total_ms = 0.0;

            if (g.config.realtime_pacing) {
                target_total_ms = std::max(target_total_ms, sim_ms);
            }

            const double duty =
                std::clamp<double>(g.config.gpu_duty_target, 0.05, 1.0);
            target_total_ms = std::max(
                target_total_ms,
                compute_ms / duty);

            const double sleep_ms =
                std::max(0.0, target_total_ms - compute_ms);
            if (sleep_ms > 0.0) {
                std::this_thread::sleep_for(
                    std::chrono::duration<double, std::milli>(sleep_ms));
                sleep_total += sleep_ms;
            }
        }

        first += steps;
    }

    if (!g.readback(
        g.bin_stats,
        static_cast<size_t>(kBrains) * g.bin_count,
        result.bin_spike_counts, err)) return false;

    if (!g.readback(
        g.neuron_spike_counts,
        g.total_neurons,
        result.neuron_spike_counts, err)) return false;

    if (!g.readback(
        g.membrane,
        g.total_neurons,
        result.final_membrane_mv, err)) return false;

    if (!g.readback(
        g.syn_drive,
        g.total_neurons,
        result.final_synaptic_drive_mv, err)) return false;

    const auto total_stop = std::chrono::steady_clock::now();
    const double total_wall = std::chrono::duration<double, std::milli>(
        total_stop - total_start).count();

    result.adapter_name = g.adapter_name;
    result.dedicated_vram_bytes = g.dedicated_vram;
    result.neuron_count = g.neuron_count;
    result.edge_count = g.edge_count;
    result.delay_slots = g.delay_slots;
    result.effective_delay_ms = g.delay_slots * g.config.dt_ms;
    result.gpu_compute_wall_ms = compute_total;
    result.scheduled_sleep_ms = sleep_total;
    result.total_wall_ms = total_wall;

    result.realtime_factor_compute_only =
        g.config.duration_ms / std::max(0.001, compute_total);
    result.realtime_factor_paced =
        g.config.duration_ms / std::max(0.001, total_wall);

    return true;
}

} // namespace flyarena
