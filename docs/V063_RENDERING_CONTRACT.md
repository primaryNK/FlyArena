# FlyArena v0.6.3 rendering contract

This is a design constraint for the upcoming visual arena.

## Minimum visual refresh

The rendered arena must target **at least 60 Hz**.

The renderer must not be coupled 1:1 to the neural or world simulation tick.

Planned clocks:

- neural integration: 1 ms internal GPU step
- sensory/world control snapshot: initially ~20 Hz (50 ms), tunable later
- renderer: minimum 60 Hz
- optional presentation targets: 120 Hz / 144 Hz / display refresh / uncapped benchmark

## Interpolation

The simulation publishes timestamped immutable world snapshots.

The renderer reads the previous and current snapshots and interpolates:

- body position
- heading
- limb pose
- weapon/shield transforms
- UI gauges

This lets a 20–60 Hz world/control update appear smoothly at 60–144+ rendered frames per second without changing neural dynamics.

## Thread boundary

Planned architecture:

`Neural GPU -> World/Physics -> snapshot buffer -> Renderer`

The renderer must never mutate neural state.

If simulation falls behind, rendering may repeat/interpolate the latest valid snapshots rather than speeding up biological time.

## Future settings

The simulator UI should expose:

- VSync on/off
- frame cap: 60 / 120 / 144 / display / uncapped
- telemetry overlay
- neural visualization quality

Default should prefer the display refresh rate while never intentionally targeting below 60 FPS on capable hardware.
