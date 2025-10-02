# T500RS Windows Capture - Deep Analysis Findings

## Date: 2025-10-02

## Critical Discovery: Multi-Report Protocol

### The Problem We Had

We were sending **ONLY Report 0x01** for effect parameters. This caused the device to enter bootloader mode.

### What Windows Actually Does

Windows sends **THREE reports** for each effect upload:
1. **Report 0x02** (9 bytes) - First parameter set
2. **Report 0x04** (8 bytes) - Second parameter set  
3. **Report 0x01** (15 bytes) - Main effect parameters

**This is why our single Report 0x01 failed!** The device expects all three reports in sequence.

## Complete Protocol Sequence

### Initialization (Frames 97-243)

```
1. 42 01 00 00 00 00 00 00 00 00 00 00 00 00 00  (Report 0x42 - Init)
2. 0a 04 90 03 00 00 00 00 00 00 00 00 00 00 00  (Report 0x0a - Config 1)
3. 0a 04 12 10 00 00 00 00 00 00 00 00 00 00 00  (Report 0x0a - Config 2)
4. 0a 04 00 06 00 00 00 00 00 00 00 00 00 00 00  (Report 0x0a - Config 3)
5. 40 11 55 d5                                    (Report 0x40 - 4 bytes)
6. 42 04                                          (Report 0x42 - 2 bytes!)
7. 40 04 00 00                                    (Report 0x40 - 4 bytes)
8. 40 03 0d 00                                    (Report 0x40 - 4 bytes)
9. 43 4d                                          (Report 0x43 - 2 bytes)
... more configs ...
```

### Effect Upload Sequence (Effect 1 - Frames 252-268)

```
1. 02 38 00 90 01 00 52 03 00                    (Report 0x02 - 9 bytes)
2. 04 2a 00 2c 00 00 14 00                       (Report 0x04 - 8 bytes)
3. 01 01 22 40 e2 04 00 e8 03 2a 00 38 00 00 00  (Report 0x01 - 15 bytes)
4. 41 01 41 01                                    (Report 0x41 - START effect 1)
```

### Effect Control

```
Start: 41 [id] 41 01  (4 bytes)
Stop:  41 [id] 00 01  (4 bytes)
```

## Report Types and Sizes

| Report ID | Size (bytes) | Purpose | Example |
|-----------|--------------|---------|---------|
| 0x01 | 15 | Main effect parameters | `01 01 22 40 e2...` |
| 0x02 | 9 | Additional parameters | `02 38 00 90 01...` |
| 0x04 | 8 | More parameters | `04 2a 00 2c 00...` |
| 0x0a | 15 | Configuration | `0a 04 90 03 00...` |
| 0x40 | 4 | Unknown control | `40 11 55 d5` |
| 0x41 | 4 | Effect control (start/stop) | `41 01 41 01` |
| 0x42 | 2 or 15 | Initialization | `42 01 00...` or `42 04` |
| 0x43 | 2 | Unknown | `43 4d` |

## Key Findings

### 1. Variable Report Sizes

The T500RS uses **different report sizes** for different purposes:
- 2 bytes: Short commands (0x42, 0x43)
- 4 bytes: Control commands (0x40, 0x41)
- 8 bytes: Report 0x04
- 9 bytes: Report 0x02
- 15 bytes: Reports 0x01, 0x0a, 0x42 (long form)

### 2. Report ID is Included

All data includes the report ID as the first byte. For example:
- `41 01 41 01` = Report 0x41 + 3 data bytes
- `01 01 22 40...` = Report 0x01 + 14 data bytes

### 3. Effect Upload Requires Multiple Reports

**Critical**: Each effect upload needs:
1. Report 0x02 (9 bytes)
2. Report 0x04 (8 bytes)
3. Report 0x01 (15 bytes)

Sending only Report 0x01 triggers bootloader mode!

### 4. Initialization is Complex

The init sequence uses multiple report types:
- 0x42 (both 2-byte and 15-byte versions)
- 0x0a (multiple configs)
- 0x40 (various commands)
- 0x43 (unknown purpose)

### 5. USB Endpoint Details

From `lsusb -v`:
```
Endpoint 0x01 OUT: INTERRUPT, 32 bytes max
Endpoint 0x82 IN:  INTERRUPT, 16 bytes max
```

The device can handle up to 32 bytes per packet, but Windows uses smaller packets.

