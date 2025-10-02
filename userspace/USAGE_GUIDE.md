# T500RS Force Feedback Driver - Usage Guide

## Quick Start

```bash
cd ~/Documents/hid-tmff2/userspace
sudo ./run.sh
```

That's it! The driver will start and you should see:

```
========================================
T500RS Force Feedback Driver
========================================

[INFO] Initializing libusb...
[INFO] Opening T500RS device...
[INFO] Device opened successfully
[INFO] Detaching kernel driver...
[INFO] Claiming USB interface...
[INFO] Initializing T500RS...
[INFO] Initialization complete
[INFO] Setting up uinput device...
[INFO] uinput device created successfully
[INFO] ========================================
[INFO] T500RS Force Feedback Driver Running
[INFO] ========================================
[INFO] Device: /dev/input/eventX (check dmesg for exact number)
[INFO] Press Ctrl+C to stop
```

## Testing Force Feedback

### Step 1: Find the Device

In another terminal:

```bash
dmesg | tail | grep "T500RS"
```

You should see:
```
input: Thrustmaster T500RS (FFB) as /dev/input/event25
```

Note the event number (e.g., `event25`).

### Step 2: Test with fftest

```bash
# Install fftest if not already installed
sudo pacman -S linuxconsole

# Run fftest (replace event25 with your number)
fftest /dev/input/event25
```

### Step 3: Upload and Play an Effect

In fftest:

1. Press **Enter** to see the menu
2. Select option **1** (Upload a constant force effect)
3. Enter force level: **10000** (medium force)
4. The effect will be uploaded
5. Select option **2** (Play effect)
6. **You should feel the force in the wheel!**
7. Select option **3** (Stop effect)

## Using with Games

### Assetto Corsa Competizione

1. Start the driver: `sudo ./run.sh`
2. Launch ACC
3. Go to Settings → Controls
4. The wheel should be detected as "Thrustmaster T500RS (FFB)"
5. Configure force feedback settings
6. Enjoy!

### Other Games

Most racing games that support force feedback will automatically detect the device.
If not, look for "Thrustmaster T500RS" in the controller settings.

## Advanced Usage

### Running in Background

```bash
# Start in background
sudo ./t500rs-ffb &

# Check if running
ps aux | grep t500rs-ffb

# Stop
sudo killall t500rs-ffb
```

### Viewing Logs

The driver outputs detailed logs. To save them:

```bash
sudo ./t500rs-ffb 2>&1 | tee t500rs.log
```

### Debug Mode

To see all USB communication:

```bash
# Edit t500rs-ffb.c and change LOG_DEBUG to always print
# Then rebuild:
make clean && make
sudo ./t500rs-ffb
```

## Troubleshooting

### Problem: "Cannot open device"

**Solution**:
```bash
# Check if device is connected
lsusb | grep 044f:b65e

# If not found, reconnect the wheel
```

### Problem: "Failed to detach kernel driver"

**Solution**:
```bash
# Unload kernel driver manually
sudo rmmod hid_tmff_new

# Then run driver again
sudo ./run.sh
```

### Problem: "Failed to open /dev/uinput"

**Solution**:
```bash
# Load uinput module
sudo modprobe uinput

# Make it permanent
echo "uinput" | sudo tee /etc/modules-load.d/uinput.conf
```

### Problem: No force feedback in games

**Checklist**:
1. ✅ Driver is running (check with `ps aux | grep t500rs`)
2. ✅ Device created (check `ls /dev/input/event*`)
3. ✅ fftest works (test with `fftest /dev/input/eventX`)
4. ✅ Game settings configured for force feedback
5. ✅ Force feedback strength not set to 0 in game

### Problem: Wheel feels weak

The force might be subtle. Try:
1. Increase force in game settings
2. Test with maximum force in fftest (32767)
3. Check if initialization completed successfully

### Problem: Driver crashes

**Solution**:
```bash
# Check dmesg for errors
dmesg | tail -50

# Try running with debug output
sudo ./t500rs-ffb 2>&1 | tee crash.log

# Share crash.log for help
```

## Performance Tips

### Reduce Latency

```bash
# Set CPU governor to performance
sudo cpupower frequency-set -g performance

# Increase USB polling rate (if supported)
# Edit /etc/modprobe.d/usbhid.conf:
options usbhid mousepoll=1
```

### Optimize for Racing

1. Close unnecessary applications
2. Disable desktop effects
3. Use a lightweight desktop environment
4. Consider using a real-time kernel

## Integration with System

### Auto-start on Boot

Create systemd service:

```bash
sudo nano /etc/systemd/system/t500rs-ffb.service
```

Add:
```ini
[Unit]
Description=T500RS Force Feedback Driver
After=multi-user.target

[Service]
Type=simple
ExecStart=/usr/local/bin/t500rs-ffb
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Enable:
```bash
sudo systemctl enable t500rs-ffb
sudo systemctl start t500rs-ffb
```

### Auto-start on Device Plug

Create udev rule:

```bash
sudo nano /etc/udev/rules.d/99-t500rs.rules
```

Add:
```
ACTION=="add", SUBSYSTEM=="usb", ATTRS{idVendor}=="044f", ATTRS{idProduct}=="b65e", RUN+="/usr/local/bin/t500rs-ffb"
```

Reload:
```bash
sudo udevadm control --reload-rules
```

## Comparison with Kernel Driver

| Feature | Userspace Driver | Kernel Driver |
|---------|------------------|---------------|
| Force Feedback | ✅ Works | ❌ Blocked by HID |
| Easy to Update | ✅ Yes | ❌ Needs recompile |
| Debugging | ✅ Easy | ❌ Difficult |
| Performance | ✅ Good | ✅ Slightly better |
| Installation | ✅ Simple | ❌ Complex |
| Stability | ✅ Stable | ❌ Bootloader issues |

**Conclusion**: Userspace driver is the better solution for T500RS.

## FAQ

### Q: Do I need to run this every time?

A: Yes, or set up auto-start (see above).

### Q: Can I use the wheel without the driver?

A: Yes, for input (buttons/axes), but no force feedback.

### Q: Will this work with other Thrustmaster wheels?

A: No, this is specifically for T500RS. Other wheels use different protocols.

### Q: Can I contribute?

A: Yes! See the main repository for contribution guidelines.

### Q: Is this safe?

A: Yes, we've tested extensively. The driver uses the exact same protocol as Windows.

### Q: Why userspace instead of kernel?

A: The T500RS uses a proprietary protocol that doesn't fit the Linux HID model. Userspace gives us the flexibility we need.

## Getting Help

If you have issues:

1. Check this guide
2. Check `README.md`
3. Check `../NEXT_STEPS_PLAN.md`
4. Check dmesg for errors
5. Ask on Linux gaming forums
6. Open an issue on GitHub

## Success Stories

Share your experience! If this driver works for you, let us know:
- What games you're playing
- Your force feedback settings
- Any tips for other users

## Changelog

### Version 1.0 (2025-10-02)
- Initial release
- Constant and spring effects
- Proper initialization
- Tested and working!

---

**Enjoy your T500RS with full force feedback on Linux!** 🏁🎮

