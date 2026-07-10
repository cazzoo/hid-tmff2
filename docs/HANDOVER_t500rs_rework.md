# Handover: T500RS driver rework (EffectID=0x00 + software expiry)

**Repo:** `cazzoo/hid-tmff2`
**Branch:** `session/agent_a361d90b-d010-4399-8b16-7988ad63f2f8` (pushed to `origin`)
**Prepared for:** a local agent with T500RS hardware access (build + load + capture-verify)
**Date:** 2026-07-10

---

## 1. Goal

Make the T500RS force-feedback driver behave like the other wheels in this
repo (T300RS etc.): an effect that is started should **terminate when its
`replay.length` elapses**, instead of running forever and accumulating.

Root cause (established from `docs/T500RS_FFBEFFECTS.md` + capture analysis):

1. **EffectID must be `0x00`** for every `0x01` upload **and** every `0x41`
   START/STOP. The old code put a per-effect hardware id (`1..15`) into this
   field. The wheel does not crash, but **constant force produces no torque**
   and other effects become unreliable. This is the primary divergence from
   T300RS.
2. **The T500RS hardware never auto-stops an effect.** Once STARTed it plays
   until an explicit `0x41` STOP. The shared `tmff2` core relies on hardware
   auto-stop (which T300RS has via a count field) to end finite effects; T500RS
   lacks it, so finite effects run forever. A **driver-side software-expiry
   tracker** is required.

---

## 2. Authoritative protocol facts (`docs/T500RS_FFBEFFECTS.md`)

- `EffectID = 0x00` for all `0x01` and `0x41` (only exception: init-time
  autocenter STOP at fixed id `15` — not used by this code path, which disables
  autocenter via `0x40 0x04`).
- **Constant force uses FIXED subtypes** and must NOT use per-effect subtypes:
  - `0x01` b9 (param) = `0x0e`, b11 (env) = `0x1c`
  - `0x02` subtype = `0x1c`
  - `0x03` code = `0x0e`
  - Using per-effect subtypes for constant breaks level updates (no force felt).
- Per-effect slot index `n` for non-constant effects:
  - `param_sub = 0x0e + 0x1c * n`
  - `env_sub  = 0x1c + 0x1c * n`
  - The wheel distinguishes simultaneous effects by these subtypes, not by
    `effect_id`.
- `0x41` START/STOP: `id=0x41`, `effect_id=0x00`, `command=0x41` (START) or
  `0x00` (STOP), `arg=0x01`.
- Constant & periodic `0x01` duration should be `0xffff` (run until STOP);
  ramp uses real ms. The driver can send the real `replay.length` in the `0x01`
  field (the device ignores it and runs until STOP) — the software expiry
  enforces the real duration.

---

## 3. Current state of the code

### 3a. Reworked HID-report version — `.preview/tmt500rs.c` (committed at HEAD)
This is the full rework, **already complete and committed**. It:
- sends `EffectID = 0x00` for `0x01` and `0x41` (`T500RS_EFFECT_ID`);
- special-cases constant force to fixed subtypes `T500RS_CONSTANT_PARAM_SUB`
  (`0x0e`) / `T500RS_CONSTANT_ENV_SUB` (`0x1c`);
- derives non-constant subtypes from `effect->id + 1`;
- implements the **software-expiry tracker** (see §4).

It is **NOT compiled** — `Kbuild` does not reference it.

### 3b. Built driver — `src/tmt500rs/hid-tmt500rs-usb.c` (compiled via Kbuild)
This is what actually loads today. It:
- already sends `effect_id = 0x00` for `0x01` (lines ~290,436,546,599,669,721)
  and `0x41` (lines ~245,824,860,873) — **the EffectID fix is already present**;
- uses the USB interrupt endpoint `0x01 OUT` (different transport from the
  HID-report version);
- has `t500rs_play_effect` / `t500rs_stop_effect` but **NO software-expiry
  tracker** — finite effects still run forever.

