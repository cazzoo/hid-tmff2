# 09 — Action items (prioritised)

Cross-reference between captures and `src/tmt500rs/hid-tmt500rs.c`. Each item lists
the problem, evidence, proposed change, risk, and verification procedure.

## Priority legend

| Priority | Meaning |
|----------|---------|
| 🔴 P1 | Likely bug; affects real users; well-evidenced by captures |
| 🟡 P2 | Likely missing feature or correctness issue; needs hw-verify |
| 🟢 P3 | Documentation / hygiene / minor enhancement |
| 🔵 P4 | Investigation only; no code change planned |

> **Second-pass additions (2026-08-12):** items **P1-2, P1-3, P2-5, P2-6, P2-7, P3-5,
> P3-6, P4-3** were added after exhaustive second-pass analysis — see
> `10_second_pass_findings.md`. Existing items were re-validated; the original
> effect_id bug (P1-1) is fully confirmed across all 8 `0x01` + 10 `0x41` packets.
>
> **Third-pass additions (2026-08-12, later):** items **P3-7, P4-4, P4-5** added after
> discovering that vendor request polling (`0x49`, `0x47`, etc.) is much more
> extensive than initially documented — see `11_vendor_request_polling.md`. Several
> pass-1/pass-2 claims about IN report `0x49` were corrected.

---

## 🔴 P1-1: Fix `effect_id` to use slot index, not hardcoded `0x00`

**Evidence:** `04_effect_id_bug.md`
- C1 f73: `01 01 41 40 ...` (effect_id=1, damper)
- C2 f2657, f373805: `01 01 41 40 ...` (effect_id=1, damper)
- C1 + C2 START/STOP: `41 01 41 01`, `41 01 00 01` (slot 1)

**Files to change:**
- `src/tmt500rs/hid-tmt500rs.h` — remove `T500RS_EFFECT_ID` macro, keep only `T500RS_AUTOCENTER_STOP_ID`
- `src/tmt500rs/hid-tmt500rs.c` — thread `effect_id` through:
  - `t500rs_send_packet_sequence()` (compute from slot index)
  - `t500rs_build_r01_main()` (already parametrised — stop hardcoding)
  - `t500rs_send_start()`, `t500rs_send_stop_now()`, `t500rs_send_stop()` (add `effect_id` param)
  - `t500rs_play_effect()`, `t500rs_stop_effect()` (pass the right ID per effect type)
- `docs/T500RS_FFBEFFECTS.md` — rewrite section 3 "the one rule that trips everyone up"

**Risk:** Medium. Existing games may have learned to live with the buggy IDs. The
"global STOP" hack in `t500rs_expiry_work` (lines 1395-1471) can be removed once this
is fixed, simplifying ~80 lines.

**Verification:** replay a known FFB-heavy game session (DiRT Rally 2.0 under Proton)
and confirm the produced packet sequence matches captures 1+2.

---

## 🟡 P2-1: Decide whether to support the `0x04`-with-code-`0x0e` constant-force form

**Evidence:** `05_periodic_0x04_anomaly.md`
- C2 has 32 222 `0x04` packets with `code=0x0e`, varying `b4`, magic `b6-b7=0x2710`
- Our driver never produces this; we use `0x03` for constant force

**Question to resolve first:** does the F1 rim accept our `0x03 0e 00 <level>` form?

**If yes:** no code change. Document that both forms work.

**If no:**
- Add an alternative `t500rs_send_constant_packet_alt()` that builds
  `04 0e 00 00 <level> 00 10 27`
- Select between them based on PID (`0xb65e` std rim vs `0xb662` F1 rim) or via
  module param
- This may explain any open "F1 rim no FFB" issues on the upstream repo

**Risk:** Low. Additive change behind a probe / module param.

**Verification:** manual `fftest` with F1 rim + USB capture.

---

## 🟡 P2-2: hw-verify the `delay`/`reserved1` field of `0x01` packet

**Evidence:** `03_packet_inventory.md`
- C1 f65: bytes 6-7 = `00 ff` (interpreted as delay=0xff00=65280ms), byte 8 = `ff`
- C1 f73: same
- C2 f2637: bytes 6-7 = `00 00`, byte 8 = `00` (clean)

Our struct says bytes 6-7 = `delay_ms` and byte 8 = `reserved1`. The C1 pattern
(`ff00`/`ff`) is implausible for delay and contradicts the "reserved" comment.

