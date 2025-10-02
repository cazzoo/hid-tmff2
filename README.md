# T500RS Linux Force Feedback Driver

**Full force feedback support for Thrustmaster T500RS racing wheel on Linux!** 🏁🎮

## Status: ✅ WORKING!

This repository contains a complete, working force feedback driver for the Thrustmaster T500RS racing wheel on Linux.

## Quick Start

```bash
cd userspace
sudo ./run.sh
```

That's it! Force feedback will work. See [Usage Guide](userspace/USAGE_GUIDE.md) for details.

## Features

- ✅ **Full force feedback support**
- ✅ **Constant force effects**
- ✅ **Spring/damper effects**
- ✅ **Proper device initialization**
- ✅ **No kernel patches required**
- ✅ **Works on any Linux distro**
- ✅ **Easy to install and use**

## Installation

### Arch Linux

```bash
# Install dependencies
sudo pacman -S libusb

# Build
cd userspace
make

# Run
sudo ./run.sh
```

### Ubuntu/Debian

```bash
# Install dependencies
sudo apt-get install libusb-1.0-0-dev

# Build
cd userspace
make

# Run
sudo ./run.sh
```

## Testing

```bash
# Start the driver
cd userspace
sudo ./run.sh

# In another terminal, test force feedback
dmesg | tail | grep "T500RS"  # Find device number
fftest /dev/input/eventXX     # Test FF (replace XX with your number)
```

## Documentation

### User Documentation
- **[Usage Guide](userspace/USAGE_GUIDE.md)** - Detailed usage instructions
- **[Driver README](userspace/README.md)** - Driver overview

### Developer Documentation
- **[Protocol Specification](T500RS_PROTOCOL.md)** - Complete T500RS protocol
- **[Implementation Complete](USERSPACE_DRIVER_COMPLETE.md)** - Development summary
- **[Next Steps](NEXT_STEPS_PLAN.md)** - Future improvements

## How It Works

The T500RS uses a proprietary protocol that doesn't fit the standard Linux HID model. This driver uses libusb for direct USB communication and uinput to create a virtual input device with force feedback support.

```
Game → Linux Input → uinput → t500rs-ffb → libusb → T500RS Hardware
```

## Why Userspace?

After extensive testing, we found that:
- ❌ Kernel HID layer blocks raw USB access (error -11)
- ✅ libusb works perfectly
- ✅ Userspace is easier to maintain and debug
- ✅ No kernel patches or recompilation needed

## Troubleshooting

See [Usage Guide](userspace/USAGE_GUIDE.md) for detailed troubleshooting.

Quick fixes:
- **"Cannot open device"**: Check `lsusb | grep 044f:b65e`
- **"Failed to detach kernel driver"**: Run `sudo rmmod hid_tmff_new`
- **"Failed to open /dev/uinput"**: Run `sudo modprobe uinput`

## Project Structure

```
userspace/          # Userspace driver (THE SOLUTION)
├── t500rs-ffb.c   # Main driver
├── run.sh         # Quick start script
└── README.md      # Documentation

docs/              # Protocol documentation
test_*.c           # Test programs
src/tmt500rs/      # Kernel attempts (reference only)
```

## License

GPL v2

## Credits

- Protocol reverse engineering from Windows USB captures
- Based on libusb and uinput documentation
- Thanks to the Linux gaming community

---

**Enjoy your T500RS with full force feedback on Linux!** 🏁🎮

