# T500RS Driver - CRITICAL ISSUES FOUND

**Date:** 2025-10-14  
**Status:** ⚠️ **SYSTEM HANG ISSUE - DO NOT USE CURRENT DRIVER**

---

## CRITICAL PROBLEM

**User Report:** "The last time I triggered a constant force, the PC hanged and I had to hardly reboot it"

**Root Cause Analysis:**

### Issue #1: Wrong HID API Usage

**Current Implementation (WRONG):**
```c
ret = hid_hw_raw_request(t500rs->hdev, T500RS_FFB_REPORT_ID,
                         send_buffer, len,
                         HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
```

**What Working Modules Do (T300RS, T248, TX, TSXW):**
```c
// 1. Get HID report and field from device
report_list = &hdev->report_enum[HID_OUTPUT_REPORT].report_list;
report = list_entry(report_list->next, struct hid_report, list);
ff_field = report->field[0];

// 2. Copy data to field
for (i = 0; i < len; ++i)
    ff_field->value[i] = send_buffer[i];

// 3. Send via hid_hw_request
hid_hw_request(hdev, report, HID_REQ_SET_REPORT);
```

**Problem:** We're using `hid_hw_raw_request()` with FEATURE reports, but all working modules use `hid_hw_request()` with OUTPUT reports via the HID field mechanism.

### Issue #2: No Locking/Synchronization

**Current Implementation:**
- No mutex or spinlock protection
- Multiple threads could call `t500rs_send_buf()` simultaneously
- Buffer corruption possible
- Race conditions likely

**What Working Modules Do:**
- T300RS has buffer protection (though commented as needing improvement)
- Separate buffers for range setting to avoid conflicts

### Issue #3: Unvalidated Buffer Size

**Current Implementation:**
- Always sends 11560 bytes
- No check if device actually supports this size
- No verification of HID descriptor

**What Working Modules Do:**
```c
if (len > t300rs->buffer_length)
    return -EINVAL;
```

### Issue #4: Report ID Mismatch

**Current Implementation:**
```c
#define T500RS_FFB_REPORT_ID 0xEF  // Low byte of 0xCFEF
```

**Problem:** We're using 0xEF as the report ID, but:
- Windows driver uses 0xCFEF (53231)
- We truncated to 0xEF (239) to fit in u8
- This may not match the actual HID descriptor

---

## ARCHITECTURAL MISMATCH

### T500RS Protocol (from Ghidra analysis):
- Uses **HID FEATURE reports** (11560 bytes)
- Report ID: 0xCFEF
- Windows uses `HidD_SetFeature()`

### Other Thrustmaster Wheels:
- Use **HID OUTPUT reports** (63-256 bytes)
- Use `hid_hw_request()` with field mechanism
- Much smaller buffers

### The Conflict:
Our implementation tries to use `hid_hw_raw_request()` like the Ghidra guide suggests, but:
1. All other modules use `hid_hw_request()` with fields
2. We may be sending data the device doesn't understand
3. The kernel may be blocking or hanging on the large transfer

---

## WHY IT MIGHT HANG

### Hypothesis 1: USB Transfer Timeout
- 11560 bytes is HUGE for USB Full-Speed (12 Mbps)
- Transfer time: ~7.7ms minimum
- If device doesn't ACK, kernel may block indefinitely
- No timeout handling in our code

### Hypothesis 2: Wrong Report Type
- Device expects OUTPUT report
- We're sending FEATURE report
- Device ignores it or NAKs it
- Kernel waits forever for response

### Hypothesis 3: Invalid Report ID
- Device doesn't have report ID 0xEF
- HID core tries to find it in descriptor
- Fails and blocks

### Hypothesis 4: Buffer Overflow
- Device HID descriptor says smaller size
- We send 11560 bytes anyway
- USB stack or device firmware crashes

---

## IMMEDIATE ACTIONS REQUIRED

### DO NOT:
- ❌ Load the current driver
- ❌ Test force feedback with current code
- ❌ Use on production system

### MUST DO:
1. **Dump HID descriptor** to see actual report IDs and sizes
2. **Check if T500RS has OUTPUT or FEATURE reports**
3. **Rewrite to use correct HID API** (likely `hid_hw_request()` with fields)
4. **Add proper locking** (spinlock or mutex)
5. **Add buffer size validation**
6. **Test with minimal data first** (not full 11560 bytes)

---

## DIAGNOSTIC STEPS

### Step 1: Examine HID Descriptor
```bash
# Get detailed HID info
sudo lsusb -v -d 044f:b65e | grep -A50 "HID Device Descriptor"

# Or use hidraw
sudo cat /sys/kernel/debug/hid/0003:044F:B65E.*/rdesc
```

### Step 2: Check Available Reports
```bash
# List HID reports
ls -l /sys/class/hidraw/hidraw*/device/
cat /sys/class/hidraw/hidraw*/device/report_descriptor | hexdump -C
```

### Step 3: Test with Minimal Data
Before sending 11560 bytes, test with small packets:
```c
// Test 1: Send 4 bytes only
u8 test_buf[4] = {0x03, 0x0e, 0x01, 0x40};
ret = hid_hw_output_report(hdev, test_buf, 4);

// Test 2: Try different report IDs
// Test 3: Try OUTPUT instead of FEATURE
```

---

## PROPOSED FIX STRATEGY

### Option A: Follow T300RS Pattern (RECOMMENDED)
1. Find OUTPUT report in HID descriptor
2. Get `ff_field` from report
3. Copy data to `ff_field->value[]`
4. Call `hid_hw_request()`
5. Add spinlock protection

### Option B: Fix Raw Request Method
1. Verify correct report ID from descriptor
2. Add timeout handling
3. Start with small transfers
4. Gradually increase to 11560 bytes
5. Add extensive error checking

### Option C: Hybrid Approach
1. Use OUTPUT reports for small commands (play/stop)
2. Use FEATURE reports only for effect upload
3. Separate code paths for each

---

## REFERENCE: Working T300RS Send Function

```c
int t300rs_send_buf(struct t300rs_device_entry *t300rs, u8 *send_buffer, size_t len)
{
    int i;
    /* check that send_buffer fits into our report */
    if (len > t300rs->buffer_length)
        return -EINVAL;

    /* fill with actual data */
    for (i = 0; i < len; ++i)
        t300rs->ff_field->value[i] = send_buffer[i];

    /* fill the rest with zeroes */
    for (i = len; i < t300rs->buffer_length; ++i)
        t300rs->ff_field->value[i] = 0;

    hid_hw_request(t300rs->hdev, t300rs->report, HID_REQ_SET_REPORT);
    return 0;
}
```

**Key Differences from Our Code:**
1. Uses `ff_field->value[]` not raw buffer
2. Uses `hid_hw_request()` not `hid_hw_raw_request()`
3. Validates buffer length
4. Zero-fills unused space
5. Uses OUTPUT report, not FEATURE report

---

## NEXT STEPS

1. **STOP** using current driver immediately
2. **INVESTIGATE** HID descriptor to find correct report type/ID
3. **REWRITE** send function to match working modules
4. **TEST** incrementally with small data first
5. **ADD** proper error handling and locking
6. **VALIDATE** system stability before real testing

---

## SAFETY REMINDER

**This is a kernel driver.** Bugs can:
- Crash the entire system
- Corrupt USB stack
- Damage hardware (unlikely but possible)
- Require hard reboot

**Always:**
- Test in VM first (if possible)
- Have unsaved work backed up
- Be prepared for hard reboot
- Monitor kernel logs continuously

---

**Status: DRIVER UNSAFE - REQUIRES COMPLETE REWRITE OF SEND MECHANISM**

