# T500RS Comprehensive Reverse Engineering Analysis

**Date**: 2025-01-08  
**Source**: Ghidra analysis of Windows `tmpid.dll` driver  
**Purpose**: Guide Linux userspace and kernel driver improvements

## Executive Summary

The T500RS uses a sophisticated Windows userspace driver that communicates via HID reports with a custom 0xEF-prefixed protocol. The driver implements a Windows hook system to intercept application messages and translate them to device commands.

## 1. Driver Architecture Analysis

### Windows Driver Structure
- **Type**: Userspace DLL library (`tmpid.dll`)
- **Hook System**: Uses `SetWindowsHookExA` with `WH_GETMESSAGE` (type 3)
- **Multi-Device Support**: Supports up to 16 devices simultaneously (0x10 max)
- **Device Structure**: Each device uses 2864 bytes (0xB30) of state memory
- **Communication**: HID output reports via Windows HID APIs

### Key Components
```c
// Core driver entry points (addresses from Ghidra)
entry()                   // 0x180040fbc - DLL main entry point
FUN_180034d50()          // Main initialization and device management  
FUN_180035960()          // Device initialization with Windows hook setup
FUN_180035e14()          // Windows message hook procedure
FUN_180034108()          // Message dispatch to device handler
FUN_18002581c()          // Core device processing loop
```

## 2. HID Protocol Specification

### Primary HID Report Format
All T500RS commands use this structure:
```c
struct t500rs_hid_output {
    uint8_t  report_id;      // Always 0xEF
    uint8_t  command_type;   // Command identifier (see below)
    uint16_t parameter;      // Command parameter (little-endian)
    uint8_t  reserved[2];    // Usually zero
    uint8_t  flags;          // Additional command flags
    uint8_t  payload[];      // Variable length payload
};

#define T500RS_REPORT_ID        0xEF
#define T500RS_REPORT_SIZE_STD  24    // 0x18 bytes typical
```

### Command Types Identified
```c
// From Ghidra analysis of FUN_18000ebf4
#define T500RS_CMD_SYSTEM         0x01  // System/initialization command
#define T500RS_CMD_FF_PRIMARY     0x03  // Primary force feedback command
#define T500RS_CMD_FF_SECONDARY   0x04  // Secondary force feedback command
#define T500RS_CMD_CONFIG         0x05  // Device configuration
#define T500RS_CMD_STATUS         0x06  // Status/query command
#define T500RS_CMD_FF_EXTENDED    0x11  // Extended force feedback (0x11 = 17)

// Internal command constants discovered
#define T500RS_INTERNAL_FF_ENABLE  0x313  // 787 decimal - FF enable
#define T500RS_INTERNAL_FF_PARAM   0x303  // 771 decimal - FF parameter
#define T500RS_INTERNAL_RANGE_SET  0x97   // Range enable command
#define T500RS_INTERNAL_RANGE_DIS  0x98   // Range disable command
```

### Device State Structure
Based on offset analysis from FUN_18002581c:
```c
struct t500rs_device_state {
    // Core identification and handles
    void*    hid_handle;             // +0x590 - Windows HID handle
    uint32_t device_id;              // +0x106 - Device identifier
    uint32_t enabled;                // +0x105 - Device enabled flag
    
    // Report configuration  
    uint16_t output_report_size;     // +0x5B4 - Output report size (24 bytes)
    uint16_t input_report_size;      // +0x5B2 - Input report size
    uint16_t feature_report_size;    // +0x66C - Feature report size
    uint16_t collection_size;        // +0x676 - HID collection size
    
    // Force feedback state
    uint32_t ff_enabled;             // +0x15E - Force feedback on/off
    uint32_t ff_gain;                // +0x15F - Global FF gain
    uint32_t constant_level;         // +0x15B - Constant force level
    
    // Range and position
    uint32_t steering_range;         // +0x157 - Steering range (default 10000)
    uint32_t current_x;              // +0xAF4 - Current X position
    uint32_t current_y;              // +0xAFC - Current Y position
    
    // Status and control
    uint32_t update_rate;            // Update frequency
    uint32_t dirty_flag;             // +0x164 - Needs update flag
    uint32_t initialized;            // +0x82C - Initialization complete
    
    // Error/default values used on failure
    uint32_t error_position_x;       // 0x500 (1280) - Center position
    uint32_t error_position_y;       // 900 - Default Y
    uint32_t error_status;           // 0x8F - Default status flags
};
```

