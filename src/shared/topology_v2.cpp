#include "topology_v2.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace flyarena {

namespace {

template <typename T>
bool read_array(std::ifstream& in, uint64_t offset, std::vector<T>& out, size_t count, std::string& err) {
    out.resize(count);
    in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!in) {
        err = "seek failed at offset " + std::to_string(offset);
        return false;
    }
    const uint64_t bytes = static_cast<uint64_t>(count) * sizeof(T);
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(bytes));
    if (!in) {
        err = "read failed at offset " + std::to_string(offset) + " for " + std::to_string(bytes) + " bytes";
        return false;
    }
    return true;
}

} // namespace

bool load_topology_v2(const std::string& path, TopologyV2& topo, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "Could not open topology cache: " + path;
        return false;
    }

    TopologyV2Header h{};
    in.read(reinterpret_cast<char*>(&h), sizeof(h));
    if (!in) {
        err = "Could not read V2 header.";
        return false;
    }

    const char expected[8] = {'F','A','R','V','2','\0','\0','\0'};
    if (std::memcmp(h.magic, expected, 8) != 0) {
        err = "Not a FlyArena V2 topology cache.";
        return false;
    }
    if (h.version != 2 || h.header_size != sizeof(TopologyV2Header)) {
        err = "Unsupported FlyArena cache version/header.";
        return false;
    }

    topo.neuron_count = h.neuron_count;
    topo.edge_count = h.edge_count;

    if (!read_array(in, h.offsets_offset, topo.offsets, static_cast<size_t>(h.neuron_count) + 1, err)) return false;
    if (!read_array(in, h.targets_offset, topo.targets, static_cast<size_t>(h.edge_count), err)) return false;
    if (!read_array(in, h.synapse_count_offset, topo.synapse_count, static_cast<size_t>(h.edge_count), err)) return false;
    if (!read_array(in, h.norm_offset, topo.norm, static_cast<size_t>(h.edge_count), err)) return false;
    if (!read_array(in, h.root_ids_offset, topo.root_ids, h.neuron_count, err)) return false;
    if (!read_array(in, h.nt_class_offset, topo.nt_class, h.neuron_count, err)) return false;
    if (!read_array(in, h.nt_score_offset, topo.nt_score, h.neuron_count, err)) return false;
    if (!read_array(in, h.region_offset, topo.region, h.neuron_count, err)) return false;
    if (!read_array(in, h.neuron_flags_offset, topo.neuron_flags, h.neuron_count, err)) return false;

    return validate_topology_v2(topo, err);
}

bool validate_topology_v2(const TopologyV2& t, std::string& err) {
    if (t.neuron_count == 0 || t.edge_count == 0) {
        err = "Topology is empty.";
        return false;
    }
    if (t.offsets.size() != static_cast<size_t>(t.neuron_count) + 1) {
        err = "offsets size mismatch.";
        return false;
    }
    if (t.targets.size() != t.edge_count ||
        t.synapse_count.size() != t.edge_count ||
        t.norm.size() != t.edge_count) {
        err = "edge array size mismatch.";
        return false;
    }
    if (t.root_ids.size() != t.neuron_count ||
        t.nt_class.size() != t.neuron_count ||
        t.nt_score.size() != t.neuron_count ||
        t.region.size() != t.neuron_count ||
        t.neuron_flags.size() != t.neuron_count) {
        err = "neuron metadata size mismatch.";
        return false;
    }
    if (t.offsets.front() != 0 || t.offsets.back() != t.edge_count) {
        err = "CSR offsets do not span the full edge list.";
        return false;
    }
    for (uint32_t i = 0; i < t.neuron_count; ++i) {
        if (t.offsets[i] > t.offsets[i + 1]) {
            err = "CSR offsets are not monotonic.";
            return false;
        }
    }

    const uint64_t sample_stride = std::max<uint64_t>(1, t.edge_count / 100000);
    for (uint64_t e = 0; e < t.edge_count; e += sample_stride) {
        if (t.targets[static_cast<size_t>(e)] >= t.neuron_count) {
            err = "Target index out of range.";
            return false;
        }
        if (t.synapse_count[static_cast<size_t>(e)] == 0) {
            err = "Zero synapse-count edge found.";
            return false;
        }
        const float n = t.norm[static_cast<size_t>(e)];
        if (!std::isfinite(n) || n < 0.0f) {
            err = "Invalid norm value.";
            return false;
        }
    }

    if (!std::is_sorted(t.root_ids.begin(), t.root_ids.end())) {
        err = "root_ids must be sorted for compact-index lookup.";
        return false;
    }
    if (std::adjacent_find(t.root_ids.begin(), t.root_ids.end()) != t.root_ids.end()) {
        err = "Duplicate BANC root_ids in compact map.";
        return false;
    }

    return true;
}

const char* neurotransmitter_name(uint8_t value) {
    switch (static_cast<NeurotransmitterClass>(value)) {
        case NeurotransmitterClass::Acetylcholine: return "acetylcholine";
        case NeurotransmitterClass::Dopamine: return "dopamine";
        case NeurotransmitterClass::Gaba: return "gaba";
        case NeurotransmitterClass::Glutamate: return "glutamate";
        case NeurotransmitterClass::Histamine: return "histamine";
        case NeurotransmitterClass::Octopamine: return "octopamine";
        case NeurotransmitterClass::Serotonin: return "serotonin";
        case NeurotransmitterClass::Tyramine: return "tyramine";
        default: return "unknown";
    }
}

const char* region_name(uint8_t value) {
    switch (static_cast<RegionClass>(value)) {
        case RegionClass::CentralBrain: return "central_brain";
        case RegionClass::OpticLobe: return "optic_lobe";
        case RegionClass::VentralNerveCord: return "ventral_nerve_cord";
        case RegionClass::CervicalConnective: return "cervical_connective";
        default: return "unknown";
    }
}

} // namespace flyarena
