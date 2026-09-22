#pragma once

#include <cstdint>

namespace flyarena {

enum class MatchResult : int32_t {
    Live = 0,
    Red = 1,
    Blue = 2,
    Draw = 3
};

inline bool match_time_expired(
    double elapsed_ms,
    double duration_ms)
{
    return duration_ms > 0.0 && elapsed_ms >= duration_ms;
}

inline bool match_has_ko(int32_t red_hp, int32_t blue_hp) {
    return red_hp <= 0 || blue_hp <= 0;
}

inline MatchResult classify_match_result(
    int32_t red_hp,
    int32_t blue_hp)
{
    if (red_hp > blue_hp)
        return MatchResult::Red;
    if (blue_hp > red_hp)
        return MatchResult::Blue;
    return MatchResult::Draw;
}

} // namespace flyarena
