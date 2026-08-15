# 03 — Packet inventory (per report ID)

Authoritative list of every report ID observed in either capture, with sample bytes,
frequency, and the driver's current understanding.

Legend:
- ✅ = driver builds/sends/parses this report
- ⚠️ = driver builds this report but bytes don't fully match captures
- ❌ = driver never builds this report (it's missing)
- 🟡 = direction the driver doesn't handle (input only)

## OUT (host → device) reports

### `0x01` — Main effect upload (15 bytes)

**Driver status:** ✅ (built by `t500rs_build_r01_main()`, struct `t500rs_pkt_r01_main`)

**Samples:**

| Source | Bytes (15) | Decoded |
|--------|------------|---------|
| C1 f65 | `01 00 00 40 ff ff 00 ff ff 0e 00 1c 00 00 00` | effect_id=0, type=CONSTANT, dur=∞, **delay=`ff00` b8=`ff`**, psub=`000e`, esub=`001c` |
| C1 f73 | `01 01 41 40 ff ff 00 ff ff 2a 00 38 00 00 00` | **effect_id=1**, type=DAMPER, dur=∞, psub=`002a`, esub=`0038` |
| C2 f2637 | `01 00 22 40 ff ff 00 00 00 0e 00 1c 00 00 00` | effect_id=0, type=SINE, dur=∞, delay=`0000`, p8=`00`, psub=`000e`, esub=`001c` |
| C2 f2657 | `01 01 41 40 ff ff 00 00 00 2a 00 38 00 00 00` | **effect_id=1**, type=DAMPER, dur=∞, psub=`002a`, esub=`0038` |

**Driver discrepancy:** driver hardcodes `effect_id = T500RS_EFFECT_ID = 0x00`. Captures
prove the real protocol uses non-zero IDs for non-constant slots. See `04_effect_id_bug.md`.

**Mystery bytes:** C1 has bytes `6-7 = ff 00` and byte `8 = ff` (interpreted by our
struct as `delay_ms=0xff00=65280ms` and `reserved1=0xff`). C2 has the same fields as
`00 00 00`. **Likely a struct-layout bug or capture-1 corruption** — TODO hw-verify.

---

### `0x02` — Envelope (9 bytes)

**Driver status:** ✅ (`t500rs_build_r02_envelope()`, struct `t500rs_pkt_r02_envelope`)

**Samples:**
- C1 (×1): `02 1c 00 00 00 00 00 00 00` — env sub=`0x1c`, all-zero (constant force envelope)
- C2 (×4): `02 1c 00 00 00 00 00 00 00` — same pattern

**Driver match:** ✅ perfect (we always send zeros for non-ramp effects, per the firmware
limitation documented at line 770-786).

---

### `0x03` — Constant force level (4 bytes)

**Driver status:** ✅ (`t500rs_build_r03_constant()`, struct `t500rs_r03_const`)

**Samples (capture 1, 67 864 packets):**
- `03 0e 00 00` (level 0) — 1 684 occurrences
- `03 0e 00 01` (level +1) — 1 723
- `03 0e 00 ff` (level -1) — 1 723
- `03 0e 00 fd` (level -3) — 2 628  (most common — game's "default" torque)
- `03 0e 00 03` (level +3) — 2 532
- ... range spans -127 .. +127

**Driver match:** ✅ layout matches.

> ⚠️ **Capture 2 has ZERO `0x03` packets.** Constant-force-like updates in rFactor2 are
> sent via `0x04` packets with code `0x0e`. See `05_periodic_0x04_anomaly.md`.

---

### `0x04` — Periodic / ramp parameters (8 bytes)

**Driver status:** ⚠️ (`t500rs_build_r04_periodic()` and `t500rs_build_r04_ramp()`)

**Samples (capture 2, 32 222 packets — ALL with code `0x0e`):**

| Bytes | Count | Interpretation per our struct |
|-------|-------|-------------------------------|
| `04 0e 00 00 01 00 10 27` | 670 | mag=0 offset=0 **phase=1** period=0x1000=4096ms **reserved=0x27** ❌ |
| `04 0e 00 00 02 00 10 27` | 666 | phase=2 ... |
| `04 0e 00 00 00 00 10 27` | 620 | phase=0 |
| `04 0e 00 00 ff 00 10 27` | 608 | phase=0xff (-1) |
| `04 0e 00 00 fe 00 10 27` | 568 | phase=0xfe (-2) |
| `04 0e 00 00 7f 00 10 27` | (positive peak) | phase=+127 |
| `04 0e 00 00 80 00 10 27` | (negative peak) | phase=-128 |

**Driver discrepancies:**
1. `b7` (reserved) is **always `0x27`** in C2, but the driver's struct marks it
   `reserved (always 0x00)` and `memset()` zeroes it. **This is a struct-layout bug or
   an undocumented field.**
2. `b5-b6` is **always `0x10 0x00`** in C2 (period_ms = 4096ms = 4 seconds —
   suspiciously slow for "periodic"), but the driver writes a real period in ms.
3. The varying byte (`b4` = "phase" per our struct) ranges -127 .. +127 and tracks the
   FFB torque signal. This is functionally a **signed level**, not a phase.

**Two interpretations:**

**(A) Driver struct is correct; rFactor2 is encoding a low-frequency periodic**
- period = 4096 ms (a 0.25 Hz wave), magnitude = 0, phase = varying start angle
- This doesn't make physical sense for an FFB torque signal.

**(B) Driver struct is wrong for the F1 rim; actual layout is**
```
b0 = 0x04 (periodic)
b1 = code
b2 = 0x00 (reserved)
b3 = 0x00 (sign-extended high byte of level, always 0)
b4 = LEVEL (signed 8-bit force level) ← this is what varies
b5 = 0x00 (constant)
b6-b7 = 0x2710 LE = 10000 ← magic / scale factor ("constant mode marker")
```
Under this interpretation the packet is **really a constant-force update** disguised as
a periodic. The `0x2710` (`10000`) constant would be a magic value telling the firmware
"this is a DC force of magnitude `b4`".

**Either way:** our driver never produces a `0x04` packet with `code=0x0e` because the
constant-force path uses `0x03`. rFactor2 / Windows chose `0x04` for the same purpose.
We should investigate whether sending `0x03 0e ...` and `0x04 0e ...` are interchangeable
or whether some games require the `0x04` form.

**TODO:** hw-verify by manually sending both forms for the same torque level and seeing
which the device responds to.

---

### `0x05` — Conditional effect (11 bytes)

**Driver status:** ✅ (`t500rs_build_r05_condition()`, struct `t500rs_pkt_r05_condition`)

**Samples:**
- C1 (×2): `05 38 00 ...`, `05 2a 00 ...` — full 11-byte bodies not captured in extract
- C2 (×4): `05 38 00 ...` (Y axis, env_sub=`0x38`), `05 2a 00 ...` (X axis, psub=`0x2a`)

**Driver match:** ✅ structurally. But coefficient scaling (`coeff * 10 / 32767`) and
deadband (`/65`) are TODO-unverified — see `07_condition_deadband_unverified.md`.

---

### `0x40` — Configuration (4 bytes)

**Driver status:** ✅ (`struct t500rs_pkt_r40_config`)

**Samples:**
| Source | Bytes | Decoded |
|--------|-------|---------|
| C2 f220 | `40 11 55 55` | Range: 0x5555 / 60 = 364° |
| C2 f258 | `40 04 00 00` | Disable autocenter |
| C2 f261 | `40 03 0d 00` | Set autocenter strength = 13% |
| Our init | `40 11 42 7b` | "FFB-enable magic" (NOT range — 526°) |

**Driver discrepancy:** our `t500rs_wheel_init()` reuses `0x40 0x11` for the FFB-arm
"magic" (`0x42 0x7b`). The same subcmd `0x11` is also the range subcmd in
`t500rs_set_range()`. **This is overlapping semantics** — Windows distinguishes by data
value, our driver distinguishes by call site. See `02_init_sequence_diffs.md`.

---

### `0x41` — START / STOP command (4 bytes)

**Driver status:** ✅ (`struct t500rs_r41_cmd`)

**Samples:**
- `41 00 41 01` (effect_id=0, START, arg=1)
- `41 01 41 01` (**effect_id=1**, START, arg=1) ← C1 f? and C2
- `41 00 00 01` (effect_id=0, STOP, arg=1)
- `41 01 00 01` (effect_id=1, STOP, arg=1)

**Driver discrepancy:** same as `0x01` — driver hardcodes effect_id=0 in all START/STOP,
but real protocol uses the slot's ID. See `04_effect_id_bug.md`.

---

### `0x42` — Sync / handshake (2 or 8 or 16 bytes)

**Driver status:** ⚠️ We only send 2-byte variants; the 8-byte / 16-byte variants are
not produced.

**Samples:**
| Bytes | Where | Notes |
|-------|-------|-------|
| `42 04` | C1, C2, our driver | Mandatory opening sync |
| `42 05` | C1 (×2), C2, our driver | Apply-pending-settings |
| `42 00` | C1, C2, our driver | Closing sync |
| `42 01 00 00 00 00 00 00` | **C2 only** (f109) | **NEW: 8-byte variant** |
| `42 01 00 ... 00` (16 B) | **C2 only** (f183) | **NEW: 16-byte padded variant** |

**Driver discrepancy:** We don't emit `42 01 00 ...`. Likely F1-rim specific.

---

### `0x43` — Global gain (2 bytes)

**Driver status:** ✅ (`t500rs_set_gain()` line 985 and init line 1980)

**Samples:**
- Our init: `43 ff` (gain = 100%)
- C2 f263: `43 5a` (gain = 90%)
- C2 f373...: gain updates during gameplay

**Driver discrepancy:** value-only. We hardcode `0xff` at init; Windows uses `0x5a`.

---

### `0x0a` — **UNKNOWN OUT report**

**Driver status:** ❌ driver has no struct, no builder, never sends.

**Samples (C2 only, ×6):**

| Frame | t (s) | Bytes (8 or 16) |
|-------|-------|------------------|
| 116 | 8.962 | `0a 04 90 03 00 00 00 00` |
| 120 | 8.964 | `0a 04 12 10 00 00 00 00` |
| 124 | 8.968 | `0a 04 00 06 00 00 00 00` |
| 190 | 9.870 | `0a 04 90 03 00 00 00 00 00 00 00 00 00 00 00 00` (16-byte) |
| 194 | 9.874 | `0a 04 12 10 00 00 00 00 00 00 00 00 00 00 00 00` |
| 198 | 9.878 | `0a 04 00 06 00 00 00 00 00 00 00 00 00 00 00 00` |

**Observations:**
- Sent only during the boot-mode-switch window (t=8.9–9.9s), then never again
- The 3 unique payloads repeat (8-byte version, then 16-byte version of same data)
- `0x0390`, `0x1012`, `0x0600` are also seen as IN data (frames 94, 102, 104)
- `0a 04` prefix mirrors the `40 04` (autocenter-disable) command pattern

**Hypothesis:** the Windows driver echoes back to the device the values it just read
from the boot-mode vendor requests (`0x56`, `0x47`, etc.), confirming the wheel's
identity to its firmware. This may be required for the F1 attachment to enable certain
features (force feedback mode selection, rim-specific button mapping).

**Driver action:** none yet. Add documentation and possibly implement if F1-rim users
report missing features.

See `06_unknown_reports.md` for IN-side correlations.

---

## IN (device → host) reports

### `0x14` — Wheel state report (15 bytes, ~230 Hz)

**Driver status:** 🟡 driver relies on stock HID parsing; no struct or handler.

**Sample (C2, f118):** `14 20 90 03 01 39 74 05 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00`

**Decoded (conjectural):**
| Offset | Value | Field (guess) |
|--------|-------|---------------|
| 0 | `0x14` | Report ID |
| 1 | `0x20` | Buttons / hat bitmask high byte |
| 2–3 | `0x0390` LE | Wheel X axis (signed? range?) |
| 4 | `0x01` | Pedals / clutch low nibble |
| 5–7 | `0x39 0x74 0x05` | Other axes / accelerator / brake |
| 8–14 | `0x00 ... 0x00` | Padding |

**Driver action:** none. The stock HID descriptor parses this transparently.

---

### `0x49` — Status / identification IN report (16 bytes, very low rate)

**Driver status:** 🟡 no handler.

**Sample (C1, f8):** `49 00 03 04 01 00 0a 00 03 00 00 00 02 02 00 00`

**Decoded (pure speculation):**
- `0x49` = report ID ('I' ASCII = "info"?)
- Only ~26 packets across a 32-min capture → ~1 per minute
- Likely a periodic "I'm alive" or "thermal / firmware status" report
- Field meaning unknown without correlating to wheel state

**Driver action:** none. If we ever want thermal / fault monitoring, this is the channel.

---

## Vendor / class control transfers (boot mode only)

These appear ONLY in the boot-mode window of capture 2 (PID `0xb65d`):

| bmRequestType | bRequest | wValue | wLength | Reply | Purpose |
|---------------|----------|--------|---------|-------|---------|
| `0xc1` | `0x56` | 0 | 8 | `56 2b 00 00 ...` | GET_MODEL_INFO — model byte `0x2b` = T500RS F1 |
| `0xc1` | `0x47` | 0 | 8 | `47 00 07 00 00 00 03 00` | GET_FW_INFO |
| `0xc1` | `0x42` | 0 | 3 | `42 e8 03` | unknown |
| `0xc1` | `0x4e` | 0 | 2 | `4e 14` | unknown |
| `0x41` | `0x53` | 0x0003 | 0 | — | **SWITCH TO ADVANCED MODE** |

See `01_boot_mode_switch.md` for details.
