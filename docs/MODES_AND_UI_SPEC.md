# Modes and UI Specification

## Overall Layout

Keep the existing three-column concept:

- left HUD / fly slot
- center arena
- right HUD / fly slot

Each HUD keeps:

- fly name
- large custom fly portrait
- HP
- stamina
- representative neural activity lights
- learning/frozen status badge
- loaded file/source indicator

Developer telemetry should not dominate the default UI.

## Top-Level Controls

Add visible clickable controls:

- `Create Fly`
- `Training`
- `Battle`
- Settings / speed control as appropriate

### Implemented minimum product loop

The current post-v0.6.5a working tree provides clickable `CREATE FLY`,
`TRAINING`, and `BATTLE` controls plus `PLAY`, `PAUSE`, `REPEAT`, and `RESET`.

- changing Training/Battle cancels the current match and starts a fresh
  CNS/arena run in the selected mode
- Battle disables exploration, learning updates, and checkpoint writes
- REPEAT starts the next episode after a short result-screen delay
- RESET cancels and restarts the current mode
- renderer/window lifetime is independent from individual matches

The current restart boundary reinitializes the GPU CNS and repeats neutral
calibration. Preserving a calibrated CNS allocation across episode resets is a
later performance optimization, not a semantic requirement.

### Training Submode
Inside Training:

- `Random Trainer`
- `Imported Opponent`

Both submodes are implemented. Random Trainer grants mutation permission to
both slots and owns the right slot. Imported Opponent grants it only to the
left slot; the right slot may load a policy but never explores, updates, resets,
or writes it.

## Slot States

Recommended slot badges:

- `LEARNING`
- `FROZEN`
- `TRAINER`
- `EMPTY`
- `BATTLE`

Do not make the user infer which fly is being modified.

## Drag and Drop

Each HUD panel is a drop target.

During drag:
- highlight valid target
- show accepted file type
- show rejection reason if incompatible

On drop:
- validate first
- do not mutate existing slot until validation succeeds
- update portrait/equipment/training metadata atomically

## Random Trainer Mode

Left:
- user fly
- LEARNING

Right:
- generated `Trainer`
- randomized legal equipment
- randomized skins
- separate Trainer training checkpoint

Buttons:
- `Randomize Trainer`
- `Reset User Learning` only if clearly separated and confirmed
- speed selector
- pause/resume

`Randomize Trainer` must:
- stop current episode safely
- create new Trainer identity state
- randomize skin/equipment
- reset Trainer checkpoint
- begin next episode

Do not reset the user fly.

## Imported Opponent Training

Left:
- user fly
- LEARNING

Right:
- imported opponent
- FROZEN

Both HUDs accept drops.

Only the learning fly may update `.flytrain`.

## Battle

Left and right:
- imported/selected fly
- both FROZEN

No:
- learning
- exploration noise
- checkpoint writeback

A Battle must be reproducible enough for debugging from saved inputs/settings, even if full deterministic replay is a later milestone.

## Create Fly Dialog

Fields:

- Name
- Body skin
- Wing skin
- Sword skin
- Shield skin
- Sword length
- Wing size
- Shield mass
- Shield size if retained

Show:
- large live preview
- derived gameplay values:
  - sword reach
  - sword recovery
  - top speed / thrust multiplier
  - movement stamina multiplier
  - shield stamina drain
  - movement penalty
  - parry stun range

Save as `.flypack`.

The implemented minimum dialog includes all listed identity/skin/equipment
fields, a live preview, live derived gameplay values, and validated `.flypack`
saving. Loading the newly created file into a running HUD slot is reserved for
the drag-and-drop slice.

## Training Speed UI

Prefer a compact selector:

`1x  2x  4x  8x  16x  MAX`

At high speed:
- simulation is priority
- renderer may update less often
- UI remains responsive
- no neural-state reset from changing speed

Show both:
- simulated time
- wall-clock speed multiplier

Implemented options are `1x 2x 4x 8x 16x MAX`. They change the D3D12 neural
real-time pacing target without changing the 1 ms neural timestep, 50 ms world
cadence, or 5 ms physics substeps. MAX removes only the real-time target; the
configured GPU duty budget remains active. Battle is fixed at 1x pacing.
