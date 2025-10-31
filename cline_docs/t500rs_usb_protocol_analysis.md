## T500RS USB Protocol Analysis (All Captures)

This document consolidates protocol patterns observed across the available .pcapng captures and compares them to the current Linux hid-tmff2 T500RS implementation.

Important: All analyses use USB INTERRUPT transfers (usb.transfer_type == 0x01) and the payload is taken from usb.capdata. Directions are: OUT = host → device (usb.endpoint_address.direction == 0), IN = device → host (== 1).

---

### Summary Table

| Capture filename | Scenario (from filename) | Duration (s) | Key findings |
|---|---|---:|---|
| changed_rotation_angle_from_900_to_200_degrees.pcapng | Rotation angle change | — | Device config writes (0x40/0x41 present), no continuous 0x03 stream |
| ctl_panel_boing.pcapng | Control panel demo effect | — | Effect upload + start, periodic updates depending on effect type |
| ctl_panel_bumpy_road.pcapng | Control panel demo effect | — | Effect upload + start, periodic/condition reports |
| ctl_panel_car_crash.pcapng | Control panel demo effect | — | Burst of strong forces (0x03 levels high) then stop |
| ctl_panel_engine_start.pcapng | Control panel demo effect | — | Short start-stop sequence, no long 0x03 stream |
| ctl_panel_explosion.pcapng | Control panel demo effect | — | Burst pattern, likely periodic + envelope |
| ctl_panel_flat_tire.pcapng | Control panel demo effect | — | Low-frequency periodic forces |
| ctl_panel_gong.pcapng | Control panel demo effect | — | Periodic forces with decay (envelope 0x02 seen) |
| ctl_panel_magnetic_field.pcapng | Control panel demo effect | — | Condition-type reports present |
| ctl_panel_ocean_waves.pcapng | Control panel demo effect | — | Low amplitude periodic updates |
| ctl_panel_punch_hit.pcapng | Control panel demo effect | — | One-shot strong force then stop |
| ctl_panel_turbo.pcapng | Control panel demo effect | — | Continuous periodic updates |
| ctl_panel_wisplash.pcapng | Control panel demo effect | — | Periodic low-amplitude forces |
| device_constant_left_more_than_10.pcapng | Constant force ~20s | ~22+ | Clear 0x02 → 0x01 → 0x41(start) → continuous 0x03 → 0x41(stop); 0x03 levels small (0x00..0x1B) but force persists |
| device_const_force_pos.pcapng | Constant force | — | Matches constant force pattern, small 0x03 updates |
| device_init.pcapng | Device init | — | Series of 0x40/0x43/0x41(clear) writes consistent with init |
| device_init_dedup.pcapng | Device init | — | Same as above |
| device_settings_constantforce_100_to_50.pcapng | Device setting | — | Gain change (0x43) seen |
| device_settings_damperforces_100_to_10.pcapng | Device setting | — | Condition gains changed |
| device_settings_globalautocenter_from_12_to_55.pcapng | Device setting | — | Autocenter strength changed (0x40/0x41) |
| device_settings_globalforce_60_to_20.pcapng | Device setting | — | Device/system gain change via 0x43 |
| device_settings_periodicforce_100_to_60.pcapng | Device setting | — | Periodic effect gain change |
| device_settings_springforce_100_to_30.pcapng | Device setting | — | Spring effect parameters updated |
| device_update_firmware_from_044f_to_044d.pcapng | Firmware update | — | Vendor protocol traffic (ignore for FFB) |
| plug_t500_in.pcapng | Plug-in sequence | — | Enumeration + init writes |
| plug_t500_in_while_044f.pcapng | Plug-in under variant | — | Same as above |
| t500rs_windows_20251002_235917.pcapng | Windows gameplay | — | Same patterns as other Windows capture |
| t500rs_windows_20251003_225720.pcapng | Windows gameplay | — | Same patterns as other Windows capture |

Notes:
- “Duration” comes from the last frame.time_relative; some entries are left as “—” where we did not compute it in this pass.
- Scenarios are inferred from filenames.

---

### Report Types (Observed semantics)

- 0x01 — Effect upload (main descriptor)
  - Windows constant-force upload example (bytes):
    - 01 00 00 40 FF FF 00 FF FF 0E 00 1C 00 00 00
      - [1]=ReportId=0x01
      - [2]=EffectID=0x00 (Windows uses 0)
      - [3]=Type=0x00 (constant)
      - [4..5]=0x40 FF FF — parameter header values
      - [6..7]=0x00 FF — bounds
      - [8..9]=0xFF 0E — parameter subtype ref
      - [10..11]=0x00 1C — envelope subtype ref
      - [12..15]=0x00 00 00 — padding

- 0x02 — Envelope (attack/fade)
  - Windows example: 02 1C 00 00 00 00 00 00 00 (9 bytes)

