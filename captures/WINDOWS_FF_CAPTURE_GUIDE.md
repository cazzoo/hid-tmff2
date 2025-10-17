# Windows Force Feedback Capture Guide for T500RS
**Purpose:** Systematically capture all force feedback commands from Windows drivers  
**Tools Required:** USBPcap, fedit.exe (or alternatives), Windows VM with T500RS

---

## Prerequisites

### Software Setup

1. **USBPcap** (for packet capture)
   ```
   Download: https://desowin.org/usbpcap/
   Install to: C:\Program Files\USBPcap\
   ```

2. **Wireshark** (for viewing captures)
   ```
   Download: https://www.wireshark.org/
   Ensure USBPcap is installed first
   ```

3. **Force Feedback Testing Tools** (choose one or more):

   **Option A: fedit.exe** (Immersion Force Editor - RECOMMENDED)
   - Professional FF testing tool
   - Allows precise effect parameter control
   - Direct HID communication
   - Download: Often bundled with DirectX SDK or available from Immersion Corp

   **Option B: Windows Control Panel**
   - Built-in: Control Panel → Devices and Printers → T500RS → Properties → Test tab
   - Limited but always available
   - Good for basic constant force, sine, square tests

   **Option C: ffbwrap** (Open Source)
   ```
   Download: https://github.com/berarma/ffbwrap
   Build with MinGW or use pre-compiled binaries
   Command-line tool for testing FF effects
   ```

   **Option D: SDL2 Joystick Test**
   ```
   Download: https://www.libsdl.org/
   Use testjoystick.exe from SDL2 distribution
   Good for periodic effects
   ```

   **Option E: Custom Test Program**
   ```cpp
   // Simple C++ program using DirectInput
   #include <dinput.h>
   #pragma comment(lib, "dinput8.lib")
   #pragma comment(lib, "dxguid.lib")
   
   // See full example below
   ```

### Hardware Setup

1. Connect T500RS to Windows VM/PC
2. Install Thrustmaster drivers
3. Verify device appears in Device Manager as "TRS Racing wheel"
4. Test basic functionality (wheel turns, pedals work)

---

## Method 1: Using fedit.exe (RECOMMENDED)

### Step 1: Setup USBPcap

1. Open Wireshark
2. Select your T500RS USB interface (usually "USBPcap1" or similar)
3. Apply display filter: `usb.idVendor == 0x044f && usb.idProduct == 0xb65e`
4. Click "Start capturing packets"

### Step 2: Test Effects with fedit.exe

**Launch fedit.exe:**
```cmd
cd C:\Path\To\fedit
fedit.exe
```

**In fedit.exe interface:**

1. **Select Device:**
   - File → Open Device
   - Select "ThrustMaster T500RS"

2. **Test Each Effect Type:**

#### A. Constant Force

**Test Sequence:**
1. Create New Effect → Constant Force
2. Set Direction: 0° (right), Magnitude: 5000
3. Click "Start"
4. Save capture as `constant_force_right_5000.pcapng`
5. Click "Stop"

6. Change Direction: 180° (left), Magnitude: 5000
7. Click "Start"
8. Save capture as `constant_force_left_5000.pcapng`
9. Click "Stop"

10. Test various magnitudes:
    - 2000, 5000, 8000, 10000
    - Save each as separate capture

**Envelope Test:**
1. Edit Effect → Attack Time: 1000ms, Attack Level: 8000
2. Fade Time: 1000ms, Fade Level: 2000
3. Click "Start"
4. Save as `constant_force_envelope.pcapng`

#### B. Periodic Effects

**Sine Wave:**
1. Create New Effect → Sine Wave
2. Set Frequency: 10 Hz, Magnitude: 5000
3. Period: 100ms
4. Click "Start"
5. Save as `sine_10hz_5000.pcapng`

Repeat for:
- 5 Hz, 10 Hz, 20 Hz
- Magnitudes: 2000, 5000, 8000

**Square Wave:**
1. Create New Effect → Square Wave
2. Test same parameters as Sine
3. Save as `square_10hz_5000.pcapng`

**Triangle Wave:**
1. Create New Effect → Triangle Wave  
2. Test same parameters
3. Save as `triangle_10hz_5000.pcapng`

**Sawtooth Up/Down:**
1. Create New Effect → Sawtooth Up
2. Test same parameters
3. Save as `sawtooth_up_10hz_5000.pcapng`
4. Repeat for Sawtooth Down

#### C. Ramp Effect

1. Create New Effect → Ramp
2. Start Level: 0, End Level: 10000
3. Duration: 2000ms
4. Click "Start"
5. Save as `ramp_0_to_10000_2s.pcapng`

Test variations:
- Ramp up: 0 → 10000
- Ramp down: 10000 → 0
- Ramp both: -5000 → 5000

#### D. Condition Effects

**Spring:**
1. Create New Effect → Spring
2. Positive Coefficient: 100, Negative Coefficient: 100
3. Positive Saturation: 10000, Negative Saturation: 10000
4. Center Point: 0, Deadband: 1000
5. Click "Start"
6. Save as `spring_standard.pcapng`

Test variations:
- Weak spring: coefficients 50/50
- Strong spring: coefficients 150/150
- Off-center: Center Point: 2000

**Damper:**
1. Create New Effect → Damper
2. Same parameters as Spring
3. Save as `damper_standard.pcapng`

**Friction:**
1. Create New Effect → Friction
2. Same parameters
3. Save as `friction_standard.pcapng`

**Inertia:**
1. Create New Effect → Inertia
2. Same parameters
3. Save as `inertia_standard.pcapng`

### Step 3: Stop Capture and Save