**Possible explanations:**
1. C1's game sent an actual 65s delay (unlikely)
2. Our struct is shifted by one byte for some firmware versions
3. C1's source game (unknown) deliberately sets a "magic" delay value

**Action:** no code change until verified. Add a `// CAPTURE-VERIFY:` comment block
referencing the discrepancy.

**Verification:** upload a constant effect with `delay=1000` via `fftest` on Linux
and confirm the resulting bytes are `e8 03 00` (1000ms LE + reserved 0).

---

## 🟡 P2-3: Init sequence: consider matching Windows more closely

**Evidence:** `02_init_sequence_diffs.md`

Three deltas vs. Windows capture 2:
1. We send `40 11 42 7b` (FFB-arm magic) — Windows sends `40 11 55 55` (range init)
2. We don't send `40 03 0d 00` (default autocenter strength)
3. We send `43 ff` (gain 100%) — Windows sends `43 5a` (gain 90%)

**Proposed change:**
- Keep `40 11 42 7b` (mandatory per existing comment) but ADD a `40 11 55 55` default
  range OR call `set_range(default_range)` after init
- Optionally add `40 03 0d 00` for autocenter strength default 13%
- Change `43 ff` → `43 5a` (safer default, matches Windows)

**Risk:** Low. Each change is a single byte value.

**Verification:** test that the wheel still initializes and responds to `set_gain`
/ `set_range` callbacks after.

---

## 🟡 P2-4: Verify the F1 rim's HID descriptor is properly parsed

**Evidence:** `06_unknown_reports.md`
- C2 IN traffic uses report ID `0x14` at 230 Hz, but our driver has no `report_fixup`
  or `raw_event` handler — we rely on stock HID parsing

**Risk:** if the F1 rim's report descriptor maps axes differently from the standard
rim, users see mis-mapped pedals/buttons.

**Action:** capture the HID report descriptor (`0x22`) for both PIDs (`0xb65e` and
`0xb662`) on Linux using `usbhid-dump`, compare, and document differences.

**Files to add:** `docs/T500RS_HID_Descriptors.md` with both descriptors + decode.

---

## 🟢 P3-1: Document the boot-mode-switch dependency clearly

**Evidence:** `01_boot_mode_switch.md`

**Action:** update `README.md` to explicitly say T500RS (not just TX/TS-XW) requires
`hid-tminit` or mainline `hid-thrustmaster` to leave boot mode. Currently the README
implies this is optional.

**Risk:** None — documentation only.

---

## 🟢 P3-2: Add a CAPTURE-VERIFY comment block for condition deadband/coeff

**Evidence:** `07_condition_deadband_unverified.md`

**Action:** add structured comments at `hid-tmt500rs.c:482` (deadband) and
`hid-tmt500rs.c:463-468` (coefficients) referencing the test plan in
`07_condition_deadband_unverified.md`.

**Risk:** None — comments only.

---

## 🟢 P3-3: Make init gain configurable — ✅ DONE (798ba2b)

**Evidence:** `02_init_sequence_diffs.md` — Windows uses 90%, we use 100%.

**Action:** add module param `default_gain` (default 100 for backwards compat),
use it at `hid-tmt500rs.c:1981` instead of hardcoded `0xff`.

**Risk:** None — backwards-compatible default.

**Status:** Implemented as specified. `default_gain` (0-100, default 100 →
byte `0xff`, wire-identical to the historical init). Hardware-verified stable.

**⚠️ Regression history:** the first implementation (76998f5, reverted)
deviated from this spec by reusing the shared `gain` param (default 40000 →
byte `0x9b`), silently changing the default init wire bytes; the user
reported driver crashes (URB storm → device drop) with that build. The
init-gain byte was the only guaranteed code delta between the crashing
build and the stable re-implementation — treat the init 0x43 byte as
effectively frozen at `0xff` unless a capture says otherwise.

---

## 🟢 P3-4: Document unknown reports in driver source

**Evidence:** `06_unknown_reports.md`

**Action:** add a comment block at the top of `hid-tmt500rs.c` listing the
unhandled report IDs (`0x0a` OUT, `0x14` IN, `0x49` IN) with a pointer to
`work/analysis/06_unknown_reports.md`.

**Risk:** None.

---

## 🔵 P4-1: Investigate the model-byte → PID mapping for T500RS variants

