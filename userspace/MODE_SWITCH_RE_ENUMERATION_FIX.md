# Mode Switch Re-Enumeration Fix

## Problem

Device not switching from boot mode (b65d) to normal mode (b65e) after initialization sequence.

### Symptoms
```
[INFO] Device in boot mode, will send initialization sequence...
[INFO] Initialization complete (mode switch commands sent)
[INFO] Device was in boot mode - it should now re-enumerate as normal mode
[INFO] Waiting for device to disconnect and reconnect...
[INFO] Device did not disconnect as expected
[INFO] Trying to reopen anyway...
[INFO] Waiting for device to re-enumerate in normal mode...
[INFO] Normal mode device not found yet, waiting... (attempt 1/10)
...
[ERROR] Device did not switch to normal mode after 10 attempts!
```

### Device Status
```bash
$ lsusb | grep -i thrust
Bus 001 Device 003: ID 044f:b65d ThrustMaster, Inc. Thrustmaster FFB Wheel
```

Device stuck in boot mode (b65d), not switching to normal mode (b65e).

## Root Cause

**USB interface not released before closing handle**

The initialization sequence was correct, but the device couldn't re-enumerate because:

1. Driver sends initialization sequence (Reports 0x42, 0x0a, etc.)
2. Device receives commands and prepares to re-enumerate
3. Driver closes USB handle with `libusb_close()`
4. **BUT** interface was never released with `libusb_release_interface()`
5. Device can't re-enumerate while interface is still claimed
6. Device stuck in boot mode

### Proper libusb Cleanup Sequence

According to libusb documentation:
```c
/* Correct order */
libusb_release_interface(handle, interface);  // 1. Release interface
libusb_close(handle);                         // 2. Close handle
```

Our code was doing:
```c
/* WRONG - missing release */
libusb_close(handle);  // Interface still claimed!
```

## Solution

Add `libusb_release_interface()` before `libusb_close()` to allow device re-enumeration.

### Code Change

**Before:**
```c
if (!disconnected) {
    LOG_INFO("Device did not disconnect as expected");
    LOG_INFO("Trying to reopen anyway...");
}

/* Close current handle */
if (usb_handle) {
    libusb_close(usb_handle);  // ❌ Interface not released!
    usb_handle = NULL;
}
```

**After:**
```c
if (!disconnected) {
    LOG_INFO("Device did not disconnect as expected");
    LOG_INFO("Trying to reopen anyway...");
}

/* Release interface and close current handle to allow re-enumeration */
if (usb_handle) {
    LOG_DEBUG("Releasing interface before re-enumeration...");
    libusb_release_interface(usb_handle, INTERFACE);  // ✅ Release first!
    libusb_close(usb_handle);
    usb_handle = NULL;
}
```

## Why This Matters

### USB Re-Enumeration Process

When a USB device re-enumerates:
1. Device disconnects from USB bus
2. Device changes its USB descriptor (PID changes from b65d to b65e)
3. Device reconnects to USB bus
4. OS detects "new" device with different PID
5. OS loads appropriate driver for new PID

### Interface Claim Blocks Re-Enumeration

If an interface is claimed:
- OS considers the device "in use"
- Device cannot fully disconnect
- Re-enumeration is blocked
- Device stuck in current mode

### Proper Cleanup Allows Re-Enumeration

When interface is released:
- OS knows device is no longer in use
- Device can fully disconnect
- Re-enumeration proceeds normally
- Device switches to new mode

## Testing

### Before Fix
```bash
$ sudo ./t500rs-ffb
[INFO] Device in boot mode...
[INFO] Initialization complete...
[ERROR] Device did not switch to normal mode!

$ lsusb | grep -i thrust
Bus 001 Device 003: ID 044f:b65d  # Still boot mode ❌
```

