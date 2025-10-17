# T500RS udev Rules Setup

## What Are udev Rules?

udev rules are Linux system rules that control device permissions and behavior. By default, hardware device settings require root (sudo) access. udev rules allow you to grant normal users permission to access specific devices.

## Why Do You Need This?

Without udev rules, you need `sudo` every time you want to:
- Run the T500RS Control GUI
- Use Oversteer to configure the wheel
- Manually read/write sysfs attributes

With udev rules installed:
- ✅ No `sudo` required for GUI
- ✅ No `sudo` required for Oversteer
- ✅ No `sudo` required for manual configuration
- ✅ Proper integration with desktop applications

## Installation

### Method 1: Automatic Installation (Recommended)

```bash
# Run the installation script
./install-udev-rules.sh

# Follow the prompts
# The script will:
#   1. Copy rules to /etc/udev/rules.d/
#   2. Reload udev
#   3. Trigger udev

# Reconnect your wheel
# Unplug USB, wait 5 seconds, plug back in
```

### Method 2: Manual Installation

```bash
# Copy rules file
sudo cp 99-thrustmaster-t500rs.rules /etc/udev/rules.d/

# Reload udev rules
sudo udevadm control --reload-rules

# Trigger udev to apply rules
sudo udevadm trigger

# Reconnect wheel (unplug and replug USB)
# Or reboot system
```

### Method 3: Let Oversteer Do It

When Oversteer shows the permission warning:
1. Click "Yes" to let Oversteer install the rules
2. Reboot when prompted
3. Done!

## Verification

After installation and reconnecting the wheel, test without sudo:

```bash
# Test reading gain (should work without sudo)
cat /sys/bus/hid/devices/0003:044F:B65E.*/gain

# Test writing gain (should work without sudo)
echo 50000 > /sys/bus/hid/devices/0003:044F:B65E.*/gain

# Run GUI without sudo
./t500rs-control-gui.py

# Use Oversteer
oversteer
```

If you get "Permission denied", the rules haven't taken effect yet. Try:
1. Unplug the wheel
2. Wait 5 seconds
3. Plug it back in
4. Or reboot your system

## What the Rules Do

The udev rule in `99-thrustmaster-t500rs.rules`:

```
SUBSYSTEM=="hid", ATTRS{idVendor}=="044f", ATTRS{idProduct}=="b65e", MODE="0666", TAG+="uaccess"
```

**Breakdown**:
- `SUBSYSTEM=="hid"` - Applies to HID (Human Interface Device) subsystem
- `ATTRS{idVendor}=="044f"` - Matches Thrustmaster vendor ID
- `ATTRS{idProduct}=="b65e"` - Matches T500RS product ID
- `MODE="0666"` - Sets read/write permissions for all users
- `TAG+="uaccess"` - Allows systemd-logind to manage access

## Security Considerations

**Is MODE="0666" safe?**

Yes, for a gaming wheel:
- ✅ Only affects T500RS (specific VID/PID)
- ✅ Only affects sysfs attributes (gain, range, etc.)
- ✅ Cannot damage hardware
- ✅ Standard practice for gaming peripherals
- ✅ Same approach used by other wheel drivers

**More restrictive alternative**:

If you prefer, you can use group-based permissions:

```bash
# Edit the rules file
sudo nano /etc/udev/rules.d/99-thrustmaster-t500rs.rules

# Change MODE="0666" to:
MODE="0660", GROUP="input"

# Add your user to input group
sudo usermod -a -G input $USER

# Reload rules
sudo udevadm control --reload-rules
sudo udevadm trigger

# Log out and log back in (for group membership to take effect)
```

## Troubleshooting

### Rules Not Working

**Symptom**: Still get "Permission denied" after installation

**Solutions**:
1. **Reconnect the wheel**:
   ```bash
   # Unplug USB cable
   # Wait 5 seconds
   # Plug back in
   ```

2. **Check if rules file exists**:
   ```bash
   ls -la /etc/udev/rules.d/99-thrustmaster-t500rs.rules
   ```

3. **Check rules syntax**:
   ```bash
   sudo udevadm test /sys/bus/hid/devices/0003:044F:B65E.* 2>&1 | grep -i "99-thrustmaster"
   ```

4. **Reload udev**:
   ```bash
   sudo udevadm control --reload-rules
   sudo udevadm trigger
   ```

5. **Reboot**:
   ```bash
   sudo reboot
   ```

### Check Current Permissions

```bash
# List all T500RS sysfs attributes and their permissions
ls -la /sys/bus/hid/devices/0003:044F:B65E.*/

# Should show:
# -rw-rw-rw- (0666) for most attributes
```

### Verify udev Rule is Active

```bash
# Check if udev sees the rule
udevadm info /sys/bus/hid/devices/0003:044F:B65E.* | grep -i "rule\|mode"
```

### Remove Rules (If Needed)

```bash
# Remove the rules file
sudo rm /etc/udev/rules.d/99-thrustmaster-t500rs.rules

# Reload udev
sudo udevadm control --reload-rules
sudo udevadm trigger

# Reconnect wheel
```

## Files

- **99-thrustmaster-t500rs.rules** - The udev rules file
- **install-udev-rules.sh** - Automatic installation script
- **UDEV_SETUP.md** - This documentation

## Alternative: Run with sudo

If you prefer not to install udev rules, you can still use the GUI with sudo:

```bash
# GUI
sudo ./t500rs-control-gui.py

# Or use the launcher script
./t500rs-control.sh  # Automatically uses sudo
```

## Integration with Oversteer

After installing udev rules:

1. **Launch Oversteer**:
   ```bash
   oversteer
   ```

2. **Select T500RS** from the device list

3. **Configure settings**:
   - Rotation range
   - Force feedback gain
   - Autocenter
   - Per-effect gains (if supported)

4. **Save profiles** for different games

Oversteer should now work without permission errors!

## See Also

- **GUI_README.md** - T500RS Control GUI documentation
- **SYSFS_SETTINGS.md** - Complete sysfs settings reference
- **QUICK_START_GUI.md** - Quick start guide for GUI

## Summary

**Recommended Setup**:
1. Run `./install-udev-rules.sh`
2. Reconnect wheel (unplug/replug USB)
3. Test: `cat /sys/bus/hid/devices/0003:044F:B65E.*/gain`
4. Use GUI without sudo: `./t500rs-control-gui.py`
5. Use Oversteer: `oversteer`

**Benefits**:
- No more sudo prompts
- Seamless desktop integration
- Works with all wheel configuration tools
- Follows Linux best practices

Enjoy your T500RS with proper permissions! 🎮🏎️

