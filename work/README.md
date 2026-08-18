# T500RS Capture Analysis — Work Files

Reverse-engineering work product derived from `comunity-captures/*.pcapng` cross-referenced
against `src/tmt500rs/hid-tmt500rs.[ch]` and `docs/T500RS_FFBEFFECTS.md`.

These files are **evidence-driven** (every claim cites a frame number, hex bytes, or source
line) and intended as a permanent reference for future driver fixes.

## Captures analysed

| File | Source | Devices | Purpose |
|------|--------|---------|---------|
| `T500 win capture.pcapng` | Community (Windows 11, 2026-07-18) | T500RS @ `1.6.0` (PID **0xb65e**) | Standard rim, single effect (constant + damper) |
| `t500rs_f1_wheel_rfactor2_f1_1967_bt24_kyalami_1976_online.pcapng` | Community (Windows 10, 2026-07-20) | T500RS @ `2.3.0` (boot PID **0xb65d** → advanced PID **0xb662**) + hub + MSI peripheral | F1 attachment, rFactor2, full race session |

## File index

| # | File | What's inside |
|---|------|---------------|
| 00 | `analysis/00_capture_inventory.md` | Devices, PIDs, endpoints, packet-type census per capture |
| 01 | `analysis/01_boot_mode_switch.md` | The vendor request that flips T500RS from boot → advanced mode (NOT done by our driver) |
| 02 | `analysis/02_init_sequence_diffs.md` | Real Windows init sequence vs. our `t500rs_wheel_init()` |
| 03 | `analysis/03_packet_inventory.md` | Every report ID seen, with sample bytes + frequency |
| 04 | `analysis/04_effect_id_bug.md` | **Critical:** `effect_id` is NOT always `0x00` — driver assumption is wrong |
| 05 | `analysis/05_periodic_0x04_anomaly.md` | rFactor2 sends 32k `0x04` packets with code `0x0e` — Hypothesis B CONFIRMED |
| 06 | `analysis/06_unknown_reports.md` | OUT report `0x0a`, IN reports `0x07`/`0x14`, vendor-request `0x49` (was misidentified as IN report) |
| 07 | `analysis/07_condition_deadband_unverified.md` | Why `deadband / 65` is still TODO and how to verify |
| 08 | `analysis/08_tshark_recipes.md` | Reusable tshark filter recipes that produced these results |
| 09 | `analysis/09_action_items.md` | Prioritised list of bugs/uncertainties to address (P1-1..P4-5) |
| **10** | **`analysis/10_second_pass_findings.md`** | **Second-pass corrections + new findings. Read BEFORE pass-1 docs.** |
| **11** | **`analysis/11_vendor_request_polling.md`** | **Third correction: vendor request `0x49` polling during init (was misidentified as IN report).** |
| 12 | `analysis/12_hw_verification_procedure.md` | Step-by-step fftest + usbmon/tshark procedures to close P3-5 (periodic layout) and P3-6 (ramp envelope) on real hardware |
| **13** | **`analysis/13_periodic_wedge.md`** | **P3-5 verdict (🔴→✅): per-slot periodic declarations wedge the wheel; re-implemented as host-side synthesis (slot-0 sine MAIN + `04 0e` level stream). HW validation pending** |

> ⚠️ **Pass-1 docs (00-09) contain errors that passes 2 and 3 correct.**
> Always consult files 10 and 11 first. Major corrections:
> - The dominant C2 IN report is `0x07` (not `0x14` as pass-1 claimed)
> - **C1's "IN report 0x49" was actually 24 replies to Windows' vendor request `0x49` polling during init (200ms burst)** — not a low-rate heartbeat
> - C1 has ZERO interrupt-IN traffic (only vendor-request replies on control endpoint)
> - C2 has 64 vendor requests during init (boot mode + advanced mode), all stop after t=43.4s
> - `0x41` START arg byte is `0xff` in C2 (pass-1 didn't analyse this)
> - STOP is per-slot, not global (driver's workaround is built on a wrong assumption)
> - Driver's `40 11 42 7b` is a range command (526°), not an "FFB-arm magic"

## Headline findings (TL;DR)

> Updated 2026-08-12 after second pass. See `analysis/10_second_pass_findings.md` for
> the validation/correction pass.

1. **🔴 `effect_id` is not always `0x00`.** Validated across all 8 `0x01` + 10 `0x41`
   packets. The rule is `effect_id == slot_index`. Driver's hardcoded `0x00` is wrong.
   See `04_effect_id_bug.md`.
2. **🔴 `0x41` START arg byte is `0xff` in rFactor2** (driver hardcodes `0x01`).
   New P1-2. See `10_second_pass_findings.md` §2.
3. **🔴 Driver's `0x40 0x11 0x42 0x7b` "FFB-arm magic" is wrong** — it's actually a
   range command setting 526°. Windows never sends this. New P1-3. See `10_second_pass_findings.md` §5.
4. **🔴 STOP is per-slot, not global** — driver's ~80-line expiry-tracker workaround
   is built on a misreading. Both captures send `41 XX 00 01` for each slot. See
   `10_second_pass_findings.md` §2.
5. **🔴 `0x04` with code `0x0e` is 100% confirmed as constant-force-via-periodic.**
   b4 is signed level, b6/b7 are magic `0x2710`. Validated across all 32 222 packets.
   See `05_periodic_0x04_anomaly.md` (Hypothesis B confirmed).
6. **🟡 Boot-mode-switch is performed by `hid-tminit`/`hid-thrustmaster`, NOT by our driver.**
   Vendor request `0x53 wValue=0x0003` flips the wheel from `0xb65d` → `0xb662`/`0xb65e`.
   See `01_boot_mode_switch.md`.
7. **🟡 Dominant C2 IN report is `0x07`** (152 549 packets, 230 Hz, 15 bytes) — driver
   relies on stock HID parsing. Pass-1 incorrectly said `0x14` (only 6 packets).
8. **🟡 Unknown OUT report `0x0a`** sent 6× during F1 init — never sent by us.
9. **🟡 Unknown IN report `0x14`** sent 6× during init — likely device-identification.
   Contains base_wheel_PID `0xb65e` even on F1 rim. New P4-3.
10. **🟢 Init runs 3× in C2** (t=11.6, t=12.4, t=43.4) plus a 32-byte reset pair at
    t=42.4. Driver does it once. New P2-6.
11. **🟢 `42 05` (apply) sent up to 3× in a row** in both captures — driver sends once.
    New P2-5.
12. **🟢 ZERO `0x04` with code != `0x0e` in either capture** — driver's periodic support
    (`0x04 0x2a ...`) is unverified. New P3-5.