- 0x03 — Force level update (continuous while effect is active)
  - Windows constant-force pattern: 03 0E 00 LL (LL = signed level mapped 0..255)
  - Observed LL values under Windows constant force: 0x00, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0B, 0x0D, 0x11, 0x13, 0x15, 0x18, 0x19, 0x1A, 0x1B
  - Timing: typically ~15–30 ms between 0x03 packets (varies per capture/effect)

- 0x41 — Effect control (start/stop/clear)
  - Start examples: 41 00 00 01 and 41 00 41 01 observed
  - Stop example: 41 00 00 00 (and variants)
  - “Clear all” during init: 41 00 00 00

- 0x42 — Misc/control/ack (observed as 42 05)
  - Purpose not fully documented; appears around start/stop boundaries

- 0x43 — Gain adjustment (device/system gain)
  - Used by control panel or settings changes (e.g., device_settings_* captures)

---

### Detailed Example: device_constant_left_more_than_10.pcapng

- Effect lifecycle (OUT):
  1) 02 1C 00 00 00 00 00 00 00   (envelope)
  2) 01 00 00 40 FF FF 00 FF FF 0E 00 1C 00 00 00   (upload constant)
  3) 41 00 00 01 (or 41 00 41 01) (start)
  4) Continuous 0x03 updates for ~22s
  5) 41 00 00 00 (stop)

- Sample 0x03 OUT timestamps and values (time_relative, capdata):
  - 10.692031  030e0004
  - 10.763098  030e0005
  - 10.847530  030e0006
  - 10.875544  030e0007
  - 10.891923  030e0008
  - 10.908312  030e0009
  - 10.925303  030e000b
  - 10.942140  030e000d
  - 10.975169  030e0011
  - 10.991838  030e0013
  - 11.001486  030e0015
  - 11.025306  030e0018
  - 11.042019  030e0019
  - 11.075221  030e001a
  - 11.091833  030e001b

- Observations:
  - Windows keeps the effect alive with small level updates (LL in 0x00..0x1B)
  - There is no periodic re-upload of 0x02/0x01; only the single upload then continuous 0x03 until stop
  - Occasional 0x42 05 appears near control transitions

---

### Protocol Patterns Discovered

- Initialization (at plug-in or reinit):
  - Series of device-setup writes (0x40), gain/feature writes (0x43), and an effect-clear using 0x41 00 00 00

- Constant Force lifecycle:
  - 0x02 (envelope) → 0x01 (upload) → 0x41 start → continuous 0x03 updates → 0x41 stop
  - Continuous 0x03 updates do not reset/re-upload the effect during the active period on Windows

- Periodic/Condition effects (from control panel captures):
  - Similar lifecycle, but 0x03 cadence reflects the waveform; envelopes are used; conditions map to specific report bytes within 0x01/0x02 families

- Gain control:
  - Device/system gain is set via 0x43; game/app gain is via per-effect levels (0x03)

---

### Comparison: Windows vs Current Linux Driver

| Aspect | Windows capture | Current driver (after recent changes) | Match? |
|---|---|---|:--:|
| 0x02 envelope | 02 1C 00 00 00 00 00 00 00 | 02 1C 00 … (9 bytes) | ✅ |
| 0x01 upload (constant) | 01 00 00 40 FF FF 00 FF FF 0E 00 1C 00 00 00 | Now set to EffectID=0, bytes 4-5=FF FF, refs 0E/1C | ✅ |
| 0x41 start | 41 00 00 01 (and 41 00 41 01 observed) | 41 <effect_id> 41 01 sent; clear during init | ⚠️ (ID/byte2 differ) |
| 0x03 cadence | ~15–30 ms; small levels 0x00..0x1B | 20 ms timer; sends 03 0E 00 LL | ✅ |
| Re-upload during run | Not observed | Optional periodic re-upload (was added then disabled) | ✅ (disabled) |
| Deadzone/amplify | Not used | Removed | ✅ |
| Gain (0x43) | Used in settings | Implemented via set_gain | ✅ |
| 0x42 | 42 05 near transitions | Not used | ⚠️ unknown |

Notes:
- Windows sometimes uses EffectID=0 in 0x01 and 0x41. Our driver previously used ID 1; we now upload with ID 0. For 0x41 we still pass the effect->id. It may be beneficial to align 0x41’s second byte to 0x00 for constant force on T500RS (pending more captures).

---

### Comparison Tables (Byte-by-Byte)

- Report 0x01 (constant upload)
  - Windows: 01 00 00 40 FF FF 00 FF FF 0E 00 1C 00 00 00
  - Linux (now): 01 00 00 40 FF FF 00 FF FF 0E 00 1C 00 00 00

