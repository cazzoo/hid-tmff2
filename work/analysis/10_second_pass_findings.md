# 10 — Second-pass findings and corrections

> **Purpose:** validate or refute pass-1 assumptions and document everything found
> after exhaustive second-pass analysis. This file supersedes incorrect claims in
> `00_capture_inventory.md` (IN report IDs) and `06_unknown_reports.md` (state report).
> Read this BEFORE the pass-1 docs.

## TL;DR of corrections to pass 1

| # | Pass-1 claim | Status | Correction |
|---|--------------|--------|------------|
| 1 | C2 dominant IN report is `0x14` at 230 Hz | ❌ WRONG | It's **`0x07`** at 230 Hz (152 549 packets). `0x14` is rare (6 packets). |
| 2 | C2 also has `0x49` IN reports (low-rate) | ❌ WRONG | C2 has **ZERO `0x49`** packets. The `0x49` is C1-only. |
| 3 | C1 has `0x49` IN reports at low rate | ⚠️ PARTIAL | True, but C1 has **NO interrupt-IN traffic at all** (only control-endpoint IN). C1 is incomplete for IN analysis. |
| 4 | `0x41` arg byte is always `0x01` | ❌ WRONG | C2 rFactor2 uses **`0xff`** for START. Driver hardcodes `0x01`. |
| 5 | STOP is global (`41 00 00 01`) | ❌ WRONG | STOP is **per-slot** in C1 (`41 00 00 01` + `41 01 00 01`). C2 only stops slot 0 explicitly. |
| 6 | Driver's `40 11 42 7b` is "FFB-arm magic" | ❌ LIKELY WRONG | In NEITHER capture. C2 sends `40 11 55 55` (range=364°). Our "magic" is probably setting a non-standard range. |
| 7 | Init runs once | ❌ WRONG | C2 init runs **3 times** (t=11.6, t=12.4, t=43.4) plus a 32-byte reset pair at t=42.4. |

## TL;DR of new findings

| # | Finding | Impact |
|---|---------|--------|
| A | Effect_id == slot_index (validated across all 8 `0x01` and 10 `0x41` packets) | Confirms P1-1 |
| B | 0x41 START uses arg=0xff in C2 (driver hardcodes 0x01) | **New P2 bug** |
| C | STOP is per-slot — driver's "global STOP" hack (~80 lines) is built on wrong assumption | Confirms P1-1 fix unblocks major simplification |
| D | 0x04 with code=0x0e: 100% confirmed as constant-force-via-periodic (b4 = signed level, b6/b7 = magic 0x2710) | Confirms Hypothesis B in `05_periodic_0x04_anomaly.md` |
| E | ZERO `0x04` with code != 0x0e in either capture | Driver's periodic support (`04 2a ...`) is **unverified against any real capture** |
| F | C1 has ZERO `0x40`, ZERO `0x43`, ZERO `0x0a` | C1 is a minimal-protocol session; driver was tuned to a degenerate case |
| G | C2 init runs 3 times with a 32-byte reset pair in the middle | Driver does init once; may need re-init logic |
| H | URB errors: ZERO in both captures | Protocol is well-formed; any transfer errors in our driver are bugs on our side |
| I | 32-byte variants of `42 05`/`42 00` exist as "full reset" commands | Driver doesn't send these |
| J | Multi-effect upload: MAIN can come before OR after parameters (both captures prove it) | Driver's fixed sequence isn't the only valid one |
| K | C1 0x05 condition packets have ALL-ZERO coefficients but max saturation | Game-specific or "use firmware defaults" semantics |
| L | C2 STOP leaves slot 1 playing (never explicitly stopped) | Suggests per-slot lifecycle is independent; games manage them separately |
| M | `42 05` (apply) is sent 2-3 times in a row in both captures | Driver sends once; redundancy may matter |
| N | C2 sends `0x01 MAIN` upload twice in a row with `42 05` between them | Possibly to commit changes through the apply gate |

---

## 1. Effect_id validation (P1-1 confirmed)

All 8 `0x01` packets across both captures:

