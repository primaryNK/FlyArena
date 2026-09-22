# Fly customization boundary

Future fly customization is split into independent layers.

Identity:
- name
- author
- lineage / generation
- icon

Cosmetics:
- body color/pattern
- eye appearance
- wing skin/shape presentation
- sword skin
- shield skin
- trails / presentation effects

Physics-affecting equipment:
- sword collision geometry, length, mass, center of mass, inertia
- shield geometry, mass, inertia
- wing/body actuator geometry and physical parameters

Brain/training:
- learned/plastic state
- training provenance
- compatibility version

For league/ranked play, cosmetic choices should be free while physics-affecting equipment must obey a validated ruleset/budget. Sandbox mode can be much more permissive.

Future `.flypack` files should remain data-only; they should not contain executable DLLs/scripts.
