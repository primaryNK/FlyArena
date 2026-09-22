#pragma once
#include "flyarena_format.h"

#include <cstdint>
#include <string>
#include <vector>

namespace flyarena {

struct TopologyV2 {
    uint32_t neuron_count = 0;
    uint64_t edge_count = 0;

    std::vector<uint64_t> offsets;       // N + 1
    std::vector<uint32_t> targets;       // E
    std::vector<uint32_t> synapse_count; // E
    std::vector<float> norm;             // E

    std::vector<uint64_t> root_ids;      // N, compact index -> BANC v888 root id
    std::vector<uint8_t> nt_class;       // N
    std::vector<float> nt_score;         // N
    std::vector<uint8_t> region;         // N
    std::vector<uint8_t> neuron_flags;   // N
};

bool load_topology_v2(const std::string& path, TopologyV2& topo, std::string& err);
bool validate_topology_v2(const TopologyV2& topo, std::string& err);

const char* neurotransmitter_name(uint8_t value);
const char* region_name(uint8_t value);

} // namespace flyarena
