# T500RS Force Loss After ~9 Seconds - Troubleshooting Log

## Issue Summary

**Symptom**: Force feedback works initially but drops to zero or becomes unfelt after approximately 9 seconds during gameplay in both Automobilista 2 (AMS2) and Assetto Corsa Competizione (ACC) under Proton/Steam.

**Affected Games**:
- Automobilista 2 (Proton/Steam)
- Assetto Corsa Competizione (ACC) (Proton/Steam)

**Unaffected**:
- Live For Speed (Wine) - FFB works continuously
- fftest - FFB works continuously

**Timing**: Consistently occurs after approximately 9 seconds (not 10 seconds)

**User Observations**:
- "I have a left turn, I take it, I feel forces, and still in the curve, with same car speed, wheel angle, the force that was applied (almost) constantly to the wheel suddenly drops!"
- "If I get to the menu, and exists still in the same curve, the FF is activated back, just like before it dropped."
- In ACC: Getting to menu doesn't restore FFB - must go to control settings
- In AMS2: Menu forces are strong for ~10s then wheel "releases"
- The 3 force updates sent with level=-1 were **not felt AT ALL**

## Root Cause Analysis

### Initial Hypothesis (INCORRECT)
- Thought it was a 10-second timeout in driver or wheel firmware
- **Result**: No timeout found in driver code

### Second Hypothesis (INCORRECT)
- Thought the game was sending very weak forces that needed amplification
- **Result**: Games ARE sending weak forces, but amplification caused other issues

### Actual Root Cause (CONFIRMED via logs)

**Pattern observed in dmesg logs**:

```
Force update #403 (9687 ms): sending level=-36  ← Strong force
Force update #404 (9710 ms): sending level=0    ← Rounds to 0!
Force update #405-408: All sending level=0
Timer stopped at 9810 ms
```

**After fix to preserve sign**:
```
Force update #404 (9597 ms): sending level=66   ← Strong force
Force update #405 (9620 ms): sending level=-1   ← Now -1 instead of 0
Force update #406-407: sending level=-1
Timer stopped at 9677 ms (user killed game)
```

**Conclusion**: 
1. Games (AMS2/ACC) send very small force values (level=-1 to +1) after ~9 seconds
2. These values scale to 0 or ±1 in 8-bit range
3. **T500RS hardware does NOT respond to forces of ±1** (user confirmed: "not felt AT ALL")
4. This appears to be a **hardware deadzone** in the T500RS motors

## Attempted Solutions

### Attempt 1: Minimum Threshold ±1 (CURRENT)
**Date**: 2025-10-28
**Code Location**: `src/tmt500rs/hid-tmt500rs-usb.c` lines 434-448 and 926-940
**Implementation**:
```c
if (level == 0) {
    signed_level = 0;
} else {
    scaled = (level * 127) / 32767;
    if (scaled == 0) {
        /* Preserve sign for very small non-zero forces */
        signed_level = (level > 0) ? 1 : -1;
    } else {
        signed_level = (s8)scaled;
    }
}
```
**Result**: Driver sends -1, but **user cannot feel it** - T500RS doesn't respond to ±1

### Attempt 2: Minimum Threshold ±5 (TESTED - FAILED)
**Date**: 2025-10-28
**Implementation**: Amplified forces that scale to -4 to +4 to ±5
**Result**: Driver sends -5 (0xfb) continuously, but **user cannot feel it** - T500RS doesn't respond to ±5
**Logs**: Force updates #399-433 all sending level=-5, no force felt

### Attempt 3: Minimum Threshold ±10 (TESTED - FAILED)
**Date**: 2025-10-28
**Implementation**: Amplified forces that scale to -9 to +9 to ±10
**Result**: Driver sends -10 (0xf6) continuously, but **user cannot feel it** - T500RS doesn't respond to ±10
**Logs**: Force updates sending level=-10, no force felt

### Attempt 4: Deadzone ±2000 (TESTED - FAILED)
**Date**: 2025-10-28
**Implementation**: Treat forces below ±2000 (6% of max) as zero instead of amplifying
**Reasoning**: If game sends -1 (0.003%), it's intentionally saying "almost zero"
**Result**: Same behavior - game still sends -1 after ~9 seconds, forces drop to zero
**Logs**: `Upload constant: id=1, level=-1` after normal forces

