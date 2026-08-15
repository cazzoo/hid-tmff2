# T500RS capture analysis — Executive summary

**Date:** 2026-08-12 (updated after second pass)
**Source captures:** `comunity-captures/{T500 win capture.pcapng, t500rs_f1_wheel_rfactor2_f1_1967_bt24_kyalami_1976_online.pcapng}`
**Driver analysed:** `src/tmt500rs/hid-tmt500rs.[ch]` + `docs/T500RS_FFBEFFECTS.md`
**Work files:** `work/` (11 docs in `work/analysis/`, raw extracts in `work/raw_data/`,
reproducible extractor `work/extract.sh`)

> 📌 **Read `work/analysis/10_second_pass_findings.md` first** — it corrects several
> pass-1 errors and adds new findings.

## Top 4 confirmed bugs (P1, all in driver)

| # | Bug | Evidence |
|---|-----|----------|
| **P1-1** | `effect_id` hardcoded to `0x00` — should be slot index | All 8 `0x01` + 10 `0x41` packets across both captures |
| **P1-2** | `0x41` START arg byte hardcoded to `0x01` — rFactor2 uses `0xff` | C2 f2651, f2659, f373753, f373823 |
| **P1-3** | `0x40 0x11 0x42 0x7b` "FFB-arm magic" is actually a range cmd (526°), in neither capture | C1: zero `0x40` packets. C2: `40 11 55 55` only |
| **P1-4** | STOP is per-slot — driver's 80-line "global STOP" workaround is built on wrong assumption | C1 f135803+f135809: explicit per-slot STOPs |

## Other major findings

5. **`0x04` with code `0x0e` is constant-force-via-periodic** (Hypothesis B confirmed
   across all 32 222 C2 packets). b4 is signed level, b6-b7 are magic `0x2710`.
6. **Dominant C2 IN report is `0x07`** (152 549 packets, 230 Hz, 15 B). Pass 1 had
   incorrectly identified it as `0x14` (only 6 packets).
7. **C1 has ZERO interrupt-IN traffic** — only control-endpoint IN (26 × `0x49`).
   C1 is incomplete for IN analysis.
8. **Init runs 3× in C2** (t=11.6, 12.4, 43.4) plus 32-byte reset pair at t=42.4.
9. **`42 05` sent up to 3× in a row** by Windows; driver sends once.
10. **ZERO `0x04` with code != `0x0e`** in either capture — driver's periodic support
    is unverified.
11. **ZERO URB errors** in either capture — protocol is well-formed.
12. **ZERO `0x40` packets in C1** — the C1 game used wheel defaults entirely.

## Inventory at a glance (corrected)

| | Capture 1 | Capture 2 |
|---|---|---|
| Wheel PID | `0x044f:0xb65e` (std rim, already advanced) | `0x044f:0xb65d` → `0xb662` (F1 rim, boot switch) |
| Duration | 32 min | 11 min |
| Packets | 135 812 | 374 590 |
| OUT report IDs seen | `01 02 03 05 41 42` | `01 02 04 05 0a 40 41 42 43` |
| IN interrupt endpoint traffic | **ZERO** | 152 556 packets (mostly `0x07` state) |
| IN control-endpoint traffic | 26 × `0x49` | boot-mode vendor replies + 6 × `0x14` ID report |
| Dominant OUT report | `0x03` (67 864×, constant force updates) | `0x04` (32 222×, periodic-as-constant) |
| Vendor requests in advanced mode | none | none |
| Init re-runs | 1 | 3 + 32-byte reset pair |
| URB errors | 0 | 0 |

## What's already correct in the driver

- ✅ Constant-force packet layout (`0x03 0e 00 <level>`) — matches capture 1 perfectly
- ✅ Sync handshake `42 04` / `42 05` / `42 00` (just sent too few times)
- ✅ Autocenter command structure (`0x40 0x03 XX 00`, `0x40 0x04 00 00`)
- ✅ Range command structure (`0x40 0x11 <lo> <hi>`, value = `range * 60`)
- ✅ Envelope-must-be-zero firmware quirk handling
- ✅ Subtype formula `psub = 0x0e + 0x1c*n`, `esub = 0x1c + 0x1c*n`
- ✅ Conditional `center / 20` scaling (verified)
- ✅ `effect_id == 0` for slot 0 (the constant-force default slot)
- ✅ Per-effect subtypes for non-constant effects (the subtypes are right; only the IDs are wrong)
- ✅ `0x05` X/Y condition pair pattern
- ✅ `0x02` envelope as zeros for non-ramp effects

## What's wrong or unverified

| Area | Status |
|------|--------|
| `effect_id` | 🔴 hardcoded `0x00`, should be slot index (P1-1) |
| `0x41` START arg byte | 🔴 hardcoded `0x01`, should be `0xff` for START (P1-2) |
| `0x40 0x11 0x42 0x7b` at init | 🔴 wrong — it's a range command, not FFB-arm (P1-3) |
| STOP semantics | 🔴 "global STOP" theory wrong — should be per-slot (P1-4) |
| Periodic (`0x04` with code `0x2a`) | 🟡 no capture to verify against (P3-5) |
| Ramp envelope non-zero values | 🟡 no capture to verify against (P3-6) |
| Condition deadband `/65` divisor | 🟡 all captures have deadband=0 (P3-2 in pass 1) |
| `42 05` send count | 🟡 once, should be 3× (P2-5) |
| Autocenter re-arm | 🟡 once, should be periodic (P2-6) |
| `0x04` with code `0x0e` form | 🟡 not produced by driver (P2-1) |
| Boot-mode-switch | 🟢 correct — handled by `hid-tminit` (not our scope) |

## What to read next

- **`work/README.md`** — full index (now flags pass-1 errors)
- **`work/analysis/10_second_pass_findings.md`** — corrections + new findings
- **`work/analysis/09_action_items.md`** — full prioritised backlog (P1-1..P4-3)
- **`work/analysis/04_effect_id_bug.md`** — the main bug, with proposed patch sketch
- **`work/analysis/05_periodic_0x04_anomaly.md`** — Hypothesis B now confirmed
- **`work/analysis/08_tshark_recipes.md`** — reproducible filter recipes