## 3. Force Feedback Protocol

### Force Feedback Commands
The driver uses a complex system to translate Windows FF effects:

```c
// Command construction in FUN_18000ebf4
void construct_ff_command(uint8_t cmd_type, uint16_t param, uint8_t flags) {
    buffer[0] = 0xEF;           // Report ID
    buffer[1] = cmd_type;       // Command type (1,3,4,5,6,0x11)
    buffer[2] = param & 0xFF;   // Parameter low byte
    buffer[3] = (param >> 8);   // Parameter high byte
    buffer[6] = flags;          // Flags byte
    
    // Special handling for different command types
    if (cmd_type == 0x01) {
        // System command gets 7-byte payload
        memcpy(&buffer[7], payload, 7);
    } else if (cmd_type == 0x03 || cmd_type == 0x04 || cmd_type == 0x11) {
        // Force feedback commands get parameter in bytes 2-3
        // Additional data may go in later bytes
    }
}
```

### Range Setting Protocol
From FUN_1800260c0 analysis:
```c
int set_steering_range(struct t500rs_device *dev, uint32_t range_raw) {
    uint32_t range_scaled;
    
    // The Windows driver scales ranges differently based on device mode
    if (dev->legacy_mode == 0) {
        // Modern mode: scale range from 0-10000 to 0-100
        range_scaled = MulDiv(100, range_raw, 10000);
    } else {
        // Legacy mode: use raw value
        range_scaled = range_raw & 0xFFFF;
    }
    
    // Send range configuration commands
    send_hid_command(dev, 0xEF, T500RS_INTERNAL_FF_ENABLE, range_scaled, 0);
    return send_hid_command(dev, 0xEF, T500RS_INTERNAL_FF_PARAM, range_scaled, 0);
}
```

## 4. Critical Implementation Findings

### Default Values and Error Handling
When communication fails, the Windows driver sets these defaults:
```c
#define T500RS_DEFAULT_RANGE        10000
#define T500RS_DEFAULT_UPDATE_RATE  10000
#define T500RS_CENTER_X            0x500   // 1280 decimal
#define T500RS_CENTER_Y             900
#define T500RS_ERROR_STATUS         0x8F
#define T500RS_ERROR_POSITION      -1      // 0xFFFFFFFF
```

### Communication Patterns
1. **Initialization Sequence** (from Windows driver):
   - Device enumeration via HID APIs
   - Capability queries using `HidP_GetCaps`
   - Attribute retrieval via `HidD_GetAttributes`
   - Report descriptor parsing with `HidD_GetPreparsedData`
   - Device state initialization

2. **Runtime Loop**:
   - Continuous polling of device state changes
   - Message hook processing for application input
   - Force feedback effect translation
   - Status synchronization

### Thread-Safe Operations
The Windows driver uses:
- Critical sections for device state access (`EnterCriticalSection`/`LeaveCriticalSection`)
- Windows message hooks for real-time input processing
- Event signaling for initialization completion (`SetEvent`)

## 5. Linux Implementation Recommendations

### Userspace Driver Improvements Needed

Based on current code analysis and Windows driver findings:

#### 1. Enhanced Protocol Support
```c
// Current userspace driver uses hardcoded reports - needs protocol abstraction
struct t500rs_protocol {
    uint8_t report_id;
    uint8_t (*construct_command)(uint8_t cmd, uint16_t param, uint8_t flags);
    int (*send_range_command)(int range_degrees);
    int (*send_ff_command)(uint8_t effect_type, int16_t level);
};
```

#### 2. Better Range Handling
Current driver hardcodes range values. Windows driver shows:
```c
// Implement proper range scaling
int t500rs_set_range(struct t500rs_device *dev, int degrees) {
    // Scale degrees (270-1080) to internal range (0-10000)
    uint32_t internal_range = (degrees - 270) * 10000 / (1080 - 270);
    
    // Use Windows driver's scaling formula
    uint32_t scaled = MulDiv(100, internal_range, 10000);
    
    return send_hid_report(dev, 0xEF, T500RS_INTERNAL_FF_ENABLE, scaled);
}
```

