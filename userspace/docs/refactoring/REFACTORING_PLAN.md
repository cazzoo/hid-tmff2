# T500RS Driver Refactoring Plan

## Overview

Refactoring the monolithic t500rs-ffb.c (2998 lines, 38 functions) into a modular, maintainable codebase following industry best practices.

## Current State

```
userspace/
├── t500rs-ffb.c          (2998 lines - MONOLITHIC)
├── test_*.c              (scattered test files)
├── *.md                  (documentation mixed with code)
└── Makefile
```

## Target State

```
userspace/
├── src/                  # Source files
│   ├── t500rs_main.c     # Main event loop and coordination
│   ├── t500rs_usb.c      # USB communication and initialization
│   ├── t500rs_input.c    # Input handling
│   ├── t500rs_effects.c  # FFB effect management
│   └── t500rs_force.c    # Force calculation and processing
├── include/              # Header files
│   ├── t500rs_common.h   # Shared definitions (DONE)
│   ├── t500rs_usb.h      # USB module API (DONE)
│   ├── t500rs_input.h    # Input module API (DONE)
│   ├── t500rs_effects.h  # Effects module API (DONE)
│   └── t500rs_force.h    # Force module API (DONE)
├── tests/                # Test programs
│   ├── test_direction.c
│   ├── test_envelope.c
│   ├── test_multi_effect.c
│   ├── test_all_effects.c
│   └── test_input_reading.c
├── docs/                 # Documentation
│   ├── *.md
│   └── README.md
├── Makefile              # Updated build system
└── README.md             # Project overview
```

## Module Breakdown

### 1. t500rs_usb.c (USB Communication)
**Functions to extract:**
- `usb_send()` - Send data via interrupt transfer
- `usb_receive()` - Receive data via interrupt transfer
- `t500rs_initialize()` - Device initialization and mode switch
- `usb_device_open()` - Open device, detach kernel driver, claim interface
- `usb_device_close()` - Release interface, close device
- `usb_wait_for_reenumeration()` - Wait for mode switch completion

**Responsibilities:**
- Low-level USB communication
- Device initialization sequence
- Mode switch (b65d → b65e)
- Re-enumeration handling
- Error handling for USB operations

### 2. t500rs_input.c (Input Handling)
**Functions to extract:**
- `input_device_create()` - Create uinput device
- `input_device_destroy()` - Destroy uinput device
- `input_process_report()` - Parse and process input reports
- `input_thread_start()` - Start input reading thread
- `input_thread_stop()` - Stop input reading thread
- `input_thread_func()` - Input reading thread function

**Responsibilities:**
- uinput device management
- Input report parsing (steering, pedals, buttons, D-pad)
- Pedal inversion
- Input event generation
- Background input reading

### 3. t500rs_effects.c (Effect Management)
**Functions to extract:**
- `upload_constant_effect()` - Upload constant force
- `upload_periodic_effect()` - Upload periodic effects
- `upload_condition_effect()` - Upload condition effects
- `upload_ramp_effect()` - Upload ramp effects (disabled)
- `start_effect()` - Start playing effect
- `stop_effect()` - Stop playing effect
- `set_gain()` - Set global gain
- `set_autocenter()` - Set autocenter strength
- `apply_effect_gain()` - Apply per-effect-type gain

**Responsibilities:**
- Effect upload to device
- Effect lifecycle management
- Gain control
- Autocenter control
- Effect state tracking

### 4. t500rs_force.c (Force Calculation)
**Functions to extract:**
- `apply_envelope()` - Calculate envelope (attack/fade)
- `apply_force_smoothing()` - Exponential smoothing
- `mix_forces()` - Multi-effect mixing
- `calculate_update_interval()` - Dynamic update rate
- `get_elapsed_ms()` - Time calculation
- `force_thread_start()` - Start force update thread
- `force_thread_stop()` - Stop force update thread
- `force_update_thread_func()` - Force update thread function

**Responsibilities:**
- Envelope calculation
- Force smoothing
- Multi-effect mixing
- Dynamic update rate optimization
- Continuous force updates

### 5. t500rs_main.c (Main Program)
**Functions to keep:**
- `main()` - Program entry point
- `cleanup()` - Cleanup and shutdown
- `signal_handler()` - Signal handling
- Event loop coordination

**Responsibilities:**
- Program initialization
- Event loop (uinput events)
- Module coordination
- Cleanup and shutdown
- Signal handling

## Implementation Steps

### Phase 1: Headers (DONE ✅)
- [x] Create include/ directory
- [x] Create t500rs_common.h
- [x] Create t500rs_usb.h
- [x] Create t500rs_input.h
- [x] Create t500rs_effects.h
- [x] Create t500rs_force.h

### Phase 2: Source Files (IN PROGRESS)
- [ ] Create src/ directory
- [ ] Extract t500rs_usb.c
- [ ] Extract t500rs_input.c
- [ ] Extract t500rs_effects.c
- [ ] Extract t500rs_force.c
- [ ] Create t500rs_main.c

### Phase 3: Build System
- [ ] Update Makefile for modular build
- [ ] Add dependency tracking
- [ ] Separate compilation for each module
- [ ] Link all modules together

### Phase 4: Testing
- [ ] Move test files to tests/
- [ ] Update test Makefile
- [ ] Verify all tests compile
- [ ] Run all tests to verify functionality

### Phase 5: Documentation
- [ ] Move docs to docs/
- [ ] Create README.md
- [ ] Update documentation for new structure
- [ ] Add module documentation

### Phase 6: Cleanup
- [ ] Remove old t500rs-ffb.c
- [ ] Remove unused code
- [ ] Clean up warnings
- [ ] Final testing

## Benefits

### Code Quality
- **Single Responsibility**: Each module has one clear purpose
- **Maintainability**: Easier to understand and modify
- **Testability**: Modules can be tested independently
- **Reusability**: Modules can be reused in other projects

### Development
- **Faster Compilation**: Only changed modules recompile
- **Easier Debugging**: Smaller, focused modules
- **Better Documentation**: Clear module boundaries
- **Code Review**: Easier to review changes

### Professional Standards
- **Industry Best Practices**: Follows C project conventions
- **Scalability**: Easy to add new features
- **Collaboration**: Multiple developers can work on different modules
- **Quality**: Passes professional code review standards

## Constraints

### Must Maintain
- ✅ All existing functionality
- ✅ Same external behavior
- ✅ All working features
- ✅ Test compatibility
- ✅ Performance characteristics

### Must Not Break
- ✅ Mode switch
- ✅ Input handling
- ✅ Force feedback
- ✅ All effects
- ✅ Advanced features (smoothing, mixing, etc.)

## Success Criteria

- [ ] All modules compile without errors
- [ ] All tests pass
- [ ] No functionality regression
- [ ] Code is well-documented
- [ ] Build system works correctly
- [ ] Professional code organization
- [ ] Passes code review standards

## Timeline

1. **Headers**: 1 hour (DONE ✅)
2. **Source extraction**: 3-4 hours
3. **Build system**: 1 hour
4. **Testing**: 1 hour
5. **Documentation**: 1 hour
6. **Cleanup**: 30 minutes

**Total**: ~7-8 hours of focused work

## Current Status

- ✅ Phase 1: Headers complete
- 🔄 Phase 2: Source files (next)
- ⏳ Phase 3: Build system
- ⏳ Phase 4: Testing
- ⏳ Phase 5: Documentation
- ⏳ Phase 6: Cleanup

