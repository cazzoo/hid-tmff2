# 06 — Unknown / unhandled reports

> ⚠️ **Corrected 2026-08-12** — pass 1 incorrectly identified `0x14` as the dominant
> C2 IN report. The actual dominant IN report is `0x07` (152 549 packets at 230 Hz).
> `0x14` is a rare init-only report (6 packets). See `10_second_pass_findings.md` §7.

Report IDs observed in captures that our driver does not produce (OUT) or parse (IN).

## OUT reports we never send

### `0x0a` — F1 attachment setup (capture 2 only)

**Occurrences:** 6 in capture 2, all during the boot-mode-switch / early-init window
(t=8.96 to 9.88s). Never sent again.

| Frame | t (s) | Bytes (8-byte form) | Bytes (16-byte form) |
|-------|-------|---------------------|----------------------|
| 116 | 8.962 | `0a 04 90 03 00 00 00 00` | — |
| 120 | 8.964 | `0a 04 12 10 00 00 00 00` | — |
| 124 | 8.968 | `0a 04 00 06 00 00 00 00` | — |
| 190 | 9.870 | — | `0a 04 90 03 00 00 00 00 00 00 00 00 00 00 00 00` |
| 194 | 9.874 | — | `0a 04 12 10 00 00 00 00 00 00 00 00 00 00 00 00` |
| 198 | 9.878 | — | `0a 04 00 06 00 00 00 00 00 00 00 00 00 00 00 00` |

**Byte breakdown:**
- `b0 = 0x0a` — report ID
- `b1 = 0x04` — subcmd (mirrors the `0x40 0x04` autocenter-disable subcmd)
- `b2-b3` (LE u16) — three distinct values: `0x0390`, `0x1012`, `0x0600`
- `b4-b7` — always zero
- (16-byte form just zero-pads `b8-b15`)

**Correlation:** the same three values appear as IN data from the boot-mode vendor
requests `0x56` (returns `56 2b 00 00` — model), `0x47` (returns `47 00 07 00 00 00 03 00`),
`0x42` (returns `42 e8 03`), `0x4e` (returns `4e 14`). Specifically:
- `0x0390` ↔ `0x90 0x03` — fragment of `42 e8 03` or similar
- `0x1012` ↔ `0x12 0x10` — could be derived from `0x1012` = fw version component
- `0x0600` ↔ `0x00 0x06` — fragment of `47 00 07 00 00 00 03 00`?

**Best hypothesis:** the Windows driver sends back to the wheel the values it just read
from the boot-mode vendor requests, as a confirmation / activation handshake. The wheel
may use these to enable F1-specific features (rim button mapping, display protocol,
force mode).

**Driver action:** none currently. Implement only if F1-rim users report missing
features. Add a TODO in `t500rs_wheel_init()` referencing this doc.

### Missing sync variants: `42 01 00 ...` (8-byte and 16-byte)

**Occurrences:** 2 in capture 2.

| Frame | Bytes |
|-------|-------|
| 109 | `42 01 00 00 00 00 00 00` |
| 183 | `42 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00` |

Our driver only emits 2-byte `42 XX` syncs (lines 1911-1935, 1738-1742,
1800-1808, 840-851). The 8-byte/16-byte forms are not produced.

**Likely purpose:** reset command / state-machine re-arm. The `42 01` may mean
"reset to slot 0" or "re-sync protocol state".

**Driver action:** probably not needed — our 2-byte `42 04`/`42 05`/`42 00` sequence
is proven working on the standard rim. May be required for F1 rim, but unverified.

---

## IN reports we don't parse

### `0x07` — High-rate wheel state (capture 2) — **THE dominant IN report**

> ⚠️ **Corrected from pass 1.** Pass 1 misidentified `0x14` as the dominant C2 IN
> report. The actual dominant IN report is `0x07` (152 549 packets at 230 Hz). `0x14`
> is rare (6 packets, init-only — see below).

**Occurrences:** 152 549 in capture 2, **0 in capture 1's interrupt endpoint**
(C1 has zero interrupt-IN traffic — only control-endpoint IN, see `0x49` below).
~230 Hz steady rate.

**Sample (C2, f51251):** `07 e0 7d ff 03 ff 03 00 00 00 00 00 00 00 f0`

**Byte breakdown (15-byte payload, decoded across 152 549 packets):**

| Byte | Distribution | Likely field |
|------|--------------|--------------|
| b0 | `0x07` always (152 549) | Report ID |
| b1 | widely varied (top: fb=977, 14=866, f7=850, 00=850, 04=847, ef=834) | **Buttons bitfield** |
| b2 | bimodal `0x80` (14 639) + `0x7f` (12 036), spread to `0x81-0x87` | **Wheel X axis** (signed byte, centered) |
| b3 | `0xff` (131 943) mostly, `0x00` (3 913) rare | Buttons high byte or hat |
| b4 | `0x03` (134 096) mostly, some `0x00/0x01/0x02` | High-precision X bits or Y axis |
| b5 | `0xff` always | Constant / sync marker |
| b6 | `0x03` always | Constant / sync marker |
| b7 | bimodal `0x00` (61 929) + `0xff` (47 256) | **Pedal axis** (gas/brake centered) |
| b8 | low-cardinality `0x00` (67 739), `0x03` (58 225), `0x02` (14 614), `0x01` (11 971) | Hat switch or shifter |
| b9, b10 | `0x00` always | Reserved |
| b11 | mostly `0x00`, rare `0x01`/`0x02` | Third axis (clutch?) |
| b12 | mostly `0x00`, rare `0x20` | Flags |
| b13 | `0x00` always | Reserved |
| b14 | `0xf0` always (152 549) | **Sync marker / report version** |

