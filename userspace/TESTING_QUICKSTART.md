# T500RS Effect Translation Layer - Quick Start Testing Guide

## Prerequisites

Before testing, ensure you have:
- T500RS wheel connected via USB
- Linux with kernel 4.x or newer
- `libusb-1.0` development libraries installed
- Root/sudo access for USB device access

## Step 1: Build the Driver

```bash
cd /home/caz/Documents/hid-tmff2/userspace
make clean
make
```

Expected output:
```
rm -f t500rs-ffb test_all_effects *.o
gcc -Wall -Wextra -O2 -pthread -DUSE_WINDOWS_PROTOCOL=1 -c t500rs-ffb.c -o t500rs-ffb.o
gcc -Wall -Wextra -O2 -pthread -DUSE_WINDOWS_PROTOCOL=1 -c t500rs_protocol.c -o t500rs_protocol.o
gcc -Wall -Wextra -O2 -pthread -DUSE_WINDOWS_PROTOCOL=1 -c t500rs_effects.c -o t500rs_effects.o
gcc t500rs-ffb.o t500rs_protocol.o t500rs_effects.o -o t500rs-ffb -lusb-1.0 -pthread
```

✅ **Success**: Binary `t500rs-ffb` created (48KB)

## Step 2: Verify T500RS Connection

Check that the wheel is detected:

```bash
lsusb | grep "044f:b65"
```

Expected output (one of):
- `044f:b65d` - Boot mode (needs initialization)
- `044f:b65e` - Normal mode (already initialized)

## Step 3: Run the Driver

```bash
sudo ./t500rs-ffb
```

Expected output:
```
[INFO] T500RS Force Feedback Userspace Driver
[INFO] Initializing libusb...
[INFO] Found T500RS at bus X, device Y
[INFO] Opening device...
[INFO] Claiming interface...
[INFO] Initializing T500RS...
[INFO] Device initialized successfully
[INFO] Setting up uinput device...
[INFO] Created virtual device: /dev/input/eventX
[INFO] Driver running, press Ctrl+C to stop
```

✅ **Success**: Virtual input device created

## Step 4: Find Your Virtual Device

In a new terminal:

```bash
ls -l /dev/input/by-id/ | grep T500RS
```

Example output:
```
lrwxrwxrwx ... usb-Thrustmaster_T500RS_Racing_wheel-event-joystick -> ../event15
```

Note the event number (e.g., `event15`).

## Step 5: Quick Effect Test

### Test 1: Constant Force (Left)

```bash
sudo fftest /dev/input/event15
```

In fftest menu:
1. Select option `1` (upload effect)
2. Choose `Constant Force`
3. Set level: `-16000` (negative = left force)
4. Upload effect (ID 0)
5. Select option `2` (play effect)
6. Enter effect ID: `0`

**Expected**: Wheel pulls strongly to the LEFT

### Test 2: Constant Force (Right)

Repeat but set level: `+16000` (positive = right force)

**Expected**: Wheel pulls strongly to the RIGHT

### Test 3: Spring (Centering)

1. Upload new effect
2. Choose `Spring`
3. Set right coefficient: `10000`
4. Set left coefficient: `10000`
5. Play effect

**Expected**: Wheel resists movement, returns to center

### Test 4: Damper (Resistance)

1. Upload new effect
2. Choose `Damper`
3. Set coefficients: `8000`
4. Play effect

**Expected**: Wheel resists turning (velocity-based)

### Test 5: Periodic Sine Wave

1. Upload new effect
2. Choose `Periodic`
3. Select waveform: `Sine`
4. Set magnitude: `12000`
5. Set period: `1000` ms (1 second)
6. Play effect

**Expected**: Smooth back-and-forth oscillation

## Step 6: Advanced Testing with test_all_effects

```bash
sudo ./test_all_effects
```

This will automatically:
1. Upload all supported effect types
2. Play each effect for 3 seconds
3. Show detailed information

Watch for:
```
[EFFECTS] Translating effect: type=80, id=0
[EFFECTS DEBUG] Constant effect: level=16383 (after gain: 16383)
[EFFECTS DEBUG] Constant command: magnitude=16383, flags=0x00
[EFFECTS] ✅ Effect translation successful
[INFO] Effect 0 uploaded using Windows protocol
```

## Step 7: Verify Effect Translation

While driver is running, check logs for Windows protocol usage:

```bash
# In the terminal where t500rs-ffb is running:
# You should see:
[INFO] Effect X uploaded using Windows protocol
[EFFECTS] Translating effect: type=Y, id=X
[EFFECTS] ✅ Effect translation successful
```

## Step 8: Test Gain Control

Using fftest:

1. Set global gain to 50%:
   - In fftest, option `3` (set gain)
   - Enter: `32767` (50% of 65535)

