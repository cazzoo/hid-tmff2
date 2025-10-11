# T500RS Force Feedback Protocol - Complete Analysis

## Overview

This document combines findings from:
1. TShark analysis of USB captures
2. Manual analysis from Ghidra decompilation
3. Driver implementation review

## Report Format Summary

All FFB commands are sent as USB interrupt transfers to endpoint 0x01.

### Report 0x01 - Effect Upload

**Format:**
```
01 00 XX 40 YY YY 00 ff ff 0e 00 1c 00 00 00
```

**Fields:**
- Byte 0: 0x01 (report ID)
- Byte 1: 0x00 (effect slot)
- Byte 2: Effect type
  - 0x00 = Constant force
  - 0x20 = Square wave
  - 0x21 = Triangle wave
  - 0x22 = Sine wave
  - 0x23 = Sawtooth up
  - 0x24 = Sawtooth down / Ramp
  - 0x40 = Spring
  - 0x41 = Damper/Friction/Inertia
- Byte 3: 0x40 (constant)
- Bytes 4-5: Duration (little-endian, milliseconds)
- Bytes 6-14: Additional parameters

**Examples from manual analysis:**

Constant force:
```
01 00 00 40 69 23 00 ff ff 0e 00 1c 00 00 00
```

Ramp:
```
01 00 24 40 69 23 00 ff ff 0e 00 1c 00 00 00
```

Spring:
```
01 00 40 40 17 25 00 ff ff 0e 00 1c 00 00 00
```

Sine:
```
01 00 22 40 17 25 00 ff ff 0e 00 1c 00 00 00
```

### Report 0x02 - Envelope Parameters

**Format:**
```
02 1c 00 XX XX YY ZZ ZZ WW
```

**Fields:**
- Byte 0: 0x02 (report ID)
- Byte 1: 0x1c (constant)
- Byte 2: 0x00 (usually)
- Bytes 3-4: Attack length (little-endian, milliseconds)
- Byte 5: Attack level (0x00-0x7f)
- Bytes 6-7: Fade length (little-endian, milliseconds)
- Byte 8: Fade level (0x00-0x7f)

**Examples from manual analysis:**

Attack length variations:
```
02 1c 00 2e 00 02 45 0a 00  # Attack length = 0x002e (46ms)
02 1c 00 c9 06 02 45 0a 00  # Attack length = 0x06c9 (1737ms)
```

Attack level variations:
```
02 1c 00 c9 06 00 45 0a 00  # Attack level = 0x00 (0%)
02 1c 00 c9 06 59 45 0a 00  # Attack level = 0x59 (70%)
```

Fade length variations:
```
02 1c 00 00 00 0b 00 00 0b  # Fade length = 0x0000 (0ms)
02 1c 00 00 00 0b e7 09 00  # Fade length = 0x09e7 (2535ms)
```

Fade level variations:
```
02 1c 00 00 00 0b e7 09 02  # Fade level = 0x02 (2%)
02 1c 00 00 00 0b 00 0b 7f  # Fade level = 0x7f (100%)
```

**CRITICAL FINDING:** Report 0x02 is also used for continuous force updates during playback!

From TShark analysis:
```
02 1c 00 XX YY 5e 00 00 21  # Continuous update format
02 1c 00 XX YY 3f 00 00 21  # Continuous update format (different direction?)
```

Where:
- Bytes 3-4: Force magnitude (0x0000-0x05dc, 0-1500 decimal)
- Byte 5: Direction flag (0x5e or 0x3f)
- Byte 8: Constant 0x21

### Report 0x03 - Force Level (DEPRECATED?)

**Format:**
```
03 0e 00 XX
```

**Fields:**
- Byte 0: 0x03 (report ID)
- Byte 1: 0x0e (constant)
- Byte 2: 0x00
- Byte 3: Force level (signed byte, -128 to 127)

**Examples from manual analysis:**

```
03 0e 00 01  # Level = 1 (left, small)
03 0e 00 29  # Level = 41 (left, bigger)
03 0e 00 cc  # Level = -52 (right, bigger)
03 0e 00 ff  # Level = -1 (middle/neutral)
```

