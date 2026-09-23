# Contributing to FlyArena

## Contributions are welcome

Pull requests to the official FlyArena repository are welcome.

You may fork this repository and modify the source for the purpose of preparing
a contribution to the official FlyArena repository.

If you would like to contribute:

1. Check open Issues, especially those labeled `help wanted` or `good first issue`.
2. Fork the repository.
3. Create a branch for your change.
4. Build with `build_v068.bat`.
5. Run `test_v068_profile_learning.bat`.
6. Open a Pull Request describing the change and how it was tested.

By submitting a contribution, you agree to follow the contribution process
described here.

Before a contribution can be accepted, the contributor must accept the
[Contributor License Agreement](CONTRIBUTOR_LICENSE_AGREEMENT.md).

CLA acceptance is managed through **CLA Assistant** when a Pull Request is opened.
Contributors must complete the CLA Assistant signing process before their
Pull Request can be merged.

Please note that FlyArena is source-available under FASL-1.0 rather than an
OSI-approved open-source project. Permission to contribute to the official
repository does not automatically grant permission to publicly distribute
derivative versions, mods, ports, or commercial products. See [LICENSE](LICENSE)
for details.

## Changes that require prior discussion

Please open an Issue or Discussion before implementing changes that affect:

- neural dynamics or BANC interpretation
- learning/reward behavior
- `.flypack` or `.flytrain` compatibility
- security or file import behavior
- build/release infrastructure
- core combat semantics

Large architectural changes may be declined even if they build successfully.

## Project rules

Keep neural outputs at the actuator level: forward thrust, turn, sword drive,
and shield drive. HIT, BLOCK, PARRY, and DODGE must remain consequences of
geometry and timing.

Rendering consumes snapshots and events and must not mutate simulation state.
Equipment appearance and physics must share canonical parameters, and
`.flypack` files must remain data-only.

Label biology-related claims as measured anatomy, inferred physiology, or
gameplay/model assumptions.

Add deterministic tests and telemetry for behavior changes where practical.

Do not commit BANC raw/cache files, personal `.flytrain` checkpoints, runtime
telemetry, credentials, or machine-specific paths. Small purpose-built test
fixtures are welcome.

## Before opening a Pull Request

- Build the current application.
- Run the existing tests.
- Check that no private files or credentials are included.
- Explain what changed and why.
- Explain how the change was tested.
- Complete the CLA Assistant signing process when requested on the Pull Request.

See `LICENSE_GUIDE_KO.md` and `PERMISSION_REQUESTS.md` for the project's
distribution and derivative-work rules.
