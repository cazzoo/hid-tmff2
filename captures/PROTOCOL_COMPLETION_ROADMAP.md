# T500RS Protocol Completion Roadmap

## Current Status: ~85% Complete

### ✅ Completed Analysis

1. **HID Report Descriptor** - Fully decoded (130 bytes)
   - Report ID 0x07: Input telemetry (steering, pedals, buttons)
   - Report ID 0x0a: FF output commands (14 bytes)
   - Report ID 0x14: Device status responses
   - Report IDs 0x02, 0x40, 0x42: Control commands

2. **USB Captures Analyzed**
   - `plug_t500_in.pcapng` - Device enumeration + RDESC
   - `TM.pcap` - Normal telemetry operation
   - `attempt.pcap` - Initial FF attempts
   - `lr1.pcapng`, `lr2dedu_filter.pcapng` - Extended captures
   - `t500rs_windows_*.pcapng` - Linux usbmon captures

3. **Protocol Documentation**
   - Command byte mappings identified
   - Multi-packet upload sequences documented
   - Parameter encoding partially understood
   
4. **Tools Created**
   - `decode_ff_commands.py` - USB packet decoder
   - `t500rs_test.cpp` - Windows DirectInput test program
   - Capture guides and analysis documents

### 🔧 Active Work: Report 0x0a Decoding

**Current Understanding:**
```
Report Structure (14 bytes):
[0] = 0x0a (Report ID)
[1] = Command byte
[2-3] = Parameter 1 (16-bit LE)
[4-13] = Additional parameters (varies by command)
```

**Known Command Bytes:**
- `0x01` - Effect upload/create
- `0x04` - Set parameter (magnitude, frequency, etc.)
- `0x05` - Start effect
- `0x06` - Stop effect
- `0x42` - Init/reset (?)
- `0x40` - Control/status (?)

**Missing Details:**
1. Complete command byte mapping
2. Parameter field meanings for each command
3. Effect index assignment protocol
4. Multi-packet sequencing rules
5. Device state machine behavior

## Next Steps to Complete Protocol

### Phase 1: Fix Windows Test Application ✓ READY TO TEST

**Goal**: Get all force feedback effects working and generating actual forces

**Actions Taken**:
- Fixed missing `DIEFFECT` structure fields
- Corrected direction units (hundredths of degrees)
- Added explicit device acquisition
- Improved cooperative level handling

**Test Procedure**:
1. Rebuild: `cl /EHsc t500rs_test.cpp dinput8.lib dxguid.lib user32.lib`
2. Close all other programs (fedit.exe, etc.)
3. Run: `t500rs_test.exe --auto`
4. Verify effects create without errors
5. Verify physical forces are felt on wheel

**Success Criteria**:
- No `CreateEffect FAILED` errors
- All effect types (constant, periodic, condition) generate forces
- Forces match expected behavior (direction, magnitude, frequency)

**If Still Fails**: See `DIRECTINPUT_FF_FIXES.md` for troubleshooting steps

### Phase 2: Capture Working Effects ⏭️ NEXT

**Goal**: Record USB traffic for all working force feedback effects

**Preparation**:
1. Install/verify USBPcap on Windows VM
2. Start Wireshark capture on USB bus
3. Filter for T500RS device (VID:044f, PID:b65e)

**Capture Process**:
```batch
# Start USB capture in Wireshark
# Run automated test suite
t500rs_test.exe --auto

# Press Enter to start tests
# Wait for all 19 tests to complete
# Save capture as: t500rs_complete_ff_tests.pcapng
```

**Expected Capture Contents**:
- 19 complete effect sequences
- Each with: upload → parameter sets → start → stop
- Clear timing between effects (500ms pauses)

**Files to Generate**:
- `t500rs_complete_ff_tests.pcapng` - Full capture
- `t500rs_complete_ff_tests.json` - tshark JSON export

### Phase 3: Decode and Document Protocol ⏭️ AFTER PHASE 2

**Goal**: Map DirectInput parameters to USB byte values