| Source | Bytes | effect_id | type | param_sub | env_sub | slot n |
|--------|-------|-----------|------|-----------|---------|--------|
| C1 f65 | `01 00 00 40 ...` | 0 | CONSTANT (0x00) | 0x000e | 0x001c | 0 |
| C1 f73 | `01 01 41 40 ...` | **1** | DAMPER (0x41) | 0x002a | 0x0038 | **1** |
| C2 f2637, f2649 | `01 00 22 40 ...` | 0 | SINE (0x22) | 0x000e | 0x001c | 0 |
| C2 f2657, f373805 | `01 01 41 40 ...` | **1** | DAMPER (0x41) | 0x002a | 0x0038 | **1** |

**Slot 0 is NOT reserved for constant force.** In C2 it's reused for SINE. The slot
assignment is "first-come first-served" — whatever effect the game uploads first gets
slot 0 with subtypes `0x000e`/`0x001c`.

This refines the driver doc's rule. The correct statement is:

> Each effect occupies a slot. The slot's `effect_id` byte equals its index (0, 1, 2,
> ...). Slot 0 uses subtypes `0x000e`/`0x001c`, slot 1 uses `0x002a`/`0x0038`, etc.
> The constant-force path in this driver always uses slot 0; non-constant effects get
> slots 1..14. Games may put any effect in any slot, but our convention works because
> the framework deduplicates effect IDs.

## 2. 0x41 START/STOP full inventory

All 10 `0x41` packets across both captures:

| Source | Bytes | Decoded | Context |
|--------|-------|---------|---------|
| C1 f67 | `41 00 41 01` | START slot 0, **arg=0x01** | After constant upload |
| C1 f75 | `41 01 41 01` | START slot 1, **arg=0x01** | After damper upload |
| C1 f135803 | `41 00 00 01` | STOP slot 0, arg=0x01 | End of session |
| C1 f135809 | `41 01 00 01` | STOP slot 1, arg=0x01 | 11 ms after slot 0 stop |
| C2 f2651 | `41 00 41 ff` | START slot 0, **arg=0xff** | After sine upload |
| C2 f2659 | `41 01 41 ff` | START slot 1, **arg=0xff** | After damper upload |
| C2 f373671 | `41 00 00 01` | STOP slot 0, arg=0x01 | End of race 1 |
| C2 f373753 | `41 00 41 ff` | START slot 0, arg=0xff | Race 2 begin |
| C2 f373823 | `41 01 41 ff` | START slot 1, arg=0xff | Race 2 begin |
| C2 f374311 | `41 00 00 01` | STOP slot 0, arg=0x01 | End of race 2 |

### Findings

**A. STOP is per-slot.** Both captures send `41 XX 00 01` for each slot's effect_id.
Our driver's "global STOP" theory (in `t500rs_expiry_work` at lines 1395-1471) is
based on a misreading — the original concern ("global STOP needed because effect_id is
always 0") is invalid because effect_id is NOT always 0.

**B. The arg byte (b3) is `0xff` for START in C2, `0x01` for STOP.**
Our driver hardcodes `0x01` everywhere. The C2 arg byte may mean:
- `0x01` = single-shot / start with default gain
- `0xff` = start with full gain / "infinite" repetitions

OR it could be a per-effect gain scaling factor (0-255).

**C. C2 never explicitly stops slot 1.** After race 1 ends (f373671 stops slot 0), slot
1's damper is left playing. When race 2 starts (f373753 START slot 0, f373823 START
slot 1), slot 1 gets re-STARTed without being STOPped first.

This suggests:
- Each slot's effect lifecycle is independent
- Re-STARTing an already-playing slot just resets its playback
- The game (rFactor2) doesn't bother STOPping because it knows it'll re-START soon

**Implication for the driver:** the "global STOP when nothing is playing" guard at
`hid-tmt500rs.c:1469-1470` is unnecessary. Each effect's STOP should be independent.

## 3. 0x05 condition packets — full bodies

All 6 `0x05` packets:

| Source | Bytes (11) | right_coeff | left_coeff | center | deadband | right_sat | left_sat |
|--------|------------|-------------|------------|--------|----------|-----------|----------|
| C1 f69 | `05 2a 00 00 00 00 00 00 00 64 64` | 0 | 0 | 0 | 0 | 100 | 100 |
| C1 f71 | `05 38 00 00 00 00 00 00 00 64 64` | 0 | 0 | 0 | 0 | 100 | 100 |
| C2 f2653 | `05 2a 00 0a 0a 00 00 00 00 0a 0a` | **10** | **10** | 0 | 0 | **10** | **10** |
| C2 f2655 | `05 38 00 00 00 00 00 00 00 64 64` | 0 | 0 | 0 | 0 | 100 | 100 |
| C2 f373769 | `05 2a 00 0a 0a 00 00 00 00 0a 0a` | 10 | 10 | 0 | 0 | 10 | 10 |
| C2 f373787 | `05 38 00 00 00 00 00 00 00 64 64` | 0 | 0 | 0 | 0 | 100 | 100 |

