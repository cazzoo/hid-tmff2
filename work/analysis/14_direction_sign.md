# 14 — Direction sign on the wire (M0 verdict, final)

> **M0 CONFIRMED (2026-08-20, ffpanel on hardware):** the wheel pulls
> opposite to the UAPI prediction for both the `04 0e` stream channel
> **and** the native `03 0e` constant channel. Both channels are
> sign-inverted relative to UAPI on this hardware. The driver now negates
> at both writers so a positive level byte means a **rightward** (east)
> pull everywhere.

**Date:** 2026-08-20 · **Tool:** `tools/ffpanel` (parity monitor,
golden-vector backed) · **Driver:** `src/tmt500rs/hid-tmt500rs.c`

## The evidence

| Test | Path | Level | Wheel did | UAPI says |
|------|------|-------|-----------|-----------|
| constant, positive level | native `03 0e` | +38 (s8) | **LEFT** | right ✗ |
| constant, negative level | native `03 0e` | −38 (s8) | **RIGHT** | right ✓ |
| ramp, start = −10000 | synth `04 0e` stream | −38 (s8) | **LEFT** | right ✗ |
| ramp, mid-sweep | stream | 0 | 0 | 0 ✓ |

The native constant test originally used to "verify" the native channel
ran on a wheel that had already been exercised by the stream channel in
the same boot; the post-fix cross-check (positive constant → LEFT)
showed the native `03` byte is inverted too. So both channels are
inverted relative to UAPI on this hardware, and the fix negates at both
writers.

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

Both packet writers negate the level before writing, so the wire byte
carries UAPI sign semantics (positive = rightward) on every channel:

- `t500rs_synth_stream_level()` (single writer of `04 0e` level bytes)
  negates the level;
- `t500rs_scale_const_with_direction()` negates the projected level for
  the native `03 0e` constant channel.

After both, periodic/ramp/rumble direction is UAPI-correct (sine/rumble
are sign-symmetric — behavior unchanged; saw up/down swap to their
correct mirror; ramp sweeps in the predicted direction), and constants
— whether played native or through the stream while `synth_mode` is on
— are also correct.

The tool needs no math change: the parity contract models the semantic
level (sample → project → clamp → s8), and the wire negation happens
below that abstraction, inside the driver's packet writers.

## Remaining gate

> **FINAL IN-GAME VERDICT (2026-08-26) — both M0 negations removed; both
> channels are UAPI-standard pass-through.**
>
> Test history (three builds, same hardware):
>
> 1. M0 negated build (stream + native negated): rF2 correct at +100%;
>   ACC / Dirt Rally 2.0 mirrored.
> 2. Native negation removed only: rF2 still correct at +100%; ACC/DR2
>   **still mirrored** — decisive: those runs rode the *stream* (synth_mode
>   latches for the whole probe once rF2 runs), so the native revert never
>   executed. The surviving stream negation was the inverter.
> 3. Conclusion: the stream channel is NOT wire-inverted. rF2 alone was
>   correct through the negated stream ⇒ **rF2's own effect encoding is
>   sign-inverted relative to UAPI** (matching the C2 mean-negative
>   centering-bias observation). The M0 lab ramp test was misleading the
>   other way (and the native "cross-check" was the tainted same-boot
>   reading already flagged above).
>
> Final wire model: `03 0e` (native) and `04 0e` (stream) both pass the
> semantic level through — positive byte = rightward. rF2 sets its in-game
> FFB invert (-100%); every standard-encoded game (ACC, DR2, AC, …) is
> correct at default sign on both paths, in any launch order.
>
> A driver-side per-game exception is not implementable: the kernel FF API
> carries no game identity (a "negate magnitude-0/offset≠0 periodic"
> heuristic was considered and rejected as unmaintainable).

Per the plan, the driver-side sign work is complete pending the
in-game sanity check: torque feels correct with **no** invert-FF option
enabled in the game. Update this file with the verdict (and the fix
commit hash) when done.

> **2026-08-21:** the rF2 in-game run doubles as the gate for
> `15_rf2_synth_sign_fold.md` — the direction projection was sign-folded
> (byte-identical at 0x4000/0xC000), so one rF2 session closes both this
> gate and that one. See 15 for the procedure.
