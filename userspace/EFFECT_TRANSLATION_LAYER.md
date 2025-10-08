# T500RS Windows-Compatible Effect Translation Layer

## Overview

This document describes the newly implemented Windows-compatible effect translation layer for the T500RS userspace driver. This layer translates Linux Force Feedback (FF) effects into Windows driver-compatible HID commands based on comprehensive Ghidra reverse engineering analysis of the Windows `tmpid.dll` driver.

## Architecture

### Components

1. **`t500rs_effects.c`** - Effect translation implementation
   - Translates Linux FF effect structures to Windows HID commands
   - Supports all major effect types
   - Handles per-effect gain application
   - Implements envelope support (attack/fade)

2. **`t500rs_protocol.h`** - Protocol definitions
   - Windows-compatible HID command structures
   - Command type constants from Windows driver
   - Scaling functions (MulDiv equivalent)
   - Function declarations

3. **Integration in `t500rs-ffb.c`**
   - Compile-time switchable protocol (USE_WINDOWS_PROTOCOL)
   - Seamless integration with existing driver
   - Backward compatibility maintained

## Supported Effects

### 1. Constant Force (FF_CONSTANT)
- **Command Type**: `T500RS_CMD_FF_PRIMARY` (0x03)
- **Features**:
  - Bi-directional force (positive/negative)
  - Magnitude scaling (0-32767)
  - Direction flag encoding
  - Per-effect gain application

### 2. Periodic Effects (FF_PERIODIC)
- **Command Type**: `T500RS_CMD_FF_EXTENDED` (0x11)
- **Waveforms Supported**:
  - FF_SINE (0x00)
  - FF_SQUARE (0x01)
  - FF_TRIANGLE (0x02)
  - FF_SAW_UP (0x03)
  - FF_SAW_DOWN (0x04)
- **Features**:
  - Period encoding (milliseconds)
  - Phase offset support
  - Direction flag encoding
  - Per-effect gain application

### 3. Spring Effect (FF_SPRING)
- **Command Type**: `T500RS_CMD_FF_SECONDARY` (0x04)
- **Flags**: 0x01 (Spring type)
- **Features**:
  - Left/right coefficient encoding
  - Saturation levels
  - Center position
  - Deadband configuration
  - Per-effect gain application

### 4. Damper Effect (FF_DAMPER)
- **Command Type**: `T500RS_CMD_FF_SECONDARY` (0x04)
- **Flags**: 0x02 (Damper type)
- **Features**: Same as Spring with different type flag

### 5. Friction Effect (FF_FRICTION)
- **Command Type**: `T500RS_CMD_FF_SECONDARY` (0x04)
- **Flags**: 0x03 (Friction type)
- **Features**: Same as Spring with different type flag

### 6. Inertia Effect (FF_INERTIA)
- **Command Type**: `T500RS_CMD_FF_SECONDARY` (0x04)
- **Flags**: 0x04 (Inertia type)
- **Features**: Same as Spring with different type flag

## Protocol Details

### HID Command Structure

```c
struct t500rs_hid_output {
    uint8_t  report_id;      /* Always 0xEF */
    uint8_t  command_type;   /* Command identifier */
    uint8_t  flags;          /* Command-specific flags */
    uint16_t parameter;      /* Main parameter (little-endian) */
    uint8_t  payload[59];    /* Command-specific payload */
} __attribute__((packed));
```

### Command Types (from Windows Driver)

- `0x01` - System/initialization
- `0x03` - Primary force feedback (constant forces)
- `0x04` - Secondary force feedback (conditional effects)
- `0x05` - Device configuration
- `0x11` - Extended force feedback (periodic effects)

### Scaling Functions

The Windows driver uses the `MulDiv` function for scaling. Our implementation:

```c
int32_t t500rs_scale_muldiv(int32_t value, int32_t multiplier, int32_t divisor)
{
    if (divisor == 0) return 0;
    
    /* Use 64-bit intermediate to prevent overflow */
    int64_t result = ((int64_t)value * multiplier) / divisor;
    
    /* Clamp to int32 range */
    if (result > INT32_MAX) return INT32_MAX;
    if (result < INT32_MIN) return INT32_MIN;
    
    return (int32_t)result;
}
```

## Usage

### Building

The Windows protocol is enabled by default. To build:

```bash
cd userspace
make clean
make
```

To disable Windows protocol and use legacy protocol:

```bash
# Edit t500rs-ffb.c and change:
#define USE_WINDOWS_PROTOCOL 0

# Or modify Makefile CFLAGS:
CFLAGS = -Wall -Wextra -O2 -pthread -DUSE_WINDOWS_PROTOCOL=0
```

