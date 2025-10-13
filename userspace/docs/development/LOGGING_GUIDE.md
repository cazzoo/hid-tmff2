# T500RS GUI - Logging & Debugging Guide

## Enhanced Logging Output

The GUI now provides comprehensive logging to help debug force feedback effects. Here's what each log message means:

---

## 🎮 Effect Triggering

When you click an effect button, you'll see:

```
🎮 TRIGGERING EFFECT: 'flat_tire'
  Parameters: waveform=SQUARE, magnitude=25000, period=400ms, duration=3s
✅ Uploaded effect 'flat_tire' → ID=0, Type=0x51, Duration=3.0s
▶️  Playing effect ID 0
```

**Breakdown:**
- **🎮 TRIGGERING EFFECT** - Shows which effect button was clicked
- **Parameters** - Exact values being sent (magnitude, period, duration, etc.)
- **✅ Uploaded** - Effect successfully uploaded to kernel with assigned ID
- **Type=0x51** - Effect type constant (0x51 = FF_PERIODIC)
- **▶️ Playing** - Effect is now active

---

## ⏹️ Effect Completion

After the effect duration ends, you'll see:

```
⏹️  Stopped effect 'flat_tire' (ID 0)
🗑️  Removed effect ID 0 from kernel
```

**Breakdown:**
- **⏹️ Stopped** - Effect playback stopped
- **🗑️ Removed** - Effect deleted from kernel memory (freed the slot)

---

## 🛑 Stop All Effects

When you click "STOP ALL EFFECTS":

```
🛑 STOP ALL EFFECTS REQUESTED
📋 Active effects to stop: [0, 1, 2]
✅ Stopped 3 effects, removed 3 from kernel
🏁 Stop all complete
```

**Breakdown:**
- **📋 Active effects** - Lists all effect IDs currently tracked
- **✅ Stopped/removed** - Count of effects that were successfully cleared
- **🏁 Complete** - All cleanup finished

---

## ❌ Error Messages Explained

### 1. **"No effect slots available"**
```
❌ Effect 'crash_impact' failed: No effect slots available! Use 'Stop All Effects' first.
```

**Cause:** The kernel has a limited number of effect slots (usually 16). They weren't freed properly.

**Solution:** Click "STOP ALL EFFECTS" button to clear all slots, then try again.

---

### 2. **"Effect type not supported"**
```
❌ Effect 'spring_center' failed: Effect type not supported by device (errno 38)
```

**Cause:** The device/driver doesn't support that specific effect type (e.g., FF_SPRING, FF_DAMPER).

**Solution:** This is a hardware/driver limitation. Some effects may not work on all devices.

**Note:** The T500RS hardware may not support all Linux FF effect types. The kernel driver might only implement:
- FF_CONSTANT (0x52) ✅
- FF_PERIODIC (0x51) ✅
- FF_SPRING (0x53) ❓
- FF_DAMPER (0x55) ❓
- FF_FRICTION (0x54) ❓
- FF_INERTIA (0x56) ❓
- FF_RAMP (0x57) ❓

---

### 3. **"Invalid argument" (errno 22)**
```
⚠️  Error removing effect 'flat_tire' (ID 5): [Errno 22] Invalid argument
```

**Cause:** Tried to remove an effect that doesn't exist or was already removed.

**Solution:** This is usually harmless and automatically handled. The effect tracking will be updated.

**Note:** This error is now **silently ignored** when it occurs during automatic cleanup, so you won't see it spamming the console anymore.

---

### 4. **QTimer float error (FIXED)**
```
Error playing effect: arguments did not match any overloaded call:
  singleShot(msec: int, slot: PYQT_SLOT): argument 1 has unexpected type 'float'
```

**Cause:** QTimer.singleShot requires integer milliseconds, but was receiving float.

**Status:** ✅ **FIXED** - Duration is now converted to `int` before passing to QTimer.

---

## 📊 Understanding Effect Types

Each effect logs its type constant:

| Type Constant | Effect Name | Description |
|--------------|-------------|-------------|
| 0x52 | FF_CONSTANT | Steady directional force |
| 0x51 | FF_PERIODIC | Repeating waveform (sine, square, etc.) |
| 0x53 | FF_SPRING | Position-based resistance |
| 0x55 | FF_DAMPER | Velocity-based resistance |
| 0x54 | FF_FRICTION | Constant friction throughout range |
| 0x56 | FF_INERTIA | Acceleration-based resistance |
| 0x57 | FF_RAMP | Linearly changing force |

---

## 🔍 Debugging Tips

### 1. **Check Effect IDs**
Look for increasing effect IDs (0, 1, 2, 3...). If IDs keep increasing without wrapping back to 0, slots aren't being freed properly.

