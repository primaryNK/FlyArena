# Current Version Matrix

## v0.6.8 experimental working tree

Build/test validated on Windows on 2026-09-22:

- `build_v068.bat` produces `bin/FlyArena-v0.6.8.exe`
- `test_v068_profile_learning.bat` passes
- the locally built portable package includes one-click BANC topology/IO setup
  and a validated launcher; its unsigned public Windows download is withheld
  while a suspected Defender cloud false positive is investigated
- BANC/GPU/calibration state remains resident across repeated rounds
- Training 1x–16x pace the complete world loop; `MAX` removes wall-clock
  pacing, uses a 4 FPS frozen-spawn status view, and decimates visual/CSV work
  without changing neural/world/physics timesteps
- RED/BLUE spawn on opposite sides facing inward
- powered sword collision samples the complete swept Swing arc, including the
  final Swing-to-Recovery substep
- rendered sword pivot, rotation, and length match canonical collision geometry;
  direct tests prove longer swords gain physical reach
- body and size-scaled left/right wing capsules are damageable hitboxes
- Create Fly saves are immediately queued into the LEFT slot for a fresh match
- Create Fly exposes the linked sword, wings, and shield trade-offs
- `.flypack` v4 stores separate two-color gradients for body/wings/sword/shield
  and built-in skin IDs 0..5; v1/v2/v3 remain readable
- repository-local Git, Windows CI, source-only tag release packaging,
  checksums, citation, FASL-1.0 notices, clean-source staging, and issue
  templates are prepared

## v0.6.3 series
Established:

- visual arena
- independent renderer
- virtual canvas / resize handling
- emoji fly art
- large HUD portraits
- key-neuron activity lights
- inertial flight
- wall bounce
- integer HP
- 60-second matches
- result screen

## v0.6.4 series
Established:

- sword drive
- shield drive
- real sword geometry
- real shield geometry
- active-swing-only HIT
- timed BLOCK/PARRY
- integer stamina
- sword length -> reach/recovery
- shield hold stamina cost
- block/parry attacker stun
- stronger wall damage
- reward-ready HP-loss signal

Last user-confirmed good gameplay state: **v0.6.4d**.

## v0.6.5a
Experimental latest:

- first online plastic readout learner
- exploration
- eligibility traces
- reward-modulated updates
- persistent `.flytrain`
- learning vs frozen battle launchers
- slightly stronger wall damage

Treat v0.6.5a as requiring real Windows compile/runtime validation before building large UI features on top of it.

## Post-v0.6.5a profile/equipment vertical slice (current working tree)

Validated on Windows in the repository environment:

- `build_v065.bat` succeeds
- a short learning match creates nonzero learned weights/checkpoints
- a subsequent frozen Battle leaves checkpoint hashes and timestamps unchanged
- portable profile/learning tests pass with `/W4 /WX`
- a short D3D12 Battle loads Ruby/Azure from `.flypack`

Added:

- transactional, versioned, data-only `FlyProfile` / `.flypack` v1
- CLI `--red-flypack` and `--blue-flypack`
- canonical profile equipment drives both physics and rendering sizes
- built-in body/wing/sword/shield skin IDs 0..2 are now visibly rendered in
  both the world and the HUD portrait
- shield mass now continuously affects turn response as well as movement,
  parry, and stamina costs; mass 1.0 preserves the previous turn response
- summary telemetry records profile UUID, equipment inputs, and derived
  movement multipliers

## Minimum product-loop controls (current working tree)

- Create Fly modal with live skin/equipment preview and derived values
- clickable Training/Battle switching
- mode switching cancels and starts a fresh match
- PLAY, PAUSE, REPEAT, and RESET controls
- persistent renderer across repeated episodes
- episode number in HUD status and summary
- built-in skins now change cosmetic silhouette/ornament as well as color;
  automated tests guarantee skin IDs do not change physics
- HIT/BLOCK/PARRY/DODGE effects originate at their recorded physical contact
  or avoidance position instead of the arena center
- each HUD has a persistent dotted `.flytrain` drop target, visible active
  save path/status, and a confirmed Training-only learning reset
- dropping a valid `.flytrain` restarts the match and makes that file the
  active checkpoint for the selected slot; invalid files retain the old state

Newly completed vertical slices:

- Random Trainer and frozen Imported Opponent submodes with slot-specific
  exploration/update/write permissions
- transactional HUD `.flypack` import and OLE drag-hover feedback
- Training-only 1x/2x/4x/8x/16x/MAX pacing and measured multiplier display
- visual skin cards, large preview, and canonical physical sliders
- backwards-compatible `.flypack` v2
- adjustable reward preferences with automatic sign-budget balance/capping
- progressive stamina feedback and reason-specific telemetry
- shared physical/visual continuous shield deployment
- event-driven HIT/BLOCK/PARRY/DODGE audio and throttled magnetic wall hum
- uniformly window-scaled combat VFX and a responsive dark Create Fly dialog
- one user-facing arena artifact: `bin\FlyArena.exe`; tests/regressions stay
  under `obj`

Still pending:

- `.flytrain` v2 compatibility metadata and UUID binding
- final hands-on GUI/runtime stabilization pass

The separate `PUBLIC_RELEASE_MILESTONE.md` tracks the remaining clean-machine
and rights-holder checks before the first public tag.

## Important

Do not accidentally regress from v0.6.4d combat rules while integrating v0.6.5 learning.

The v0.6.5a validation gate was completed before the mode/customization UI:

1. compile v0.6.5a,
2. run one learning match,
3. verify weights change,
4. verify checkpoint saves,
5. run battle mode,
6. verify weights remain unchanged,
7. only then begin mode/UI refactor.

Repeat these checks after changes to learning, persistence, mode transitions,
or combat rules.