> NOTE: `.preview/tmt500rs.c` and `src/tmt500rs/hid-tmt500rs-usb.c` are two
> different implementations (HID-report vs USB-interrupt). The rework was done
> on the HID-report version; the built one is the USB-interrupt variant.

---

## 4. Software-expiry tracker design (port this into the built driver)

Reference implementation lives in `.preview/tmt500rs.c`. Port it into
`src/tmt500rs/hid-tmt500rs-usb.c`, adapting to its existing send helpers.

**Includes** (add if missing):
```c
#include <linux/jiffies.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
```

**Device struct** (`t500rs_device_entry`):
```c
spinlock_t expiry_lock;
struct delayed_work expiry_work;
u8 *expiry_buffer;          /* dedicated DMA-safe buffer, never send_buffer */
struct t500rs_active_effect {
    bool active;
    unsigned long start_j;   /* jiffies at play */
    u32 total_ms;           /* replay.length; 0 = infinite */
} active[T500RS_MAX_EFFECTS];
```

**play_effect** (on successful START):
```c
u32 total = effect->replay.length;
spin_lock_irqsave(&t500rs->expiry_lock, flags);
if (total == 0)
    t500rs->active[effect->id].active = false;   /* infinite: never stop */
else {
    t500rs->active[effect->id].active = true;
    t500rs->active[effect->id].start_j = jiffies;
    t500rs->active[effect->id].total_ms = total;
}
t500rs_expiry_arm_locked(t500rs);
spin_unlock_irqrestore(&t500rs->expiry_lock, flags);
```

**stop_effect**: mark `active[id]=false`, then `t500rs_expiry_arm_locked()`.

**arm helper** (caller holds `expiry_lock`):
```c
static void t500rs_expiry_arm_locked(struct t500rs_device_entry *t500rs)
{
    unsigned long soonest = 0; bool found = false;
    for (int i = 0; i < T500RS_MAX_EFFECTS; i++) {
        struct t500rs_active_effect *a = &t500rs->active[i];
        unsigned long deadline;
        if (!a->active || a->total_ms == 0) continue;
        deadline = a->start_j + msecs_to_jiffies(a->total_ms);
        if (!found || time_before(deadline, soonest)) { soonest = deadline; found = true; }
    }
    if (found) {
        long delay = (long)soonest - (long)jiffies;
        if (delay < 0) delay = 0;
        mod_delayed_work(system_wq, &t500rs->expiry_work, (unsigned long)delay);
    } else {
        cancel_delayed_work(&t500rs->expiry_work);
    }
}
```

**worker** (runs in workqueue context — use `expiry_buffer`, not `send_buffer`):
```c
static void t500rs_expiry_work(struct work_struct *work)
{
    struct t500rs_device_entry *t500rs =
        container_of(to_delayed_work(work), struct t500rs_device_entry, expiry_work);
    unsigned long flags, now = jiffies; bool any_expired = false;
    spin_lock_irqsave(&t500rs->expiry_lock, flags);
    for (int i = 0; i < T500RS_MAX_EFFECTS; i++) {
        struct t500rs_active_effect *a = &t500rs->active[i];
        if (!a->active || a->total_ms == 0) continue;
        if (time_after_eq(now, a->start_j + msecs_to_jiffies(a->total_ms))) {
            a->active = false; any_expired = true;
        }
    }
    t500rs_expiry_arm_locked(t500rs);
    spin_unlock_irqrestore(&t500rs->expiry_lock, flags);
    if (any_expired)
        t500rs_send_stop_now(t500rs, t500rs->expiry_buffer); /* global 0x41 STOP */
}
```

**init** (`t500rs_wheel_init`): `kzalloc(expiry_buffer, buffer_length)`,
`spin_lock_init`, `INIT_DELAYED_WORK(&expiry_work, t500rs_expiry_work)`,
`memset(active, 0, ...)`.
**destroy**: `cancel_delayed_work_sync(&expiry_work)`, `kfree(expiry_buffer)`.

