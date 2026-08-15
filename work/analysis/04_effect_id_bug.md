# 04 — 🔴 The `effect_id` bug

## Summary

The driver assumes `effect_id` (the second byte of every `0x01` main-upload packet and
every `0x41` START/STOP packet) is **always `0x00`**. Captures prove it is not: the real
protocol uses the **slot index** (which equals the param_sub-derived slot number).

This is the single most important driver bug surfaced by the captures.

## Evidence

### Source code (the assumption)

`src/tmt500rs/hid-tmt500rs.h:28-34`:
```c
/* Effect ID: T500RS always uses 0x00 for 0x01 uploads and 0x41 START/STOP.
 * Non-zero IDs mute constant force and make other effects unreliable
 * (see docs/T500RS_FFBEFFECTS.md). The init-time autocenter STOP is the only
 * exception, which targets a fixed ID 15. */
#define T500RS_EFFECT_ID 0x00
#define T500RS_AUTOCENTER_STOP_ID 15
```

`docs/T500RS_FFBEFFECTS.md:64-71`:
> On the T500RS, the `effect_id` byte is **always `0x00`** ... If you send anything
> other than `0x00`, the wheel does not crash, but constant force produces **no torque
> at all** and other effects become unreliable. This was confirmed on real hardware.

### Capture evidence (the contradiction)

Both community captures show non-zero `effect_id` for non-constant effects:

| Source | Frame | Bytes (first 4) | Decoded |
|--------|-------|------------------|---------|
| **C1** | 73 | `01 01 41 40 ...` | `0x01` upload, **effect_id=0x01**, type=0x41 (DAMPER) |
| **C2** | 2657 | `01 01 41 40 ...` | `0x01` upload, **effect_id=0x01**, type=0x41 (DAMPER) |
| **C2** | 373805 | `01 01 41 40 ...` | (same) |

And corresponding START/STOP commands with the same non-zero ID:

| Source | Bytes | Decoded |
|--------|-------|---------|
| C1 | `41 01 41 01` | `0x41` START, **effect_id=0x01** |
| C1 | `41 01 00 01` | `0x41` STOP, **effect_id=0x01** |
| C2 | `41 01 41 01`, `41 00 41 01`, `41 01 00 01`, `41 00 00 01` | Mix of slot 0 and slot 1 |

### The pattern

Looking at the subtype formula in the driver:

```c
*param_sub = 0x000e + (0x001c * idx);   // idx 0 → 0x0e,  idx 1 → 0x2a,  idx 2 → 0x46
*env_sub   = 0x001c + (0x001c * idx);   // idx 0 → 0x1c,  idx 1 → 0x38,  idx 2 → 0x54
```

And the captures:
| effect_id | param_sub | env_sub | slot n |
|-----------|-----------|---------|--------|
| 0x00 | 0x000e | 0x001c | 0 |
| 0x01 | 0x002a | 0x0038 | 1 |

**The relationship is exact: `effect_id == slot_index == (param_sub - 0x0e) / 0x1c`.**

The slot index the driver already computes internally (and writes into `param_sub` /
`env_sub`) is meant to be echoed in the `effect_id` byte too. The driver only writes it
into the subtypes and hardcodes `0x00` for the ID.

## What the doc got wrong

`docs/T500RS_FFBEFFECTS.md` claims non-zero IDs "mute constant force". Re-reading the
doc carefully, the warning is actually:

> If you send anything other than `0x00`, ... constant force produces **no torque at all**

But the captures show that **the constant force slot (slot 0) always uses effect_id=0x00
in practice**, while non-constant effects use their own slot's ID. The doc warning may
be a misinterpretation of: "if you put a non-zero effect_id on a CONSTANT upload, the
constant force breaks" — which is consistent with both captures.

In other words, the rule is **not "effect_id is always 0"**, it's **"the constant-force
slot's effect_id is 0"**. Other slots use their own ID.

## Why the current driver "works" today

The current driver sends:
- `0x01` upload: `effect_id=0x00` for ALL effects (constant, periodic, condition, ramp)
- `0x41` START/STOP: `effect_id=0x00` for ALL effects

For single-effect scenarios this works because the wheel has only one slot active at a
time. The driver compensates with the per-effect **subtypes** (which DO vary per slot).