**Question:** what model byte does the standard T500RS rim return from vendor request
`0x56`? The F1 rim returns `0x2b`. Capture 1 doesn't show this (wheel was already in
advanced mode at capture start).

**Action:** on Linux, before `hid-tminit` runs, capture the boot-mode vendor
requests with `usbmon`. Identify the standard rim's model byte and confirm it maps
to PID `0xb65e`.

**Why it matters:** helps `hid-tminit` maintainers extend their wheel-support table.

---

## 🔵 P4-2: Reverse-engineer the unknown boot-mode vendor requests

**Evidence:** `01_boot_mode_switch.md`
- `0x42` returns 3 bytes (`42 e8 03`)
- `0x4e` returns 2 bytes (`4e 14`)

These are not identified in the captures. They might be:
- `0x42` — bootloader version / CRC
- `0x4e` — hardware revision

**Action:** run these against `lsusb -vvv` output and mainline `hid-thrustmaster.c`
source to see if they're decoded there. If not, capture more samples from different
firmware versions and try to infer field meanings.

---

## 🔴 P1-2: Fix `0x41` START arg byte (hardcoded `0x01`, should be `0xff`)

**Evidence:** `10_second_pass_findings.md` §2
- C2 f2651: `41 00 41 ff` (START slot 0, **arg=0xff**)
- C2 f2659: `41 01 41 ff` (START slot 1, **arg=0xff**)
- C1 f67, f75: `41 0X 41 01` (START, arg=0x01)
- C1 + C2 STOPs always use arg=0x01

Our driver hardcodes `arg=0x01` at `hid-tmt500rs.c:1066` (stop_now) and `:1101`
(start). Real protocol uses `0xff` for START in rFactor2, `0x01` for STOP everywhere.

**Hypothesis:** arg byte encodes per-effect start gain (0..255). `0xff` = "start at
full strength"; `0x01` = "start with minimal strength" (C1's choice for some reason).

**Files to change:**
- `hid-tmt500rs.c:1088-1103` (`t500rs_send_start`) — add `u8 start_gain` param
- `hid-tmt500rs.c:1477-1537` (`t500rs_play_effect`) — pass through user-controlled
  gain (or default to `0xff`)
- `hid-tmt500rs.h:208` — update `struct t500rs_r41_cmd` comment

**Risk:** Low — single byte change. Default `0xff` matches the dominant Windows pattern.

**Verification:** replay rFactor2 session, confirm produced `41 0X 41 ff` matches.

---

## 🔴 P1-3: Remove the `40 11 42 7b` "FFB-arm magic" from init

**Evidence:** `10_second_pass_findings.md` §5
- C1: ZERO `0x40` packets — Windows doesn't send this command at all
- C2: only `40 11 55 55` (range=364°) — different data
- Our driver: `40 11 42 7b` (line 1944-1950), data 0x7b42 = 31554 → range 526°

Our "FFB-arm magic" is actually a **range command setting 526°**. The driver comment
("magic value seen in captures that enables FFB on the base") is mistaken — that
capture evidence doesn't exist in either community capture.

**Files to change:**
- `hid-tmt500rs.c:1937-1955` — remove the entire `0x40 0x11 0x42 0x7b` block
- Optionally: add a `t500rs_set_range(default_range)` call to set a sensible default
  (e.g. 900° or 1080°) at init, matching what Windows does (`40 11 55 55` for F1)

**Risk:** Medium. The driver has been "working" with this packet for a while — it may
be load-bearing on some firmware versions even though Windows doesn't send it. Test
carefully on hardware.

**Verification:**
1. Boot T500RS on Linux, run `fftest`, confirm FFB works without `40 11 42 7b`.
2. If it doesn't, the packet IS needed — keep it but rename to "range init" not "magic".

---

## 🟡 P2-5: Send `42 05` apply 3× in a row during uploads

**Evidence:** `10_second_pass_findings.md` §10
- C2 frames 2639/2641/2643: three consecutive `42 05` packets
- C1 frames 57/59: two consecutive `42 05` packets
- Our driver: only one `42 05` per sequence step

The redundancy likely improves reliability against dropped packets. May also force
the device to commit through multiple "apply gates".

**Files to change:** `hid-tmt500rs.c:840-851` (the `T500RS_SEQ_SYNC_42_05` case) —
send 3× in a loop instead of once.

**Risk:** Low — additive change.

---

