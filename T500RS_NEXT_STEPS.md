# T500RS Driver - Next Steps (USB INTERRUPT Implementation)

**Date:** 2025-10-14  
**Status:** 🎯 **Ready to implement real force feedback!**

---

## Critical Discovery

From `T500RS_PROTOCOL.md` (actual Windows USB captures):

**The T500RS uses USB INTERRUPT transfers to endpoint 0x01, NOT HID reports!**

This is why:
- ❌ `hid_hw_raw_request()` hung the system
- ❌ `hid_hw_request()` with HID fields won't work either
- ✅ Must use `usb_interrupt_msg()` like TX/TSXW modules do

---

## The Real Protocol

### Report 0x41: Effect Control (4 bytes) - MOST IMPORTANT
```c
struct t500rs_effect_control {
    uint8_t report_id;    // 0x41
    uint8_t effect_id;    // 0-15
    uint8_t command;      // 0x00 = Stop, 0x41 = Start
    uint8_t constant;     // Always 0x01
} __attribute__((packed));
```

**Examples:**
- `41 00 00 01` - Stop effect 0
- `41 01 41 01` - Start effect 1
- `41 02 41 01` - Start effect 2

### Report 0x01: Effect Upload (15 bytes)
```c
struct t500rs_effect_upload {
    uint8_t report_id;    // 0x01
    uint8_t effect_id;    // 0-15
    uint8_t flags;        // Effect type/flags
    uint16_t param1;      // Little-endian
    uint16_t param2;
    uint16_t param3;
    uint16_t param4;
    uint16_t param5;
    uint16_t param6;
} __attribute__((packed));
```

### Report 0x0a: Configuration (14 bytes)
```c
struct t500rs_config {
    uint8_t report_id;    // 0x0a
    uint8_t command;      // 0x04 = config
    uint16_t param1;      // e.g., 0x0390 = 912 (range?)
    uint8_t padding[10];  // Usually zeros
} __attribute__((packed));
```

---

## Implementation Strategy

### Phase 1: USB INTERRUPT Infrastructure (NEXT)

**Goal:** Replace HID API with USB INTERRUPT transfers

**Changes needed:**
1. Find USB INTERRUPT OUT endpoint (0x01)
2. Replace `t500rs_send_buf()` with `usb_interrupt_msg()`
3. Test basic connectivity

**Code pattern (from TX/TSXW):**
```c
// Find endpoint
struct usb_interface *usbif = to_usb_interface(hdev->dev.parent);
struct usb_host_endpoint *ep = &usbif->cur_altsetting->endpoint[1];
int b_ep = ep->desc.bEndpointAddress;  // Should be 0x01

// Send via INTERRUPT
ret = usb_interrupt_msg(usbdev,
                        usb_sndintpipe(usbdev, b_ep),
                        send_buffer, len,
                        &transferred,
                        USB_CTRL_SET_TIMEOUT);
```

### Phase 2: Basic Effect Control (SIMPLE)

**Goal:** Implement start/stop with Report 0x41

**Functions to implement:**
```c
int t500rs_play_effect() {
    uint8_t cmd[4] = {0x41, effect_id, 0x41, 0x01};
    return t500rs_send_interrupt(t500rs, cmd, 4);
}

int t500rs_stop_effect() {
    uint8_t cmd[4] = {0x41, effect_id, 0x00, 0x01};
    return t500rs_send_interrupt(t500rs, cmd, 4);
}
```

