# T500RS Development Session Summary

## Date: 2025-10-01

## Major Achievements ✅

### 1. USB Protocol Discovered
- **Captured Windows driver USB traffic** (2.6MB, 25,813 packets)
- **Identified transport method**: INTERRUPT transfers to endpoint 0x01 (NOT SET_REPORT!)
- **Documented protocol**: See `T500RS_PROTOCOL.md`
- **Key finding**: Report 0x41 (4 bytes) for effect control

### 2. Protocol Documentation
Created comprehensive documentation:
- `T500RS_PROTOCOL.md` - Complete protocol specification
- `IMPLEMENTATION_NEXT_STEPS.md` - Implementation guide
- `USB_CAPTURE_GUIDE.md` - Capture instructions (consolidated)

### 3. Driver Implementation
- ✅ Changed from SET_REPORT to INTERRUPT transfers
- ✅ Found INTERRUPT OUT endpoint (0x01)
- ✅ Implemented Report 0x41 control commands
- ✅ No more error -32 (EPIPE)

## Current Blocker ❌

### Error -11 (EAGAIN)
**Symptom**: All USB transfers fail with error -11
**Tried**:
1. `usb_interrupt_msg()` - Error -11
2. `hid_hw_output_report()` - Error -11  
3. `usb_control_msg()` with SET_REPORT - Error -11

**Possible causes**:
1. Device not ready / needs initialization
2. HID layer interfering
3. Wrong transfer method still
4. Missing device-specific setup

### Debug Output Issue
The data bytes are not being printed:
```
Sending INTERRUPT: ep=0x01, len=4, data=
```
Should show: `data=41 00 41 01`

This suggests either:
- Buffer is empty (bug in our code)
- Print is failing
- Data is not being populated

## What We Know Works

From Windows capture:
```
Report 0x41 (Effect Control):
41 00 41 01  - Start effect 0
41 01 41 01  - Start effect 1
41 00 00 01  - Stop effect 0
```

The device **definitely** accepts these commands in Windows.

## Technical Details

### Endpoint Information
- **Endpoint**: 0x01 (OUT)
- **Type**: INTERRUPT
- **Found**: Successfully detected during init

### Protocol Structure
```c
// Report 0x41 - Effect Control (4 bytes)
buf[0] = 0x41;           // Report ID
buf[1] = effect_id;      // 0-15
buf[2] = 0x41 or 0x00;   // Start/Stop
buf[3] = 0x01;           // Constant
```

### Current Code Flow
1. `t500rs_play_effect()` called
2. Calls `t500rs_send_control()`
3. Builds 4-byte buffer
4. Calls `t500rs_send_interrupt()`
5. Tries `usb_control_msg()` with SET_REPORT
6. **Fails with -11**

## Possible Solutions

### Option 1: Check Buffer Population
The data not printing suggests the buffer might not be populated. Need to verify:
- Is `t500rs_send_control()` actually building the buffer correctly?
- Is the buffer being passed correctly?

### Option 2: Bypass HID Layer Completely
The Windows driver might be using raw USB, not HID:
- Use `usb_submit_urb()` directly
- Don't go through HID layer at all
- Requires more complex URB management

### Option 3: Device Initialization
Windows sends initialization before any effects:
```
42 01 00 00 00 00 00 00 00 00 00 00 00 00  (Report 0x42)
0a 04 90 03 00 00 00 00 00 00 00 00 00 00  (Report 0x0a)
```
Maybe device needs this first?

### Option 4: Check HID Descriptor
The device might not declare the INTERRUPT endpoint in HID descriptor.
Need to check if we need to use raw USB interface instead of HID.

## Files Modified

### Core Implementation
- `src/tmt500rs/hid-tmt500rs-simple.h` - Updated structures and defines
- `src/tmt500rs/hid-tmt500rs-simple.c` - Implemented INTERRUPT support

### Testing & Documentation
- `test_with_debug.sh` - Fixed device path issue
- `find_t500rs.sh` - Helper to find active device
- `capture_t500rs_usb.sh` - Automated Windows capture
- `analyze_capture.sh` - Protocol analysis

### Documentation
- `T500RS_PROTOCOL.md` - Protocol specification
- `IMPLEMENTATION_NEXT_STEPS.md` - Implementation guide
- `USB_CAPTURE_GUIDE.md` - Consolidated capture guide
- `PHASE1_TEST_RESULTS.md` - Phase 1 completion
- `PHASE2_STATUS.md` - Current status

## Statistics

- **Time invested**: ~6 hours
- **Lines of code**: ~400 (driver), ~300 (scripts)
- **Documentation**: ~1500 lines
- **USB packets captured**: 25,813
- **Protocol commands identified**: 100+

## Next Steps (Recommendations)

### Immediate (Debug Current Issue)
1. **Fix debug output** - Find out why data isn't printing
2. **Verify buffer** - Add more debug to see if buffer is populated
3. **Check error details** - Get more info about why -11 occurs

### Short Term (Try Alternatives)
1. **Send initialization** - Try Report 0x42 and 0x0a first
2. **Use raw USB** - Bypass HID layer completely
3. **Check other wheels** - See how T300RS does INTERRUPT

### Long Term (If Still Stuck)
1. **Kernel mailing list** - Ask HID/USB experts
2. **Compare with Windows** - More detailed protocol analysis
3. **Hardware debugging** - USB analyzer if available

## Key Learnings

1. **USB capture is invaluable** - Saved us from guessing
2. **T500RS is different** - Uses INTERRUPT, not SET_REPORT like others
3. **Error codes matter** - -32 vs -11 tell different stories
4. **Documentation helps** - Having protocol documented makes debugging easier

## Resources

- Windows capture: `captures/t500rs_windows_20251001_165723.pcapng`
- Protocol doc: `T500RS_PROTOCOL.md`
- Implementation guide: `IMPLEMENTATION_NEXT_STEPS.md`
- USB capture guide: `USB_CAPTURE_GUIDE.md`

## Status

**Phase 1**: ✅ Complete - Device detection working
**Phase 2**: 🔄 In Progress - Protocol known, implementation blocked
**Phase 3**: ⏳ Pending - Force feedback not working yet

**Blocker**: Error -11 on all USB transfers
**Next**: Debug why transfers fail and data doesn't print

---

## For Next Session

1. Start by fixing debug output to see actual data
2. Try sending initialization commands first
3. Consider bypassing HID layer entirely
4. If still stuck, may need USB analyzer or kernel expert help

The protocol is fully documented and understood. The issue is purely in the implementation/communication layer.

