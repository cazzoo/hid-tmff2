# T500RS Force Feedback Capture Analysis

## Overview

Systematic analysis of FFB packets from Windows USB captures to verify and improve the userspace driver implementation.

## Captures Analyzed

- **t500rs_windows_20251002_235917.pcapng** - Main FFB capture (293 packets)
- **device_const_force_pos.pcapng** - Constant force test
- **t500rs_windows_20251003_225720.pcapng** - Additional FFB data

## Report Types Found

### Report 0x02 - Force/Envelope Updates (238 occurrences)

**Format variations:**

1. **Short format (9 bytes):**
   ```
   02 1c 00 XX YY 5e 00 00 21
   ```
   - Byte 0: 0x02 (report ID)
   - Byte 1: 0x1c (constant - likely effect slot or type)
   - Byte 2: 0x00 (usually zero)
   - Bytes 3-4: XX YY (force magnitude, little-endian)
   - Byte 5: 0x5e or 0x3f (two distinct values - possibly direction?)
   - Bytes 6-7: 0x00 0x00
   - Byte 8: 0x21 (constant)

2. **Long format (14 bytes):**
   ```
   02 1c 00 dc 05 3f cc 01 5e 00 00 00 00 21
   ```
   - Extended envelope/parameter data
   - Appears during complex effects

**Analysis:**
- Most common report type (81% of all FFB packets)
- Used for continuous force updates
- Byte 5 alternates between 0x5e and 0x3f (possibly left/right direction)
- Magnitude in bytes 3-4 ranges from 0x0000 to 0x05dc (0-1500 decimal)

### Report 0x04 - Effect Parameters (29 occurrences)

**Format:**
```
04 0e 00 XX YY 00 5d 0c
```
- Byte 0: 0x04 (report ID)
- Byte 1: 0x0e (constant - effect type?)
- Byte 2: 0x00
- Bytes 3-4: XX YY (parameters - magnitude, offset, or period)
- Byte 5: 0x00
- Bytes 6-7: 0x5d 0x0c (constant - possibly period or duration)

**Observed sequences:**
```
04 0e 00 00 00 00 5d 0c  - Initial/reset
04 0e 00 01 ff 00 5d 0c  - Parameter change
04 0e 00 01 01 00 5d 0c  - Parameter change
04 0e 00 04 05 00 5d 0c  - Parameter change
...
04 0e 00 22 22 00 5d 0c  - Maximum value seen
```

**Analysis:**
- Bytes 3-4 increment gradually (0x00 → 0x22)
- Likely setting effect magnitude or periodic parameters
- Used before or during effect playback
- May be for periodic effects (sine, triangle, etc.)

### Report 0x41 - Effect Control (13 occurrences)

**Format:**
```
41 00 XX YY
```
- Byte 0: 0x41 (report ID)
- Byte 1: 0x00 (effect slot - always 0 in this capture)
- Byte 2: 0x41 or 0x00 (command)
- Byte 3: 0x01 (parameter)

**Commands observed:**
```
41 00 41 01  - Start effect (10 occurrences)
41 00 00 01  - Stop effect (3 occurrences)
```

**Analysis:**
- Clear start/stop pattern
- Byte 2: 0x41 = START, 0x00 = STOP
- Byte 3: Always 0x01 (possibly effect enable flag)
- Effect slot (byte 1) always 0 in this capture

### Report 0x42 - Configuration (4 occurrences)

**Format:**
```
42 01 00 00 00 00 00 00 00
```
- Byte 0: 0x42 (report ID)
- Byte 1: 0x01 (constant)
- Bytes 2-8: 0x00 (all zeros)

**Analysis:**
- Same as initialization sequence
- Likely device configuration or reset
- Sent at beginning of session

### Report 0x40 - Configuration (3 occurrences)

**Format:**
```
40 03 0d 00
```
- Byte 0: 0x40 (report ID)
- Byte 1: 0x03
- Byte 2: 0x0d
- Byte 3: 0x00

**Analysis:**
- Same as initialization sequence
- Configuration command

### Report 0x0a - Configuration (3 occurrences)

**Format:**
```
0a 04 XX XX 00 00 00 00
```

**Variations:**
```
0a 04 90 03 00 00 00 00
0a 04 12 10 00 00 00 00
0a 04 00 06 00 00 00 00
```

**Analysis:**
- Same as initialization sequence
- Configuration commands

### Report 0x01 - Unknown (2 occurrences)

**Format:**
```
01 XX ...
```

**Analysis:**
- Rare in this capture
- May be effect upload command (seen in driver code)

### Report 0x43 - Unknown (1 occurrence)

