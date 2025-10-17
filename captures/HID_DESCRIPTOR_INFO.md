# T500RS HID Descriptor Information

**Captured:** 2025-10-14  
**Device:** Thrustmaster T500RS (PID 0xB65E - Normal Mode)  
**Method:** `lsusb -vvv -d 044f:b65e`

---

## USB Device Descriptor

```
Bus 002 Device 003: ID 044f:b65e ThrustMaster, Inc. TRS Racing wheel
Negotiated speed: Full Speed (12Mbps)

Device Descriptor:
  bLength                18
  bDescriptorType         1
  bcdUSB               2.00
  bDeviceClass            0 [unknown]
  bDeviceSubClass         0 [unknown]
  bDeviceProtocol         0 
  bMaxPacketSize0         8
  idVendor           0x044f ThrustMaster, Inc.
  idProduct          0xb65e TRS Racing wheel
  bcdDevice            1.00
  iManufacturer           1 Thrustmaster
  iProduct                2 TRS Racing wheel
  iSerial                 0 
  bNumConfigurations      1
```

---

## USB Configuration Descriptor

```
Configuration Descriptor:
  bLength                 9
  bDescriptorType         2
  wTotalLength       0x0029  (41 bytes)
  bNumInterfaces          1
  bConfigurationValue     1
  iConfiguration          0 
  bmAttributes         0xc0
    Self Powered
  MaxPower              100mA
```

---

## USB Interface Descriptor

```
Interface Descriptor:
  bLength                 9
  bDescriptorType         4
  bInterfaceNumber        0
  bAlternateSetting       0
  bNumEndpoints           2
  bInterfaceClass         3 Human Interface Device
  bInterfaceSubClass      0 [unknown]
  bInterfaceProtocol      0 
  iInterface              0 
```

---

## HID Device Descriptor

```
HID Device Descriptor:
  bLength                 9
  bDescriptorType        33  (0x21 - HID Descriptor)
  bcdHID               1.11
  bCountryCode            0 Not supported
  bNumDescriptors         1
  bDescriptorType        34  (0x22 - HID Report Descriptor)
  wDescriptorLength     130  (0x82 bytes)
  
  Report Descriptors: 
    ** UNAVAILABLE VIA lsusb **
    (Requires hidraw access or USB capture)
```

**Note:** The HID Report Descriptor is 130 bytes but couldn't be read via lsusb. It can be extracted using:
- hidraw device access (`/dev/hidrawX`)
- USB packet capture during device enumeration
- Windows INF file analysis
- Kernel HID parser output

---

## USB Endpoints

### Endpoint 1: Input (Device → Host)
```
Endpoint Descriptor:
  bLength                 7
  bDescriptorType         5
  bEndpointAddress     0x82  EP 2 IN
  bmAttributes            3
    Transfer Type            Interrupt
    Synch Type               None
    Usage Type               Data
  wMaxPacketSize     0x0010  (16 bytes, 1x 16 bytes)
  bInterval               2  (2ms polling - 500 Hz)
```

**Purpose:** HID Input Reports (wheel position, pedals, buttons)  
**Actual Usage:** Polled at ~1000 Hz (1ms) by drivers

### Endpoint 2: Output (Host → Device)
```
Endpoint Descriptor:
  bLength                 7
  bDescriptorType         5
  bEndpointAddress     0x01  EP 1 OUT
  bmAttributes            3
    Transfer Type            Interrupt
    Synch Type               None
    Usage Type               Data
  wMaxPacketSize     0x0020  (32 bytes, 1x 32 bytes)
  bInterval               4  (4ms polling)
```

**Purpose:** HID Output/Feature Reports (force feedback commands)  
**Actual Usage:** Used by userspace driver for effect uploads

---

## Key Findings

### 1. Device Information
- **Vendor ID:** 0x044F (ThrustMaster, Inc.)
- **Product ID:** 0xB65E (Normal mode - after mode switch)
- **Boot Mode PID:** 0xB65D (before mode switch)
- **USB Speed:** Full Speed (12 Mbps)
- **Power:** Self-powered, draws 100mA from USB

### 2. HID Classification
- **Class:** HID (Human Interface Device)
- **SubClass:** 0 (No Boot Interface)
- **Protocol:** 0 (No specific protocol)
- **HID Version:** 1.11
- **Report Descriptor Size:** 130 bytes

### 3. Endpoint Configuration

**Input Endpoint (0x82):**
- Max packet size: 16 bytes
- Polling interval: 2ms (but driver polls at 1ms)
- Direction: IN (Device to Host)
- Usage: Wheel state, button state, pedal values

**Output Endpoint (0x01):**
- Max packet size: 32 bytes
- Polling interval: 4ms
- Direction: OUT (Host to Device)
- Usage: Force feedback commands, device configuration

