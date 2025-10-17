# TMT500RS Test Troubleshooting

## Current Test Status (2025-01-14)
- Build: ✅ Successful
- Module Loading: ⚠️ Partial Success
- Test Execution: ❌ Failed
- Test Results: ❌ Tests failing (0/9)

## Test Coverage
1. Device Initialization
   - [x] Device initialization
   - [x] API population
   - [x] Wheel initialization
   - [x] Wheel fixup
   - [x] Device open

2. Force Feedback Effects
   - [x] Constant force effect
   - [x] Spring effect
   - [x] Damper effect
   - [x] Friction effect
   - [x] Periodic effects
     - [x] Sine wave
     - [x] Square wave
     - [x] Triangle wave
     - [x] Saw up wave
     - [x] Saw down wave

3. Error Handling
   - [x] Invalid effect type
   - [x] NULL device
   - [x] NULL effect
   - [x] Uninitialized device
   - [x] Resource exhaustion
   - [x] Invalid parameters
     - [x] Invalid waveform
     - [x] Invalid magnitude
     - [x] Invalid coefficients

4. Resource Management
   - [x] Device cleanup
   - [x] Resource deallocation
   - [x] Error recovery
   - [x] Stress testing

5. Effect Combinations
   - [x] Constant force with periodic effect
   - [x] Spring with damper effect
   - [x] Multiple effects simultaneously
   - [x] Rapid effect changes

6. Edge Cases
   - [x] Maximum magnitude
   - [x] Minimum magnitude
   - [x] All waveform types
   - [x] Rapid effect switching
   - [x] Resource limits

## Recent Issues and Solutions

1. Test Module Loading
   - Issue: Module not loading with modprobe
   - Solution: Use insmod directly with the module path

2. Test Detection
   - Issue: Test completion not detected
   - Solution: Updated test completion detection strings in run_tests.sh

3. Invalid Effect Type Test
   - Issue: Test failing with FF_PERIODIC + 1
   - Solution: Use FF_MAX_EFFECTS + 1 for invalid effect type

4. Permission Issues
   - Issue: dmesg access denied
   - Solution: Added sudo to all dmesg commands in run_tests.sh

5. USB Initialization Issues (2025-01-14)
   - Issue: Device disconnecting during initialization
   - Solution: Added delays between initialization steps
   - Issue: URB submission errors (-19)
   - Solution: Modified probe function to use minimal features during init
   - Issue: hid-generic binding conflict
   - Solution: Added explicit unbinding from hid-generic

6. Mode Switch Cycling (2025-01-15)
   - Issue: Device cycling between b65d and not reaching b65e mode
   - Root Cause: Improper state machine transitions and timing
   - Solution: 
     - Added proper state verification before mode switch
     - Increased delays between USB operations
     - Added explicit state tracking
     - Added retry mechanism with backoff
     - Added URB cleanup before mode switch
     - Added mode switch completion verification
     - Added proper error recovery

7. Duplicate Debug Parameter (2025-01-15)
   - Issue: sysfs error - cannot create duplicate filename '/module/hid_tmff_new/parameters/debug'
   - Root Cause: TMT500RS module including <linux/hid-debug.h> while also using main driver's debug parameter
   - Solution:
     - Removed unnecessary <linux/hid-debug.h> includes from TMT500RS module files
     - Added extern bool debug declaration in main driver's header file (hid-tmff2.h)
     - Kept extern bool debug declaration in TMT500RS module header file
     - Verified debug functionality still works through main driver's parameter
   - Status: ✅ Fixed - Verified debug parameter is properly shared between modules

8. Device Cycling During Mode Switch Test (2025-01-15)
   - Issue: Device repeatedly disconnecting and reconnecting during mode switch test
   - Root Cause: Mode switch state machine not properly handling device reconnection
   - Symptoms:
     - Device detected as b65d but not transitioning to b65e
     - Multiple USB device number increments (56, 59, 61, etc.)
     - Test timeout due to device not stabilizing
   - Solution:
     - Added proper USB power management handling during mode switch
     - Disabled USB autosuspend during device reconnection phase
     - Added explicit USB interface management
     - Increased stabilization delays
     - Added additional state verification checks
     - Added proper error recovery with retries
   - Status: ⚠️ In Progress - Testing solution effectiveness

