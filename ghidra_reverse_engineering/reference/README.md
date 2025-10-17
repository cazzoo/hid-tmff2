# T500RS Comprehensive Reverse Engineering Guide

## Project Overview
This directory contains a systematic and exhaustive analysis of the Windows T500RS driver components to enable complete Linux driver implementation (both userspace and kernel).

## Current Status
- ✅ **Ghidra Setup**: Project T500RS configured with all driver binaries
- ✅ **tmpid.dll Connected**: Analysis in progress via MCP (port 8193)
- ✅ **SetPeriodic Analyzed**: Complete HID report construction documented
- ⏳ **Remaining**: Multiple drivers and hundreds of functions

## Directory Structure
```
ghidra_reverse_engineering/
├── README.md                    # This file
├── 00_ANALYSIS_INDEX.md         # Master analysis tracker
├── 01_ANALYSIS_PLAN.md          # Detailed analysis workflow
├── analysis/                    # Per-function detailed analysis
│   ├── functions/               # Individual function decompilations
│   ├── constants/               # Constants and enums
│   └── algorithms/              # Key algorithms extracted
├── findings/                    # Major discoveries
│   ├── tmpid_dll_analysis.md
│   ├── tmpid_setperiodic_analysis.md  # ✅ COMPLETE
│   ├── tmhidusb_sys_analysis.md
│   ├── tmeffcpl_dll_analysis.md
│   └── device_structures.md
├── protocols/                   # Protocol specifications
│   ├── hid_protocol_complete.md
│   ├── ff_effects_translation.md
│   ├── initialization_sequence.md
│   └── linux_implementation_guide.md
└── scripts/                     # Automation scripts
    └── auto_analyze_all.py      # ✅ CREATED
```

## How to Continue the Analysis

### Option 1: Automated Analysis (Recommended for Bulk Extraction)
Run the Python automation script to systematically analyze all key functions:

```bash
cd /home/caz/Documents/hid-tmff2/ghidra_reverse_engineering/scripts
chmod +x auto_analyze_all.py
python3 auto_analyze_all.py
```

This will:
- Extract all force feedback functions (SetPeriodic, SetConstant, SetEnvelope, etc.)
- Decompile device control functions
- Document initialization sequences
- Generate markdown analysis files
- Create JSON summary for further processing

### Option 2: Interactive Analysis via Warp AI
Continue the manual analysis by asking Warp AI to:

1. **Analyze specific functions**:
   - "Decompile and analyze CPidDevice::SetConstant"
   - "Find and analyze the device initialization function"
   - "Extract the HID report sending function FUN_180017e24"

2. **Switch to other binaries**:
   - "Open tmhidusb.sys in Ghidra and analyze IRP handlers"
   - "Switch to tmeffcpl.dll and find range setting functions"

3. **Cross-reference analysis**:
   - "Find all callers of the SetPeriodic function"
   - "Trace data flow from DirectInput API to USB send"

### Option 3: Manual Ghidra Exploration
Use Ghidra GUI directly for:
- Complex control flow analysis
- Data structure recovery
- Graph visualization
- Custom annotations

## Key Files to Analyze

### Priority 1: Core Force Feedback (tmpid.dll)
Current binary: tmpid.dll (port 8193)

**Force Feedback Functions** (Critical for Linux):
- ✅ `CPidDevice::SetPeriodic` (0x18000cbbc) - ANALYZED
- ⏳ `CPidDevice::SetConstant` (0x1800030a0)  
- ⏳ `CPidDevice::SetEnvelope` (0x180001f30)
- ⏳ `CPidDevice::SetCondition` (0x180002c80)
- ⏳ `CPidDevice::SetEffect` (0x180002ca0)
- ⏳ `CPidDevice::EffectOperation` (0x180002c60)

