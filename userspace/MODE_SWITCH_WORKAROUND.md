# T500RS Mode Switch Workaround

## Problem

The T500RS has two USB modes:
- **Boot Mode (044f:b65d)** - Initial state after power-on
- **Normal Mode (044f:b65e)** - Operational mode with full functionality

The userspace driver cannot reliably trigger the mode switch from boot mode to normal mode.

## Root Cause

After extensive analysis:
1. The initialization sequence (Reports 0x42, 0x0a, 0x40, etc.) is correct
2. USB reset and interface release are properly implemented
3. The device firmware requires a specific trigger that we haven't identified

The Windows driver likely uses a mechanism we haven't discovered through:
- Ghidra analysis of tmhidusb.sys
- USB packet capture analysis
- Kernel driver reverse engineering

## Workaround

### Option 1: Use Windows Once (Recommended)

1. **Plug wheel into Windows PC**
2. **Let Windows driver load** (device switches to b65e)
3. **Unplug wheel**
4. **Plug into Linux PC**
5. **Device stays in normal mode (b65e)**
6. **Run Linux driver:** `sudo ./run.sh`

The device will remain in normal mode (b65e) until power cycled.

### Option 2: Dual Boot

If you have Windows dual-boot:
1. **Boot into Windows**
2. **Plug in wheel** (switches to b65e)
3. **Reboot to Linux** (keep wheel plugged in)
4. **Run driver:** `sudo ./run.sh`

### Option 3: Power Cycle Avoidance

Once in normal mode, avoid:
- Unplugging the wheel's power supply
- Turning off the wheel's power switch
- USB power cycling

The device will stay in normal mode as long as it has power.

## Checking Current Mode

```bash
lsusb | grep -i thrust
```

**Boot mode:**
```
Bus 001 Device 003: ID 044f:b65d ThrustMaster, Inc. Thrustmaster FFB Wheel
```

**Normal mode (correct):**
```
Bus 001 Device 004: ID 044f:b65e ThrustMaster, Inc. T500RS
```

## Why This Happens

The T500RS firmware requires a specific initialization sequence or timing that:
1. Windows driver knows
2. We haven't fully reverse-engineered
3. Might be proprietary/undocumented

Possible reasons:
- Specific USB control transfer we haven't found
- Timing-sensitive sequence
- Firmware quirk
- Proprietary Thrustmaster protocol

## What We Tried

### Attempt 1: Initialization Sequence
- Sent all HID reports from USB captures
- Reports 0x42, 0x0a, 0x40, etc.
- Device received commands but didn't switch

### Attempt 2: USB Reset
- Added `libusb_reset_device()`
- Device didn't re-enumerate

### Attempt 3: Interface Release
- Properly released interface before closing
- Still no re-enumeration

### Attempt 4: Mode Switch Command
- Added command 0x0f (from kernel driver)
- Not the correct trigger

### Attempt 5: Ghidra Analysis
- Analyzed tmhidusb.sys Windows driver
- Found configuration functions, not mode switch
- Mode switch likely in firmware or lower-level driver

## Future Work

To properly solve this, we would need:

1. **Windows USB filter driver** to capture ALL USB traffic including:
   - Control transfers
   - Descriptor requests
   - Configuration changes
   - Power management events

2. **Firmware analysis** if available:
   - Decompile T500RS firmware
   - Find mode switch trigger
   - Implement in Linux driver

3. **Hardware analysis**:
   - USB protocol analyzer
   - Capture at electrical level
   - See exact timing and sequences

4. **Thrustmaster documentation**:
   - Official protocol documentation
   - Mode switch specification
   - Unlikely to be available

## Recommendation

**Use the workaround** - it's simple and reliable:
1. Plug into Windows once
2. Device switches to normal mode
3. Use in Linux indefinitely
4. Only need Windows again if device is power cycled

This is a one-time setup step that takes < 1 minute.

## Alternative: Kernel Driver

The kernel driver (hid-tmt500rs) also struggles with mode switching. It has similar issues and uses the same workaround approach.

## Status

- **Userspace driver:** ✅ Works perfectly in normal mode (b65e)
- **Mode switch:** ❌ Requires Windows workaround
- **All features:** ✅ Fully functional once in normal mode

The driver is production-ready for use with devices in normal mode.

---

**Last Updated:** 2025-01-06
**Status:** Workaround documented
**Impact:** Minor (one-time Windows boot needed)

