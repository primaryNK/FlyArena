#pragma once

#include "render_snapshot.h"
#include "runtime_tuning.h"
#include "combat_event.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <string>

namespace flyarena {

struct RendererConfig {
    int width = 1280;
    int height = 720;
    int target_fps = 120; // 0 = uncapped
    float arena_radius_world = 0.90f;
};

class ArenaRenderer {
public:
    ArenaRenderer(
        SnapshotBuffer& snapshots,
        RuntimeTuning& tuning,
        const RendererConfig& config);

    ~ArenaRenderer();

    ArenaRenderer(const ArenaRenderer&) = delete;
    ArenaRenderer& operator=(const ArenaRenderer&) = delete;

    int run(HINSTANCE instance, int show_cmd);

private:
    struct Impl;
    Impl* impl_;
};

} // namespace flyarena
