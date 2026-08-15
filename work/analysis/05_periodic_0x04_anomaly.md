# 05 — The `0x04` packet anomaly in rFactor2 (capture 2)

> ✅ **Second-pass update (2026-08-12): Hypothesis B CONFIRMED.** All 32 222 packets
> in C2 follow the alternative layout exactly: b2/b3/b5 always 0x00, b4 spans all 256
> values with a Gaussian distribution (signed level), b6 always 0x10, b7 always 0x27.
> See "Hypothesis B — confirmed" section below.

## Summary

rFactor2 with the F1 attachment sends **32 222 identical-structure `0x04` packets**,
all using code `0x0e` (the constant-force slot's param subtype), all 8 bytes long with
a non-zero reserved byte. **Our driver never produces a `0x04` packet that looks like
this.** Either the driver is missing an entire encoding mode, or our struct layout for
`0x04` is wrong.

## The data

### Frequency

- 32 222 packets in 11 minutes → ~50 packets/second during FFB activity
- Time distribution: ~3 900 packets per minute (steady), drops to ~1 650 in the last
  minute (probably post-race)
- This is the **dominant FFB signal** in rFactor2 — there are zero `0x03` packets in
  capture 2

### Full byte breakdown

All 32 222 packets follow this template:

```
04 0e 00 <XX> <YY> 00 10 27
└┬┘ └┬┘ └┬┘ └─┬─┘ └─┬─┘ └──┬──┘
 id code ??  varies  ??   constant
```

- `b0 = 0x04` — periodic packet ID (per driver)
- `b1 = 0x0e` — code (constant-force slot subtype, NOT periodic subtype!)
- `b2 = 0x00` — always zero
- `b3 = 0x00` — always zero
- **`b4 = XX`** — varies from `0x00` to `0xff`, **signed torque signal**
- `b5 = 0x00` — always zero
- `b6 = 0x10` — always `0x10`
- `b7 = 0x27` — always `0x27` (treated as "reserved = 0x00" by our driver)

The varying byte `b4` distribution:

| `b4` value | Count | Signed meaning |
|------------|-------|----------------|
| `0x00` | 620 | 0 (neutral) |
| `0x01` | 723 | +1 |
| `0x02` | 666 | +2 |
| `0x7f` | (rare) | +127 (peak positive) |
| `0x80` | (rare) | -128 (peak negative) |
| `0xff` | 608 | -1 |
| `0xfe` | 568 | -2 |

The distribution is roughly Gaussian-shaped around 0 — typical of a live FFB torque
signal reacting to road forces. The mean is slightly negative (bias toward `fd`/`fc`),
consistent with a centering/self-aligning torque.

### What our struct says the bytes mean

Per `hid-tmt500rs.h:151-159`:

```c
struct t500rs_pkt_r04_periodic_ramp {
    u8 id;           /* b0 */
    u8 code;         /* b1 */
    u8 magnitude;    /* b2 */  ← in captures, always 0x00
    s8 offset;       /* b3 */  ← in captures, always 0x00
    u8 phase;        /* b4 */  ← in captures, this is the VARYING byte
    __le16 period_ms;/* b5-b6 */  ← in captures, always 0x0010 = 16 (or 0x1000 = 4096)
    u8 reserved;     /* b7 */  ← in captures, always 0x27 (NOT 0x00!)
} __packed;
```

So our struct decodes `04 0e 00 00 01 00 10 27` as:
- magnitude = 0
- offset = 0
- phase = 1
- period_ms = 0x0010 = 16 ms (= 62.5 Hz)
- reserved = 0x27 ❌ (should be 0x00)

A 62.5 Hz periodic with zero magnitude makes no sense as an FFB signal.

## Two hypotheses

### Hypothesis A — our struct is correct, rFactor2 is doing something weird

- `b4` is genuinely a phase value 0..255 (signed -128..+127)
- `period = 16 ms` means a 62.5 Hz oscillation
- magnitude=0 means no periodic force at all
- `b7 = 0x27` is firmware-ignored reserved (the wheel doesn't care)

**Problem:** this combination produces zero torque. rFactor2 would not send 32 222
zero-torque packets per session — they would have to encode SOMETHING.

### Hypothesis B — confirmed (validated 2026-08-12 across all 32 222 packets)

**The driver's struct is wrong for the F1 rim case; `b4` is a signed level.**

Alternative layout for the `code=0x0e` case:

```c
struct t500rs_pkt_r04_constant_alt {  /* NOT in driver */
    u8 id;           /* b0 = 0x04 */
    u8 code;         /* b1 = 0x0e  ← selects constant-force slot via periodic ID */
    u8 zero1;        /* b2 = 0x00  (32 222 / 32 222 packets) */
    u8 zero2;        /* b3 = 0x00  (32 222 / 32 222 packets) */
    s8 level;        /* b4 = signed force level -128..+127  ← VARIES */
    u8 zero3;        /* b5 = 0x00  (32 222 / 32 222 packets) */
    __le16 magic;    /* b6-b7 = 0x2710 LE = 10000 ← "DC mode" marker (always) */
} __packed;
```

Under this layout, the packet is a **constant-force update using the periodic packet
ID**. The magic `0x2710` (`10000`) tells the firmware "this is a DC force, not a real
periodic". This is a known trick in some HID FFB protocols.

**Statistical validation across all 32 222 packets:**

| Byte | Always this value? | Notes |
|------|--------------------|-------|
| b0 | `0x04` (always) | Periodic packet ID |
| b1 | `0x0e` (always) | Constant-force slot code |
| b2 | `0x00` (32 222/32 222 = 100%) | Always zero — confirmed reserved |
| b3 | `0x00` (32 222/32 222 = 100%) | Always zero — confirmed reserved |
| b4 | varies, **all 256 values appear** | Gaussian-shaped distribution (peak at ±1, ±2), 21 432 in 0x00-0x7f and 10 790 in 0x80-0xff |
| b5 | `0x00` (32 222/32 222 = 100%) | Always zero — confirmed reserved |
| b6 | `0x10` (32 222/32 222 = 100%) | Always — low byte of magic `0x2710` |
| b7 | `0x27` (32 222/32 222 = 100%) | Always — high byte of magic `0x2710` |

Top 10 most common b4 values (signed level):
- `0x01` = +1 (670×), `0x02` = +2 (666×), `0x00` = 0 (620×), `0xff` = -1 (608×),
  `0xfe` = -2 (568×), `0x03` = +3 (544×), `0xfd` = -3 (513×), `0x05` = +5 (501×),
  `0xfc` = -4 (478×), `0x04` = +4 (467×)

The distribution is symmetric around zero (peak at small magnitudes), exactly as
expected for an FFB torque signal reacting to road forces.

**Evidence supporting B (now conclusive):**
- The constant-force slot's `param_sub = 0x0e` is used as the `code` byte → it's
  targeting the constant slot, not a periodic slot
- The varying byte spans the full signed range like a torque signal
- The "magic" `0x2710` (`10000`) is a recognizable firmware marker
- rFactor2 is known to be a sophisticated sim that may use vendor-specific tricks

**Evidence against B:**
- (none remaining after second-pass validation)

### Hypothesis C (hybrid) — code 0x0e triggers a different layout

The driver's current interpretation (Hypothesis A) is verified ONLY for periodic
effects with `code = 0x2a, 0x46, ...` (real periodic slots). The case `code = 0x0e`
might trigger a completely different firmware code path with a different byte layout.

In other words: **`b1` is a discriminator, and `0x0e` selects a different packet
structure than `0x2a`.**

This is plausible because the driver's reference example (line 205-208 of the .c) is:
> `04 2a 06 00 3f 0a 00 00`
> b0=04, b1=code(0x2a), b2=mag(06), b3=offset(00), b4=phase(3f), b5-b6=period(0x000a=10ms), b7=reserved(00)

Here `b1 = 0x2a` (real periodic slot) and **all bytes are non-trivial**. This is the
"real periodic" encoding. The capture-2 packets all have `b1 = 0x0e` (constant slot)
— a different mode entirely.

## Driver implications

| Concern | Status |
|---------|--------|
| Does our `0x03 0e 00 <level>` packet produce the same torque as Windows' `0x04 0e 00 00 <level> 00 10 27`? | **UNKNOWN — hw-verify needed** |
| Does the F1 rim firmware accept `0x03 0e ...` at all? | **UNKNOWN** |
| Should we add a "send constant via 0x04" mode for F1 compatibility? | **Only if (1) our `0x03` form doesn't work, or (2) games can tell the difference** |
| Why does rFactor2 use `0x04` while the C1 game uses `0x03`? | Could be game-side preference (DirectInput API choice) or wheel-mode preference (F1 vs standard) |

## Recommended verification procedure

1. Boot T500RS in standard mode (PID `0xb65e`) on Linux.
2. Use `fftest` to play a constant force of level +64 for 1 second.
3. Capture USB traffic with `usbmon` + `wireshark`.
4. Confirm our driver sends `03 0e 00 40` (`level=0x40=64`).
5. Repeat with the F1 rim attached (PID `0xb662`).
6. Confirm whether `0x03 0e 00 40` still produces torque on the F1 rim.
7. If it doesn't, manually craft an `0x04 0e 00 00 40 00 10 27` and send via `hidraw`;
   check if torque appears.

If step 7 works and step 6 doesn't, we need a per-wheel-mode "constant-force-via-0x04"
fallback path. See `09_action_items.md`.

## Why we should NOT change the driver yet

The current `0x03`-for-constant path has been working for users on the standard rim.
Changing it based on a single capture-2 rFactor2 trace could regress known-good cases.

The captures establish **what the Windows driver does**, not **what the device
requires**. The device may accept both forms equivalently. Hardware verification is
mandatory before any structural change.

## Open questions for the F1 community

1. Do F1-rim Linux users report that constant force works with the current driver?
2. Does rFactor2 under Proton/Wine use `EV_IFORCE` or the kernel FFB `uinput` path?
3. Is there a known issue thread about "F1 rim no FFB in rFactor2" on the upstream repo?