#### 3. Force Feedback Translation Layer
Current driver directly maps Linux FF to device commands. Windows driver shows intermediary layer:
```c
struct t500rs_effect_translator {
    int (*translate_constant)(struct ff_effect *linux_effect, 
                             struct t500rs_hid_output *device_cmd);
    int (*translate_periodic)(struct ff_effect *linux_effect,
                             struct t500rs_hid_output *device_cmd);
    int (*translate_conditional)(struct ff_effect *linux_effect,
                                struct t500rs_hid_output *device_cmd);
};
```

#### 4. State Management System
Implement Windows-style device state tracking:
```c
struct t500rs_state_manager {
    struct t500rs_device_state current_state;
    struct t500rs_device_state pending_state;
    pthread_mutex_t state_lock;
    
    bool needs_update;
    uint32_t update_rate_ms;
    pthread_t update_thread;
};
```

### Kernel Driver Development Path

#### HID Report Descriptor Analysis
Need to capture and analyze the actual HID report descriptor to understand:
- Input report structure (button and axis data)
- Output report capabilities
- Feature report functions

#### Kernel Module Structure
```c
// Based on Windows driver findings
static int t500rs_hid_probe(struct hid_device *hdev, 
                            const struct hid_device_id *id) {
    // Initialize device state structure (2864 bytes)
    // Set up HID report sizes (24 bytes output)
    // Configure force feedback capabilities
    // Create input device with proper ranges
}

static int t500rs_hid_raw_event(struct hid_device *hdev, 
                               struct hid_report *report, 
                               u8 *data, int size) {
    // Parse input reports based on Windows driver knowledge
    // Update input device state
    // Handle status updates
}
```

## 6. Next Implementation Steps

### Phase 1: Protocol Implementation (High Priority)
1. **Extract and Document HID Report Descriptor**
   - Use `hidrd-convert` or similar to get complete descriptor
   - Map all input/output report structures
   - Identify unused/vendor-specific fields

2. **Implement 0xEF Command Protocol**
   ```c
   // Replace current hardcoded commands with protocol layer
   int t500rs_send_command(struct t500rs_device *dev, 
                          uint8_t cmd_type, uint16_t param, uint8_t flags);
   ```

3. **Add Windows-Compatible Range Setting**
   - Implement MulDiv scaling function
   - Add support for 0x313/0x303 command sequence
   - Test with known working ranges (270°, 900°, 1080°)

### Phase 2: Enhanced Force Feedback (Medium Priority)  
1. **Effect Translation Layer**
   - Create proper constant force mapping
   - Implement periodic effect support
   - Add conditional effects (spring, damper, friction)

2. **Real-time Parameter Updates**
   - Implement gain control per effect type
   - Add envelope support (attack/fade)
   - Create effect combination logic

### Phase 3: Kernel Driver (Lower Priority)
1. **HID Driver Framework**
   - Use findings to create proper kernel HID driver
   - Implement report parsing based on Windows knowledge
   - Add sysfs interface for configuration

2. **Integration Testing**
   - Test compatibility with userspace driver knowledge
   - Verify protocol matches Windows behavior
   - Performance optimization

## 7. Testing and Validation Protocol

### Validation Methods
1. **USB Traffic Comparison**
   - Capture Windows driver USB traffic
   - Compare with Linux implementation output
   - Ensure identical command sequences for same operations

2. **Device Response Verification**
   - Test all command types (0x01, 0x03, 0x04, 0x05, 0x06, 0x11)
   - Verify range settings produce expected wheel behavior
   - Confirm force feedback effects match Windows experience

3. **Application Compatibility**
   - Test with games that work on Windows
   - Verify input event generation matches expectations
   - Confirm force feedback timing and intensity

### Debug Tools Needed
```bash
# USB monitoring
sudo usbmon
tshark -i usbmon1 -Y "usb.addr == X"

# HID analysis  
sudo hid-replay /dev/hidrawX
hidrd-convert --input-format=raw --output-format=spec

# Force feedback testing
fftest /dev/input/eventX
```

This analysis provides the foundation for significantly improving the Linux T500RS driver by implementing the sophisticated protocol and state management discovered in the Windows driver.