# T500RS Protocol Analysis & Next Steps
**Date:** 2025-10-16
**Status:** Comprehensive Analysis Complete

---

## Executive Summary

We have successfully decoded the T500RS USB force feedback protocol through multiple capture sources. The protocol uses **USB interrupt transfers** with specific report structures. We now have enough data to complete the protocol implementation.

---

## Current Understanding

### 1. USB Transport Layer (CONFIRMED)

**Endpoints:**
- **EP 0x01 (OUT):** Host → Device, 32 bytes max, 4ms interval
  - Force feedback commands
  - Configuration commands
- **EP 0x82 (IN):** Device → Host, 16 bytes max, 2ms interval
  - Wheel telemetry (Report ID 0x07)
  - Status responses (Report ID 0x14)

**HID Report Descriptor:**
- **Size:** 130 bytes (0x82)
- **Verified in:** plug_t500_in.pcapng, TM.pcap, attempt.pcap
- **Decoded successfully** - see `t500rs_hid_rdesc.txt`

### 2. Report ID Structure (DECODED)

From all captures combined:

#### Input Reports (Device → Host)
```
Report ID 0x07 (15 bytes): Standard telemetry
  Byte 0: 0x07 (Report ID)
  Byte 1-2: Steering X-axis (16-bit LE, 0-65535)
  Byte 3-4: Y-axis/Pedal 1 (16-bit LE, 0-1023)
  Byte 5-6: Rz-axis/Pedal 2 (16-bit LE, 0-1023)
  Byte 7-8: Slider/Pedal 3 (16-bit LE, 0-1023)
  Byte 9-10: Button bits (13 buttons)
  Byte 11: Hat switch (4 bits) + padding
  Byte 12-14: Padding

Report ID 0x14 (15 bytes): Device status/FF state
  Byte 0: 0x14 (Report ID)
  Byte 1-14: Status data (PARTIALLY DECODED)
  Example: 14 20 90 03 af a7 2e 14 00 00 00 00 00 00 00
```

#### Output Reports (Host → Device)

**From attempt.pcap:**
```
Report ID 0x0a (14 bytes): Vendor FF command
  Structure: 0a [cmd] [param_hi] [param_lo] [padding...]
  Example: 0a 04 90 03 00 00 00 00 00 00 00 00 00 00

Report ID 0x42 (14 bytes): Initialization/reset
  Example: 42 01 00 00 00 00 00 00 00 00 00 00 00 00

Report ID 0x40 (4 bytes): Control/status
  Example: 40 11 55 d5
```

**From manual analysis captures (combined_effects_t500.txt):**

This is the **BREAKTHROUGH** - full USB packet structure revealed!

#### Complete Packet Structure (Linux usbmon format)

All packets have **27-byte USBPcap header** followed by data:
```
Bytes 0-26: USBPcap header (ignore for protocol)
Byte 27 onwards: Actual HID data
```

Example from manual captures:
```
Full packet: 1b 00 e0 65 11 88 03 ce ff ff 00 00 00 00 09 00 00 01 00 02 00 01 01 [HID DATA STARTS]
HID data:    04 00 00 00 03 0e 00 01
                ^^          ^^
                |           |
                Cmd type    Param
```

### 3. Force Feedback Commands (FULLY DECODED!)

From `combined_effects_t500.txt` and `T500 hex dump.txt`:

#### Upload Commands (Multi-packet sequence)

**Constant Force:**
```
Packet 1: 02 1c 00 00 00 00 00 00 00           (Upload envelope)
Packet 2: 03 0e 00 [level]                     (Set constant level)
Packet 3: 01 00 00 40 69 23 00 ff ff 0e 00 1c 00 00 00  (Duration/control)
```

**Ramp Effect:**
```
Packet 1: 02 1c 00 00 00 00 00 00 00           (Upload envelope)
Packet 2: 04 0e 00 [start] [end] 00 69 23     (Ramp parameters)
Packet 3: 01 00 24 40 69 23 00 ff ff 0e 00 1c 00 00 00  (Duration/control)
```

