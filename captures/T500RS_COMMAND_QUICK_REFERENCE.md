# T500RS Force Feedback Command Quick Reference

## Command Type Codes

| Code | Name | Purpose |
|------|------|---------|
| 0x01 | Duration/Control | Final packet with effect type and duration |
| 0x02 | Upload Envelope | Attack/fade parameters |
| 0x03 | Constant Force | Level parameter |
| 0x04 | Periodic/Ramp | Frequency or ramp parameters |
| 0x05 | Condition | Spring/damper/friction coefficients |
| 0x41 | Start/Stop | Effect playback control |
| 0x42 | Initialize | Device reset/init |

## Effect Type Codes (in Command 0x01, byte 2)

| Code | Effect Type |
|------|-------------|
| 0x00 | Constant Force |
| 0x20 | Square Wave |
| 0x21 | Triangle Wave |
| 0x22 | Sine Wave |
| 0x23 | Sawtooth Up |
| 0x24 | Sawtooth Down (Ramp) |
| 0x40 | Spring |
| 0x41 | Damper/Friction/Inertia |

## Upload Sequences

### Constant Force
```
1. 02 1c 00 00 00 00 00 00 00           [Envelope: no attack/fade]
2. 03 0e 00 [level]                     [Level: 0x00-0x7f right, 0x80-0xff left]
3. 01 00 00 40 [dur_lo] [dur_hi] ...    [Duration in ms, effect type 0x00]
4. 41 00 41 01                          [START]
```

### Sine Wave
```
1. 02 1c 00 00 00 00 00 00 00           [Envelope]
2. 04 0e 00 00 00 00 [freq_lo] [freq_hi] [Frequency in Hz*100]
3. 01 00 22 40 [dur_lo] [dur_hi] ...    [Duration, type 0x22 = sine]
4. 41 00 41 01                          [START]
```

### Spring
```
1. 05 0e 00 [+coef] [-coef] 00 00 00 00 [+sat] [-sat]
2. 05 1c 00 [ctr_lo] [ctr_hi] 00 00 00 00 [deadband] ...
3. 01 00 40 40 [dur_lo] [dur_hi] ...    [Duration, type 0x40 = spring]
4. 41 00 41 01                          [START]
```

### Stop Effect
```
41 00 00 01                              [STOP]
```

## Parameter Encoding

### Force/Level (8-bit signed)
- **0x00** = center/zero
- **0x01-0x7f** = positive/right (1-127)
- **0x80-0xff** = negative/left (-128 to -1)

### Frequency (16-bit LE)
- **Value = Hz × 100**
- 10 Hz = 0x03e8 (1000)
- 20 Hz = 0x07d0 (2000)
- Stored in: Command 0x04, bytes 6-7

### Duration (16-bit LE)
- **Value in milliseconds**
- 2000 ms = 0x07d0
- 9495 ms = 0x2517
- Stored in: Command 0x01, bytes 4-5

### Envelope

**Command 0x02 structure:**
```
Byte 0:   0x02 (command)
Byte 1:   0x1c (subcommand)
Byte 2-4: Attack length (24-bit LE, milliseconds)
Byte 5:   Attack level (0-127)
Byte 6-8: Fade length (24-bit LE, milliseconds)
Byte 9:   Fade level (0-127, optional)
```

### Condition Coefficients

**Command 0x05 (first packet):**
```
Byte 3: Positive coefficient (0-255)
Byte 4: Negative coefficient (0-255)
Byte 9: Positive saturation (0-255)
Byte 10: Negative saturation (0-255)
```

**Command 0x05 (second packet, type 0x1c):**
```
Byte 3-4: Center point (16-bit signed LE)
Byte 8: Deadband (0-255)
```

## Common Values

### Typical Force Levels
- Light: 0x20 (32)
- Medium: 0x40 (64)
- Strong: 0x60 (96)
- Maximum: 0x7f (127)

### Typical Frequencies
- Low: 5 Hz (0x01f4 = 500)
- Medium: 10 Hz (0x03e8 = 1000)
- High: 20 Hz (0x07d0 = 2000)

### Typical Durations
- Short: 500 ms (0x01f4)
- Medium: 2000 ms (0x07d0)
- Long: 5000 ms (0x1388)
- Very long: 9495 ms (0x2517)

### Typical Spring/Damper
- Weak: coefficient 50, saturation 5000
- Medium: coefficient 100, saturation 10000
- Strong: coefficient 150, saturation 15000

## Packet Format

### USB Packet Structure
```
[27 bytes USBPcap header] [HID data]
                          ^
                          Strip this when decoding
```

### HID Data Format
```
Byte 0: Command type (0x01-0x05, 0x41, 0x42)
Byte 1: Subcommand/flags
Byte 2-N: Parameters (vary by command)
```

## Testing Quick Start

### Linux - Extract & Decode
```bash
# Extract from capture
tshark -r capture.pcap -Y "usb.endpoint_address == 0x01" -x > hex.txt

# Decode
python3 decode_ff_commands.py hex.txt
```

### Windows - Capture
```cmd
1. Open Wireshark
2. Select USBPcap interface
3. Filter: usb.idVendor == 0x044f && usb.idProduct == 0xb65e
4. Start capture
5. Test effect in Control Panel or fedit.exe
6. Stop and save
```

## Example: Creating Sine Wave 10Hz

```c
// Packet 1: Envelope (no attack/fade)
uint8_t pkt1[] = {0x02, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Packet 2: Periodic params (10 Hz = 1000 = 0x03e8)
uint8_t pkt2[] = {0x04, 0x0e, 0x00, 0x00, 0x00, 0x00, 0xe8, 0x03};

// Packet 3: Duration (3000ms = 0x0bb8), type sine (0x22)
uint8_t pkt3[] = {0x01, 0x00, 0x22, 0x40, 0xb8, 0x0b, 0x00, 
                  0xff, 0xff, 0x0e, 0x00, 0x1c, 0x00, 0x00, 0x00};

// Packet 4: Start
uint8_t pkt4[] = {0x41, 0x00, 0x41, 0x01};

// Send sequence
usb_send(pkt1, sizeof(pkt1));
usleep(5000);
usb_send(pkt2, sizeof(pkt2));
usleep(5000);
usb_send(pkt3, sizeof(pkt3));
usleep(5000);
usb_send(pkt4, sizeof(pkt4));
```

## Troubleshooting

### Effect not felt
- Check packet sequence (envelope → params → duration → start)
- Verify timing delays between packets (5-10ms)
- Check force level not zero
- Ensure effect started (command 0x41 sent)

### Wrong direction
- Constant force: check sign of level byte
- For left/counterclockwise: use 0x80-0xff
- For right/clockwise: use 0x01-0x7f

### Wrong frequency
- Remember: stored value = Hz × 100
- 10 Hz needs 1000 (0x03e8), not 10 (0x000a)

### Effect stops immediately
- Check duration value
- Ensure not sending stop command
- Verify duration encoding (little-endian)

## See Also

- `PROTOCOL_ANALYSIS_AND_NEXT_STEPS.md` - Complete protocol details
- `WINDOWS_FF_CAPTURE_GUIDE.md` - How to capture from Windows
- `decode_ff_commands.py` - Python decoder tool
- `manual analysis/combined_effects_t500.txt` - Raw command examples
