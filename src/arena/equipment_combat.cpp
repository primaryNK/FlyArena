#include "equipment_combat.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace flyarena { namespace {
constexpr float kPi=3.14159265358979323846f;
float len(Vec2 v){return std::sqrt(v.x*v.x+v.y*v.y);} float dot(Vec2 a,Vec2 b){return a.x*b.x+a.y*b.y;} float cross(Vec2 a,Vec2 b){return a.x*b.y-a.y*b.x;}
Vec2 add(Vec2 a,Vec2 b){return {a.x+b.x,a.y+b.y};} Vec2 sub(Vec2 a,Vec2 b){return {a.x-b.x,a.y-b.y};} Vec2 mul(Vec2 a,float s){return {a.x*s,a.y*s};} Vec2 dir(float a){return {std::cos(a),std::sin(a)};} Vec2 perp(Vec2 v){return {-v.y,v.x};}
struct Closest{Vec2 p{};float t=0,d=0;}; Closest closest(Vec2 a,Vec2 b,Vec2 p){Vec2 ab=sub(b,a);float den=std::max(1e-8f,dot(ab,ab));float t=std::clamp(dot(sub(p,a),ab)/den,0.0f,1.0f);Vec2 q=add(a,mul(ab,t));return {q,t,len(sub(p,q))};}
struct SegmentPair{Vec2 sword{},target{};float d=0;};
SegmentPair closest_segments(Vec2 p1,Vec2 q1,Vec2 p2,Vec2 q2){
 Vec2 d1=sub(q1,p1),d2=sub(q2,p2),r=sub(p1,p2);
 float a=dot(d1,d1),e=dot(d2,d2),f=dot(d2,r),s=0,t=0;
 if(a<=1e-8f&&e<=1e-8f)return {p1,p2,len(sub(p1,p2))};
 if(a<=1e-8f){t=std::clamp(f/e,0.0f,1.0f);}
 else {float c=dot(d1,r);if(e<=1e-8f)s=std::clamp(-c/a,0.0f,1.0f);else{float b=dot(d1,d2),den=a*e-b*b;if(den!=0)s=std::clamp((b*f-c*e)/den,0.0f,1.0f);t=(b*s+f)/e;if(t<0){t=0;s=std::clamp(-c/a,0.0f,1.0f);}else if(t>1){t=1;s=std::clamp((b-c)/a,0.0f,1.0f);}}}
 Vec2 ps=add(p1,mul(d1,s)),pt=add(p2,mul(d2,t));return {ps,pt,len(sub(ps,pt))};
}
struct G{Vec2 pivot,tip,sdir,tipv,shield,shieldv;float sr=0;};
G geom_at_sword_angle(const FlyBodyState& b,float br,float sword_relative_angle){
 G g;Vec2 f=dir(b.heading_rad),right=dir(b.heading_rad-kPi*.5f);
 float sa=b.heading_rad+sword_relative_angle;g.sdir=dir(sa);
 g.pivot=add(b.position,add(mul(f,br*.30f),mul(right,br*.45f)));
 g.tip=add(g.pivot,mul(g.sdir,sword_length_world(b.equipment)));
 Vec2 rt=sub(g.tip,b.position);
 g.tipv=add(b.velocity,mul(perp(rt),b.angular_velocity+b.sword_angular_velocity));
 float ha=b.heading_rad+b.shield_relative_angle;Vec2 hd=dir(ha);
 g.sr=shield_radius_world(b.equipment);
 float arm=shield_center_distance_world(b.equipment,br,b.shield_deploy);
 g.shield=add(b.position,mul(hd,arm));Vec2 rs=sub(g.shield,b.position);
 Vec2 angular=mul(perp(rs),b.angular_velocity+b.shield_angular_velocity);
 float radial_speed=shield_center_deploy_derivative_world(
     b.equipment,br,b.shield_deploy)*b.shield_deploy_velocity;
 g.shieldv=add(add(b.velocity,angular),mul(hd,radial_speed));return g;}
