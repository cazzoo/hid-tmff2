# T500RS HID Descriptor Analysis

**Date:** 2025-10-14

---

## HID Report Descriptor (130 bytes)

```
00000000  05 01 09 04 a1 01 09 01  a1 00 85 07 09 30 15 00  |.............0..|
00000010  27 ff ff 00 00 35 00 47  ff ff 00 00 75 10 95 01  |'....5.G....u...|
00000020  81 02 09 31 26 ff 03 46  ff 03 81 02 09 35 81 02  |...1&..F.....5..|
00000030  09 36 81 02 81 03 05 09  19 01 29 0d 25 01 45 01  |.6........).%.E.|
00000040  75 01 95 0d 81 02 75 0b  95 01 81 03 05 01 09 39  |u.....u........9|
00000050  25 07 46 3b 01 55 00 65  14 75 04 81 42 65 00 81  |%.F;.U.e.u..Be..|
00000060  03 85 0a 06 00 ff 09 0a  75 08 95 0e 26 ff 00 46  |........u...&..F|
00000070  ff 00 91 02 85 02 09 02  81 02 09 14 85 14 81 02  |................|
00000080  c0 c0                                             |..|
```

---

## Parsed Report Structure

### Report ID 0x07 (INPUT - Main wheel state)
- **Type:** INPUT (81 02)
- **Size:** Variable (axes + buttons)
- **Contents:**
  - X axis (steering): 16-bit (75 10 95 01)
  - Y axis (throttle): 16-bit, max 1023 (26 ff 03)
  - Z axis (brake): 16-bit
  - Rz axis (clutch): 16-bit
  - 13 buttons (75 01 95 0d)
  - Hat switch (09 39, 4-bit)

### Report ID 0x0A (OUTPUT - Force Feedback!)
- **Type:** OUTPUT (91 02) ← **THIS IS KEY!**
- **Offset:** 0x60 in descriptor
- **Bytes:** `85 0a 06 00 ff 09 0a 75 08 95 0e 26 ff 00 46 ff 00 91 02`
- **Decoded:**
  - `85 0a` = Report ID 10 (0x0A)
  - `06 00 ff` = Usage Page: Vendor-Specific (0xFF00)
  - `09 0a` = Usage: 0x0A
  - `75 08` = Report Size: 8 bits
  - `95 0e` = Report Count: **14 bytes**
  - `26 ff 00` = Logical Maximum: 255
  - `46 ff 00` = Physical Maximum: 255
  - `91 02` = **OUTPUT** (Data, Variable, Absolute)

### Report ID 0x02 (INPUT)
- **Type:** INPUT (81 02)
- **Size:** 14 bytes (same as output)
- **Purpose:** Unknown (possibly feedback from device)

### Report ID 0x14 (INPUT)
- **Type:** INPUT (81 02)
- **Size:** 14 bytes
- **Purpose:** Unknown

---

## CRITICAL FINDINGS

### 1. NO 11560-byte FEATURE Report!
**Expected (from Ghidra):** Feature Report ID 0xCFEF, 11560 bytes  
**Actual (from HID descriptor):** **DOES NOT EXIST**

The HID descriptor is only 130 bytes total and defines NO feature reports at all!

### 2. OUTPUT Report is Only 14 Bytes!
**Expected (from Ghidra):** 11560-byte buffer  
**Actual (from HID descriptor):** **14 bytes** (Report ID 0x0A)

### 3. Vendor-Specific Usage Page
The OUTPUT report uses Usage Page 0xFF00 (Vendor-Specific), which means it's a custom protocol not standard HID.

---

## Hypothesis: Windows Driver Uses Different Method

### Possibility 1: Raw USB Control Transfers
The Windows driver might bypass HID entirely and use:
```c
// Windows API
DeviceIoControl(device, IOCTL_HID_SET_FEATURE, buffer, 11560, ...);

// Or raw USB:
WinUsb_ControlTransfer(device, setup_packet, buffer, 11560, ...);
```

### Possibility 2: Multiple Small Packets
Instead of one 11560-byte transfer, the driver might send:
- Multiple 14-byte OUTPUT reports (Report ID 0x0A)
- Sequenced commands to build up the effect

