# T500RS Control GUI - Quick Start

## 🚀 Installation (One-Time Setup)

```bash
# 1. Navigate to driver directory
cd /home/caz/Documents/hid-tmff2

# 2. Run installation script
./install-gui.sh

# Choose option:
#   1 = System-wide installation (recommended)
#   2 = User-only (desktop launcher)
#   3 = Skip (run from current directory)
```

## 🎮 Launching the GUI

### Method 1: Application Menu (After Installation)
1. Open application menu
2. Search for "T500RS Control"
3. Click to launch

### Method 2: Terminal
```bash
# If installed system-wide
sudo t500rs-control

# If not installed
sudo ./t500rs-control-gui.py
```

### Method 3: Launcher Script
```bash
./t500rs-control.sh
```

## 📊 GUI Overview

```
┌─────────────────────────────────────────────────┐
│         T500RS Force Feedback Control           │
├─────────────────────────────────────────────────┤
│ Device Status                                   │
│ ✓ T500RS detected at: /sys/bus/hid/devices/... │
├─────────────────────────────────────────────────┤
│ Global Settings                                 │
│ ┌─────────────────────────────────────────────┐ │
│ │ Global Gain: 65535 (100%)                   │ │
│ │ [━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━] │ │
│ │                                             │ │
│ │ Rotation Range: 900°                        │ │
│ │ [━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━] │ │
│ └─────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────┤
│ Per-Effect Gains (0-100%)                       │
│ ┌─────────────────────────────────────────────┐ │
│ │ Constant Force: 100%                        │ │
│ │ [━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━] │ │
│ │                                             │ │
│ │ Periodic Effects: 100%                      │ │
│ │ [━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━] │ │
│ │                                             │ │
│ │ Spring Effects: 80%                         │ │
│ │ [━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━] │ │
│ │                                             │ │
│ │ Damper Effects: 70%                         │ │
│ │ [━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━] │ │
│ └─────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────┤
│ Base Effect Levels (0-100%)                     │
│ ┌─────────────────────────────────────────────┐ │
│ │ Spring Level: 30%                           │ │
│ │ Damper Level: 30%                           │ │
│ │ Friction Level: 30%                         │ │
│ └─────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────┤
│ [Refresh Values]  [Load Preset...]             │
└─────────────────────────────────────────────────┘
```

## 🎯 Quick Actions

### Adjust Global Gain (Master Volume)
1. Find "Global Gain" slider
2. Drag to desired value (0-65535)
3. Value updates immediately
4. 100% = 65535, 50% = 32768, 25% = 16384

### Change Wheel Rotation
1. Find "Rotation Range" slider
2. Drag to desired angle (270-1080°)
3. Common values:
   - 270° = Formula 1 style
   - 540° = Rally style
   - 900° = Road car style
   - 1080° = Truck/bus style

### Load a Preset
1. Click "Load Preset..." button
2. Choose preset:
   - **Racing Simulation**: Realistic, 100% gain, 900°
   - **Arcade Racing**: Easy, 50% gain, 540°
   - **Rally/Drift**: 75% gain, 540°, low spring
3. Click OK
4. All settings applied instantly

### Refresh Values
1. Click "Refresh Values" button
2. All sliders reload from device
3. Use if another program changed settings

## 🎮 Recommended Presets

### For Assetto Corsa / iRacing / rFactor 2
```
Preset: Racing Simulation
- Global Gain: 100%
- Range: 900°
- All effects: High (80-100%)
```

### For Forza / Need for Speed / Grid
```
Preset: Arcade Racing
- Global Gain: 50%
- Range: 540°
- Effects: Moderate (50-80%)
```

### For Dirt Rally / WRC
```
Preset: Rally/Drift
- Global Gain: 75%
- Range: 540°
- Spring: Low (50%)
- Damper: High (80%)
```

## 🔧 Troubleshooting

### "T500RS not detected!"
```bash
# Check wheel connected
lsusb | grep 044f

# Check driver loaded
lsmod | grep hid_tmff_new

# Reload driver
sudo ./reload_modules.sh

# Restart GUI
sudo ./t500rs-control-gui.py
```

### "Permission Error"
```bash
# Always run with sudo
sudo ./t500rs-control-gui.py

# Or use launcher script
./t500rs-control.sh
```

### Values Don't Change
```bash
# Check sysfs access
cat /sys/bus/hid/devices/0003:044F:B65E.*/gain

# Check dmesg for errors
dmesg | tail -20

# Click "Refresh Values" in GUI
```

### Missing Dependencies
```bash
# Install on Arch/Manjaro
sudo pacman -S python python-gobject gtk3

# Install on Debian/Ubuntu
sudo apt install python3 python3-gi gir1.2-gtk-3.0
```

## 📝 Tips

### Before Gaming
1. Launch GUI
2. Load appropriate preset
3. Fine-tune if needed
4. Close GUI (settings saved)
5. Launch game

### During Gaming
- Keep GUI open for real-time adjustments
- Experiment with different gains
- Find your preferred settings

### Save Your Settings
- GUI reads/writes directly to device
- Settings persist until wheel power cycle
- Create shell scripts for favorite configs

## 🔗 See Also

- **GUI_README.md** - Complete GUI documentation
- **SYSFS_SETTINGS.md** - All settings reference
- **GAIN_IMPLEMENTATION.md** - Gain system details
- **PER_EFFECT_GAIN.md** - Per-effect gain info
- **AUTOCENTER_IMPLEMENTATION.md** - Autocenter control

## 💡 Pro Tips

1. **Start with presets** - Load a preset, then fine-tune
2. **Adjust per-effect gains** - Balance different effect types
3. **Lower damper if too heavy** - Reduce damper_gain to 50-70%
4. **Boost periodic for rumble** - Increase periodic_gain for curbs
5. **Use 540° for rally** - Better for quick steering inputs
6. **Keep GUI open** - Real-time adjustments while testing

## 🎉 Enjoy!

The GUI makes it easy to find your perfect force feedback settings. Experiment with different values and presets to find what feels best for your driving style!

