# T500RS Sysfs Settings Reference

## Overview

This document lists all available sysfs settings for the T500RS driver. These settings allow you to configure force feedback behavior, wheel parameters, and per-effect gains.

## Sysfs Location

All settings are located under:
```
/sys/bus/hid/devices/0003:044F:B65E.XXXX/
```

Where `XXXX` is the device instance number (varies each time you plug in the wheel).

**Tip**: Use wildcards to avoid typing the full path:
```bash
echo 50 > /sys/bus/hid/devices/0003:044F:B65E.*/gain
```

## Base Driver Settings

These settings are provided by the base `hid-tmff2` driver and are common across all supported wheels.

### 1. gain
**Path**: `/sys/bus/hid/devices/0003:044F:B65E.*/gain`

**Description**: Global force feedback gain (master volume for all effects)

**Range**: 0-65535
- `0` = No force feedback (0%)
- `32768` = Half strength (50%)
- `65535` = Full strength (100%)

**Default**: 40000 (~61%)

**Usage**:
```bash
# Set to 100% (maximum)
echo 65535 > /sys/bus/hid/devices/0003:044F:B65E.*/gain

# Set to 50%
echo 32768 > /sys/bus/hid/devices/0003:044F:B65E.*/gain

# Set to 25%
echo 16384 > /sys/bus/hid/devices/0003:044F:B65E.*/gain

# Read current value
cat /sys/bus/hid/devices/0003:044F:B65E.*/gain
```

**Implementation**: Sends Report 0x43 to device with combined device_gain × game_gain

---

### 2. range
**Path**: `/sys/bus/hid/devices/0003:044F:B65E.*/range`

**Description**: Wheel rotation range (steering angle)

**Range**: 270-1080 degrees
- `270` = Minimum rotation (Formula 1 style)
- `540` = Half rotation (rally style)
- `900` = Default rotation (road car style)
- `1080` = Maximum rotation (truck/bus style)

**Default**: 900 degrees

**Usage**:
```bash
# Set to 900 degrees (default)
echo 900 > /sys/bus/hid/devices/0003:044F:B65E.*/range

# Set to 540 degrees (rally)
echo 540 > /sys/bus/hid/devices/0003:044F:B65E.*/range

# Set to 270 degrees (F1)
echo 270 > /sys/bus/hid/devices/0003:044F:B65E.*/range

# Read current value
cat /sys/bus/hid/devices/0003:044F:B65E.*/range
```

**Implementation**: Sends Report 0x42 0x05 (preliminary - may need further investigation)

**Note**: Current implementation is preliminary. The actual range setting protocol needs more investigation.

---

### 3. spring_level
**Path**: `/sys/bus/hid/devices/0003:044F:B65E.*/spring_level`

**Description**: Spring effect strength multiplier (base driver level)

**Range**: 0-100 (percentage)

**Default**: 30

**Usage**:
```bash
# Set to 50%
echo 50 > /sys/bus/hid/devices/0003:044F:B65E.*/spring_level

# Read current value
cat /sys/bus/hid/devices/0003:044F:B65E.*/spring_level
```

**Note**: This is a base driver setting. For T500RS-specific spring gain, use `t500rs_spring_gain` instead.

---

### 4. damper_level
**Path**: `/sys/bus/hid/devices/0003:044F:B65E.*/damper_level`

**Description**: Damper effect strength multiplier (base driver level)

**Range**: 0-100 (percentage)

**Default**: 30

**Usage**:
```bash
# Set to 50%
echo 50 > /sys/bus/hid/devices/0003:044F:B65E.*/damper_level

# Read current value
cat /sys/bus/hid/devices/0003:044F:B65E.*/damper_level
```

**Note**: This is a base driver setting. For T500RS-specific damper gain, use `t500rs_damper_gain` instead.

---

### 5. friction_level
**Path**: `/sys/bus/hid/devices/0003:044F:B65E.*/friction_level`

**Description**: Friction effect strength multiplier

**Range**: 0-100 (percentage)

**Default**: 30

**Usage**:
```bash
# Set to 50%
echo 50 > /sys/bus/hid/devices/0003:044F:B65E.*/friction_level

# Read current value
cat /sys/bus/hid/devices/0003:044F:B65E.*/friction_level
```

