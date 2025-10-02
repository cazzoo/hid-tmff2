# Racing Game Test Guide - T500RS Linux Driver

This guide helps you test the T500RS force feedback driver with popular racing games on Linux.

---

## Prerequisites

1. **Driver Running**
   ```bash
   cd ~/Documents/hid-tmff2/userspace
   sudo ./run.sh
   ```

2. **Check Device**
   ```bash
   # Find the event device
   ls -l /dev/input/by-id/ | grep T500RS
   
   # Test basic input
   evtest /dev/input/eventX  # Replace X with your device number
   ```

3. **Verify Force Feedback**
   ```bash
   # Run quick test
   sudo ./test_all_effects
   # Try test 1 (weak constant force) to verify it works
   ```

---

## Supported Racing Games on Linux

### Native Linux Games

1. **Assetto Corsa Competizione** (Steam)
2. **DiRT Rally 2.0** (Steam/Proton)
3. **F1 2020/2021/2022** (Steam/Proton)
4. **Project CARS 2** (Steam/Proton)
5. **Automobilista 2** (Steam/Proton)
6. **BeamNG.drive** (Steam)
7. **Wreckfest** (Steam)

### Windows Games via Proton/Wine

Most racing games work through Steam's Proton compatibility layer.

---

## Game-Specific Setup

### Assetto Corsa Competizione

**Setup:**
1. Launch game
2. Go to Settings → Controls
3. Select "T500RS Racing Wheel" or "Generic Wheel"
4. Enable Force Feedback
5. Set Force Feedback strength: 50-100%

**Expected Effects:**
- ✅ Road bumps and curbs (constant force)
- ✅ Self-centering (spring)
- ✅ Resistance when turning (damper)
- ✅ Vibrations from engine/road (periodic)

**Test Track:** Monza - Good mix of high-speed and technical sections

---

### DiRT Rally 2.0

**Setup:**
1. Launch game
2. Go to Options → Controls
3. Select wheel device
4. Enable Force Feedback
5. Set Self-Aligning Torque: 100%
6. Set Wheel Friction: 50%
7. Set Tire Friction: 50%

**Expected Effects:**
- ✅ Gravel/dirt surface feel (constant + periodic)
- ✅ Wheel pull from terrain (constant)
- ✅ Self-centering (spring)
- ✅ Suspension feedback (damper)

**Test Stage:** Greece - Varied terrain

---

### F1 2020/2021/2022

**Setup:**
1. Launch game
2. Go to Settings → Controls & Vibration
3. Select wheel
4. Enable Force Feedback
5. Set Force Feedback Strength: 70-100%
6. Set On-Track Effects: 50%
7. Set Rumble Strip Effects: 50%

**Expected Effects:**
- ✅ Downforce changes (constant)
- ✅ Kerb vibrations (periodic)
- ✅ Wheel resistance (damper)
- ✅ Lock-ups and slides (constant)

**Test Track:** Spa-Francorchamps - High-speed with elevation

---

### BeamNG.drive

**Setup:**
1. Launch game
2. Go to Options → Controls → Force Feedback
3. Enable Force Feedback
4. Set Strength: 100%
5. Set Smoothing: 0.5

**Expected Effects:**
- ✅ Realistic physics-based forces
- ✅ Crash impacts (constant)
- ✅ Terrain feedback (constant + periodic)
- ✅ Vehicle weight transfer (damper)

**Test Scenario:** West Coast USA - Free roam

---

## Testing Checklist

For each game, test these scenarios:

### Basic Functionality
- [ ] Wheel detected by game
- [ ] Steering input works correctly
- [ ] Pedals work (throttle, brake, clutch)
- [ ] Buttons work (shifter paddles, etc.)
- [ ] Force feedback is active

### Force Feedback Quality
- [ ] **Centering Force** - Wheel returns to center when released
- [ ] **Road Feel** - Can feel bumps and surface changes
- [ ] **Resistance** - Wheel gets heavier in corners
- [ ] **Vibrations** - Can feel engine/road vibrations
- [ ] **Impacts** - Feel crashes and collisions
- [ ] **No Oscillation** - Wheel doesn't shake uncontrollably
- [ ] **Smooth Operation** - No jerky or stuttering forces

### Performance
- [ ] No lag or delay in force feedback
- [ ] No stuttering or frame drops
- [ ] Driver stays running (no crashes)
- [ ] USB device stays connected
- [ ] No kernel errors (check `dmesg`)

