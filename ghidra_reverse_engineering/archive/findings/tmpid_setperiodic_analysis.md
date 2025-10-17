# tmpid.dll SetPeriodic Function Analysis

## Function Address
**0x18000cbbc** (FUN_18000cbbc - this is CPidDevice::SetPeriodic)

## Function Signature
```c
uint SetPeriodic(
    longlong device,              // param_1 - Device object pointer
    void *report_buffer,          // param_2 - Output report buffer
    undefined4 effect_block_idx,  // param_3 - Effect block index (Usage 0x22)
    undefined4 magnitude,         // param_4 - Magnitude value (Usage 0x70)
    undefined4 offset,            // param_5 - Offset value (Usage 0x6F)
    uint phase,                   // param_6 - Phase value (Usage 0x71) - DIVIDED BY 100!
    undefined4 period,            // param_7 - Period value (Usage 0x72)
    undefined4 duty_cycle,        // param_8 - Duty cycle (Usage 0x2B) - optional
    undefined1 *dirty_flag        // param_9 - Output: set to 1 if report changed
)
```

## Critical Findings

### HID Report Structure
The function uses Windows HID API to build reports:
- **Report Type**: Output Report (ReportType = 1)
- **Usage Page**: 0x0F (Physical Interface Device Page)
- **Link Collection**: Stored at offset `device+0x666`
- **Preparsed Data**: Stored at offset `device+0x598`
- **Report Size**: Stored at offset `device+0x5b2` (Input report size)

### HID Usage IDs for Periodic Effects
```c
#define USAGE_EFFECT_BLOCK_INDEX   0x22   // Effect block index
#define USAGE_MAGNITUDE            0x70   // Periodic magnitude
#define USAGE_OFFSET               0x6F   // Periodic offset
#define USAGE_PHASE                0x71   // Phase (degrees / 100)
#define USAGE_PERIOD               0x72   // Period
#define USAGE_DUTY_CYCLE           0x2B   // Duty cycle (optional, if device+0x1b0 != 0)
```

### Device Structure Offsets Discovered
```c
struct CPidDevice {
    // ... (base members)
    uint16_t input_report_size;        // +0x5B2
    void*    preparsed_data;           // +0x598
    uint16_t link_collection;          // +0x666
    bool     device_ready;             // +0x638
    bool     periodic_supported;       // +0x658
    bool     supports_duty_cycle;      // +0x1B0
    bool     use_total_effect_report;  // +0x1A8
    CRITICAL_SECTION cs;               // +0x7E0
    // ...
};
```

### Report Construction Algorithm
1. **Allocate report buffer** (`malloc(input_report_size)`)
2. **Zero buffer** (`memset`)
3. **Enter critical section** (thread safety)
4. **Check device ready** (offset +0x638)
5. **Check feature supported** (offset +0x658)
6. **Set HID values using Windows HID API**:
   - `HidP_SetUsageValue()` for discrete values (Effect Block Index)
   - `HidP_SetScaledUsageValue()` for scaled values (Magnitude, Offset, Phase, Period)
7. **Phase is divided by 100** before sending!
8. **Optional Duty Cycle** set if `device+0x1b0 != 0`
9. **Compare with previous report** (`memcmp`) - optimization to avoid redundant sends
10. **Set dirty flag** if report changed
11. **Send report** via `FUN_180017e24()` if not using total effect report mode
12. **Leave critical section**

### Error Codes
- **0xC0110020**: Periodic effects not supported by device
- **0xC0110008**: Device not ready
- **0x80004005**: Send failed (inverted from iVar4)
- **Success**: 0 or positive HidP status

### Key Behaviors
1. **Thread-safe**: Uses Windows CRITICAL_SECTION
2. **Optimization**: Compares new report with previous to avoid redundant USB traffic
3. **Conditional features**: Duty cycle only set if device supports it
4. **Two modes**: 
   - Normal mode: Sends report immediately via `FUN_180017e24`
   - Total effect report mode: Queues for batch send (if `device+0x1a8 != 0`)

### Windows HID API Usage
```c
// Setting effect block index (discrete value)
HidP_SetUsageValue(
    ReportType: 1,              // Output report
    UsagePage: 0x0F,            // PID page
    LinkCollection: device->link_collection,
    Usage: 0x22,                // Effect Block Index
    UsageValue: effect_block_idx,
    PreparsedData: device->preparsed_data,
    Report: buffer,
    ReportLength: device->input_report_size
);

// Setting magnitude (scaled value)
HidP_SetScaledUsageValue(
    ReportType: 1,
    UsagePage: 0x0F,
    LinkCollection: device->link_collection,
    Usage: 0x70,                // Magnitude
    UsageValue: magnitude,
    PreparsedData: device->preparsed_data,
    Report: buffer,
    ReportLength: device->input_report_size
);

// Phase is special - divided by 100!
HidP_SetScaledUsageValue(..., Usage: 0x71, UsageValue: phase / 100, ...);
```

## Linux Implementation Notes

### Direct USB Report Construction
Since Linux doesn't have HidP_SetUsageValue, we need to:
1. Understand the exact report descriptor from the device
2. Manually construct the HID report bytes
3. Map each Usage ID to its bit position/byte offset in the report
4. Handle scaling manually (especially phase / 100)

### Device Capability Detection
The Windows driver checks:
- `device+0x658`: Whether periodic effects are supported
- `device+0x1b0`: Whether duty cycle is supported

Linux driver should query HID descriptor or use feature reports to detect these.

### Threading
- Windows uses CRITICAL_SECTION
- Linux should use pthread_mutex or similar
- Protect report construction and device access

### Optimization Pattern
The Windows driver implements report comparison to avoid redundant USB traffic:
```c
if (memcmp(new_report, old_report, report_size) == 0) {
    // Skip sending, report unchanged
} else {
    // Send report
    memcpy(old_report, new_report, report_size);
}
```
Linux driver should implement similar optimization.

## Next Analysis Steps
1. Find `FUN_180017e24` - the actual USB send function
2. Analyze `SetConstant`, `SetEnvelope`, `SetCondition` for other effect types
3. Find report descriptor parsing code
4. Analyze device initialization to understand capability detection