1. Stop Wireshark capture (red square button)
2. File → Save As → Choose location
3. Use descriptive naming: `<effect>_<param1>_<param2>.pcapng`

---

## Method 2: Using Windows Control Panel (SIMPLE)

### Setup

1. Start USBPcap in Wireshark (same as above)
2. Open Control Panel → Devices and Printers
3. Right-click T500RS → Game controller settings → Properties
4. Go to "Test" tab

### Test Procedure

1. Click "Test Force Feedback"
2. Use sliders to adjust:
   - **Direction:** 0-360°
   - **Magnitude:** 0-10000
3. Click "Start" then "Stop" for each test
4. Save capture after each effect

**Limitations:**
- Only tests constant force
- Limited parameter control
- No envelope support
- Good for quick validation

---

## Method 3: Using ffbwrap (COMMAND LINE)

### Installation

```cmd
cd C:\Tools
git clone https://github.com/berarma/ffbwrap
cd ffbwrap
mkdir build && cd build
cmake ..
make
```

### Testing Commands

**Constant Force:**
```cmd
ffbtest.exe --device 0 --constant --magnitude 5000 --direction 90 --duration 2000
```

**Periodic Effects:**
```cmd
ffbtest.exe --device 0 --sine --frequency 10 --magnitude 5000 --duration 2000
ffbtest.exe --device 0 --square --frequency 10 --magnitude 5000 --duration 2000
```

**Spring Effect:**
```cmd
ffbtest.exe --device 0 --spring --coefficient 100 --saturation 10000 --duration 2000
```

**Capture while running:**
1. Start Wireshark capture
2. Run ffbtest command
3. Wait for effect to complete
4. Stop capture and save

---

## Capture Organization

### Recommended File Structure

```
T500RS_Captures/
├── constant/
│   ├── constant_right_2000.pcapng
│   ├── constant_right_5000.pcapng
│   ├── constant_left_5000.pcapng
│   └── constant_envelope.pcapng
├── periodic/
│   ├── sine_5hz_5000.pcapng
│   ├── sine_10hz_5000.pcapng
│   ├── sine_20hz_5000.pcapng
│   ├── square_10hz_5000.pcapng
│   ├── triangle_10hz_5000.pcapng
│   ├── sawtooth_up_10hz_5000.pcapng
│   └── sawtooth_down_10hz_5000.pcapng
├── ramp/
│   ├── ramp_up_2s.pcapng
│   └── ramp_down_2s.pcapng
└── condition/
    ├── spring_weak.pcapng
    ├── spring_strong.pcapng
    ├── damper.pcapng
    ├── friction.pcapng
    └── inertia.pcapng
```

### Transfer to Linux

```bash
# On Windows VM, copy to shared folder
copy T500RS_Captures Z:\\VM_Shared\\captures\\

# On Linux host
cd /home/caz/VM_Shared/captures
ls -lh  # Verify files transferred
```

---

## Decoding Captures on Linux

### Extract FF Commands

```bash
cd /home/caz/Documents/hid-tmff2/captures

# Extract all packets for an effect
tshark -r /home/caz/VM_Shared/captures/constant_right_5000.pcapng \
  -Y "usb.endpoint_address == 0x01 and usb.src == \"host\"" \
  -x > constant_right_5000_hex.txt

# Decode with our Python script
python3 decode_ff_commands.py constant_right_5000_hex.txt > constant_right_5000_decoded.txt
```

### Batch Processing

```bash
# Process all captures
for f in /home/caz/VM_Shared/captures/*.pcapng; do
  base=$(basename "$f" .pcapng)
  echo "Processing $base..."
  
  tshark -r "$f" \
    -Y "usb.endpoint_address == 0x01 and usb.src == \"host\"" \
    -x > "${base}_hex.txt"
  
  python3 decode_ff_commands.py "${base}_hex.txt" > "${base}_decoded.txt"
done
```

---

## Expected Results

For each effect type, you should see a sequence of 3-4 packets:

**Example: Constant Force**
```
Packet 1: [Upload Envelope] Attack: 0ms, Fade: 0ms
Packet 2: [Constant Force Param] Level: 80 (RIGHT, magnitude 80)
Packet 3: [Duration/Control] Effect Type: Constant Force, Duration: 2000ms
Packet 4: [Start/Stop Control] START Effect
```

**Example: Sine Wave**
```
Packet 1: [Upload Envelope] Attack: 0ms, Fade: 0ms
Packet 2: [Periodic/Ramp Param] Periodic: Frequency 10.0Hz
Packet 3: [Duration/Control] Effect Type: Sine Wave, Duration: 3000ms
Packet 4: [Start/Stop Control] START Effect
```

---

## Troubleshooting

### USBPcap not capturing

- Restart Windows
- Reinstall USBPcap
- Try different USB port
- Check Device Manager for T500RS

### fedit.exe not working

- Run as Administrator
- Install DirectX Runtime
- Check T500RS drivers installed
- Try alternative tools (Windows Control Panel)

### No force feedback felt

- Check wheel is in PS3 mode (not PS4)
- Verify power supply connected
- Test with Windows Game Controller settings
- Check USB cable connection

---

## Summary

**Quick Start (5 minutes):**
1. Connect T500RS
2. Start USBPcap in Wireshark
3. Open Windows Control Panel → Test Force Feedback
4. Test constant force left/right
5. Save capture
6. Transfer to Linux and decode

**Complete Testing (30 minutes):**
1. Use fedit.exe or custom program
2. Test all effect types systematically
3. Vary parameters (magnitude, frequency, etc.)
4. Save each as separate capture
5. Decode all on Linux
6. Build complete command reference

**Result:** Complete understanding of Windows force feedback protocol for T500RS!
