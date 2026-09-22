# Equipment / Cosmetic Customization Specification

All equipment must have one canonical parameter source consumed by both physics and rendering.

Never maintain one “visual length” and a different “physics length” unless the difference is explicit and intentional.

## Sword

### Editable
- skin
- `length_scale`
- v4 derives recovery directly from length; legacy v2 `recovery_scale` remains
  readable but is not exposed by Create Fly

### Physics
Length affects:

- physical reach
- rendered length
- post-swing recovery/cooldown

The active slash itself stays fast and deterministic.

Recommended rule:

`recovery = base_recovery * length_scale * recovery_scale`

Do not reintroduce sword-mass inertia into swing animation unless explicitly requested later.

### Gameplay Meaning
- short sword: less reach, faster repeated attacks
- long sword: more reach, slower repeated attacks

## Wings

### Editable
- skin
- `size_scale` as the single v4 speed/endurance trade-off
- legacy v2 `drive_speed_scale` and `stamina_cost_scale` remain readable but
  are fixed to neutral values in newly created v4 packages

### Physics
Larger wings should increase:

- thrust effectiveness / acceleration
- maximum movement speed

But also increase:

- stamina drain while moving

Use smooth curves and expose constants in a central balance struct.

A reasonable starting shape is:

- speed multiplier ≈ `size_scale^0.5` or gentle linear blend
- stamina drain multiplier ≈ `size_scale^2`

Tune from telemetry, not aesthetics alone.

### Rendering
Wing size in the world and portrait must use the same canonical `size_scale`.

## Shield

### Editable
- skin
- `mass_scale`
- v4 fixes `size_scale` and `stamina_cost_scale` to neutral values so mass is
  the single strength/burden trade-off

### Physics
Mass should affect:

- BLOCK stun duration
- PARRY stun duration / strength
- PARRY knockback impulse
- shield-hold stamina drain
- movement-speed penalty
- turn-response penalty

Heavier shield:
- stronger parry
- larger stamina burden
- slower movement/turning

Recommended movement penalty should be continuous, not binary.

Example starting concept:

`move_multiplier = 1 / (1 + k * (mass_scale - 1))`

Clamp to a safe range.

### Block / Parry
- BLOCK: short attacker stun
- PARRY: substantially longer attacker stun
- PARRY remains timing-derived, never an explicit action channel

## Skins

Skins are cosmetic IDs/assets and must not silently alter physics.

Built-in skins may change visible silhouette and decorative geometry, not only
color. Wing lobes, body proportions/segments, blade ornament, and shield shape
may differ as long as canonical reach, collision coverage, mass, actuator
response, and stamina costs remain entirely determined by equipment fields.

The current built-in IDs are:

- 0: neutral rounded silhouette
- 1: Ruby swept/angular silhouette
- 2: Azure split/segmented silhouette
- 3: Onyx narrow/heavy silhouette
- 4: Aurora fan/twin silhouette
- 5: Royal plume/orb silhouette

Body, wings, sword and shield each have independent primary/secondary gradient
endpoints in Flypack v4. Both the Create Fly preview and arena/HUD renderer
consume the same canonical skin IDs, sizes and component colors.
Portable tests assert that changing only a skin ID cannot alter physics.

Possible categories:

- body
- wings
- sword
- shield

The Create/Edit UI should preview changes immediately in the large HUD portrait.

## Balance / Validation

For sandbox/training, allow broad parameter ranges.

Keep validation metadata so a future ranked ruleset can impose budgets/limits without changing file format.

Never trust imported files blindly:
- clamp numeric ranges
- validate version
- validate asset IDs
- reject NaN/Inf
- ignore unknown fields safely when possible
