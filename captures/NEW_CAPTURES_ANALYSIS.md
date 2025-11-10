# Analysis of New Windows Captures

## Overview

Two new captures analyzed:
1. **plug_t500_in_while_044f.pcapng** - Device initialization sequence
2. **device_update_firmware_from_044f_to_044d.pcapng** - Firmware update process

## 1. plug_t500_in_while_044f.pcapng

### Device Information
- **Vendor ID**: 0x044f (Thrustmaster)
- **Product ID**: 0xb65e (T500RS normal mode)
- **Capture Type**: Device plug-in and initialization

### USB Commands Sent (Host → Device)

```
Frame 303: 42 01 00 00 00 00 00 00
  Command: 0x42 (Initialize)
  Subtype: 0x01
  
Frame 310: 0a 04 90 03 00 00 00 00
  Command: 0x0a (HID Feature Report?)
  Bytes: 04 90 03
  
Frame 314: 0a 04 12 10 00 00 00 00
  Command: 0x0a
  Bytes: 04 12 10
  
Frame 317: 0a 04 00 06 00 00 00 00
  Command: 0x0a
  Bytes: 04 00 06
  
Frame 370: 40 11 42 7b
  Command: 0x40 (Settings/Control)
  Bytes: 11 42 7b
```

### Analysis

**Report 0x42 Variants:**
- `42 01` - Seen in initialization
- `42 04` - Seen in our driver init
- `42 05` - Seen in control panel effects

**Report 0x0a - NEW DISCOVERY!**
This is a **HID Feature Report** that we haven't implemented yet!

Looking at the bytes:
- `0a 04 90 03` - Unknown purpose
- `0a 04 12 10` - Unknown purpose  
- `0a 04 00 06` - Unknown purpose

These might be:
- Device configuration
- Force feedback engine setup
- Motor calibration
- Effect slot initialization

**Report 0x40:**
- `40 11 42 7b` - Settings command with specific parameters

### Comparison with Our Driver

**Our initialization sequence** (from hid-tmt500rs-usb.c):
```c
42 01 00 00 00 00 00 00  ✅ MATCHES Frame 303
0a 04 90 03 00 00 00 00  ✅ MATCHES Frame 310
0a 04 12 10 00 00 00 00  ✅ MATCHES Frame 314
0a 04 00 06 00 00 00 00  ✅ MATCHES Frame 317
40 11 55 d5              ❌ DIFFERENT from Frame 370 (40 11 42 7b)
42 04                    ❓ Not in this capture
40 04 00 00              ❓ Not in this capture
43 00                    ❓ Not in this capture
41 00 00 00              ❓ Not in this capture
40 08 00 00              ❓ Not in this capture
40 03 0d 00              ❓ Not in this capture
05 0e 00 00 00 00 00 00  ❓ Not in this capture
05 1c 00 00 00 00 00 00  ❓ Not in this capture
41 0f 00 01              ❓ Not in this capture
```

**Key Findings:**
1. ✅ First 4 commands match exactly!
2. ❌ Report 0x40 has different parameters (42 7b vs 55 d5)
3. ❓ Our driver sends many more initialization commands

**Hypothesis:**
The minimal initialization might only need:
- `42 01` - Initialize
- `0a 04 90 03` - Configure device
- `0a 04 12 10` - Configure device
- `0a 04 00 06` - Configure device
- `40 11 42 7b` - Set parameters

Then the rest of our init commands might be for:
- Disabling autocenter (05, 41 0f)
- Setting force levels (40, 43)
- Clearing effects (41 00)

## 2. device_update_firmware_from_044f_to_044d.pcapng

### Device Information
- **Vendor ID**: 0x044f (Thrustmaster)
- **Product IDs**: 
  - 0xb65e (T500RS normal mode)
  - 0xb65f (T500RS bootloader/DFU mode)

### Firmware Update Commands

All commands start with `0xff` (firmware/DFU command):

```
Frame 3995: ff 3d 00 00 00 00 00 00 00 00 00 00
Frame 3999: ff 3c fe a3 00 00 00 00 00 00 00 00
Frame 4003: ff 3c 02 a4 00 00 00 00 00 00 00 00
Frame 4679: ff 38 00 00 00 00 00 00 00 00 00 00
Frame 4683: ff 39 00 14 c0 14 04 00 00 00 00 00
... (hundreds more firmware data packets)
```

**Command Types:**
- `ff 3d` - Firmware command type 1
- `ff 3c` - Firmware command type 2
- `ff 38` - Firmware command type 3
- `ff 39` - Firmware data transfer (most common)

**Analysis:**
This is the firmware update protocol. The device switches to bootloader mode (0xb65f) to receive firmware updates.

**NOT RELEVANT** for force feedback implementation - this is for firmware flashing only.

## Critical Discoveries

### 1. Report 0x0a is Missing!

Windows sends **Report 0x0a** during initialization, but we don't!

This might be critical for force feedback to work. Report 0x0a appears to be a HID Feature Report for device configuration.

### 2. Report 0x40 Parameters Differ

Windows: `40 11 42 7b`
Our driver: `40 11 55 d5`

The last two bytes differ. These might be:
- Force scaling factors
- Motor parameters
- Device-specific calibration

### 3. Minimal vs Full Initialization

Windows minimal init (from plug_t500_in capture):
```
42 01
0a 04 90 03
0a 04 12 10
0a 04 00 06
40 11 42 7b
```

Our driver full init (13 commands):
```
42 01
0a 04 90 03
0a 04 12 10
0a 04 00 06
40 11 55 d5  ← Different!
42 04
40 04 00 00
43 00
41 00 00 00
40 08 00 00
40 03 0d 00
05 0e 00 00 00 00 00 00
05 1c 00 00 00 00 00 00
41 0f 00 01
```

## Recommendations

### Priority 1: Fix Report 0x40 Parameters

Change from `40 11 55 d5` to `40 11 42 7b` to match Windows exactly.

**Location**: `src/tmt500rs/hid-tmt500rs-usb.c` - wheel_init function

### Priority 2: Verify Report 0x0a Implementation

Check if our Report 0x0a commands are being sent correctly. They appear to match Windows, so this is likely OK.

### Priority 3: Test Minimal Initialization

Try using ONLY the 5 commands Windows sends:
```c
42 01 00 00 00 00 00 00
0a 04 90 03 00 00 00 00
0a 04 12 10 00 00 00 00
0a 04 00 06 00 00 00 00
40 11 42 7b              // ← Use Windows parameters!
```

Then test if force feedback works with this minimal init.

### Priority 4: Document Report 0x0a

We need to understand what Report 0x0a does:
- Is it a HID Feature Report?
- What do the parameters mean?
- Is it required for force feedback?

## Next Steps

1. **Fix Report 0x40 parameters** - Change to match Windows
2. **Test with current driver** - See if force feedback works now
3. **If still broken, try minimal init** - Use only 5 commands
4. **Capture more Windows data** - Get a clean constant force test with upload visible

## Summary

**Key Finding**: Report 0x40 parameters differ between Windows and our driver!

**Windows**: `40 11 42 7b`
**Our driver**: `40 11 55 d5`

This could be the reason force feedback doesn't work! The device might be configured with wrong parameters.

**Confidence**: 🔥 **HIGH** 🔥 - This is a concrete difference we can fix and test!

