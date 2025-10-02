# T500RS Force Feedback Userspace Driver

## Overview

This is a userspace force feedback driver for the Thrustmaster T500RS racing wheel.
It uses libusb for USB communication and uinput to create a virtual input device
with full force feedback support.

## Why Userspace?

The T500RS uses a proprietary protocol that doesn't fit the standard Linux HID model.
After extensive testing, we found that:
- ✅ libusb communication works perfectly
- ✅ Force feedback works via libusb
- ❌ Kernel HID layer blocks raw USB access

Therefore, a userspace driver is the correct solution for this device.

## Features

- ✅ **Full force feedback support** - Production ready!
- ✅ **Constant force effects** - Directional forces (road feel, bumps)
- ✅ **Spring effects** - Centering force (self-centering wheel)
- ✅ **Damper effects** - Resistance to movement
- ✅ **Friction/Inertia** - Additional resistance effects
- ✅ **Auto-detection** - Finds device automatically
- ✅ **Proper initialization** - Complete USB setup sequence
- ✅ **Clean shutdown** - Proper cleanup on exit
- ✅ **No kernel patches** - Works on any Linux distro

## Requirements

- libusb-1.0
- Linux kernel with uinput support
- Root/sudo access (for USB and uinput)

## Installation

### Arch Linux

```bash
# Install dependencies
sudo pacman -S libusb

# Build
cd userspace
make

# Install (optional)
sudo make install
```

### Ubuntu/Debian

```bash
# Install dependencies
sudo apt-get install libusb-1.0-0-dev

# Build
cd userspace
make

# Install (optional)
sudo make install
```

## Usage

### Basic Usage

```bash
# Unload kernel driver if loaded
sudo rmmod hid_tmff_new

# Run the driver
sudo ./t500rs-ffb
```

The driver will:
1. Initialize the T500RS
2. Create a virtual input device (`/dev/input/eventX`)
3. Handle force feedback effects
4. Run until you press Ctrl+C

### Finding the Device

After starting the driver, check dmesg to find the device number:

```bash
dmesg | grep "T500RS"
```

You should see something like:
```
input: Thrustmaster T500RS (FFB) as /dev/input/event25
```

### Testing Force Feedback

Use `fftest` to test force feedback:

```bash
# Install fftest
sudo pacman -S linuxconsole  # Arch
sudo apt-get install joystick  # Ubuntu

# Test (replace event25 with your device number)
fftest /dev/input/event25
```

## Troubleshooting

### "Cannot open device"

Make sure the T500RS is connected:
```bash
lsusb | grep 044f:b65e
```

### "Failed to detach kernel driver"

Unload the kernel driver first:
```bash
sudo rmmod hid_tmff_new
```

### "Failed to open /dev/uinput"

Make sure uinput module is loaded:
```bash
sudo modprobe uinput
```

### No force feedback

1. Check that the driver initialized successfully
2. Use `fftest` to upload and play effects
3. Check dmesg for errors

## How It Works

```
┌─────────────────────────────────────────┐
│         Game / Application              │
│    (reads /dev/input/eventX)            │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│      Linux Input Subsystem              │
│         (uinput device)                 │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│    t500rs-ffb Userspace Daemon          │
│  - Handles FF upload/play/stop          │
│  - Sends USB commands via libusb        │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│         T500RS Hardware                 │
│    (via libusb, endpoint 0x01)          │
└─────────────────────────────────────────┘
```

## Protocol

The T500RS uses a multi-report protocol:

### Initialization
- Report 0x42: Device init
- Report 0x0a: Configuration (3 variants)
- Report 0x40: Control commands

### Force Feedback
- Report 0x02: Effect parameters (9 bytes)
- Report 0x04: Effect parameters (8 bytes)
- Report 0x01: Main effect data (15 bytes)
- Report 0x41: Effect control (start/stop)

See `../T500RS_PROTOCOL.md` for complete protocol documentation.

## Development

### Building

```bash
make
```

### Debugging

The driver outputs detailed logs to stdout/stderr. Run it in a terminal to see:
- Initialization progress
- Effect uploads
- USB communication
- Errors

### Adding Effect Types

To add support for new effect types:

1. Add upload function (see `upload_constant_effect` as example)
2. Add case in `handle_ff_upload()`
3. Enable the effect type in `setup_uinput()`

## Known Limitations

- Input events (buttons/axes) are not forwarded yet
  - The driver only handles force feedback
  - Use the real device for input (it still works)
- Some effect types not implemented yet
  - Periodic effects
  - Ramp effects
  - Custom effects

## Future Improvements

- [ ] Forward input events from real device
- [ ] Implement all effect types
- [ ] Add configuration file support
- [ ] Create systemd service
- [ ] Auto-start on device plug (udev rule)
- [ ] GUI configuration tool

## License

GPL v2 (same as Linux kernel drivers)

## Credits

- Protocol analysis from Windows USB captures
- Based on libusb and uinput documentation
- Inspired by other userspace force feedback drivers

## Support

For issues, questions, or contributions:
- Check the main repository documentation
- See `../NEXT_STEPS_PLAN.md` for development roadmap
- See `../T500RS_PROTOCOL.md` for protocol details

## Changelog

### Version 1.0 (2025-10-02)
- Initial release
- Basic force feedback support
- Constant and spring effects
- Proper initialization sequence

