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
    - hid-tmt500rs.h - Core definitions and structures
    - hid-tmt500rs-init.c - Device initialization and cleanup
    - hid-tmt500rs-ff.c - Force feedback implementation
    - hid-tmt500rs-utils.h - Utility functions and command validation
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
   - HID protocol initialization
   - Device mode configuration
   - Device-specific setup
   
2. Force Feedback
   - Effect requests from userspace
   - Processing in core driver (hid-tmff2.c)
   - Device-specific protocol translation
   - USB communication for effect execution
   - Error handling and retries

## External Dependencies
- Linux Kernel HID Subsystem
- Force Feedback Subsystem
- USB Core
- DKMS (for distribution)

## Recent Significant Changes
- Implemented TMT500RS module structure
- Added command validation system
- Integrated force feedback handlers
- Fixed build issues and warnings
- Set up error handling and logging

## Documentation Structure
- docs/ - Main project documentation
- source/ - USB captures and specifications
  - device_init.pcapng - Device initialization sequence
  - t500rs_constant_force.pcapng - Force feedback commands
  - combined_effects_t500.txt - Combined effects analysis
  - INIT_SEQUENCES_TRACKING.md - Initialization tracking
  - constant_force_detailed.txt - Detailed force analysis
  - GET_DESCRIPTIOR_HEX_DUMPS - USB descriptor data
- cline_docs/ - Development tracking and planning

## User Feedback Integration
- GitHub issues tracking
- Community testing and feedback
- Device-specific quirks and improvements
