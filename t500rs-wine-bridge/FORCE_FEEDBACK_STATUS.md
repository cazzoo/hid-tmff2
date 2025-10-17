# Force Feedback Implementation Status

## What Was Added

Added HID PID (Physical Interface Device) descriptors to advertise force feedback capability to Wine/DirectInput applications.

### HID PID Reports Added

1. **Set Effect Report (ID 0x01)**
   - Effect Block Index (which effect slot)
   - Effect Type (constant, ramp, periodic, condition)
   - Duration, trigger settings, gain
   - Axes enable, direction
   - All standard PID effect parameters

2. **Effect Operation Report (ID 0x02)**
   - Effect Block Index
   - Operation (Start, Start Solo, Stop)
   - Loop Count

## Current Status

✅ **HID Descriptor**: Complete with FF capability advertised  
✅ **Proxy**: Forwards SET_REPORT commands from Wine to driver  
🔄 **Driver**: Receives FF reports but needs implementation  
❌ **Effect Translation**: Not yet implemented

## The Challenge

Wine/DirectInput uses **HID PID protocol** for force feedback:
```
Wine sends HID reports → UHID → Proxy → Driver
```

Our driver uses **Linux Force Feedback API** (uinput):
```
Application → uinput → Driver → USB → Device
```

These are fundamentally different protocols and need translation.

## What Happens Now

When you enable FF in LFS (or any Wine game):

1. ✅ Wine sees the device supports FF (HID descriptor)
2. ✅ Wine sends SET_REPORT commands (HID PID format)
3. ✅ UHID kernel forwards to proxy
4. ✅ Proxy forwards via socket to driver  
5. ✅ Driver receives the FF report
6. ⚠️  Driver logs it but doesn't act on it yet

## Next Steps

### Option 1: HID PID → Driver Internal (Recommended)

Parse Wine's HID PID reports and convert to driver's internal effect format:

```c
// In t500rs_bridge.c
case MSG_FF_REPORT:
    if (msg.report_id == 0x01) {  // Set Effect
        // Parse HID PID effect parameters
        uint8_t effect_id = msg.data[0];
        uint8_t effect_type = msg.data[1];
        // ... parse duration, gain, etc.
        
        // Convert to driver format and upload
        upload_effect_from_hid_pid(effect_id, effect_type, ...);
    }
    else if (msg.report_id == 0x02) {  // Effect Operation
        uint8_t effect_id = msg.data[0];
        uint8_t operation = msg.data[1];
        if (operation == 0x01) {  // Start
            start_effect_internal(effect_id);
        }
        else if (operation == 0x03) {  // Stop
            stop_effect_internal(effect_id);
        }
    }
    break;
```

This would call the existing effect functions (`upload_constant_effect`, `start_effect`, etc.) bypassing uinput.

###Option 2: Simpler Approach - Constant Force Only

For racing games, the most important effect is constant force (steering resistance/feedback):

```c
// Just handle Report ID 0x01 with type 0x26 (constant force)
if (msg.report_id == 0x01 && msg.data[1] == 0x26) {
    int16_t magnitude = (msg.data[X] | (msg.data[Y] << 8));
    // Send directly to device via USB
    send_constant_force_to_device(magnitude);
}
```

This would give basic centering spring and road feel without full effect support.

### Option 3: Fork uinput Events

Create a second uinput device that the bridge "plays" effects on, mimicking what Wine would do:

```c
// Bridge creates a fake app that uses the real uinput device
int fake_uinput_fd = open_real_uinput_device();

// When HID PID reports arrive:
translate_hid_pid_to_uinput_ioctl(msg.data, fake_uinput_fd);
```

This reuses all existing driver logic but adds overhead.

## Testing Current State

Run the updated bridge and driver, then launch LFS:

```bash
sudo ./test-wine-bridge.sh
# Watch the driver terminal for:
# [INFO] Bridge: Force feedback report received (id=X, Y bytes)
```

The logs will show what reports Wine is sending, which helps us implement the translator.

## Implementation Priority

For racing games specifically:

1. **Constant Force** (highest priority)
   - Steering resistance
   - Road feel/texture
   - Centering spring

2. **Spring/Damper** (medium priority)
   - Weight/inertia feel
   - Return-to-center

3. **Periodic Effects** (lower priority)
   - Engine vibration
   - Rumble strips
   - Not critical for wheel feel

Most racing games rely heavily on constant force for the core steering feedback, so implementing just that would give 80% of the FF experience.

## Resources

- USB HID PID Spec: https://www.usb.org/sites/default/files/pid1_01.pdf
- Linux FF API: https://www.kernel.org/doc/html/latest/input/ff.html
- Wine FF code: https://source.winehq.org/source/dlls/dinput/joystick_hid.c

## Recommendation

Start with **Option 1 + Constant Force only**:
1. Parse HID PID Set Effect reports (ID 0x01)
2. Extract magnitude for constant force
3. Call existing `upload_constant_effect()` 
4. Parse Effect Operation reports (ID 0x02)
5. Call existing `start_effect()` / `stop_effect()`

This gives working FF with minimal new code, reusing the battle-tested effect engine.

---

**Status**: 🔄 **In Progress** - HID layer complete, translation layer needed
