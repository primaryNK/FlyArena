# FlyArena File Formats — Proposed Contract

All shareable fly files must be **data-only**.

Never execute code, DLLs, scripts, shaders, plugins, or arbitrary asset paths from imported fly files.

## `.flypack`

Purpose: portable/shareable character package.

Suggested versioned JSON or ZIP-with-manifest structure.

### Implemented v1/v2/v3/v4 formats

The v0.6.7 working tree implements small, dependency-free, line-oriented
manifests with magic headers:

`FLYARENA_FLYPACK_V1`

`FLYARENA_FLYPACK_V2`

`FLYARENA_FLYPACK_V3`

`FLYARENA_FLYPACK_V4`

The extension remains `.flypack`. This first representation is deliberately
data-only and can be migrated to JSON or a ZIP manifest in a future format
version; readers must never silently reinterpret version 1.

Implementation:

- `src/profile/fly_profile.h`
- `src/profile/fly_profile.cpp`
- default packages in `data/flies/`

Shared limits and validation:

- maximum file size: 64 KiB
- required matching magic/`format_version`, `sim_compatibility=v0.6.5`, and
  `banc_compatibility=v888/v3`
- bounded identity strings and restricted UUID characters
- built-in skin registry IDs 0..5 in v3 (older values remain valid)
- finite equipment values only; finite out-of-range values are clamped with
  warnings to the canonical physics ranges
- a training reference, when present, is a leaf `.flytrain` filename only;
  it can never supply an absolute or traversing path
- duplicate known fields are rejected
- unknown fields are ignored for forward-compatible optional metadata
- load is transactional: a failure does not mutate the active profile

Sword mass and thickness are not customizable. They are normalized to
1.0 so an imported package cannot smuggle hidden sword physics behind a skin.

Contains:

```text
format_version
sim_compatibility
banc_compatibility
identity
  name
  uuid
  lineage
cosmetics
  body_skin_id
  wing_skin_id
  sword_skin_id
  shield_skin_id
  primary_color_rgb                # v3 shared palette, packed 0xRRGGBB
  secondary_color_rgb              # v3 shared palette
  body_primary_color_rgb           # v4
  body_secondary_color_rgb         # v4
  wing_primary_color_rgb           # v4
  wing_secondary_color_rgb         # v4
  sword_primary_color_rgb          # v4
  sword_secondary_color_rgb        # v4
  shield_primary_color_rgb         # v4
  shield_secondary_color_rgb       # v4
equipment
  sword_length_scale
  sword_recovery_scale              # v2
  wing_size_scale
  wing_drive_speed_scale            # v2
  wing_stamina_cost_scale           # v2
  shield_mass_scale
  shield_size_scale
  shield_stamina_cost_scale         # v2
training
  optional embedded/reference readout checkpoint metadata
statistics
  optional lifetime/episode counters
```

v1 remains readable and supplies neutral `1.0` defaults for all v2 tuning
fields. V1 and v2 receive safe default gradient colors. V3's shared gradient
is copied to all four v4 components. Create Fly writes v4.
Readers reject magic/version mismatches and missing version-required fields
rather than silently reinterpreting them.

If ZIP is used in a future format, only load a strict whitelist of data files.

## `.flytrain`

Purpose: learned state/checkpoint.

Current v0.6.5 experimental checkpoint stores the plastic readout weights and training metadata.

Going forward, version it explicitly and include:

```text
format_version
learner_version
feature_schema_version
action_schema_version
banc_version
io_map_version
fly_uuid
training_steps
cumulative_reward
weights
optimizer/eligibility state if needed
```

A `.flytrain` dropped onto an existing fly updates learned state only. It must not unexpectedly replace cosmetics/equipment.

Current product-loop behavior:

- drop one `.flytrain` onto the dotted left/right HUD target;
- the validated file becomes that slot's visible active save path;
- the current match is cancelled so the imported policy starts in a fresh match;
- a failed load leaves the previous policy/path active and shows an error;
- `RESET THIS FLY LEARNING` is available only in Training, requires confirmation,
  and writes a zero-step checkpoint to the displayed active path;
- Battle never performs learning, exploration, periodic save, or reset writes.

## `.flyreplay`

Future deterministic/debug replay.

Should store:

- fly package hashes
- training checkpoint hashes
- simulator version
- balance/config version
- initial RNG seeds
- time scale
- event stream / control stream as needed

## Combat telemetry naming

Newly generated combat CSV/summary output uses telemetry schema version 3 and
the names `BLOCK`, `red_blocks`, `blue_blocks`, and `total_block_events`.
Schema 3 adds reward raw-positive/raw-negative budgets, automatic sign scales,
reward clamping, detailed stamina accounting, and continuous shield deployment.
Historical result files are retained verbatim for reproducibility and may
still contain the former `GUARD` field names; the underlying physical rule is
unchanged.

## Compatibility Rules

Never silently reinterpret an old field.

On incompatible data:
- reject cleanly,
- show the required version,
- keep the currently loaded fly intact.

Unknown optional fields may be ignored if the format version permits forward compatibility.

## Security

Imported packages:
- numeric bounds checked
- NaN/Inf rejected
- string lengths bounded
- asset IDs resolved only through trusted built-in/user-approved asset registries
- no absolute path traversal
- no executable content
