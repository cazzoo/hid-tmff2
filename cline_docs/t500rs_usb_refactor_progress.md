# T500RS USB refactor – progress log (keep updated)

Rule: After each meaningful change or validation run, update this file with what changed, why, and next steps.

## 2025-11-01

Initial implementation pass (commit to local workspace; build verified):

- Removed duplicated infrastructure
  - Deleted custom sysfs attrs: constant_gain, periodic_gain, t500rs_spring_gain, t500rs_damper_gain
  - Removed open/close wrappers
  - Removed second workqueue, timer, and continuous force streaming logic
  - Dropped duplicated device/game gain state; accept base-combined gain only
- Simplified code paths
  - set_gain now maps 0..65535  0..127 and sends 0x43
  - FF_CONSTANT: play -> send 0x03 then 0x41 START; update -> single 0x03; stop -> 0x41 STOP
  - wheel_init: switched to synchronous sends; removed deliberate sleeps
  - wheel_destroy: minimal cleanup only
- Safety/legality
  - Comments rephrased to black-box methodology

Verification
- Build: make  success (hid_tmff_new.ko produced)

Next steps
- Load module and smoke-test (fftest, device detection, basic effects)
- Validate set_range transitions without the mdelay; reintroduce bounded micro-delays only if hardware proves to need them
- If any regressions surface in specific titles, document and prepare separate branch with streaming experiment (per user request)


## 2025-11-01 (cont.)

Reload attempt
- Tried to run reload_modules.sh via sudo -n to avoid interactive prompts; system requires password and blocked execution.
- Could not perform module reload in this session due to lack of root privileges.

Next steps
- User to run: sudo ./reload_modules.sh from repository root
- After running, capture outputs and dmesg; I will update this log and proceed with functional smoke tests.


## 2025-11-01 (cont.)

Crash/WARN triage and fix
- Observed kernel WARNING during probe: "transfer buffer is on stack" from usb_hcd_map_urb_for_dma, originating at t500rs_set_gain() via t500rs_send_usb()
- Root cause: passing stack-allocated buffers to USB core (DMA-mapped) in two places
  - set_gain: used `u8 buf[2]`
  - update_effect (FF_CONSTANT): used `u8 buf3[4]`
- Fix: switched both paths to use the preallocated DMA-safe `t500rs->send_buffer`
  - set_gain now uses `send_buffer` and checks for NULL
  - update_effect (constant) now uses `send_buffer` and checks for NULL

Validation
- Rebuilt: make success
- Reloaded via sudo ./reload_modules.sh: success
- dmesg: T500RS initialized successfully (USB INTERRUPT mode); no new WARN/OOPS entries related to transfer buffer

Next steps
- Functional smoke test with fftest: constant force, spring, damper, friction
- Tune set_range if necessary (only add minimal bounded delays if hardware requires pacing)


## 2025-11-01 (cont.)

Game start crashes – scheduling while atomic (fixed)
- Symptom: On starting a game, multiple kernel BUGs: "scheduling while atomic" in tmff2_work_handler calling t500rs_* (stop/upload) → usb_start_wait_urb
- Root cause: We had switched to synchronous USB calls (usb_interrupt_msg) which block. Base driver calls device callbacks from an atomic context (workqueue with spinlocks held). Blocking there triggers the BUG.

Fix
- Restored a dedicated high-priority workqueue for T500RS USB I/O
- Implemented a lock-free ring queue protected by spinlock
- Replaced tmff2 callbacks with queue wrappers so atomic contexts only enqueue:
  - set_gain, set_range, set_autocenter, upload_effect, update_effect, play_effect, stop_effect
- The worker (process context) performs the actual t500rs_send_usb() calls
- Added forward declarations to satisfy build order for worker references

Validation
- Rebuilt and reloaded via sudo ./reload_modules.sh
- Ran: fftest /dev/input/event8
  - Master gain set OK; Constant/Spring/Damper/Rumble uploaded OK; Periodic sinusoidal upload returned -EINVAL (non-fatal; will address later)
  - Played Constant, Spring, Damper successfully
- dmesg: No new "scheduling while atomic" after reload and fftest; saw tmff2_play scheduling messages only (expected, non-fatal)

Next steps
- Add/verify periodic effect parameter mapping (current -EINVAL)
- Broader in-game validation; capture any new logs and adjust queueing scope if any path still sleeps in atomic
- Consider rate limiting of tmff2_play "Work already pending" log spam


## 2025-11-01 (cont.)

Periodic effect upload fixed
- Symptom: fftest showed "Uploading effect #0 (Periodic sinusoidal) ... Error:: Invalid argument"
- Cause: Periodic upload sequence was missing the main effect (Report 0x01) that sets the waveform/type before sending Report 0x04 parameters
- Fix: Inserted Report 0x01 (effect type 0x20..0x24) between Report 0x02 (envelope) and Report 0x04 (periodic params)

Validation
- Rebuilt + reloaded via reload_modules.sh
- fftest /dev/input/event8:
  - Periodic sinusoidal now uploads and plays: "Now Playing: Sine vibration"
  - Previously working effects (Constant/Spring/Damper/Rumble) remain OK
- dmesg: no new errors; only benign tmff2_play scheduling messages from base driver

Next
- Broader in-game validation for periodic effects (sine/square/triangle/saw)
- Keep an eye on range/autocenter behavior in titles; adjust if required


## 2025-11-01 (cont.)

ABS feedback / periodic strength mapping
- Observation: In-game ABS felt weak; rumble is rewritten to FF_PERIODIC (sine, 50 ms) by base driver.
- Change: Adjusted global gain mapping to device range for Report 0x43. set_gain now maps 0..65535 -> 0..255 (0xFF = 100%), matching device init and ensuring full headroom for periodic/rumble-derived ABS.
- Implementation: Updated both queued and direct set_gain paths to use 255 scale; Report 0x43 payload now uses full byte.

Validation
- Build + reload OK; no new dmesg warnings (only expected tmff2_play info logs).
- fftest: periodic plays OK.
- User report: ABS is now felt in-game.

Next
- If further boost is desired, add optional module params (conservative defaults):
  - periodic_boost_percent (scale periodic magnitude)
  - periodic_min_mag (raise minimum periodic amplitude)