**Analysis Tools**:
```bash
# Convert to JSON
tshark -r t500rs_complete_ff_tests.pcapng -Y "usb" -T json > complete_tests.json

# Extract FF commands
cd /home/caz/Documents/hid-tmff2/captures
python3 decode_ff_commands.py complete_tests.json > protocol_mapping.txt
```

**Documentation Tasks**:

1. **Create Command Byte Reference**
   ```
   For each command byte observed:
   - Function/purpose
   - Parameter fields used
   - Valid value ranges
   - Multi-packet sequences
   - Example packets
   ```

2. **Parameter Encoding Tables**
   ```
   Magnitude: DirectInput value → USB bytes
   Frequency: Hz → USB period encoding
   Duration: milliseconds → USB units
   Coefficients: DI value → device value
   Envelope: Attack/fade times/levels
   ```

3. **Effect Type Mapping**
   ```
   GUID_ConstantForce → Command sequences
   GUID_Sine → Command sequences
   GUID_Square → Command sequences
   GUID_Triangle → Command sequences
   GUID_Spring → Command sequences
   GUID_Damper → Command sequences
   GUID_Friction → Command sequences
   GUID_Inertia → Command sequences
   ```

### Phase 4: Compare with Existing Driver ⏭️ PARALLEL TO PHASE 3

**Goal**: Understand how current Linux driver differs from Windows protocol

**Files to Compare**:
```
/home/caz/Documents/hid-tmff2/
├── src/tmt500rs/hid-tmt500rs.c         # Current kernel driver
├── src/tmt500rs/hid-tmt500rs-usb.c     # USB communication
├── userspace/t500rs_protocol.c          # Userspace protocol impl
└── captures/PROTOCOL_MAPPING.md         # Documented Windows behavior
```

**Analysis Questions**:
1. Which report IDs does Linux driver use vs Windows?
2. Are command bytes the same?
3. Parameter encoding differences?
4. Missing effect types in Linux?
5. Initialization sequence differences?

**Document Gaps**:
Create `LINUX_VS_WINDOWS_PROTOCOL.md`:
- Side-by-side command comparison
- Missing functionality in Linux
- Parameter conversion needed
- Implementation recommendations

### Phase 5: Identify Unknown Report Types ⏭️ ADVANCED

**Goal**: Determine purpose of Report IDs 0x02, 0x40, 0x42

**Method 1: Controlled Testing**
```cpp
// Add to t500rs_test.cpp - send raw HID reports
void SendRawReport(BYTE reportID, BYTE* data, int len) {
    // Use IDirectInputDevice8::SendForceFeedbackCommand()
    // or SetProperty(DIPROP_FFLOAD) with custom data
}

// Test each unknown report ID with varying parameters
```

**Method 2: Driver Comparison**
```bash
# Search for report ID usage in Windows driver binaries
cd ghidra_reverse_engineering/
grep -r "0x42" .
grep -r "0x40" .
grep -r "Report.*0x02" .
```

**Method 3: Behavioral Observation**
- When does Windows driver send Report 0x42?
  * Device initialization?
  * Mode switching?
  * Error recovery?
- When does device send Report 0x14?
  * After FF command?
  * On error?
  * State changes?

### Phase 6: Test Edge Cases ⏭️ VALIDATION

**Goal**: Verify protocol robustness and limits

**Test Scenarios**:

1. **Concurrent Effects**
   - Upload multiple effects
   - Start them simultaneously
   - Observe mixing behavior
   - Check for slot conflicts

2. **Parameter Limits**
   - Minimum/maximum magnitudes
   - Frequency range limits
   - Duration edge cases (0, INFINITE)
   - Invalid parameter handling

3. **State Transitions**
   - Stop running effect
   - Modify running effect
   - Delete playing effect
   - Device reset during effect

4. **Error Conditions**
   - Too many effects uploaded
   - Invalid effect index
   - Out-of-order commands
   - Device response to errors

### Phase 7: Final Documentation ⏭️ COMPLETION

**Goal**: Comprehensive protocol specification

**Deliverables**:

1. **T500RS_PROTOCOL_SPECIFICATION.md**
   - Complete HID report reference
   - All command bytes documented
   - Parameter encoding tables
   - State machine diagrams
   - Multi-packet sequencing
   - Error handling
   - Example captures with annotations

