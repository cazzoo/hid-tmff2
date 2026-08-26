# 15 — rF2 weak/erratic FFB: direction sign-fold in the synth/constant paths

**Date:** 2026-08-21 · **Driver:** `src/tmt500rs/hid-tmt500rs.c` · **Trigger:** user
report — rF2 FFB nearly absent/erratic on T500RS while constant-force games feel
fine; needed gain 100% + damper 100% in Oversteer to feel "something that doesn't
sound a force feedback effect at all".

## In-game verdicts (final, 2026-08-26)

Three builds, same hardware, decisive sequence:

| Build | Stream neg. | Native neg. | rF2 | ACC / DR2 |
|---|---|---|---|---|
| M0 (ff3b190) + sign-fold | yes | yes | ✅ at +100% | ❌ mirrored |
| native revert only | yes | no | ✅ at +100% | ❌ **still mirrored** |
| final (this doc) | **no** | **no** | ✅ **at -100%** (game invert) | ✅ confirmed |

Final build user-verified across the whole suite (2026-08-26): rF2 (-100%),
ACC, Dirt Rally 2.0, AC, BeamNG.drive (Proton), Live For Speed (plain Wine) —
all correct. Two transients during verification, both game/Wine-side, resolved
by restart, no driver action:

- **BeamNG zero FFB**: known BeamNG/Wine issue — default FFB "Update Type:
  Fast" relies on `GetEffectStatus()`, which Wine always reports "playing" on
  Linux (Wine bug 57085), so START may never be sent. Workaround/fix: Update
  Type → Full, or BeamNG ≥0.33.1 (forces start() at effect creation).
- **LFS no forces**: plain-Wine prefix/SDL attach flakiness; resolved by game
  restart. Remember the known LFS autocenter-100% trap (see
  t500rs_set_autocenter warning).

The middle row is the decisive one: ACC/DR2 stayed mirrored even with the
native channel fixed — because `synth_mode` latches per probe, their
constants were riding the **still-negated stream** after rF2 had run. rF2
being correct *through* the negated stream while standard games were not
means **rF2's own effect encoding is sign-inverted relative to UAPI** —
the M0 stream negation was compensating for the game, not for the wire
(the C2 mean-negative centering-bias observation corroborates this).

Final model of record: **both `03 0e` (native) and `04 0e` (stream) are
UAPI-standard pass-through — positive byte = rightward.** Both M0
negations are removed. rF2 uses its in-game FFB invert (-100%); no
driver-side per-game exception exists (the kernel FF API carries no game
identity; a magnitude-0/offset≠0 heuristic was considered and rejected
as unmaintainable).

Final test protocol (order no longer matters — both paths share the same
sign semantics):
```sh
make && sudo rmmod hid_tmff_new && sudo insmod ./hid-tmff-new.ko
# ACC / DR2 / AC ...: default sign, must feel correct in any order
# rFactor 2: set FFB to -100% (invert), must feel correct
```

## Root cause chain

