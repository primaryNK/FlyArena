#pragma once
#include <cstdint>

namespace flyarena {

#pragma pack(push, 1)
struct TopologyV2Header {
    char magic[8];                 // "FARV2\0\0\0"
    uint32_t version;              // 2
    uint32_t header_size;          // sizeof(TopologyV2Header) == 128
    uint32_t neuron_count;
    uint32_t flags_schema_version; // 1
    uint64_t edge_count;

    uint64_t offsets_offset;
    uint64_t targets_offset;
    uint64_t synapse_count_offset;
    uint64_t norm_offset;
    uint64_t root_ids_offset;
    uint64_t nt_class_offset;
    uint64_t nt_score_offset;
    uint64_t region_offset;
    uint64_t neuron_flags_offset;
    uint64_t file_size;

    uint8_t reserved[16];
};
#pragma pack(pop)

static_assert(sizeof(TopologyV2Header) == 128, "TopologyV2Header must remain 128 bytes");

enum class NeurotransmitterClass : uint8_t {
    Unknown = 0,
    Acetylcholine = 1,
    Dopamine = 2,
    Gaba = 3,
    Glutamate = 4,
    Histamine = 5,
    Octopamine = 6,
    Serotonin = 7,
    Tyramine = 8
};

enum class RegionClass : uint8_t {
    Unknown = 0,
    CentralBrain = 1,
    OpticLobe = 2,
    VentralNerveCord = 3,
    CervicalConnective = 4
};

enum NeuronFlagBits : uint8_t {
    Proofread = 1 << 0,
    RoughlyProofread = 1 << 1
};

} // namespace flyarena
