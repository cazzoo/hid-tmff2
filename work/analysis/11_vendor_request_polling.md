# 11 — Vendor request polling during init (correction to pass-1 + pass-2)

> **Third correction pass (2026-08-12).** This document supersedes incorrect claims
> about `0x49` in `06_unknown_reports.md` and the "vendor requests only happen during
> boot mode" claim in `10_second_pass_findings.md` §12.

## TL;DR

**The Windows driver uses vendor control requests throughout init in BOTH captures** —
not just during the boot-mode-switch window. After ~30-40 seconds of init activity,
polling stops and Windows relies on interrupt-IN reports.

| Capture | Vendor requests total | Time window | Polling rate during init |
|---------|-----------------------|-------------|--------------------------|
| **C1** | 24 | t=960.5-960.7s (200 ms) | ~120 req/s (just `0x49`) |
| **C2** | 64 | t=8.7-43.4s (35 s) | varies (boot mode + advanced mode) |

## What I got wrong in pass 1 and pass 2

| Claim | Source | Reality |
|-------|--------|---------|
| "C1 has 26 IN reports of `0x49` at ~1/min" | `06_unknown_reports.md` | WRONG. They are **24 replies** to Windows' `0x49` vendor requests, all within 200ms during init. |
| "C2 has ZERO vendor requests in advanced mode" | `10_second_pass_findings.md` §12 | WRONG. C2 has 38 vendor requests in advanced mode (after t=9.6s) during the init window. |
| "`0x49` is a low-rate status heartbeat" | `06_unknown_reports.md` | WRONG. It's a vendor request the host polls during init. The device replies with 16 bytes of identification/status data. |
| "Vendor requests only happen during boot mode" | `10_second_pass_findings.md` §12 | WRONG. They continue for ~30 seconds AFTER the advanced-mode switch. |

## The actual vendor request picture

### C1 vendor requests (24 total, all `0x49`)

All 24 are `bmRequestType=0xc1 bRequest=0x49 wLength=0x10` (vendor IN, 16-byte reply).
They happen in a single burst at the start of game FFB usage (t=960.5-960.7s, ~200ms).

The C1 capture starts at the moment the game opens the wheel (the device was already
enumerated before capture began at t=0). So Windows' `0x49` polling happens on game
start, not on device plug-in.

### C2 vendor requests (64 total)

Two phases:

**Phase 1 — boot mode (PID `0xb65d`, t=8.7-9.2s):** 14 requests before the
advanced-mode switch.
- 2× `0x56` (GET_MODEL_INFO) → reply `56 2b 00 00 ...` (model byte `0x2b`)
- 6× `0x49` (status poll) — three pairs of three
- 2× `0x47` (GET_FW_INFO) → reply `47 00 07 00 00 00 03 00`
- 1× `0x42` → reply `42 e8 03`
- 1× `0x4e` → reply `4e 14`
- 1× `0x53` wValue=0x0003 → **the advanced-mode switch** (no reply, device re-enumerates)

**Phase 2 — advanced mode (PID `0xb662`, t=9.6-43.4s):** 50 requests after the switch.
- 1× `0x55` wLength=0x10 → reply (16 bytes)
- 1× `0x48` wLength=0x40 → reply (64 bytes — **the longest vendor reply seen**)
- 1× `0x41 0x48 0x40 ...` (vendor OUT, frame 211) — sends 64 bytes back to the device
- 3× `0x42` → reply `42 e8 03`
- 2× `0x4e` → reply `4e 14`
- 5× `0x56` → reply `56 2b 00 00`
- 6× `0x47` → reply `47 00 07 00 00 00 03 00`
- ~30× `0x49` (status poll) → reply (16 bytes)

**Phase 3 — after t=43.4s:** ZERO vendor requests for the remaining 11 minutes.
Windows relies entirely on interrupt-IN `0x07` state reports.

## The `0x49` polling pattern (the recurring loop)

