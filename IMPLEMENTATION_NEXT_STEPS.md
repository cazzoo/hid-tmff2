# T500RS Implementation - Next Steps

## What We Discovered

From the Windows USB capture, we found that:

1. **T500RS uses INTERRUPT transfers, NOT SET_REPORT**
   - Endpoint: 0x01 (OUT)
   - This is why we got error -32 (EPIPE)
   - We were using `hid_hw_raw_request()` which uses control transfers
   - We need to use `usb_interrupt_msg()` or URB submission

2. **Protocol Structure** (see T500RS_PROTOCOL.md for details)
   - Report 0x41 (4 bytes): Effect control (start/stop)
   - Report 0x01 (15 bytes): Effect parameters
   - Report 0x02 (9 bytes): Additional parameters
   - Report 0x04 (8 bytes): More parameters
   - Report 0x42: Initialization
   - Report 0x0a: Configuration

## Required Changes

### 1. Change Transport Method

**Current (WRONG):**
```c
hid_hw_raw_request(hdev, report_id, buf, len, HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);
```

**Needed (CORRECT):**
```c
usb_interrupt_msg(usbdev, usb_sndintpipe(usbdev, ep_out), buf, len, &actual, timeout);
```

Or use URB submission for better performance.

### 2. Find INTERRUPT Endpoint

Need to find the OUT INTERRUPT endpoint (should be 0x01):

```c
struct usb_host_interface *interface;
struct usb_endpoint_descriptor *ep;

interface = usbdev->actconfig->interface[0]->cur_altsetting;
for (i = 0; i < interface->desc.bNumEndpoints; i++) {
    ep = &interface->endpoint[i].desc;
    if (usb_endpoint_is_int_out(ep)) {
        ep_out = ep->bEndpointAddress;
        break;
    }
}
```

### 3. Implement Effect Control (Report 0x41)

Simple 4-byte command:
```c
buf[0] = 0x41;           // Report ID
buf[1] = effect_id;      // 0-15
buf[2] = 0x41;           // Start (or 0x00 for stop)
buf[3] = 0x01;           // Constant

usb_interrupt_msg(usbdev, usb_sndintpipe(usbdev, ep_out), buf, 4, &actual, 1000);
```

### 4. Implement Effect Parameters (Report 0x01)

15-byte command with effect parameters:
```c
buf[0] = 0x01;           // Report ID
buf[1] = effect_id;      // Effect ID
buf[2] = effect_type;    // Type/flags
// bytes 3-14: parameters (need to decode from capture)

usb_interrupt_msg(usbdev, usb_sndintpipe(usbdev, ep_out), buf, 15, &actual, 1000);
```

## Implementation Strategy

### Phase 1: Basic INTERRUPT Support (1-2 hours)

1. **Update structure** to store USB device and endpoint
2. **Find INTERRUPT endpoint** during initialization
3. **Replace send function** to use `usb_interrupt_msg()`
4. **Test** that commands are sent without error

### Phase 2: Effect Control (30 minutes)

1. **Implement Report 0x41** for start/stop
2. **Test** with simple on/off commands
3. **Verify** wheel responds

### Phase 3: Effect Parameters (1-2 hours)

1. **Decode parameter structure** from capture
2. **Map Linux FF parameters** to T500RS format
3. **Implement effect upload**
4. **Test** different effect types

### Phase 4: Polish (1 hour)

1. **Add initialization** sequence (Report 0x42, 0x0a)
2. **Handle all effect types**
3. **Error handling**
4. **Testing and validation**

## Code Changes Needed

### File: `src/tmt500rs/hid-tmt500rs-simple.h`

```c
struct t500rs_simple_entry {
    struct hid_device *hdev;
    struct input_dev *input_dev;
    struct usb_device *usbdev;
    
    /* INTERRUPT endpoint */
    int ep_out;
    
    /* Send buffer */
    u8 *send_buffer;
    
    int (*open)(struct input_dev *dev);
    void (*close)(struct input_dev *dev);
};
```

### File: `src/tmt500rs/hid-tmt500rs-simple.c`

**New send function:**
```c
static int t500rs_send_interrupt(struct t500rs_simple_entry *t500rs, 
                                  u8 *buf, size_t len)
{
    int ret, actual;
    
    ret = usb_interrupt_msg(t500rs->usbdev,
                           usb_sndintpipe(t500rs->usbdev, t500rs->ep_out),
                           buf, len, &actual, 1000);
    
    if (ret < 0) {
        hid_err(t500rs->hdev, "interrupt transfer failed: %d\n", ret);
        return ret;
    }
    
    return 0;
}
```

**Effect control:**
```c
static int t500rs_play_effect(void *data, struct tmff2_effect_state *state)
{
    struct t500rs_simple_entry *t500rs = data;
    u8 buf[4];
    
    buf[0] = 0x41;                    // Report ID
    buf[1] = state->effect.id;        // Effect ID
    buf[2] = 0x41;                    // Start command
    buf[3] = 0x01;                    // Constant
    
    return t500rs_send_interrupt(t500rs, buf, 4);
}

static int t500rs_stop_effect(void *data, struct tmff2_effect_state *state)
{
    struct t500rs_simple_entry *t500rs = data;
    u8 buf[4];
    
    buf[0] = 0x41;                    // Report ID
    buf[1] = state->effect.id;        // Effect ID
    buf[2] = 0x00;                    // Stop command
    buf[3] = 0x01;                    // Constant
    
    return t500rs_send_interrupt(t500rs, buf, 4);
}
```

**Find endpoint:**
```c
static int t500rs_find_endpoint(struct t500rs_simple_entry *t500rs)
{
    struct usb_host_interface *interface;
    struct usb_endpoint_descriptor *ep;
    int i;
    
    interface = t500rs->usbdev->actconfig->interface[0]->cur_altsetting;
    
    for (i = 0; i < interface->desc.bNumEndpoints; i++) {
        ep = &interface->endpoint[i].desc;
        if (usb_endpoint_is_int_out(ep)) {
            t500rs->ep_out = ep->bEndpointAddress;
            hid_info(t500rs->hdev, "Found INTERRUPT OUT endpoint: 0x%02x\n",
                     t500rs->ep_out);
            return 0;
        }
    }
    
    hid_err(t500rs->hdev, "No INTERRUPT OUT endpoint found\n");
    return -ENODEV;
}
```

## Testing Plan

1. **Build and load** modified driver
2. **Check endpoint** detection in dmesg
3. **Test effect start/stop** with fftest
4. **Monitor** for errors
5. **Feel the wheel** - should respond!

## Time Estimate

- **Phase 1**: 1-2 hours (change to INTERRUPT)
- **Phase 2**: 30 minutes (basic control)
- **Phase 3**: 1-2 hours (parameters)
- **Phase 4**: 1 hour (polish)

**Total**: 3-5 hours to working force feedback

## Success Criteria

- ✅ No error -32 (EPIPE)
- ✅ Commands sent successfully
- ✅ Wheel responds to force feedback
- ✅ Effects can be started/stopped
- ✅ Different force levels work

## Next Action

Start with Phase 1: Implement INTERRUPT transfer support.

Would you like me to implement these changes now?

