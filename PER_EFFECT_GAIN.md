# T500RS Per-Effect Gain Implementation

## Overview

This document describes the implementation of per-effect gain multipliers for the T500RS driver, providing fine-grained control over different force feedback effect types.

## Motivation

### Problem Statement

After implementing dynamic gain adjustment (Report 0x43), the driver had proper global gain control but lacked the ability to fine-tune individual effect types. Users reported:

1. **Inconsistent Effect Strength**
   - Some effect types (damper, spring) felt too strong
   - Other effects (periodic) felt too weak
   - No way to balance different effect types

2. **Windows Driver Parity**
   - Windows driver has separate sliders for:
     - Global Force
     - Periodic Force
     - Spring Force
     - Damper Force
     - Autocenter Strength
   - Linux driver only had global gain

3. **User Preference**
   - Different games and users prefer different effect balances
   - Racing sims may want strong damper, weak spring
   - Arcade games may want opposite balance

## USB Protocol Analysis

### Key Finding

Analysis of Windows driver USB captures revealed:
- **Only global gain uses Report 0x43** (sent to device hardware)
- **Per-effect gains are NOT sent via USB**
- **Per-effect gains are applied in software** by the Windows driver

This means:
- The T500RS hardware only supports a single global gain value
- Per-effect gains must be implemented in the driver software
- Gains are applied by scaling effect parameters before sending to device

## Implementation

### Data Structure

Added per-effect gain fields to `struct t500rs_device_entry`:

```c
/* Per-effect gain multipliers (0-100 range, where 100 = 100%) */
/* These are applied in software when uploading/playing effects */
u8 constant_gain;  /* Constant force effects gain */
u8 periodic_gain;  /* Periodic effects (sine, square, triangle) gain */
u8 spring_gain;    /* Spring effects gain */
u8 damper_gain;    /* Damper effects gain */
u8 friction_gain;  /* Friction effects gain */
u8 inertia_gain;   /* Inertia effects gain */
```

**Range**: 0-100 (where 100 = 100%, 50 = 50%, etc.)
**Default**: All initialized to 100 (full strength)

### Sysfs Attributes

Created device-specific sysfs attributes for user control:

```bash
/sys/bus/hid/devices/0003:044F:B65E.XXXX/constant_gain      # 0-100
/sys/bus/hid/devices/0003:044F:B65E.XXXX/periodic_gain      # 0-100
/sys/bus/hid/devices/0003:044F:B65E.XXXX/t500rs_spring_gain # 0-100
/sys/bus/hid/devices/0003:044F:B65E.XXXX/t500rs_damper_gain # 0-100
```

**Note**: Spring and damper use `t500rs_` prefix to avoid conflicts with existing base driver `spring_level` and `damper_level` attributes.

### Gain Application Helper

Created inline helper function for applying gains:

```c
static inline int apply_effect_gain(int value, u8 effect_gain)
{
    /* effect_gain is 0-100, value is effect-specific range */
    /* Return value scaled by effect_gain percentage */
    return (value * effect_gain) / 100;
}
```

### Effect Upload Functions

#### Periodic Effects

Updated `t500rs_upload_periodic()`:

```c
int magnitude = effect->u.periodic.magnitude;

/* Apply per-effect gain (periodic_gain is 0-100) */
magnitude = apply_effect_gain(magnitude, t500rs->periodic_gain);

/* Scale to device range (0-127) */
mag = (magnitude * 127) / 32767;
```

#### Condition Effects (Spring, Damper, Friction, Inertia)

Updated `t500rs_upload_condition()`:

```c
/* Select appropriate gain based on effect type */
switch (effect->type) {
case FF_SPRING:
    effect_gain = t500rs->spring_gain;
    break;
case FF_DAMPER:
    effect_gain = t500rs->damper_gain;
    break;
case FF_FRICTION:
    effect_gain = t500rs->friction_gain;
    break;
case FF_INERTIA:
    effect_gain = t500rs->inertia_gain;
    break;
}

/* Get effect parameters */
right_strength = effect->u.condition[0].right_saturation;
left_strength = effect->u.condition[0].left_saturation;

/* Apply per-effect gain */
right_strength = apply_effect_gain(right_strength, effect_gain);
left_strength = apply_effect_gain(left_strength, effect_gain);

/* Scale to device range (0-127) */
right_strength = (right_strength * 127) / 65535;
left_strength = (left_strength * 127) / 65535;
```

**Key Improvement**: Now uses actual effect parameters (`right_saturation`, `left_saturation`) instead of hardcoded values (50).

## Gain Calculation Flow

### Complete Gain Chain

The final force strength is calculated through multiple gain stages:

```
Final Force = Effect Parameters × Per-Effect Gain × Device Gain × Game Gain
```

#### Stage 1: Effect Parameters
- Set by the game/application
- Examples:
  - Constant force: `level` (-32767 to 32767)
  - Periodic: `magnitude` (0 to 32767)
  - Condition: `right_saturation`, `left_saturation` (0 to 65535)

#### Stage 2: Per-Effect Gain (Software)
- Applied in driver during effect upload
- Range: 0-100 (percentage)
- User-configurable via sysfs
- Different for each effect type

#### Stage 3: Device Gain (Hardware)
- Sent to device via Report 0x43
- Range: 0-127 (where 127 = 100%)
- Set via sysfs (future implementation)
- Affects ALL effects globally

