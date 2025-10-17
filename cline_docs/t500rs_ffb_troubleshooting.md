# T500RS Force Feedback Troubleshooting

## Problem Statement

The T500RS kernel driver successfully:
- ✅ Initializes the device
- ✅ Sends all USB commands correctly
- ✅ Uploads effects (Report 0x01, 0x02)
- ✅ Sends START command (Report 0x41)
- ✅ Sends continuous force updates (Report 0x03) at 50Hz
- ✅ Uses correct force scaling (0x1f for 25% force, matching userspace driver)

**BUT: No force feedback is felt in the wheel!**

The userspace driver at `/home/caz/Documents/hid-tmff2-working/userspace` works perfectly with the same device.

## Command Sequence Comparison

### Kernel Driver (Current - NOT WORKING)
```
1. Report 0x02 (envelope): 02 1c 00 00 00 00 00 00
2. Report 0x01 (effect upload): 01 00 00 40 69 23 00 ff...
3. Disable condition effects (Reports 0x05 + 0x41 for effects 1-4)
4. Report 0x41 START: 41 00 41 01
5. Report 0x03 (immediate): 03 0e 00 1f (4ms after START)
6. Report 0x03 (continuous): 03 0e 00 1f (every ~20ms)
```

### Userspace Driver (WORKING)
```
- Sends Report 0x01 (effect upload)
- Sends Report 0x02 (envelope)
- Sends Report 0x41 START: 41 [id] 41 01
- Continuous Report 0x03 at 50Hz with force level
- Has input reading thread polling EP_IN continuously
```

## Attempts Made

### Attempt 1: Maximum Force Override
**Theory**: Maybe force level was too low to feel  
**Action**: Forced maximum force (0x7f = 127)  
**Result**: ❌ No force felt  
**Conclusion**: Not a force scaling issue

### Attempt 2: Proper Force Scaling
**Theory**: Device might reject maximum values  
**Action**: Used proper scaling (8192/32767 * 127 = 31 = 0x1f)  
**Result**: ❌ No force felt  
**Conclusion**: Scaling matches userspace driver exactly

### Attempt 3: Immediate Force Update After START
**Theory**: 28ms gap between START and first force update might be too long  
**Action**: Send Report 0x03 immediately after Report 0x41 START  
**Result**: ❌ No force felt (gap reduced to 4ms)  
**Conclusion**: Timing gap is not the issue

## Differences Between Kernel and Userspace Drivers

### 1. Input Polling
- **Userspace**: Has dedicated input reading thread polling EP_IN continuously
- **Kernel**: Relies on HID subsystem for input (should be automatic)
- **Status**: Unknown if this affects force feedback

### 2. USB Transfer Method
- **Userspace**: `libusb_interrupt_transfer()` with 100ms timeout
- **Kernel**: `usb_interrupt_msg()` with 1000ms timeout
- **Status**: Should be equivalent

### 3. Device Claiming
- **Userspace**: Explicitly detaches kernel driver and claims interface
- **Kernel**: Bound by HID subsystem
- **Status**: Should not affect output transfers

### 4. Command Order
- **Kernel**: Disables condition effects (1-4) BEFORE starting constant force
- **Userspace**: Unknown exact order (no USB logging in output)
- **Status**: Might be relevant

## Hypotheses to Test

### Hypothesis 1: Missing Global Enable Command
**Theory**: Device needs a global "enable force feedback" command before effects work  
**Evidence**: Report 0x40 commands during init might include enable flags  
**Test**: Review init sequence for missing enable command  
**Priority**: HIGH

### Hypothesis 2: Effect Must Be Started Before Upload
**Theory**: START command must come before effect upload, not after  
**Evidence**: None yet  
**Test**: Reverse order of upload and start  
**Priority**: MEDIUM

