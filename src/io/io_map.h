#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace flyarena {

enum SensoryBits : uint32_t {
    SensoryVisualLeft  = 1u << 0,
    SensoryVisualRight = 1u << 1,
    SensoryBodyLeft    = 1u << 2,
    SensoryBodyRight   = 1u << 3,
    SensoryBodyCenter  = 1u << 4
};

enum ReadoutBits : uint32_t {
    ReadoutDescendingLeft   = 1u << 0,
    ReadoutDescendingRight  = 1u << 1,
    ReadoutDescendingCenter = 1u << 2,
    ReadoutMotorLeft        = 1u << 3,
    ReadoutMotorRight       = 1u << 4,
    ReadoutMotorCenter      = 1u << 5,
    ReadoutAscendingLeft    = 1u << 6,
    ReadoutAscendingRight   = 1u << 7
};

#pragma pack(push, 1)
struct IOMapV1Header {
    char magic[8];               // "FAIO1\0\0\0"
    uint32_t version;            // 1
    uint32_t header_size;        // 64
    uint32_t neuron_count;
    uint32_t sensory_schema;     // 1
    uint32_t readout_schema;     // 1
    uint32_t reserved0;
    uint32_t reserved1;
    uint32_t reserved2;
    uint64_t sensory_mask_offset;
    uint64_t readout_mask_offset;
    uint64_t file_size;
};
#pragma pack(pop)

static_assert(sizeof(IOMapV1Header) == 64, "IOMapV1Header must remain 64 bytes");

struct IOMapV1 {
    uint32_t neuron_count = 0;
    std::vector<uint32_t> sensory_mask;
    std::vector<uint32_t> readout_mask;
};

bool load_io_map_v1(const std::string& path, IOMapV1& map, std::string& err);
const char* sensory_bit_name(uint32_t bit);
const char* readout_bit_name(uint32_t bit);

} // namespace flyarena
