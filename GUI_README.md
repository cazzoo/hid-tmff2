# T500RS Control GUI

## Overview

A simple graphical user interface for controlling all Thrustmaster T500RS force feedback settings. This GUI provides an easy way to adjust all sysfs settings without using command-line tools.

![T500RS Control GUI](https://via.placeholder.com/600x700.png?text=T500RS+Control+GUI)

## Features

- ✅ **Real-time control** of all T500RS settings
- ✅ **Visual sliders** for easy adjustment
- ✅ **Live value display** with percentage indicators
- ✅ **Preset configurations** (Racing, Arcade, Rally)
- ✅ **Refresh button** to reload values from device
- ✅ **Auto-detection** of T500RS device
- ✅ **Permission handling** with helpful error messages

## Requirements

### Python Dependencies

```bash
# Install Python 3 and GTK 3
sudo pacman -S python python-gobject gtk3

# Or on Debian/Ubuntu:
sudo apt install python3 python3-gi gir1.2-gtk-3.0
```

### Driver Requirements

- T500RS driver must be loaded (`hid_tmff_new` module)
- T500RS wheel must be connected and detected
- Device should appear at `/sys/bus/hid/devices/0003:044F:B65E.*`

## Installation

### Quick Start

```bash
# Navigate to driver directory
cd /home/caz/Documents/hid-tmff2

# Make scripts executable (already done)
chmod +x t500rs-control-gui.py
chmod +x t500rs-control.sh

# Run the GUI
sudo ./t500rs-control-gui.py
```

### Desktop Launcher (Optional)

```bash
# Copy desktop file to applications directory
cp t500rs-control.desktop ~/.local/share/applications/

# Update desktop database
update-desktop-database ~/.local/share/applications/
```

After this, you can launch "T500RS Control" from your application menu.

## Usage

### Launching the GUI

**Method 1: Direct execution**
```bash
sudo ./t500rs-control-gui.py
```

**Method 2: Using launcher script**
```bash
./t500rs-control.sh
```

**Method 3: Desktop launcher**
- Search for "T500RS Control" in your application menu
- Click to launch

### GUI Layout

The GUI is organized into sections:

#### 1. Device Status
- Shows whether T500RS is detected
- Displays device path

#### 2. Global Settings
- **Global Gain** (0-65535): Master volume for all effects
- **Rotation Range** (270-1080°): Wheel rotation angle

#### 3. Per-Effect Gains (0-100%)
- **Constant Force**: Gain for constant force effects
- **Periodic Effects**: Gain for rumble/vibration effects
- **Spring Effects**: Gain for centering/suspension effects
- **Damper Effects**: Gain for resistance/weight effects

#### 4. Base Effect Levels (0-100%)
- **Spring Level**: Base spring multiplier
- **Damper Level**: Base damper multiplier
- **Friction Level**: Friction multiplier

#### 5. Autocenter
- Information about autocenter control (FFB API)

#### 6. Control Buttons
- **Refresh Values**: Reload all values from device
- **Load Preset...**: Apply preset configurations

### Using Sliders

1. **Drag slider** to adjust value
2. **Value updates in real-time** and is written to sysfs
3. **Current value displayed** next to slider
4. **Percentage shown** for most settings

### Loading Presets

Click "Load Preset..." button and choose:

#### Racing Simulation (Realistic)
- Global Gain: 100%
- Range: 900°
- All effect gains: High (80-100%)
- Best for: Realistic racing simulators

#### Arcade Racing (Easy)
- Global Gain: 50%
- Range: 540°
- Effect gains: Moderate (50-80%)
- Best for: Casual racing games

#### Rally/Drift
- Global Gain: 75%
- Range: 540°
- Spring: Low (50%) for easy drift
- Damper: High (80%) for control
- Best for: Rally games, drifting

### Refreshing Values

Click "Refresh Values" to reload all settings from the device. Useful if:
- Another program changed settings
- You want to verify current values
- Settings were changed via command line

## Permissions

The GUI requires **root permissions** to write to sysfs.

### Running with sudo

```bash
sudo ./t500rs-control-gui.py
```

### Permission Errors

If you see "Permission Error" dialog:
1. Close the GUI
2. Relaunch with `sudo`
3. Or run via `./t500rs-control.sh` (handles sudo automatically)

### Alternative: udev Rules (Advanced)

To avoid needing sudo, create udev rules:

```bash
# Create udev rule file
sudo nano /etc/udev/rules.d/99-t500rs.rules

# Add this line:
SUBSYSTEM=="hid", ATTRS{idVendor}=="044f", ATTRS{idProduct}=="b65e", MODE="0666"

# Reload udev rules
sudo udevadm control --reload-rules
sudo udevadm trigger

# Reconnect wheel
```

After this, you can run the GUI without sudo.

## Troubleshooting

### Device Not Detected

**Symptom**: GUI shows "T500RS not detected!"

**Solutions**:
1. Check wheel is connected: `lsusb | grep 044f`
2. Check driver loaded: `lsmod | grep hid_tmff_new`
3. Check device path exists: `ls /sys/bus/hid/devices/0003:044F:B65E.*`
4. Reload driver: `sudo ./reload_modules.sh`

### Cannot Write Values

**Symptom**: "Permission Error" dialog appears

**Solutions**:
1. Run with sudo: `sudo ./t500rs-control-gui.py`
2. Use launcher script: `./t500rs-control.sh`
3. Set up udev rules (see above)

### Values Not Updating

**Symptom**: Slider moves but value doesn't change

**Solutions**:
1. Click "Refresh Values" to reload
2. Check dmesg for errors: `dmesg | tail -20`
3. Verify sysfs attribute exists: `ls -la /sys/bus/hid/devices/0003:044F:B65E.*/gain`

### GUI Won't Start

**Symptom**: Error about missing modules

**Solutions**:
```bash
# Install GTK dependencies
sudo pacman -S python-gobject gtk3

# Or on Debian/Ubuntu:
sudo apt install python3-gi gir1.2-gtk-3.0
```

### Slider Ranges Wrong

**Symptom**: Can't set desired value

**Check**: Each setting has specific ranges:
- Global Gain: 0-65535
- Range: 270-1080
- All gains/levels: 0-100

## Command-Line Equivalent

The GUI is equivalent to these commands:

```bash
# Set global gain to 100%
echo 65535 > /sys/bus/hid/devices/0003:044F:B65E.*/gain

# Set range to 900 degrees
echo 900 > /sys/bus/hid/devices/0003:044F:B65E.*/range

# Set constant gain to 80%
echo 80 > /sys/bus/hid/devices/0003:044F:B65E.*/constant_gain

# Read current gain
cat /sys/bus/hid/devices/0003:044F:B65E.*/gain
```

## Advanced Usage

### Scripting

You can import the GUI module in your own scripts:

```python
#!/usr/bin/env python3
from t500rs_control_gui import T500RSControlGUI

# Create GUI instance
gui = T500RSControlGUI()

# Access device path
print(f"Device: {gui.device_path}")

# Read value
gain = gui.read_sysfs("gain")
print(f"Current gain: {gain}")

# Write value
gui.write_sysfs("gain", 65535)
```

### Custom Presets

Edit the preset functions in `t500rs-control-gui.py`:

```python
def load_preset_custom(self):
    """Load custom preset"""
    self.gain_scale.set_value(50000)  # ~76%
    self.range_scale.set_value(720)
    self.constant_gain_scale.set_value(90)
    # ... etc
```

## Integration with Games

### Before Gaming

1. Launch T500RS Control GUI
2. Load appropriate preset or adjust manually
3. Close GUI (settings are saved to device)
4. Launch game

### During Gaming

- Keep GUI open to adjust settings in real-time
- Use "Refresh Values" if game changes settings
- Experiment with different gains for best feel

### Game-Specific Profiles

Create shell scripts for different games:

```bash
#!/bin/bash
# assetto-corsa-profile.sh

echo 65535 > /sys/bus/hid/devices/0003:044F:B65E.*/gain
echo 900 > /sys/bus/hid/devices/0003:044F:B65E.*/range
echo 100 > /sys/bus/hid/devices/0003:044F:B65E.*/constant_gain
echo 100 > /sys/bus/hid/devices/0003:044F:B65E.*/periodic_gain
echo 80 > /sys/bus/hid/devices/0003:044F:B65E.*/t500rs_spring_gain
echo 70 > /sys/bus/hid/devices/0003:044F:B65E.*/t500rs_damper_gain
```

## Future Enhancements

Planned features:
- [ ] Save/load custom presets to file
- [ ] Per-game profile management
- [ ] Autocenter control via FFB API
- [ ] Real-time force feedback monitoring
- [ ] Graphical force feedback visualization
- [ ] Integration with Oversteer
- [ ] System tray icon
- [ ] Auto-apply profiles when games launch

## Files

- `t500rs-control-gui.py` - Main GUI application (Python/GTK)
- `t500rs-control.sh` - Launcher script (handles sudo)
- `t500rs-control.desktop` - Desktop launcher file
- `GUI_README.md` - This documentation

## See Also

- **SYSFS_SETTINGS.md** - Complete sysfs settings reference
- **GAIN_IMPLEMENTATION.md** - Gain system documentation
- **PER_EFFECT_GAIN.md** - Per-effect gain details
- **AUTOCENTER_IMPLEMENTATION.md** - Autocenter control
- **T500RS_TESTING_GUIDE.md** - Testing procedures

## Support

If you encounter issues:

1. Check this README's troubleshooting section
2. Check driver logs: `dmesg | grep -i tmff`
3. Verify device detection: `ls /sys/bus/hid/devices/0003:044F:B65E.*`
4. Test command-line access: `cat /sys/bus/hid/devices/0003:044F:B65E.*/gain`

## License

This GUI is part of the hid-tmff2 driver project.

## Credits

- GUI developed for T500RS Linux driver
- Based on GTK 3 and Python 3
- Inspired by Oversteer wheel configuration tool

