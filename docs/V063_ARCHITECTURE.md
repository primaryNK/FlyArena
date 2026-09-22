# V0.6.3 visual architecture

```text
D3D12 whole-CNS simulation thread
        |
        | 50 ms world snapshots
        v
SnapshotBuffer
(previous + current + publish time)
        |
        | interpolation
        v
Direct2D / DirectWrite renderer
60 / 120 / 144+ FPS
```

The renderer intentionally operates with one world-tick display latency. When a new snapshot arrives, the renderer starts at the previous snapshot and interpolates toward the new one over the next world interval.

This prevents a 20 Hz world tick from looking like a 20 FPS animation.

## V0.6.4 event flow

Future physical combat is planned as:

```text
weapon / shield / body collision
        |
        v
combat classifier
        |
        +--> HIT
        +--> BLOCK
        +--> PARRY
        +--> DODGE
        |
        v
CombatEvent
        |
        +--> renderer VFX
        +--> replay
        +--> telemetry
        +--> training reward (later)
```

The renderer consumes events but never decides whether a parry or hit occurred.