### Findings

**A. Y-axis (code `0x38`) is always zero coefficients, max saturation in both captures.**
This is consistent with the wheel being single-axis: the Y-axis condition is uploaded
as a formality but does nothing.

**B. C1's X-axis (code `0x2a`) is also zero coefficients.** The C1 game uploaded a
damper with zero strength — probably just to declare the slot, not to apply real
damping. This is a degenerate test session.

**C. C2's X-axis has coeff=10 (max in 0-10 scale) and sat=10 (low).**
rFactor2 used a high-strength damper with low saturation cap. The asymmetric scaling
in our driver (`coeff * 10 / 32767` vs `sat * 100 / 65535`) means same input value
yields very different device bytes. **Verifies our scaling formulas are at least
internally consistent and produce values in the right ranges.**

**D. Deadband is always 0 in every capture.** The `/65` divisor remains unverified.

## 4. 0x02 envelope packets — all zeros

All 5 `0x02` packets: `02 1c 00 00 00 00 00 00 00` (subtype `0x1c` = slot 0 envelope).

**No non-zero envelope has ever been captured.** The driver's ramp-envelope support
(per `t500rs_send_envelope_packet` for `FF_RAMP`) is **completely unverified** against
real device behavior. The doc's claim "only ramps use real envelope values" is
plausible but unproven.

## 5. 0x40 subcommand inventory

**C1: ZERO `0x40` packets.** The C1 game set no range, no autocenter.

**C2: 7 `0x40` packets, 3 unique:**
| Bytes | Count | Meaning |
|-------|-------|---------|
| `40 11 55 55` | 1 | Range: 0x5555 / 60 = **364°** (F1-typical) |
| `40 04 00 00` | 3 | Disable autocenter |
| `40 03 0d 00` | 3 | Set autocenter strength = 13% |

### Critical finding: our `0x40 0x11 0x42 0x7b` is wrong

Our driver's init sends `0x40 0x11 0x42 0x7b` at line 1944-1950, calling it "magic
value that enables FFB on the base". But:

1. **This packet is in NEITHER capture** — Windows never sends `0x40 0x11 0x42 0x7b`.
2. **Subcommand `0x11` is the RANGE command** (per `t500rs_set_range()` line 1785).
3. Our packet's data `0x42 0x7b` (LE) = `0x7b42` = 31554. As a range value:
   `31554 / 60 = 526°` — a real but non-standard range.
4. **C2's `0x40 0x11 0x55 0x55` is also a range command** — data `0x5555` = 21845,
   `21845 / 60 = 364°` (the actual F1 range).

**Conclusion:** our `0x40 0x11 0x42 0x7b` is NOT an "FFB-arm magic" — it's a range
command that sets the wheel to 526° at init. The driver comment at line 1937-1941
("magic value seen in captures that enables FFB") is mistaken.

**Implication:** removing this packet from init should not break FFB (Windows doesn't
send it). But we should test on hardware to be sure.

## 6. 0x42 sync packet inventory

**C1: 6 packets, all 2-byte:**
- `42 04` ×1, `42 05` ×4, `42 00` ×1

**C2: 21 packets, 6 distinct length/prefix combinations:**

| Length | Bytes | Count | Notes |
|--------|-------|-------|-------|
| 2 | `42 04` | 3 | Opening sync |
| 2 | `42 05` | 11 | Apply (sometimes 3× in a row) |
| 2 | `42 00` | 3 | Closing sync |
| 9 | `42 01 00 00 00 00 00 00 00` | 1 | Reset (mid-init) |
| 15 | `42 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00` | 1 | Reset (longer) |
| 32 | `42 05 00 ... 00` | 1 | **Full reset** at t=42.4s |
| 32 | `42 00 00 ... 00` | 1 | **Full reset** at t=42.4s |

### Findings

**A. `42 05` is sent multiple times consecutively** (up to 3× in a row, frames
2639/2641/2643). The driver only sends it once. This redundancy may improve
reliability.

**B. 32-byte variants exist as "full reset" commands.** They appear once at t=42.4s,
right before the third init sequence. They probably clear all effect slots and reset
the FFB state machine.