**IMPORTANT:** Report 0x03 was NOT found in TShark analysis of Windows captures!
This suggests it may not be the correct way to update force continuously.

### Report 0x04 - Periodic Effect Parameters

**Format:**
```
04 0e 00 XX YY ZZ WW VV
```

**Fields:**
- Byte 0: 0x04 (report ID)
- Byte 1: 0x0e (constant)
- Byte 2: 0x00
- Byte 3: Magnitude (0x00-0x7f)
- Byte 4: Offset (signed, -128 to 127)
- Byte 5: Phase (0x00-0xff)
- Bytes 6-7: Period (little-endian, milliseconds)

**Examples from manual analysis:**

Magnitude variations:
```
04 0e 00 00 00 00 e8 03  # Magnitude = 0 (0%)
04 0e 00 09 00 00 e8 03  # Magnitude = 9 (7%)
04 0e 00 7f 00 00 e8 03  # Magnitude = 127 (100%)
```

Offset variations:
```
04 0e 00 00 00 00 85 03  # Offset = 0 (center)
04 0e 00 00 00 34 85 03  # Offset = 52 (right)
04 0e 00 00 00 ff 85 03  # Offset = -1 (left)
```

Period variations:
```
04 0e 00 00 00 ff 00 00  # Period = 0x0000 (0ms)
04 0e 00 00 00 ff c2 01  # Period = 0x01c2 (450ms)
04 0e 00 00 00 ff d0 07  # Period = 0x07d0 (2000ms)
```

**From TShark analysis:**
```
04 0e 00 00 00 00 5d 0c  # Initial/reset
04 0e 00 01 ff 00 5d 0c  # Parameter change
04 0e 00 22 22 00 5d 0c  # Maximum value
```

### Report 0x05 - Condition Effect Parameters

**Format:**
```
05 0e 00 XX YY ZZ WW VV UU TT SS
```

**Fields:**
- Byte 0: 0x05 (report ID)
- Byte 1: 0x0e or 0x1c (axis selector?)
- Byte 2: 0x00
- Byte 3: Right coefficient (0x00-0xff)
- Byte 4: Left coefficient (0x00-0xff)
- Byte 5: Right saturation (0x00-0xff)
- Byte 6: Left saturation (0x00-0xff)
- Byte 7: Deadband (0x00-0xff)
- Byte 8: Center (signed, -128 to 127)
- Bytes 9-10: Additional parameters

**Examples from manual analysis:**

Spring:
```
05 0e 00 64 64 00 00 00 00 54 54  # Report 0x05 (0x0e)
05 1c 00 00 00 00 00 00 00 46 54  # Report 0x05 (0x1c)
```

Friction:
```
05 0e 00 64 64 00 00 00 00 64 64  # Report 0x05 (0x0e)
05 1c 00 00 00 00 00 00 00 64 64  # Report 0x05 (0x1c)
```

Damper:
```
05 0e 00 64 64 00 00 00 00 64 64  # Report 0x05 (0x0e)
05 1c 00 00 00 00 00 00 00 64 64  # Report 0x05 (0x1c)
```

Coefficient variations (friction):
```
05 0e 00 00 01 00 00 00 00 64 64  # Right coef = 1
05 0e 00 21 21 00 00 00 00 64 64  # Both coef = 33
05 0e 00 ff 00 00 00 00 00 64 64  # Left coef = 255
05 0e 00 a8 a8 00 00 00 00 64 64  # Both coef = 168
```

### Report 0x41 - Effect Control

**Format:**
```
41 00 XX 01
```

**Fields:**
- Byte 0: 0x41 (report ID)
- Byte 1: 0x00 (effect slot - always 0 in captures)
- Byte 2: Command
  - 0x41 = START effect
  - 0x00 = STOP effect
- Byte 3: 0x01 (constant)

**Examples:**

Start effect:
```
41 00 41 01
```

Stop effect:
```
41 00 00 01
```

**Verified:** This matches both TShark analysis and manual analysis perfectly.

## Key Differences: Driver vs. Captures

### 1. Report 0x02 Usage ⚠️

