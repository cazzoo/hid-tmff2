# T500RS Mode Switch Fix - Summary

## Problem
After restarting the computer and plugging in the T500RS wheel, the device remained stuck in boot mode (PID: 0xb65d) instead of switching to normal mode (PID: 0xb65e). The userspace driver was unable to complete the mode switch.

## Root Cause
The userspace driver (`t500rs-ffb.c`) was missing the **USB control requests** that trigger the actual mode switch. It was only sending interrupt transfers for initialization, but the T500RS (like other Thrustmaster wheels) requires specific USB control requests to switch from boot mode to normal mode.

## Solution
Added the missing USB control requests to the initialization sequence:

### 1. Model ID Query (USB Control Request)
```c
libusb_control_transfer(usb_handle,
    0xc1,  /* bmRequestType: IN, vendor, device */
    73,    /* bRequest */
    0,     /* wValue */
    0,     /* wIndex */
    model_response,
    16,    /* wLength */
    5000); /* timeout ms */
```

### 2. Mode Switch Command (USB Control Request)
```c
libusb_control_transfer(usb_handle,
    0x41,  /* bmRequestType: OUT, vendor, device */
    83,    /* bRequest */
    0x0002,/* wValue - T500RS switch value */
    0,     /* wIndex */
    NULL,  /* no data */
    0,     /* wLength */
    5000); /* timeout ms */
```

### 3. Removed USB Reset
The problematic `libusb_reset_device()` call was removed as it:
- Invalidated the USB device handle
- Caused `LIBUSB_ERROR_BUSY` when trying to reclaim the interface
- Was unnecessary since the control requests trigger the mode switch directly

## Technical Details

### Mode Switch Sequence
Based on the kernel driver `hid-tminit`:

1. **Interrupt Transfers** (initialization):
   - Report 0x42 (init)
   - Reports 0x0a (config 1-3)
   - Reports 0x40 (various commands)
   - Report 0x42, 0x4e, 0x56 (queries and settings)

2. **USB Control Request** (model query):
   - bRequestType: 0xc1
   - bRequest: 73
   - Response: Model ID (0x49 for T500RS)

3. **USB Control Request** (mode switch):
   - bRequestType: 0x41
   - bRequest: 83
   - wValue: 0x0002 (T500RS specific)
   - Device disconnects and re-enumerates with new PID

4. **Re-enumeration**:
   - Device reconnects as 044f:b65e
   - Driver detects and claims the new device
   - Continues with normal operation

## Test Results

### Before Fix
```
[INFO] Device in boot mode, attempting mode switch...
[INFO] Attempting USB reset...
[ERROR] Failed to claim interface: LIBUSB_ERROR_BUSY
```
Device remained at **044f:b65d** (boot mode)

### After Fix
```
[INFO] Sending USB control requests for mode switch...
[INFO] Model ID response received (16 bytes)
[INFO] Response type: 0x0049
[INFO] Sending mode switch control request (value=0x0002)...
[INFO] Device disconnected during mode switch (expected behavior)
[INFO] Device switched to normal mode successfully!
```
Device successfully switched to **044f:b65e** (normal mode)

## Files Modified
- `userspace/t500rs-ffb.c`:
  - Added USB control requests in `t500rs_initialize()` function
  - Simplified boot mode handling in `main()` function
  - Removed problematic USB reset logic

## References
- Kernel driver: `deps/hid-tminit/tminit.c`
- T500RS switch value: 0x0002 (defined in tm_wheels_infos array)
- USB control request format: Based on kernel driver's `model_request` and `change_request`

## How to Test
1. Unplug the wheel
2. Wait 5 seconds
3. Plug it back in
4. Run: `sudo ./t500rs-ffb`
5. Verify: `lsusb | grep -i thrust` shows **044f:b65e** (normal mode)

## Status
✅ **FIXED** - Mode switch now works reliably on system restart
