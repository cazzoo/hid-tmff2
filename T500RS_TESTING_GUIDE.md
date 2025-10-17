# T500RS Testing Guide

Quick reference for testing the newly implemented T500RS driver module.

---

## Pre-Testing Checklist

- [ ] T500RS wheel connected via USB
- [ ] Wheel powered on
- [ ] No other Thrustmaster drivers loaded
- [ ] Module compiled successfully (`hid_tmff_new.ko` exists)

---

## Step 1: Verify Device Connection

```bash
# Check USB device is detected
lsusb | grep -i thrustmaster

# Expected output (one of):
# Bus XXX Device XXX: ID 044f:b65d Thrustmaster T500 RS (boot mode)
# Bus XXX Device XXX: ID 044f:b65e Thrustmaster T500 RS (normal mode)

# Check current kernel driver
lsusb -v -d 044f: | grep -A5 "iProduct"
```

---

## Step 2: Load the Module

```bash
# Remove any existing tmff modules
sudo modprobe -r hid_tmff_new 2>/dev/null

# Load the new module
sudo modprobe hid_tmff_new

# Verify module loaded
lsmod | grep tmff

# Check for initialization messages
dmesg | tail -30 | grep -i t500
```

**Expected Output:**
```
[timestamp] hid_tmff_new: T500RS force feedback initialized
```

---

## Step 3: Verify Device Detection

```bash
# Check input devices
cat /proc/bus/input/devices | grep -A10 -B5 "T500\|Thrustmaster"

# Find the event device number
ls -l /dev/input/by-id/ | grep -i thrustmaster

# Check force feedback capabilities
cat /sys/class/input/event*/device/name | grep -n T500
```

**Expected Output:**
```
N: Name="Thrustmaster T500 RS"
P: Phys=usb-XXXX
S: Sysfs=/devices/...
U: Uniq=
H: Handlers=event15 js0
B: PROP=0
B: EV=20001b
B: KEY=...
B: ABS=...
B: FF=...  <-- Force feedback capabilities
```

---

## Step 4: Test Force Feedback

### Install fftest (if not already installed)

```bash
# Debian/Ubuntu
sudo apt-get install fftest

# Arch/Manjaro
sudo pacman -S linuxconsole

# Fedora
sudo dnf install linuxconsoletools
```

### Run fftest

```bash
# Find your event device (replace X with actual number)
fftest /dev/input/eventX

# Or use by-id path
fftest /dev/input/by-id/usb-Thrustmaster_T500_RS-event-joystick
```

### Test Each Effect Type

**In fftest menu:**

1. **Test Constant Force**
   - Select option `1` (Constant force)
   - Set magnitude: `5000` (50% force)
   - Set direction: `0` (center)
   - Upload and play effect
   - **Expected**: Wheel should resist movement

2. **Test Spring Effect**
   - Select option `3` (Spring)
   - Set center: `0`
   - Set coefficient: `5000`
   - Upload and play effect
   - **Expected**: Wheel should center itself

3. **Test Damper Effect**
   - Select option `4` (Damper)
   - Set coefficient: `5000`
   - Upload and play effect
   - **Expected**: Wheel should resist fast movements

4. **Test Periodic Effect**
   - Select option `6` (Periodic - Sine)
   - Set magnitude: `5000`
   - Set period: `1000` (1 second)
   - Upload and play effect
   - **Expected**: Wheel should oscillate smoothly

---

## Step 5: Monitor System Stability

### Watch Kernel Messages

```bash
# In a separate terminal, monitor kernel messages
dmesg -w

# Look for:
# - Effect upload confirmations
# - USB communication errors
# - Any kernel warnings or errors
```

### Check USB Communication

```bash
# Check for USB errors
dmesg | grep -i "usb.*error" | tail -20

# Check HID communication
dmesg | grep -i "hid.*t500" | tail -20
```

### Verify No System Hangs

- System should remain responsive
- No kernel panics or crashes
- USB device should not disconnect unexpectedly
- Wheel should respond to all commands

---

## Step 6: Test Advanced Features

### Test Gain Control

```bash
# Set gain via sysfs
echo 50 > /sys/module/hid_tmff_new/parameters/gain

# Verify gain changed
cat /sys/module/hid_tmff_new/parameters/gain

# Test effect with new gain
fftest /dev/input/eventX
```

### Test Range Setting

```bash
# Set wheel range (270-1080 degrees)
echo 900 > /sys/module/hid_tmff_new/parameters/range

# Verify range changed
cat /sys/module/hid_tmff_new/parameters/range
```

