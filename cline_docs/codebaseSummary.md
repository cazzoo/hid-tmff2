# Codebase Summary

## Key Components and Their Interactions

### Core Driver (src/hid-tmff2.c, src/hid-tmff2.h)
- Main driver implementation
- Handles core HID and force feedback functionality
- Manages device registration and initialization
- Provides common utilities for device-specific implementations

### Device-Specific Drivers
- Each device has its own implementation directory:
  - src/tmt500rs/ - TMT500RS implementation (**NEW SIMPLIFIED APPROACH**)
    - hid-tmt500rs.h - Core definitions and structures
    - hid-tmt500rs-simple.c - **NEW**: Simplified main implementation
    - hid-tmt500rs-simple.h - **NEW**: Simplified header definitions
    - hid-tmt500rs.c - Modified populate_api function
    - hid-tmt500rs-init.c - Device initialization and cleanup (legacy)
    - hid-tmt500rs-ff.c - Force feedback implementation (legacy)
    - hid-tmt500rs-utils.h - Utility functions and command validation (legacy)
  - src/tmt300rs/ - TMT300RS implementation (working reference)
  - src/tmt248/ - TMT248 implementation (working reference)
  - src/tmtsxw/ - TMTSXW implementation (working reference)
  - src/tmtx/ - TMTX implementation (working reference)

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
  - **NEW**: t500rs_implementation_plan.md - Complete T500RS strategy
  - projectRoadmap.md - High-level goals and progress
  - currentTask.md - Current objectives and next steps
  - techStack.md - Architecture and technology decisions
  - force_feedback.md - Effect implementation details
  - usb_protocol.md - Communication protocol documentation
- .augment/rules/ - Development workflow and standards
  - **NEW**: guide.md - T500RS development guide with safety requirements
- tests/ - Test framework and test cases

## Recent Significant Changes
- **MAJOR**: Created comprehensive T500RS implementation plan with simplified approach
- **MAJOR**: Established 6-phase development roadmap prioritizing safety and stability
- **MAJOR**: Documented USB protocol analysis methodology for host/guest validation
- **MAJOR**: Created development guide integrating safety requirements with workflow standards
- Updated project documentation structure with proper cross-references

## User Feedback Integration
- GitHub issues tracking
- Community testing and feedback
- Device-specific quirks and improvements
- **NEW**: Safety-first development methodology based on system stability requirements