9. Kernel Oops During Mode Switch (2025-01-15)
   - Issue: Kernel oops during device disconnection phase of mode switch
   - Root Cause: USB power management issue during device disconnection
   - Symptoms:
     - Kernel oops in usb_autopm_put_interface
     - Unable to handle page fault at address 0x579768
     - Device state corruption during power management
   - Next Steps:
     - Disable USB autosuspend during mode switch
     - Add proper USB power management handling
     - Review USB interface cleanup sequence
     - Add additional error handling for power management failures
     - Consider implementing manual power management control

## T500RS URB Submission Failure (-19) Investigation

### Current Symptoms
- URB submission consistently fails with error -19 (ENODEV - No such device)
- Increasing delays and retries does not resolve the issue
- Error occurs during device initialization phase

### USB Capture Analysis
From device_init_detailed.txt:
1. Normal initialization shows:
   - Control transfers (0x02) to endpoint 0x80
   - Interrupt IN transfers (0x01) to endpoint 0x82
   - No -19 errors present in working capture
   - Clear alternation between control and interrupt transfers

### Investigation Plan

#### Phase 1: USB Stack State Verification
1. [ ] Verify USB device state when URB submission is attempted
   - Log usb_get_dev_state() before submission
   - Check if device is in USB_STATE_CONFIGURED
   - Verify endpoint 0x82 exists and is enabled

2. [ ] Monitor USB disconnect/reconnect events
   - Add usbcore.autosuspend=-1 to kernel parameters
   - Log all USB state transitions
   - Check for premature disconnects

#### Phase 2: URB Configuration Validation
1. [ ] Verify URB setup matches captured traffic
   - Compare endpoint numbers (0x82)
   - Validate transfer_buffer size
   - Check interval timing
   - Verify transfer_flags

2. [ ] Test with minimal URB configuration
   - Remove DMA mapping initially
   - Test without URB_NO_TRANSFER_DMA_MAP
   - Try different interval values

#### Phase 3: Timing Analysis
1. [ ] Capture detailed timing of USB operations
   - Log timestamps of all USB operations
   - Compare with working capture timing
   - Identify any race conditions

2. [ ] Profile driver initialization sequence
   - Log all delays and their impact
   - Compare with working device timing
   - Identify critical timing dependencies

#### Phase 4: Hardware State Management
1. [ ] Verify hardware mode state
   - Log current mode before URB submission
   - Verify endpoint configuration matches mode
   - Check for mode transition issues

2. [ ] Test with mode_switch_test.sh
   - Run full test sequence
   - Log all state transitions
   - Verify cleanup effectiveness

### Required Additional Data
1. USB captures needed:
   - Full initialization sequence with -19 error
   - Capture with usbmon at debug level
   - Compare with working capture

2. Kernel logs needed:
   - Full USB subsystem debug logs
   - Module loading sequence
   - USB state transitions

### Test Methodology
1. Use mode_switch_test.sh as base
2. Add detailed logging points
3. Compare against known working state
4. Document all test cases and results

### Success Criteria
1. Identify exact point of URB submission failure
2. Understand USB state at failure point
3. Determine if issue is:
   - Timing related
   - State management related
   - Hardware protocol related
   - Driver initialization related

### Next Steps
1. [ ] Implement Phase 1 logging
2. [ ] Gather additional USB captures
3. [ ] Review USB state management
4. [ ] Test with mode_switch_test.sh

## Next Steps
1. Add more test categories:
   - [x] Effect combinations
   - [x] Edge cases
   - [ ] Performance testing
   - [ ] Long-running tests
   - [x] Mode switch stability testing

2. Improve test framework:
   - [x] Better progress display
   - [x] Enhanced error reporting
   - [x] Automatic log saving
   - [ ] Test coverage reporting
   - [x] Mode switch verification

3. Documentation:
   - [ ] Document test procedures
   - [ ] Add debugging guidelines
   - [ ] Update test framework documentation

## Progress Tracking
- Initial Implementation: ✅ Complete
- Basic Test Suite: ✅ Complete
- Test Reporting: ✅ Complete
- Force Feedback Tests: ❌ Failing
- Extended Test Coverage: ⚠️ Partial
- Documentation: 🔄 In Progress

## Recent Updates
- Added effect combination tests
- Added edge case tests
- Added comprehensive parameter validation
- Added all waveform type tests
- Added rapid effect change tests
- Added USB initialization troubleshooting
- Modified probe function for better device stability
- Added device state transition delays
- Updated error handling for USB issues

## Next Review
After resolving USB initialization issues
