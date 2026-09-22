# FlyArena v0.6.7 change note

## Full-loop training speed

Training 1x, 2x, 4x, 8x and 16x now pace the complete 50 ms world tick. Neural
integration, readout, learning, 5 ms physics substeps, timer and movement all
advance together. If the selected rate exceeds the machine's capacity, the
simulation runs as fast as the hardware can compute without dropping model
steps.

MAX has no wall-clock target. It suppresses per-tick CSV and arena snapshots,
renders at 4 FPS, and publishes a status heartbeat every 250 ms of wall time.
The display freezes both flies at their opposing spawn locations while showing
episode number, actual simulated time and measured throughput. The hidden
physical fight and learning continue at the hardware limit.

## Spawn and sword collision

RED starts at `(-0.62, 0.0)` facing right and BLUE starts at `(0.62, 0.0)`
facing left. These are explicit gameplay/model coordinates.

Sword collision remains active throughout each powered Swing substep, including
the substep that transitions to Recovery. Collision tests sample the swept blade
arc rather than checking only the final segment pose, reducing tunneling while
preserving geometry-derived HIT/BLOCK/PARRY outcomes.

## Flypack v4 component gradients

Flypack v4 stores separate primary/secondary sRGB endpoints for body, wings,
sword and shield. Versions 1–3 remain readable; their former shared palette is
copied to every component so their appearance is preserved.

Create Fly exposes eight color buttons grouped as four gradients. Preview and
arena rendering consume the same canonical equipment, skin IDs, dimensions and
component color endpoints. Cosmetics remain physics-invariant.
