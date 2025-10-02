# T500RS Careful Testing Guide

## ⚠️ IMPORTANT: Read This First!

The T500RS entered bootloader mode during previous testing. We must proceed with **extreme caution**.

## Current Safety Status

✅ **Safe Mode ENABLED** - No commands sent by default
✅ **Initialization DISABLED** - No 0x42/0x0a commands
✅ **Incremental flags** - Can enable specific commands one at a time

## Before ANY Testing

### 1. Verify Device Health
- [ ] Test wheel in Windows
- [ ] Confirm force feedback works in Windows
- [ ] Confirm calibration works
- [ ] Confirm no unusual behavior

### 2. Have Recovery Plan Ready
- [ ] Know how to unload driver: `sudo rmmod hid_tmff_new`
- [ ] Know how to power cycle device
- [ ] Have Windows available for recovery
- [ ] Be prepared to wait 10+ minutes if needed

### 3. Monitor Device
- [ ] Watch for green light status
- [ ] Listen for unusual sounds
- [ ] Feel for excessive heat
- [ ] Watch dmesg for errors

## Testing Phases

### Phase 0: Safe Mode (CURRENT - SAFE)

**Status**: All commands blocked
**Risk**: None
**Purpose**: Verify driver loads and logic works

**Test**:
```bash
sudo ./test_with_debug.sh
```

**Expected**: See `[SAFE MODE - NOT SENT]` messages
**Action**: Review logs, verify commands look correct

---

### Phase 1: Stop Commands Only (LOW RISK)

**Why start here**: Stop commands are safest - they tell device to do nothing

**Enable**:
Edit `src/tmt500rs/hid-tmt500rs-simple.h`:
```c
#define T500RS_ALLOW_STOP_ONLY 1  // Change from 0 to 1
```

**Rebuild**:
```bash
make
```

**Test**:
```bash
sudo ./test_with_debug.sh
```

**Expected**:
- See `[STOP ALLOWED]` messages
- Device should remain stable
- No force feedback (we're only sending stop)

**Watch for**:
- ⚠️ Device disconnecting
- ⚠️ Green light changing
- ⚠️ Any errors in dmesg

**If problems**: Immediately `sudo rmmod hid_tmff_new` and power cycle

**If successful**: Device stays stable, no issues → Proceed to Phase 2

---

### Phase 2: Parameter Upload (MEDIUM RISK)

**Why**: Need to upload effect parameters before playing

**Enable**:
Edit `src/tmt500rs/hid-tmt500rs-simple.h`:
```c
#define T500RS_ALLOW_STOP_ONLY 1  // Keep enabled
#define T500RS_ALLOW_PARAMS 1     // Change from 0 to 1
```

**Rebuild and test** as above

**Expected**:
- See `[PARAMS ALLOWED]` messages
- Device accepts parameter uploads
- Still no force feedback (not sending start yet)

**Watch for same warning signs**

**If successful**: Proceed to Phase 3

---

### Phase 3: Start Commands (HIGH RISK)

**Why**: This actually plays effects - most likely to cause issues

**Enable**:
Edit `src/tmt500rs/hid-tmt500rs-simple.h`:
```c
#define T500RS_ALLOW_STOP_ONLY 1  // Keep enabled
#define T500RS_ALLOW_PARAMS 1     // Keep enabled
#define T500RS_ALLOW_START 1      // Change from 0 to 1
```

**Rebuild and test**

**Expected**:
- See `[START ALLOWED]` messages
- **FORCE FEEDBACK SHOULD WORK!** 🎉
- Feel effects in wheel

**Watch for**:
- ⚠️ Excessive force
- ⚠️ Unusual behavior
- ⚠️ Device instability

**If successful**: Force feedback works! 🎉

---

### Phase 4: Disable Safe Mode (VERY HIGH RISK - NOT RECOMMENDED YET)

**Only if Phase 3 succeeds perfectly**

Edit `src/tmt500rs/hid-tmt500rs-simple.h`:
```c
#define T500RS_SAFE_MODE 0  // DANGEROUS!
```

This removes all safety checks. **Not recommended** until we understand why bootloader mode happened.

---

## Emergency Procedures

### If Device Becomes Unresponsive

1. **Immediately**:
   ```bash
   sudo rmmod hid_tmff_new
   ```

2. **Unplug USB**

3. **Unplug power**

4. **Wait 10 minutes**

5. **Plug power back in**

6. **Wait for green light**

7. **Test in Windows**

8. **Do NOT reload driver** until understood

### If Bootloader Mode Again

1. Follow emergency procedure above

2. **STOP ALL TESTING**

3. Review what was sent in dmesg

4. Analyze why it happened

5. Consider:
   - Asking Thrustmaster for documentation
   - Contacting kernel developers
   - Finding other T500RS Linux users
   - Using different approach

## What We're Testing

### Commands We Know Are Safe (from Windows):
- `41 XX 00 01` - Stop effect XX
- `01 XX ...` - Upload effect XX parameters
- `41 XX 41 01` - Start effect XX

### Commands We're AVOIDING:
- `42 01 00 ...` - Initialization (triggered bootloader!)
- `0a 04 90 ...` - Configuration (may have triggered bootloader!)

## Success Criteria

### Phase 1 Success:
- ✅ Device stays powered
- ✅ No disconnects
- ✅ Stop commands accepted
- ✅ No errors in dmesg

### Phase 2 Success:
- ✅ All Phase 1 criteria
- ✅ Parameter uploads accepted
- ✅ Device remains stable

### Phase 3 Success:
- ✅ All Phase 2 criteria
- ✅ **FORCE FEEDBACK WORKS!**
- ✅ Effects can be felt
- ✅ Device stable during effects

## Failure Criteria (STOP IMMEDIATELY)

- ❌ Device disconnects
- ❌ Green light goes off
- ❌ Unusual sounds from motor
- ❌ Excessive heat
- ❌ Errors in dmesg
- ❌ Device becomes unresponsive
- ❌ Bootloader mode again

## Current Recommendation

**Start with Phase 1** (stop commands only):
1. Safest possible test
2. Verifies communication works
3. Minimal risk to device
4. If this fails, we know to stop

**Do NOT skip phases!** Each phase builds on the previous and increases risk.

## Notes

- Take your time between phases
- Monitor device carefully
- Have recovery plan ready
- Test in Windows between phases if concerned
- Document everything that happens

## Questions to Answer

As we test, we're trying to understand:
1. ✅ Do effect commands (0x01, 0x41) work safely?
2. ❓ What triggered bootloader mode?
3. ❓ Were init commands (0x42, 0x0a) the culprit?
4. ❓ Is there a specific sequence required?
5. ❓ Are there timing requirements?

## Final Checklist Before Phase 1

- [ ] Wheel tested and working in Windows
- [ ] Recovery procedure understood
- [ ] Monitoring dmesg in separate terminal
- [ ] Ready to unload driver quickly if needed
- [ ] Wheel is stable and powered on
- [ ] Only `T500RS_ALLOW_STOP_ONLY` enabled
- [ ] Driver rebuilt with new settings
- [ ] Mentally prepared for potential issues

**If all checked, proceed with Phase 1.**

**If any concerns, WAIT and discuss first.**

---

## Remember

**Device safety > Force feedback functionality**

We can always try again later. We can't un-brick a device.

Good luck! 🍀