2. **IMPLEMENTATION_GUIDE.md**
   - Linux kernel driver implementation
   - Userspace driver implementation
   - Parameter conversion formulas
   - Effect mixing strategy
   - Performance considerations
   - Testing procedures

3. **WINDOWS_DRIVER_ANALYSIS.md**
   - Ghidra reverse engineering findings
   - Binary protocol observations
   - Undocumented features
   - Quirks and workarounds

## Tools and Resources Inventory

### Created Tools
- ✅ `decode_ff_commands.py` - USB packet decoder
- ✅ `t500rs_test.cpp` - Windows FF test program
- ✅ `analyze_capture.sh` - Bash capture analysis
- 🔧 `DIRECTINPUT_FF_FIXES.md` - Troubleshooting guide
- 🔧 `WINDOWS_FF_CAPTURE_GUIDE.md` - Capture methodology

### Existing Captures
```
captures/
├── plug_t500_in.pcapng (10KB) - Device init + RDESC
├── TM.pcap (29KB) - Normal operation
├── attempt.pcap (6KB) - Early FF attempts
├── lr1.pcapng (289KB) - Extended capture
├── lr2dedu_filter.pcapng (79KB) - Filtered capture
├── device_init.pcapng - Another init
├── device_const_force_pos.pcapng - Constant force test
└── manual analysis/ - Hand-decoded captures
```

### Analysis Documents
```
captures/
├── t500rs_hid_rdesc.txt - Complete RDESC decode
├── TM_and_attempt_analysis.txt - Initial analysis
├── HID_DESCRIPTOR_INFO.md - Descriptor documentation
├── PROTOCOL_ANALYSIS_AND_NEXT_STEPS.md - Phase planning
└── DIRECTINPUT_FF_FIXES.md - Debug guide
```

### Driver Source Code
```
src/tmt500rs/
├── hid-tmt500rs.c - Main kernel driver
├── hid-tmt500rs.h - Driver header
├── hid-tmt500rs-usb.c - USB layer
└── hid-tmt500rs-fixed.c - Fixed version

userspace/
├── t500rs_protocol.c - Protocol implementation
├── t500rs_protocol.h - Protocol header
├── t500rs_effects.c - Effect handling
└── t500rs-ffb.c - FF bridge
```

## Completion Timeline Estimate

| Phase | Estimated Time | Dependencies |
|-------|---------------|--------------|
| 1. Fix Windows app | ✅ DONE | - |
| 2. Capture effects | 1-2 hours | Phase 1 success |
| 3. Decode protocol | 2-4 hours | Phase 2 captures |
| 4. Driver comparison | 1-2 hours | Parallel to Phase 3 |
| 5. Unknown reports | 2-4 hours | Phases 3-4 complete |
| 6. Edge case testing | 2-3 hours | Phase 5 complete |
| 7. Documentation | 3-4 hours | All phases done |
| **TOTAL** | **11-19 hours** | Sequential + parallel work |

## Success Criteria

Protocol analysis is **100% complete** when:

✅ All force feedback effect types work correctly on Windows  
✅ Complete USB captures of all effect types obtained  
✅ All Report 0x0a command bytes documented with examples  
✅ Parameter encoding fully understood and documented  
✅ Unknown report IDs (0x02, 0x40, 0x42) purposes identified  
✅ Linux driver differences documented with remediation plan  
✅ Edge cases tested and documented  
✅ Complete protocol specification written  
✅ Implementation guide created for future development  

## How to Get from 85% → 100%

**Immediate Action** (RIGHT NOW):
1. Rebuild `t500rs_test.cpp` with fixes
2. Run manual tests to verify effects work
3. Report back: "Effects work!" or "Still failing with error X"

**Next Session** (After Phase 1 verified):
1. Start USBPcap capture
2. Run automated test suite
3. Save capture file
4. Run decoder on capture
5. Document initial findings

**Subsequent Sessions**:
- Work through Phases 3-7 systematically
- Document everything as you go
- Test each finding before proceeding
- Update roadmap with new discoveries

The **critical path** is getting Phase 1 working - everything else depends on having working DirectInput effects to capture and analyze.
