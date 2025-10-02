# T500RS USB Protocol (Discovered from Windows Capture)

## Transport Method

**INTERRUPT transfers** to endpoint 0x01 (NOT SET_REPORT!)

This is why our driver failed - we were using `hid_hw_raw_request` with SET_REPORT, but the T500RS uses INTERRUPT transfers.

## Report Structure

### Report 0x41: Effect Control (4 bytes)
```
Byte 0: 0x41 (Report ID)
Byte 1: Effect ID (0-15)
Byte 2: Command
  - 0x00 = Stop effect
  - 0x41 = Start/Play effect
Byte 3: 0x01 (constant)
```

**Examples from capture:**
- `41 00 00 01` - Stop effect 0
- `41 01 41 01` - Start effect 1
- `41 02 41 01` - Start effect 2

**Frequency:** Most common commands (26 stops, 20 starts for effect 1)

### Report 0x01: Effect Parameters - Type 1 (15 bytes)
```
Byte 0: 0x01 (Report ID)
Byte 1: Effect ID
Byte 2: Effect type/flags
Byte 3-4: Parameter 1 (16-bit little-endian)
Byte 5-6: Parameter 2 (16-bit little-endian)
Byte 7-8: Parameter 3 (16-bit little-endian)
Byte 9-10: Parameter 4 (16-bit little-endian)
Byte 11-12: Parameter 5 (16-bit little-endian)
Byte 13-14: Parameter 6 (16-bit little-endian)
```

**Examples:**
- `01 00 22 40 e2 04 00 e8 03 0e 00 1c 00 00 00` - Effect 0 parameters
- `01 01 22 40 e2 04 00 e8 03 2a 00 38 00 00 00` - Effect 1 parameters
- `01 02 20 40 fa 00 00 0e 06 46 00 54 00 00 00` - Effect 2 parameters

### Report 0x02: Effect Parameters - Type 2 (9 bytes)
```
Byte 0: 0x02 (Report ID)
Byte 1-2: Parameter 1 (16-bit)
Byte 3-4: Parameter 2 (16-bit)
Byte 5-6: Parameter 3 (16-bit)
Byte 7-8: Parameter 4 (16-bit)
```

**Examples:**
- `02 1c 00 90 01 00 52 03 00` - Parameters for effect
- `02 38 00 90 01 00 52 03 00` - Parameters for effect
- `02 54 00 b3 00 00 46 00 36` - Parameters for effect

### Report 0x04: Effect Parameters - Type 3 (8 bytes)
```
Byte 0: 0x04 (Report ID)
Byte 1-2: Parameter 1 (16-bit)
Byte 3-4: Parameter 2 (16-bit)
Byte 5-6: Parameter 3 (16-bit)
Byte 7: Parameter 4 (8-bit)
```

**Examples:**
- `04 2a 00 2c 00 00 14 00` - Parameters
- `04 46 00 59 00 00 c2 01` - Parameters

### Report 0x42: Initialization (variable length)
```
42 01 00 00 00 00 00 00 00 00 00 00 00 00 - Init command
42 04 - Short init
42 05 - Short init
```

### Report 0x0a: Configuration (15 bytes)
```
0a 04 90 03 00 00 00 00 00 00 00 00 00 00 - Config
0a 04 12 10 00 00 00 00 00 00 00 00 00 00 - Config
```

## Command Sequence

### Initialization
1. `42 01 00 00 00 00 00 00 00 00 00 00 00 00` - Init
2. `0a 04 90 03 00 00 00 00 00 00 00 00 00 00` - Config
3. `0a 04 12 10 00 00 00 00 00 00 00 00 00 00` - Config

### Upload Effect
1. Send Report 0x01 with effect parameters (15 bytes)
2. Optionally send Report 0x02 with additional parameters (9 bytes)
3. Optionally send Report 0x04 with more parameters (8 bytes)

### Play Effect
1. `41 [effect_id] 41 01` - Start effect

### Stop Effect
1. `41 [effect_id] 00 01` - Stop effect

## Key Findings

1. **Uses INTERRUPT transfers, not SET_REPORT**
   - Must use `usb_interrupt_msg()` or similar
   - NOT `hid_hw_raw_request()` with HID_REQ_SET_REPORT

2. **Multiple report types for different purposes**
   - 0x41 for control (start/stop)
   - 0x01, 0x02, 0x04 for effect parameters
   - 0x42 for initialization
   - 0x0a for configuration

3. **Effect control is simple**
   - 4-byte command: `41 [id] [cmd] 01`
   - Start = 0x41, Stop = 0x00

4. **Effect parameters are complex**
   - 15-byte packets with multiple 16-bit parameters
   - Need to decode what each parameter means

## Implementation Plan

### Phase 1: Basic INTERRUPT Support
1. Change from `hid_hw_raw_request` to INTERRUPT endpoint
2. Find output INTERRUPT endpoint
3. Implement `usb_interrupt_msg()` or URB submission

### Phase 2: Effect Control
1. Implement Report 0x41 (start/stop)
2. Test with simple on/off commands

### Phase 3: Effect Parameters
1. Decode Report 0x01 parameter structure
2. Map Linux FF effect parameters to T500RS format
3. Implement effect upload

### Phase 4: Full Support
1. Implement all report types
2. Handle initialization sequence
3. Test all effect types

## Next Steps

1. Modify driver to use INTERRUPT endpoint instead of SET_REPORT
2. Implement Report 0x41 for basic start/stop
3. Test with physical device
4. Iterate on effect parameters

## Capture Statistics

- Total packets: 25,813
- INTERRUPT OUT: 424
- SET_REPORT: 2 (not used for FF)
- Most common: Effect control (0x41) - 85 commands
- Effect parameters (0x01) - 40+ variations

This capture shows extensive force feedback testing with multiple effects!