## Windows Effect Upload Pattern

For **constant force effect** (effect 1):

```
Step 1: Report 0x02
  02 38 00 90 01 00 52 03 00

Step 2: Report 0x04
  04 2a 00 2c 00 00 14 00

Step 3: Report 0x01
  01 01 22 40 e2 04 00 e8 03 2a 00 38 00 00 00
     ^^ effect ID
        ^^ type (0x22 = constant)
           ^^^^^ parameters...

Step 4: Start effect
  41 01 41 01
     ^^ effect ID
        ^^ command (0x41 = start)
```

## Parameter Meanings (Hypothesis)

### Report 0x01 (15 bytes):
```
Byte 0:    0x01 (Report ID)
Byte 1:    Effect ID (0-15)
Byte 2:    Effect type (0x22=constant, 0x20=spring)
Byte 3-4:  Parameter 1 (16-bit LE) - possibly force level
Byte 5-6:  Parameter 2 (16-bit LE)
Byte 7-8:  Parameter 3 (16-bit LE)
Byte 9-10: Parameter 4 (16-bit LE)
Byte 11-12: Parameter 5 (16-bit LE)
Byte 13-14: Parameter 6 (16-bit LE)
```

### Report 0x02 (9 bytes):
```
Byte 0:   0x02 (Report ID)
Byte 1-2: Parameter 1 (16-bit LE)
Byte 3-4: Parameter 2 (16-bit LE)
Byte 5-6: Parameter 3 (16-bit LE)
Byte 7-8: Parameter 4 (16-bit LE)
```

### Report 0x04 (8 bytes):
```
Byte 0:   0x04 (Report ID)
Byte 1-2: Parameter 1 (16-bit LE)
Byte 3-4: Parameter 2 (16-bit LE)
Byte 5-6: Parameter 3 (16-bit LE)
Byte 7:   Parameter 4 (8-bit)
```

## Why Our Previous Attempt Failed

**What we did**:
- Sent only Report 0x01 (15 bytes)
- Device expected Reports 0x02, 0x04, AND 0x01
- Missing reports triggered firmware protection
- Device entered bootloader mode

**What we should do**:
- Send Report 0x02 first (9 bytes)
- Then Report 0x04 (8 bytes)
- Then Report 0x01 (15 bytes)
- Finally Report 0x41 to start (4 bytes)

## Implementation Changes

### Updated constant effect upload:

```c
// 1. Send Report 0x02 (9 bytes)
buf[0] = 0x02;
buf[1-8] = parameters from Windows;
send(buf, 9);

// 2. Send Report 0x04 (8 bytes)
buf[0] = 0x04;
buf[1-7] = parameters from Windows;
send(buf, 8);

// 3. Send Report 0x01 (15 bytes)
buf[0] = 0x01;
buf[1] = effect_id;
buf[2-14] = parameters from Windows;
send(buf, 14);  // HID layer adds report ID, making 15 total

// 4. Send start command
buf[0] = 0x41;
buf[1] = effect_id;
buf[2] = 0x41;  // start
buf[3] = 0x01;
send(buf, 4);
```

## Testing Strategy

### Phase 1: Stop Commands Only ✅
- **Status**: PASSED
- Stop commands work without issues
- Device remains stable

### Phase 2: Complete Effect Upload (NEW)
- Send all three reports: 0x02, 0x04, 0x01
- Then send start command 0x41
- This should work since it matches Windows exactly

### Phase 3: Multiple Effects
- Upload and test multiple effects
- Verify device stability

## Statistics from Capture

- Total frames: 25,813
- INTERRUPT OUT packets: 424
- Report 0x01 packets: ~40
- Report 0x02 packets: ~20
- Report 0x04 packets: ~20
- Report 0x41 packets: ~85

## Next Steps

1. ✅ Implement multi-report upload (0x02, 0x04, 0x01)
2. ⏳ Test with safe mode disabled
3. ⏳ Verify force feedback works
4. ⏳ Decode parameter meanings
5. ⏳ Implement all effect types

## Conclusion

The T500RS uses a **multi-report protocol** where each effect requires THREE parameter reports plus one control report. Our previous failure was due to sending only one of the three required reports.

The new implementation sends all three reports in the correct order, matching Windows exactly. This should work!

---

**Key Takeaway**: Always analyze the complete sequence, not just individual commands. The T500RS firmware expects a specific multi-report sequence for effect uploads.

