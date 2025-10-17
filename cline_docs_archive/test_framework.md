# TMT500RS Test Framework Documentation

## Overview
The TMT500RS test framework is designed to validate the functionality of the TMT500RS force feedback wheel driver. It provides comprehensive testing of device initialization, force feedback effects, error handling, resource management, effect combinations, and edge cases.

## Test Categories

### 1. Device Initialization
Tests the proper initialization of the device, including:
- Device structure allocation and setup
- USB interface initialization
- HID device initialization
- Device cleanup and resource deallocation

### 2. Force Feedback Effects
Validates all supported force feedback effects:
- Constant force effects
- Spring effects
- Damper effects
- Friction effects
- Periodic effects (Sine, Square, Triangle, Saw Up, Saw Down)

### 3. Error Handling
Tests error handling and validation:
- Invalid effect types
- NULL device handling
- NULL effect handling
- Uninitialized device handling
- Resource exhaustion
- Invalid parameters
  - Invalid waveforms
  - Invalid magnitudes
  - Invalid coefficients

### 4. Resource Management
Tests proper resource management:
- Device cleanup
- Resource deallocation
- Error recovery
- Stress testing

### 5. Effect Combinations
Tests combinations of effects:
- Constant force with periodic effects
- Spring with damper effects
- Multiple simultaneous effects
- Rapid effect changes

### 6. Edge Cases
Tests boundary conditions:
- Maximum magnitude
- Minimum magnitude
- All waveform types
- Rapid effect switching
- Resource limits

## Test Coverage Reporting
The framework includes comprehensive test coverage reporting:
- Per-category test statistics
- Overall test coverage
- Pass/fail rates
- Execution statistics

## Running Tests

### Prerequisites
- Linux kernel headers
- Build tools (make, gcc)
- Root privileges for module loading

### Build Instructions
```bash
# Clean previous build
make clean

# Build the test module
make
```

### Running Tests
```bash
# Run the test suite
./run_tests.sh
```

### Test Output
The test output includes:
- Test progress
- Test results for each category
- Test coverage report
- Overall test summary

### Log Files
Test logs are saved in the `logs` directory:
- `dmesg_YYYYMMDD_HHMMSS.log`: Kernel log output
- `test_YYYYMMDD_HHMMSS.log`: Test results

## Troubleshooting

### Common Issues

1. Module Loading Failures
   - Issue: Module fails to load with modprobe
   - Solution: Use insmod directly with the module path

2. Permission Issues
   - Issue: dmesg access denied
   - Solution: Run with sudo or add user to appropriate group

3. Build Errors
   - Issue: Missing kernel headers
   - Solution: Install appropriate kernel headers package

### Debug Tips
1. Enable kernel debug messages:
   ```bash
   echo 8 > /proc/sys/kernel/printk
   ```

2. Monitor kernel log in real-time:
   ```bash
   sudo dmesg -w
   ```

3. Check module dependencies:
   ```bash
   lsmod | grep tmff
   ```

## Adding New Tests

### Test Structure
1. Define test function:
   ```c
   static int test_new_feature(struct mock_t500rs_device *mock_dev)
   {
       // Test implementation
   }
   ```

2. Add test to main sequence:
   ```c
   test_stats.current_phase++;
   ret = test_new_feature(test_mock_dev);
   test_stats.total_tests++;
   if (ret == 0) {
       pr_info("New feature test passed\n");
       test_stats.passed_tests++;
   } else {
       pr_err("New feature test failed: %d\n", ret);
       test_stats.failed_tests++;
   }
   ```

3. Update test coverage:
   ```c
   record_test_result(&coverage_report, TC_CATEGORY, ret == 0);
   ```

### Best Practices
1. Keep tests focused and independent
2. Add proper error handling
3. Clean up resources after tests
4. Add descriptive logging
5. Update documentation for new tests

## Future Enhancements
1. Performance testing
2. Long-running tests
3. Automated regression testing
4. Hardware simulation improvements
5. Extended coverage metrics 