# Force Feedback Implementation

## Overview
This document describes the force feedback implementation for Thrustmaster wheels, with specific details for each supported model.

## Supported Effects
Common effects supported across wheels:
- Constant Force
- Spring
- Damper
- Friction
- Inertia
- Periodic (Sine, Triangle, Square, etc.)
- Autocenter
- Gain

## T500RS Implementation

### Effect Types
The T500RS supports the following effects:
```c
FF_CONSTANT    // Constant force
FF_SPRING      // Spring effect
FF_DAMPER      // Damper effect
FF_FRICTION    // Friction effect
FF_INERTIA     // Inertia effect
FF_PERIODIC    // Periodic effects
FF_SINE        // Sine wave
FF_TRIANGLE    // Triangle wave
FF_SQUARE      // Square wave
FF_SAW_UP      // Sawtooth up
FF_SAW_DOWN    // Sawtooth down
FF_AUTOCENTER  // Auto-centering
FF_GAIN        // Overall gain
```

### Command Protocol
Each force feedback command follows this structure:
```c
struct t500rs_command {
    u8 report_id;     // Always 0x03
    u8 command_type;  // Always 0x0e for FF
    u8 effect_id;     // 0-15 or special
    u8 parameter;     // Effect-specific
} __packed;
```

### Effect Commands
- Upload Effect: Command 0x0e with effect-specific parameters
- Play Effect: Command 0x0e with effect ID | 0x10
- Stop Effect: Command 0x0e with effect ID | 0x10, param 0x00
- Set Gain: Command 0x0e with 0xf0
- Set Autocenter: Command 0x0e with 0xf1
- Set Range: Command 0x0e with 0xf2

### Value Scaling
All effect parameters are scaled to fit in the 0-127 range:
```c
scaled_value = original_value >> 8;  // Scale from 16-bit to 7-bit
```

### Device States
The T500RS has several operational states:
- DISCONNECTED
- INITIALIZING
- SWITCHING_MODE
- READY
- ERROR

### Error Handling
- Parameter validation
- Effect ID range checking (0-15)
- Command transmission error reporting
- State validation for commands

### Protocol Notes
- Fixed 4-byte commands
- Maximum 16 simultaneous effects
- Parameter values capped at 127
- No duration control (handled by start/stop)

## Integration with TMFF2
The T500RS driver integrates with the base TMFF2 framework by:
1. Implementing the required callbacks (upload_effect, play_effect, etc.)
2. Using the common effect state tracking
3. Following the standard initialization sequence
4. Sharing the common parameter handling (gain, range, etc.) 