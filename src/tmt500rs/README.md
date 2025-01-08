# Thrustmaster T500RS Module

## Overview
This module provides support for the Thrustmaster T500RS wheel as part of the TMFF2 driver framework.

## Features

### Force Feedback Support
- Constant force effects
- Periodic effects (sine, square, triangle, saw up/down)
- Condition effects:
  - Spring
  - Damper
  - Friction
  - Inertia
- Global settings:
  - Overall gain control
  - Autocenter strength
- Up to 16 simultaneous effects

### Input Support
- 16-bit wheel axis precision
- Pedal input
- Button mapping
- LED indicators

## Technical Details

### USB Protocol
The T500RS uses a simple 4-byte protocol for force feedback:
```
Byte 0: Report ID (0x03)
Byte 1: Command Type (0x0e)
Byte 2: Effect ID (0-15)
Byte 3: Parameter (0-127)
```

For detailed protocol information, see:
- [Force Feedback Implementation](../../docs/cline_docs/force_feedback.md)
- [USB Protocol Analysis](../../docs/cline_docs/usb_protocol.md)

### Implementation Notes
- All force values are scaled to 7-bit range (0-127)
- Effects are managed through simple command packets
- No complex command sequences required
- Immediate effect response with no timing requirements

## Integration
This module integrates with the main TMFF2 driver framework through the standard force feedback API. See the main [README.md](../../README.md) for general driver installation and usage instructions. 