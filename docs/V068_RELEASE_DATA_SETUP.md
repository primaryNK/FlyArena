# FlyArena v0.6.8 change note

## Portable Release data setup

The portable ZIP intentionally excludes the large BANC source/cache. It now
contains `SETUP_DATA_AND_RUN.bat`, which checks Python 3 and curl, downloads
BANC v888, converts the topology, builds the IO map, and launches FlyArena.
Completed files are reused. `START_FLYARENA.bat` validates both generated files
before later launches.

Directly launching `FlyArena.exe` without data now shows an English-only setup
message rather than an internal topology-cache error. Keeping this startup
dialog ASCII-only avoids corrupted Korean text on machines whose compiler or
system code page differs. Runtime-root discovery
supports both source-build `bin\` layout and a portable ZIP with the executable
at its root.

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

GPU duty behavior is now explicit: 1x–16x Training and Battle honor the
configured `--gpu-budget` (40% in the supplied launchers), including inside the
neural dispatch loop. MAX intentionally bypasses that duty sleep and can show
near-100% GPU usage because its purpose is maximum-throughput learning. Select
16x or lower when temperature, fan noise, or power use matters more than speed.

## Spawn, sword reach, and wing hitboxes

RED starts at `(-0.62, 0.0)` facing right and BLUE starts at `(0.62, 0.0)`
facing left. These are explicit gameplay/model coordinates.

Sword collision remains active throughout each powered Swing substep, including
the substep that transitions to Recovery. Collision tests sample the swept blade
arc rather than checking only the final segment pose, reducing tunneling while
preserving geometry-derived HIT/BLOCK/PARRY outcomes.

The rendered sword now uses the same body-relative pivot, world-to-screen
rotation direction, and canonical world length as collision. A direct regression
places the same target outside a 0.60x sword and inside a 1.80x sword, so future
changes cannot silently collapse customized reach back to a fixed value.

Damageable fly geometry now includes the circular body and two body-oriented
wing capsules. `wing_size_scale` expands the visible wings and those collision
capsules together; skin ornaments remain cosmetic. The test suite verifies that
a body-missing slash misses a 0.65x wing but hits the same fly at 1.60x.

## Flypack v4 component gradients

Flypack v4 stores separate primary/secondary sRGB endpoints for body, wings,
sword and shield. Versions 1–3 remain readable; their former shared palette is
copied to every component so their appearance is preserved.

Create Fly exposes eight color buttons grouped as four gradients. Preview and
arena rendering consume the same canonical equipment, skin IDs, dimensions and
component color endpoints. Saving from Create Fly now queues that fly into the
LEFT slot and starts a fresh match, so the previewed dimensions cannot remain
detached from the active fighter. Cosmetics remain physics-invariant.
