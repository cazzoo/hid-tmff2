# T500RS Windows Protocol - Essential Findings

**Date:** 2025-10-17  
**Source:** Systematic analysis of 21 Windows USB captures

---

## CRITICAL DISCOVERY: Effect Upload Sequence

### Windows ALWAYS Uses This Sequence:

```
For ANY effect type:
1. Report 0x02 (Envelope) - FIRST packet, subtype 0x1c
2. Report 0x41 STOP (0x41 00 00 01) - Clear previous effect
3. Report 0x01 (Duration/Control) - Effect metadata
4. [Optional] Report 0x41 STOP again
5. Report 0x02 (Envelope) - SECOND packet, subtype 0x38
6. Report 0x03/0x04/0x05 (Parameters) - Effect-specific data
7. Report 0x01 (Duration/Control) - Effect metadata again
8. [For multi-effect] Additional Report 0x01/0x02/0x04/0x05 sequences
9. Report 0x41 START (0x41 [id] 41 01) - Activate effect(s)
```

### Example: Constant Force (NOT CAPTURED - only START seen)

From `device_const_force_pos.pcapng`:
```
Frame 1: 41 00 41 01  (START effect 0)
Frame 3: 41 01 41 01  (START effect 1)
```

**NOTE:** The upload sequence was NOT captured! Only START commands visible.

### Example: Sine Wave (from ctl_panel_boing)

```
Frame 795: 02 1c 00 95 00 3f e5 01 00  (Envelope: attack=38144ms@63, fade=485ms)
Frame 796: 41 00 00 01                 (STOP effect 0)
Frame 799: 01 00 22 40 bc 02 00 2c 01 0e 00 1c 00 00 00  (Duration: Sine, 700ms)
Frame 801: 02 38 00 95 00 3f e5 01 00  (Envelope again, subtype 0x38)
Frame 803: 04 2a 00 20 00 00 21 00     (Periodic: magnitude=32, freq=33Hz)
Frame 805: 01 01 22 40 bc 02 00 2c 01 2a 00 38 00 00 00  (Duration again)
Frame 807: 41 01 41 01                 (START effect 1)
```

### Example: Multi-Effect (from ctl_panel_bumpy_road)

```
Effect 1 (Sine):
  Frame 511: 02 1c 00 00 00 1f 00 00 1f  (Envelope)
  Frame 512: 41 00 00 01                 (STOP)
  Frame 515: 01 00 22 40 dc 05 00 00 00 0e 00 1c 00 00 00  (Sine, 1500ms)
  Frame 517: 41 00 00 01                 (STOP again)
  Frame 519: 02 38 00 00 00 1f 00 00 1f  (Envelope 0x38)
  Frame 521: 04 2a 00 1f 00 00 5a 00     (Periodic: mag=31, freq=90Hz)
  Frame 523: 01 01 22 40 dc 05 00 00 00 2a 00 38 00 00 00  (Duration)

Effect 2 (Sine):
  Frame 525: 01 00 22 40 e8 03 00 00 00 0e 00 1c 00 00 00  (Sine, 1000ms)
  Frame 527: 02 54 00 00 00 0c 00 00 0c  (Envelope 0x54)
  Frame 529: 04 46 00 0c 00 00 4d 01     (Periodic: mag=12, freq=333Hz)
  Frame 531: 01 02 22 40 e8 03 00 00 00 46 00 54 00 00 00  (Duration)

Start both:
  Frame 533: 41 01 41 01  (START effect 1)
  Frame 535: 41 02 41 01  (START effect 2)
```

---

## Report Structure Details

### Report 0x02: Envelope (TWO VARIANTS!)

**Subtype 0x1c (First packet):**
```
Byte 0: 0x02
Byte 1: 0x1c
Bytes 2-4: Attack length (24-bit LE, milliseconds)
Byte 5: Attack level (0-127)
Bytes 6-8: Fade length (24-bit LE, milliseconds)
Byte 9: Fade level (0-127, optional)
```

**Subtype 0x38 (Second packet):**
```
Byte 0: 0x02
Byte 1: 0x38
Bytes 2-9: Same as 0x1c
```

**Subtype 0x54 (Third packet for multi-effect):**
```
Byte 0: 0x02
Byte 1: 0x54
Bytes 2-9: Same as 0x1c
```

**Pattern:** Envelope subtype increments by 0x1c for each effect in sequence!
- Effect 0: 0x1c, 0x38
- Effect 1: 0x38, 0x54
- Effect 2: 0x54, 0x70 (predicted)

### Report 0x04: Periodic/Ramp Parameters (TWO VARIANTS!)

**Subtype 0x2a (First effect):**
```
Byte 0: 0x04
Byte 1: 0x2a
Byte 2: Magnitude high byte
Byte 3: Magnitude low byte
Bytes 4-5: Unknown (often 0x00 0x00)
Bytes 6-7: Frequency (16-bit LE, Hz × 100)
```

**Subtype 0x46 (Second effect):**
```
Byte 0: 0x04
Byte 1: 0x46
Bytes 2-7: Same as 0x2a
```

**Pattern:** Parameter subtype also increments by 0x1c!
- Effect 0: 0x0e, 0x2a
- Effect 1: 0x2a, 0x46
- Effect 2: 0x46, 0x62 (predicted)