G geom(const FlyBodyState& b,float br){
 return geom_at_sword_angle(b,br,b.sword_relative_angle);
}
struct SweepContact{Closest closest{};G geometry{};};
SweepContact swept_sword_circle(
    const FlyBodyState& b,float br,Vec2 center)
{
 const float delta=wrap_angle(
     b.sword_relative_angle-b.sword_previous_relative_angle);
 const int samples=std::clamp(
     static_cast<int>(std::ceil(std::fabs(delta)/0.035f))+1,2,24);
 SweepContact best;
 best.closest.d=std::numeric_limits<float>::max();
 for(int i=0;i<samples;++i){
   const float t=samples>1?static_cast<float>(i)/static_cast<float>(samples-1):1.0f;
   const float angle=b.sword_previous_relative_angle+delta*t;
   G candidate=geom_at_sword_angle(b,br,angle);
   Closest contact=closest(candidate.pivot,candidate.tip,center);
   if(contact.d<best.closest.d){best.closest=contact;best.geometry=candidate;}
 }
 return best;
}
struct WingCapsule{Vec2 root{},tip{};float radius=0;};
WingCapsule wing_capsule(const FlyBodyState& b,float br,float side){
 const float scale=wing_size_scale(b.equipment);
 const Vec2 forward=dir(b.heading_rad),right=dir(b.heading_rad-kPi*.5f);
 auto point=[&](float longitudinal,float lateral){return add(b.position,add(mul(forward,longitudinal*br*scale),mul(right,lateral*side*br*scale)));};
 return {point(kWingHitRootForwardBodyRadii,kWingHitRootLateralBodyRadii),point(kWingHitTipForwardBodyRadii,kWingHitTipLateralBodyRadii),kWingHitRadiusBodyRadii*br*scale};
}
struct TargetContact{Vec2 sword_point{},target_point{};G geometry{};float surface_distance=std::numeric_limits<float>::max();};
TargetContact swept_sword_fly(const FlyBodyState& a,const FlyBodyState& d,float br){
 const float delta=wrap_angle(a.sword_relative_angle-a.sword_previous_relative_angle);
 const int samples=std::clamp(static_cast<int>(std::ceil(std::fabs(delta)/0.035f))+1,2,24);
 const WingCapsule wings[2]={wing_capsule(d,br,-1.0f),wing_capsule(d,br,1.0f)};
 TargetContact best;
 for(int i=0;i<samples;++i){
   const float t=samples>1?static_cast<float>(i)/static_cast<float>(samples-1):1.0f;
   G candidate=geom_at_sword_angle(a,br,a.sword_previous_relative_angle+delta*t);
   const Closest body=closest(candidate.pivot,candidate.tip,d.position);
   const float body_surface=body.d-br;
   if(body_surface<best.surface_distance){best={body.p,d.position,candidate,body_surface};}
   for(const WingCapsule& wing:wings){
     const SegmentPair pair=closest_segments(candidate.pivot,candidate.tip,wing.root,wing.tip);
     const float wing_surface=pair.d-wing.radius;
     if(wing_surface<best.surface_distance){best={pair.sword,pair.target,candidate,wing_surface};}
   }
 }
 return best;
}
void evt(std::vector<CombatEvent>& o,CombatEventType t,FlyBodyState& a,FlyBodyState& b,Vec2 p,double ms,uint64_t& id,int dmg,float stun,float intensity,float direction){CombatEvent e;e.type=t;e.sim_time_ms=ms;e.event_id=id++;e.actor_name=a.identity.name;e.target_name=b.identity.name;e.world_x=p.x;e.world_y=p.y;e.damage=dmg;e.stun_seconds=stun;e.intensity=intensity;e.direction=direction;o.push_back(e);}
} // anon