For multi-effect scenarios (e.g. a game using both constant force and a damper
simultaneously), the wrong `effect_id` may cause:
- START commands hitting the wrong slot
- STOP commands stopping the wrong effect (the driver's "global STOP" workaround at
  line 1441+ tries to mitigate this, but it's a band-aid for the wrong root cause)
- The `0x41` arg byte (`0x01`) being misinterpreted when `effect_id=0x00` but the
  effect lives in slot 1+

## Proposed fix

### Step 1: change the protocol constants

```diff
- /* Effect ID: T500RS always uses 0x00 for 0x01 uploads and 0x41 START/STOP.
-  * ... */
- #define T500RS_EFFECT_ID 0x00
- #define T500RS_AUTOCENTER_STOP_ID 15
+ /* Per Windows USB captures (community captures f65, f73, f2637, f2657), the
+  * effect_id byte mirrors the slot index derived from the param_sub formula:
+  *   effect_id == (param_sub - 0x000e) / 0x001c
+  *
+  * The constant-force slot (slot 0) uses effect_id=0; every other slot uses its
+  * own index. START/STOP commands address the specific slot's effect_id.
+  *
+  * The init-time autocenter teardown uses the dedicated slot 15.
+  */
+ #define T500RS_AUTOCENTER_STOP_ID 15
```

### Step 2: thread effect_id through the upload path

`t500rs_send_packet_sequence()` already computes `param_sub`/`env_sub` per effect.
Add an `effect_id` derivation alongside:

```c
u8 effect_id;
if (effect->type == FF_CONSTANT) {
    effect_id = 0;                          /* slot 0 = constant */
    param_sub = T500RS_CONSTANT_PARAM_SUB;  /* 0x0e */
    env_sub   = T500RS_CONSTANT_ENV_SUB;    /* 0x1c */
} else {
    unsigned int slot = effect->id + 1;
    effect_id = (u8)slot;                    /* slot 1, 2, ... */
    t500rs_index_to_subtypes(slot, &param_sub, &env_sub);
}
```

Then pass `effect_id` into `t500rs_build_r01_main()` (already a parameter — just stop
hardcoding `T500RS_EFFECT_ID`).

### Step 3: per-slot START/STOP

`t500rs_send_start()` and `t500rs_send_stop_now()` need an `effect_id` parameter:

```c
static int t500rs_send_start(struct t500rs_device_entry *t500rs, u8 effect_id)
{
    r41->id = 0x41;
    r41->effect_id = effect_id;     /* was T500RS_EFFECT_ID */
    r41->command = 0x41;
    r41->arg = 0x01;
    ...
}
```

`play_effect` / `stop_effect` then pass `effect->id + (effect->type != FF_CONSTANT)`.

### Step 4: simplify the expiry tracker

Once START/STOP are per-slot, the "global STOP" hack (lines 1395-1471, ~80 lines) can
be removed entirely. Each effect's STOP only halts that slot. This is a major
simplification.

The `t500rs_active_effect.playing` flag stays (still needed to track software-expiry
deadlines since the device has no auto-stop), but the
`t500rs_any_playing_locked()` gating logic goes away.

### Step 5: validate against captures

After the change, replay a known game session and verify the produced packet sequence
matches `04_capture_inventory.md` rows for `0x01` and `0x41`. Specifically:
- Constant force upload: `01 00 00 40 ... 0e 00 1c 00 00 00` ✅ (unchanged)
- Damper upload: `01 01 41 40 ... 2a 00 38 00 00 00` ✅ (was `01 00 41 ...`)
- START constant: `41 00 41 01` ✅ (unchanged)
- START damper: `41 01 41 01` ✅ (was `41 00 41 01`)
- STOP constant: `41 00 00 01` ✅
- STOP damper: `41 01 00 01` ✅

## Risk assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| Existing games may have learned to live with the wrong IDs (the doc was wrong but stable) | Medium | Test with a small panel of games (DiRT Rally 2.0, rFactor 2, Assetto Corsa) before/after |
| The autocenter init STOP at slot 15 (`T500RS_AUTOCENTER_STOP_ID`) is unaffected — keep it | Low | No change to that constant |
| The "expiry tracker" simplification could regress the DiRT Rally 2.0 "sustained force + transient" scenario | Medium | Run that scenario specifically |
| If real hardware DOES reject non-zero IDs in some firmware version (per old doc), we'd regress | Low | Add a module param `legacy_zero_effect_id` to revert |

## Recommendation

**Apply the fix.** It aligns the driver with two independent community captures and
simplifies the code substantially. The old doc claim is contradicted by the captures
and was likely a misreading of "don't put non-zero IDs on constant force".

The doc (`docs/T500RS_FFBEFFECTS.md` section 3 "The one rule that trips everyone up")
should be rewritten as:

> On the T500RS, each effect slot has a fixed `effect_id` equal to its index:
> constant force = slot 0, every other effect = its assigned slot 1..14. The
> `effect_id` byte in `0x01` and `0x41` packets must match the slot encoded in
> the param_sub / env_sub channels. Constant force's slot is always 0; do not
> put a non-zero ID on a constant-force upload.