**C. The 9-byte and 15-byte `42 01 00 ...` variants** appear during init only (t=8.9
and t=9.8). They're not in C1. May be F1-rim specific.

## 7. IN reports — corrected

### Capture 1

**ZERO interrupt-IN traffic on endpoint 0x82.** Only 26 control-endpoint IN packets
(report `0x49`, 16 bytes each). C1 is **incomplete for IN analysis** — USBPcap didn't
capture interrupt URB completions for IN, or the game didn't poll the wheel.

### Capture 2 (corrected)

| Report ID | Count | Length | Purpose |
|-----------|-------|--------|---------|
| `0x07` | **152 549** | 15 B | **Main state report** at ~230 Hz (axes + buttons) |
| `0x14` | 6 | 27 B (×3) or 15 B (×3) | **Device identification** during init only |
| `0x02` | 1 | (control endpoint response, not state) | boot-mode reply |

### 0x07 state report byte distribution (152 549 packets analyzed)

| Byte | Distribution | Likely field |
|------|--------------|--------------|
| b0 | `0x07` always | Report ID |
| b1 | widely varied (top values: fb, 14, f7, 00, 04, ef, 0c, 1c) | **Buttons bitfield** |
| b2 | bimodal `0x80` (14 639) + `0x7f` (12 036), spread to `0x81-0x87` | **Wheel X axis** (signed, centered) |
| b3 | `0xff` (131 943) mostly, `0x00` (3 913) rare | Buttons continued or high-byte of axis |
| b4 | `0x03` (134 096) mostly, some 0/1/2 | High-precision X bits or another axis |
| b5 | `0xff` always | Constant / sync marker |
| b6 | `0x03` always | Constant / sync marker |
| b7 | bimodal `0x00` (61 929) + `0xff` (47 256) | **Pedal axis** (gas/brake centered) |
| b8 | `0x00` / `0x03` / `0x02` / `0x01` (low cardinality) | Hat switch or shifter |
| b9, b10 | `0x00` always | Reserved |
| b11 | mostly `0x00`, rare `0x01`/`0x02` | Third axis (clutch?) |
| b12 | mostly `0x00`, rare `0x20` | Flags |
| b13 | `0x00` always | Reserved |
| b14 | `0xf0` always | **Sync marker / report version** |

**Note:** without the HID report descriptor (not captured), this decode is conjectural.
The driver relies on stock HID parsing and doesn't decode this manually.

### 0x14 IN report (6 packets, init only)

Three unique payloads (27-byte and 15-byte versions of each):
- `14 20 90 03 01 39 74 05 00 00 ...` — contains `0x0390` and `0x0574 0x39 0x01`
- `14 20 12 10 2b 00 5e b6 00 00 ...` — contains `0x1012`, `0x002b`, **`0xb65e`** (std rim PID!)
- `14 20 00 06 18 28 00 00 00 00 ...` — contains `0x0600`, `0x2818`

The `0x5e 0xb6` LE = `0xb65e` (T500RS standard-rim PID) appears in the F1-rim's
identification report. Suggests the F1 attachment reports the base wheel's PID, not
the F1-specific PID `0xb662`. The report likely encodes (base_PID, attachment_ID,
firmware_version, hardware_revision).

## 8. Init runs 3 times in C2

| Time | Sequence | Notes |
|------|----------|-------|
| t=11.67 s | `42 04 / 40 04 00 00 / 40 03 0d 00 / 43 5a / 42 05 / 42 00` | Initial setup |
| t=12.42 s | `42 04 / 40 04 00 00 / 40 03 0d 00` | Repeated setup (1s later) |
| t=42.42 s | `42 05 00...00 (32B) / 42 00 00...00 (32B)` | **Full reset** |
| t=43.42 s | `42 04 / 40 04 00 00 / 40 03 0d 00 / 42 05` | Repeated setup (1s after reset) |

The Windows driver re-runs the autocenter disable + strength setting **3 times in 32
seconds**. The middle re-init is preceded by a 32-byte reset pair.

**Implication:** the device's autocenter state may revert over time, requiring periodic
re-arm. Our driver does this once at probe and never again. Could explain user reports
of "wheel develops centering force after some time".

## 9. 0x04 with code=0x0e — fully decoded

