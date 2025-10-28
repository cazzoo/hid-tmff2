# T500RS Force Feedback in Proton Games

## Problem

Force feedback works perfectly in Wine games (Live For Speed) but doesn't work in Proton/Steam games (Automobilista 2, Assetto Corsa Competizione, etc.).

## Why This Happens

**Wine vs Proton FFB Handling:**

- **Wine (Live For Speed)**: Uses Linux kernel driver directly via `/dev/input/event*`
- **Proton (Steam games)**: Uses SDL2 library which has specific requirements
- **Windows drivers in Proton prefix**: Don't work - Proton uses Linux drivers!

**Common Issues:**
1. Steam Input interfering with direct device access
2. SDL2 not detecting force feedback capabilities correctly
3. Device permissions (though udev rules should fix this)
4. Game-specific FFB implementation issues

## Solutions

### Solution 1: Disable Steam Input (MOST IMPORTANT)

Steam Input can block direct FFB access. **This is the most common fix.**

#### Method A: Per-Game Setting (Recommended)

1. **Right-click Automobilista 2** in Steam library
2. **Properties** → **Controller**
3. **Override for Automobilista 2**: Set to **"Disable Steam Input"**
4. **Close** and **launch the game**

#### Method B: Global Setting

1. **Steam** → **Settings** → **Controller**
2. **Desktop Configuration**
3. Uncheck **"PlayStation Configuration Support"**
4. Uncheck **"Xbox Configuration Support"**
5. Uncheck **"Generic Gamepad Configuration Support"**
6. **Restart Steam**

### Solution 2: Set SDL2 Environment Variables (CRITICAL FOR T500RS)

**RECOMMENDED LAUNCH OPTIONS for T500RS:**

```bash
SDL_JOYSTICK_DEVICE=/dev/input/event31 %command%
```

**Replace `event31` with your wheel's event device number!** Find it with:
```bash
cat /proc/bus/input/devices | grep -A 5 "Thrustmaster"
# Look for the line: H: Handlers=event31 js1
```

**How to set:**
1. Right-click game in Steam
2. **Properties** → **General**
3. **Launch Options**: Add the above line

**Why this works:**
- **Bypasses js0/js1 conflicts** - Some systems have multiple joystick devices (e.g., "Mouse passthrough") that confuse games
- **Direct event device access** - SDL2 uses the event device directly, avoiding joystick device conflicts
- **All axes work** - Steering wheel AND pedals are detected correctly