void step_equipment(
    FlyBodyState& b,
    const ArenaControlFrame& c,
    const CombatConfig& cfg,
    float dt)
{
    dt = std::max(0.0f, dt);

    const float stun_scale =
        b.stun_remaining_s > 0.0f ? 0.35f : 1.0f;

    const float sword_drive =
        std::clamp(c.sword_drive, 0.0f, 1.0f);

    const float shield_drive =
        std::clamp(c.shield_drive, 0.0f, 1.0f);

    // -------------------------------------------------------------
    // SWORD
    // -------------------------------------------------------------
    // No inertia simulation. A neural rising edge launches one fast,
    // deterministic slash. Sword LENGTH controls only the recovery time:
    // longer reach -> longer time before another slash can be armed.
    if (sword_drive <= cfg.sword_rearm_threshold)
        b.sword_trigger_armed = true;

    const bool sword_rising =
        b.sword_trigger_armed
        && b.sword_phase == SwordPhase::Ready
        && sword_drive >= cfg.sword_trigger_threshold
        && b.previous_sword_drive < cfg.sword_trigger_threshold;

    if (sword_rising) {
        b.sword_phase = SwordPhase::Swing;
        b.sword_phase_time_s = 0.0f;
        b.sword_relative_angle = cfg.sword_ready_angle;
        b.sword_recovery_start_angle = cfg.sword_ready_angle;
        b.sword_angular_velocity = 0.0f;
        b.sword_trigger_armed = false;
        ++b.sword_swings;
    }

    b.previous_sword_drive = sword_drive;

    const float old_sword_angle =
        b.sword_relative_angle;
    const SwordPhase sword_phase_during_step = b.sword_phase;
    b.sword_previous_relative_angle = old_sword_angle;
    b.sword_hit_active_this_step = false;

    if (b.sword_phase == SwordPhase::Ready) {
        b.sword_phase_time_s = 0.0f;
        b.sword_relative_angle = cfg.sword_ready_angle;
        b.sword_angular_velocity = 0.0f;
    }
    else if (b.sword_phase == SwordPhase::Swing) {
        b.sword_phase_time_s += dt * stun_scale;

        const float t =
            std::clamp(
                b.sword_phase_time_s
                / std::max(0.001f, cfg.sword_swing_duration_s),
                0.0f, 1.0f);

        // Smoothstep gives a fast visual slash without simulated inertia.
        const float eased =
            t * t * (3.0f - 2.0f * t);

        b.sword_relative_angle =
            cfg.sword_ready_angle
            + (cfg.sword_strike_angle - cfg.sword_ready_angle)
              * eased;

        b.sword_angular_velocity =
            dt > 1e-6f
            ? (b.sword_relative_angle - old_sword_angle) / dt
            : 0.0f;

        if (t >= 1.0f) {
            b.sword_phase = SwordPhase::Recovery;
            b.sword_phase_time_s = 0.0f;
            b.sword_recovery_start_angle =
                b.sword_relative_angle;
        }
    }

    else { // Recovery
        b.sword_phase_time_s += dt * stun_scale;

        const float length_ratio =
            sword_length_world(b.equipment)
            / kBaseSwordLengthWorld;

        const float recovery_s =
            cfg.sword_recovery_base_s
            * std::clamp(length_ratio, 0.60f, 1.80f)
            * std::clamp(
                b.equipment.sword.recovery_scale, 0.60f, 1.80f);

        const float t =
            std::clamp(
                b.sword_phase_time_s
                / std::max(0.001f, recovery_s),
                0.0f, 1.0f);

        const float eased =
            t * t * (3.0f - 2.0f * t);

        b.sword_relative_angle =
            b.sword_recovery_start_angle
            + (cfg.sword_ready_angle
               - b.sword_recovery_start_angle)
              * eased;

        b.sword_angular_velocity =
            dt > 1e-6f
            ? (b.sword_relative_angle - old_sword_angle) / dt
            : 0.0f;

        if (t >= 1.0f) {
            b.sword_phase = SwordPhase::Ready;
            b.sword_phase_time_s = 0.0f;
            b.sword_relative_angle = cfg.sword_ready_angle;
            b.sword_angular_velocity = 0.0f;
        }
    }

    // Collision is active for every physical substep that advanced the swing,
    // including the final substep that transitions the state to Recovery.
    // This prevents the last part of the blade arc from losing hit validity.
    b.sword_hit_active_this_step =
        sword_phase_during_step == SwordPhase::Swing
        && std::fabs(b.sword_angular_velocity)
            >= cfg.sword_min_hit_angular_speed;

    // -------------------------------------------------------------
    // SHIELD
    // -------------------------------------------------------------
    // Raising/holding a block is independent from sword. Shield MASS affects
    // stamina drain while held; it does not change wall-damage immunity
    // because there is no wall immunity.
    if (shield_drive <= cfg.shield_rearm_threshold)
        b.shield_trigger_armed = true;

    const bool request_raise =
        b.shield_trigger_armed
        && !b.shield_raised
        && shield_drive >= cfg.shield_raise_threshold
        && b.previous_shield_drive < cfg.shield_raise_threshold
        && b.stamina >= cfg.shield_min_stamina_to_raise;

    if (request_raise) {
        b.shield_raised = true;
        b.shield_trigger_armed = false;
        b.parry_window_remaining_s =
            cfg.parry_window_s;
        ++b.shield_raises;
    }

    if (b.shield_raised) {
        const float mass_ratio =
            shield_mass(b.equipment)
            / kBaseShieldMass;

        const float drain =
            cfg.shield_hold_stamina_drain_per_s
            * std::clamp(mass_ratio, 0.60f, 2.00f)
            * std::clamp(
                b.equipment.shield.stamina_cost_scale,
                0.60f, 1.80f)
            * dt;

        const float actual_stamina_delta = apply_stamina_delta(
            b,
            -drain);
        b.stamina_spent_shield_points +=
            std::max(0.0f, -actual_stamina_delta);

        const bool released =
            shield_drive < cfg.shield_hold_threshold;

        const bool exhausted =
            b.stamina <= 0;

        if (released || exhausted) {
            b.shield_raised = false;
            b.parry_window_remaining_s = 0.0f;

            // Exhaustion requires the neural drive to fall before block can
            // be raised again; this prevents zero-stamina flickering.
            if (exhausted)
                b.shield_trigger_armed = false;
        }
    }

    b.previous_shield_drive =
        shield_drive;

    const float old_shield_deploy = b.shield_deploy;
    const float deploy_target = b.shield_raised ? 1.0f : 0.0f;
    const float deploy_duration = b.shield_raised
        ? cfg.shield_raise_duration_s
        : cfg.shield_lower_duration_s;
    const float deploy_step =
        dt / std::max(0.001f, deploy_duration);
    if (b.shield_deploy < deploy_target)
        b.shield_deploy = std::min(
            deploy_target, b.shield_deploy + deploy_step);
    else if (b.shield_deploy > deploy_target)
        b.shield_deploy = std::max(
            deploy_target, b.shield_deploy - deploy_step);
    b.shield_deploy_velocity = dt > 1e-6f
        ? (b.shield_deploy - old_shield_deploy) / dt
        : 0.0f;

    const float shield_target =
        b.shield_raised ? 0.12f : 1.10f;

    const float old_shield_angle =
        b.shield_relative_angle;

    const float travel =
        std::fabs(shield_target - b.shield_relative_angle);

    const float duration =
        b.shield_raised
        ? cfg.shield_raise_duration_s
        : cfg.shield_lower_duration_s;

    const float max_step =
        (1.10f - 0.12f)
        * dt
        / std::max(0.001f, duration);

    if (b.shield_relative_angle < shield_target)
        b.shield_relative_angle =
            std::min(
                shield_target,
                b.shield_relative_angle + max_step);
    else if (b.shield_relative_angle > shield_target)
        b.shield_relative_angle =
            std::max(
                shield_target,
                b.shield_relative_angle - max_step);

    (void)travel;

    b.shield_angular_velocity =
        dt > 1e-6f
        ? (b.shield_relative_angle - old_shield_angle) / dt
        : 0.0f;
}

