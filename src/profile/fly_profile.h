#pragma once

#include "fly_equipment.h"

#include <cstdint>
#include <string>
#include <vector>

namespace flyarena {

constexpr uint32_t kFlypackFormatVersion = 4;
constexpr uint32_t kFlypackOldestReadableFormatVersion = 1;
constexpr const char* kFlypackSimCompatibility = "v0.6.5";
constexpr const char* kFlypackBancCompatibility = "v888/v3";
constexpr uint32_t kFlypackMaxBuiltInSkinId = 5;

struct FlyProfileIdentity {
    std::string name = "Unnamed Fly";
    std::string uuid;
    std::string author = "local";
    std::string lineage = "created";
};

struct FlyTrainingReference {
    bool present = false;
    // A leaf filename only. Importers never resolve an arbitrary path from a
    // flypack; the application chooses the trusted training directory.
    std::string checkpoint_file;
    std::string learner_version = "plastic-readout-v1";
    uint64_t training_steps = 0;
};

struct FlyLifetimeStatistics {
    uint64_t episodes = 0;
    uint64_t wins = 0;
    uint64_t losses = 0;
    uint64_t draws = 0;
};

struct FlyProfile {
    uint32_t format_version = kFlypackFormatVersion;
    std::string sim_compatibility = kFlypackSimCompatibility;
    std::string banc_compatibility = kFlypackBancCompatibility;
    FlyProfileIdentity identity;
    FlyEquipment equipment;
    FlyTrainingReference training;
    FlyLifetimeStatistics statistics;
};

// Validates strings and compatibility metadata, rejects non-finite values,
// and clamps finite equipment/skin values to the v1 sandbox limits.
bool validate_and_normalize_fly_profile(
    FlyProfile& profile,
    std::string& error,
    std::vector<std::string>* warnings = nullptr);

// Loading is transactional: out_profile is unchanged on every failure.
// Unknown fields are ignored so v1 optional additions remain forward-safe.
bool load_flypack(
    const std::string& path,
    FlyProfile& out_profile,
    std::string& error,
    std::vector<std::string>* warnings = nullptr);

bool save_flypack(
    const std::string& path,
    const FlyProfile& profile,
    std::string& error);

// Creates a deterministic, data-only Trainer loadout inside the same legal
// sandbox ranges accepted by flypack v1. The seed is recorded in the UUID so
// a generated opponent can be reproduced from telemetry.
FlyProfile make_random_trainer_profile(uint64_t seed);

} // namespace flyarena