### Possibility 3: Undocumented Feature Report
The device might accept feature reports that aren't in the HID descriptor:
- Report ID 0xEF (or 0xCF, 0xEF as two bytes)
- Accessed via raw USB control transfer
- Not advertised in HID descriptor

---

## Why Our Driver Hangs

### Current Implementation:
```c
ret = hid_hw_raw_request(t500rs->hdev, T500RS_FFB_REPORT_ID,  // 0xEF
                         send_buffer, 11560,  // 11560 bytes
                         HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
```

### Problems:
1. **Report ID 0xEF doesn't exist** in HID descriptor
2. **No FEATURE reports defined** at all
3. **Trying to send 11560 bytes** when max is 14 bytes
4. **HID core blocks** waiting for a report that doesn't exist

### Result:
- Kernel HID layer tries to find Report ID 0xEF
- Fails to find it in parsed descriptor
- Either returns error OR blocks indefinitely
- System hangs because kernel thread is stuck

---

## Correct Implementation Strategy

### Option A: Use OUTPUT Report (RECOMMENDED)
Follow the pattern of T300RS/T248/TX/TSXW:

```c
// 1. Get OUTPUT report (ID 0x0A)
report_list = &hdev->report_enum[HID_OUTPUT_REPORT].report_list;
report = list_entry(report_list->next, struct hid_report, list);
ff_field = report->field[0];

// 2. Send 14-byte commands
for (i = 0; i < 14; ++i)
    ff_field->value[i] = send_buffer[i];

// 3. Submit via hid_hw_request
hid_hw_request(hdev, report, HID_REQ_SET_REPORT);
```

**Advantages:**
- Uses actual HID descriptor
- Matches working wheel implementations
- Won't hang system
- Proper HID layer integration

**Disadvantages:**
- Only 14 bytes per transfer
- Need to figure out command protocol for 14-byte packets
- May need multiple transfers for complex effects

### Option B: Raw USB Interrupt OUT
Use the USB endpoint directly:

```c
// Endpoint 0x01 OUT, Interrupt, 32 bytes max
ret = usb_interrupt_msg(usbdev,
                        usb_sndintpipe(usbdev, 0x01),
                        send_buffer, len,
                        &transferred,
                        USB_CTRL_SET_TIMEOUT);
```

**Advantages:**
- Bypasses HID layer
- Can send larger packets (up to 32 bytes per endpoint spec)
- Direct hardware access

**Disadvantages:**
- More complex
- Need to manage USB transfers manually
- Less integration with HID subsystem

### Option C: Raw USB Control Transfer (Feature Report)
Try to send undocumented feature report:

```c
ret = usb_control_msg(usbdev,
                      usb_sndctrlpipe(usbdev, 0),
                      HID_REQ_SET_REPORT,
                      USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                      (HID_REPORT_TYPE_FEATURE << 8) | report_id,
                      0,  // interface 0
                      buffer, len,
                      USB_CTRL_SET_TIMEOUT);
```

**Advantages:**
- Might match Windows driver behavior
- Could access undocumented reports

**Disadvantages:**
- Speculative - may not work
- Still limited by USB packet sizes
- Requires chunking for 11560 bytes

---

## Recommended Next Steps

### Immediate (Safe Testing):
1. **Rewrite to use OUTPUT Report ID 0x0A (14 bytes)**
2. **Follow T300RS pattern** with `hid_hw_request()`
3. **Test with minimal commands** first
4. **Monitor for hangs** - should be safe now

### Investigation:
1. **Capture Windows USB traffic** to see actual protocol
2. **Test different report IDs** (0xEF, 0xCF, etc.)
3. **Try raw USB control transfers** for feature reports
4. **Analyze if 11560 bytes is chunked** into smaller packets

### Long-term:
1. **Reverse engineer actual command protocol** for 14-byte packets
2. **Determine if effects need multiple packets** or single packet
3. **Optimize for performance** once working

---

## Conclusion

**The 11560-byte feature report from Ghidra analysis does NOT exist in the actual HID descriptor!**

We must either:
1. Use the 14-byte OUTPUT report (ID 0x0A) - **SAFEST**
2. Use raw USB transfers to bypass HID - **MORE COMPLEX**
3. Find undocumented feature report access method - **SPECULATIVE**

**Recommendation:** Start with Option A (14-byte OUTPUT report) using the proven T300RS pattern. This will be safe and won't hang the system.