**Driver action:** none — relies on the stock HID report descriptor parsing in
`hid_input_field()`. We do NOT install a `report_fixup` or `raw_event` handler.

**Risk:** if the F1 rim's HID descriptor differs from the standard rim's (likely, given
the extra buttons / display), the stock parser may misinterpret axes. Our driver
comment at `hid-tmt500rs.c:2040-2050` explicitly notes we DO NOT patch the descriptor
and rely on userspace (SDL/Wine/game) to remap. This is a deliberate choice but means
Linux users may see mis-mapped pedals until they remap.

### `0x14` — Device-identification IN report (capture 2, init only, 6 packets)

**Occurrences:** 6 in capture 2, **0 in capture 1**. All during init (t=8.96-9.88s).

**Samples:**

| Frame | Bytes |
|-------|-------|
| 118 | `14 20 90 03 01 39 74 05 00 ...` (27 B) |
| 122 | `14 20 12 10 2b 00 5e b6 00 ...` (27 B) ← contains **`0xb65e`** (std rim PID) |
| 126 | `14 20 00 06 18 28 00 00 00 ...` (27 B) |
| 192, 196, 200 | 15-byte versions of the same payloads |

**Decoded (conjectural):**
- `b0 = 0x14` — report ID
- `b1 = 0x20` — likely a status bitfield
- `b2-b3` — varies (`0x0390 / 0x1012 / 0x0600` LE), probably hw/fw identifiers
- `b4-b7` — likely (model_byte, hw_revision, fw_version_lo, fw_version_hi)
- The `0x5e 0xb6` LE = `0xb65e` in packet 2 = standard-rim PID, **even though the F1
  rim is attached** (advanced PID `0xb662`) — suggests this report encodes the BASE
  wheel's identity, not the rim's

**Best hypothesis:** a **device-identification IN report** the host reads during init.
Probably correlates with the boot-mode vendor request `0x56` replies (`56 2b 00 00`) —
note `0x2b` also appears in the `0x14` payload (`14 20 12 10 2b 00`).

**Driver action:** none. Could be used for F1/standard rim auto-detection at probe
time (see P4-3 in `09_action_items.md`).

### `0x49` — Vendor request status poll reply (corrected; see `11_vendor_request_polling.md`)

> ⚠️ **Corrected 2026-08-12 (pass 3).** Pass 1 misidentified these packets as an
> unsolicited device-initiated IN report. They are actually the **16-byte replies
> to a vendor control request (`bmRequestType=0xc1 bRequest=0x49 wLength=0x10`)**
> that the Windows driver polls during init. See `11_vendor_request_polling.md`
> for the full polling-pattern analysis.

**Occurrences:** 24 requests in C1 (all within a 200 ms burst at game start,
t=960.5-960.7s ≈ 120 req/s) + ~30 requests in C2 (spread across the 35 s init
window, always in a recurring 3× `0x49` + 1× `0x47` cycle). **Zero requests
after init ends** in either capture.

**Sample reply (C1, f8):** `49 00 03 04 01 00 0a 00 03 00 00 00 02 02 00 00`

**Byte breakdown (conjectural — reply is constant across all 24 C1 requests):**
- `b0 = 0x49` — echoes bRequest
- `b1 = 0x00` — status flag (0 = OK?)
- `b2-b3 = 0x03 0x04` — firmware version fields?
- `b4-b6 = 0x01 0x00 0x0a` — hw revision / capability fields?
- `b7-b15` — mostly zero, `b12-b13 = 0x02 0x02` — feature flags?

**Best hypothesis:** a device-capability descriptor Windows reads during init to
decide which features to enable. Not telemetry, not a heartbeat.

**Driver action:** none. Linux's stock HID enumeration supplies the equivalent
capability information via the report descriptor. If F1-rim-specific features
turn out to be missing (see P4-5), this reply format may need decoding first
(P4-4).

---

## Summary table of unhandled reports

| ID | Dir | Seen in | Bytes | Driver handles? | Likely purpose |
|----|-----|---------|-------|-----------------|----------------|
| `0x0a` | OUT | C2 only | 8 / 15 | ❌ | F1 attachment activation handshake |
| `42 01 00 ...` | OUT | C2 only | 9 / 15 / 32 | ❌ | Protocol re-sync / reset (3 length variants) |
| `42 05 00...00` (32 B) | OUT | C2 only (×1) | 32 | ❌ | "Full reset" — clears all effect slots |
| `42 00 00...00` (32 B) | OUT | C2 only (×1) | 32 | ❌ | "Full reset" — clears all state |
| **`0x07`** | **IN** | **C2 only** | **15** | **🟡 (stock HID)** | **Dominant state report at 230 Hz (axes/buttons)** |
| `0x14` | IN | C2 only | 15 / 27 | 🟡 (stock HID) | Device-identification IN (init only, 6 packets) |
| `0x49` | ctrl IN reply | C1 (24) + C2 (~30), init only | 16 | n/a (host-initiated) | Vendor-request capability poll reply — see `11_vendor_request_polling.md` |

## Recommendations

| Priority | Report | Action |
|----------|--------|--------|
| Low | `0x0a` | Document in driver; implement only if F1 features missing |
| Low | `42 01 00`, 32-byte `42` resets | Document; possibly add to init sequence after hw-verify |
| Medium | `0x07` | Verify HID descriptor properly parses axes for F1 rim (the dominant state report) |
| Medium | `0x14` | Decode fully — may enable F1/standard auto-detection (P4-3) |
| Low | `0x49` (vendor req) | Decode reply format (P4-4); only needed if F1 features require it (P4-5) |
