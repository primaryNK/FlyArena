#include "fly_profile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace flyarena {
namespace {

constexpr const char* kMagicV1 = "FLYARENA_FLYPACK_V1";
constexpr const char* kMagicV2 = "FLYARENA_FLYPACK_V2";
constexpr const char* kMagicV3 = "FLYARENA_FLYPACK_V3";
constexpr const char* kMagicV4 = "FLYARENA_FLYPACK_V4";
constexpr uintmax_t kMaximumFileBytes = 64u * 1024u;
constexpr size_t kMaximumLineBytes = 4096;

bool safe_text(
    const std::string& text,
    size_t maximum_bytes,
    bool allow_empty)
{
    if ((!allow_empty && text.empty()) || text.size() > maximum_bytes)
        return false;

    for (unsigned char c : text) {
        if (c < 0x20 || c == 0x7f)
            return false;
    }
    return true;
}

bool safe_uuid(const std::string& value) {
    if (value.empty() || value.size() > 64)
        return false;
    for (unsigned char c : value) {
        const bool allowed =
            (c >= 'a' && c <= 'z')
            || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9')
            || c == '-' || c == '_';
        if (!allowed)
            return false;
    }
    return true;
}

bool safe_checkpoint_leaf(const std::string& value) {
    if (!safe_text(value, 128, false))
        return false;
    if (value.find('/') != std::string::npos
        || value.find('\\') != std::string::npos
        || value.find(':') != std::string::npos
        || value == "." || value == "..")
    {
        return false;
    }

    std::string lower = value;
    std::transform(
        lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    constexpr const char* extension = ".flytrain";
    return lower.size() > 9
        && lower.compare(lower.size() - 9, 9, extension) == 0;
}

template <typename T>
bool parse_scalar(std::istringstream& in, T& value) {
    if (!(in >> value))
        return false;
    in >> std::ws;
    return in.eof();
}

bool parse_quoted(std::istringstream& in, std::string& value) {
    if (!(in >> std::quoted(value)))
        return false;
    in >> std::ws;
    return in.eof();
}

template <typename T>
void clamp_with_warning(
    T& value,
    T minimum,
    T maximum,
    const char* field,
    std::vector<std::string>* warnings)
{
    const T original = value;
    value = std::clamp(value, minimum, maximum);
    if (warnings && value != original)
        warnings->push_back(std::string(field) + " was clamped to the v1 sandbox range");
}

bool mark_once(
    std::unordered_set<std::string>& seen,
    const std::string& key,
    std::string& error)
{
    if (seen.insert(key).second)
        return true;
    error = "Duplicate flypack field: " + key;
    return false;
}

uint64_t splitmix64(uint64_t& state) {
    uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

float random_range(uint64_t& state, float minimum, float maximum) {
    const uint32_t bits = static_cast<uint32_t>(splitmix64(state) >> 40);
    const float unit = static_cast<float>(bits) / 16777215.0f;
    return minimum + (maximum - minimum) * unit;
}

} // namespace

FlyProfile make_random_trainer_profile(uint64_t seed) {
    uint64_t state = seed ^ 0x545241494e4552ULL;

    FlyProfile profile;
    profile.identity.name = "Trainer";
    {
        std::ostringstream uuid;
        uuid << "trainer-" << std::hex << std::setw(16)
             << std::setfill('0') << seed;
        profile.identity.uuid = uuid.str();
    }
    profile.identity.author = "FlyArena";
    profile.identity.lineage = "random-trainer-v1";

    profile.equipment.body_skin_id =
        static_cast<uint32_t>(splitmix64(state) % 6ULL);
    profile.equipment.wings.skin_id =
        static_cast<uint32_t>(splitmix64(state) % 6ULL);
    profile.equipment.sword.skin_id =
        static_cast<uint32_t>(splitmix64(state) % 6ULL);
    profile.equipment.shield.skin_id =
        static_cast<uint32_t>(splitmix64(state) % 6ULL);

    profile.equipment.primary_color_rgb =
        static_cast<uint32_t>(splitmix64(state) & 0x00ffffffULL);
    profile.equipment.secondary_color_rgb =
        static_cast<uint32_t>(splitmix64(state) & 0x00ffffffULL);
    profile.equipment.wings.primary_color_rgb =
        static_cast<uint32_t>(splitmix64(state) & 0x00ffffffULL);
    profile.equipment.wings.secondary_color_rgb =
        static_cast<uint32_t>(splitmix64(state) & 0x00ffffffULL);
    profile.equipment.sword.primary_color_rgb =
        static_cast<uint32_t>(splitmix64(state) & 0x00ffffffULL);
    profile.equipment.sword.secondary_color_rgb =
        static_cast<uint32_t>(splitmix64(state) & 0x00ffffffULL);
    profile.equipment.shield.primary_color_rgb =
        static_cast<uint32_t>(splitmix64(state) & 0x00ffffffULL);
    profile.equipment.shield.secondary_color_rgb =
        static_cast<uint32_t>(splitmix64(state) & 0x00ffffffULL);

    profile.equipment.sword.length_scale =
        random_range(state, 0.60f, 1.80f);
    profile.equipment.sword.recovery_scale = 1.0f;
    profile.equipment.wings.size_scale =
        random_range(state, 0.65f, 1.60f);
    profile.equipment.wings.drive_speed_scale = 1.0f;
    profile.equipment.wings.stamina_cost_scale = 1.0f;
    profile.equipment.shield.mass_scale =
        random_range(state, 0.60f, 2.00f);
    profile.equipment.shield.size_scale = 1.0f;
    profile.equipment.shield.stamina_cost_scale = 1.0f;

    profile.training.present = true;
    profile.training.checkpoint_file = "Trainer_v065.flytrain";
    return profile;
}

bool validate_and_normalize_fly_profile(
    FlyProfile& profile,
    std::string& error,
    std::vector<std::string>* warnings)
{
    error.clear();
    if (warnings)
        warnings->clear();

    if (profile.format_version < kFlypackOldestReadableFormatVersion
        || profile.format_version > kFlypackFormatVersion)
    {
        error = "Unsupported flypack format version; readable versions are 1-4";
        return false;
    }
    if (profile.sim_compatibility != kFlypackSimCompatibility) {
        error = "Incompatible simulator version; required compatibility is v0.6.5";
        return false;
    }
    if (profile.banc_compatibility != kFlypackBancCompatibility) {
        error = "Incompatible BANC topology; required compatibility is v888/v3";
        return false;
    }
    if (!safe_text(profile.identity.name, 64, false)) {
        error = "Fly name is empty, too long, or contains control characters";
        return false;
    }
    if (!safe_uuid(profile.identity.uuid)) {
        error = "Fly UUID must be 1-64 ASCII letters, digits, '-' or '_'";
        return false;
    }
    if (!safe_text(profile.identity.author, 64, true)
        || !safe_text(profile.identity.lineage, 128, true))
    {
        error = "Fly author or lineage is too long or contains control characters";
        return false;
    }
    if (!safe_text(profile.training.learner_version, 64, false)) {
        error = "Learner version is invalid";
        return false;
    }
    if (profile.training.present
        && !safe_checkpoint_leaf(profile.training.checkpoint_file))
    {
        error = "Training reference must be a leaf .flytrain filename";
        return false;
    }
    if (!profile.training.present)
        profile.training.checkpoint_file.clear();

    float* finite_values[] = {
        &profile.equipment.sword.length_scale,
        &profile.equipment.sword.recovery_scale,
        &profile.equipment.sword.mass_scale,
        &profile.equipment.sword.thickness_scale,
        &profile.equipment.wings.size_scale,
        &profile.equipment.wings.drive_speed_scale,
        &profile.equipment.wings.stamina_cost_scale,
        &profile.equipment.shield.mass_scale,
        &profile.equipment.shield.size_scale,
        &profile.equipment.shield.stamina_cost_scale
    };
    for (const float* value : finite_values) {
        if (!std::isfinite(*value)) {
            error = "Flypack equipment contains NaN or infinity";
            return false;
        }
    }

    // Sword mass/thickness are not editable in flypack v1. Keep their legacy
    // fields at the canonical neutral value instead of exposing hidden physics.
    if (profile.equipment.sword.mass_scale != 1.0f && warnings)
        warnings->push_back("sword_mass_scale is fixed to 1.0 in flypack v1");
    if (profile.equipment.sword.thickness_scale != 1.0f && warnings)
        warnings->push_back("sword_thickness_scale is fixed to 1.0 in flypack v1");
    profile.equipment.sword.mass_scale = 1.0f;
    profile.equipment.sword.thickness_scale = 1.0f;

    clamp_with_warning(profile.equipment.sword.length_scale, 0.60f, 1.80f, "sword_length_scale", warnings);
    clamp_with_warning(profile.equipment.sword.recovery_scale, 0.60f, 1.80f, "sword_recovery_scale", warnings);
    clamp_with_warning(profile.equipment.wings.size_scale, 0.65f, 1.60f, "wing_size_scale", warnings);
    clamp_with_warning(profile.equipment.wings.drive_speed_scale, 0.65f, 1.60f, "wing_drive_speed_scale", warnings);
    clamp_with_warning(profile.equipment.wings.stamina_cost_scale, 0.60f, 1.80f, "wing_stamina_cost_scale", warnings);
    clamp_with_warning(profile.equipment.shield.mass_scale, 0.60f, 2.00f, "shield_mass_scale", warnings);
    clamp_with_warning(profile.equipment.shield.size_scale, 0.65f, 1.60f, "shield_size_scale", warnings);
    clamp_with_warning(profile.equipment.shield.stamina_cost_scale, 0.60f, 1.80f, "shield_stamina_cost_scale", warnings);
    clamp_with_warning(profile.equipment.body_skin_id, 0u, kFlypackMaxBuiltInSkinId, "body_skin_id", warnings);
    clamp_with_warning(profile.equipment.wings.skin_id, 0u, kFlypackMaxBuiltInSkinId, "wing_skin_id", warnings);
    clamp_with_warning(profile.equipment.sword.skin_id, 0u, kFlypackMaxBuiltInSkinId, "sword_skin_id", warnings);
    clamp_with_warning(profile.equipment.shield.skin_id, 0u, kFlypackMaxBuiltInSkinId, "shield_skin_id", warnings);
    clamp_with_warning(profile.equipment.primary_color_rgb, 0u, 0x00ffffffu, "primary_color_rgb", warnings);
    clamp_with_warning(profile.equipment.secondary_color_rgb, 0u, 0x00ffffffu, "secondary_color_rgb", warnings);
    clamp_with_warning(profile.equipment.wings.primary_color_rgb, 0u, 0x00ffffffu, "wing_primary_color_rgb", warnings);
    clamp_with_warning(profile.equipment.wings.secondary_color_rgb, 0u, 0x00ffffffu, "wing_secondary_color_rgb", warnings);
    clamp_with_warning(profile.equipment.sword.primary_color_rgb, 0u, 0x00ffffffu, "sword_primary_color_rgb", warnings);
    clamp_with_warning(profile.equipment.sword.secondary_color_rgb, 0u, 0x00ffffffu, "sword_secondary_color_rgb", warnings);
    clamp_with_warning(profile.equipment.shield.primary_color_rgb, 0u, 0x00ffffffu, "shield_primary_color_rgb", warnings);
    clamp_with_warning(profile.equipment.shield.secondary_color_rgb, 0u, 0x00ffffffu, "shield_secondary_color_rgb", warnings);

    if (profile.format_version >= 3) {
        auto link_to_neutral = [&](float& value, const char* field) {
            if (value != 1.0f && warnings)
                warnings->push_back(std::string(field)
                    + " is derived from the v3 three-part loadout and was reset to 1.0");
            value = 1.0f;
        };
        link_to_neutral(profile.equipment.sword.recovery_scale,
            "sword_recovery_scale");
        link_to_neutral(profile.equipment.wings.drive_speed_scale,
            "wing_drive_speed_scale");
        link_to_neutral(profile.equipment.wings.stamina_cost_scale,
            "wing_stamina_cost_scale");
        link_to_neutral(profile.equipment.shield.size_scale,
            "shield_size_scale");
        link_to_neutral(profile.equipment.shield.stamina_cost_scale,
            "shield_stamina_cost_scale");
    }

    return true;
}

bool load_flypack(
    const std::string& path,
    FlyProfile& out_profile,
    std::string& error,
    std::vector<std::string>* warnings)
{
    error.clear();
    if (warnings)
        warnings->clear();

    try {
        const fs::path input_path(path);
        if (!fs::exists(input_path)) {
            error = "Flypack does not exist: " + path;
            return false;
        }
        if (!fs::is_regular_file(input_path)
            || fs::file_size(input_path) > kMaximumFileBytes)
        {
            error = "Flypack is not a regular file or exceeds 64 KiB: " + path;
            return false;
        }

        std::ifstream in(input_path, std::ios::binary);
        if (!in) {
            error = "Could not open flypack: " + path;
            return false;
        }

        std::string line;
        if (!std::getline(in, line)) {
            error = "Unsupported or damaged flypack: " + path;
            return false;
        }
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        uint32_t magic_version = 0;
        if (line == kMagicV1)
            magic_version = 1;
        else if (line == kMagicV2)
            magic_version = 2;
        else if (line == kMagicV3)
            magic_version = 3;
        else if (line == kMagicV4)
            magic_version = 4;
        else {
            error = "Unsupported or damaged flypack: " + path;
            return false;
        }

        FlyProfile candidate;
        std::unordered_set<std::string> seen;

        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            if (line.size() > kMaximumLineBytes) {
                error = "Flypack line exceeds 4096 bytes";
                return false;
            }

            std::istringstream row(line);
            std::string key;
            row >> key;
            if (key.empty())
                continue;

            const bool known =
                key == "format_version"
                || key == "sim_compatibility"
                || key == "banc_compatibility"
                || key == "identity.name"
                || key == "identity.uuid"
                || key == "identity.author"
                || key == "identity.lineage"
                || key == "cosmetics.body_skin_id"
                || key == "cosmetics.wing_skin_id"
                || key == "cosmetics.sword_skin_id"
                || key == "cosmetics.shield_skin_id"
                || key == "cosmetics.primary_color_rgb"
                || key == "cosmetics.secondary_color_rgb"
                || key == "cosmetics.body_primary_color_rgb"
                || key == "cosmetics.body_secondary_color_rgb"
                || key == "cosmetics.wing_primary_color_rgb"
                || key == "cosmetics.wing_secondary_color_rgb"
                || key == "cosmetics.sword_primary_color_rgb"
                || key == "cosmetics.sword_secondary_color_rgb"
                || key == "cosmetics.shield_primary_color_rgb"
                || key == "cosmetics.shield_secondary_color_rgb"
                || key == "equipment.sword_length_scale"
                || key == "equipment.sword_recovery_scale"
                || key == "equipment.wing_size_scale"
                || key == "equipment.wing_drive_speed_scale"
                || key == "equipment.wing_stamina_cost_scale"
                || key == "equipment.shield_mass_scale"
                || key == "equipment.shield_size_scale"
                || key == "equipment.shield_stamina_cost_scale"
                || key == "training.present"
                || key == "training.checkpoint_file"
                || key == "training.learner_version"
                || key == "training.training_steps"
                || key == "statistics.episodes"
                || key == "statistics.wins"
                || key == "statistics.losses"
                || key == "statistics.draws";

            if (!known)
                continue;
            if (!mark_once(seen, key, error))
                return false;

            bool parsed = false;
            if (key == "format_version") parsed = parse_scalar(row, candidate.format_version);
            else if (key == "sim_compatibility") parsed = parse_quoted(row, candidate.sim_compatibility);
            else if (key == "banc_compatibility") parsed = parse_quoted(row, candidate.banc_compatibility);
            else if (key == "identity.name") parsed = parse_quoted(row, candidate.identity.name);
            else if (key == "identity.uuid") parsed = parse_quoted(row, candidate.identity.uuid);
            else if (key == "identity.author") parsed = parse_quoted(row, candidate.identity.author);
            else if (key == "identity.lineage") parsed = parse_quoted(row, candidate.identity.lineage);
            else if (key == "cosmetics.body_skin_id") parsed = parse_scalar(row, candidate.equipment.body_skin_id);
            else if (key == "cosmetics.wing_skin_id") parsed = parse_scalar(row, candidate.equipment.wings.skin_id);
            else if (key == "cosmetics.sword_skin_id") parsed = parse_scalar(row, candidate.equipment.sword.skin_id);
            else if (key == "cosmetics.shield_skin_id") parsed = parse_scalar(row, candidate.equipment.shield.skin_id);
            else if (key == "cosmetics.primary_color_rgb") parsed = parse_scalar(row, candidate.equipment.primary_color_rgb);
            else if (key == "cosmetics.secondary_color_rgb") parsed = parse_scalar(row, candidate.equipment.secondary_color_rgb);
            else if (key == "cosmetics.body_primary_color_rgb") parsed = parse_scalar(row, candidate.equipment.primary_color_rgb);
            else if (key == "cosmetics.body_secondary_color_rgb") parsed = parse_scalar(row, candidate.equipment.secondary_color_rgb);
            else if (key == "cosmetics.wing_primary_color_rgb") parsed = parse_scalar(row, candidate.equipment.wings.primary_color_rgb);
            else if (key == "cosmetics.wing_secondary_color_rgb") parsed = parse_scalar(row, candidate.equipment.wings.secondary_color_rgb);
            else if (key == "cosmetics.sword_primary_color_rgb") parsed = parse_scalar(row, candidate.equipment.sword.primary_color_rgb);
            else if (key == "cosmetics.sword_secondary_color_rgb") parsed = parse_scalar(row, candidate.equipment.sword.secondary_color_rgb);
            else if (key == "cosmetics.shield_primary_color_rgb") parsed = parse_scalar(row, candidate.equipment.shield.primary_color_rgb);
            else if (key == "cosmetics.shield_secondary_color_rgb") parsed = parse_scalar(row, candidate.equipment.shield.secondary_color_rgb);
            else if (key == "equipment.sword_length_scale") parsed = parse_scalar(row, candidate.equipment.sword.length_scale);
            else if (key == "equipment.sword_recovery_scale") parsed = parse_scalar(row, candidate.equipment.sword.recovery_scale);
            else if (key == "equipment.wing_size_scale") parsed = parse_scalar(row, candidate.equipment.wings.size_scale);
            else if (key == "equipment.wing_drive_speed_scale") parsed = parse_scalar(row, candidate.equipment.wings.drive_speed_scale);
            else if (key == "equipment.wing_stamina_cost_scale") parsed = parse_scalar(row, candidate.equipment.wings.stamina_cost_scale);
            else if (key == "equipment.shield_mass_scale") parsed = parse_scalar(row, candidate.equipment.shield.mass_scale);
            else if (key == "equipment.shield_size_scale") parsed = parse_scalar(row, candidate.equipment.shield.size_scale);
            else if (key == "equipment.shield_stamina_cost_scale") parsed = parse_scalar(row, candidate.equipment.shield.stamina_cost_scale);
            else if (key == "training.present") { int value = 0; parsed = parse_scalar(row, value) && (value == 0 || value == 1); candidate.training.present = value != 0; }
            else if (key == "training.checkpoint_file") parsed = parse_quoted(row, candidate.training.checkpoint_file);
            else if (key == "training.learner_version") parsed = parse_quoted(row, candidate.training.learner_version);
            else if (key == "training.training_steps") parsed = parse_scalar(row, candidate.training.training_steps);
            else if (key == "statistics.episodes") parsed = parse_scalar(row, candidate.statistics.episodes);
            else if (key == "statistics.wins") parsed = parse_scalar(row, candidate.statistics.wins);
            else if (key == "statistics.losses") parsed = parse_scalar(row, candidate.statistics.losses);
            else if (key == "statistics.draws") parsed = parse_scalar(row, candidate.statistics.draws);

            if (!parsed) {
                error = "Invalid value for flypack field: " + key;
                return false;
            }
        }

        const char* required[] = {
            "format_version", "sim_compatibility", "banc_compatibility",
            "identity.name", "identity.uuid", "identity.author", "identity.lineage",
            "cosmetics.body_skin_id", "cosmetics.wing_skin_id",
            "cosmetics.sword_skin_id", "cosmetics.shield_skin_id",
            "equipment.sword_length_scale", "equipment.wing_size_scale",
            "equipment.shield_mass_scale", "equipment.shield_size_scale"
        };
        for (const char* field : required) {
            if (!seen.contains(field)) {
                error = std::string("Missing required flypack field: ") + field;
                return false;
            }
        }

        if (candidate.format_version != magic_version) {
            error = "Flypack magic/version mismatch";
            return false;
        }
        if (candidate.format_version >= 2) {
            const char* v2_required[] = {
                "equipment.sword_recovery_scale",
                "equipment.wing_drive_speed_scale",
                "equipment.wing_stamina_cost_scale",
                "equipment.shield_stamina_cost_scale"
            };
            for (const char* field : v2_required) {
                if (!seen.contains(field)) {
                    error = std::string(
                        "Missing required flypack v2 field: ") + field;
                    return false;
                }
            }
        }
        if (candidate.format_version == 3) {
            const char* v3_required[] = {
                "cosmetics.primary_color_rgb",
                "cosmetics.secondary_color_rgb"
            };
            for (const char* field : v3_required) {
                if (!seen.contains(field)) {
                    error = std::string(
                        "Missing required flypack v3 field: ") + field;
                    return false;
                }
            }
        }
        else if (candidate.format_version < 3) {
            // Legacy packages had palette implied by the body skin. Preserve
            // that appearance while materializing explicit v3 endpoints.
            if (candidate.equipment.body_skin_id == 1) {
                candidate.equipment.primary_color_rgb = 0xEF333Bu;
                candidate.equipment.secondary_color_rgb = 0xFF6470u;
            } else if (candidate.equipment.body_skin_id == 2) {
                candidate.equipment.primary_color_rgb = 0x3380FFu;
                candidate.equipment.secondary_color_rgb = 0x54A6FFu;
            }
        }

        if (candidate.format_version < 4) {
            // V1-V3 had one shared gradient. Preserve their appearance by
            // materializing that same pair for every v4 component.
            candidate.equipment.wings.primary_color_rgb =
                candidate.equipment.primary_color_rgb;
            candidate.equipment.wings.secondary_color_rgb =
                candidate.equipment.secondary_color_rgb;
            candidate.equipment.sword.primary_color_rgb =
                candidate.equipment.primary_color_rgb;
            candidate.equipment.sword.secondary_color_rgb =
                candidate.equipment.secondary_color_rgb;
            candidate.equipment.shield.primary_color_rgb =
                candidate.equipment.primary_color_rgb;
            candidate.equipment.shield.secondary_color_rgb =
                candidate.equipment.secondary_color_rgb;
        } else {
            const char* v4_required[] = {
                "cosmetics.body_primary_color_rgb",
                "cosmetics.body_secondary_color_rgb",
                "cosmetics.wing_primary_color_rgb",
                "cosmetics.wing_secondary_color_rgb",
                "cosmetics.sword_primary_color_rgb",
                "cosmetics.sword_secondary_color_rgb",
                "cosmetics.shield_primary_color_rgb",
                "cosmetics.shield_secondary_color_rgb"
            };
            for (const char* field : v4_required) {
                if (!seen.contains(field)) {
                    error = std::string(
                        "Missing required flypack v4 field: ") + field;
                    return false;
                }
            }
        }

        if (!validate_and_normalize_fly_profile(candidate, error, warnings))
            return false;

        out_profile = std::move(candidate);
        return true;
    }
    catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool save_flypack(
    const std::string& path,
    const FlyProfile& profile,
    std::string& error)
{
    error.clear();
    try {
        FlyProfile normalized = profile;
        if (!validate_and_normalize_fly_profile(normalized, error))
            return false;

        const fs::path output_path(path);
        if (output_path.has_parent_path())
            fs::create_directories(output_path.parent_path());

        std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "Could not open flypack for writing: " + path;
            return false;
        }

        out << (normalized.format_version >= 4 ? kMagicV4
                : normalized.format_version >= 3 ? kMagicV3
                : normalized.format_version >= 2 ? kMagicV2 : kMagicV1) << '\n'
            << "format_version " << normalized.format_version << '\n'
            << "sim_compatibility " << std::quoted(normalized.sim_compatibility) << '\n'
            << "banc_compatibility " << std::quoted(normalized.banc_compatibility) << '\n'
            << "identity.name " << std::quoted(normalized.identity.name) << '\n'
            << "identity.uuid " << std::quoted(normalized.identity.uuid) << '\n'
            << "identity.author " << std::quoted(normalized.identity.author) << '\n'
            << "identity.lineage " << std::quoted(normalized.identity.lineage) << '\n'
            << "cosmetics.body_skin_id " << normalized.equipment.body_skin_id << '\n'
            << "cosmetics.wing_skin_id " << normalized.equipment.wings.skin_id << '\n'
            << "cosmetics.sword_skin_id " << normalized.equipment.sword.skin_id << '\n'
            << "cosmetics.shield_skin_id " << normalized.equipment.shield.skin_id << '\n'
            ;
        if (normalized.format_version == 3) {
            out << "cosmetics.primary_color_rgb " << normalized.equipment.primary_color_rgb << '\n'
                << "cosmetics.secondary_color_rgb " << normalized.equipment.secondary_color_rgb << '\n';
        }
        if (normalized.format_version >= 4) {
            out << "cosmetics.body_primary_color_rgb " << normalized.equipment.primary_color_rgb << '\n'
                << "cosmetics.body_secondary_color_rgb " << normalized.equipment.secondary_color_rgb << '\n'
                << "cosmetics.wing_primary_color_rgb " << normalized.equipment.wings.primary_color_rgb << '\n'
                << "cosmetics.wing_secondary_color_rgb " << normalized.equipment.wings.secondary_color_rgb << '\n'
                << "cosmetics.sword_primary_color_rgb " << normalized.equipment.sword.primary_color_rgb << '\n'
                << "cosmetics.sword_secondary_color_rgb " << normalized.equipment.sword.secondary_color_rgb << '\n'
                << "cosmetics.shield_primary_color_rgb " << normalized.equipment.shield.primary_color_rgb << '\n'
                << "cosmetics.shield_secondary_color_rgb " << normalized.equipment.shield.secondary_color_rgb << '\n';
        }
        out
            << std::setprecision(std::numeric_limits<float>::max_digits10)
            << "equipment.sword_length_scale " << normalized.equipment.sword.length_scale << '\n'
            ;
        if (normalized.format_version >= 2) {
            out << "equipment.sword_recovery_scale " << normalized.equipment.sword.recovery_scale << '\n';
        }
        out
            << "equipment.wing_size_scale " << normalized.equipment.wings.size_scale << '\n'
            ;
        if (normalized.format_version >= 2) {
            out << "equipment.wing_drive_speed_scale " << normalized.equipment.wings.drive_speed_scale << '\n'
                << "equipment.wing_stamina_cost_scale " << normalized.equipment.wings.stamina_cost_scale << '\n';
        }
        out
            << "equipment.shield_mass_scale " << normalized.equipment.shield.mass_scale << '\n'
            << "equipment.shield_size_scale " << normalized.equipment.shield.size_scale << '\n'
            ;
        if (normalized.format_version >= 2) {
            out << "equipment.shield_stamina_cost_scale " << normalized.equipment.shield.stamina_cost_scale << '\n';
        }
        out
            << "training.present " << (normalized.training.present ? 1 : 0) << '\n'
            << "training.checkpoint_file " << std::quoted(normalized.training.checkpoint_file) << '\n'
            << "training.learner_version " << std::quoted(normalized.training.learner_version) << '\n'
            << "training.training_steps " << normalized.training.training_steps << '\n'
            << "statistics.episodes " << normalized.statistics.episodes << '\n'
            << "statistics.wins " << normalized.statistics.wins << '\n'
            << "statistics.losses " << normalized.statistics.losses << '\n'
            << "statistics.draws " << normalized.statistics.draws << '\n';

        if (!out) {
            error = "Failed while writing flypack: " + path;
            return false;
        }
        return true;
    }
    catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace flyarena
