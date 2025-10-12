# Advanced Force Feedback Configuration

## Overview

The T500RS modular driver includes three advanced force feedback features that can be toggled at runtime without recompiling:

1. **Force Smoothing** - Exponential smoothing to prevent sudden force jumps
2. **Multi-Effect Mixing** - Advanced mixing of multiple simultaneous effects
3. **Dynamic Update Rate** - Adaptive update frequency based on force changes

These features are **enabled by default** but can cause "odd" or "unnatural" force feedback behavior for some users. This tool allows you to test with/without each feature to find your optimal settings.

## Quick Start

### 1. Start the Driver

```bash
cd ~/Documents/hid-tmff2/userspace
sudo ./t500rs-ffb-modular
```

### 2. Run the Configuration GUI

In a separate terminal:

```bash
cd ~/Documents/hid-tmff2/userspace
python3 t500rs_config_gui.py
```

The GUI will auto-detect your T500RS device and show three toggle switches.

### 3. Test Different Configurations

Try toggling each feature on/off while testing force feedback in your game or simulator:

- **Disable Force Smoothing** - For more direct, immediate force response
- **Disable Multi-Effect Mixing** - To use only the strongest effect
- **Disable Dynamic Update Rate** - For consistent 50Hz updates

## Features Explained

### Force Smoothing

**What it does:**
- Applies exponential smoothing (0.3 factor) to force changes
- Prevents sudden jumps when effects start/stop
- Formula: `new_force = old_force + (target_force - old_force) * 0.3`

**When to disable:**
- If FF feels "laggy" or "delayed"
- If you want more direct, immediate response
- If you prefer raw, unfiltered force feedback

**When to enable:**
- If FF feels too "jerky" or "sudden"
- If you want smoother transitions
- If you experience uncomfortable force jumps

### Multi-Effect Mixing

**What it does:**
- Combines multiple simultaneous effects using clamped addition
- Allows realistic overlapping of effects (e.g., road texture + spring)
- Clamps result to valid range (-32767 to +32767)

**When to disable:**
- If FF feels "confused" with multiple effects
- If you want simpler, more predictable behavior
- If only one effect should be active at a time

**When to enable:**
- If you want realistic multi-effect combinations
- If your game/sim uses multiple overlapping effects
- If you want more complex force feedback

### Dynamic Update Rate

**What it does:**
- Adjusts update frequency based on force change rate
- Fast changes: 100Hz (10ms updates)
- Slow changes: 25Hz (40ms updates)
- Optimizes CPU usage while maintaining responsiveness

**When to disable:**
- If FF feels inconsistent or "stuttery"
- If you want predictable, constant update rate
- If you prefer fixed 50Hz updates

**When to enable:**
- If you want optimized CPU usage
- If you want faster response to sudden changes
- If you want adaptive performance

## GUI Usage

### Main Window

```
┌─────────────────────────────────────────────┐
│  T500RS Advanced FF Configuration           │
│  Device: /dev/input/event5                  │
├─────────────────────────────────────────────┤
│  Force Smoothing                  [✓] Enabled│
│  Exponential smoothing to prevent jumps     │
│                                             │
│  Multi-Effect Mixing              [✓] Enabled│
│  Advanced mixing of multiple effects        │
│                                             │
│  Dynamic Update Rate              [✓] Enabled│
│  Adaptive update frequency (25-100Hz)       │
├─────────────────────────────────────────────┤
│  [Get Current Config] [Reset] [Close]       │
│  Status: Ready                              │
└─────────────────────────────────────────────┘
```

### Buttons

- **Get Current Config** - Logs current settings to driver output
- **Reset to Defaults** - Enables all features (default state)
- **Close** - Exit the GUI

### Status Bar

Shows the last action performed and current state.

## Testing Procedure

### Recommended Testing Steps

1. **Start with all features enabled** (default)
   - Test your game/sim
   - Note how FF feels

2. **Disable Force Smoothing**
   - Test again
   - Compare: More direct? More jerky?

3. **Re-enable Force Smoothing, disable Multi-Effect Mixing**
   - Test again
   - Compare: Simpler? Less realistic?

4. **Re-enable Mixing, disable Dynamic Update Rate**
   - Test again
   - Compare: More consistent? Less responsive?

5. **Try different combinations**
   - Find what feels best for you
   - Settings are saved while driver is running

### What to Test

- **Road texture effects** - Bumps, curbs, rumble strips
- **Spring/damper effects** - Steering resistance, centering
- **Collision effects** - Sudden impacts
- **Multiple simultaneous effects** - Complex scenarios

## Command Line Usage

You can also control settings via command line:

```bash
# Send events directly using evtest or similar tools
# Event codes:
#   0x80 = Toggle Force Smoothing (value: 0=off, 1=on)
#   0x81 = Toggle Multi-Effect Mixing (value: 0=off, 1=on)
#   0x82 = Toggle Dynamic Update Rate (value: 0=off, 1=on)
#   0x83 = Get Current Config (value: any)
```

## Troubleshooting

### GUI doesn't find device

```bash
# List input devices
ls -la /dev/input/event*

# Find T500RS
grep -H . /sys/class/input/event*/device/name | grep T500RS

# Specify device manually
python3 t500rs_config_gui.py /dev/input/eventX
```

### Changes don't take effect

- Make sure the driver is running
- Check driver logs for confirmation messages
- Try toggling the setting off and on again

### Driver logs show errors

- Check that you're using the modular driver (t500rs-ffb-modular)
- Ensure driver was compiled with latest changes
- Rebuild: `make -f Makefile.modular rebuild`

## Default Settings

All features are **enabled by default** for backward compatibility:

```
Force Smoothing:      ENABLED
Multi-Effect Mixing:  ENABLED
Dynamic Update Rate:  ENABLED
```

## Recommended Configurations

### "Raw" Configuration (Most Direct)
```
Force Smoothing:      DISABLED
Multi-Effect Mixing:  DISABLED
Dynamic Update Rate:  DISABLED
```
- Most direct, unfiltered force feedback
- May feel jerky or sudden
- Good for testing or preference for raw feel

### "Smooth" Configuration (Most Refined)
```
Force Smoothing:      ENABLED
Multi-Effect Mixing:  ENABLED
Dynamic Update Rate:  ENABLED
```
- Smoothest, most refined force feedback
- May feel slightly delayed
- Good for comfort and realism

### "Balanced" Configuration
```
Force Smoothing:      DISABLED
Multi-Effect Mixing:  ENABLED
Dynamic Update Rate:  ENABLED
```
- Direct response with advanced features
- Good compromise for most users

## Technical Details

### Force Smoothing Algorithm

```c
smoothed = last + (target - last) * 0.3
```

- Smoothing factor: 0.3 (30% of change per update)
- Small deltas (<100): No smoothing to avoid drift
- Update rate: Depends on dynamic rate setting

### Multi-Effect Mixing Algorithm

```c
// Clamped addition
combined = effect1 + effect2 + ... + effectN
if (combined > 32767) combined = 32767
if (combined < -32767) combined = -32767
```

### Dynamic Update Rate Thresholds

```
Force Delta > 5000:  100Hz (10ms)
Force Delta > 2000:   66Hz (15ms)
Force Delta > 500:    50Hz (20ms)
Force Delta > 100:    33Hz (30ms)
Force Delta ≤ 100:    25Hz (40ms)
```

When disabled: Fixed 50Hz (20ms)

## Feedback

If you find a configuration that works particularly well (or poorly), please share your findings! This helps improve the default settings for all users.

---

**Last Updated**: 2025-01-06
**Version**: 1.0
**Status**: Production Ready

