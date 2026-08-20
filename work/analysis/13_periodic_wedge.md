# 13 — Periodic-effect family wedges the firmware (P3-5 verdict)

> **RESOLUTION (2026-08-18, later):** periodic/ramp support was re-implemented
> on the host-side synthesis model described in the erratum below — slot-0
> sine MAIN byte-identical to C2 f2637 plus a `04 0e` level stream from a
> software waveform engine. FF_PERIODIC (all waveforms), FF_RAMP and FF_RUMBLE
> are advertised again; no per-slot periodic bytes are ever sent. The original
> verdict below is kept for the record.
>
> **First b65e datapoint (2026-08-18, synthesis build):** the slot-0 sine
> MAIN was **accepted** (no wedge, no EPROTO, no re-enumeration) and a
> periodic effect played — the C2 f2637 form is valid on b65e firmware too.
>
> **VERIFIED (2026-08-18, same day, ffctl):** frequency scaling confirmed
> correct on hardware with explicit periods — `--period 2000` plays a slow
> ~0.5 Hz wobble, shorter periods play proportionally faster rumble. The
> earlier "feels like rumble" report was fftest's uninitialized scanf inputs,
> not a driver bug. **P3-5 is closed ✅ — periodic/ramp/rumble work on b65e
> via host-side synthesis.** (ffctl was later retired into
> `ffpanel play` — `tools/ffpanel` — with identical flags.)

**Date:** 2026-08-18 · **Wheel:** T500RS standard rim, `044f:b65e` · **Capture:**
`raw_data/t500_verify_20260818.pcapng` (164 packets, tail truncated — tshark was
killed after the wedge) · **Driver:** `796eade` + dynamic debug

This is the execution of `12_hw_verification_procedure.md` §1 (P3-5). fftest was
used to upload a sine periodic effect while usbmon captured the wire.

## Verdict

🔴 **The periodic/ramp effect family is rejected by the firmware and wedges the
wheel.** FF_PERIODIC (all waveforms: square/sine/triangle/saw) and FF_RAMP are
now unadvertised in `t500rs_effects[]` until a real encoding is sourced from a
Windows capture.

## The evidence

Driver log (sequence for the sine, slot 1):

```
step 1/6 packet type 0x00  -> f26  41 01 00 01            STOP slot 1      OK
step 2/6 packet type 0x01  -> f28  42 05                   sync            OK
step 3/6 packet type 0x02  -> f30  42 04                   sync            OK
step 4/6 packet type 0x03  -> f32  02 38 00 00 00 00 00…  envelope slot 1 OK
step 5/6 packet type 0x05  -> f34  04 2a 7f 00 00 0a 00 00 periodic params OK
step 6/6 packet type 0x08  -> f36  01 01 22 40 20 4e e8 03 00 2a 00 38 00 00 00
                             *** -EPROTO here ***
```

After f36 every subsequent URB fails `-71`: constant envelope (f39 `02 1c…`),
spring/damper conditions (f42 `05 62…`, f45 `05 7e…`), STOPs for later slots
(f47 `41 05 00 01`, f51 `41 06 00 01`). The wheel then disappears from the bus
and re-enumerates as a new device instance (frames f109–f121 show `hid-tminit`
re-running its init on the re-enumerated device: `42 01…`, `0a 04…` bursts,
vendor requests `0x49`/`0x53`). This re-enumeration is the "driver crash"
reported by the user.

## Analysis

The failing MAIN packet f36 `01 01 22 40 … 2a 00 38 00 00 00` differs from the
capture-proven-good damper MAIN (C1 f73 `01 01 41 40 … 2a 00 38 …` — same slot,
same subtypes) by **exactly one byte: `b2` effect_type `0x22` vs `0x41`**.

Two hypotheses, indistinguishable from this log (the `-71` lands on the first
URB after the poison either way):

- **(a)** the firmware STALLs the MAIN packet because effect_type `0x22` is
  invalid/unsupported on this firmware;
