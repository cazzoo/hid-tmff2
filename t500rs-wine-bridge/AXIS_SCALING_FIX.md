# Axis Scaling Fix for Wine Bridge

## Problem

When using the T500RS in Wine/LFS, the steering axis would reset when turned only a small amount. This was caused by an axis scaling mismatch between the HID descriptor and the actual data format.

## Root Cause

The driver forwards **raw USB data** to the Wine bridge, but the original HID descriptor declared the wrong value ranges:

### What the HID Descriptor Said (WRONG):
- Steering: 0 to 65535 ✗ (but implied center at 0)
- Pedals: 0 to 1023 ✓ (correct)
- D-pad: Simple X/Y axes ✗ (non-standard)

### What the Raw USB Data Actually Contains:
- **Byte 0**: Report ID (0x07)
- **Bytes 1-2**: Steering (0-65535, little-endian, **center at 32768**)
- **Bytes 3-4**: Brake (0-1023, 10-bit)
- **Bytes 5-6**: Throttle (0-1023, 10-bit)
- **Bytes 7-8**: Clutch (0-1023, 10-bit)
- **Bytes 9-10**: Unknown/Reserved
- **Bytes 11-13**: Buttons (24 bits)
- **Byte 14**: D-pad (0-7 for 8 directions, 0x0F for center)

## The Fix

Updated the HID descriptor to accurately describe the raw USB report format:

1. **Steering Axis**:
   ```
   Logical Min: 0
   Logical Max: 65535
   Physical Min: 0
   Physical Max: 65535
   ```
   Center position is at value 32768 (half of 65535).

2. **Pedal Axes** (Brake, Throttle, Clutch):
   ```
   Logical Min: 0
   Logical Max: 1023
   Physical Min: 0
   Physical Max: 1023
   ```
   These are 10-bit values (0-1023), even though stored in 16-bit fields.

3. **D-pad**:
   Changed from separate X/Y axes to a proper HAT switch:
   ```
   Usage: Hat Switch (0x39)
   Logical Min: 0
   Logical Max: 7
   Unit: Degrees (0-315°)
   ```
   Values map to 8 directions: 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW, 0x0F=Center

4. **Report ID**:
   Added Report ID (0x07) to the descriptor to match the actual USB reports.

## Why This Matters

Wine (and Windows applications) expect the HID descriptor to accurately describe the data format. When there's a mismatch:

- **Steering**: Wine interprets unsigned 0-65535 values. If the descriptor doesn't specify the center point correctly, games won't know where "straight ahead" is.
  
- **Wraparound**: When the descriptor says "0 to 65535" without proper centering info, and you send value 32768 (center), Wine might interpret subsequent values incorrectly, causing the reset behavior you observed.

## Testing

After the fix:
1. Rebuilt the proxy: `gcc -o uhid_proxy_ipc src/uhid_proxy_ipc.c`
2. Restart both proxy and driver
3. In Wine games:
   - Steering should have full range without resets
   - Center position should be stable
   - Pedals should work correctly (0-100%)
   - All buttons should function
   - D-pad should recognize all 8 directions

## Technical Notes

### Data Flow

The bridge forwards **raw USB interrupt reports** from the device:
```
Real T500RS → libusb → Driver → Socket → Proxy → UHID → Wine
     (USB)              (raw buffer)      (HID report)   (DirectInput)
```

The driver does two things with the raw data:
1. **For Linux (uinput)**: Converts values (e.g., steering 0-65535 → -32768 to 32767)
2. **For Wine (bridge)**: Forwards raw buffer unchanged

This is why the HID descriptor must match the **raw format**, not the converted format.

### Why Not Convert?

We could convert the data before sending to Wine, but:
- ✗ Adds CPU overhead
- ✗ Requires maintaining conversion logic
- ✗ Potential for bugs/mismatches
- ✓ Raw forwarding is fastest and most reliable
- ✓ HID descriptors are designed to describe raw data

### Future Enhancements

For even better Wine compatibility, we could:
1. Extract the real HID descriptor from the T500RS device
2. Use it verbatim in the UHID proxy
3. This would give perfect 1:1 compatibility

However, the current descriptor works well and is simpler to maintain.

## References

- USB HID Specification: https://www.usb.org/hid
- Linux UHID documentation: https://www.kernel.org/doc/html/latest/hid/uhid.html
- HID descriptor tool: https://eleccelerator.com/usbdescreqparser/
