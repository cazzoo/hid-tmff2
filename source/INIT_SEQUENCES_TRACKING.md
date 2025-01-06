# T500RS Initialization Sequences Tracking

## USB Endpoint Configuration
From GET_DESCRIPTOR_HEX_DUMPS:
- Configuration Descriptor:
  - bNumInterfaces: 1
  - bConfigurationValue: 1
  - iConfiguration: 0
  - bmAttributes: 0xc0 (Self-powered)
  - bMaxPower: 50mA (0x32)

- Interface Descriptor:
  - bInterfaceNumber: 0
  - bAlternateSetting: 0
  - bNumEndpoints: 2
  - bInterfaceClass: 3 (HID)
  - bInterfaceSubClass: 0
  - bInterfaceProtocol: 0

- Endpoint Configuration:
  - Endpoint 0x82 (IN): Interrupt transfer, max packet size 16 bytes
  - Endpoint 0x01 (OUT): Interrupt transfer, max packet size 32 bytes

## Tried Initialization Sequences

### Sequence 1 (Initial Attempt)
```c
static const u8 setup_0[8] = { 0x42, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const u8 setup_1[8] = { 0x41, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
```
Result: Failed with error -32 (EPIPE)

### Sequence 2 (With Configuration)
```c
static const u8 setup_0[8] = { 0x42, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const u8 setup_1[8] = { 0x41, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const u8 setup_2[8] = { 0x09, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // Configuration
static const u8 setup_3[8] = { 0x09, 0x02, 0x29, 0x00, 0x01, 0x01, 0x00, 0x00 }; // Interface
```
Result: Failed with error -32 (EPIPE)

## Next Sequences to Try

### Sequence 3 (Based on HID Report Descriptor)
```c
// Initialize HID report first
static const u8 setup_0[8] = { 0x85, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // Report ID 7
static const u8 setup_1[8] = { 0x41, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // Mode command
static const u8 setup_2[8] = { 0x42, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // Init command
```

### Sequence 4 (Based on USB Captures)
```c
// Set configuration first
static const u8 setup_0[8] = { 0x09, 0x02, 0x29, 0x00, 0x01, 0x01, 0x00, 0x00 }; // Set config
static const u8 setup_1[8] = { 0x09, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // Set interface
static const u8 setup_2[8] = { 0x42, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // Init command
static const u8 setup_3[8] = { 0x41, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // Mode command
```

## Notes
- Error -32 (EPIPE) indicates a broken pipe, suggesting the device is not accepting our commands
- Need to ensure proper USB endpoint configuration before sending commands
- May need to add delays between configuration and command packets
- Consider using HID raw requests instead of direct USB commands 