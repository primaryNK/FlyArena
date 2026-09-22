#include "io_map.h"

#include <cstring>
#include <fstream>
#include <limits>

namespace flyarena {
namespace {

template <typename T>
bool read_array(
    std::ifstream& f,
    uint64_t offset,
    size_t count,
    std::vector<T>& out,
    std::string& err)
{
    if (count > (std::numeric_limits<size_t>::max)() / sizeof(T)) {
        err = "IO map array too large.";
        return false;
    }

    out.resize(count);
    f.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!f) {
        err = "IO map seek failed.";
        return false;
    }

    const auto bytes = static_cast<std::streamsize>(count * sizeof(T));
    f.read(reinterpret_cast<char*>(out.data()), bytes);
    if (!f) {
        err = "IO map array read failed.";
        return false;
    }
    return true;
}

} // namespace

bool load_io_map_v1(const std::string& path, IOMapV1& map, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "Could not open IO map: " + path;
        return false;
    }

    IOMapV1Header h{};
    f.read(reinterpret_cast<char*>(&h), sizeof(h));
    if (!f) {
        err = "Could not read IO map header.";
        return false;
    }

    const char expected[8] = {'F','A','I','O','1','\0','\0','\0'};
    if (std::memcmp(h.magic, expected, 8) != 0) {
        err = "Invalid IO map magic.";
        return false;
    }
    if (h.version != 1 || h.header_size != sizeof(IOMapV1Header)) {
        err = "Unsupported IO map version/header.";
        return false;
    }
    if (h.neuron_count == 0) {
        err = "IO map neuron count is zero.";
        return false;
    }

    f.seekg(0, std::ios::end);
    const uint64_t actual_size = static_cast<uint64_t>(f.tellg());
    if (h.file_size != actual_size) {
        err = "IO map file-size mismatch.";
        return false;
    }

    IOMapV1 tmp;
    tmp.neuron_count = h.neuron_count;
    if (!read_array(f, h.sensory_mask_offset, h.neuron_count, tmp.sensory_mask, err))
        return false;
    if (!read_array(f, h.readout_mask_offset, h.neuron_count, tmp.readout_mask, err))
        return false;

    map = std::move(tmp);
    return true;
}

const char* sensory_bit_name(uint32_t bit) {
    switch (bit) {
    case SensoryVisualLeft: return "visual_left";
    case SensoryVisualRight: return "visual_right";
    case SensoryBodyLeft: return "body_left";
    case SensoryBodyRight: return "body_right";
    case SensoryBodyCenter: return "body_center";
    default: return "unknown";
    }
}

const char* readout_bit_name(uint32_t bit) {
    switch (bit) {
    case ReadoutDescendingLeft: return "descending_left";
    case ReadoutDescendingRight: return "descending_right";
    case ReadoutDescendingCenter: return "descending_center";
    case ReadoutMotorLeft: return "motor_left";
    case ReadoutMotorRight: return "motor_right";
    case ReadoutMotorCenter: return "motor_center";
    case ReadoutAscendingLeft: return "ascending_left";
    case ReadoutAscendingRight: return "ascending_right";
    default: return "unknown";
    }
}

} // namespace flyarena
