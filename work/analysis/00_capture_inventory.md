# 00 — Capture inventory

## Capture 1: `T500 win capture.pcapng`

| Property | Value |
|----------|-------|
| Capture host | Windows 11 25H2 (build 26200), AMD Ryzen 5 5600X |
| Tool | Dumpcap / Wireshark 4.6.7 via USBPcap1 |
| Duration | 1937.9 s (≈32 min) |
| Total packets | 135 812 |
| Avg packet rate | 70 pkt/s |
| Earliest packet | 2026-07-18 06:33:41 |
| Game | unknown (probably a Windows FFB tester / simple game) |

### Devices

Only one USB device present:

| Bus.Addr | VID:PID | bcdDevice | Identity |
|----------|---------|-----------|----------|
| `1.6.0` | **0x044f:0xb65e** | 0x0100 | Thrustmaster T500RS (advanced / normal mode, standard rim) |

### Interface layout (1.6.0)

Single configuration, single interface:
- `bInterfaceClass=0x03 (HID)`, `bInterfaceSubClass=0x00`, `bInterfaceProtocol=0x00`
- Endpoint **0x82 (IN, interrupt, maxPacketSize = 16)** — wheel → host (input reports)
- Endpoint **0x01 (OUT, interrupt, maxPacketSize = 32)** — host → wheel (output reports)

> ⚠️ **No separate control-interface / audio / bootloader interface** — the T500RS exposes
> a single plain HID interface, unlike T300RS which exposes 2-3. This is unique to T500RS.

### Packet census (host → device, OUT endpoint 0x01)

67 879 OUT URBs with payload. Decomposed by report ID:

| Report ID | Count | Sample bytes | Meaning per driver |
|-----------|-------|--------------|--------------------|
| `0x03` | **67 864** | `03 0e 00 fd` | Constant-force level updates (slot 0, level=`fd`=-3) |
| `0x42` | 6 | `42 05`, `42 04`, `42 00` | Sync / handshake |
| `0x41` | 4 | `41 01 41`, `41 00 41`, `41 01 00`, `41 00 00` | START / STOP commands |
| `0x05` | 2 | `05 38 00 ...`, `05 2a 00 ...` | Condition (spring/damper) X and Y |
| `0x01` | 2 | `01 00 00 40 ff ff 00 ff ff 0e 00 1c 00 00 00` | Main upload (constant + damper) |
| `0x02` | 1 | `02 1c 00 ...` | Envelope (zeros) |

### IN packets (1.6.0 → host)

Only **26 IN packets** total across the whole capture. Pattern is **report ID `0x49`**,
16 bytes: `49 00 03 04 01 00 0a 00 03 00 00 00 02 02 00 00`. Very low rate (~1 per minute).
Not parsed by our driver. See `06_unknown_reports.md`.

### Enumeration sequence (frames 1–6, at t=0.000s)

| Frame | Direction | URB function | Setup | Notes |
|-------|-----------|--------------|-------|-------|
| 1 | host→1.6.0 | GET_DESCRIPTOR | `80 06 0100 0000 0012` | DEVICE descriptor request |
| 2 | 1.6.0→host | CONTROL_TRANSFER | — | Returns 0x044f:0xb65e / bcdDevice 0x0100 |
| 3 | host→1.6.0 | GET_DESCRIPTOR | `80 06 0002 0000 0029` | CONFIGURATION descriptor (41 B) |
| 4 | 1.6.0→host | CONTROL_TRANSFER | — | Returns 1 interface, HID class, 1 report descriptor |
| 5 | host→1.6.0 | SELECT_CONFIGURATION | `00 09 0000 0000 0000` | SET_CONFIGURATION |
| 6 | 1.6.0→host | SELECT_CONFIGURATION | — | ACK |

**No GET_DESCRIPTOR for HID Report (`0x22`)** is captured — Windows already has the
descriptor cached from a previous session, or it fetches it lazily after game start.

> **No traffic for ~960 seconds** between frame 6 (t=0.000s) and frame 7 (t=960.503s).
> Game / FFB app starts at t=960.5s. Real FFB traffic begins at frame 55 (t=960.714s).

---

## Capture 2: `t500rs_f1_wheel_rfactor2_f1_1967_bt24_kyalami_1976_online.pcapng`

| Property | Value |
|----------|-------|
| Capture host | Windows 10 22H2 (build 19045), AMD Ryzen 7 5800X3D |
| Tool | Dumpcap / Wireshark 4.6.7 via USBPcap2 |
| Duration | 661.6 s (≈11 min) |
| Total packets | 374 590 |
| Avg packet rate | 566 pkt/s |
| Earliest packet | 2026-07-20 10:44:36 |
| Game | **rFactor 2**, F1 1967 BT24 @ Kyalami 1976 (online race), **F1 attachment** |