---

## Troubleshooting

### Game Doesn't Detect Wheel

**Solution 1: Check uinput device**
```bash
ls -l /dev/input/by-id/ | grep T500RS
evtest /dev/input/eventX
```

**Solution 2: Restart driver**
```bash
sudo pkill t500rs-ffb
sudo ./run.sh
```

**Solution 3: Check permissions**
```bash
sudo chmod 666 /dev/uinput
```

### No Force Feedback in Game

**Solution 1: Enable in game settings**
- Make sure Force Feedback is enabled
- Set strength to 100%
- Disable any "Soft Lock" or "Damper" settings

**Solution 2: Test with test program**
```bash
sudo ./test_all_effects
# Try test 1 - if this works, the driver is fine
```

**Solution 3: Check driver logs**
```bash
# In the terminal where driver is running, look for:
# [DEBUG] Playing effect X
# [DEBUG] Starting effect X
```

### Force Feedback Too Strong/Weak

**Adjust in game settings:**
- Reduce Force Feedback Strength: 50-70%
- Reduce Self-Aligning Torque
- Increase Damper/Friction settings

**Note:** Gain control not yet implemented in driver

### Wheel Oscillates/Shakes

**Solution 1: Increase damper in game**
- Set Wheel Friction: 30-50%
- Set Damper: 30-50%

**Solution 2: Reduce force feedback strength**
- Lower overall FFB strength to 70-80%

### Driver Crashes or Stops

**Check kernel logs:**
```bash
dmesg | tail -50
```

**Restart driver:**
```bash
sudo pkill -9 t500rs-ffb
sudo ./run.sh
```

**If persistent, report issue with:**
- Game name and version
- Driver logs
- Kernel logs (dmesg)

---

## Performance Tuning

### For Best Experience

1. **Start with default settings**
   - FFB Strength: 100%
   - All effects enabled

2. **Adjust per game**
   - Some games need lower strength (70-80%)
   - Some need higher damper (30-50%)

3. **Monitor driver**
   - Watch for errors in driver terminal
   - Check `dmesg` for kernel issues

4. **Test different tracks**
   - Smooth tracks (Monza, Spa)
   - Bumpy tracks (Nürburgring, Bathurst)
   - Off-road (DiRT Rally stages)

---

## Reporting Issues

If you encounter problems, please report with:

1. **Game Information**
   - Game name and version
   - Native or Proton/Wine
   - Game settings (FFB strength, etc.)

2. **Driver Logs**
   ```bash
   # Copy output from driver terminal
   ```

3. **Kernel Logs**
   ```bash
   dmesg | tail -100 > kernel_log.txt
   ```

4. **System Information**
   ```bash
   uname -a
   lsusb | grep 044f
   ```

5. **Description**
   - What you expected
   - What actually happened
   - Steps to reproduce

---

## Expected Results

### What Should Work
- ✅ All steering and pedal inputs
- ✅ Road surface feedback
- ✅ Centering force
- ✅ Resistance in corners
- ✅ Vibrations from engine/road
- ✅ Impact forces from crashes

### What Might Not Work
- ⚠️ Gain control (not implemented yet)
- ⚠️ Autocenter (not implemented yet)
- ❌ Ramp effects (disabled due to bug)

### Known Limitations
- Ramp effects disabled (rarely used in games)
- Gain control not implemented (use in-game settings)
- Autocenter not implemented (wheel won't self-center when no game running)

---

## Success Criteria

The driver is working correctly if:
1. ✅ Game detects the wheel
2. ✅ All inputs work (steering, pedals, buttons)
3. ✅ Force feedback is active and responsive
4. ✅ Can feel road surface and impacts
5. ✅ Wheel self-centers when released
6. ✅ No crashes or disconnects
7. ✅ Smooth and realistic feel

---

## Next Steps After Testing

Once you've tested with games:

1. **Report Results**
   - Which games work
   - Which games have issues
   - Overall experience rating

2. **Fine-Tune Settings**
   - Document optimal settings per game
   - Share with community

3. **Request Features**
   - Gain control
   - Autocenter
   - Game-specific profiles

---

## Community

Share your experience:
- GitHub Issues: Report bugs and request features
- Reddit: r/simracing, r/linux_gaming
- Discord: Linux gaming communities

---

**Happy Racing! 🏁🎮🚗💨**

