# 14 — Direction sign on the wire (M0 verdict, amended)

> **M0 CONFIRMED (2026-08-20, ffpanel on hardware):** the wheel pulls
> opposite to the UAPI prediction for **streamed** effects. Amended
> verdict (same day, after cross-checking constant vs ramp): the
> inversion is **channel-specific** — the `04 0e` level stream is
> sign-inverted relative to the native `03` param channel — not a
> global wire-sign flip. Fixed at the stream write; see below.

**Date:** 2026-08-20 · **Tool:** `tools/ffpanel` (parity monitor,
golden-vector backed) · **Driver:** `src/tmt500rs/hid-tmt500rs.c`

## The evidence

| Test | Path | Level | Wheel did | UAPI says |
|------|------|-------|-----------|-----------|
| constant, negative level | native `03 0e` | −38 (s8) | **RIGHT** | right ✓ |
| ramp, start = −10000 (2 sessions) | synth `04 0e` stream | −38 (s8) | **LEFT** | right ✗ |
| ramp, mid-sweep | stream | 0 | 0 | 0 ✓ |

The same negative byte through the two channels produces opposite
forces — impossible unless the channels' sign conventions differ. The
constant test that M0 was originally based on (and today's re-test) ran
**before any periodic upload in the boot**, i.e. on the native channel;
every inverted-feeling observation (original ffctl report, ramp start)
ran on the stream channel (a constant played after any periodic/ramp
upload in the same boot also rides the stream — `synth_mode` is
one-way).

Corroboration from the C2 capture (`05_periodic_0x04_anomaly.md`,
Hypothesis B): the stream byte is a signed s8 with a Gaussian
distribution whose mean is slightly **negative** while carrying a
self-aligning (centering) torque — consistent with a stream convention
opposite to rightward-positive.

Secondary observation — "ramp gets harsh-left when the display rises
toward L": with the inverted stream, every level played mirrored; the
 sharpest artifact is the count>1 restart (level snaps +38 → −38 →
 wheel snaps hard). With count=1 the sweep is a pure mirror.

## The fix (implements the corrected D1)

`t500rs_synth_stream_level()` (the single writer of `04 0e` level
bytes) negates the level before writing, so the wire byte carries UAPI
sign semantics and the stream matches the native channel:

- periodic/ramp/rumble direction becomes UAPI-correct (sine/rumble are
  sign-symmetric — behavior unchanged; saw up/down swap to their
  correct mirror; ramp sweeps in the predicted direction);
- constants played while `synth_mode` is on also become correct
  (they previously inherited the inversion through the stream).

D1 **as originally drafted** (negate `t500rs_scale_const_with_direction()`
and `t500rs_synth_dir_project()`) is **rejected**: it would have
broken the native channel, which is UAPI-correct (M0-verified).

The tool needs no math change: the parity contract models the semantic
level (sample → project → clamp → s8), and the wire negation happens
below that abstraction, inside the driver's packet writer.

## Remaining gate

Per the plan, the driver-side sign work is complete pending the
in-game sanity check: torque feels correct with **no** invert-FF option
enabled in the game. Update this file with the verdict (and the fix
commit hash) when done.