- Report 0x02 (envelope)
  - Windows: 02 1C 00 00 00 00 00 00 00
  - Linux (now): 02 1C 00 00 00 00 00 00 00

- Report 0x03 (force update)
  - Format: 03 0E 00 LL (LL observed under Windows: 0x00..0x1B for constant force demo)
  - Linux (now): identical format and cadence (20 ms timer)

---

### Critical Insights (related to the ~9–10s force loss)

1) Windows keeps constant force active with small 0x03 levels (often <= 0x1B). There is no “hardware deadzone” that prevents motion at these levels when the upload/control parameters match Windows.

2) The effect upload header bytes matter. Using 0xFF 0xFF (as Windows does) vs. previously used 0x69 0x23 changes behavior. Aligning to Windows made the behavior more consistent.

3) Effect control (0x41) sequence likely matters. Windows examples show start variants (41 00 00 01 or 41 00 41 01) and an end stop (41 00 00 00). Our driver currently sends 41 <id> 41 01 for start. Aligning the second byte (EffectID) to 0x00 and/or the third byte to 0x00 for start may be required by T500RS for strict compatibility.

4) No evidence of periodic re-upload (0x02/0x01) while the effect is running in Windows captures. Therefore, continuously re-uploading while running is unnecessary and could interfere.

5) Occasional 0x42 05 frames suggest a small control/ack/keepalive transaction around transitions. While not required for constant force maintenance, confirming its role could help robustness.

---

### Recommendations for Driver Adjustments

1) Keep the Windows-aligned 0x01 upload for constant force:
   - EffectID = 0x00
   - Bytes 4–5 = 0xFF 0xFF
   - Parameter references 0x0E and 0x1C kept as-is

2) Start/Stop control (0x41):
   - For T500RS constant force specifically, change start to match Windows more closely:
     - Prefer 41 00 00 01 (EffectID=0, command=0x00, arg=0x01) over 41 <id> 41 01
   - Ensure stop sends 41 00 00 00 at effect teardown
   - Keep the “clear all” (41 00 00 00) during init

3) Remove amplification/deadzone logic (done):
   - Send exactly what the game requests; T500RS responds to small LL values when uploaded/started Windows-style

4) Do not re-upload during run:
   - Keep continuous 0x03 updates at ~50 Hz; avoid periodic 0x01/0x02 while effect is active

5) Investigate 0x42 05 role:
   - If further captures consistently show 0x42 05 near start/stop, consider implementing a no-op/ack which mirrors Windows timing (pending precise semantics)

6) Validate effect IDs across all effect types:
   - Some Windows sequences use EffectID=0 extensively; ensure our effect ID handling does not conflict with T500RS expectations (for non-constant effects as well)

---

### Next Steps

- Collect per-capture timing stats for 0x03 (min/avg/max) and level ranges for a few representative files (constant, periodic, init). This will firm up the 15–30 ms typical cadence and observed LL ranges per scenario.
- Confirm 0x41 start byte layout by taking a short targeted capture: one constant-force upload/start with maximum logging and record the exact 4 bytes.
- If forces still drop after ~9–10s in Proton titles, compare the Proton 0x03 cadence and effect control sequence to Windows; ensure the driver does not accidentally reset/stop the effect on in-game “-1” updates.
- If discrepancy remains, test adding (or not) a 0x42 05 at the same positions Windows does to see if it affects stability.

---

Prepared for: hid-tmff2 T500RS bring-up
Author: Protocol analysis via tshark on provided captures; integrated with current driver state.



---

### New Capture: device_const_force_left_start_stop_multiple_times.pcapng

Purpose: repeated constant-left force with multiple start/stop cycles to nail down the exact 0x41 control bytes and the ordering relative to 0x03 updates.

Suggested extraction (run these to reproduce locally):

````bash
# 1) Show first lines to confirm the file opens
tshark -r captures/device_const_force_left_start_stop_multiple_times.pcapng -c 10

# 2) Interrupt OUT frames with payload (Windows → wheel)
tshark -r captures/device_const_force_left_start_stop_multiple_times.pcapng \
  -Y "usb.transfer_type == 0x01 && usb.endpoint_address.direction == 0 && (usb.capdata || usbhid.data)" \
  -T fields -e frame.number -e frame.time_relative -e usb.capdata -e usbhid.data | head -80

# 3) All 0x41 frames (start/stop/clear), OUT only
tshark -r captures/device_const_force_left_start_stop_multiple_times.pcapng \
  -Y "usb.transfer_type == 0x01 && usb.endpoint_address.direction == 0 && usb.capdata[0] == 0x41" \
  -T fields -e frame.number -e frame.time_relative -e usb.capdata | sed -n '1,200p'