### 4. Comparison with Documentation

| Feature | USB Descriptor | MASTER_GUIDE | Userspace Driver | Match? |
|---------|----------------|--------------|------------------|--------|
| Input endpoint | 0x82 (EP 2 IN) | Not specified | 0x82 (EP_IN) | ✅ |
| Output endpoint | 0x01 (EP 1 OUT) | Not specified | 0x01 (EP_OUT) | ✅ |
| Input packet size | 16 bytes | 64 bytes (assumed) | Variable | ⚠️ |
| Output packet size | 32 bytes | Variable | Variable | ✅ |
| HID Report Desc | 130 bytes | Not captured | N/A | ⚠️ |
| Feature Report | Via Control EP 0 | 0xCFEF, 11560 bytes | Not used | ✅ |

---

## Analysis Notes

### Input Report Size Discrepancy

The USB descriptor shows **16 bytes** max packet size for input endpoint, but:
- MASTER_GUIDE assumes 64 bytes (typical for gaming devices)
- Userspace driver handles variable sizes
- Actual input reports may be smaller than max packet size

**Resolution:** The 16-byte max is the USB packet size limit. Actual HID input reports are likely smaller (8-16 bytes) containing wheel/pedal/button data.

### Feature Report Transport

The **0xCFEF feature report (11560 bytes)** is NOT sent via interrupt endpoints. It uses:
- **USB Control Endpoint 0** (default control pipe)
- **HID SET_FEATURE request** (bRequest = 0x09)
- **bmRequestType = 0x21** (Host-to-device, Class, Interface)

This is why the userspace driver uses **interrupt transfers** instead - it's using a different protocol that sends smaller chunks via the output interrupt endpoint.

### HID Report Descriptor (Missing)

The 130-byte HID Report Descriptor defines:
- Input report structure (wheel, pedals, buttons)
- Output report structure (if any)
- Feature report structure (0xCFEF)
- Usage pages and usages
- Logical/physical min/max values

**How to extract:**
1. **USB Capture:** Capture GET_DESCRIPTOR request during enumeration
2. **Hidraw:** Read from `/sys/class/hidraw/hidrawX/device/report_descriptor`
3. **Kernel:** Parse from kernel HID driver logs
4. **Windows:** Extract from driver INF or captured traffic

---

## Recommendations

### For Kernel Driver Development

1. **Use Feature Reports (0xCFEF)**
   - Send via Control Endpoint (EP 0)
   - Use `hid_hw_raw_request()` with `HID_FEATURE_REPORT`
   - 11560-byte buffer as documented

2. **Parse Input Reports**
   - Max 16 bytes per packet
   - Contains wheel position, pedals, buttons
   - Poll at 1000 Hz (1ms intervals)

3. **Handle Output Endpoint**
   - Max 32 bytes per packet
   - Can be used for alternative protocol (like userspace driver)
   - Faster than feature reports for small commands

### For Userspace Driver

1. **Current Approach Works**
   - Using interrupt endpoint (0x01 OUT)
   - Max 32 bytes per transfer
   - Multiple transfers per effect

2. **Potential Optimization**
   - Could switch to feature reports for compatibility
   - Would match Windows driver behavior
   - Requires larger buffer allocation

### For HID Descriptor Capture

```bash
# Method 1: From hidraw device
sudo cat /sys/class/hidraw/hidraw*/device/report_descriptor | hexdump -C

# Method 2: From USB capture
sudo tshark -i usbmon2 -Y "usb.setup.bRequest == 6 && usb.setup.wValue == 0x2200"

# Method 3: During device enumeration
sudo usbmon -i 2 | grep "GET_DESCRIPTOR.*HID"
```

---

## Summary

✅ **What We Have:**
- Complete USB device/configuration/interface/endpoint descriptors
- Endpoint addresses and packet sizes confirmed
- HID descriptor metadata (length = 130 bytes)
- Power requirements and USB speed

⚠️ **What's Missing:**
- The actual 130-byte HID Report Descriptor
- Detailed input report format
- Exact feature report structure within the descriptor

❌ **Impact of Missing Info:**
- **Minimal** - Protocol is well-documented from reverse engineering
- Can implement kernel driver without HID descriptor
- HID core will parse descriptor automatically
- Only needed for complete documentation

**Status:** Ready to proceed with kernel driver implementation using existing protocol documentation.

---

**Related Documents:**
- `MASTER_IMPLEMENTATION_GUIDE.md` - Complete protocol specification
- `IMPLEMENTATION_ANALYSIS.md` - Detailed architecture analysis
- `captures/manual analysis/` - Raw USB packet captures
- `userspace/src/t500rs_usb.c` - Working implementation

**END OF HID DESCRIPTOR ANALYSIS**
