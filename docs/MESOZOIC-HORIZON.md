# MESOZOIC-HORIZON — the cerebellum for the haltere reflex

> Design for the *attitude* layer above MESOZOIC's rate damper. The bare brain
> damps the tumble; HORIZON adds a restoring force toward level. This is the
> evolutionary step that turns an unstable wing form into a flyer — the moment a
> vestibular reflex grows a cerebellum.

---

## 1. What MESOZOIC does now (and why it cannot hold level)

The rate-only brain (Zephyrus.cpp `update()` under `MESOZOIC_ONLY`) is a
**haltere reflex**: raw gyro LSB → bias-correct → board-rotate → three
fixed-point PIDs with **target = 0** → °/s corrections → wing mixer.

- Attitude is hard-zeroed: `rollDeg = 0.0f; pitchDeg = 0.0f;`
- The accelerometer is read (`_mpuReadSensors` fetches the 14-byte block) but
  **discarded** — gravity lies dead in the register.
- `ZephyrusConfig.h` states the limitation honestly: *"the brain only damps the
  tumble. The pilot + dihedral do the levelling."*

A rate damper drives rate → 0 but leaves attitude a **free integrator**: the
body holds whatever attitude it drifted to. There is no gravity reference, no
horizon, no restoring moment. Rate-stable ≠ level-stable.

### The pitch-instability argument

Most natural flyers are deliberately unstable in pitch — a destabilizing pitch
moment (nose-up perturbation → more nose-up moment) is aerodynamically
advantageous: it removes the need for passive stabilizing surfaces and lets the
tail shrink away, in exchange for a *nervous* stabilizer. A single rate loop
can damp that positive pole, but it **cannot** both place it stably *and*
supply a level reference — the attitude term is structurally required.

The flapping wing compounds this: reaction torque + asymmetric lift pulse a
periodic pitch moment at beat frequency. Rate damping + I-term trims the *mean*,
but the periodic component still excites pitch at beat frequency, and any
residual mean (CG shift, wing wear, phase offset) drifts uncorrected into a
dive or stall.

---

## 2. The real obstacle is not Mahony — it is a polluted gravity sensor

Mahony was dropped correctly, but for the wrong reason. It is not "expensive";
it is **wrong on a flapping body** because the accelerometer is saturated with
beat-frequency self-acceleration (the same self-noise ESPELHO cancels on the
gyro axis in OrniFlight). A naive complementary filter feeds that beat noise
into the attitude estimate, so the attitude oscillates at beat frequency —
useless for HORIZON.

The child (attitude) was thrown out with the bathwater (noise). The step is to
**filter the bathwater, keep the child**: recover a *flap-decoupled gravity
vector*, then fuse it slowly.

---

## 3. Architecture: cascade, like a real nervous system

```
   gyro LSB ──► mesoPid (fast, 250 Hz) ──► wing mixer     [already exists]
   accel LSB ─► flap-decoupled gravity ─► attitude (slow) ─┐
                                                          │ attitude PID
   stick horizon ────────────────────────────────────────►│ (1–2 Hz)
                                                          └─► rate setpoint
```

- **Inner loop (fast)** — the haltere reflex. Keep it. It is the right layer.
- **Outer loop (slow)** — attitude regulation against a *clean* gravity
  reference. Bandwidth deliberately ~1–2 Hz, far below beat frequency, so no
  residual beat component can excite the attitude.

The whole design hinges on the **flap-decoupled gravity vector**. Two tools,
both already built elsewhere in the codebase, compose to produce it:

1. **Stroke-reversal accel gating.** Sample the accelerometer only at the top
   and bottom of stroke, where wing velocity ≈ 0 and self-acceleration is
   minimal. Stroke reversal is already detectable in Ornithopter.cpp via the
   `_prevFlappingSin` edge. (The old rate-gate was correctly dropped; the
   *accel* gate is where gating belongs.)
2. **ESPELHO lock-in port to the accel axis.** The same 4-mixer orthogonal
   bank `{sinθ, cosθ, sin2θ, cos2θ}` built for OrniFlight ESPELHO learns the
   flap-coherent self-image of the accel vector and subtracts it, leaving
   gravity + true body motion. No extra trig: sin2θ/cos2θ are derived from
   sinθ/cosθ.

Together they yield a gravity vector that does **not** oscillate at beat
frequency — the precondition for a stable attitude estimate.

---

## 4. Implementation plan (gated, non-destructive)

New flag `MESOZOIC_HORIZON` layered on top of `MESOZOIC_ONLY`. Without it,
behaviour is byte-identical to today.

- **T1 — gravity recovery.** New module `MesozoicGravity` (fixed-point where
  possible): accel LSB → ESPELHO flap-coherent cancellation (reusing the
  lock-in math) + stroke-reversal gating → clean gravity vector.
- **T2 — attitude estimator.** Slow complementary blend (gyro-integrated rate +
  clean gravity), attitude bandwidth ≈ 1–2 Hz, flap-rejection verified.
- **T3 — attitude PID.** Outer loop producing rate setpoints for the existing
  `mesoPid` inner loop. HORIZON = attitude setpoint from stick, blended to rate
  at full deflection.
- **T4 — verification.** Simulator sweep: pitch-only disturbance, beat-frequency
  accel injection, confirm attitude holds level with the unstable pitch pole
  present.

### Decision (chosen: both tools)

The flap-decoupled gravity uses **stroke-reversal gating + ESPELHO lock-in
port combined** — gating removes the worst-case burst, lock-in removes the
residual coherent remainder. Either alone leaves a beat-frequency residue that
would leak into the slow attitude loop.

---

## 5. Acceptance criteria

- Attitude loop holds level against a mean destabilizing pitch moment with the
  beat-frequency accel disturbance injected (no oscillation at beat frequency).
- `MESOZOIC_ONLY` alone (flag off) produces byte-identical rate-damper behaviour.
- Fixed-point inner loop intact; no float division / trig on the hot path.
- Flash + RAM budget reported for the ESP8285 target.
