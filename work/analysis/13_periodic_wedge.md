# 13 — Periodic-effect family wedges the firmware (P3-5 verdict)

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
