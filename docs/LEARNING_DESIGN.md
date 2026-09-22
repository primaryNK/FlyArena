# Learning Design Notes

## Current v0.6.5a Concept

The first learner should remain a small plastic readout between fixed BANC CNS activity and low-level actuator channels.

Actions:

- forward residual
- turn residual
- sword-drive residual
- shield-drive residual

Inputs:

- DN population summary
- MN population summary
- left/right differences
- left/right action-population activity
- 12 representative live key-neuron activities

This is intentionally not full-connectome plasticity yet.

## Why Start Here

It gives a debuggable proof that:

1. reward changes behavior,
2. checkpoints persist,
3. training vs frozen battle works,
4. repeated episodes improve measurable metrics,
5. reward shaping does not collapse into passivity.

Only after those are stable should learning be moved deeper into BANC synapses.

## Reward Principles

The user explicitly wants flies to learn wall avoidance **without learning to avoid combat**.

Therefore:

### Negative
- own HP lost: strongly negative
- wall HP lost: naturally negative through HP loss
- defeat: negative

### Positive
- damage personally dealt to opponent
- HIT
- BLOCK
- PARRY
- DODGE
- victory
- very small engagement reward only if necessary

Do not reward:
- opponent hurting itself on wall
- time spent alive by itself
- simply being far from danger

## Implemented reward tuning / automatic balance

Training exposes user preference scales (0.25-2.0) for own-damage penalty,
attack success, defense success, terminal result, and engagement shaping.
Those preferences do not feed the learner unchecked. Each fly maintains
positive and negative EWMA budgets, symmetrically normalizes the two signs with
bounded 0.40-2.50 automatic scales, and clamps the final per-tick learning
reward to `[-30, +30]`. Telemetry schema 3 records raw sign budgets, automatic
scales, balanced reward totals, and clamp counts. These are engineering/gameplay
assumptions, not biological measurements.

## Recommended Episode Metrics

Track trends over many episodes:

- mean reward
- win rate versus fixed evaluation opponents
- wall damage per simulated minute
- damage dealt
- damage taken
- HIT efficiency = hits / swings
- block efficiency
- parry rate
- average engagement distance
- passive time
- policy weight norm

Training should be judged against a frozen evaluation set, not only against a co-learning Trainer.

## Random Trainer Caveat

If both user fly and Trainer learn simultaneously, the target distribution moves.

That is acceptable for variety, but periodically evaluate the user fly against frozen checkpoints to determine whether it is actually improving.

The `Randomize Trainer` button should clear Trainer learning and create a new randomized loadout/skin while preserving the user's checkpoint.
