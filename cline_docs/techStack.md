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

## Architecture Components

### Driver Core (src/hid-tmff2.c)
- Main driver implementation
- HID protocol handling
- Force feedback core logic
- Device management

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