# T500RS Userspace Driver - Status

## Current Status

### ✅ Mode Switch - WORKING
The device successfully switches from boot mode (044f:b65d) to normal mode (044f:b65e) automatically when the driver starts.

**What was fixed:**
- Added USB control requests (bRequest 73 and 83) to trigger mode switch
- Added normal-mode initialization commands after device re-enumeration
- Removed problematic USB reset logic that was causing interface claim errors

### ✅ Force Feedback - WORKING
Force feedback is fully functional. Confirmed working with `test_all_effects` program.

**Supported effects:**
- Constant force
- Spring, Damper, Friction, Inertia
- Periodic (sine, square, triangle, sawtooth)
- Gain control
- Autocenter

### ✅ Input - WORKING
- Steering wheel (16-bit precision)
- 3 pedals (throttle, brake, clutch)
- 16 buttons
- D-pad

## Files

### Core Driver
- `t500rs-ffb.c` - Main driver with mode switch and FFB support
- `t500rs-ffb` - Compiled binary

### Testing Tools
- `test_all_effects` - C program for comprehensive FFB testing (standalone)
- `test_ff_python.py` - Python FFB test script with auto-detection
- `t500rs_control.py` - GUI control panel (PyQt5)

### Utility Scripts  
- `find_device.sh` - Find T500RS device automatically
- `emergency_reset.sh` - Stop all effects and reset wheel
- `run.sh` - Start driver with proper setup

## Usage

### Quick Start
```bash
# Compile (if needed)
make

# Run driver
sudo ./t500rs-ffb

# Test force feedback
sudo ./test_all_effects
# or
sudo ./test_ff_python.py

# GUI control panel
sudo python3 t500rs_control.py
```

### Device Path
The driver creates a virtual input device at `/dev/input/event259` (or similar).

Find it with:
```bash
./find_device.sh
# or manually:
cat /sys/class/input/event*/device/name | grep -i t500
```

## Testing

### Test FFB with C program
```bash
# Auto-detect device
sudo ./test_all_effects

# Or specify device
sudo ./test_all_effects /dev/input/event259

# Run all tests
sudo ./test_all_effects --all
```

### Test FFB with Python
```bash
sudo ./test_ff_python.py
```

## Known Issues

### Python test script EVIOCRMFF error
The `test_ff_python.py` script may show an error when removing effects. This is harmless and doesn't affect functionality. The force feedback still works correctly.

## Mode Switch Details

The mode switch works as follows:

1. **Boot Mode (044f:b65d):**
   - Device powers on in this mode
   - Limited functionality
   - Requires initialization

2. **Initialization Sequence:**
   - Send interrupt transfers (Reports 0x42, 0x0a, 0x40, etc.)
   - Send USB control request to query model (bRequest 73)
   - Send USB control request to switch mode (bRequest 83, wValue=0x0002)
   - Device disconnects and re-enumerates

3. **Normal Mode (044f:b65e):**
   - Device reconnects with new PID
   - Driver detects and claims the device
   - Sends normal-mode initialization commands
   - Full force feedback functionality available

## Troubleshooting

### Device stuck in boot mode
```bash
# Check current mode
lsusb | grep -i thrust

# If shows b65d (boot mode), restart driver
sudo pkill -9 t500rs-ffb
sudo ./t500rs-ffb
```

### No force feedback
1. Verify device is in normal mode: `lsusb | grep 044f:b65e`
2. Check driver is running: `ps aux | grep t500rs-ffb`
3. Test with: `sudo ./test_all_effects`

### Driver won't start
```bash
# Kill any existing instances
sudo pkill -9 t500rs-ffb

# Check if kernel driver is attached
lsusb -t | grep -A2 "044f:b65e"

# If usbhid is attached, unbind it
echo "2-1.3:1.0" | sudo tee /sys/bus/usb/drivers/usbhid/unbind
```

## Auto-start on Boot

See `README.md` for instructions on setting up systemd service for automatic driver startup.

## Documentation

- `MODE_SWITCH_FIX.md` - Detailed explanation of mode switch fix
- `README.md` - Complete usage and installation guide
- `t500rs_control.py` - GUI with real-time monitoring and profiles