**Good pattern:**
```
Effect ID 0 uploaded → removed
Effect ID 1 uploaded → removed
Effect ID 2 uploaded → removed
Effect ID 0 uploaded (reused!) ✅
```

**Bad pattern:**
```
Effect ID 0 uploaded
Effect ID 1 uploaded
Effect ID 2 uploaded
...
Effect ID 15 uploaded
❌ No space left on device (slots exhausted!)
```

### 2. **Monitor Effect Lifecycle**
Each effect should follow this complete lifecycle:
1. 🎮 Triggering
2. ✅ Uploaded
3. ▶️ Playing
4. ⏹️ Stopped
5. 🗑️ Removed

If steps 4 or 5 are missing, effects aren't being cleaned up properly.

### 3. **Effect Type Support**
If you see "Function not implemented" errors, that effect type isn't supported. Focus on:
- **FF_CONSTANT** - Almost always supported
- **FF_PERIODIC** - Usually supported (sine, square waves)
- **Others** - May not be implemented by the driver

---

## 🐛 Common Issues & Solutions

### Issue: "No space left on device" after several effects

**Diagnosis:**
```
Effect ID 0 uploaded ✅
Effect ID 1 uploaded ✅
...
Effect ID 15 uploaded ✅
❌ No space left on device
```

**Cause:** Effects aren't being removed from kernel memory.

**Solutions:**
1. Click "STOP ALL EFFECTS" regularly
2. Check that `EVIOCRMFF` ioctl is being called (look for 🗑️ removed messages)
3. Ensure QTimer cleanup callbacks are firing

---

### Issue: Effects feel weak or not triggered

**Diagnosis:**
```
✅ Uploaded effect 'crash_impact' → ID=0, Type=0x52
▶️  Playing effect ID 0
(no force felt)
```

**Possible causes:**
1. Master Gain set too low → Adjust in "Force Adjustment" tab
2. Effect parameters too weak → Check magnitude values in logs
3. Driver not processing FF events → Check userspace driver logs

---

### Issue: Some effect types never work

**Diagnosis:**
```
❌ Effect 'spring_center' failed: Effect type not supported (errno 38)
❌ Effect 'damper_feel' failed: Effect type not supported (errno 38)
```

**Cause:** T500RS driver may only support FF_CONSTANT and FF_PERIODIC.

**Solution:** Use effects that rely on those types:
- ✅ Constant Left/Right
- ✅ Crash Impact, Curb Hit
- ✅ Flat Tire, Rumble Strips, Engine Vibration
- ✅ All road surface effects
- ❌ Spring, Damper, Friction, Inertia (may not be supported)

---

## 📝 Log Output Example (Complete Effect)

Here's what a successful effect looks like from start to finish:

```
🎮 TRIGGERING EFFECT: 'flat_tire'
  Parameters: waveform=SQUARE, magnitude=25000, period=400ms, duration=3s
✅ Uploaded effect 'flat_tire' → ID=2, Type=0x51, Duration=3.0s
▶️  Playing effect ID 2

[3 seconds pass - you feel the thumping effect]

⏹️  Stopped effect 'flat_tire' (ID 2)
🗑️  Removed effect ID 2 from kernel
```

**This is the ideal pattern!** All steps completed successfully.

---

## 🚀 Testing Strategy

1. **Start simple** - Test "Constant Left" first to verify basic FFB works
2. **Check logs** - Ensure upload → play → stop → remove cycle completes
3. **Test periodic** - Try "Gentle Vibration" to test FF_PERIODIC
4. **Test complex** - Try "Flat Tire" or "Crash Impact"
5. **Monitor slots** - Click "Stop All Effects" periodically to prevent slot exhaustion
6. **Check unsupported** - Identify which effect types work on your device

---

## 💡 Pro Tips

- **Effect IDs should reuse** - If you see IDs 0-15 then errors, slots aren't being freed
- **Watch for errno 38** - Indicates driver doesn't support that effect type
- **EINVAL (errno 22) on remove is normal** - Now silently handled during cleanup
- **Use Stop All frequently** - Prevents slot exhaustion
- **Check userspace driver logs** - Ensure driver is processing FF events correctly

---

## Summary of Fixes

✅ **Comprehensive logging** - Every effect action is logged with name and parameters
✅ **Error context** - Each error shows which effect caused it and why
✅ **Float duration fixed** - QTimer now receives integer milliseconds
✅ **EINVAL silently handled** - No more spam for already-removed effects
✅ **Effect slot tracking** - Better management of kernel effect memory
✅ **Clear error messages** - errno 28 and 38 now have helpful explanations

---

Run the GUI and watch the console for detailed debugging output:
```bash
sudo python3 ~/Documents/hid-tmff2/userspace/t500rs_control.py
```

Happy debugging! 🎮🔧