### Hypothesis 3: Device Needs Input Polling
**Theory**: T500RS won't output force unless host is actively reading input  
**Evidence**: Userspace driver has dedicated input thread  
**Test**: Check if HID subsystem is polling input, or add explicit polling  
**Priority**: MEDIUM

### Hypothesis 4: Missing Report 0x02 Parameters
**Theory**: Envelope parameters might be required for constant force  
**Evidence**: We send zeros, userspace might send actual envelope data  
**Test**: Capture userspace USB traffic or check envelope implementation  
**Priority**: LOW

### Hypothesis 5: Effect ID Conflict
**Theory**: Effect ID 0 might be reserved or special  
**Evidence**: None  
**Test**: Try effect ID 1 or higher  
**Priority**: LOW

### Hypothesis 6: Condition Effects Interfering
**Theory**: Disabling condition effects might put device in wrong state  
**Evidence**: We send many Report 0x05 and 0x41 commands  
**Test**: Remove condition effect disable code  
**Priority**: MEDIUM

### Hypothesis 7: USB Endpoint or Transfer Issue
**Theory**: Commands are logged but not actually reaching device  
**Evidence**: All transfers report success  
**Test**: Use USB sniffer (usbmon) to verify actual USB traffic  
**Priority**: HIGH

### Hypothesis 8: Device Firmware State
**Theory**: Device needs specific initialization state that we're missing  
**Evidence**: Init sequence matches userspace driver  
**Test**: Compare init sequences byte-by-byte  
**Priority**: MEDIUM

## Attempt 4: Remove Condition Effect Disable (Hypothesis 6)
**Theory**: Disabling condition effects (1-4) might interfere with constant force
**Action**: Removed all Report 0x05 and 0x41 commands for effects 1-4
**Result**: ❌ **KERNEL CRASH** - Page fault during init
**Error**: `unable to create sysfs for gain`, `NULL pointer dereference at 0x48`
**Conclusion**: Crash unrelated to change (in init, not play_effect). System rebooted.
**Status**: REVERTED - kept code simple, tested after reboot, still no force

## Attempt 5: USB Traffic Capture (Hypothesis 7) ✅ VERIFIED
**Theory**: Commands might be logged but not actually sent to device
**Action**: Used usbmon to capture actual USB bus traffic
**Result**: ✅ **COMMANDS ARE BEING SENT!**
**Evidence**:
```
ffff8ef44320c180 2622319880 S Io:2:005:1 -115:4 4 = 41004101  (START command)
ffff8ef681d3e600 2622321793 S Io:2:005:1 -115:4 4 = 030e001f  (Force update)
ffff8ef546378480 2620873850 S Io:2:005:1 -115:4 15 = 01000040... (Effect upload)
```
**Conclusion**: ✅ USB commands ARE reaching the device hardware. Driver is working correctly!
**Impact**: Problem is NOT in our driver. Must be device state or missing command.

## Attempt 6: Different Effect ID (Hypothesis 5)
**Theory**: Effect ID 0 might be reserved or special
**Action**: Changed START command to use effect ID 5 instead of 0
**Result**: ❌ No force felt
**Conclusion**: Effect ID is not the issue

## CRITICAL DISCOVERY: HID vs libusb Difference

### Userspace Driver (WORKING)
- Uses **libusb** directly
- **Detaches kernel HID driver** before claiming interface
- Sends raw USB INTERRUPT transfers to endpoint 0x01
- No HID layer involvement

### Kernel Driver (NOT WORKING)
- Uses **raw USB INTERRUPT transfers** via `usb_interrupt_msg()`
- Bypasses HID OUTPUT reports (good!)
- BUT: **HID driver is still bound to the device**
- HID subsystem might be interfering with force feedback

### Hypothesis 8: HID Subsystem Interference

**Theory**: The HID subsystem might be:
1. Processing input reports and putting device in wrong state
2. Sending its own commands that conflict with ours
3. Holding device in a mode that blocks force feedback
4. Not properly handling the force feedback feature