**Driver (current):**
- Sends once during upload with all zeros
- Format: `02 1c 00 00 00 00 00 00 00`

**Captures (Windows):**
- Sends continuously during playback
- Format: `02 1c 00 XX YY 5e 00 00 21` or `02 1c 00 XX YY 3f 00 00 21`
- Bytes 3-4 contain force magnitude
- Byte 5 is 0x5e or 0x3f (direction?)
- Byte 8 is 0x21 (constant)

**Recommendation:** Update Report 0x02 to match Windows format and send continuously.

### 2. Report 0x03 Not Found ❌

**Driver (current):**
- Uses Report 0x03 to set force level
- Format: `03 0e 00 XX`

**Captures (Windows):**
- Report 0x03 NOT found in TShark analysis
- Manual analysis shows it, but may be from different test

**Recommendation:** Remove Report 0x03 usage, use Report 0x02 instead.

### 3. Report 0x04 Mostly Correct ✅

**Driver (current):**
- Format matches captures
- Used for periodic effects

**Captures (Windows):**
- Same format observed
- Values match expected ranges

**Recommendation:** Keep current implementation.

### 4. Report 0x05 Needs Two Transfers ⚠️

**Driver (current):**
- Sends one Report 0x05

**Manual analysis:**
- Shows TWO Report 0x05 transfers
- One with 0x0e, one with 0x1c in byte 1

**Recommendation:** Send both 0x05 reports for condition effects.

### 5. Report 0x41 Perfect ✅

**Driver (current):**
- `41 [id] 41 01` for START
- `41 [id] 00 01` for STOP

**Captures (Windows):**
- Exact same format

**Recommendation:** No changes needed.

## Implementation Recommendations

### Priority 1: Fix Report 0x02

```c
// Current (wrong):
buf[0] = 0x02;
buf[1] = 0x1c;
buf[2] = 0x00;
buf[3] = 0x00;
buf[4] = 0x00;
buf[5] = 0x00;
buf[6] = 0x00;
buf[7] = 0x00;
buf[8] = 0x00;

// Recommended (for continuous updates):
buf[0] = 0x02;
buf[1] = 0x1c;
buf[2] = 0x00;
buf[3] = magnitude & 0xff;        // Low byte
buf[4] = (magnitude >> 8) & 0xff; // High byte
buf[5] = (force >= 0) ? 0x5e : 0x3f; // Direction flag
buf[6] = 0x00;
buf[7] = 0x00;
buf[8] = 0x21;                    // Constant
```

### Priority 2: Remove Report 0x03

Replace all Report 0x03 usage with Report 0x02 continuous updates.

### Priority 3: Add Second Report 0x05

For condition effects (spring, damper, friction, inertia):
```c
// Send first report (0x0e)
buf[0] = 0x05;
buf[1] = 0x0e;
// ... fill parameters

// Send second report (0x1c)
buf[0] = 0x05;
buf[1] = 0x1c;
// ... fill parameters
```

### Priority 4: Implement Continuous Updates

For constant force effects, send Report 0x02 continuously (e.g., every 10-20ms) to maintain force level, not just once during upload.

## Testing Plan

1. Update Report 0x02 format
2. Test constant force in both directions
3. Verify magnitude scaling (0-1500 range)
4. Test direction flag (0x5e vs 0x3f)
5. Remove Report 0x03 and verify no regression
6. Add second Report 0x05 for condition effects
7. Implement continuous update loop
8. Test all effect types

## Summary

The manual analysis from Ghidra decompilation provides valuable details about:
- Exact byte values for each parameter
- Envelope parameter encoding
- Condition effect dual-report structure

Combined with TShark analysis, we now have a complete picture of the T500RS FFB protocol.

**Critical findings:**
1. Report 0x02 is for continuous force updates (not just envelope)
2. Report 0x03 may not be used by Windows driver
3. Report 0x05 needs two transfers (0x0e and 0x1c)
4. Direction encoding in byte 5 of Report 0x02 (0x5e/0x3f)

---

**Analysis Date:** 2025-01-06
**Sources:** TShark captures + Manual Ghidra analysis
**Status:** Ready for implementation