During init, Windows does a recurring 4-request cycle:

```
0x49 (×3)   ← status poll, expects 16-byte reply
0x47 (×1)   ← firmware info, expects 8-byte reply
[loop]
```

The cycle runs ~5 times during the post-switch advanced-mode window, then stops.

### Decoded `0x49` reply

C1 reply (one example, identical for all 24 requests in C1):
```
49 00 03 04 01 00 0a 00 03 00 00 00 02 02 00 00
```

C2 reply would be similar but I didn't extract it (the replies are in
`raw_data/cap2_init_window_verbose.txt`).

Decoded (conjectural):
- `b0 = 0x49` — echoes bRequest
- `b1 = 0x00` — status flag (0 = OK)
- `b2 = 0x03` — firmware major?
- `b3 = 0x04` — firmware minor?
- `b4-b5 = 0x01 0x00` — firmware build 1
- `b6 = 0x0a` — hardware revision?
- `b7-b9 = 0x00 0x03 0x00` — ???
- `b10-b13 = 0x00 0x00 0x02 0x02` — feature flags?
- `b14-b15 = 0x00 0x00` — reserved

This is likely a **device-capability descriptor** used by Windows to enable specific
features (e.g. F1 rim detection, force feedback mode selection).

## Driver implications

| Concern | Status |
|---------|--------|
| Does our driver need to poll `0x49` during init? | **Unknown.** Probably not — Linux's stock HID enumeration should be sufficient. But if certain features (F1 rim detection) depend on it, we might miss them. |
| Does the F1 rim require the `0x55` / `0x48` / `0x41 0x48 0x40` sequence? | **Unknown.** These are init-only vendor requests unique to C2's F1 session. If they're required for F1 features, our driver is missing them. |
| Should we implement `0x49` polling? | **Probably not.** No evidence it's required for basic FFB. The polling seems to be Windows' way of detecting device capabilities, which Linux's HID subsystem does via the report descriptor. |

## Action items

| ID | Pri | Description |
|----|-----|-------------|
| **P4-4** | 🔵 | Investigate `0x49` reply format — capture on Linux with `lsusb -vvv` or custom hidraw tool to see what the device returns. Compare with C1's `49 00 03 04 01 00 0a 00 ...` |
| **P4-5** | 🔵 | Investigate `0x55`, `0x48`, and `0x41 0x48 0x40` vendor requests — likely F1-specific features. Implement only if F1 users report missing capabilities. |
| **P3-7** | 🟢 | Update `06_unknown_reports.md` to remove the "0x49 IN report" section — it was a misidentification. The actual IN report is via interrupt endpoint (`0x07` in C2, none in C1). |

## Corrections needed in pass-1 and pass-2 docs

| File | Section | Correction |
|------|---------|------------|
| `06_unknown_reports.md` | `0x49` section | Replace with: "Vendor request `0x49` polled by Windows during init (24× in C1, ~30× in C2). Reply is 16 bytes of device-capability data. Not an IN report." |
| `06_unknown_reports.md` | Summary table | Update `0x49` row: dir = "IN (vendor reply, not unsolicited)", seen in = "C1 (24) + C2 (~30)" |
| `10_second_pass_findings.md` | §12 "ZERO vendor requests in advanced mode" | Replace with: "C2 has 38 vendor requests in advanced mode during init (t=9.6-43.4s). They stop completely after init." |
| `10_second_pass_findings.md` | §7 "C1 IN report is `0x49` (low rate)" | Replace with: "C1's only IN data is 24 replies to Windows' `0x49` vendor requests during the 200ms init burst (t=960.5-960.7s). C1 has ZERO interrupt-IN traffic." |
| `00_capture_inventory.md` | "IN packets" section for C1 | Update: "C1 has ZERO interrupt-IN traffic. Its 26 control-endpoint IN packets are vendor-request replies, not state reports." |