Across all 32 222 packets:
- **b2 always 0x00** (32 222 / 32 222)
- **b3 always 0x00** (32 222 / 32 222)
- **b4 spans all 256 values**, with Gaussian distribution centered near 0
  - 21 432 packets in range `0x00-0x7f` (positive)
  - 10 790 packets in range `0x80-0xff` (negative)
  - Top values: `0x01` (670), `0x02` (666), `0x00` (620), `0xff` (608), `0xfe` (568)
- **b5 always 0x00** (32 222 / 32 222)
- **b6 always 0x10** (32 222 / 32 222)
- **b7 always 0x27** (32 222 / 32 222)

**This 100% confirms the alternative layout (Hypothesis B) in `05_periodic_0x04_anomaly.md`:**

```c
struct t500rs_pkt_r04_constant_alt {  /* NOT in driver */
    u8 id;           /* b0 = 0x04 */
    u8 code;         /* b1 = 0x0e — selects constant-force slot via periodic ID */
    u8 zero1;        /* b2 = 0x00 */
    u8 zero2;        /* b3 = 0x00 */
    s8 level;        /* b4 = signed force level -128..+127 — the FFB signal */
    u8 zero3;        /* b5 = 0x00 */
    __le16 magic;    /* b6-b7 = 0x2710 LE = 10000 — "DC mode" marker */
} __packed;
```

**The driver's existing `t500rs_pkt_r04_periodic_ramp` struct is WRONG for this case.**
It interprets b4 as `phase` (u8) and b6-b7 as `period_ms` (LE u16 = 0x1000 = 4096ms)
and b7 separately as `reserved` (should be 0x00 but is 0x27).

### Are there any 0x04 with code != 0x0e?

**NO.** Both captures: 0 packets. The driver's periodic builder (`t500rs_build_r04_periodic`)
is **based on an unverified internal example** (`04 2a 06 00 3f 0a 00 00`). We have no
real-world capture proving that layout.

## 10. Multi-effect upload sequence (full decode)

### C1 (frames 55-75, t=960.7-980.8s)
```
42 04                                  # opening sync
42 05                                  # apply
42 05                                  # apply (2nd)
02 1c 00 00 00 00 00 00 00             # envelope slot 0 (zeros)
03 0e 00 00                            # constant level slot 0 = 0
01 00 00 40 ff ff 00 ff ff 0e 00 1c 00 00 00  # MAIN slot 0 CONSTANT
41 00 41 01                            # START slot 0
05 2a 00 00 00 00 00 00 00 64 64       # condition X slot 1 (zeros, sat=max)
05 38 00 00 00 00 00 00 00 64 64       # condition Y slot 1 (zeros, sat=max)
01 01 41 40 ff ff 00 ff ff 2a 00 38 00 00 00  # MAIN slot 1 DAMPER
41 01 41 01                            # START slot 1
```

**Order: parameters → MAIN → START.** This is REVERSE of our driver's sequence.

### C2 (frames 2637-2659, t=109.0-109.1s)
```
01 00 22 40 ff ff 00 00 00 0e 00 1c 00 00 00  # MAIN slot 0 SINE
42 05                                  # apply
42 05                                  # apply
02 1c 00 00 00 00 00 00 00             # envelope slot 0 (zeros)
01 00 22 40 ff ff 00 00 00 0e 00 1c 00 00 00  # MAIN slot 0 SINE (repeated!)
41 00 41 ff                            # START slot 0 (arg=ff)
05 2a 00 0a 0a 00 00 00 00 0a 0a       # condition X slot 1 (max coeff, low sat)
05 38 00 00 00 00 00 00 00 64 64       # condition Y slot 1 (zeros, sat=max)
01 01 41 40 ff ff 00 00 00 2a 00 38 00 00 00  # MAIN slot 1 DAMPER
41 01 41 ff                            # START slot 1 (arg=ff)
```

**Order: MAIN → apply → envelope → MAIN (repeated) → START.**

### Implications

1. **MAIN upload can be sent before OR after parameters** — the device tolerates either.
   Our driver sends MAIN last (in `t500rs_seq_*` sequences, MAIN is the final step).
   This matches C1 but not C2.

2. **MAIN upload may need to be repeated** when interspersed with `42 05` apply
   commands. The apply seems to commit the parameter state, requiring MAIN re-send
   to re-bind parameters to the effect slot.

3. **Multiple `42 05` in a row** is normal in C2 (3× in a row at frames 2639/2641/2643).
   Driver sends only one.

