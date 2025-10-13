# T500RS Driver Tools

User-facing tools for controlling and configuring the T500RS driver.

## Available Tools

### install_windows_drivers.sh
**Install Windows drivers in Wine/Proton prefix for gaming**

#### Purpose
Installs the Thrustmaster Windows drivers into a Wine/Proton prefix so that Windows games running under Wine/Proton can properly detect and use the T500RS wheel.

#### Requirements
- Wine or Proton installed
- T500RS connected
- Thrustmaster Windows drivers downloaded

#### Usage

**For Steam/Proton games**:
```bash
# Install to default Steam Proton prefix
./install_windows_drivers.sh

# Install to specific game's prefix
WINEPREFIX=~/.steam/steam/steamapps/compatdata/244210/pfx ./install_windows_drivers.sh
```

**For Wine games**:
```bash
# Install to default Wine prefix
WINEPREFIX=~/.wine ./install_windows_drivers.sh

# Install to custom prefix
WINEPREFIX=/path/to/prefix ./install_windows_drivers.sh
```

#### What It Does
1. Detects Wine/Proton prefix
2. Downloads/locates Thrustmaster drivers
3. Installs drivers into the prefix
4. Configures registry entries
5. Verifies installation

#### Common Use Cases

**Assetto Corsa Competizione**:
```bash
# Find the game's app ID (e.g., 805550)
WINEPREFIX=~/.steam/steam/steamapps/compatdata/805550/pfx ./install_windows_drivers.sh
```

**Automobilista 2**:
```bash
# Find the game's app ID (e.g., 1066890)
WINEPREFIX=~/.steam/steam/steamapps/compatdata/1066890/pfx ./install_windows_drivers.sh
```

**Generic Wine game**:
```bash
WINEPREFIX=~/.wine ./install_windows_drivers.sh
```

#### Troubleshooting

**Driver not detected in game**:
- Verify installation: Check Windows Device Manager in Wine
- Restart the game
- Check game's controller settings

**Installation fails**:
- Ensure Wine/Proton is installed
- Check WINEPREFIX path is correct
- Run with sudo if permission denied

---

### t500rs_control.py
**Command-line control utility for the T500RS driver**

#### Features
- Start/stop effects
- Adjust gain
- Configure autocenter
- Monitor driver status
- Runtime configuration

#### Requirements
```bash
pip install evdev
```

#### Usage

```bash
# Show help
python3 t500rs_control.py --help

# Set gain to 75%
python3 t500rs_control.py --gain 75

# Enable autocenter at 50%
python3 t500rs_control.py --autocenter 50

# Disable autocenter
python3 t500rs_control.py --autocenter 0

# Monitor driver status
python3 t500rs_control.py --monitor

# Test constant force
python3 t500rs_control.py --test-force left

# Show current configuration
python3 t500rs_control.py --show-config
```

#### Examples

**Quick force test**:
```bash
# Test left force
python3 t500rs_control.py --test-force left

# Test right force
python3 t500rs_control.py --test-force right

# Test center spring
python3 t500rs_control.py --test-force center
```

**Adjust settings**:
```bash
# Set gain to 100%
python3 t500rs_control.py --gain 100

# Set gain to 50%
python3 t500rs_control.py --gain 50

# Enable strong autocenter
python3 t500rs_control.py --autocenter 80
```

**Monitor mode**:
```bash
# Watch driver events in real-time
python3 t500rs_control.py --monitor
```

---

### t500rs_config_gui.py
**Graphical configuration tool for the T500RS driver**

#### Features
- Visual gain control
- Autocenter adjustment
- Effect testing
- Real-time monitoring
- Configuration presets

#### Requirements
```bash
pip install evdev tkinter
```

#### Usage

```bash
# Launch GUI
python3 t500rs_config_gui.py
```

#### GUI Features

**Main Controls**:
- Gain slider (0-100%)
- Autocenter slider (0-100%)
- Effect test buttons
- Status display

**Effect Testing**:
- Test constant force (left/right)
- Test spring effect
- Test damper effect
- Test periodic effects

**Monitoring**:
- Real-time force display
- Effect status
- Input values
- Driver statistics

**Presets**:
- Save current settings
- Load saved presets
- Quick preset buttons (Soft, Medium, Strong)

---

## Installation

### System-wide Installation

```bash
# Copy tools to /usr/local/bin
sudo cp t500rs_control.py /usr/local/bin/t500rs-control
sudo cp t500rs_config_gui.py /usr/local/bin/t500rs-config-gui
sudo chmod +x /usr/local/bin/t500rs-control
sudo chmod +x /usr/local/bin/t500rs-config-gui

# Now you can run from anywhere
t500rs-control --help
t500rs-config-gui
```

### Desktop Entry (GUI Tool)

Create `~/.local/share/applications/t500rs-config.desktop`:

```ini
[Desktop Entry]
Name=T500RS Configuration
Comment=Configure T500RS Force Feedback
Exec=/usr/local/bin/t500rs-config-gui
Icon=input-gaming
Terminal=false
Type=Application
Categories=Settings;HardwareSettings;
```

---

## Usage Examples

### Quick Setup

```bash
# 1. Start driver
sudo ../t500rs-ffb-modular &

# 2. Set comfortable gain
python3 t500rs_control.py --gain 70

# 3. Enable light autocenter
python3 t500rs_control.py --autocenter 30

# 4. Test force feedback
python3 t500rs_control.py --test-force left
```

### Game-Specific Settings

**Racing Simulators** (strong FFB):
```bash
python3 t500rs_control.py --gain 100 --autocenter 20
```

**Arcade Games** (moderate FFB):
```bash
python3 t500rs_control.py --gain 70 --autocenter 50
```

**Casual Driving** (light FFB):
```bash
python3 t500rs_control.py --gain 50 --autocenter 60
```

### Troubleshooting

**Check driver status**:
```bash
python3 t500rs_control.py --monitor
```

**Test force feedback**:
```bash
python3 t500rs_control.py --test-force left
python3 t500rs_control.py --test-force right
```

**Reset to defaults**:
```bash
python3 t500rs_control.py --gain 100 --autocenter 0
```

---

## Advanced Usage

### Scripting

Create custom scripts using the control utility:

```bash
#!/bin/bash
# racing_setup.sh - Setup for racing games

# Set strong FFB
python3 /path/to/t500rs_control.py --gain 100

# Light autocenter
python3 /path/to/t500rs_control.py --autocenter 20

echo "Racing setup complete!"
```

### Integration

Integrate with game launchers:

```bash
# Before launching game
python3 t500rs_control.py --gain 100 --autocenter 20

# Launch game
steam steam://rungameid/244210

# After game exits
python3 t500rs_control.py --gain 70 --autocenter 50
```

---

## Development

### Adding Features

Both tools are Python scripts that can be easily extended:

**t500rs_control.py**:
- Add new command-line arguments
- Implement new control functions
- Add monitoring features

**t500rs_config_gui.py**:
- Add new GUI widgets
- Implement new visualizations
- Add preset management

### Contributing

1. Test changes thoroughly
2. Update help text
3. Update this README
4. Submit pull request

---

## See Also

- [Main README](../README.md) - Driver documentation
- [Test Programs](../tests/README.md) - Test suite
- [Configuration Guide](../docs/development/ADVANCED_FF_CONFIG.md) - Advanced configuration

