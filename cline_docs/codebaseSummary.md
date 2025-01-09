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
      - Supports constant, spring, damper, friction effects
      - Implements periodic effects (sine, triangle, square, saw)
      - Handles autocenter and gain control
    - hid-tmt500rs-utils.h - Utility functions and command validation
  - src/tmt300rs/ - TMT300RS implementation
  - src/tmt248/ - TMT248 implementation
  - src/tmtsxw/ - TMTSXW implementation
  - src/tmtx/ - TMTX implementation

### Test Framework
- tests/tmt500rs/ - TMT500RS test implementation
  - test-tmt500rs.c - Main test suite
    - Mock USB device implementation
    - Device initialization tests
    - Force feedback effect tests
    - Error handling tests
  - Makefile - Test build configuration
  - run_tests.sh - Test execution script

### Build System
- Kbuild system for kernel module compilation
- DKMS support via dkms/ directory
- udev rules in udev/ directory

## Data Flow

### 1. Device Connection
- USB device detection
- HID protocol initialization
- Device mode configuration
- Device-specific setup

### 2. Force Feedback
- Effect requests from userspace
- Processing in core driver (hid-tmff2.c)
- Device-specific protocol translation
  - Effect type identification
  - Parameter validation
  - Command construction
- USB communication for effect execution
  - URB management
  - Command transmission
  - Status monitoring
- Error handling and retries

### 3. Testing Flow
- Mock device initialization
- USB endpoint simulation
- Command verification
- Effect parameter validation
- Resource management verification

## External Dependencies
- Linux Kernel HID Subsystem
- Force Feedback Subsystem
- USB Core
- DKMS (for distribution)

## Recent Significant Changes
- Implemented complete force feedback support for TMT500RS
- Created comprehensive test framework with mock USB device
- Added support for all effect types
- Implemented error handling and validation
- Added test suite with device initialization and effect testing

## Documentation Structure
- docs/ - Main project documentation
- source/ - USB captures and specifications
- cline_docs/ - Development tracking and planning
- tests/ - Test framework and test cases

## User Feedback Integration
- GitHub issues tracking
- Community testing and feedback
- Device-specific quirks and improvements