- **(b)** the `04 2a …` periodic-params packet (step 5, itself unsourced — the
  layout's only reference is the example `04 2a 06 00 3f 0a 00 00`) corrupts
  firmware state and the *next* URB eats the error.

Both are periodic-path-only, so unadvertising the family fixes the wedge under
either hypothesis. Note steps 1–5 were individually *accepted* on the wire —
including `STOP(slot 1)` and both syncs — further confirming the per-slot
rework (P1-1) and sequence structure are sound.

Corroborating context: the earlier "game crashes the driver" regression (the
one attributed to the P3 batch) showed EPROTO on constant updates at 20 ms —
games upload periodic effects for road texture/engine vibes, so a periodic
upload early in the session would wedge the wheel exactly like this, with the
constant updates as collateral. That regression's timing (init-gain build)
remains best explained by the init byte, but the *mechanism* matches this wedge.

## ERRATUM (2026-08-18, same day — read this, it changes the conclusion)

The claim above that "the whole 0x20-0x24 family is unsourced (zero
appearances in any Windows capture)" is **wrong**. The pass-1 census
(`03_packet_inventory.md`) lists **C2 f2637: `01 00 22 40 ff ff 00 00 00
0e 00 1c 00 00 00` — a SINE (0x22) MAIN that played without error** during
the rFactor2 session. It was overlooked when this file was written.

Byte-for-byte against our wedging MAIN:

| field | Windows C2 f2637 (works) | our attempt (wedges) |
|-------|--------------------------|----------------------|
| b1 effect_id | `0x00` (slot 0) | `0x01` (slot 1) |
| b4-5 duration | `ffff` (infinite) | `4e20` (20000 ms) |
| b6-7 delay | `0000` | `03e8` (1000 ms) |
| b9-10 psub | `000e` (**constant channel**) | `002a` (slot-1 channel) |
| b11-12 esub | `001c` (**constant channel**) | `0038` (slot-1 channel) |

Combined with C2's 32 222 `04 0e` stream packets (b4 = signed level spanning
all 256 values, magic `0x2710`, `05_periodic_0x04_anomaly.md` Hypothesis B),
the correct model is:

**The firmware has no periodic waveform engine. Windows declares periodic
effects on slot 0 / constant channels `0e`/`1c` with an infinite MAIN, then
synthesizes the waveform host-side and streams constant-channel level updates
(`04 0e 00 00 <s8 level> 00 10 27`).** No per-slot periodic MAIN and no `04
2a`-style param packet appears in any capture because they are not part of
the real protocol.

Revised verdict: the wedge is caused by our *invented per-slot periodic
declaration* (`04 2a` params + MAIN on slot-1 channels), not by periodic
effects being fundamentally unsupported. Which exact byte STALLs (psub ≠ 0e,
finite duration, non-zero delay, or slot ≠ 0) is unknowable without more
hardware runs — but the correct implementation never sends any of them.

Implications:
- Periodic CAN be supported: replicate the C2 pattern (MAIN slot 0 / `0e`/`1c`
  / `ffff` / delay 0, type 0x22 for sine) and synthesize the waveform in
  software, streaming `04 0e` level packets from a timer worker (the parent
  already ships `linux/fixp-arith.h`).
- Restoring FF_PERIODIC in `t500rs_effects[]` also restores FF_RUMBLE
  automatically — the parent gates the rumble capability on FF_PERIODIC
  (`src/hid-tmff2.c:699`) and converts rumble → FF_PERIODIC/FF_SINE at
  upload (`src/hid-tmff2.c:473-496`).
- Caveat: the only sine-MAIN reference (C2) is b662 firmware (F1 rim); this
  wheel is b65e. The first hardware test of the replicated pattern must be
  treated as an experiment, not a certainty.

Negative result from a GitHub code search (2026-08-18): no independent T500RS
periodic wire encoding exists anywhere public (SDL/other drivers only match the
PID in joystick lists). There is nothing to port; a Windows capture of a real
periodic effect (e.g. via the Thrustmaster control panel test forces) is the
only path forward.

## Consequences

- **P3-5:** closed 🔴 — layout is not merely wrong, the family is dangerous.
- **P3-6 (ramp envelope):** blocked — FF_RAMP is unadvertised, so the ramp
  envelope path can no longer be exercised via the API.
- `docs/T500RS_FFBEFFECTS.md` effect-type table (`0x20`–`0x24`) is now known
  wrong-or-unusable on b65e firmware; needs a capture-derived rewrite before
  periodic support returns.
- Possible future hardening (NOT implemented, out of scope): `usb_clear_halt()`
  on `-EPROTO` in the send path so a single bad packet doesn't permanently
  wedge the endpoint.

## Raw extracts

```sh
tshark -r raw_data/t500_verify_20260818.pcapng -Y 'usbhid.data' \
  -T fields -e frame.number -e frame.time_relative -e usb.src \
  -e usb.dst -e usb.data_len -e usbhid.data
```

Full decode: `raw_data/t500_verify_20260818_out_frames.txt`.
