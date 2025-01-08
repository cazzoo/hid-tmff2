# T500RS USB Protocol Analysis

## Overview
Analysis of USB captures reveals a simple and consistent protocol for force feedback effects. All commands are sent as 4-byte interrupt OUT transfers on endpoint 1.

## Command Format
Every force feedback command follows this structure:
```
[Report ID] [Command Type] [Effect ID] [Parameter]
```

## Protocol Details

### Report Structure
- Report ID: 0x03 (Force Feedback)
- Command Type: 0x0e (Effect Control)
- Effect ID: 0-15 for effects, special values for global settings
- Parameter: Effect-specific value (0-127)

### Effect Types
Observed from USB captures:

1. Constant Force
   ```
   03 0e XX YY  (XX = Effect ID, YY = Force Level)
   ```

2. Periodic Effects
   ```
   03 0e XX 2Y  (Y = 0-4 for different waveforms)
   03 0e XX YY  (Second command: YY = Magnitude)
   ```

3. Condition Effects
   ```
   03 0e XX 0Y  (Y = 1-4 for Spring/Damper/Friction/Inertia)
   03 0e XX YY  (Second command: YY = Coefficient)
   ```

### Global Settings
```
03 0e 00 YY  (Autocenter, YY = Strength)
03 0e 01 YY  (Overall Gain, YY = Level)
```

### Effect Control
```
03 0e XX 41  (Start Effect)
03 0e XX 00  (Stop Effect)
```

## Timing Analysis
- No observed timing requirements between commands
- Effects respond immediately to start/stop
- No acknowledgment required

## Value Ranges
- All parameters use 7-bit values (0-127)
- Force values are centered at 64 (0x40)
- Direction values use full range

## Error Handling
- Device ignores invalid effect IDs
- No explicit error responses observed
- Malformed commands are ignored

## Implementation Notes
1. Keep commands simple and atomic
2. No need for complex sequences
3. Scale all input values to 7-bit range
4. Use consistent report ID and command type
5. Handle each effect type independently 