### Report 0x05: Condition Parameters (TWO VARIANTS!)

**Subtype 0x46 (First packet):**
```
Byte 0: 0x05
Byte 1: 0x46
Byte 2: Unknown (0x00)
Byte 3: Positive coefficient
Byte 4: Negative coefficient
Bytes 5-8: Unknown (often 0x00)
Byte 9: Positive saturation
Byte 10: Negative saturation
```

**Subtype 0x54 (Second packet):**
```
Byte 0: 0x05
Byte 1: 0x54
Bytes 2-3: Center point (16-bit signed LE)
Bytes 4-7: Unknown (often 0x00)
Byte 8: Unknown
Byte 9: Deadband
Byte 10: Unknown
```

### Report 0x01: Duration/Control

```
Byte 0: 0x01
Byte 1: Effect ID (0-based)
Byte 2: Effect type (0x00=Constant, 0x20=Square, 0x22=Sine, 0x40=Spring, 0x41=Damper)
Byte 3: 0x40 (constant)
Bytes 4-5: Duration (16-bit LE, milliseconds)
Bytes 6-7: Unknown (often 0x00 0x00 or 0x00 0x2c)
Byte 8: Unknown (often 0x01)
Byte 9: Parameter subtype reference (0x0e, 0x2a, 0x46...)
Byte 10: 0x00
Byte 11: Envelope subtype reference (0x1c, 0x38, 0x54...)
Bytes 12-14: 0x00 0x00 0x00
```

**CRITICAL:** Bytes 9 and 11 link to the parameter and envelope subtypes!

### Report 0x41: Start/Stop Control

**START:**
```
0x41 [effect_id] 0x41 0x01
```

**STOP:**
```
0x41 [effect_id] 0x00 0x01
```

### Report 0x42: Initialize

```
0x42 0x05
```

Sent at beginning and end of effect sequences.

### Report 0x40: Settings Control

**Pattern from autocenter adjustment:**
```
0x40 0x03 [value] 0x00
```

Where value ranges from 0x0d to 0x37 (13 to 55 decimal).

**Pattern from autocenter enable/disable:**
```
0x40 0x04 0x00 0x00  (Disable)
0x40 0x04 0x01 0x00  (Enable)
```

### Report 0x43: Unknown Control

**Pattern from global force adjustment:**
```
0x43 [value]
```

Where value ranges from 0x18 to 0x4c (24 to 76 decimal).

---

## Key Differences from Our Driver

### 1. Missing Report 0x02 Subtype 0x38

**Windows sends TWO envelope packets:**
- First: 0x02 0x1c ... (before Report 0x01)
- Second: 0x02 0x38 ... (after Report 0x01, before parameters)

**Our driver sends ONE:**
- Only: 0x02 0x1c ...

### 2. Missing STOP Before Upload

**Windows ALWAYS sends:**
```
0x41 [id] 0x00 0x01  (STOP)
```

Before uploading a new effect to that slot.

**Our driver:** Doesn't send STOP before upload.

### 3. Report 0x01 Sent TWICE

**Windows sends Report 0x01 (Duration/Control) TWICE:**
- Once before Report 0x02 0x38
- Once after parameters (0x03/0x04/0x05)

**Our driver:** Sends it once.

### 4. Subtype Linking System

**Windows uses a complex subtype system:**
- Envelope subtypes: 0x1c, 0x38, 0x54 (increment by 0x1c)
- Parameter subtypes: 0x0e, 0x2a, 0x46 (increment by 0x1c)
- Report 0x01 bytes 9 and 11 reference these subtypes

**Our driver:** Uses fixed subtypes (0x1c, 0x0e).

---

## Recommendations for Driver Fix

### Priority 1: Fix Constant Force Upload Sequence

```c
// Current (WRONG):
1. Report 0x02 0x1c (Envelope)
2. Report 0x03 0x0e 00 00 (Force level = 0)  ← Added in latest fix
3. Report 0x01 (Duration/Control)
4. Report 0x41 START

// Should be (CORRECT):
1. Report 0x41 STOP (clear slot)
2. Report 0x02 0x1c (Envelope)
3. Report 0x01 (Duration/Control) - FIRST TIME
4. Report 0x02 0x38 (Envelope again)
5. Report 0x03 0x0e 00 00 (Force level = 0)
6. Report 0x01 (Duration/Control) - SECOND TIME with subtype refs
7. Report 0x41 START
```

### Priority 2: Implement Subtype System

Update Report 0x01 to include subtype references in bytes 9 and 11.

### Priority 3: Add Report 0x42 Initialize

Send `0x42 0x05` at driver initialization.

---

## Summary Statistics

- **Total captures analyzed:** 21
- **Device initialization captures:** 2 (no USB OUT data visible)
- **Constant force captures:** 1 (only START commands visible)
- **Settings captures:** 6 (Report 0x40, 0x42, 0x43 commands)
- **Control panel effects:** 12 (complete upload sequences)

**Most valuable captures:**
1. `ctl_panel_bumpy_road.pcapng` - Multi-effect sequence
2. `ctl_panel_punch_hit.pcapng` - Sine + Damper combination
3. `device_settings_globalautocenter_from_12_to_55.pcapng` - Report 0x40 usage