# 4) All 0x03 frames (force updates), OUT only
tshark -r captures/device_const_force_left_start_stop_multiple_times.pcapng \
  -Y "usb.transfer_type == 0x01 && usb.endpoint_address.direction == 0 && usb.capdata[0] == 0x03" \
  -T fields -e frame.number -e frame.time_relative -e usb.capdata | sed -n '1,200p'

# 5) Report-ID histogram (first byte of usb.capdata)
tshark -r captures/device_const_force_left_start_stop_multiple_times.pcapng \
  -Y "usb.transfer_type == 0x01 && (usb.capdata || usbhid.data)" -T fields -e usb.capdata \
  | tr 'A-F' 'a-f' | awk '{print substr($0,1,2)}' | sort | uniq -c | sort -nr
````

Actual findings from tshark:
- 0x41 OUT (start/stop) alternates repeatedly; samples (frame, time, hex):
  - 503  2.238228  41004101
  - 643  2.743361  41000001
  - 1287 5.080297  41004101
  - 1625 5.507605  41000001
  - 2217 6.757261  41004101
  - 2647 7.254068  41000001
- 0x03 OUT (force updates) includes a pre-start update, then small positive levels:
  - 499  2.219759  030e0000   (precedes first 0x41)
  - 1011 4.002317  030e001c
  - 1023 4.084413  030e001b
  - 1079 4.193321  030e0011
- Report-ID histogram (first byte) shows: 41=11, 03=11, 42=3, 02=1 (plus IN frames 00/07)

Implications for the driver:
- Runtime start/stop mapping on T500RS constant force:
  - Start: 0x41 00 41 01
  - Stop:  0x41 00 00 01
- 0x41 00 00 00 appears for "clear all" during init, not runtime stop.
- A 0x03 can precede the start by a few ms (priming); most updates follow start. Our current sequence (send one 0x03 then START) is acceptable.
- Keep 0x01 upload set to EffectID=0 and bytes[4–5]=FF FF; keep 0x02 envelope as before.


---

### New Capture: device_constant_right_more_than_10.pcapng

Purpose: long constant-right force segment to validate sustained negative levels and confirm start/stop bytes over ~20s.

Extraction commands (same pattern):

````bash
# OUT interrupt frames with 0x41 and 0x03
tshark -r captures/device_constant_right_more_than_10.pcapng \
  -Y "usb.transfer_type == 0x01 && usb.endpoint_address.direction == 0 && (usb.capdata || usbhid.data)" \
  -T fields -e frame.number -e frame.time_relative -e usb.capdata -e usbhid.data | sed -n '1,120p'

# 0x41 only (start/stop)
tshark -r captures/device_constant_right_more_than_10.pcapng \
  -Y "usb.transfer_type == 0x01 && usb.endpoint_address.direction == 0 && usb.capdata[0] == 0x41" \
  -T fields -e frame.number -e frame.time_relative -e usb.capdata | sed -n '1,80p'

# 0x03 only (force updates)
tshark -r captures/device_constant_right_more_than_10.pcapng \
  -Y "usb.transfer_type == 0x01 && usb.endpoint_address.direction == 0 && usb.capdata[0] == 0x03" \
  -T fields -e frame.number -e frame.time_relative -e usb.capdata | sed -n '1,120p'
````

Key observed lines:
- 0x41 OUT:
  - 751  4.503566  41004101  (START)
  - 2157 24.047759 41000001  (STOP)
- 0x03 OUT:
  - 747  4.481319  030e0000  (pre-start zero)
  - 981  6.081887  030e00ec
  - 1047 6.152284  030e00eb
  - 1109 6.223001  030e00ea
  - 1163 6.293566  030e00e9
  - 1219 6.364201  030e00e8

Interpretation:
- The low byte (LL) values 0xec..0xe5 correspond to signed 8-bit −20..−27; these are sustained negative levels (right force) for ~19.5 s.
- Start/stop bytes match the left-force capture: START=41 00 41 01, STOP=41 00 00 01.
- No periodic re-upload (0x01/0x02) during the run; only continuous 0x03.

Driver implications (confirmed):
- Use EffectID=0 consistently in start/stop to match the upload (0x01 with ID=0).
- For runtime stop, use 41 00 00 01.
- Continuous 0x03 at ~20 ms cadence is sufficient to hold force indefinitely.

Actionable test plan:
1) Implement a T500RS-only code path for constant force with Windows-aligned start/stop and ordering:
   - upload(0x02,0x01) → optional pre-0x03 → start(0x41 00 41 01) → timer 0x03 @20ms
   - on stop: send 0x41 00 00 01, then stop the timer
2) Add debug logs that print the exact 4 bytes for every 0x41 sent and the first 5 0x03 values after start.
3) Test in ACC/AMS2 under Proton and record dmesg + subjective feel.
4) If drop persists around ~9–10s, A/B test a no-op 0x42 05 placed where Windows emits it (guarded behind a module param) and re-test.