### Devices (3 on the hub)

| Bus.Addr | VID:PID | bcdDevice | Identity |
|----------|---------|-----------|----------|
| `2.1.0` | 0x05e3:0x0608 | 0x8832 | Generic USB hub |
| `2.2.0` | 0x1462:0x7c37 | 0x0001 | MSI peripheral (motherboard HID?) — endpoint 0x81 |
| `2.3.0` | **0x044f:0xb65d** → **0x044f:0xb662** | 0x0100 | **T500RS with F1 rim** — boot mode then advanced mode |

> **Two different PIDs for 2.3.0 in the same capture = the boot-mode-switch dance.** Boot
> mode (`0xb65d`) is visible at t=8.726s, advanced mode (`0xb662`) appears at t=9.602s.
> See `01_boot_mode_switch.md`.

### Interface layout (2.3.0 in advanced mode)

Identical to capture 1: 1 HID interface, endpoints 0x82 (IN, 16 B) + 0x01 (OUT, 32 B).

### Packet census (host → 2.3.1, OUT endpoint)

32 277 OUT URBs with payload. Decomposed by report ID:

| Report ID | Count | Sample bytes | Meaning per driver |
|-----------|-------|--------------|--------------------|
| `0x04` | **32 222** | `04 0e 00 00 01 00 10 27` | Periodic — but **code = `0x0e`** (constant slot!) — see `05_periodic_0x04_anomaly.md` |
| `0x42` | 21 | `42 05`, `42 04`, `42 00`, `42 01 00` | Sync (note: **`42 01 00` not seen in capture 1**) |
| `0x40` | 7 | `40 11 55 55`, `40 04 00 00`, `40 03 0d 00` | Range, autocenter-disable, autocenter-strength |
| `0x41` | 6 | `41 01 41`, `41 00 41`, `41 00 00`, ... | START / STOP |
| `0x0a` | 6 | `0a 04 90 03 00 00 00 00` (+16-byte variant) | **UNKNOWN** — see `06_unknown_reports.md` |
| `0x01` | 6 | `01 00 22 40 ff ff 00 00 00 0e 00 1c 00 00 00` | Main upload (sine + damper) |
| `0x05` | 4 | `05 38 00 ...`, `05 2a 00 ...` | Condition X/Y |
| `0x02` | 4 | `02 1c 00 ...` | Envelope |
| `0x43` | 1 | `43 5a` | Gain = 90 (driver always uses `0xff`) |

### IN packets (2.3.2 → host)

**152 585 IN URBs**, dominant length 15 bytes (152 553 packets). Pattern starts with
report ID `0x14 0x20 ...` (state report at ~230 Hz). See `06_unknown_reports.md`.

### Time distribution

| Time window | Activity |
|-------------|----------|
| 0 – 8.5 s | Idle (hub enumeration only) |
| 8.5 – 9.6 s | **T500RS boot-mode-switch sequence** (see `01_boot_mode_switch.md`) |
| 9.6 – 11 s | Init: `42 04`, `42 01 00`, `40 11 55 55`, `40 04 00 00`, `40 03 0d 00`, `43 5a`, `42 05`, `42 00`, `0a 04 ...` |
| 11 – 109 s | Wheel idle (no FFB events); periodic state IN reports at 230 Hz |
| 109 – 627 s | Active FFB race session — 32 k `0x04` constant-force-like updates |
| 627 – 660 s | Second race burst (same pattern) |

---

## Cross-capture comparison (key deltas)

| Aspect | Capture 1 (std rim) | Capture 2 (F1 rim) |
|--------|---------------------|---------------------|
| Initial PID | `0xb65e` (already advanced) | `0xb65d` → `0xb662` (boot switch captured) |
| Init includes `42 01 00` | ❌ | ✅ |
| Init includes `0x0a 04 ...` reports | ❌ | ✅ (6×) |
| Init includes `40 11 55 55` (range set) | ❌ | ✅ (range ≈ 364°, F1-typical) |
| Init includes `40 03 0d 00` (autocenter strength) | ❌ | ✅ |
| Init includes `43 5a` (gain = 90) | ❌ | ✅ |
| Constant-force update report | `0x03` (67 864×) | `0x04` with code `0x0e` (32 222×) |
| Non-zero `effect_id` seen | ✅ (`01 01 41 ...`) | ✅ (`01 01 41 ...`) |
| IN report rate | ~1/min (0x49) | ~230/s (0x14) |

The two captures encode FFB very differently — suggesting **rFactor2 + F1 rim is a much
richer use case** than the simple Windows FFB test in capture 1. Driver decisions based
only on capture 1 may not generalise.