1. **rF2 does not use constant force for its main channel.** The C2 Windows
   capture (F1 rim, full race) contains **zero `0x03` packets**; the dominant FFB
   signal is the 32 222-packet `04 0e … 10 27` stream (`05_periodic_0x04_anomaly.md`,
   Hypothesis B). Community sources (Granite Devices forum; berarma's Wine PR
   ValveSoftware/wine#70) confirm rF2 declares a **sine periodic with magnitude 0
   and the force in the signed `offset`**, plus hardware damper/spring conditions.
   Games that feel fine (AC etc.) use plain `FF_CONSTANT` → the native `03 0e`
   path — which is why only rF2 broke.
2. **Wine delivers game polar direction verbatim.** Current Wine dinput→winebus
   evdev provider (`dlls/winebus.sys/bus_udev.c`):
   ```c
   /* Linux FF only supports polar direction ... */
   effect.direction = params->direction[0] * 0x800 / 1125;   /* polar_cd -> 0..0xC000 */
   effect.u.periodic.offset   = params->periodic.offset;      /* passed through */
   effect.u.periodic.magnitude = (params->periodic.magnitude * params->gain_percent) / 100;
   ```
   The rFactor family encodes torque **sign** as polar 0°/180° (see berarma's
   fix mapping sign→0x4000/0xC000 in the SDL path, and Moza universal-pidff
   "fixing the direction to 0x4000"). 0°/180° map to kernel direction
   **`0x0000`/`0x8000`** — exactly where `sin() == 0`.
   (The SDL provider rotates by +270°: `direction = (polar + 27000) % 36000` —
   a different pair of game angles lands on the dead axes, same class of bug.)
3. **Our synth projected the whole sample through sin().**
   `t500rs_synth_sample()` ended with
   `return t500rs_synth_dir_project(sample, e->direction)` where the projection
   was `sample * sin16(dir)/0x7fff`. With rF2 (vertical direction, magnitude 0,
   force in offset) the entire main channel was **mathematically zero**. What the
   user felt at damper 100% was only the hardware damper — matching the report
   verbatim. The native constant path
   (`t500rs_scale_const_with_direction`) had the identical defect for
   vertical-direction constants (PCars-family via the udev provider).
4. **Corroboration that this is exactly the T300/T500 delta:** the T300RS backend
   (`t300rs_calculate_periodic_values`) projects only `magnitude` (a no-op at
   magnitude 0) and passes `offset` through untouched — which is why rF2 has
   historically been survivable on T300-class drivers and died on the new T500RS
   synth engine.

Secondary Wine quirk (not fixable driver-side): per-effect gain scales
`magnitude` only, never `offset` — with rF2's magnitude-0 sine, **the in-game FFB
strength slider does nothing**; only DIPROP_FFGAIN (device `0x43`) and the sysfs
`gain` scale the main channel. This is why the user had to compensate in
Oversteer.

## The fix (2026-08-21)

All in `src/tmt500rs/hid-tmt500rs.c`:

1. **`t500rs_synth_dir_project()`**: sign-fold — `sin16(dir) < 0 ? -level : level`.
   Byte-identical to the old projection at the M0-hardware-verified
   `0x4000`/`0xC000` (sin = ±0x7fff → ±level); every other direction now passes
   full magnitude. The FF_SINE waveform math inside `t500rs_synth_sample()` is
   untouched (that sin is the waveform, not the direction).
2. **`t500rs_scale_const_with_direction()`**: same fold before the wire-negation
   (native `03 0e` path; M0 sign convention preserved).
3. **`t500rs_build_r05_condition()`**: coefficient scaling now rounds instead of
   truncating ((x·10 + 16383)/32767). At default `damper_level=30`, rF2-class
   coefficients recovered one of ten steps (1/10 → 2/10 for coeff 20000). The
   0..10 device ceiling stays — whether firmware accepts >10 is still
   capture-unverified (07_condition_deadband_unverified.md).
4. **`t500rs_update_effect()`**: periodic updates now also refresh `envelope`,
   `delay_ms`, `length_ms`; ramp updates refresh `envelope`, `delay_ms`.
5. Upload/update DBG lines enriched with `dir=/mag=/off=` so a dynamic-debug
   session shows exactly what the game sent.

Not changed, deliberately:
- **T300RS backend** — battle-tested; its constant/ramp paths have the same
  theoretical vertical-direction issue but different code; fix separately if a
  T300 user reports it.
- **`tmff2_upload()` old-refresh logic** (suspected in the initial report) —
  re-analyzed and found sound: `state->old` always equals the last state picked
  up by the worker, which is the last state actually sent; skipping the refresh
  while an update is pending is what maintains that invariant.

## Confirmation procedure (hardware, ~10 min)

> **RESOLVED 2026-08-26** — see "In-game verdicts (final)" above: sign-fold
> confirmed (rF2 forces present through the synth stream); final sign
> handling is pass-through on both channels with rF2 at in-game -100%.
> Procedure kept for the record.

The user's rF2 session is the gate for both this fix and 14_direction_sign.md's
M0 stream-sign check:

```sh
make && sudo rmmod hid_tmff_new && sudo insmod ./hid-tmff-new.ko
# enable upload logging:
echo 'module hid_tmff_new +p' | sudo tee /sys/kernel/debug/dynamic_debug/control
# run rF2, then:
sudo dmesg | grep -E 'Periodic effect.*uploaded|Periodic effect.*updated' | head
```

Expected for rF2: `waveform=0x58 (sine)` with `mag=0` and live `off=` values.
- Force present, correct direction, no invert-FF → **fix confirmed**, M0 gate
  closed — update this file and 14 with the verdict.
- Force mirrored → direction encoding was alive (0x4000/0xC000) and the
  residual is the M0 stream-sign question → flip per 14.
- Still ~nothing → capture the wire (`08_tshark_recipes.md`): our `04 0e`
  stream should now show non-zero b4 bytes while driving; if it does but the
  wheel is silent, the level semantics of `04 0e` need revisiting.

## ffpanel parity note

`tools/ffpanel` now lives in another repo (e38121e). If its parity contract
still models `sample → sin-project → clamp → s8`, the model must switch to the
sign-fold for non-cardinal direction vectors (cardinal 0x4000/0xC000 golden
vectors are unaffected — outputs identical).
