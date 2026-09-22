#pragma once
#include <cstdint>
#include <string>
namespace flyarena {
enum class CombatEventType : uint32_t { Hit=0, Block=1, Parry=2, Dodge=3 };
struct CombatEvent {
    CombatEventType type=CombatEventType::Hit;
    double sim_time_ms=0.0;
    uint64_t event_id=0;
    std::string actor_name,target_name;
    float world_x=0.0f, world_y=0.0f;
    float direction=0.0f;
    int32_t damage=0;
    float stun_seconds=0.0f;
    float intensity=0.0f;
};
inline const char* combat_event_label(CombatEventType t){
    switch(t){case CombatEventType::Hit:return "HIT!";case CombatEventType::Block:return "BLOCK!";case CombatEventType::Parry:return "PARRY!";case CombatEventType::Dodge:return "DODGE!";default:return "EVENT!";}
}
} // namespace flyarena
