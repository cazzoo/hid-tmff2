# T500RS Driver Tests

This directory contains all test programs and utilities for the T500RS driver.

## Directory Structure

```
tests/
├── c/                  - C test programs
├── python/             - Python test scripts
├── scripts/            - Utility scripts
├── Makefile            - Build system for C tests
└── README.md           - This file
```

---

## C Test Programs (tests/c/)

### Building Tests

```bash
cd tests
make                    # Build all tests
make clean              # Clean compiled tests
```

### Available Tests

#### test_all_effects
**Purpose**: Comprehensive test of all force feedback effect types

**Usage**:
```bash
sudo ./c/test_all_effects
```

**Tests**:
- Constant force (left/right)
- Periodic effects (sine, square, triangle, saw up/down)
- Spring effect
- Damper effect
- Friction effect
- Inertia effect

**Duration**: ~2 minutes

---

#### test_direction
**Purpose**: Test force direction accuracy

**Usage**:
```bash
sudo ./c/test_direction
```

**Tests**:
- 8 directions (N, NE, E, SE, S, SW, W, NW)
- Verifies correct force direction mapping

**Duration**: ~30 seconds

---

#### test_envelope
**Purpose**: Test envelope (attack/fade) processing

**Usage**:
```bash
sudo ./c/test_envelope
```

**Tests**:
- Attack phase (gradual force increase)
- Sustain phase (constant force)
- Fade phase (gradual force decrease)

**Duration**: ~20 seconds

---

#### test_input_reading
**Purpose**: Test input reading (steering, pedals, buttons)

**Usage**:
```bash
sudo ./c/test_input_reading
```

**Output**: Real-time display of all inputs

**Duration**: Runs until Ctrl+C

---

#### test_multi_effect
**Purpose**: Test multiple simultaneous effects

**Usage**:
```bash
sudo ./c/test_multi_effect
```

**Tests**:
- Multiple effects playing simultaneously
- Effect mixing
- Effect priority

**Duration**: ~30 seconds

---

## Python Test Scripts (tests/python/)

### Requirements

```bash
pip install evdev
```

### Available Scripts

#### test_direction.py
Test force direction with Python

```bash
python3 python/test_direction.py
```

#### test_direction_values.py
Test direction value calculations

```bash
python3 python/test_direction_values.py
```

#### test_config_events.py
Test configuration event handling

```bash
python3 python/test_config_events.py
```

#### test_ff_python.py
General force feedback test in Python

```bash
python3 python/test_ff_python.py
```

#### test_ff_working.py
Verify force feedback is working

```bash
python3 python/test_ff_working.py
```

#### monitor_events.py
Monitor force feedback events in real-time

```bash
python3 python/monitor_events.py
```

#### record_replay.py
Record and replay force feedback sequences

```bash
# Record
python3 python/record_replay.py --record output.json

# Replay
python3 python/record_replay.py --replay output.json
```

---

## Utility Scripts (tests/scripts/)

### emergency_reset.sh
Emergency device reset

```bash
./scripts/emergency_reset.sh
```

### find_device.sh
Find T500RS device event number

```bash
./scripts/find_device.sh
```

### run.sh
Quick run script for the driver

```bash
./scripts/run.sh
```

### verify_build.sh
Verify driver build

```bash
./scripts/verify_build.sh
```

---

## Running Tests

### Quick Test Suite

```bash
# Build driver
cd ..
make -f Makefile.modular

# Build tests
cd tests
make

# Run driver in background
sudo ../t500rs-ffb-modular &

# Run comprehensive test
sudo ./c/test_all_effects

# Stop driver
sudo pkill -INT t500rs-ffb
```

### Individual Tests

```bash
# Test specific effect type
sudo ./c/test_direction

# Test envelope processing
sudo ./c/test_envelope

# Test multi-effect mixing
sudo ./c/test_multi_effect

# Monitor inputs
sudo ./c/test_input_reading
```

---

## Test Results

All tests should:
- ✅ Complete without errors
- ✅ Produce expected force feedback
- ✅ Show correct input values
- ✅ Exit cleanly

### Expected Output

Each test displays:
- Test name and description
- Progress indicators
- Force values being sent
- Success/failure status

### Troubleshooting

**No force feedback**:
- Check device connection (`lsusb | grep 044f`)
- Verify driver is running
- Check gain settings
- Try with `fftest` utility

**Permission denied**:
- Run with `sudo`
- Check udev rules

**Device not found**:
- Ensure driver is running
- Check `/dev/input/eventX` exists
- Use `find_device.sh` to locate device

---

## Adding New Tests

### C Test Program

1. Create `tests/c/test_newfeature.c`
2. Add to Makefile:
   ```makefile
   $(C_DIR)/test_newfeature: $(C_DIR)/test_newfeature.c
       $(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)
   ```
3. Add to TESTS variable
4. Document in this README

### Python Test Script

1. Create `tests/python/test_newfeature.py`
2. Add shebang: `#!/usr/bin/env python3`
3. Make executable: `chmod +x tests/python/test_newfeature.py`
4. Document in this README

---

## Test Coverage

### Functionality Tested
- ✅ All effect types
- ✅ Force direction
- ✅ Envelope processing
- ✅ Multi-effect mixing
- ✅ Input reading
- ✅ Configuration events

### Not Yet Tested
- ⏳ Gain control
- ⏳ Autocenter
- ⏳ Effect duration
- ⏳ Effect replay

---

## See Also

- [Main README](../README.md) - Driver documentation
- [Development Guide](../docs/development/TESTING_QUICKSTART.md) - Testing quick start
- [Protocol Reference](../docs/technical/FFB_PROTOCOL_COMPLETE.md) - Technical details

