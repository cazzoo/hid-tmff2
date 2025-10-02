# T500RS Safety Incident Report

## Date: 2025-10-01

## What Happened

The T500RS wheel entered **bootloader/recovery mode** after we sent force feedback commands. The device:
- Lost power (green light went off)
- Stopped responding
- Required special recovery to restore

## Root Cause

We sent commands that the T500RS firmware interpreted as invalid or dangerous:
- Initialization commands (Report 0x42, 0x0a)
- Effect parameters (Report 0x01)
- Control commands (Report 0x41)

The firmware's safety mechanism triggered and put the device into bootloader mode to prevent potential damage.

## Recovery Method

User successfully recovered by:
1. Complete power cycle (unplug everything)
2. Wait 10+ minutes
3. Device eventually exited bootloader mode
4. Normal operation restored

## Lessons Learned

### What Went Wrong

1. **Sent untested commands** - We used Windows capture data but didn't verify each command individually
2. **No safety mode** - Driver sent commands immediately without testing
3. **Initialization sequence** - Reports 0x42 and 0x0a may have triggered firmware protection
4. **Too aggressive** - Sent multiple command types without understanding interactions

### What We Should Have Done

1. **Start read-only** - Monitor device without sending anything
2. **Test incrementally** - One command type at a time
3. **Add safety mode** - Dry-run mode that logs but doesn't send
4. **Longer delays** - Give device time to process
5. **Verify in Windows first** - Ensure baseline functionality

## Safety Measures Implemented

### 1. Safe Mode (ENABLED by default)

```c
#define T500RS_SAFE_MODE 1  // Set to 0 to enable output (DANGEROUS!)
```

When enabled:
- ✅ All commands are logged but NOT sent
- ✅ Device is protected from bad commands
- ✅ We can test logic without risk
- ✅ User must explicitly disable to send commands

### 2. Initialization Disabled

The initialization sequence (Reports 0x42, 0x0a) is now disabled:
- These may have triggered bootloader mode
- Need to understand them better before using
- Windows may send these in specific context we don't understand

### 3. Increased Delays

- Changed from 2ms to 10ms between commands
- Gives device more time to process
- Reduces risk of overwhelming firmware

### 4. Better Error Checking

- Added null pointer checks
- Validate buffer sizes
- Log everything for debugging

## Current Status

### Driver State
- ✅ Compiles successfully
- ✅ Safe mode ENABLED
- ✅ Will NOT send commands to device
- ✅ Logs what it WOULD send
- ⚠️ Force feedback disabled for safety

### Device State
- ✅ Recovered and working
- ✅ Normal operation restored
- ⚠️ Should test in Windows first
- ⚠️ Verify no permanent damage

## Next Steps (CAREFUL!)

### Phase 1: Verification (SAFE)
1. ✅ Test wheel in Windows - verify full functionality
2. ✅ Load driver in safe mode - verify no issues
3. ✅ Monitor logs - see what would be sent
4. ✅ Verify device stays stable

### Phase 2: Analysis (SAFE)
1. Study Windows capture more carefully
2. Identify MINIMAL command set needed
3. Understand initialization sequence
4. Find what triggered bootloader mode

### Phase 3: Incremental Testing (RISKY - DO NOT DO YET!)
**Only after understanding what went wrong:**

1. Test ONLY stop commands (safest)
2. Test ONLY parameter upload (no play)
3. Test ONLY play commands (no parameters)
4. Test complete sequence with long delays

### Phase 4: Full Implementation (VERY RISKY)
**Only after Phase 3 succeeds:**

1. Enable one command type at a time
2. Test extensively between each
3. Have recovery plan ready
4. Consider asking Thrustmaster for documentation

## Recommendations

### Immediate
1. ✅ Keep safe mode ENABLED
2. ✅ Test in Windows to verify device health
3. ✅ Load driver and verify stability
4. ✅ Review logs to understand what would happen

### Short Term
1. Study Windows capture in detail
2. Find minimal working command set
3. Understand why bootloader mode triggered
4. Consider reaching out to:
   - Thrustmaster support
   - Linux kernel HID developers
   - Other T500RS Linux users

### Long Term
1. Get official protocol documentation
2. Implement proper error handling
3. Add firmware version detection
4. Create comprehensive test suite

## Warning Signs to Watch For

If testing with safe mode disabled, STOP IMMEDIATELY if:
- ⚠️ Device makes unusual sounds
- ⚠️ Green light flickers or dims
- ⚠️ Wheel becomes unresponsive
- ⚠️ Excessive heat from motor
- ⚠️ Any error messages in dmesg
- ⚠️ Device disconnects/reconnects

## Recovery Procedure (If It Happens Again)

1. **Immediately unload driver**: `sudo rmmod hid_tmff_new`
2. **Unplug USB**
3. **Unplug power**
4. **Wait 10+ minutes**
5. **Plug power back in**
6. **Wait for green light**
7. **Test in Windows first**
8. **Do NOT reload driver until understood**

## Technical Analysis

### Suspected Culprits

1. **Report 0x42 (Init)**: `42 01 00 00 00 00 00 00 00 00 00 00 00 00`
   - May be firmware update command
   - May require specific context
   - Windows may send this only once at boot

2. **Report 0x0a (Config)**: `0a 04 90 03 00 00 00 00 00 00 00 00 00 00`
   - Unknown purpose
   - May set device mode
   - May have triggered protection

3. **Command Sequence**:
   - We sent init → config → effects → play
   - Windows may use different sequence
   - Timing may be critical

### What We Know Works (from Windows)
- Report 0x01 (effect parameters) - Windows sends these
- Report 0x41 (effect control) - Windows sends these
- Device accepts these in Windows

### What We Don't Know
- Why same commands triggered bootloader in Linux
- If HID layer adds/modifies data
- If timing is critical
- If device state matters

## Conclusion

We made significant progress but learned an important lesson about safety. The T500RS has robust protection mechanisms, which is good. We now have:

- ✅ Safe mode implementation
- ✅ Better understanding of risks
- ✅ Recovery procedure
- ✅ Incremental testing plan

**DO NOT DISABLE SAFE MODE** without:
1. Understanding what went wrong
2. Having recovery plan ready
3. Testing in Windows first
4. Being prepared for bootloader mode again

The goal is working force feedback, but **device safety comes first**.

---

## For Future Sessions

1. Start with safe mode enabled
2. Review logs to see what would be sent
3. Analyze Windows capture more carefully
4. Consider alternative approaches:
   - Use existing kernel drivers as reference
   - Contact Thrustmaster for documentation
   - Find other T500RS Linux implementations
   - Ask kernel developers for help

**Safety first, force feedback second.** 🛡️

