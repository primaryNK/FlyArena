# Contributing to FlyArena

FlyArena is source-available under FASL-1.0, not open source. Reading the code,
reporting bugs, and proposing ideas are welcome. A public modified fork or
derivative distribution requires prior written permission; see
`LICENSE_GUIDE_KO.md` and `PERMISSION_REQUESTS.md`.

Build the current experimental application with `build_v068.bat` and run
`test_v068_profile_learning.bat` before opening a pull request. Lightweight CI
does not download the full BANC dataset.

Keep neural outputs at the actuator level: forward thrust, turn, sword drive,
and shield drive. HIT, BLOCK, PARRY, and DODGE must remain consequences of
geometry and timing. Rendering consumes snapshots and events and must not
mutate simulation state. Equipment appearance and physics must share canonical
parameters, and `.flypack` files must remain data-only.

Label biology-related claims as measured anatomy, inferred physiology, or
gameplay/model assumptions. Add deterministic tests and telemetry for behavior
changes, including fields that separate an unattempted action from a failed
attempt. Version changes to formats, compatibility, rewards, and parameter
meaning.

Do not commit BANC raw/cache files, personal `.flytrain` checkpoints, runtime
telemetry, credentials, or machine-specific paths. Small, purpose-built test
fixtures are welcome.

Before a contribution can be accepted, its author must explicitly accept the
`CONTRIBUTOR_LICENSE_AGREEMENT.md` through the contribution mechanism selected
by the project owner. Opening a pull request alone does not transfer rights or
grant permission to distribute a modified FlyArena build.
