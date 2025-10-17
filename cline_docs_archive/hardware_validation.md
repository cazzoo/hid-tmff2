# TMT500RS Hardware Validation Plan

## Overview
This document outlines the hardware validation plan for the TMT500RS force feedback wheel driver. The validation will verify that the driver works correctly with actual hardware, focusing on USB communication, force feedback effects, and error handling.

## Prerequisites
1. Hardware Requirements
   - TMT500RS force feedback wheel
   - USB connection to test system
   - Stable power supply
   - No other force feedback drivers loaded

2. Software Requirements
   - Built driver modules (hid-tmff-new.ko)
   - Test utilities (fftest, evtest)
   - USB monitoring tools (usbmon, wireshark)
   - System with proper kernel headers

## Test Categories

### 1. Device Detection and Initialization
- [x] USB device enumeration
- [x] Driver binding
- [x] HID report descriptor parsing
- [ ] Device mode configuration
  - [ ] Initial mode detection (b65d)
  - [ ] Mode switch handling (b65d -> b65e)
  - [ ] Mode stability verification
- [ ] Initial state verification

### 2. USB Communication Stability
- [ ] URB submission reliability
  - [ ] During initialization
  - [ ] During mode switches
  - [ ] During normal operation
- [ ] Error recovery
  - [ ] From URB failures
  - [ ] From mode switch failures
  - [ ] From disconnects
- [ ] Timing analysis
  - [ ] Mode switch timing
  - [ ] Command response timing
  - [ ] Effect upload timing

### 3. Force Feedback Effects
Test each effect with actual hardware:
- [ ] Constant Force
  - [ ] Different force levels (25%, 50%, 75%, 100%)
  - [ ] Direction changes
  - [ ] Smooth transitions
- [ ] Spring Effect
  - [ ] Various stiffness levels
  - [ ] Center point accuracy
  - [ ] Dead zone behavior
- [ ] Damper Effect
  - [ ] Different damping levels
  - [ ] Speed sensitivity
  - [ ] Direction independence
- [ ] Friction Effect
  - [ ] Various friction levels
  - [ ] Movement resistance
  - [ ] Static vs dynamic friction
- [ ] Periodic Effects
  - [ ] Sine wave
  - [ ] Square wave
  - [ ] Triangle wave
  - [ ] Saw up wave
  - [ ] Saw down wave
  - [ ] Different magnitudes
  - [ ] Different periods

### 4. Effect Combinations
- [ ] Constant force + Spring
- [ ] Constant force + Damper
- [ ] Spring + Friction
- [ ] Multiple periodic effects
- [ ] Complex effect sequences

### 5. Performance Validation
- [ ] Effect latency measurement
- [ ] USB bandwidth utilization
- [ ] CPU usage monitoring
- [ ] Memory usage tracking
- [ ] Long-term stability test

### 6. Error Recovery
- [ ] USB disconnect/reconnect
- [ ] Power cycle recovery
- [ ] Invalid effect handling
- [ ] Resource exhaustion
- [ ] Concurrent access

## Current Issues (2024-03-20)

### Critical Issues
1. Mode Switch Instability
   - Device cycles between b65d and b65e modes
   - URB submission failures during mode switch
   - Inconsistent initialization sequence

### Required Fixes
1. USB Communication
   - [ ] Implement proper mode switch detection
   - [ ] Add mode switch state machine
   - [ ] Improve URB error handling
   - [ ] Add proper delays in initialization sequence

2. Device Initialization
   - [ ] Verify initial mode before switching
   - [ ] Add mode switch completion verification
   - [ ] Implement retry mechanism for failed mode switches
   - [ ] Add proper state tracking

3. Error Recovery
   - [ ] Improve disconnect handling
   - [ ] Add reconnect recovery
   - [ ] Implement proper cleanup on errors

## Test Procedures

### Mode Switch Test
1. Initial connection test:
   ```bash
   # Monitor USB traffic
   sudo usbmon -i
   # Connect device
   # Verify initial mode (b65d)
   ```

2. Mode switch test:
   ```bash
   # Monitor mode switch
   sudo usbmon -i
   # Initiate mode switch
   # Verify final mode (b65e)
   ```

3. Stability test:
   ```bash
   # Run continuous test
   ./mode_switch_test.sh
   # Monitor for failures
   dmesg -w
   ```

### Force Feedback Test
1. Use fftest utility:
   ```bash
   fftest /dev/input/eventX
   ```

### Performance Test
1. Monitor system metrics:
   ```bash
   # USB traffic
   cat /sys/kernel/debug/usb/usbmon/0u
   # CPU usage
   top -p `pidof ff_test`
   # Memory usage
   cat /proc/`pidof ff_test`/status
   ```

## Next Steps
1. Implement mode switch fixes
2. Add comprehensive mode switch testing
3. Improve error recovery
4. Add performance monitoring
5. Complete force feedback testing

## Progress Tracking
- Initial Implementation: ✅ Complete
- USB Communication: 🔄 In Progress
- Mode Switch Handling: ❌ Not Started
- Force Feedback: ❌ Not Started
- Error Recovery: ❌ Not Started
- Documentation: 🔄 In Progress 