#### Stage 4: Game Gain (Dynamic)
- Set by game through FFB API
- Range: 0-65535 (where 65535 = 100%)
- Changes during gameplay
- Multiplied with device gain before sending Report 0x43

### Example Calculation

**Scenario**: Damper effect with 80% per-effect gain

```
Game sets damper effect:
  right_saturation = 32768 (50% of max 65535)

Driver applies per-effect gain:
  right_strength = 32768 × 80 / 100 = 26214

Driver scales to device range:
  right_strength = 26214 × 127 / 65535 = 50

Device applies global gain (from Report 0x43):
  If device_gain = 100% and game_gain = 100%:
    Combined gain byte = 127 (0x7F)
  If device_gain = 100% and game_gain = 50%:
    Combined gain byte = 63 (0x3F)

Final force = 50 × (combined_gain / 127)
```

## Usage

### Reading Current Gains

```bash
# Show all per-effect gains
cat /sys/bus/hid/devices/0003:044F:B65E.*/constant_gain
cat /sys/bus/hid/devices/0003:044F:B65E.*/periodic_gain
cat /sys/bus/hid/devices/0003:044F:B65E.*/t500rs_spring_gain
cat /sys/bus/hid/devices/0003:044F:B65E.*/t500rs_damper_gain
```

### Setting Gains

```bash
# Reduce damper effects to 70%
echo 70 > /sys/bus/hid/devices/0003:044F:B65E.*/t500rs_damper_gain

# Boost periodic effects to 100% (default)
echo 100 > /sys/bus/hid/devices/0003:044F:B65E.*/periodic_gain

# Reduce spring effects to 50%
echo 50 > /sys/bus/hid/devices/0003:044F:B65E.*/t500rs_spring_gain

# Disable constant force effects
echo 0 > /sys/bus/hid/devices/0003:044F:B65E.*/constant_gain
```

### Validation

```bash
# Try to set invalid value (> 100)
echo 150 > /sys/bus/hid/devices/0003:044F:B65E.*/periodic_gain
# Returns error: "periodic_gain must be 0-100, got 150"

# Try to set negative value
echo -10 > /sys/bus/hid/devices/0003:044F:B65E.*/damper_gain
# kstrtouint will fail and return error
```

## Benefits

### 1. Fine-Grained Control
- Adjust each effect type independently
- Balance effects to personal preference
- Compensate for game-specific quirks

### 2. Problem Solving
- **Too strong damper?** Reduce `t500rs_damper_gain`
- **Weak curb effects?** Increase `periodic_gain`
- **Overpowering spring?** Reduce `t500rs_spring_gain`

### 3. Windows Parity
- Matches Windows driver functionality
- Same level of control as Windows users
- Easier migration from Windows

### 4. Game Compatibility
- Some games set effect parameters too high
- Per-effect gains allow compensation
- No need to modify game settings

## Testing

### Test Procedure

1. **Build and install:**
   ```bash
   make clean && make
   sudo make install
   sudo ./reload_modules.sh
   ```

2. **Verify sysfs attributes exist:**
   ```bash
   ls -la /sys/bus/hid/devices/0003:044F:B65E.*/constant_gain
   ls -la /sys/bus/hid/devices/0003:044F:B65E.*/periodic_gain
   ls -la /sys/bus/hid/devices/0003:044F:B65E.*/t500rs_spring_gain
   ls -la /sys/bus/hid/devices/0003:044F:B65E.*/t500rs_damper_gain
   ```

3. **Test gain adjustment:**
   ```bash
   # Reduce damper to 50%
   echo 50 > /sys/bus/hid/devices/0003:044F:B65E.*/t500rs_damper_gain
   
   # Start game and test damper effects
   # Should feel 50% weaker than before
   ```

4. **Test in-game:**
   - Drive over curbs (periodic effects)
   - Test wheel resistance (damper effects)
   - Test centering spring (spring effects)
   - Verify gains are applied correctly

### Expected Results

- ✅ Sysfs attributes created successfully
- ✅ Gains can be read and written
- ✅ Invalid values rejected (< 0 or > 100)
- ✅ Effect strength changes proportionally
- ✅ Different effect types can be balanced independently

## Future Enhancements

### 1. Persistent Settings
- Save gain settings to config file
- Restore on driver load
- Per-game profiles

### 2. GUI Configuration Tool
- Graphical sliders for each gain
- Real-time testing
- Preset profiles (racing, arcade, etc.)

### 3. Autocenter Gain
- Separate gain for autocenter spring
- Independent of regular spring effects
- Matches Windows driver behavior

## Files Modified

- `src/tmt500rs/hid-tmt500rs-usb.c`:
  - Added per-effect gain fields to struct (lines 61-67)
  - Created sysfs show/store functions (lines 883-1053)
  - Registered sysfs attributes in wheel_init (lines 1375-1388)
  - Unregistered sysfs attributes in wheel_destroy (lines 1426-1429)
  - Implemented apply_effect_gain() helper (lines 335-341)
  - Updated t500rs_upload_periodic() (line 493)
  - Updated t500rs_upload_condition() (lines 403-459)
  - Initialize all gains to 100% (lines 1180-1185)

## References

- USB capture analysis: `analyze_gain_captures.sh`, `analyze_gains.sh`
- Dynamic gain implementation: `GAIN_IMPLEMENTATION.md`
- Windows driver behavior: USB captures in `captures/device_settings_*.pcapng`