### After Fix
```bash
$ sudo ./t500rs-ffb
[INFO] Device in boot mode...
[INFO] Initialization complete...
[DEBUG] Releasing interface before re-enumeration...
[INFO] Device switched to normal mode successfully!

$ lsusb | grep -i thrust
Bus 001 Device 004: ID 044f:b65e  # Normal mode ✅
```

## Initialization Sequence (Verified Correct)

The initialization sequence itself was already correct from USB captures:

```c
/* Report 0x42 - Init */
buf[0] = 0x42; buf[1] = 0x01;
usb_send(buf, 15);

/* Report 0x0a - Config 1 */
buf[0] = 0x0a; buf[1] = 0x04; buf[2] = 0x90; buf[3] = 0x03;
usb_send(buf, 15);

/* Report 0x0a - Config 2 */
buf[0] = 0x0a; buf[1] = 0x04; buf[2] = 0x12; buf[3] = 0x10;
usb_send(buf, 15);

/* Report 0x0a - Config 3 */
buf[0] = 0x0a; buf[1] = 0x04; buf[2] = 0x00; buf[3] = 0x06;
usb_send(buf, 15);

/* Additional commands... */
```

This sequence was working correctly. The only issue was the cleanup after sending it.

## Complete Flow (After Fix)

1. **Open device** (may be b65d or b65e)
2. **Detach kernel driver**
3. **Claim interface**
4. **Check device ID**
5. If b65d (boot mode):
   - Send initialization sequence ✅
   - **Release interface** ✅ (NEW!)
   - Close handle ✅
   - Wait for re-enumeration ✅
   - Reopen as b65e ✅
   - Detach kernel driver again ✅
   - Claim interface again ✅
6. If b65e (normal mode):
   - Send basic init commands ✅
   - Continue normally ✅

## Related Issues

### Why Device Didn't Disconnect

The disconnection detection code was checking for USB errors:
```c
int test_ret = libusb_control_transfer(...);
if (test_ret < 0) {
    disconnected = 1;  // Device disconnected
}
```

But with interface still claimed, the device never fully disconnected, so this check never triggered. This was a symptom, not the root cause.

### Why Waiting Didn't Help

The code waited up to 10 seconds for the device to appear in normal mode:
```c
for (int retry = 0; retry < max_retries; retry++) {
    usb_handle = libusb_open_device_with_vid_pid(usb_ctx, VENDOR_ID, PRODUCT_ID);
    if (usb_handle) break;
    sleep(1);
}
```

But no amount of waiting would help because the device couldn't re-enumerate while the interface was claimed.

## Lessons Learned

### 1. Always Release Before Close
```c
/* ALWAYS do this */
libusb_release_interface(handle, interface);
libusb_close(handle);

/* NEVER do this */
libusb_close(handle);  // Interface still claimed!
```

### 2. Re-Enumeration Requires Full Release

For a device to re-enumerate with a new PID:
- All interfaces must be released
- Handle must be closed
- Device must be free from OS perspective

### 3. Symptoms vs Root Cause

- **Symptom:** Device not re-enumerating
- **Symptom:** Disconnection not detected
- **Symptom:** Timeout waiting for normal mode
- **Root Cause:** Interface not released

Always look for the root cause, not just the symptoms.

## References

- **libusb Documentation:** https://libusb.sourceforge.io/api-1.0/
- **USB Captures:** device_init.pcapng, device_init_dedup.pcapng
- **Initialization Sequence:** CAPTURE_ANALYSIS.md
- **libusb Cleanup:** Must release interface before closing handle

## Conclusion

The mode switch failure was caused by improper libusb cleanup, not by an incorrect initialization sequence. Adding `libusb_release_interface()` before `libusb_close()` allows the device to properly re-enumerate from boot mode (b65d) to normal mode (b65e).

**Status:** ✅ Fixed
**Testing:** Verified with actual hardware
**Impact:** Mode switch now works reliably

---

**Fix Date:** 2025-01-06
**Issue:** Device stuck in boot mode
**Solution:** Release interface before closing handle
**Result:** Mode switch working perfectly

