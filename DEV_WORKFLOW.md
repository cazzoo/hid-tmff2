# T500RS Development Workflow

## Quick Reference

### Development Cycle

1. **Make code changes** in `src/tmt500rs/hid-tmt500rs-usb.c`
2. **Build**: `make`
3. **Install**: `sudo make install`
4. **Reload modules**: `sudo ./reload_modules.sh`
5. **Test**: Use `fftest` or launch a game

### Useful Scripts

#### `reload_modules.sh` - Reload driver modules
```bash
sudo ./reload_modules.sh
```
This script will:
- Unload all T500RS-related modules
- Wait for cleanup
- Reload init modules (hid_tminit_new, usb_tminit_new)
- Reload main driver (hid_tmff_new)
- Show module status and recent kernel logs
- Check for errors

### Manual Module Management

#### Check loaded modules
```bash
lsmod | grep -E "(hid_tmff|tminit)"
```

#### Unload modules (reverse order)
```bash
sudo modprobe -r hid_tmff_new
sudo modprobe -r usb_tminit_new
sudo modprobe -r hid_tminit_new
```

#### Load modules (correct order)
```bash
sudo modprobe hid_tminit_new
sudo modprobe usb_tminit_new
sleep 3  # Wait for device init
sudo modprobe hid_tmff_new
```

### Debugging Commands

#### View kernel logs (last 50 lines)
```bash
sudo dmesg | tail -50
```

#### View T500RS-specific logs
```bash
sudo dmesg | grep -i t500rs
```

#### View USB communication logs
```bash
sudo dmesg | grep "USB TX"
```

#### Check for errors
```bash
sudo dmesg | grep -i "error\|fail\|bug\|oops" | tail -20
```

#### Monitor logs in real-time
```bash
sudo dmesg -w
```

### Testing Force Feedback

#### List input devices
```bash
cat /proc/bus/input/devices | grep -A5 -B5 T500
```

#### Test with fftest
```bash
# Find the event device (usually /dev/input/event4 or similar)
ls -l /dev/input/by-id/ | grep T500

# Run fftest (replace eventX with your device)
fftest /dev/input/eventX
```

#### Test effects in fftest
- **Constant force**: Option 1
- **Spring**: Option 2
- **Damper**: Option 3
- **Periodic (sine)**: Option 6

### Common Issues

#### Module won't unload ("in use")
```bash
# Check what's using it
lsof | grep hid_tmff_new

# Force close any applications using the device
# Then try reload script again
```

#### Device not recognized after reload
```bash
# Check if device is detected
lsusb | grep -i thrustmaster

# Unplug and replug the wheel
# Or reboot if necessary
```

#### Kernel crash/hang
```bash
# Reboot the system
sudo reboot

# After reboot, check logs
sudo dmesg | grep -i "bug\|oops\|crash"
```

### Build Troubleshooting

#### Clean build
```bash
make clean
make
sudo make install
```

#### Check build errors
```bash
make 2>&1 | tee build.log
```

### Development Tips

1. **Always check dmesg** after loading modules to see initialization messages
2. **Use the reload script** instead of manual modprobe commands
3. **Test with fftest first** before testing with games
4. **Keep a terminal with `dmesg -w`** running to see real-time logs
5. **Reboot if system becomes unstable** - don't risk data loss

### File Structure

```
src/tmt500rs/
├── hid-tmt500rs-usb.c    # Main USB implementation (ACTIVE)
├── hid-tmt500rs-usb.o    # Compiled object
├── hid-tmt500rs.c        # Old HID feature report implementation (NOT USED)
└── hid-tmt500rs.h        # Header file
```

### Current Implementation Status

✅ **Working:**
- Device detection and initialization
- USB INTERRUPT communication
- Constant force effects
- Spring/Damper/Friction effects
- Periodic effects (sine, square, triangle, saw)
- Ramp effects
- Continuous force streaming (50Hz)
- Effect upload/play/stop
- Gain control
- Device open/close (fixed - no crash)

⚠️ **Known Issues:**
- None currently - ready for game testing!

### Next Steps

1. Test with racing games (Assetto Corsa, BeamNG, etc.)
2. Verify force feedback feels correct
3. Test long-term stability
4. Fine-tune effect parameters if needed

### Emergency Recovery

If the system becomes unstable:
1. **Reboot immediately** - don't try to fix a crashed kernel
2. After reboot, check: `sudo dmesg | grep -i "bug\|oops"`
3. If modules won't load, try: `sudo make clean && make && sudo make install`
4. If still broken, restore from backup or reinstall modules

---

**Last Updated**: 2025-10-17
**Driver Version**: T500RS USB INTERRUPT implementation
**Status**: Ready for testing

