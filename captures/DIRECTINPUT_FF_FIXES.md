# DirectInput Force Feedback Fixes for T500RS

## Issues Fixed

### 1. Missing DIEFFECT Structure Fields
**Problem**: Many `DIEFFECT` structure fields were not being set, causing `E_INVALIDARG` (0x80070057) errors.

**Solution**: Added required fields:
```cpp
eff.dwSamplePeriod = 0;  // Use device default
eff.dwTriggerRepeatInterval = 0;  // No repeating
```

### 2. Direction Units Wrong
**Problem**: Direction values were being passed directly instead of in hundredths of degrees.

**Solution**: Changed from:
```cpp
LONG dirs = direction;  // WRONG
```
To:
```cpp
LONG dirs = direction * 100;  // Correct - hundredths of degrees
```

### 3. Missing Explicit Acquire
**Problem**: Device wasn't being explicitly acquired, which can prevent effects from working.

**Solution**: Added after cooperative level setup:
```cpp
hr = joystick->Acquire();
```

### 4. Cooperative Level Issues
**Problem**: Using `NULL` window handle can cause issues with exclusive mode.

**Solution**: Get actual console window handle:
```cpp
HWND hwnd = GetConsoleWindow();
if (!hwnd) {
    hwnd = GetForegroundWindow();
}
hr = joystick->SetCooperativeLevel(hwnd, DISCL_EXCLUSIVE | DISCL_FOREGROUND);
```

With fallback to NONEXCLUSIVE if needed.

## Testing the Fixed Version

### Rebuild the Program
```bash
cd captures
cl /EHsc t500rs_test.cpp dinput8.lib dxguid.lib user32.lib
```

### Expected Behavior

#### 1. Initialization Phase
```
Searching for T500RS...
Found: T500 RS Racing wheel
T500RS found!
Force Feedback: SUPPORTED
Device acquired successfully
Ready to test!
```

If you see "WARNING: SetCooperativeLevel with EXCLUSIVE failed", that's OK - it will fall back to NONEXCLUSIVE mode.

#### 2. Effect Creation
All effects should now create successfully without errors. You should NOT see:
```
CreateEffect FAILED: 0x80070057  // FIXED!
```

#### 3. Physical Force Feedback
When running tests, you should feel:

**Constant Force**:
- Strong pull to the right (positive magnitude)
- Strong pull to the left (negative magnitude)
- Strength proportional to magnitude value (2000 < 5000 < 8000)

**Periodic Effects** (Sine, Square, Triangle):
- Oscillating forces at specified frequency
- Higher frequency = faster oscillations
- Should feel smooth (sine) vs sharp (square)

**Condition Effects**:
- **Spring**: Resistance when turning away from center
- **Damper**: Resistance proportional to speed of movement
- **Friction**: Constant drag regardless of position or speed
- **Inertia**: Resistance to changes in velocity

### If Effects Still Don't Work

#### Check Device State
```cpp
// Add this diagnostic code in main():
DIDEVICEOBJECTINSTANCE doi;
doi.dwSize = sizeof(doi);
hr = joystick->GetObjectInfo(&doi, DIJOFS_X, DIPH_BYOFFSET);
if (SUCCEEDED(hr)) {
    printf("Axis found: %s\n", doi.tszName);
}
```

#### Verify Device Supports Effects
```cpp
DIEFFECTINFO ei;
ei.dwSize = sizeof(ei);
hr = joystick->GetEffectInfo(&ei, GUID_ConstantForce);
if (SUCCEEDED(hr)) {
    printf("Constant Force supported: %s\n", ei.tszName);
} else {
    printf("Constant Force NOT supported!\n");
}
```

#### Check Autocenter
The T500RS might have autocenter enabled which can interfere with effects. Disable it:
```cpp
DIPROPDWORD dipdw;
dipdw.diph.dwSize = sizeof(DIPROPDWORD);
dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
dipdw.diph.dwObj = 0;
dipdw.diph.dwHow = DIPH_DEVICE;
dipdw.dwData = FALSE;  // Disable autocenter
hr = joystick->SetProperty(DIPROP_AUTOCENTER, &dipdw.diph);
```

## Common DirectInput Error Codes

| Code | Name | Meaning | Fix |
|------|------|---------|-----|
| 0x80070057 | E_INVALIDARG | Invalid parameter | Check all DIEFFECT fields are set |
| 0x8007001E | ERROR_DEVICE_NOT_READY | Device not acquired | Call Acquire() |
| 0x80070005 | E_ACCESSDENIED | Another app has exclusive access | Close other apps or use NONEXCLUSIVE |
| 0x80040154 | REGDB_E_CLASSNOTREG | DirectInput not installed | Install DirectX End-User Runtime |

## Debugging USB Capture Correlation

When capturing with USBPcap/Wireshark, look for:

1. **Effect Upload** - Multi-packet sequence with Report ID 0x0a:
   - Packet 1: Effect header (type, index, flags)
   - Packet 2-N: Effect parameters (magnitude, frequency, envelope, etc.)
   
2. **Effect Start** - Single packet:
   - Report ID 0x0a
   - Command type: Start effect
   - Effect index being started

3. **Effect Stop** - Single packet:
   - Report ID 0x0a
   - Command type: Stop effect
   - Effect index being stopped

### Example Constant Force USB Sequence
```
OUT: 0a 01 00 05 00 00 00 ...  // Upload constant force to slot 5
OUT: 0a 04 05 88 13 00 00 ...  // Set magnitude 5000 (0x1388)
OUT: 0a 05 05 00 00 00 00 ...  // Start effect slot 5
... (effect plays for duration)
OUT: 0a 06 05 00 00 00 00 ...  // Stop effect slot 5
```

## Advanced Testing

### Test Individual Parameters

#### 1. Magnitude Scaling
```cpp
for (int mag = 1000; mag <= 10000; mag += 1000) {
    CreateConstantForce(mag, 0, 1000);
    // Capture each and compare USB values
}
```

#### 2. Frequency Range
```cpp
for (int freq = 1; freq <= 100; freq += 10) {
    CreatePeriodicEffect(GUID_Sine, freq, 5000, 2000);
    // Capture to see frequency encoding
}
```

#### 3. Envelope Effects
Modify effect structures to add envelopes:
```cpp
DIENVELOPE envelope;
envelope.dwSize = sizeof(DIENVELOPE);
envelope.dwAttackLevel = 0;
envelope.dwAttackTime = 500000;  // 0.5s
envelope.dwFadeLevel = 0;
envelope.dwFadeTime = 500000;    // 0.5s

eff.lpEnvelope = &envelope;
```

## Next Steps After Verification

Once all effects work correctly:

1. **Capture All Tests** - Run automated suite with USBPcap
2. **Decode Captures** - Use `decode_ff_commands.py` on captured data
3. **Document Protocol** - Map DirectInput parameters to USB byte values
4. **Implement in Driver** - Replicate protocol in Linux kernel driver

## Reference: Working fedit.exe Comparison

If effects still don't work, test with fedit.exe side-by-side:
1. Open fedit.exe
2. Select T500RS device
3. Create identical effect (e.g., constant force magnitude 5000)
4. Capture USB traffic from both fedit.exe and your program
5. Compare packet sequences to find discrepancies

The USB packets should be nearly identical for the same effect parameters.