void resolve_equipment_combat(FlyBodyState& a,FlyBodyState& d,float br,const CombatConfig& cfg,double ms,uint64_t& id,std::vector<CombatEvent>& out){
 if(a.weapon_contact_cooldown_s>0 || !a.sword_hit_active_this_step || std::fabs(a.sword_angular_velocity)<cfg.sword_min_hit_angular_speed) return;
 G ag=geom(a,br), dg=geom(d,br);
 // Shield only blocks while deliberately raised.
 if(d.shield_raised){
   const SweepContact shield_sweep=swept_sword_circle(a,br,dg.shield);
   Closest sc=shield_sweep.closest;
   ag=shield_sweep.geometry;
   if(sc.d<=dg.sr){
     Vec2 rv=sub(ag.tipv,dg.shieldv);
     float rs=len(rv);
     float shield_motion=
         std::fabs(d.shield_angular_velocity)
         *(br+dg.sr)
         +std::fabs(d.shield_deploy_velocity)
          *shield_center_deploy_derivative_world(
              d.equipment,br,d.shield_deploy);

     const bool parry=
         d.parry_window_remaining_s>0
         && rs>=cfg.parry_relative_speed_threshold
         && shield_motion>=cfg.parry_shield_motion_threshold;

     if(parry){
       const float mr=
           shield_mass(d.equipment)/kBaseShieldMass;

       const float stun=
           std::clamp(
               cfg.parry_stun_base_s
               + rs*cfg.parry_stun_speed_scale_s*mr,
               cfg.parry_stun_base_s,
               cfg.parry_stun_max_s);

       a.stun_remaining_s=
           std::max(a.stun_remaining_s,stun);

       Vec2 away=
           dir(std::atan2(
               a.position.y-d.position.y,
               a.position.x-d.position.x));

       a.velocity=
           add(
               a.velocity,
               mul(
                   away,
                   cfg.parry_impulse_scale*rs*mr));

       a.sword_phase=SwordPhase::Recovery;
       a.sword_phase_time_s=0;
       a.sword_recovery_start_angle=
           a.sword_relative_angle;
       d.parries++;
       a.weapon_contact_cooldown_s=
           cfg.block_cooldown_s;

       evt(
           out,CombatEventType::Parry,
           d,a,sc.p,ms,id,0,stun,
           std::clamp(rs/1.5f,0.0f,1.0f),
           cross(
               dg.shieldv,
               sub(a.position,d.position))>=0
               ?1.0f:-1.0f);
       return;
     }

     const float mr=
         shield_mass(d.equipment)/kBaseShieldMass;
     // Shield mass is the single canonical strength trade-off for both
     // defensive outcomes. A timing-derived parry remains substantially
     // stronger than an ordinary block at every legal mass.
     const float stun=std::clamp(
         cfg.block_stun_s*mr,
         cfg.block_stun_s*0.60f,
         cfg.block_stun_s*2.00f);
     a.stun_remaining_s=
         std::max(a.stun_remaining_s,stun);

     d.blocks++;
     a.sword_phase=SwordPhase::Recovery;
     a.sword_phase_time_s=0;
     a.sword_recovery_start_angle=
         a.sword_relative_angle;
     a.weapon_contact_cooldown_s=
         cfg.block_cooldown_s;

     evt(
         out,CombatEventType::Block,
         d,a,sc.p,ms,id,0,stun,
         std::clamp(rs/1.3f,0.0f,1.0f),0);
     return;
   }
 }
 const TargetContact target=swept_sword_fly(a,d,br);
 ag=target.geometry;
 const Vec2 sword_arm=sub(target.sword_point,a.position);
 const Vec2 target_arm=sub(target.target_point,d.position);
 const Vec2 sword_velocity=add(a.velocity,mul(perp(sword_arm),a.angular_velocity+a.sword_angular_velocity));
 const Vec2 target_velocity=add(d.velocity,mul(perp(target_arm),d.angular_velocity));
 Vec2 rv=sub(sword_velocity,target_velocity);float rs=len(rv);
 if(target.surface_distance<=0.0f && rs>=cfg.hit_speed_threshold){
   const float score=rs;
   const int dmg=cfg.sword_hit_damage;
   d.hp=std::max(0,d.hp-dmg);
   a.hits_landed++;
   a.weapon_contact_cooldown_s=cfg.hit_cooldown_s;
   a.sword_phase=SwordPhase::Recovery;
   a.sword_phase_time_s=0;
   a.sword_recovery_start_angle=a.sword_relative_angle;
   Vec2 idir=rs>1e-5f?mul(rv,1.0f/rs):ag.sdir;
   d.velocity=add(d.velocity,mul(idir,cfg.hit_impulse_scale*score));
   evt(out,CombatEventType::Hit,a,d,target.sword_point,ms,id,dmg,0,
       std::clamp(score/1.5f,0.0f,1.0f),
       cross(ag.sdir,rv)>=0?1.0f:-1.0f);
   return;
 }
 if(d.dodge_cooldown_s<=0 && target.surface_distance<=cfg.dodge_margin_world && target.surface_distance>0.0f && rs>=cfg.hit_speed_threshold){Vec2 rbv=sub(d.velocity,a.velocity);float lat=std::fabs(cross(rbv,ag.sdir));if(lat>=cfg.dodge_lateral_speed_threshold){d.dodges++;d.dodge_cooldown_s=cfg.dodge_cooldown_s;a.weapon_contact_cooldown_s=.06f;float di=cross(ag.sdir,sub(target.target_point,target.sword_point))>=0?1.0f:-1.0f;evt(out,CombatEventType::Dodge,d,a,target.sword_point,ms,id,0,0,std::clamp(lat,0.0f,1.0f),di);}}
}
} // namespace flyarena
