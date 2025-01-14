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
- [x] Device mode configuration
- [x] Initial state verification

### 2. Force Feedback Effects
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

### 3. Effect Combinations
- [ ] Constant force + Spring
- [ ] Constant force + Damper
- [ ] Spring + Friction
- [ ] Multiple periodic effects
- [ ] Complex effect sequences

### 4. Performance Validation
- [ ] Effect latency measurement
- [ ] USB bandwidth utilization
- [ ] CPU usage monitoring
- [ ] Memory usage tracking
- [ ] Long-term stability test

### 5. Error Recovery
- [ ] USB disconnect/reconnect
- [ ] Power cycle recovery
- [ ] Invalid effect handling
- [ ] Resource exhaustion
- [ ] Concurrent access

## Test Procedures

### Device Detection Test
1. Connect device via USB
2. Monitor dmesg output
3. Verify device enumeration
4. Check driver binding
5. Validate initial state

### Force Feedback Test
1. Use fftest utility:
   ```bash
   fftest /dev/input/eventX
   ```
2. For each effect type:
   - Upload effect
   - Start effect
   - Verify physical response
   - Modify parameters
   - Stop effect
   - Record results

### Effect Combination Test
1. Use custom test program:
   ```bash
   ./ff_combo_test /dev/input/eventX
   ```
2. Monitor physical response
3. Verify effect interactions
4. Check resource usage

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
2. Record baseline measurements
3. Run stress tests
4. Compare results

### Error Recovery Test
1. Simulate error conditions
2. Monitor recovery behavior
3. Verify device state
4. Check system stability

## Test Environment Setup
```bash
# Load required modules
sudo modprobe usbmon
sudo modprobe hid-tmff-new

# Start USB monitoring
sudo wireshark -k -i usbmon0

# Set up test device
sudo chmod 666 /dev/input/eventX
```

## Validation Tools
1. fftest - Force feedback testing utility
2. evtest - Input event monitoring
3. usbmon/wireshark - USB traffic analysis
4. custom_ff_test - Custom test suite
5. system_monitor.sh - Resource monitoring script

## Expected Results
1. Device Detection
   - Clean dmesg output
   - Proper device nodes
   - Correct permissions

2. Force Feedback
   - Smooth effect execution
   - Accurate force levels
   - Proper effect timing

3. Performance
   - < 2ms latency
   - < 10% CPU usage
   - Stable memory usage
   - No USB bandwidth issues

4. Error Handling
   - Clean recovery
   - No resource leaks
   - Proper error reporting

## Result Recording
Create detailed test reports including:
1. Test environment details
2. Test case results
3. Performance metrics
4. Error logs
5. USB captures
6. System resource usage

## Troubleshooting Guide
1. USB Issues
   - Check dmesg
   - Verify USB port power
   - Test different USB ports

2. Effect Issues
   - Monitor USB traffic
   - Check effect parameters
   - Verify driver state

3. Performance Issues
   - Check system load
   - Monitor USB bandwidth
   - Review IRQ handling

## Next Steps
1. Execute validation plan
2. Document results
3. Address any issues
4. Update driver if needed
5. Prepare release notes 