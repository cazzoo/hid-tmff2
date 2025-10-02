# T500RS Userspace Driver Plan

## Decision: Userspace is the Right Approach

After extensive testing, we've proven:
- ✅ libusb communication works perfectly
- ✅ Force feedback works via libusb
- ✅ Initialization works via libusb
- ❌ Kernel HID layer blocks all raw USB access (error -11)

**Conclusion**: The T500RS uses a proprietary protocol that doesn't fit the kernel HID model. A userspace driver is the correct solution.

## Architecture

```
┌─────────────────────────────────────────┐
│         Game / Application              │
│    (reads /dev/input/eventX)            │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│      Linux Input Subsystem              │
│         (uinput device)                 │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│    t500rs-ffb Userspace Daemon          │
│  - Reads input events from real device  │
│  - Handles FF upload/play/stop          │
│  - Sends USB commands via libusb        │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│         T500RS Hardware                 │
│    (via libusb, endpoint 0x01)          │
└─────────────────────────────────────────┘
```

## Implementation Plan

### Phase 1: Basic Daemon (2-3 hours)
- Read input from real T500RS device
- Create uinput virtual device
- Forward button/axis events
- Handle force feedback upload/play/stop
- Send commands via libusb

### Phase 2: Full Features (2-3 hours)
- All effect types (constant, spring, damper, etc.)
- Gain control
- Autocenter
- Range setting
- Configuration file

### Phase 3: Polish (1-2 hours)
- Systemd service
- Auto-start on device plug
- Logging
- Error handling

## Advantages of Userspace

1. **Works NOW** - No kernel patches needed
2. **Easy to debug** - printf debugging, gdb, etc.
3. **Easy to update** - No kernel recompile
4. **Portable** - Works on any Linux distro
5. **Proven** - libusb already works

## Similar Projects

- **Oversteer** - Logitech wheel configuration (userspace)
- **new-lg4ff** - Logitech force feedback (kernel, but standard HID)
- **ffbwrap** - Force feedback wrapper (userspace)

## Next Steps

1. Create basic daemon skeleton
2. Test with simple constant force
3. Add all effect types
4. Package and distribute

## Timeline

- **Today**: Basic working daemon (3-4 hours)
- **Tomorrow**: Full features + testing
- **Day 3**: Polish, package, release

Would you like me to start building the userspace driver?