## 🟡 P2-6: Periodic re-arm of autocenter-disable

**Evidence:** `10_second_pass_findings.md` §8
- C2 sends `40 04 00 00` (disable AC) at t=11.67, 12.42, and 43.42 — **three times
  in 32 seconds**
- Driver sends it once at probe time

Hypothesis: the device's autocenter state drifts back to "enabled" over time,
requiring periodic re-arm. Could explain user reports of "wheel develops centering
force after some time" on Linux.

**Files to change:** add a `delayed_work` that re-sends `40 04 00 00` every ~30s
when FFB is armed but no effect is playing.

**Risk:** Low — additive change. Skip when effects are playing (don't disturb live FFB).

---

## 🟡 P2-7: Investigate MAIN upload re-sending after `42 05`

**Evidence:** `10_second_pass_findings.md` §10 (C2 sequence)
```
f2637  01 00 22 40 ...  # MAIN slot 0 SINE
f2641  42 05            # apply
f2643  42 05            # apply
f2644  02 1c 00 ...     # envelope slot 0
f2649  01 00 22 40 ...  # MAIN slot 0 SINE — REPEATED
```

C2 sends MAIN twice, with `42 05` between. The `42 05` apply may invalidate the
slot's parameter binding, requiring MAIN re-send to re-establish it.

**Action:** no code change yet. Add as a TODO comment in
`t500rs_send_packet_sequence()` near the `T500RS_SEQ_MAIN` case.

**Risk:** None — documentation only.

---

## 🟢 P3-5: Verify periodic support (`0x04` with code != 0x0e)

**Evidence:** `10_second_pass_findings.md` §9
- ZERO `0x04` packets with code != 0x0e in either capture
- Driver's `t500rs_build_r04_periodic()` is based on an unsourced internal example
  (`04 2a 06 00 3f 0a 00 00`)

The driver's periodic effect support has **no captured reference**. We don't know if
our struct layout is right.

**Action:** hw-verify with `fftest`:
```bash
fftest /dev/input/eventXX
# Select sine periodic effect with magnitude > 0, period 100ms
# Capture USB traffic with usbmon + wireshark
# Compare to our struct
```

If our layout is wrong, periodic effects will misbehave but won't crash — they'd
produce wrong frequencies/magnitudes.

**Risk:** Low priority until a user reports broken periodic effects.

---

## 🟢 P3-6: Verify ramp-envelope support

**Evidence:** `10_second_pass_findings.md` §4
- ZERO non-zero `0x02` envelope packets in either capture
- Driver's `t500rs_build_r02_envelope()` supports non-zero envelopes for FF_RAMP
  (per `t500rs_send_envelope_packet` line 690-704)

The doc claim "only ramps use real envelope values" is **plausible but unproven**.

**Action:** hw-verify with `fftest` (ramp with attack/fade).

**Risk:** None until reported.

---

## 🔵 P4-3: Decode the `0x14` identification IN report

**Evidence:** `10_second_pass_findings.md` §7 (0x14 report)

Three unique 27-byte payloads seen during C2 init:
- `14 20 90 03 01 39 74 05 00 ...`
- `14 20 12 10 2b 00 5e b6 00 ...` ← `0x5e 0xb6` LE = `0xb65e` (std rim PID)!
- `14 20 00 06 18 28 00 00 00 ...`

The `0xb65e` appearance in an F1-rim (`0xb662`) session is interesting. Likely
encodes (base_wheel_PID, attachment_ID, firmware_version, hardware_revision).

**Action:** correlate with vendor request `0x56` replies (`56 2b 00 00`) — the `0x2b`
byte appears in the `14 20 12 10 2b 00` payload too. Document the mapping.

**Why it matters:** could enable automatic F1/standard rim detection at probe time.

---

## 🟢 P3-7: Correct the `0x49` misidentification in pass-1 docs

**Evidence:** `11_vendor_request_polling.md`

Pass 1 claimed `0x49` was an unsolicited IN report at ~1/min. It's actually a
**vendor request reply** (Windows polls `0x49` 24× in 200ms during C1's init).

**Action:** rewrite `06_unknown_reports.md` `0x49` section. Move from "IN report" to
"vendor request reply". Add new section listing all vendor requests observed
(`0x49`, `0x47`, `0x56`, `0x55`, `0x48`, `0x42`, `0x4e`, `0x53`).

**Risk:** None — documentation only.

---

## 🔵 P4-4: Decode the `0x49` vendor request reply

**Evidence:** `11_vendor_request_polling.md`

C1's `0x49` reply (constant across all 24 requests):
```
49 00 03 04 01 00 0a 00 03 00 00 00 02 02 00 00
```

This is likely a **device-capability descriptor** read by Windows during init to
enable specific features. Could contain firmware version, hardware revision, feature
flags.

**Action:** on Linux, send `0xc1 0x49 wValue=0 wLength=16` via `lsusb -vvv` or a
custom hidraw tool. Decode the reply fields. Compare across wheel modes / rim types.

**Why it matters:** if specific bits encode F1/standard rim, button layout, or
advanced FFB mode, our driver could read this at probe time for auto-detection.

---

## 🔵 P4-5: Investigate F1-specific vendor requests (`0x55`, `0x48`, `0x41 0x48 0x40`)

**Evidence:** `11_vendor_request_polling.md`

C2 advanced-mode init sends three vendor requests never seen elsewhere:
- `0xc1 0x55 wLength=0x10` (frame 171) — read 16 bytes
- `0xc1 0x48 wLength=0x40` (frame 173) — read **64 bytes** (largest vendor reply)
- `0x41 0x48 0x40 ...` (frame 211) — vendor OUT, sends 64 bytes back

These appear once each, immediately after the advanced-mode switch. The
`0x41 0x48 0x40` (OUT) is especially interesting: the host reads 64 bytes via `0x48`,
modifies them (the high byte changes to `0x40`), then writes them back. This pattern
typically indicates **device-feature configuration**: read current state, modify a
field, write back.

**Hypothesis:** this activates F1-rim-specific features (button mapping, display
protocol, force-feedback mode).

**Action:** on Linux with F1 rim attached, try sending these vendor requests via
`usb_control_msg()` and observe behavior. If F1 features (extra buttons, display)
don't work without them, add to `t500rs_wheel_init()` for F1-rim PIDs only.

**Why it matters:** could fix missing F1 features on Linux.

---

## Summary table

| ID | Pri | Effort | Risk | Component |
|----|-----|--------|------|-----------|
| P1-1 | 🔴 | M | M | driver core (effect_id slot mapping) |
| **P1-2** | **🔴** | **S** | **L** | **driver core (0x41 START arg byte)** |
| **P1-3** | **🔴** | **S** | **M** | **init sequence (remove 40 11 42 7b)** |
| P2-1 | 🟡 | M | L | driver core (constant force via 0x04) |
| P2-2 | 🟡 | S | L | driver core (delay field) |
| P2-3 | 🟡 | S | L | init sequence |
| P2-4 | 🟡 | M | L | HID descriptor parsing |
| **P2-5** | **🟡** | **S** | **L** | **send 42 05 ×3** |
| **P2-6** | **🟡** | **M** | **L** | **periodic autocenter re-arm** |
| **P2-7** | **🟡** | **S** | **none** | **MAIN re-send TODO** |
| P3-1 | 🟢 | S | none | README |
| P3-2 | 🟢 | S | none | source comments |
| P3-3 | 🟢 | S | none | module param |
| P3-4 | 🟢 | S | none | source comments |
| **P3-5** | **🟢** | **M** | **none** | **verify 0x04 periodic support** |
| **P3-6** | **🟢** | **M** | **none** | **verify ramp envelope support** |
| P4-1 | 🔵 | M | none | investigation |
| P4-2 | 🔵 | M | none | investigation |
| **P4-3** | **🔵** | **M** | **none** | **decode 0x14 ID report** |

## Suggested order of operations

1. **P3-2, P3-4** — cheap, prepare the ground (comments + docs)
2. **P2-2, P2-4, P3-5, P3-6** — hw-verification tasks (no code change, reduce uncertainty)
3. **P1-1, P1-2** — main bug fixes (only after P2-2 confirms the struct layout)
4. **P1-3** — remove the suspect "FFB-arm magic" (test carefully on hardware)
5. **P2-1** — only if P2-4 reveals F1-rim issue with `0x03`
6. **P2-3, P2-5, P2-6, P2-7, P3-3** — polish / Windows-parity tweaks
7. **P3-1** — README update covering what we learned
8. **P4-1, P4-2, P4-3** — open questions for future investigation

Items 1-3 can be done in parallel by separate contributors. Items 4-7 are sequential
or independent.