**Device Management Functions**:
- ⏳ `CPidDevice::Start` (0x180001c20)
- ⏳ `CPidDevice::Stop` (0x180001d18)
- ⏳ `CPidDevice::DeviceControl` (0x180002188)
- ⏳ `CPidDevice::DeviceGain` (0x1800021a8)
- ⏳ `CPidDevice::Write` (0x180002a98) 
- ⏳ `FUN_180017e24` - USB send function (called by SetPeriodic)

**Device Configuration**:
- ⏳ `CPidDevice::SetDeviceMode` (0x180002698)
- ⏳ `CPidDevice::SetDefaultWheelAngle` (0x180002ab0)
- ⏳ `CPidDevice::GetWheelID` (0x180002438)
- ⏳ `CPidDevice::GetFirmwareVersion` (0x1800021e0)

### Priority 2: Kernel Driver (tmhidusb.sys)
**Open in Ghidra**: Use Ghidra GUI to open tmhidusb.sys (x64 version)

**Key Areas**:
- Driver entry point (DriverEntry)
- Add Device routine
- IRP dispatch handlers (Create, Close, DeviceControl, InternalDeviceControl)
- URB (USB Request Block) construction
- HID report handling
- Power management

### Priority 3: Configuration (tmeffcpl.dll)
**Open in Ghidra**: Use Ghidra GUI to open tmeffcpl.dll (x64 version)

**Key Areas**:
- Range setting UI logic
- Calibration procedures
- Registry/config storage
- Device detection

### Priority 4: SDK (tm_api_lib_x64.dll)
**Analyze** if present in VM_Shared:
- Public API surface
- Application interface
- Effect translation layer

## Extraction Methodology

For each function, document:

1. **Function Signature**
   - Parameters and types
   - Return values
   - Calling convention

2. **Key Structures**
   - Struct offsets discovered
   - Data type definitions
   - Size information

3. **Algorithm/Logic**
   - Control flow
   - Key calculations
   - State transitions

4. **HID Protocol Details**
   - Report IDs used
   - Usage IDs and pages
   - Report construction
   - Data encoding

5. **Dependencies**
   - Functions called
   - External APIs used
   - Critical data accessed

6. **Linux Implementation Notes**
   - Required adaptations
   - Missing APIs to implement
   - Threading considerations
   - Error handling

## Critical Information Still Needed

### 1. HID Report Descriptor
**How to get**:
```bash
# From live device
sudo hid-decode /sys/bus/hid/devices/*/report_descriptor

# Or extract from Windows driver
# (May be embedded in tmhidusb.sys or obtained via USB capture)
```

### 2. Complete Device Structure
Extract full CPidDevice structure layout by:
- Analyzing all offset accesses across functions
- Finding constructor/destructor
- Documenting all member variables

### 3. Initialization Sequence
Document complete device startup:
- HID capability queries
- Feature report exchanges
- Mode setting
- Calibration steps

### 4. Effect Slot Management
Understanding how the driver manages multiple simultaneous effects:
- Effect allocation (`AllocateEffectIndex`)
- Effect slot mapping
- Priority handling
- Effect combinations

### 5. Advanced Features
- Screen/LED control (if applicable)
- Wheel angle/range management
- Brake pedal combining
- Firmware update protocol

## Integration with Linux Driver

### Userspace Driver (`userspace/t500rs-ffb.c`)
Apply findings to improve:
1. **Protocol Layer** (`t500rs_protocol.c`):
   - Implement exact HID report formats from analysis
   - Add all discovered Usage IDs
   - Implement proper scaling (e.g., phase/100)

2. **Effect Translation**:
   - Match Windows behavior exactly
   - Handle edge cases discovered
   - Implement optimization (report comparison)

3. **Device Detection**:
   - Add capability detection based on analysis
   - Support different device modes
   - Handle firmware variations

### Kernel Driver (Future)
Use findings to create:
1. **HID Driver Module**:
   - Based on tmhidusb.sys analysis
   - Proper report parsing
   - IRQ handling

2. **Input Device**:
   - Correct axis ranges
   - Button mapping
   - Force feedback interface

3. **Configuration Interface**:
   - sysfs attributes
   - Range control
   - Mode switching