> KNOWN LIMITATION (flagged in code): because `0x41` `effect_id` is always
> `0x00`, a single STOP halts all playback. This is correct for the common
> single-effect case. **Multi-simultaneous finite effects sharing one global
> STOP need hardware validation** — see Open Questions §6.

---

## 5. Build & test procedure (hardware agent)

```bash
# Build the module (needs kernel headers for the target kernel)
make

# On the test machine:
sudo cp hid-tmff-new.ko /lib/modules/$(uname -r)/...
sudo systemctl stop pipewire 2>/dev/null   # free the wheel from other users
sudo rmmod hid_tmff_new 2>/dev/null
sudo insmod hid-tmff-new.ko

# Functional test with fftest / ffmvforce (from linuxconsole / joystick tools)
fftest /dev/input/eventXX
# Start a CONSTANT effect with a finite length and confirm it STOPS on its own.
# Start a PERIODIC/RAMP/CONDITION effect with finite length; confirm it stops.
# Start an infinite (length=0) effect; confirm it keeps running until explicit stop.
```

**Capture diff (the key acceptance test):** capture USB traffic with `usbmon`
(or Wireshark) while running the tests above, and compare against the reference
Windows captures to confirm:
- every `0x01` has `effect_id = 0x00`;
- every `0x41` has `effect_id = 0x00` and a terminating STOP at the expected time;
- constant-force `0x03` uses code `0x0e` and actually produces torque.

Reference captures in repo: `captures/*.pcapng`, `t500rs_windows_*.pcapng`,
`source/t500rs_constant_force.pcapng`,
`device_const_force_left_start_stop_multiple_times.pcapng`.

---

## 6. Open questions / things to validate on hardware

1. **`0x41` STOP `arg` byte** — set to `0x01` (matches doc + existing code).
   Confirm against captures that this is correct for the STOP case.
2. **Global STOP semantics** — does a `0x41` STOP with `effect_id=0x00` stop
   only the most-recently-started effect, or everything? Determines whether the
   multi-simultaneous-finite-effect case needs re-START logic after a STOP.
3. **Constant torque** — after the `EffectID=0x00` + fixed-subtype change,
   verify constant force actually produces torque (the original bug).
4. **Compile** — this sandbox has no kernel headers, so the module was never
   actually built here. The first task on hardware is a successful `make`.

---

## 7. Decision needed: which file ships

Two reasonable paths; pick one and finish it:

- **(A) Port the expiry tracker into `src/tmt500rs/hid-tmt500rs-usb.c`**
  (the already-built USB-interrupt driver, whose EffectID handling is already
  correct). Lowest risk — only adds the missing tracker. **Recommended.**
- **(B) Make `.preview/tmt500rs.c` the built driver** (restore it to
  `src/tmt500rs/hid-tmt500rs.c`, wire into `Kbuild`, drop `hid-tmt500rs-usb.c`).
  Higher risk — different transport, untested on hardware in this form.

Either way, the end state must: build cleanly, send `EffectID=0x00` everywhere,
use fixed constant subtypes, and auto-stop finite effects via the tracker.

---

## 8. Quick file map

| File | Role | Rework status |
|------|------|---------------|
| `src/tmt500rs/hid-tmt500rs-usb.c` | **Built** driver (USB interrupt) | EffectID OK; **expiry tracker missing** |
| `.preview/tmt500rs.c` / `.h` | HID-report version, committed | **Complete** (EffectID + subtypes + expiry); not built |
| `src/hid-tmff2.c` / `.h` | Shared core (effect lifecycle) | Untouched (expiry is T500RS-only by design) |
| `src/tmt300rs/hid-tmt300rs.c` | Reference wheel (hardware auto-stop) | N/A |
| `docs/T500RS_FFBEFFECTS.md` | Authoritative T500RS protocol | Use as source of truth |
| `docs/T500RS_FFBEFFECTS.md`, `docs/FFBEFFECTS.md` | Capture-derived notes | Supporting |
