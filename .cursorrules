# T500RS Development Guide

## Overview

This guide provides the definitive development workflow for implementing Thrustmaster T500RS support in the hid-tmff2 Linux kernel driver. It integrates safety requirements, development standards, and project documentation structure to ensure reliable and maintainable code.

## Primary Reference Documents

### Implementation Plan
- **Primary Reference**: [T500RS Implementation Plan](../../cline_docs/t500rs_implementation_plan.md)
- **Purpose**: Complete technical strategy, USB protocol analysis, and 6-phase development roadmap
- **Usage**: Consult before starting any T500RS development work

### Project Documentation Structure
Follow the established documentation hierarchy from [.cursorrules](../../.cursorrules):

1. **[projectRoadmap.md](../../cline_docs/projectRoadmap.md)** - High-level goals and progress tracking
2. **[currentTask.md](../../cline_docs/currentTask.md)** - Current objectives and next steps
3. **[techStack.md](../../cline_docs/techStack.md)** - Technology choices and architecture
4. **[codebaseSummary.md](../../cline_docs/codebaseSummary.md)** - Project structure overview

## Development Standards and Safety Requirements

### Critical Development Rules (from .cursorrules)

#### Module Architecture
- **Keep modules split across multiple files** - Follow existing patterns in `src/tmt300rs/`, `src/tmt248/`
- **Do NOT make tmt500rs standalone** - Integrate with existing tmff2 framework
- **Do NOT edit base tmff2 driver** - Use existing interfaces and callback patterns
- **Follow integration patterns** - Study how other modules integrate (exclude test modules)

#### Build and Testing Workflow
- **Always build modules after writing code** - Use `make` in project root
- **Use modprobe for module operations** - Never manually insert/remove modules
- **Test frequently** - Build and test after each significant change
- **Monitor system stability** - Ensure no hanging, crashing, or freezing

#### USB and System Safety
- **Never remove USB base modules** - Preserve hid & usbhid modules
- **Implement robust error handling** - Prevent system instability
- **Use troubleshooting documentation** - Reference `test_troubleshooting.md` for repeated issues

### Host System Protection Guidelines

#### USB Error Handling
```c
// Required error handling patterns
- Proper URB cleanup on failures
- Graceful USB disconnect handling  
- Command validation and bounds checking
- Retry mechanisms with limits
- Resource cleanup on all error paths
```

#### Memory Management
```c
// Safety requirements
- Prevent memory leaks in all code paths
- Safe pointer handling and validation
- Proper allocation/deallocation pairing
- Resource tracking and cleanup
```

#### Driver Lifecycle Safety
```c
// Module loading/unloading safety
- Clean initialization and cleanup
- Safe driver reload capability
- Proper device state management
- Error recovery mechanisms
```

## Development Workflow

### Phase-Based Development Process

Reference the **6-Phase Development Roadmap** from the [T500RS Implementation Plan](../../cline_docs/t500rs_implementation_plan.md):

#### Phase 1: Basic Device Detection (CRITICAL)
- **Priority**: Critical - System stability foundation
- **Focus**: Device recognition and driver binding
- **Safety**: Verify clean loading/unloading, USB disconnect handling

#### Phase 2: USB Communication (CRITICAL)  
- **Priority**: Critical - Communication foundation
- **Focus**: 4-byte command structure implementation
- **Safety**: USB error recovery, command validation

#### Phase 3: Basic Force Feedback (HIGH)
- **Priority**: High - Core functionality
- **Focus**: Constant force and spring effects
- **Safety**: Effect parameter validation, resource management

#### Phase 4: Complete Effect Support (MEDIUM)
- **Priority**: Medium - Feature completeness
- **Focus**: All effect types and control features
- **Safety**: Effect combination validation, performance monitoring

#### Phase 5: Robustness & Safety (CRITICAL)
- **Priority**: Critical - Production readiness
- **Focus**: Comprehensive error handling and stability
- **Safety**: System stability under all conditions

#### Phase 6: Testing & Validation (HIGH)
- **Priority**: High - Quality assurance
- **Focus**: Long-term stability and performance
- **Safety**: Extended validation and monitoring

### Daily Development Workflow

#### Before Starting Work
1. **Read documentation in order** (per .cursorrules):
   - projectRoadmap.md (high-level context)
   - currentTask.md (current objectives)  
   - techStack.md (technical context)
   - codebaseSummary.md (structure overview)

2. **Review current phase requirements** from implementation plan
3. **Check troubleshooting docs** if encountering repeated issues

#### During Development
1. **Follow modular file structure**:
   ```
   src/tmt500rs/
   ├── hid-tmt500rs-simple.c    # Main implementation
   ├── hid-tmt500rs-simple.h    # Simplified header  
   ├── hid-tmt500rs.c          # Modified populate_api
   └── hid-tmt500rs.h          # Existing definitions
   ```

2. **Build and test frequently**:
   ```bash
   # Build after each change
   make
   
   # Test module loading
   sudo modprobe -r hid_tmff_new  # Remove if loaded
   sudo modprobe hid_tmff_new     # Load new version
   
   # Check system logs
   dmesg | tail -100
   ```

3. **Validate safety requirements**:
   - No kernel crashes or hangs
   - Clean USB communication
   - Proper resource cleanup
   - System stability maintained

#### After Completing Tasks
1. **Update documentation**:
   - currentTask.md with progress and next steps
   - projectRoadmap.md with completed tasks
   - Add troubleshooting entries if issues encountered

2. **Commit successful changes** to repository
3. **Validate system stability** before proceeding

## Quick Reference

### USB Protocol (T500RS)
```c
// 4-byte command structure
[Report ID] [Command Type] [Effect ID] [Parameter]
     0x03        0x0e         0-15       0-127

// Example: Constant force effect
0x03 0x0e 0x01 0x40  // Effect 1, 50% force
```

### Key Integration Points
```c
// Required callback functions
tmff2->play_effect = t500rs_play_effect;
tmff2->upload_effect = t500rs_upload_effect;
tmff2->update_effect = t500rs_update_effect;
tmff2->stop_effect = t500rs_stop_effect;
tmff2->wheel_init = t500rs_wheel_init;
tmff2->wheel_destroy = t500rs_wheel_destroy;
```

### Safety Validation Commands
```bash
# Check module status
lsmod | grep tmff

# Monitor USB communication
dmesg | grep -i usb | tail -20

# Verify device detection
cat /proc/bus/input/devices | grep -A5 -B5 T500

# Test force feedback
fftest /dev/input/event[X]
```

## Emergency Procedures

### System Recovery
If system becomes unstable:
1. **Remove module**: `sudo modprobe -r hid_tmff_new`
2. **Check logs**: `dmesg | tail -100`
3. **Reboot if necessary**: Ensure clean state
4. **Document issue**: Add to test_troubleshooting.md

### Development Reset
If development gets stuck:
1. **Review troubleshooting docs**: Check for known solutions
2. **Validate against working modules**: Compare with T300RS implementation
3. **Simplify approach**: Return to basic functionality
4. **Seek guidance**: Reference implementation plan and safety guidelines

## Success Criteria

Each phase must meet these criteria before proceeding:
- **Functionality**: Required features work correctly
- **Stability**: No system crashes or hangs
- **Safety**: Proper error handling and resource cleanup
- **Integration**: Follows established patterns and interfaces
- **Documentation**: Progress tracked and issues documented

This guide ensures safe, systematic development of T500RS support while maintaining system stability and code quality standards.