**Test:** Should be able to start/stop effects (even if upload doesn't work yet)

### Phase 3: Effect Upload (COMPLEX)

**Goal:** Implement Report 0x01 for effect parameters

**Need to decode:**
- What do param1-param6 mean for each effect type?
- How to map Linux FF parameters to T500RS format?
- Which effects need Report 0x02 and 0x04 as well?

**Approach:**
1. Start with constant force (simplest)
2. Analyze captured packets for constant force
3. Map magnitude/direction to parameters
4. Test and iterate

### Phase 4: Configuration (MEDIUM)

**Goal:** Implement gain, range, autocenter

**Using Report 0x0a:**
```c
int t500rs_set_range(uint16_t degrees) {
    uint8_t cmd[14] = {0x0a, 0x04, 0, 0, 0, ...};
    *(uint16_t *)(cmd + 2) = cpu_to_le16(degrees);
    return t500rs_send_interrupt(t500rs, cmd, 14);
}
```

---

## Immediate Next Steps

### 1. Create USB INTERRUPT Version

**File:** `src/tmt500rs/hid-tmt500rs-interrupt.c`

**Key changes from current version:**
- Remove HID report/field references
- Add USB endpoint discovery
- Implement `t500rs_send_interrupt()` using `usb_interrupt_msg()`
- Keep placeholder functions for now

### 2. Test USB Communication

**Goal:** Verify INTERRUPT transfers work without hanging

**Test:**
```c
// Send dummy packet to verify endpoint works
uint8_t test[4] = {0x41, 0x00, 0x00, 0x01};
ret = usb_interrupt_msg(...);
// Should return success without hanging
```

### 3. Implement Report 0x41 (Start/Stop)

**Goal:** Get basic effect control working

**Test with fftest:**
- Upload effect (placeholder - does nothing)
- **Play effect** - Send `41 [id] 41 01`
- **Stop effect** - Send `41 [id] 00 01`
- Should see wheel respond!

### 4. Decode Effect Parameters

**Goal:** Figure out Report 0x01 structure

**Method:**
- Analyze captured packets from `T500RS_PROTOCOL.md`
- Compare different effect types
- Map to Linux FF effect structures
- Implement constant force first

---

## Code Structure

### New Device Structure
```c
struct t500rs_device_entry {
    struct hid_device *hdev;
    struct usb_device *usbdev;
    struct usb_interface *usbif;
    
    int interrupt_out_endpoint;  // 0x01
    
    u8 *send_buffer;
    size_t buffer_length;  // 32 bytes (endpoint max)
    
    // No HID report/field needed!
};
```

### Send Function
```c
static int t500rs_send_interrupt(struct t500rs_device_entry *t500rs,
                                  u8 *data, size_t len)
{
    int ret, transferred;
    
    ret = usb_interrupt_msg(t500rs->usbdev,
                            usb_sndintpipe(t500rs->usbdev, 
                                          t500rs->interrupt_out_endpoint),
                            data, len,
                            &transferred,
                            USB_CTRL_SET_TIMEOUT);
    
    if (ret < 0) {
        hid_err(t500rs->hdev, "INTERRUPT transfer failed: %d\n", ret);
        return ret;
    }
    
    return 0;
}
```

---

## Testing Plan

### Test 1: Endpoint Discovery
```bash
# Load driver
sudo insmod ./hid_tmff_new.ko

# Check logs
sudo dmesg | grep -i "endpoint\|interrupt"

# Expected: "Found INTERRUPT OUT endpoint: 0x01"
```

### Test 2: Dummy Transfer
```bash
# Driver sends test packet on init
sudo dmesg | grep -i "test\|transfer"

# Expected: "Test INTERRUPT transfer: success"
```

### Test 3: Effect Control
```bash
# Run fftest
sudo fftest /dev/input/event260

# Try to play effect
# Check logs
sudo dmesg | grep -i "play\|stop"

# Expected: "Sent play command: 41 01 41 01"
# Expected: Wheel should respond!
```

---

## Success Criteria

### Phase 1 Success:
- ✅ Endpoint discovered
- ✅ INTERRUPT transfers work
- ✅ No system hangs
- ✅ Test packet sent successfully

### Phase 2 Success:
- ✅ Play command sent
- ✅ Stop command sent
- ✅ **Wheel responds to commands!**
- ✅ No crashes or hangs

### Phase 3 Success:
- ✅ Constant force works
- ✅ Spring effect works
- ✅ Effect parameters correct
- ✅ Smooth force feedback

---

## References

- **Protocol:** `T500RS_PROTOCOL.md` - USB capture analysis
- **Pattern:** `src/tmtx/hid-tmtx.c` - USB INTERRUPT example
- **Pattern:** `src/tmtsxw/hid-tmtsxw.c` - Another INTERRUPT example
- **Endpoint:** USB spec says endpoint 0x01 OUT, INTERRUPT, 32 bytes max

---

## Estimated Timeline

- **Phase 1 (USB INTERRUPT):** 1-2 hours coding + testing
- **Phase 2 (Start/Stop):** 30 minutes coding + testing
- **Phase 3 (Effect Upload):** 2-4 hours (parameter decoding)
- **Phase 4 (Configuration):** 1 hour

**Total:** ~5-8 hours to working force feedback!

---

**Status: Ready to implement USB INTERRUPT version!**

Would you like me to create the USB INTERRUPT implementation now?