**Alternative (if above doesn't work):**
```bash
SDL_JOYSTICK_HIDAPI=0 SDL_JOYSTICK_RAWINPUT=0 %command%
```

**What this does:**
- `SDL_JOYSTICK_HIDAPI=0` - Disables SDL2's HIDAPI backend (use evdev instead)
- `SDL_JOYSTICK_RAWINPUT=0` - Disables Windows raw input emulation
- `%command%` - Placeholder for the actual game command

### Solution 3: Force Proton to Use Correct Input Backend

Some games need specific Proton versions or settings:

```bash
PROTON_USE_WINED3D=1 SDL_JOYSTICK_HIDAPI=0 %command%
```

Or try:

```bash
PROTON_NO_ESYNC=1 PROTON_NO_FSYNC=1 SDL_JOYSTICK_HIDAPI=0 %command%
```

### Solution 4: Verify Device Permissions

Make sure udev rules are installed (you should have done this already):

```bash
# Check if rules exist
ls -la /etc/udev/rules.d/99-thrustmaster-t500rs.rules

# If not, install them
./install-udev-rules.sh

# Verify permissions
ls -la /dev/input/event* | grep "crw-rw-rw"
```

The wheel's event device should be `crw-rw-rw-` (world readable/writable).

### Solution 5: Check In-Game FFB Settings

Even if FFB is working at the driver level, games need proper configuration:

**Automobilista 2 Specific:**
1. **Options** → **Controls & Calibration**
2. **Force Feedback**:
   - **FFB Effects Level**: 100%
   - **Master Scale**: 100% (adjust to taste)
   - **FX Scale**: 100%
   - **Low Force Boost**: 0-20% (helps with detail)
3. **Steering**:
   - Make sure wheel is detected
   - Calibrate if needed
4. **Save** and **restart the game**

### Solution 6: Test with evtest

Verify the wheel is sending events:

```bash
# Find your wheel's event device
cat /proc/bus/input/devices | grep -A 5 "Thrustmaster"

# Test events (should show js0 or similar)
evtest /dev/input/event2  # Replace with your event number

# Turn the wheel - you should see events
# Press Ctrl+C to exit
```

### Solution 7: Create Steam Launch Script

For maximum compatibility, create a launch script:

**File: `~/launch_ams2_ffb.sh`**
```bash
#!/bin/bash
# Force correct SDL2 backend for T500RS FFB

export SDL_JOYSTICK_HIDAPI=0
export SDL_JOYSTICK_RAWINPUT=0
export SDL_JOYSTICK_DEVICE=/dev/input/event2  # Adjust if needed

# Optional: Force evdev backend
export SDL_JOYSTICK_BACKEND=linux

# Launch game
exec "$@"
```

Make it executable:
```bash
chmod +x ~/launch_ams2_ffb.sh
```

**Steam Launch Options:**
```bash
~/launch_ams2_ffb.sh %command%
```

### Solution 8: Verify SDL2 Detection

Test if SDL2 can see your wheel:

```bash
# Install SDL2 tools if needed
sudo pacman -S sdl2  # Arch/Manjaro
# or
sudo apt install libsdl2-dev  # Debian/Ubuntu

# Test SDL2 joystick detection
sdl2-jstest --list
sdl2-jstest /dev/input/js0  # Test your wheel
```

If SDL2 doesn't see force feedback, that's the issue.

### Solution 9: Check Proton Version

Some Proton versions have better FFB support:

1. **Right-click game** → **Properties** → **Compatibility**
2. **Force the use of a specific Steam Play compatibility tool**
3. Try different versions:
   - **Proton Experimental** (latest features)
   - **Proton 8.0** (stable)
   - **Proton GE** (community version with extra fixes)

**Install Proton GE:**
```bash
# Download from: https://github.com/GloriousEggroll/proton-ge-custom/releases
# Extract to: ~/.steam/steam/compatibilitytools.d/
```

### Solution 10: Game-Specific Fixes

#### Automobilista 2

**CRITICAL: Device Detection Issue**
- **Problem**: Wheel and pedals detected, but **no force feedback** (wheel turns freely)
- **Cause**: AMS2 may not properly detect FFB capabilities through Proton/SDL2
- **Solution**: Adjust in-game FFB settings to force enable effects

**Step-by-Step Fix:**

1. **Launch the game** with correct launch options: `SDL_JOYSTICK_DEVICE=/dev/input/eventXX %command%`

2. **Go to Options → Controls & Calibration**

3. **Force Feedback Settings** (CRITICAL):
   - **FFB Effects Level**: **100%** (must be 100% or FFB won't work!)
   - **Master Scale**: **100%** (start here, adjust later)
   - **FX Scale**: **100%**
   - **Low Force Boost**: **20-30%** (helps with detail on T500RS)
   - **Smoothing**: **0-5%** (too much kills detail)

4. **Advanced FFB Settings** (if available):
   - **Enable all effect types** (Spring, Damper, Friction, etc.)
   - **Tire Force**: **100%**
   - **Suspension Force**: **100%**

5. **SAVE and EXIT the game completely**

6. **Restart the game** - FFB should now work!

**If still no FFB:**
- Delete config folder: `~/.steam/steam/steamapps/compatdata/1066890/pfx/drive_c/users/steamuser/Documents/Automobilista 2/`
- Restart game and reconfigure from scratch
- Make sure **Steam Input is DISABLED** for the game

**Known issue**: AMS2 sometimes needs game restart after changing FFB settings

#### Assetto Corsa Competizione
- **Enable "Enhanced Understeer Effect"** in FFB settings
- **Set "Dynamic Damping" to 100%**
- **Minimum Force**: 0-5%

#### Other Games
- Check ProtonDB: https://www.protondb.com/
- Search for your game + "force feedback" + "Linux"

## Debugging Steps

### Step 1: Verify Kernel Driver Works

```bash
# Test with fftest
fftest /dev/input/event2

# Should show:
# - Force feedback effects types: Constant, Spring, Damper, etc.
# - Uploading effects should work
```

### Step 2: Check Steam is Not Blocking

```bash
# While game is running, check processes
ps aux | grep -i steam

# Check if Steam Input is active
ls -la ~/.steam/steam/controller_config/
```

### Step 3: Monitor FFB Commands

```bash
# In one terminal, watch kernel messages
sudo dmesg -w | grep -i "tmff\|t500"

# In another terminal, launch game
# You should see FFB commands when game tries to use FFB
```

### Step 4: Check Game Logs

```bash
# Proton logs location
~/.steam/steam/steamapps/compatdata/<APPID>/pfx/drive_c/users/steamuser/Temp/

# Enable Proton logging
PROTON_LOG=1 %command%

# Check for FFB-related errors
```

## Quick Checklist

For Automobilista 2 (or any Proton racing game):

- [ ] **Find your event device number**: `cat /proc/bus/input/devices | grep -A 5 "Thrustmaster"`
- [ ] **Disable Steam Input** for the game
- [ ] **Add launch options**: `SDL_JOYSTICK_DEVICE=/dev/input/eventXX %command%` (replace XX)
- [ ] **Verify udev rules** are installed
- [ ] **Check in-game FFB settings** are enabled and at 100%
- [ ] **Test with fftest** to confirm driver works
- [ ] **Restart game** after changing FFB settings
- [ ] **Try Proton Experimental** or Proton GE
- [ ] **Check ProtonDB** for game-specific tips

## Expected Behavior

**Working FFB:**
- Wheel resists when turning
- Feel bumps, curbs, understeer
- Wheel pulls in one direction during crashes
- Force changes with speed and grip

**Not Working:**
- Wheel turns freely (no resistance)
- No feedback from road surface
- No force during crashes
- Feels like FFB is off

## Still Not Working?

### Advanced Debugging

1. **Check if game is using the wheel at all:**
   ```bash
   # While game is running
   lsof | grep /dev/input/event2
   ```

2. **Verify FFB effects are being uploaded:**
   ```bash
   # Monitor kernel logs while playing
   sudo dmesg -w | grep "Upload\|Play\|Stop"
   ```

3. **Test with a different game:**
   - Try Assetto Corsa (original, not ACC)
   - Try DiRT Rally 2.0
   - If FFB works in one game but not another, it's game-specific

4. **Check Wine/Proton FFB implementation:**
   ```bash
   # Some games need specific Wine patches
   # Consider using Proton GE which includes community fixes
   ```

## Known Working Games

Games confirmed working with T500RS on Linux:

- ✅ **Live For Speed** (Wine) - Works perfectly
- ✅ **Assetto Corsa** (Proton) - Works with Steam Input disabled
- ✅ **DiRT Rally 2.0** (Proton) - Works with SDL2 env vars
- ✅ **F1 2020/2021** (Proton) - Works with Proton GE
- ⚠️ **Automobilista 2** (Proton) - Needs specific settings (see above)
- ⚠️ **ACC** (Proton) - Needs Enhanced Understeer Effect enabled

## Summary

**Most Common Fix for Automobilista 2:**

1. **Disable Steam Input** for the game
2. **Launch options**: `SDL_JOYSTICK_HIDAPI=0 %command%`
3. **In-game**: Set FFB to 100%, save, restart game
4. **If still not working**: Try Proton Experimental or Proton GE

**The Windows drivers you installed in the Proton prefix are not used!** Proton uses the Linux kernel driver (hid-tmff2) which is already working perfectly (as proven by Live For Speed).

The issue is purely about how Steam/Proton/SDL2 access the Linux FFB system.

## See Also

- **SYSFS_SETTINGS.md** - Adjust global FFB gain and per-effect gains
- **GUI_README.md** - Use GUI to adjust FFB strength
- **UDEV_SETUP.md** - Ensure proper device permissions
- **ProtonDB**: https://www.protondb.com/ - Game-specific reports

Good luck! 🏎️💨