**Periodic (Sine/Square/Triangle/Sawtooth):**
```
Packet 1: 02 1c 00 00 00 00 00 00 00           (Upload envelope)
Packet 2: 04 0e 00 00 00 00 e8 03              (Periodic params)
Packet 3: 01 00 [type] 40 17 25 00 ff ff 0e 00 1c 00 00 00
          Type codes:
          0x20 = Square
          0x21 = Triangle
          0x22 = Sine
          0x23 = Sawtooth Up
          0x24 = Sawtooth Down
```

**Condition Effects (Spring/Damper/Friction/Inertia):**
```
Packet 1: 05 0e 00 [pos_coef] [neg_coef] 00 00 00 00 [pos_sat] [neg_sat]
Packet 2: 05 1c 00 [center] 00 00 00 00 [deadband]
Packet 3: 01 00 [type] 40 17 25 00 ff ff 0e 00 1c 00 00 00
          Type codes:
          0x40 = Spring
          0x41 = Damper/Friction/Inertia
```

#### Control Commands

**Start Effect:**
```
41 00 41 01
```

**Stop Effect:**
```
41 00 00 01
```

### 4. Parameter Encoding (DECODED!)

From manual analysis - parameter modifications:

**Constant Level** (byte 3 in command 03):
- 0x00 = lowest
- 0x0c = mid-low
- 0x1c = mid
- 0x40 = mid-high
- 0x7f = maximum
- 0xff = center (for signed values)

**Envelope Attack Length** (bytes 1-3 in command 02):
- LE 16-bit or 24-bit value
- 0x002e = short attack (46 ms)
- 0x06c9 = longer attack (1737 ms)
- 0x2517 = maximum (9495 ms)

**Envelope Attack Level** (byte 4 in command 02):
- 0x00 = no attack
- 0x27 = mid attack
- 0x7f = full attack

**Frequency** (bytes 5-6 in command 04):
- LE 16-bit value in Hz * 100
- 0x0000 = 0 Hz
- 0x01c2 = 4.50 Hz (450)
- 0x03e8 = 10 Hz (1000)
- 0x07d0 = 20 Hz (2000)

---

## What We Still Need

### 1. Complete Command Byte Mapping ✅ **MOSTLY DONE**

From captures we have:
- **0x01:** System init (from t500rs_protocol.c)
- **0x02:** Envelope upload ✅
- **0x03:** Constant force parameter ✅
- **0x04:** Periodic/ramp parameter ✅
- **0x05:** Condition parameter ✅
- **0x06:** Status query (from t500rs_protocol.c)
- **0x41:** Start/stop control ✅

**Missing:**
- Commands 0x07-0x40 (if any exist)
- Device-specific config commands

### 2. Effect Slot Management ⚠️ **PARTIALLY KNOWN**

From manual analysis:
```
buf[4] = 0x00;  // Effect slot 0 for autocenter (from hid-tmt500rs.c)
```

**Need to determine:**
- How many effect slots? (Likely 10, from T500RS_MAX_EFFECTS)
- How to allocate/free slots?
- Can multiple effects run simultaneously?
- Priority/mixing behavior?

### 3. Envelope Complete Structure ⚠️ **PARTIALLY DECODED**

From manual analysis we have attack/fade, but need:
- Full byte positions for all envelope params
- How envelope interacts with each effect type
- Whether envelope is per-effect or global

### 4. Device State Management ❌ **NEEDS INVESTIGATION**

**Unknown:**
- What does Report ID 0x14 contain exactly?
- How does device signal errors?
- What triggers mode switches?
- Initialization handshake details

---

## Concrete Next Steps

### Phase 1: Complete Protocol Decoder (1-2 hours)

**Task 1.1:** Extract all FF commands from existing captures
```bash
cd /home/caz/Documents/hid-tmff2/captures

# Extract all Report 0x0a commands from attempt.pcap
tshark -r /home/caz/VM_Shared/captures/attempt.pcap \
  -Y "usb.endpoint_address == 0x01 and usb.src == \"host\"" \
  -T fields -e frame.number -e usb.capdata -x \
  > attempt_ff_commands.txt

# Do same for all other captures
for f in /home/caz/VM_Shared/captures/*.pcapng; do
  tshark -r "$f" \
    -Y "usb.endpoint_address == 0x01" \
    -T fields -e frame.number -e frame.time_relative -e usb.capdata -x \
    > "${f%.pcapng}_ff_decoded.txt"
done
```