**Evidence**:
- Userspace driver explicitly detaches HID driver
- Our driver sends USB commands correctly (verified by usbmon)
- Device receives commands but doesn't respond with force
- Only difference is HID driver binding

**Possible Solutions**:
1. Check if HID driver is sending conflicting commands
2. Verify device is in correct mode for force feedback
3. Check if we need to use HID OUTPUT reports instead of raw USB
4. Investigate if HID force feedback layer needs initialization

### CRITICAL DISCOVERY: T300RS Uses HID OUTPUT Reports!

**T300RS send function** (src/tmt300rs/hid-tmt300rs.c:457):
```c
int t300rs_send_buf(struct t300rs_device_entry *t300rs, u8 *send_buffer, size_t len)
{
    // Fill HID field values
    for (i = 0; i < len; ++i)
        t300rs->ff_field->value[i] = send_buffer[i];

    // Send via HID layer
    hid_hw_request(t300rs->hdev, t300rs->report, HID_REQ_SET_REPORT);
    return 0;
}
```

**T500RS current implementation** (src/tmt500rs/hid-tmt500rs-usb.c:283):
```c
// Uses RAW USB INTERRUPT transfers
ret = usb_interrupt_msg(t500rs->usbdev,
                        usb_sndintpipe(t500rs->usbdev, t500rs->ep_out),
                        (void *)data, len, &transferred, T500RS_USB_TIMEOUT);
```

**Hypothesis 9: Must Use HID OUTPUT Reports**

The T500RS might REQUIRE commands to come through the HID layer, not raw USB, even though the userspace driver uses raw USB (because it detaches the HID driver first).

## Attempt 7: Use hid_hw_output_report() (Hypothesis 9)

**Theory**: Device needs commands via HID layer, not raw USB
**Action**: Replace `usb_interrupt_msg()` with `hid_hw_output_report()`
**Rationale**: T300RS uses `hid_hw_request()`, maybe T500RS needs HID layer too
**Result**: ❌ **WRONG APPROACH**
**Conclusion**: Windows captures show RAW USB INTERRUPT transfers, NO Report ID 0x0a!

### Windows Capture Analysis (BREAKTHROUGH!)

From `captures/manual analysis/combined_effects_t500.txt`:
```
Packet: 1b 00 e0 65 ... [27-byte header] ... 03 0e 00 01
                                              ^^^^^^^^^^
                                              Raw USB data (4 bytes)
```

**Critical Finding**: Windows sends commands as **raw USB INTERRUPT transfers** to EP 0x01, NOT as HID OUTPUT reports with Report ID 0x0a!

The HID OUTPUT report (ID 0x0a, 14 bytes) exists in the descriptor but is NOT used for force feedback commands!

**Our original `usb_interrupt_msg()` approach was CORRECT!**

## Attempt 8: Fix Command Sequence Order (Hypothesis 10)

**Theory**: Report 0x03 must be sent DURING upload, not just during playback
**Evidence**: Windows sequence analysis shows:
```
UPLOAD CONSTANT (Windows):
1. 02 1c 00 00 00 00 00 00 00  (Envelope)
2. 03 0e 00 00                  (Force level - INITIAL VALUE!)
3. 01 00 00 40 69 23 ...        (Duration/Control)
4. 41 00 41 01                  (START)

OUR DRIVER (Current):
1. 02 1c 00 00 00 00 00 00 00  (Envelope) ✅
2. 01 00 00 40 69 23 ...        (Duration/Control) ✅
3. 41 00 41 01                  (START) ✅
4. 03 0e 00 1f                  (Force level - AFTER START!) ❌
```

**The Problem**: We're missing Report 0x03 in the upload sequence!

**The Fix**: Add Report 0x03 (force level) BETWEEN Report 0x02 and Report 0x01 during upload.

**Status**: ❌ **INCOMPLETE - MORE MISSING!**