---

### 6. alternate_modes
**Path**: `/sys/bus/hid/devices/0003:044F:B65E.*/alternate_modes`

**Description**: Alternate mode setting (e.g., F1 mode on some wheels)

**Range**: Wheel-specific

**Default**: 0

**Note**: T500RS does not currently support alternate modes. This attribute exists but has no effect.

---

## T500RS-Specific Settings

These settings are specific to the T500RS driver and provide fine-grained control over individual effect types.

### 7. constant_gain
**Path**: `/sys/bus/hid/devices/0003:044F:B65E.*/constant_gain`

**Description**: Per-effect gain for constant force effects

**Range**: 0-100 (percentage)
- `0` = Disable constant force effects
- `50` = Half strength
- `100` = Full strength (default)

**Default**: 100

**Usage**:
```bash
# Set to 80%
echo 80 > /sys/bus/hid/devices/0003:044F:B65E.*/constant_gain

# Disable constant force effects
echo 0 > /sys/bus/hid/devices/0003:044F:B65E.*/constant_gain

# Read current value
cat /sys/bus/hid/devices/0003:044F:B65E.*/constant_gain
```

**Effect**: Applied in software when uploading constant force effects. Multiplies the effect magnitude before sending to device.

---

### 8. periodic_gain
**Path**: `/sys/bus/hid/devices/0003:044F:B65E.*/periodic_gain`

**Description**: Per-effect gain for periodic effects (sine, square, triangle, saw waves)

**Range**: 0-100 (percentage)

**Default**: 100

**Usage**:
```bash
# Set to 80%
echo 80 > /sys/bus/hid/devices/0003:044F:B65E.*/periodic_gain

# Boost periodic effects to 100%
echo 100 > /sys/bus/hid/devices/0003:044F:B65E.*/periodic_gain

# Read current value
cat /sys/bus/hid/devices/0003:044F:B65E.*/periodic_gain
```

**Effect**: Applied to periodic effects like rumble strips, engine vibration, road texture.

---

### 9. t500rs_spring_gain
**Path**: `/sys/bus/hid/devices/0003:044F:B65E.*/t500rs_spring_gain`

**Description**: Per-effect gain for spring effects (centering, suspension)

**Range**: 0-100 (percentage)

**Default**: 100

**Usage**:
```bash
# Reduce spring effects to 70%
echo 70 > /sys/bus/hid/devices/0003:044F:B65E.*/t500rs_spring_gain

# Read current value
cat /sys/bus/hid/devices/0003:044F:B65E.*/t500rs_spring_gain
```

**Note**: Prefixed with `t500rs_` to avoid conflict with base driver's `spring_level`.

---

### 10. t500rs_damper_gain
**Path**: `/sys/bus/hid/devices/0003:044F:B65E.*/t500rs_damper_gain`

**Description**: Per-effect gain for damper effects (resistance, weight)

**Range**: 0-100 (percentage)

**Default**: 100

**Usage**:
```bash
# Reduce damper effects to 70%
echo 70 > /sys/bus/hid/devices/0003:044F:B65E.*/t500rs_damper_gain

# Read current value
cat /sys/bus/hid/devices/0003:044F:B65E.*/t500rs_damper_gain
```

**Note**: Prefixed with `t500rs_` to avoid conflict with base driver's `damper_level`.

---

## Non-Sysfs Settings

### Autocenter
**Control Method**: Linux Force Feedback API (not sysfs)

**Description**: Self-centering spring force

**Range**: 0-65535
- `0` = Disabled (wheel moves freely)
- `16384` = 25% strength
- `32768` = 50% strength
- `49152` = 75% strength
- `65535` = 100% strength

**Control via fftest**:
```bash
fftest /dev/input/eventX
# Use the autocenter option in the menu
```

**Control via Oversteer**:
```bash
oversteer --set-autocenter 50
```

**Implementation**: 
- Report 0x40 0x04 0x00/0x01 (disable/enable)
- Report 0x40 0x03 [percentage] (set strength)
- Report 0x42 0x05 (apply)

---

## Quick Reference Table

