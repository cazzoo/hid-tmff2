# T500RS USB refactor plan (based on issue comments 3474338182 and 3476252158)

Purpose: Align src/tmt500rs/hid-tmt500rs-usb.c with the tmff2 framework (as T300RS/T248 do), remove duplicated infrastructure, and stay within black‑box reverse‑engineering boundaries. This implements the eight items we agreed on, mapped to code locations and verification steps.

## Scope of changes

1) Remove duplicated sysfs and per‑effect software gains
- Delete custom attributes: constant_gain, periodic_gain, t500rs_spring_gain, t500rs_damper_gain
- Keep only base driver sysfs (PARAM_GAIN, PARAM_RANGE, PARAM_SPRING_LEVEL, PARAM_DAMPER_LEVEL, PARAM_FRICTION_LEVEL)
- Remove software re‑multiplication or per‑effect gain inflation

2) Simplify set_gain
- Accept already‑combined gain from base driver (device×game)
- Convert 0–65535 → device byte 0–127 and send Report 0x43

3) Remove second workqueue and async path
- Use a single synchronous usb_interrupt_msg helper (t500rs_send_usb)
- No per‑device workqueue, no custom work items

4) Remove continuous force streaming/timer
- Drop timer, work handler, and all related state
- For FF_CONSTANT: send one 0x03 level update followed by 0x41 START; subsequent updates via update_effect

5) Remove input open/close wrappers
- Do not wrap base open/close; avoid duplication and possible recursion/deadlocks

6) Eliminate sleeps in steady‑state paths
- Remove usleep_range in init sequence unless required by real device errata (kept none for now)
- Remove mdelay(2) during range stepping; let USB stack pace transfers

7) Allocation context
- Keep allocations safe; avoid sleeping where caller may be atomic (set_autocenter remains GFP_ATOMIC)

8) Update comments to neutral black‑box phrasing
- Avoid references to internal/illegal sources; rely on public behavior captures and empirical device responses

## Code mapping (current file)

- Struct t500rs_device_entry: removed wq/timer/lock/force state, duplicate gains, open/close pointers
- Removed: constant/periodic/t500rs_* gain sysfs section and creation/removal in init/destroy
- set_gain: simplified to byte mapping and 0x43 send
- play_effect/stop_effect/update_effect (FF_CONSTANT): no timer; send 0x03, START, and direct updates
- wheel_init: converted t500rs_send_usb_blocking→t500rs_send_usb; removed sleeps and sysfs creation; removed wq/timer init
- wheel_destroy: simplified to resource freeing only
- populate_api: removed open/close registration

## Validation

Build
- make (kernel module builds without warnings/errors)

Runtime smoke
- Load: sudo modprobe -r hid_tmff_new; sudo modprobe hid_tmff_new
- Logs: dmesg | tail -100 (no errors)
- Device: cat /proc/bus/input/devices | grep -A5 -B5 T500

Functional
- fftest /dev/input/eventX → constant force, spring, damper, friction
- Proton titles (if available): quick lap to confirm no stalls/crashes

Notes
- If any title relied on the old streaming hack, that will regress; this is by design and will be explored in a separate branch as requested.