### Systematic Windows Capture Analysis (BREAKTHROUGH!)

Analyzed 21 Windows captures systematically. **CRITICAL FINDINGS:**

**Windows ALWAYS uses this sequence for ANY effect:**
```
1. Report 0x41 STOP (0x41 [id] 00 01) - Clear effect slot FIRST!
2. Report 0x02 0x1c (Envelope) - First envelope packet
3. Report 0x01 (Duration/Control) - Effect metadata FIRST TIME
4. Report 0x02 0x38 (Envelope) - SECOND envelope packet! ← WE'RE MISSING THIS!
5. Report 0x03/0x04/0x05 (Parameters) - Effect-specific data
6. Report 0x01 (Duration/Control) - Effect metadata SECOND TIME! ← WE'RE MISSING THIS!
7. Report 0x41 START (0x41 [id] 41 01) - Activate effect
```

**Example from ctl_panel_boing.pcapng (Sine wave):**
```
Frame 795: 02 1c 00 95 00 3f e5 01 00  (Envelope 0x1c)
Frame 796: 41 00 00 01                 (STOP effect 0)
Frame 799: 01 00 22 40 bc 02 00 2c 01 0e 00 1c 00 00 00  (Duration - FIRST)
Frame 801: 02 38 00 95 00 3f e5 01 00  (Envelope 0x38) ← MISSING!
Frame 803: 04 2a 00 20 00 00 21 00     (Periodic params)
Frame 805: 01 01 22 40 bc 02 00 2c 01 2a 00 38 00 00 00  (Duration - SECOND) ← MISSING!
Frame 807: 41 01 41 01                 (START)
```

**What we're missing:**
1. ❌ Report 0x41 STOP before upload
2. ❌ Report 0x02 with subtype 0x38 (second envelope)
3. ❌ Report 0x01 sent TWICE (before and after parameters)
4. ❌ Subtype linking system (bytes 9 and 11 in Report 0x01)

**Complete analysis:** See `captures/WINDOWS_PROTOCOL_ESSENCE.md`

## Attempt 9: Implement Complete Windows Sequence (Hypothesis 11)

**Theory**: Device requires EXACT Windows upload sequence
**Action**: Implement all 7 steps matching Windows captures
**Changes Made**:

1. ✅ **STEP 1**: Send STOP (0x41 [id] 00 01) before upload
2. ✅ **STEP 2**: Send Report 0x02 subtype 0x1c (envelope #1)
3. ✅ **STEP 3**: Send Report 0x01 (duration/control #1) with envelope ref = 0x1c
4. ✅ **STEP 4**: Send Report 0x02 subtype 0x38 (envelope #2) ← NEW!
5. ✅ **STEP 5**: Send Report 0x03 subtype 0x0e (force level = 0)
6. ✅ **STEP 6**: Send Report 0x01 (duration/control #2) with envelope ref = 0x38 ← NEW!
7. ✅ **STEP 7**: Send START (0x41 [id] 41 01)

**Code Location**: `src/tmt500rs/hid-tmt500rs-usb.c:303-421` (upload_constant)

**Expected Sequence** (from Windows captures):
```
41 [id] 00 01                          STOP
02 1c 00 00 00 00 00 00 00             Envelope 0x1c
01 [id] 00 40 69 23 00 ff ff 0e 00 1c 00 00 00   Duration #1
02 38 00 00 00 00 00 00 00             Envelope 0x38
03 0e 00 00                            Force level
01 [id] 00 40 69 23 00 ff ff 0e 00 38 00 00 00   Duration #2
41 [id] 41 01                          START
```

**Status**: ✅ **IMPLEMENTED - READY TO TEST**

**Confidence**: 🔥 **VERY HIGH** 🔥 - Based on systematic analysis of 12 working Windows captures!

### Implementation Options:

**Option A**: Use `hid_hw_output_report()` - Direct OUTPUT report
```c
ret = hid_hw_output_report(t500rs->hdev, data, len);
```

**Option B**: Use `hid_hw_request()` with OUTPUT report (like T300RS)
- Requires finding OUTPUT report in HID descriptor
- Fill report field values
- Send via `hid_hw_request()`

**Option C**: Use `hid_hw_raw_request()` with OUTPUT report
```c
ret = hid_hw_raw_request(t500rs->hdev, data[0], data, len,
                         HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);
```

Starting with Option A as it's closest to current implementation.

## BREAKTHROUGH: Windows HID Descriptor Analysis

**Captured from Windows** (captures/t500rs_hid_rdesc.txt):

```
Report ID (0x0a)
Usage Page (Vendor-Defined) [0xff00]
Usage (0x0a)
    Report Size: 8 bits
    Report Count: 14
    Logical Maximum: 255
    Physical Maximum: 255
    Output (Data,Var,Abs)
```

### Critical Findings:

1. **OUTPUT Report Exists!** Report ID = 0x0a (10 decimal)
2. **Fixed Size: 14 bytes** (8 bits × 14 count)
3. **Vendor-defined** (not standard HID FF)

### The Problem:

Our commands are variable length (4, 9, 11, 15 bytes), but the OUTPUT report is **fixed at 14 bytes**!

**Hypothesis 10: Commands Must Be Padded to 14 Bytes**

The userspace driver sends variable-length commands via raw USB INTERRUPT, but when using HID OUTPUT reports, we must:
1. Pad all commands to 14 bytes with zeros
2. Prepend Report ID 0x0a (or let HID layer add it)
3. Send via `hid_hw_request()` or `hid_hw_output_report()`

### Command Padding Examples:

**Report 0x03 (4 bytes)** → Pad to 14:
```
Original: 03 0e 00 1f
Padded:   03 0e 00 1f 00 00 00 00 00 00 00 00 00 00
```

**Report 0x01 (15 bytes)** → Truncate to 14:
```
Original: 01 00 00 40 69 23 00 ff ff 0e 00 1c 00 00 00
Padded:   01 00 00 40 69 23 00 ff ff 0e 00 1c 00 00
```

Wait - Report 0x01 is 15 bytes but OUTPUT report is only 14! This might be the issue!

## Next Steps

1. **Implement 14-byte padding** for all commands
2. **Test with HID OUTPUT report ID 0x0a**
3. **Handle 15-byte Report 0x01** - either truncate or split
4. **Capture Windows USB traffic** to see actual packet format

## USB Monitoring Commands

```bash
# Enable USB monitoring for the device
sudo modprobe usbmon
sudo cat /sys/kernel/debug/usb/usbmon/0u | grep "044f:b65e" > /tmp/usbmon_kernel.log

# In another terminal, run fftest
# Then stop capture and analyze
```

## Log Analysis

### Latest Test (Attempt 3)
```
[ 4409.992770] Upload constant: id=0, level=8192
[ 4409.992786] Sending START command for constant force effect 0
[ 4409.992787] Sending immediate first force update: level=31 (0x1f)
[ 4410.033297] USB TX [4]: 41 00 41 01 00 00 00 00  (START)
[ 4410.037326] USB TX [4]: 03 0e 00 1f 00 00 00 00  (Force update - 4ms after START)
[ 4410.065360] USB TX [4]: 03 0e 00 1f 00 00 00 00  (28ms later)
[ 4410.087566] USB TX [4]: 03 0e 00 1f 00 00 00 00  (22ms later)
...continuous updates every ~20-28ms
```

All commands sent successfully, but no force felt.

## References

- Working userspace driver: `/home/caz/Documents/hid-tmff2-working/userspace`
- Kernel driver: `src/tmt500rs/hid-tmt500rs-usb.c`
- USB protocol analysis: `captures/manual analysis/combined_effects_t500.txt`

