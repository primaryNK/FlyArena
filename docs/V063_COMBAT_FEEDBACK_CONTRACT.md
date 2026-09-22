# Combat feedback / presentation contract

The renderer should present combat outcomes as short-lived world-space feedback.

Examples:

- `Ruby — PARRY!`
- `Azure — HIT!`
- `Ruby — BLOCK!`
- `Azure — DODGE!`

Each `CombatEvent` contains:

- event type
- simulation timestamp
- actor
- target
- world position
- intensity
- future stable event ID

## Important semantic boundary

The neural model does not output `PARRY`, `HIT`, `BLOCK`, or `DODGE` actions.

These labels must be classified from physical events.

Planned semantics:

- HIT: weapon collision transfers qualifying impulse/energy to vulnerable body geometry.
- BLOCK: shield intercepts an incoming weapon/body threat without satisfying parry timing/kinematic criteria.
- PARRY: shield/weapon interception satisfies a tighter timing + relative-velocity/geometry criterion and materially redirects the attack.
- DODGE: a threatened attack trajectory would have intersected the body but body motion clears the predicted collision window without block/parry contact.

These definitions will be parameterized and logged so replay and ranked matches classify the same physical event identically.

## Renderer behavior

At 60+ FPS, presentation may include:

- world-space text burst
- brief scale/fade animation
- contact spark / ring
- actor name
- sound cue later

The renderer consumes events but never decides combat results.