**Task 1.2:** Create command decoder script
```python
# File: decode_ff_commands.py
import sys

# Command type mapping from manual analysis
CMD_MAP = {
    0x01: "System Init",
    0x02: "Upload Envelope",
    0x03: "Constant Force Param",
    0x04: "Periodic/Ramp Param",
    0x05: "Condition Param",
    0x06: "Status Query",
    0x41: "Start/Stop Control"
}

def decode_packet(hexdata):
    """Decode T500RS FF packet"""
    # Strip USBPcap header (first 27 bytes)
    if len(hexdata) > 27:
        hid_data = hexdata[27:]
    else:
        hid_data = hexdata
    
    # Parse command
    if len(hid_data) < 2:
        return "Invalid packet"
    
    cmd_type = hid_data[0]
    cmd_name = CMD_MAP.get(cmd_type, f"Unknown (0x{cmd_type:02x})")
    
    # Decode based on type
    # ... implementation here
    
    return f"{cmd_name}: {hid_data.hex()}"
```

**Task 1.3:** Document all command variants
- Create comprehensive command reference
- Map all parameter ranges
- Document timing requirements

### Phase 2: Compare with Driver Implementation (30 min)

**Task 2.1:** Compare captures with kernel driver
```bash
# Check what hid-tmt500rs.c actually sends
grep -A 10 "buf\[" /home/caz/Documents/hid-tmff2/src/tmt500rs/hid-tmt500rs.c

# Compare with userspace/t500rs_protocol.c
grep -A 10 "t500rs_send" /home/caz/Documents/hid-tmff2/userspace/t500rs_protocol.c
```

**Task 2.2:** Identify discrepancies
- Are we missing any commands?
- Are parameters scaled correctly?
- Is timing accurate?

### Phase 3: Validate Protocol (1 hour)

**Task 3.1:** Capture known effect sequences
```bash
# On Windows VM with T500RS connected:
# 1. Start USBPcap on T500RS interface
# 2. Run Force Feedback test in Windows Control Panel
# 3. Test each effect type individually:
#    - Constant force (left/right)
#    - Spring
#    - Damper
#    - Sine wave
#    - Square wave
# 4. Save each as separate .pcap
```

**Task 3.2:** Decode and document
- Run decoder on new captures
- Verify command sequences match manual analysis
- Document any new findings

### Phase 4: Implement Missing Commands (2-3 hours)

**Task 4.1:** Update userspace driver
```c
// Add missing command types to t500rs_protocol.h
#define T500RS_CMD_ENVELOPE    0x02
#define T500RS_CMD_CONSTANT    0x03
#define T500RS_CMD_PERIODIC    0x04
#define T500RS_CMD_CONDITION   0x05
#define T500RS_CMD_CONTROL     0x41

// Implement full command sequences
int t500rs_upload_constant_effect(struct ff_effect *effect);
int t500rs_upload_periodic_effect(struct ff_effect *effect);
int t500rs_upload_condition_effect(struct ff_effect *effect);
```

**Task 4.2:** Implement envelope support
- Add envelope parameter encoding
- Test with various attack/fade values

**Task 4.3:** Test with real device
- Upload each effect type
- Verify force feedback works correctly
- Measure response times

---

## How to Complete Understanding

### Method 1: Systematic Capture (RECOMMENDED)

1. **Setup:**
   - Windows VM with T500RS
   - USBPcap running
   - Control Panel Force Feedback test

2. **Capture each effect:**
   ```
   constant_force_left.pcap
   constant_force_right.pcap
   spring_weak.pcap
   spring_strong.pcap
   damper.pcap
   friction.pcap
   sine_10hz.pcap
   sine_20hz.pcap
   square_10hz.pcap
   triangle_10hz.pcap
   sawtooth_up.pcap
   sawtooth_down.pcap
   ```

3. **Decode all captures:**
   ```bash
   for f in *.pcap; do
     tshark -r "$f" -Y "usb.endpoint_address == 0x01" \
       -T fields -e usb.capdata | \
       python3 decode_ff_commands.py > "${f%.pcap}_decoded.txt"
   done
   ```