## Tools and Resources

### Ghidra MCP Access
```bash
# Check current instance
curl http://localhost:8192/info

# List all instances
curl http://localhost:8192/instances

# Switch binary (in Warp AI):
# "Open tmhidusb.sys in Ghidra and connect via MCP"
```

### USB Traffic Comparison
Compare Windows vs Linux USB traffic:
```bash
# Capture Windows traffic (in VM/Wine)
# Use Wireshark/USBPcap

# Capture Linux traffic
sudo usbmon
sudo tshark -i usbmon1 -Y "usb.addr == X"
```

### HID Tools
```bash
# Decode HID descriptor
hidrd-convert --input-format=raw --output-format=spec < descriptor.bin

# Test force feedback
fftest /dev/input/eventX

# Monitor HID reports
sudo hid-replay /dev/hidrawX
```

## Next Steps

1. **Complete tmpid.dll Analysis**:
   - Run automated script
   - Manually analyze complex functions
   - Document all force feedback functions

2. **Analyze tmhidusb.sys**:
   - Open in Ghidra
   - Find IRP handlers
   - Document USB communication

3. **Extract Complete Protocol**:
   - Consolidate all HID findings
   - Create reference implementation
   - Document all command formats

4. **Test and Validate**:
   - Compare USB traffic
   - Verify protocol implementation
   - Test all effect types

5. **Update Linux Drivers**:
   - Apply findings to userspace driver
   - Fix Wine/game compatibility issues
   - Consider kernel driver if needed

## Notes and Observations

### Key Insights from SetPeriodic Analysis
- Windows uses standard HID PID (Physical Interface Device) protocol
- Usage Page 0x0F with standard PID usages
- Report construction via HidP_SetUsageValue/HidP_SetScaledUsageValue
- Critical: Phase value divided by 100 before sending
- Optimization: Report comparison to avoid redundant USB traffic
- Thread-safe via Critical Sections
- Two modes: immediate send vs batched (Total Effect Report)

### Device Structure Offsets Discovered
```c
struct CPidDevice {
    // +0x598: PHIDP_PREPARSED_DATA
    // +0x5b2: uint16_t input_report_size
    // +0x638: bool device_ready
    // +0x658: bool periodic_supported
    // +0x666: uint16_t link_collection  
    // +0x1a8: bool use_total_effect_report
    // +0x1b0: bool supports_duty_cycle
    // +0x7e0: CRITICAL_SECTION
};
```

### Common HID Usage IDs (PID Page 0x0F)
```c
#define PID_EFFECT_BLOCK_INDEX  0x22
#define PID_MAGNITUDE           0x70
#define PID_OFFSET              0x6F
#define PID_PHASE               0x71
#define PID_PERIOD              0x72
#define PID_DUTY_CYCLE          0x2B
// More to be discovered...
```

## Contributing Your Findings

As you analyze more functions:
1. Create markdown files in `analysis/` or `findings/`
2. Update this README with new discoveries
3. Add to the protocol documentation in `protocols/`
4. Test findings against real hardware
5. Update Linux driver implementation

## Questions to Answer

- [ ] What are all the HID report IDs used?
- [ ] How are conditional effects (spring, damper, friction) implemented?
- [ ] What is the exact device initialization sequence?
- [ ] How does range/angle management work?
- [ ] What are the complete error codes and handling?
- [ ] How does the driver detect device capabilities?
- [ ] What is the firmware update protocol (if any)?
- [ ] How are multiple effects combined/prioritized?

## References
- Previous analysis: `../T500RS_REVERSE_ENGINEERING_ANALYSIS.md`
- USB captures: `../captures/`
- Userspace driver: `../userspace/`
- HID PID specification: USB Physical Interface Device (PID) specification
- Windows HID API documentation

---

**Last Updated**: 2025-10-14  
**Analyst**: Automated Ghidra MCP Analysis  
**Status**: Initial analysis phase - tmpid.dll SetPeriodic complete