**Format:**
```
43 XX ...
```

**Analysis:**
- Single occurrence
- Purpose unknown

## Comparison with Driver Implementation

### What Matches ✅

1. **Report 0x41 (Effect Control)**
   - Driver: `41 [id] 41 01` for START, `41 [id] 00 01` for STOP
   - Capture: `41 00 41 01` for START, `41 00 00 01` for STOP
   - ✅ **MATCHES** (byte 2 controls start/stop)

2. **Report 0x04 (Effect Parameters)**
   - Driver: `04 0e 00 [mag] [offset] [phase] [period_lo] [period_hi]`
   - Capture: `04 0e 00 XX YY 00 5d 0c`
   - ✅ **SIMILAR** (structure matches, values differ)

3. **Report 0x02 (Envelope)**
   - Driver: `02 1c 00 00 00 00 00 00 00`
   - Capture: `02 1c 00 XX YY 5e 00 00 21`
   - ⚠️ **PARTIAL** (driver sends zeros, capture has varying data)

### What Differs ⚠️

1. **Report 0x02 Usage**
   - Driver: Sends once during upload with all zeros
   - Capture: Sends continuously with varying magnitude
   - **Issue:** Driver may not be updating force continuously

2. **Report 0x02 Byte 5**
   - Driver: Always 0x00
   - Capture: Alternates between 0x5e and 0x3f
   - **Issue:** May be missing direction encoding

3. **Report 0x02 Byte 8**
   - Driver: 0x00
   - Capture: 0x21
   - **Issue:** Missing constant value

4. **Report 0x03 (Force Level)**
   - Driver: Uses `03 0e 00 [level]` to set force
   - Capture: No Report 0x03 found!
   - **Issue:** Driver may be using wrong report for force updates

## Key Findings

### Critical Discovery: Report 0x02 is for Force Updates

The capture shows Report 0x02 being sent **continuously** with varying magnitude, not just once during upload.

**Hypothesis:**
- Report 0x02 is the **primary force update mechanism**
- Bytes 3-4 contain the force magnitude
- Byte 5 (0x5e vs 0x3f) may encode direction
- Byte 8 (0x21) is a constant that driver is missing

### Report 0x03 Not Found

The driver uses Report 0x03 to set force level, but this report is **not present** in the Windows capture.

**Implication:**
- Report 0x03 may not be the correct way to update force
- Report 0x02 should be used instead

### Effect Control is Correct

Report 0x41 usage matches perfectly:
- `41 [id] 41 01` = START
- `41 [id] 00 01` = STOP

## Recommendations

### 1. Fix Report 0x02 Format

**Current (driver):**
```c
buf[0] = 0x02;
buf[1] = 0x1c;
buf[2] = 0x00;
buf[3] = 0x00;  // Should be magnitude low byte
buf[4] = 0x00;  // Should be magnitude high byte
buf[5] = 0x00;  // Should be 0x5e or 0x3f (direction?)
buf[6] = 0x00;
buf[7] = 0x00;
buf[8] = 0x00;  // Should be 0x21
```

**Recommended:**
```c
buf[0] = 0x02;
buf[1] = 0x1c;
buf[2] = 0x00;
buf[3] = magnitude & 0xff;        // Magnitude low byte
buf[4] = (magnitude >> 8) & 0xff; // Magnitude high byte
buf[5] = direction_flag;          // 0x5e or 0x3f
buf[6] = 0x00;
buf[7] = 0x00;
buf[8] = 0x21;                    // Constant
```

### 2. Use Report 0x02 for Continuous Updates

Instead of Report 0x03, use Report 0x02 to update force continuously during effect playback.

### 3. Investigate Byte 5 Direction Encoding

Test whether:
- 0x5e = positive force (left pull)
- 0x3f = negative force (right pull)

Or vice versa.

### 4. Add Continuous Force Updates

For constant force effects, send Report 0x02 continuously (not just once) to maintain force level.

## Next Steps

1. **Modify Report 0x02 format** to match capture
2. **Remove Report 0x03** usage (not in Windows driver)
3. **Test direction encoding** (byte 5: 0x5e vs 0x3f)
4. **Implement continuous updates** for constant force
5. **Verify periodic effects** use Report 0x04 correctly

## Testing Plan

1. Update Report 0x02 format
2. Test constant force in both directions
3. Verify force magnitude scaling
4. Test periodic effects (sine, triangle, etc.)
5. Compare behavior with Windows driver

---

**Analysis Date:** 2025-01-06
**Status:** Recommendations ready for implementation
**Priority:** HIGH - Report 0x02 format is critical for proper FFB