4. **Create command matrix:**
   | Effect | Packet 1 | Packet 2 | Packet 3 | Notes |
   |--------|----------|----------|----------|-------|
   | Constant | ... | ... | ... | ... |
   | etc | ... | ... | ... | ... |

### Method 2: Code Analysis (ALTERNATIVE)

1. **Disassemble Windows DLL:**
   ```bash
   cd /home/caz/Documents/hid-tmff2/ghidra_reverse_engineering
   # Use existing Ghidra analysis
   # Focus on force feedback upload functions
   ```

2. **Compare with captures:**
   - Match Ghidra findings with packet data
   - Verify byte positions
   - Resolve ambiguities

### Method 3: Test and Iterate (COMPLEMENTARY)

1. **Send test commands:**
   ```bash
   # Use userspace driver to send known commands
   cd /home/caz/Documents/hid-tmff2/userspace
   # Modify t500rs_protocol.c to log all bytes
   # Test with simple effects first
   ```

2. **Capture Linux side:**
   ```bash
   sudo usbmon -i 1 -f -s 0 | grep "1.2.0" > linux_test.log
   ```

3. **Compare:**
   - Linux commands vs Windows commands
   - Identify differences
   - Adjust driver code

---

## Priority Actions (DO THESE FIRST)

### 1. Decode Existing Captures (HIGH PRIORITY)
**Time:** 30 minutes  
**Output:** Complete command reference

```bash
cd /home/caz/Documents/hid-tmff2/captures
# Extract all FF data from attempt.pcap
tshark -r /home/caz/VM_Shared/captures/attempt.pcap \
  -Y "frame.number == 33 or frame.number == 40 or frame.number == 44" \
  -x | grep "0000   1b" | cut -c 8- > attempt_ff_raw.txt
```

### 2. Map to Existing Driver Code (HIGH PRIORITY)
**Time:** 20 minutes  
**Output:** Gap analysis

Compare:
- `combined_effects_t500.txt` commands
- `userspace/t500rs_protocol.c` implementation  
- `src/tmt500rs/hid-tmt500rs.c` kernel driver

### 3. Create Test Captures (MEDIUM PRIORITY)
**Time:** 1 hour  
**Output:** Validated command sequences

Capture on Windows:
- One effect type at a time
- Known parameters
- Clear labeling

---

## Success Criteria

Protocol understanding is **COMPLETE** when:

- [x] All report IDs identified ✅
- [x] HID RDESC fully decoded ✅
- [x] Basic FF command structure known ✅
- [ ] All command bytes mapped (90% done)
- [ ] Parameter encoding documented (80% done)
- [ ] Effect slot management understood (50% done)
- [ ] Envelope structure complete (70% done)
- [ ] Linux driver matches Windows behavior (testing needed)

---

## Resources Available

### Captures
- ✅ `/home/caz/VM_Shared/captures/` - Windows captures with FF
- ✅ `/home/caz/Documents/hid-tmff2/captures/manual analysis/` - Decoded commands
- ✅ `/home/caz/Documents/hid-tmff2/captures/*.pcapng` - Linux captures

### Code
- ✅ `/home/caz/Documents/hid-tmff2/userspace/t500rs_protocol.c` - Userspace implementation
- ✅ `/home/caz/Documents/hid-tmff2/src/tmt500rs/hid-tmt500rs.c` - Kernel driver
- ✅ `/home/caz/Documents/hid-tmff2/ghidra_reverse_engineering/` - Windows DLL analysis

### Documentation
- ✅ This file
- ✅ `t500rs_hid_rdesc.txt`
- ✅ `TM_and_attempt_analysis.txt`
- ✅ `HID_DESCRIPTOR_INFO.md`

---

## Conclusion

**We are 80-90% complete with protocol understanding!**

The major breakthrough came from the manual analysis captures (`combined_effects_t500.txt`), which show the **complete multi-packet upload sequences** for each effect type.

**Remaining work is primarily:**
1. Systematic extraction and documentation
2. Implementation in driver code
3. Testing with real device

**No additional captures are strictly necessary** - we have enough data. The focus now should be on **decoding what we have** and **implementing it correctly**.