### Attempt 5: Minimum Threshold ±15 (PREVIOUSLY TRIED)
**Implementation**: Amplified forces that scale to -14 to +14 to ±15
**Result**: User reported: "The clamp stuff doesn't help anyhow, we should remove it, it is complexifying the system for nothing."
**Issue**: Clamping was masking actual force variations from the game

## Critical Insight

**THE GAME IS SENDING -1 INTENTIONALLY AFTER ~9 SECONDS**

This is NOT a driver bug. The pattern is consistent across all tests:
1. Game sends normal forces (5000-15000 range) for ~9 seconds
2. Game suddenly sends level=-1 (0.003% of maximum)
3. Driver correctly processes this as zero or very weak force
4. User feels no force because -1 is too weak for T500RS hardware

**This appears to be a Proton/Wine translation issue or game-specific behavior.**

The question is: **Why does the game send -1?** Possible reasons:
1. Proton/Wine is incorrectly translating Windows FFB API calls
2. Game has a bug in its Linux/Proton FFB implementation
3. Game is checking for a specific Windows driver response that we're not providing
4. There's a missing initialization or capability flag

## Key Questions to Answer

1. **What is the minimum force value the T500RS hardware actually responds to?**
   - Need to test: ±1, ±5, ±10, ±15, ±20
   - Method: Use fftest or custom test to send specific force values

2. **Why do AMS2/ACC send such weak forces after ~9 seconds?**
   - Is this game behavior or Proton/Wine translation issue?
   - Does this happen on Windows with official drivers?

3. **How do Windows drivers handle this?**
   - Do they amplify weak forces?
   - Do they have a minimum threshold?
   - Need to analyze Windows USB captures

4. **Is there a T500RS-specific command to adjust motor deadzone?**
   - Check USB protocol documentation
   - Check if there's a sensitivity/deadzone setting command

## Next Steps

### Step 1: Find Minimum Feelable Force
Create a test to determine the minimum force value the T500RS responds to:
- Test values: ±1, ±3, ±5, ±7, ±10, ±15, ±20
- Use fftest or custom kernel module test
- Document the minimum value that produces feelable force

### Step 2: Implement Adaptive Minimum Threshold
Once minimum value is known (let's call it `MIN_FORCE`):
```c
if (level == 0) {
    signed_level = 0;
} else {
    scaled = (level * 127) / 32767;
    if (scaled >= -MIN_FORCE && scaled <= MIN_FORCE && scaled != 0) {
        /* Amplify to minimum feelable level */
        signed_level = (level > 0) ? MIN_FORCE : -MIN_FORCE;
    } else {
        signed_level = (s8)scaled;
    }
}
```

### Step 3: Make Minimum Threshold Configurable
Add sysfs attribute to allow users to adjust minimum force threshold:
- `/sys/bus/hid/devices/.../min_force_threshold`
- Default value based on testing
- Range: 0-20 (0 = disabled)

### Step 4: Investigate Game Behavior
- Test with other racing games (iRacing, rFactor 2, etc.)
- Compare behavior between Wine and Proton
- Check if issue occurs with other Thrustmaster wheels (T300RS, TX)

## Technical Notes

### Force Scaling Math
- Input range: -32767 to +32767 (16-bit signed)
- Output range: -127 to +127 (8-bit signed)
- Scaling formula: `(level * 127) / 32767`
- Values -257 to +257 round to 0
- Value -258 rounds to -1, value +258 rounds to +1

### T500RS USB Protocol
- Report 0x03: Force level command
- Format: `[0x03] [0x0e] [0x00] [force_level]`
- force_level is unsigned 8-bit (0-255)
- Negative forces use two's complement: -1 = 0xff, -127 = 0x81

### Timer Behavior
- Force updates sent at 50Hz (every 20ms)
- Timer runs continuously while effect is active
- Timer stops when effect is stopped or force becomes 0

## Related Files
- `src/tmt500rs/hid-tmt500rs-usb.c` - Main implementation
- `cline_docs/t500rs_implementation_plan.md` - Overall implementation plan
- `cline_docs/test_troubleshooting.md` - General troubleshooting guide

## References
- USB captures showing force commands
- Windows driver behavior (needs investigation)
- T500RS hardware specifications (needs investigation)