### Test Spring/Damper Levels

```bash
# Set spring level (0-100)
echo 50 > /sys/module/hid_tmff_new/parameters/spring_level

# Set damper level (0-100)
echo 50 > /sys/module/hid_tmff_new/parameters/damper_level

# Set friction level (0-100)
echo 50 > /sys/module/hid_tmff_new/parameters/friction_level
```

---

## Step 7: Test with Real Applications

### Test with Wine/Proton Games

```bash
# Example: Test with a racing game
wine /path/to/racing_game.exe

# Or with Steam/Proton
steam steam://rungameid/XXXXX
```

**Verify:**
- Game detects T500RS wheel
- Force feedback effects work in-game
- No crashes or disconnections
- Effects feel appropriate

---

## Troubleshooting

### Problem: Module Won't Load

```bash
# Check module dependencies
modinfo hid_tmff_new.ko | grep depends

# Check for conflicting modules
lsmod | grep hid

# Try loading with verbose output
sudo modprobe -v hid_tmff_new
```

### Problem: Device Not Detected

```bash
# Check if device is in boot mode (0xb65d)
lsusb | grep 044f:b65d

# If in boot mode, may need hid-tminit to switch to normal mode
# Check deps/hid-tminit/

# Verify udev rules
ls -l /etc/udev/rules.d/*thrustmaster*
```

### Problem: No Force Feedback

```bash
# Check if FF capabilities are present
cat /sys/class/input/event*/device/capabilities/ff

# Verify effect upload in kernel log
dmesg | grep -i "effect.*upload"

# Check for HID report errors
dmesg | grep -i "hid.*report.*fail"
```

### Problem: Weak or No Effects

```bash
# Check gain setting
cat /sys/module/hid_tmff_new/parameters/gain

# Increase gain
echo 65535 > /sys/module/hid_tmff_new/parameters/gain

# Check effect magnitude in fftest
# Try higher magnitude values (e.g., 10000)
```

### Problem: System Hangs or Crashes

```bash
# Immediately unload module
sudo modprobe -r hid_tmff_new

# Check kernel log for errors
dmesg | tail -100

# Report issue with full kernel log
dmesg > kernel_log.txt
```

---

## Success Criteria

✅ **Module Loads**: No errors in dmesg  
✅ **Device Detected**: Shows up in /proc/bus/input/devices  
✅ **FF Capabilities**: FF bits set in device capabilities  
✅ **Constant Force Works**: Wheel resists movement  
✅ **Spring Works**: Wheel centers itself  
✅ **Damper Works**: Wheel resists fast movements  
✅ **Periodic Works**: Wheel oscillates smoothly  
✅ **System Stable**: No crashes, hangs, or USB errors  
✅ **Game Compatible**: Works with Wine/Proton games

---

## Reporting Issues

If you encounter problems, collect this information:

```bash
# System information
uname -a
lsusb | grep -i thrustmaster

# Module information
modinfo hid_tmff_new.ko

# Kernel messages
dmesg > dmesg_output.txt

# Device information
cat /proc/bus/input/devices > input_devices.txt

# USB details
lsusb -v -d 044f: > usb_details.txt
```

---

## Next Steps After Successful Testing

1. **Install Module Permanently**
   ```bash
   sudo make install
   sudo depmod -a
   ```

2. **Install udev Rules**
   ```bash
   sudo make udev-rules
   sudo udevadm control --reload-rules
   ```

3. **Configure for Auto-Load**
   ```bash
   echo "hid_tmff_new" | sudo tee /etc/modules-load.d/hid-tmff-new.conf
   ```

4. **Test After Reboot**
   - Reboot system
   - Verify module auto-loads
   - Test force feedback still works

---

## Performance Benchmarks

### Expected Latency
- Effect upload: < 10ms
- Effect start: < 5ms
- Effect update: < 5ms

### Test Latency

```bash
# Use fftest with timing
time fftest /dev/input/eventX

# Monitor effect timing in kernel log
dmesg -w | grep -i effect
```

---

## Safety Reminders

⚠️ **Important Safety Notes:**

1. **Never force remove USB** while effects are playing
2. **Monitor system stability** during initial testing
3. **Start with low magnitude** effects (< 5000)
4. **Keep hands on wheel** during testing
5. **Have emergency stop ready** (Ctrl+C in fftest)

---

## Conclusion

This testing guide provides a comprehensive workflow for validating the T500RS driver implementation. Follow each step carefully and document any issues encountered.

**Happy Testing! 🏎️**

