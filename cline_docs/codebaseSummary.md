# Codebase Summary

## Key Components and Their Interactions

### Core Driver (src/hid-tmff2.c, src/hid-tmff2.h)
- Main driver implementation
- Handles core HID and force feedback functionality
- Manages device registration and initialization
- Provides common utilities for device-specific implementations

### Device-Specific Drivers
- Each device has its own implementation directory:
  - src/tmt500rs/ - TMT500RS implementation
  - src/tmt300rs/ - TMT300RS implementation
  - src/tmt248/ - TMT248 implementation
  - src/tmtsxw/ - TMTSXW implementation
  - src/tmtx/ - TMTX implementation

### Build System
- Kbuild system for kernel module compilation
- DKMS support via dkms/ directory
- udev rules in udev/ directory

## Data Flow
1. Device Connection
   - USB device detection
   - HID protocol initialization (41004101)
   - Device mode configuration (41000001)
   - Device-specific setup
   
2. Force Feedback
   - Effect requests from userspace
   - Processing in core driver (hid-tmff2.c)
   - Device-specific protocol translation
     - Command format: 030e00XX
     - 8-bit force value resolution
     - 16-32ms update intervals
   - USB communication for effect execution
   - Error handling and retries

## External Dependencies
- Linux Kernel HID Subsystem
- Force Feedback Subsystem
- USB Core
- DKMS (for distribution)

## Recent Significant Changes
- Initial project structure setup
- Basic driver framework implementation
- Device detection system

## Documentation Structure
- docs/ - Main project documentation
- source/ - USB captures and specifications
- cline_docs/ - Development tracking and planning

## User Feedback Integration
- GitHub issues tracking
- Community testing and feedback
- Device-specific quirks and improvements
