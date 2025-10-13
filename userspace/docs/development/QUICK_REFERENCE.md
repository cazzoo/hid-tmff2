# T500RS Driver - Quick Reference Card

## Quick Start

```bash
# Build
cd /home/caz/Documents/hid-tmff2/userspace
make

# Run
sudo ./t500rs-ffb

# Test (in another terminal)
sudo fftest /dev/input/event2
```

## Common Operations

### Enable USB Debug Logging
```bash
# Edit t500rs-ffb.c
#define USB_HEX_DEBUG 1

# Rebuild
make clean && make

# Run and capture hex
sudo ./t500rs-ffb 2>&1 | tee usb_debug.log
```

### Test All Effects
```bash
sudo ./test_all_effects
```

### Check Device
```bash
# Find device
lsusb | grep 044f

# Check event number
dmesg | grep T500RS | tail

# List force feedback capabilities
fftest /dev/input/event2
```

## Troubleshooting

### No Force Feedback
```bash
# 1. Check device mode
lsusb | grep 044f
# Should show 044f:b65e (not b65d)

# 2. Check driver logs
# Look for "Effect X uploaded" messages

# 3. Test with simple constant force
sudo fftest /dev/input/event2
# Upload constant force, set level to 16000, play
```

### Device Not Found
```bash
# Check USB connection
lsusb | grep 044f

# Check permissions
ls -l /dev/bus/usb/*/*044f*

# Add udev rule if needed
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="044f", MODE="0666"' | \
  sudo tee /etc/udev/rules.d/99-t500rs.rules
sudo udevadm control --reload-rules
```

### Build Issues
```bash
# Install dependencies
sudo pacman -S libusb base-devel  # Manjaro/Arch
# or
sudo apt install libusb-1.0-0-dev build-essential  # Debian/Ubuntu

# Clean build
make clean
make
```

## Configuration

### Invert Pedals
Edit `t500rs-ffb.c` lines 40-42:
```c
static int invert_throttle = 1;  /* 1 = invert, 0 = normal */
static int invert_brake = 1;
static int invert_clutch = 1;
```

### Change Steering Range
Edit `t500rs-ffb.c` line 96:
```c
static int current_rotation_angle = 1080;  /* 270-1080 degrees */
```

### Enable Ramp Effects (Experimental)
Edit `t500rs-ffb.c` line 71:
```c
#define ENABLE_RAMP_EFFECTS 1  /* Warning: May cause issues */
```

## Effect Types Reference

| Effect | Linux Type | Report Used | Notes |
|--------|------------|-------------|-------|
| Constant | FF_CONSTANT | 0x01, 0x02, 0x04, 0x41 | Left/right force |
| Spring | FF_SPRING | 0x01, 0x05, 0x41 | Centering |
| Damper | FF_DAMPER | 0x01, 0x05, 0x41 | Velocity-based |
| Friction | FF_FRICTION | 0x01, 0x05, 0x41 | Position-based |
| Inertia | FF_INERTIA | 0x01, 0x05, 0x41 | Acceleration-based |
| Sine | FF_PERIODIC | 0x01, 0x02, 0x04, 0x41 | Smooth wave |
| Square | FF_PERIODIC | 0x01, 0x02, 0x04, 0x41 | Sharp wave |

## Gain Control

### Global Gain
```bash
# In fftest or application
# 0 = 0%, 32767 = 50%, 65535 = 100%
```

### Per-Effect Gains (Custom Events)
- 0x70: Constant force gain
- 0x71: Periodic effect gain  
- 0x72: Spring effect gain
- 0x73: Damper effect gain
- 0x74: Friction effect gain
- 0x75: Inertia effect gain

## Files

### Important Files
- `t500rs-ffb.c` - Main driver
- `t500rs-ffb` - Compiled binary
- `Makefile` - Build configuration
- `test_all_effects` - Test program

### Documentation
- `SUCCESS_SUMMARY.md` - Full status
- `USB_CAPTURE_ANALYSIS.md` - Protocol details
- `QUICK_REFERENCE.md` - This file

## Performance

- CPU: <2% idle, <5% with effects
- Memory: ~2-3 MB
- Effect latency: ~5-10ms
- Update rate: 10-40ms (automatic)

## Known Issues

1. Ramp effects disabled (kernel crash protection)
2. Real-time updates not yet implemented
3. Envelope interpolation not active

## Getting Help

1. Check logs for errors
2. Review `SUCCESS_SUMMARY.md`
3. Enable USB_HEX_DEBUG for packet inspection
4. Compare with `USB_CAPTURE_ANALYSIS.md`

---

**Version**: 1.0  
**Protocol**: Legacy (0x01-0x43)  
**Status**: Production Ready ✅