## 11. Pre-race idle window (t=11-109s in C2)

97-second gap between init (t=11.7s) and race start (t=109s). Activity:
- t=12.4s: 3 init packets (repeat of `42 04 / 40 04 / 40 03 0d`)
- t=42.4s: 32-byte reset pair (`42 05 00...00` + `42 00 00...00`)
- t=43.4s: 4 init packets (repeat)

No OUT packets to T500RS outside these windows. The wheel just sends `0x07` state
reports at 230 Hz continuously.

**Conclusion:** Windows re-arms autocenter-disable every ~30 seconds during idle.
Probably a defensive measure against the wheel's autocenter auto-reverting.

## 12. Vendor requests in advanced mode (post-boot-switch)

**ZERO vendor requests sent to the device in advanced mode** in either capture.
The vendor requests (`0x56`, `0x47`, `0x42`, `0x4e`, `0x53`) appear ONLY during
the boot-mode window in C2.

The advanced-mode device communicates entirely via HID reports on the interrupt
endpoints. No class-specific or vendor-specific control transfers needed.

## 13. URB errors and disconnects

**ZERO URB errors in either capture.** All transfers complete successfully.

**No mid-session USB reconnections.** Device stays connected throughout:
- C1: device `1.6.0` enumerates once at t=0, stays connected for 32 minutes.
- C2: device `2.3.0` enumerates twice (boot → advanced) at t=8.7s/9.6s, then stays
  connected for 11 minutes.

**Implication:** any transfer errors or disconnects seen on Linux are bugs in our
driver or the Linux USB stack, not protocol-level issues.

---

## Updated action items (additions to `09_action_items.md`)

| ID | Pri | Description |
|----|-----|-------------|
| **P1-2** | 🔴 | **Fix the `arg` byte of `0x41` START command.** C2 uses `0xff`, not `0x01`. Likely encodes per-effect start-gain. Make it a parameter or hardcode `0xff` for START, keep `0x01` for STOP. |
| **P1-3** | 🔴 | **Remove the `40 11 42 7b` "FFB-arm magic" from init** (line 1944-1955). It's a range command setting 526°, not an FFB enable. Windows doesn't send it. Test that FFB still works without it. |
| **P2-5** | 🟡 | **Send `42 05` apply 3× in a row** during init and effect uploads (matches Windows). |
| **P2-6** | 🟡 | **Add periodic re-arm of autocenter-disable** (every ~30s, matches Windows). |
| **P2-7** | 🟡 | **Investigate whether MAIN upload needs re-sending after `42 05`**. C2 does this; C1 doesn't. |
| **P3-5** | 🟢 | **Verify periodic support (`0x04` with code=0x2a)** — no capture exists. May need a hardware test with `fftest` to capture a real periodic effect. |
| **P3-6** | 🟢 | **Verify ramp-envelope support** — no capture of non-zero envelope exists. |
| **P4-3** | 🔵 | **Decode the `0x14` identification report** — likely encodes (base_PID, attachment_ID, fw_version). Useful for F1/standard rim detection. |

## Corrections needed in pass-1 docs

| File | Section | Correction |
|------|---------|------------|
| `00_capture_inventory.md` | "IN packets" sections | Replace `0x14` dominant with `0x07`. C1 has zero interrupt-IN. |
| `03_packet_inventory.md` | `0x14` IN report section | Renamed: `0x14` is rare (init-only). Add new `0x07` section as dominant state report. |
| `06_unknown_reports.md` | `0x14` section | Move to "rare init report". Add `0x07` as the dominant IN report (15 B, 230 Hz). |
| `02_init_sequence_diffs.md` | Init table | Add note that init runs 3× in C2, plus 32-byte reset pair. |
| `04_effect_id_bug.md` | "Why current driver works" section | Refine: also note that slot 0 isn't constant-only (C2 uses slot 0 for sine). |
| `05_periodic_0x04_anomaly.md` | Hypothesis B section | **Upgrade from "hypothesis" to "confirmed"** — b4 is signed level, b6-b7 magic 0x2710, validated across 32 222 packets. |
| `09_action_items.md` | All | Add P1-2, P1-3, P2-5, P2-6, P2-7, P3-5, P3-6, P4-3. |

The next two files (`11_corrections_to_pass_1.md` and updated `09_action_items.md`)
apply these corrections to the existing work tree.
