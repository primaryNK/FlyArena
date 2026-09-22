#pragma once

#include <cstdint>

namespace flyarena {

// V0.6.1 reserves this controller-neutral contract for the arena.
// A future BrainController and HumanController can both emit the same frame.
// No game action (attack/parry/dodge) is encoded here; those remain physical
// outcomes of body/weapon motion.
struct ArenaControlFrame {
    float forward = 0.0f;
    float lateral = 0.0f;
    float turn = 0.0f;
    float vertical = 0.0f;

    float left_foreleg = 0.0f;
    float right_foreleg = 0.0f;
    float wing_drive = 0.0f;

    // Independent low-level equipment actuators. These are NOT combat
    // outcomes such as attack/parry/dodge. sword_drive is muscle-like input
    // that may trigger a powered swing; shield_drive raises/holds the shield.
    // Physics decides HIT/BLOCK/PARRY afterwards.
    float sword_drive = 0.0f;
    float shield_drive = 0.0f;

    uint32_t source_flags = 0;
};

enum class ArenaControllerKind : uint32_t {
    Brain = 0,
    Human = 1
};

} // namespace flyarena
