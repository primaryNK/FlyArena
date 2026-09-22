#pragma once

#include <algorithm>

namespace flyarena {

inline double gpu_pacing_sleep_ms(
    double gpu_active_wall_ms,
    double simulated_ms,
    double realtime_multiplier,
    double duty_target,
    bool unlimited)
{
    if (unlimited || realtime_multiplier <= 0.0)
        return 0.0;

    const double active_ms = std::max(0.0, gpu_active_wall_ms);
    const double duty = std::clamp(duty_target, 0.05, 1.0);
    const double realtime_target_ms =
        std::max(0.0, simulated_ms) / realtime_multiplier;
    const double duty_target_ms = active_ms / duty;
    return std::max(
        0.0,
        std::max(realtime_target_ms, duty_target_ms) - active_ms);
}

} // namespace flyarena