| Setting | Path | Range | Default | Description |
|---------|------|-------|---------|-------------|
| **gain** | `.../gain` | 0-65535 | 40000 | Global FFB gain (master volume) |
| **range** | `.../range` | 270-1080 | 900 | Wheel rotation angle (degrees) |
| **spring_level** | `.../spring_level` | 0-100 | 30 | Base spring multiplier |
| **damper_level** | `.../damper_level` | 0-100 | 30 | Base damper multiplier |
| **friction_level** | `.../friction_level` | 0-100 | 30 | Friction multiplier |
| **constant_gain** | `.../constant_gain` | 0-100 | 100 | Constant force gain |
| **periodic_gain** | `.../periodic_gain` | 0-100 | 100 | Periodic effects gain |
| **t500rs_spring_gain** | `.../t500rs_spring_gain` | 0-100 | 100 | Spring effects gain |
| **t500rs_damper_gain** | `.../t500rs_damper_gain` | 0-100 | 100 | Damper effects gain |
| **autocenter** | FFB API | 0-65535 | 0 | Self-centering spring (not sysfs) |

---

## Gain Calculation Flow

The final force strength is calculated through multiple stages:

```
Final Force = Effect Parameters × Per-Effect Gain × Global Gain × Game Gain
```

### Example: Damper Effect

1. **Game sets effect**: `right_saturation = 32768` (50% of max)
2. **Per-effect gain applied**: `32768 × 70 / 100 = 22938` (if t500rs_damper_gain = 70)
3. **Scale to device range**: `22938 × 127 / 65535 = 44`
4. **Global gain applied**: Device applies gain from Report 0x43
5. **Final force**: `44 × (global_gain / 127)`

---

## Recommended Settings

### Racing Simulation (Realistic)
```bash
echo 65535 > .../gain              # 100% global gain
echo 900 > .../range               # 900° rotation
echo 100 > .../constant_gain       # Full constant force
echo 100 > .../periodic_gain       # Full periodic effects
echo 80 > .../t500rs_spring_gain   # Slightly reduced spring
echo 70 > .../t500rs_damper_gain   # Reduced damper for smoothness
```

### Arcade Racing (Easy)
```bash
echo 32768 > .../gain              # 50% global gain
echo 540 > .../range               # 540° rotation
echo 80 > .../constant_gain        # Reduced constant force
echo 100 > .../periodic_gain       # Full periodic effects
echo 60 > .../t500rs_spring_gain   # Light spring
echo 50 > .../t500rs_damper_gain   # Light damper
```

### Rally/Drift
```bash
echo 49152 > .../gain              # 75% global gain
echo 540 > .../range               # 540° rotation
echo 100 > .../constant_gain       # Full constant force
echo 120 > .../periodic_gain       # ERROR: max is 100!
echo 100 > .../periodic_gain       # Full periodic effects
echo 50 > .../t500rs_spring_gain   # Very light spring for drift
echo 80 > .../t500rs_damper_gain   # Medium damper
```

---

## Troubleshooting

### Settings Not Taking Effect

**Check 1**: Verify attribute exists
```bash
ls -la /sys/bus/hid/devices/0003:044F:B65E.*/gain
```

**Check 2**: Check for errors
```bash
dmesg | tail -20
```

**Check 3**: Verify value was written
```bash
cat /sys/bus/hid/devices/0003:044F:B65E.*/gain
```

### Permission Denied

**Solution**: Use sudo or add user to appropriate group
```bash
sudo echo 65535 > /sys/bus/hid/devices/0003:044F:B65E.*/gain
```

### Invalid Value

**Solution**: Check range limits
```bash
# This will fail (> 100)
echo 150 > /sys/bus/hid/devices/0003:044F:B65E.*/constant_gain

# This will work
echo 100 > /sys/bus/hid/devices/0003:044F:B65E.*/constant_gain
```

---

## See Also

- **GAIN_IMPLEMENTATION.md** - Detailed gain system documentation
- **PER_EFFECT_GAIN.md** - Per-effect gain implementation details
- **AUTOCENTER_IMPLEMENTATION.md** - Autocenter control documentation
- **T500RS_TESTING_GUIDE.md** - Testing procedures