2. Play a constant force effect
   - Should be half as strong

3. Test per-effect gains (if supported by your application):
   - Use custom events 0x70-0x75
   - Each effect type can have individual gain

## Troubleshooting

### Problem: "Device not found"

**Solution**:
```bash
# Check USB connection
lsusb | grep 044f

# Check permissions
sudo chmod 666 /dev/bus/usb/XXX/YYY  # Use actual bus/device numbers

# Or add udev rule (permanent):
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="044f", ATTR{idProduct}=="b65e", MODE="0666"' | sudo tee /etc/udev/rules.d/99-t500rs.rules
sudo udevadm control --reload-rules
```

### Problem: "Failed to claim interface"

**Solution**:
```bash
# Another driver might be using the device
# Unload hid-tmff2 kernel module if loaded:
sudo rmmod hid_tmff2

# Or kill existing t500rs-ffb instance:
sudo killall t500rs-ffb
```

### Problem: No force feedback

**Check**:
1. Effect uploaded successfully? Look for "✅ Effect translation successful"
2. Effect playing? Check fftest shows "playing"
3. USB communication OK? Look for USB errors in logs
4. Wheel in correct mode? Should be 044f:b65e, not b65d

### Problem: Weak forces

**Check**:
1. Global gain setting (should be 65535 for 100%)
2. Effect magnitude (try higher values)
3. Per-effect gain (should be 65535 for 100%)

## Comparing Windows vs Legacy Protocol

### Test Windows Protocol (default):

```bash
# Already enabled by default
sudo ./t500rs-ffb
# Look for: "Effect X uploaded using Windows protocol"
```

### Test Legacy Protocol:

```bash
# Edit Makefile, change:
CFLAGS = -Wall -Wextra -O2 -pthread -DUSE_WINDOWS_PROTOCOL=0

# Rebuild:
make clean && make

# Run:
sudo ./t500rs-ffb
# Should NOT see "Windows protocol" in logs
```

Compare feel and behavior of effects between protocols.

## Expected Behavior Summary

| Effect | Expected Sensation |
|--------|-------------------|
| Constant (positive) | Strong pull to RIGHT |
| Constant (negative) | Strong pull to LEFT |
| Spring | Returns to center, resists turning |
| Damper | Resists fast movements, smooth |
| Friction | Grainy resistance, position-based |
| Inertia | Heavy feel, resists acceleration |
| Sine wave | Smooth oscillation |
| Square wave | Sharp back-and-forth |

## Log Files

Capture complete logs for debugging:

```bash
sudo ./t500rs-ffb 2>&1 | tee test_$(date +%Y%m%d_%H%M%S).log
```

## Performance Testing

Monitor resource usage:

```bash
# In another terminal:
watch -n 1 'ps aux | grep t500rs-ffb'
```

Should show:
- CPU: < 5% normally, < 20% during heavy effect usage
- Memory: ~2-5 MB

## Safety Notes

⚠️ **Important Safety Information**:

1. **Force Limits**: Constant forces > 20000 can be very strong
2. **Duration**: Don't play max force for extended periods
3. **Stop Command**: Ctrl+C cleanly stops all effects
4. **Emergency Stop**: Power off wheel if behavior is erratic

## Success Criteria

✅ Driver builds without errors  
✅ Device detected and initialized  
✅ Virtual input device created  
✅ Effects translate successfully  
✅ Forces felt at wheel  
✅ Gain control works  
✅ Multiple effects can be uploaded  
✅ No USB errors during operation  

## Next Steps After Testing

If all tests pass:

1. **Document Results**: Note which effects work best
2. **Report Issues**: Log any problems with specific effects
3. **Performance Tuning**: Adjust gains for your preferences
4. **Extended Testing**: Try with actual games/sims
5. **Contribute**: Share findings and improvements

## Getting Help

If you encounter issues:

1. Check `EFFECT_TRANSLATION_LAYER.md` for technical details
2. Review `IMPLEMENTATION_SUMMARY.md` for known limitations
3. Examine log files for error messages
4. Test with both Windows and legacy protocols
5. Create issue report with:
   - Hardware: T500RS model/firmware
   - System: Linux kernel version, distro
   - Logs: Complete driver output
   - Problem: Specific effect or behavior

## References

- **Full Documentation**: `EFFECT_TRANSLATION_LAYER.md`
- **Implementation Details**: `IMPLEMENTATION_SUMMARY.md`
- **Windows Analysis**: `T500RS_REVERSE_ENGINEERING_ANALYSIS.md`
- **Future Plans**: `T500RS_IMPROVEMENT_PLAN.md`

---

**Last Updated**: January 2025  
**Driver Version**: 1.0 with Windows Protocol  
**Status**: Ready for Hardware Testing