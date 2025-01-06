# Technical Stack and Architecture

## Kernel Development
- Linux Kernel Module Development
- HID Subsystem Integration
- Force Feedback Subsystem
- USB Protocol Implementation

## Programming Languages and Tools
- C (Kernel Programming)
- Kernel Build System (Kbuild)
- DKMS for module distribution
- GCC Compiler toolchain

## Development Tools
- Make build system
- USB Protocol Analysis Tools (Wireshark/usbmon)
- Git for version control
- DKMS for module management
- tshark and wireshark to decode USB captures (pcapng)

## Architecture Components

### Driver Core (src/hid-tmff2.c)
- Main driver implementation
- HID protocol handling
- Force feedback core logic
- Device management

### Force Feedback Protocol
- Command Structure:
  - Start Effect: 41 00 41 01
  - Stop Effect: 41 00 00 01
  - Effect Upload Commands:
    - Constant Force: 02 1c (upload) / 03 0e (modify)
    - Spring: 05 0e (parameters) / 05 1c (envelope)
    - Damper: 05 0e (parameters)
    - Friction: 05 0e (parameters)
    - Periodic Effects: 04 0e (parameters)
  - Envelope Support:
    - Attack/Fade Length
    - Attack/Fade Level
    - Duration Control

- Effect Types:
  - Constant Force (8-bit resolution)
  - Spring Effect (with coefficients)
  - Damper Effect (with coefficients)
  - Friction Effect (with coefficients)
  - Inertia Effect
  - Periodic Effects:
    - Square Wave
    - Sine Wave
    - Triangle Wave
    - Sawtooth Up/Down

### Device-Specific Implementations
- TMT500RS implementation (src/tmt500rs/)
- TMT300RS implementation (src/tmt300rs/)
- TMT248 implementation (src/tmt248/)
- TMTSXW implementation (src/tmtsxw/)
- TMTX implementation (src/tmtx/)

### Build System
- Kbuild integration
- DKMS support for distribution
- Modular compilation support

### Testing Infrastructure
- USB capture analysis
- Force feedback effect testing
- Device initialization verification

## Dependencies
- Linux Kernel Headers
- HID Subsystem
- Force Feedback Subsystem
- USB Core
