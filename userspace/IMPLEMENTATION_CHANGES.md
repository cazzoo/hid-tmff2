# T500RS Driver Implementation Changes

## Overview

Implementation of improvements based on comprehensive USB capture analysis.

## Changes Implemented

### 1. Report 0x02 - Fixed Format for Continuous Updates ✅

**Previous Implementation:**
```c
// Sent once during upload with all zeros
buf[0] = 0x02;
buf[1] = 0x1c;
buf[2] = 0x00;
buf[3] = 0x00;  // All zeros
buf[4] = 0x00;
buf[5] = 0x00;
buf[6] = 0x00;
buf[7] = 0x00;
buf[8] = 0x00;
```

**New Implementation:**
```c
// New function: send_force_update()
// Sends continuous force updates with proper format
buf[0] = 0x02;
buf[1] = 0x1c;
buf[2] = 0x00;
buf[3] = magnitude & 0xff;        // Magnitude low byte (0-1500)
buf[4] = (magnitude >> 8) & 0xff; // Magnitude high byte
buf[5] = direction;                // 0x5e or 0x3f based on force sign
buf[6] = 0x00;
buf[7] = 0x00;
buf[8] = 0x21;                     // Constant from captures
```

**Changes:**
- Added `send_force_update()` function
- Magnitude scaled to 0-1500 range (from USB captures)
- Direction flag: 0x5e for positive force, 0x3f for negative
- Byte 8 set to 0x21 (constant from captures)

**Status:** ✅ Implemented

### 2. Removed Report 0x03 Usage ✅

**Previous Implementation:**
```c
// Used Report 0x03 to set force level
buf[0] = 0x03;
buf[1] = 0x0e;
buf[2] = 0x00;
buf[3] = level;  // Signed force level
```

**New Implementation:**
- Removed Report 0x03 from `start_effect()`
- Replaced with `send_force_update()` using Report 0x02
- Report 0x03 not found in TShark analysis of Windows captures

**Rationale:**
- TShark analysis found NO Report 0x03 in Windows USB captures
- Windows driver uses Report 0x02 for continuous updates
- Report 0x03 may not be correct protocol

**Status:** ✅ Implemented

### 3. Report 0x05 - Already Correct ✅

**Current Implementation:**
```c
// First report (0x0e)
buf[0] = 0x05;
buf[1] = 0x0e;
// ... coefficients

// Second report (0x1c)
buf[0] = 0x05;
buf[1] = 0x1c;
// ... deadband and center
```

**Analysis:**
- Driver already sends TWO Report 0x05 transfers
- Matches manual capture analysis
- No changes needed

**Status:** ✅ Already correct

## Testing Required

### Test 1: Constant Force Direction

**Purpose:** Verify direction encoding (0x5e vs 0x3f)

**Method:**
```bash
sudo ./test_all_effects
```

**Expected:**
- Positive force should pull in one direction
- Negative force should pull in opposite direction
- If reversed, swap 0x5e and 0x3f in code

**Current Mapping:**
- `force >= 0` → `0x5e`
- `force < 0` → `0x3f`

### Test 2: Force Magnitude Scaling

**Purpose:** Verify 0-1500 magnitude range is correct

**Method:**
```bash
sudo ./test_all_effects
```

**Expected:**
- Force should scale smoothly from weak to strong
- Maximum force should feel appropriate (not too weak or too strong)

**Current Scaling:**
```c
magnitude = (abs(force_level) * 1500) / 32767;
```

### Test 3: Periodic Effects

**Purpose:** Verify periodic effects still work without Report 0x03

**Method:**
```bash
sudo ./test_all_effects
```

**Expected:**
- Sine, triangle, square, sawtooth waves should work
- Magnitude should be controlled by Report 0x04

### Test 4: Condition Effects

**Purpose:** Verify spring, damper, friction, inertia still work

**Method:**
```bash
sudo ./test_all_effects
```

**Expected:**
- All condition effects should work as before
- Two Report 0x05 transfers already implemented

## Known Issues

### Issue 1: Continuous Updates Not Implemented

**Problem:**
- `send_force_update()` is called once when effect starts
- Should be called continuously during effect playback
- Windows driver sends Report 0x02 continuously (238 times in capture)

**Solution (Future):**
- Create update thread that runs during effect playback
- Send Report 0x02 every 10-20ms
- Update magnitude based on envelope (attack/fade)

**Priority:** HIGH (but not in this commit)

### Issue 2: Direction Flag Needs Verification

**Problem:**
- Direction encoding (0x5e vs 0x3f) is a guess
- May be reversed

**Solution:**
- Test with actual wheel
- If direction is wrong, swap the values

**Priority:** HIGH (test immediately)

### Issue 3: Envelope Not Implemented

**Problem:**
- Attack and fade parameters not used
- Report 0x02 sent with zeros for envelope during upload
- Continuous updates don't implement envelope

**Solution (Future):**
- Parse envelope parameters from effect
- Implement attack/fade in continuous update loop
- Gradually increase/decrease magnitude

**Priority:** MEDIUM

## Code Changes Summary

### New Functions

1. **`send_force_update(int force_level)`**
   - Sends Report 0x02 with proper format
   - Calculates magnitude (0-1500)
   - Sets direction flag (0x5e/0x3f)
   - Sets constant 0x21 in byte 8

### Modified Functions

1. **`upload_constant_effect()`**
   - Updated comments to explain Report 0x02 dual purpose
   - Envelope parameters still sent as zeros (for now)

2. **`start_effect()`**
   - Removed Report 0x03 usage for constant force
   - Replaced with `send_force_update()` call
   - Removed Report 0x03 usage for periodic effects

### Unchanged Functions

1. **`upload_condition_effect()`**
   - Already sends two Report 0x05 transfers
   - No changes needed

2. **`upload_periodic_effect()`**
   - Uses Report 0x04 for parameters
   - No changes needed

3. **`stop_effect()`**
   - Uses Report 0x41 with 0x00
   - No changes needed

## Verification Checklist

- [x] Code compiles without errors
- [ ] Constant force works in both directions
- [ ] Direction encoding is correct (0x5e/0x3f)
- [ ] Magnitude scaling feels appropriate
- [ ] Periodic effects still work
- [ ] Condition effects still work
- [ ] No regression in existing functionality

## Next Steps

### Immediate (This Session)
1. Test constant force direction
2. Verify magnitude scaling
3. Test all effect types
4. Fix direction encoding if reversed

### Future (Next Session)
1. Implement continuous update loop
2. Add envelope support (attack/fade)
3. Optimize update frequency
4. Add force smoothing/interpolation

## References

- **CAPTURE_ANALYSIS.md** - Mode switch and initialization
- **FFB_CAPTURE_ANALYSIS.md** - TShark FFB analysis
- **FFB_PROTOCOL_COMPLETE.md** - Combined protocol analysis
- **PROTOCOL_COMPARISON.md** - Ghidra vs USB captures
- **ANALYSIS_SUMMARY.md** - Executive summary

---

**Implementation Date:** 2025-01-06
**Status:** Initial implementation complete, testing required
**Priority:** Test direction encoding immediately

