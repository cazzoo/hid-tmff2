# T500RS Gain Implementation

## Overview

This document describes the implementation of proper gain handling for the T500RS driver, including dynamic in-game gain adjustment and the two-level gain system.

## Problem Statement

### Issues Observed

1. **In-Game Gain Adjustment Failure**
   - When adjusting the FFB gain slider in games during gameplay, the force strength would drop significantly
   - The gain appeared to reset to a very low value instead of adjusting proportionally
   - This indicated the driver was not properly handling dynamic gain changes from the game's FFB API

2. **Inconsistent Force Feedback**
   - Constant force effects worked reliably
   - Other effects (damper, spring, friction) worked intermittently
   - Inconsistency was worse when multiple effects were combined
   - Particularly noticeable during off-road driving, curbs, rumble strips, and wheel lock during braking

## USB Protocol Analysis

### Captures Analyzed

Using tshark to analyze USB packet captures from Windows driver:
- `device_settings_globalforce_60_to_20.pcapng` - Global/Master gain adjustment
- `device_settings_periodicforce_100_to_60.pcapng` - Periodic effects gain
- `device_settings_springforce_100_to_30.pcapng` - Spring effects gain
- `device_settings_damperforces_100_to_10.pcapng` - Damper effects gain
- `device_settings_globalautocenter_from_12_to_55.pcapng` - Autocenter strength

### Protocol Discovery

**Report 0x43 - Set Global Gain**
```
Byte 0: 0x43 (Report ID)
Byte 1: 0x00-0x7F (Gain value, where 0x7F = 127 = 100%)
```

Example sequence from global force 60% → 20%:
```
0x43 0x4C  (76 decimal ≈ 60%)
0x43 0x4A  (74 decimal ≈ 58%)
...
0x43 0x19  (25 decimal ≈ 20%)
0x43 0x18  (24 decimal ≈ 19%)
```

**Key Findings:**
- Only global/master gain uses Report 0x43
- Per-effect gains (periodic, spring, damper) are NOT sent via USB
- Per-effect gains appear to be applied in software by the Windows driver
- The device only supports a single global gain value

## Two-Level Gain System

Force feedback systems use TWO gain levels that multiply together:

### 1. Device-Level Gain (Master Volume)
- Set via sysfs attributes or control panel settings
- Acts as a "master volume" for all effects
- Persistent across game sessions
- Range: 0-65535 (where 65535 = 100%)
- Sent to device via Report 0x43

### 2. In-Game Gain (Dynamic Adjustment)
- Set dynamically by games through the FFB API
- Allows games to adjust force strength during gameplay
- Changes frequently based on game events
- Range: 0-65535 (where 65535 = 100%)
- Applied by multiplying with device gain

### Combined Gain Calculation

```c
combined_gain = (device_gain * game_gain) / 65535
device_byte = (combined_gain * 127) / 65535
```

Example:
- Device gain: 65535 (100%)
- Game gain: 32768 (50%)
- Combined: (65535 * 32768) / 65535 = 32768 (50%)
- Device byte: (32768 * 127) / 65535 = 63 (0x3F)

## Implementation

### Data Structure Changes

Added gain tracking to `struct t500rs_device_entry`:

```c
/* Gain settings (0-65535 range, where 65535 = 100%) */
u16 device_gain;  /* Device-level master gain (set via sysfs) */
u16 game_gain;    /* In-game gain (set dynamically by game via set_gain callback) */
```

### Initialization

In `t500rs_wheel_init()`:
```c
/* Initialize gain settings to 100% (65535 = 100%) */
t500rs->device_gain = 65535;  /* Device-level master gain */
t500rs->game_gain = 65535;    /* In-game gain */
```

### Dynamic Gain Adjustment

Implemented in `t500rs_set_gain()`:

```c
int t500rs_set_gain(void *data, u16 gain)
{
    struct t500rs_device_entry *t500rs = data;
    u8 buf[4];
    u8 device_gain_byte;
    u32 combined_gain;
    
    /* Store the game's gain setting */
    t500rs->game_gain = gain;
    
    /* Calculate combined gain: (device_gain * game_gain) / 65535 */
    combined_gain = ((u32)t500rs->device_gain * (u32)gain) / 65535;
    
    /* Convert to device range (0-127, where 127 = 100%) */
    device_gain_byte = (u8)((combined_gain * 127) / 65535);
    
    /* Send Report 0x43 - Set global gain */
    buf[0] = 0x43;
    buf[1] = device_gain_byte;
    
    return t500rs_send_usb(t500rs, buf, 2);
}
```

### Effect Playing

Removed software gain application from `t500rs_play_effect()`:

**Before:**
```c
/* Apply global gain (from hid-tmff2.c) */
extern int gain;
level = (level * gain) / 65535;
```

**After:**
```c
/* NOTE: Gain is now sent to device via Report 0x43 in set_gain() */
/* No need to apply gain here - the device handles it */
```

## Benefits

1. **Proper Dynamic Gain Adjustment**
   - Games can now adjust FFB strength during gameplay
   - Gain changes are sent immediately to the device
   - No more "gain reset" issues

2. **Hardware-Accelerated Gain**
   - Gain is applied by the device hardware, not in software
   - More accurate and responsive
   - Reduces CPU overhead

3. **Two-Level Control**
   - Device-level gain acts as master volume
   - In-game gain allows dynamic adjustment
   - Both levels multiply together correctly

4. **Standards Compliance**
   - Follows standard force feedback system behavior
   - Compatible with all games that use FFB API
   - Matches Windows driver behavior

## Testing

### Test Procedure

1. **Build and install driver:**
   ```bash
   make clean && make
   sudo make install
   sudo ./reload_modules.sh
   ```

2. **Test dynamic gain adjustment:**
   - Start a racing game with FFB support
   - Adjust the FFB gain slider in-game while driving
   - Verify force strength changes proportionally
   - Verify no "gain reset" or sudden drops

3. **Test combined effects:**
   - Drive over curbs and rumble strips
   - Test wheel lock during braking
   - Verify all effect types work consistently
   - Check for proper force strength

### Expected Results

- ✅ In-game gain slider works correctly
- ✅ Force strength adjusts proportionally
- ✅ No sudden drops or resets
- ✅ All effect types work reliably
- ✅ Combined effects work correctly

## Future Work

### Per-Effect Gain Settings (Optional)

While the device only supports global gain via Report 0x43, per-effect gains could be implemented in software:

1. **Add sysfs attributes:**
   - `/sys/.../periodic_gain` (0-100%)
   - `/sys/.../spring_gain` (0-100%)
   - `/sys/.../damper_gain` (0-100%)

2. **Apply in effect upload/play:**
   - Multiply effect parameters by per-effect gain
   - Before sending to device
   - Similar to how Windows driver likely works

3. **Benefits:**
   - Fine-tune different effect types
   - Match Windows driver behavior exactly
   - User preference customization

**Note:** This is not critical for basic functionality and can be added later if needed.

## Files Modified

- `src/tmt500rs/hid-tmt500rs-usb.c`:
  - Added `device_gain` and `game_gain` fields to struct
  - Implemented proper `t500rs_set_gain()` with Report 0x43
  - Removed software gain application from `t500rs_play_effect()`
  - Initialize gain values to 100% in `t500rs_wheel_init()`

## References

- USB captures in `captures/device_settings_*.pcapng`
- T500RS Implementation Plan: `cline_docs_archive/t500rs_implementation_plan.md`
- Force Feedback API: Linux input subsystem documentation