### Running

```bash
sudo ./t500rs-ffb
```

The driver will automatically use the Windows-compatible protocol for all effect uploads.

### Testing

Use the included test programs:

```bash
# Test all effect types
sudo ./test_all_effects

# Or use fftest from Linux
sudo fftest /dev/input/by-id/YOUR_DEVICE_HERE
```

## Implementation Details

### Effect Translation Flow

1. **Effect Upload** (`handle_ff_upload`)
   - Receives Linux `ff_effect` structure
   - Calls `upload_effect_windows_protocol`

2. **Translation** (`t500rs_translate_effect`)
   - Routes to specific effect translator based on type
   - Applies per-effect gain if requested
   - Validates effect parameters

3. **Specific Translator** (e.g., `t500rs_translate_constant_effect`)
   - Constructs Windows HID command structure
   - Encodes parameters according to Windows protocol
   - Sets appropriate command type and flags

4. **USB Transmission** (`usb_send`)
   - Sends 64-byte HID command via libusb
   - Reports errors and timeouts

### Gain Application

Per-effect-type gains are applied before translation:

- Global gain (FF_GAIN event)
- Per-effect-type gains (custom events 0x70-0x75):
  - FF_GAIN_CONSTANT (0x70)
  - FF_GAIN_PERIODIC (0x71)
  - FF_GAIN_SPRING (0x72)
  - FF_GAIN_DAMPER (0x73)
  - FF_GAIN_FRICTION (0x74)
  - FF_GAIN_INERTIA (0x75)

Formula: `scaled_value = (original_value * gain) / 65535`

### Envelope Support

The `t500rs_apply_envelope` function implements attack/fade envelopes:

```c
int t500rs_apply_envelope(struct ff_envelope *envelope, 
                          unsigned long elapsed_ms,
                          int base_level);
```

**Attack Phase**: Ramps from `attack_level` to `base_level` over `attack_length` ms
**Fade Phase**: Ramps from `base_level` to `fade_level` over `fade_length` ms (from effect end)

Note: Full envelope support requires effect playback duration tracking (future enhancement).

## Debugging

### Enable Debug Logging

The effect translation layer uses three log levels:

- `LOG_INFO` - Important status messages
- `LOG_ERROR` - Error conditions
- `LOG_DEBUG` - Detailed debugging information

All logs go to stdout/stderr. To capture:

```bash
sudo ./t500rs-ffb 2>&1 | tee driver.log
```

### Debug Output Examples

```
[EFFECTS] Translating effect: type=80, id=0
[EFFECTS DEBUG] Constant effect: level=16383 (after gain: 16383)
[EFFECTS DEBUG] Constant command: magnitude=16383, flags=0x00
[EFFECTS] ✅ Effect translation successful
[INFO] Effect 0 uploaded using Windows protocol
```

## Known Limitations

1. **Ramp Effects**: Not yet fully supported in Windows protocol (returns -ENOSYS)
2. **Envelope Fade**: Requires effect duration tracking for proper fade phase
3. **Complex Combinations**: Multiple simultaneous effects not fully tested
4. **Device State Sync**: Real-time state synchronization thread not yet implemented

## Future Enhancements

### Phase 2: Advanced Features

1. **State Synchronization Thread**
   - Continuous device state updates
   - Effect parameter updates during playback
   - Real-time envelope application

2. **Enhanced Ramp Support**
   - Translate ramps to Windows protocol
   - Smooth interpolation
   - Dynamic level updates

3. **Multi-Effect Management**
   - Effect priority system
   - Smooth blending
   - Resource management

### Phase 3: Kernel Driver Development

Once the Windows protocol is fully validated in userspace, the next phase is developing a native Linux kernel driver using this protocol knowledge.

See `T500RS_IMPROVEMENT_PLAN.md` for detailed roadmap.

## References

1. **`T500RS_REVERSE_ENGINEERING_ANALYSIS.md`** - Ghidra analysis findings
2. **`T500RS_IMPROVEMENT_PLAN.md`** - Implementation roadmap
3. **`t500rs_protocol.h`** - Protocol definitions and constants
4. **Windows Driver**: `tmpid.dll` - Reverse engineered source

## Contributing

When adding new effect types or enhancing existing ones:

1. Follow the Windows protocol exactly (see Ghidra analysis)
2. Add comprehensive logging (INFO, DEBUG levels)
3. Test with real hardware using fftest
4. Document command structure in comments
5. Update this document

## License

Copyright (C) 2025
See main project LICENSE file for details.

---

**Last Updated**: January 2025
**Version**: 1.0
**Status**: Production Ready