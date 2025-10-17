# Wine/Proton Compatibility Issue - Analysis & Solutions

**Date:** 2025-10-14  
**Problem:** Userspace driver works in native Linux but Wine/Proton games cannot see it  
**Status:** Critical issue identified, solutions proposed

---

## 🔴 The Problem

### Current Situation

```
┌─────────────────────────────────────────────────────────┐
│         NATIVE LINUX (WORKS ✅)                         │
│                                                          │
│  Game → /dev/input/eventX → uinput device → FFB works  │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│         WINE/PROTON (BROKEN ❌)                         │
│                                                          │
│  Game → DirectInput → looks for USB HID device          │
│         (044f:b65e) → NOT FOUND!                        │
│                                                          │
│  Why? Userspace driver has claimed the USB device       │
│       using libusb_claim_interface()                    │
└─────────────────────────────────────────────────────────┘
```

### Root Cause

The userspace driver (`t500rs-ffb-modular`) does:

1. **Claims the real USB device** via libusb
   ```c
   // From t500rs_usb.c
   libusb_claim_interface(usb_handle, INTERFACE);
   ```
   This makes the device **unavailable** to other programs, including Wine!

2. **Creates a uinput virtual device**
   ```c
   // From t500rs_input.c
   uinput_fd = open("/dev/uinput", O_RDWR | O_NONBLOCK);
   // Creates: "T500RS Force Feedback Wheel" as /dev/input/eventX
   ```

3. **Wine HID can't see uinput devices properly**
   - Wine's HID implementation looks for **real USB HID devices**
   - Wine checks `/dev/bus/usb/` or uses hidraw
   - uinput devices are not recognized as the original VID:PID

### Why Windows DLL Installation Doesn't Help

Even with Windows drivers installed:
```
Wine Game → Windows tmPID64.dll → HidD_SetFeature() →
Wine HID layer → looks for USB device 044f:b65e →
NOT FOUND (claimed by Linux driver) → FAIL
```

---

## ✅ Solution Options

### Option 1: **UHID Proxy** (Recommended)

Create a UHID device that emulates the real T500RS to Wine while forwarding to userspace driver.

**How it works:**
```
┌──────────────────────────────────────────────────────────────┐
│  Wine Game                                                    │
│      ↓                                                        │
│  DirectInput → Wine HID                                       │
│      ↓                                                        │
│  UHID device (/dev/uhid) - appears as real T500RS            │
│  VID:044F PID:B65E                                           │
│      ↓                                                        │
│  uhid_proxy daemon (NEW COMPONENT)                           │
│      ↓                                                        │
│  t500rs-ffb-modular (userspace driver)                       │
│      ↓                                                        │
│  Real USB device                                             │
└──────────────────────────────────────────────────────────────┘
```

**Implementation:**
```c
// uhid_t500rs_proxy.c
#include <linux/uhid.h>
#include <sys/socket.h>
#include <sys/un.h>

// 1. Create UHID device with T500RS descriptors
// 2. Accept connections from userspace driver
// 3. Forward HID reports between Wine and driver
// 4. Translate Wine FF commands to driver format
```

**Pros:**
- Wine sees a "real" T500RS device
- No Wine code changes needed
- Works with any Wine game
- Proper VID:PID matching

**Cons:**
- Requires UHID kernel module
- Needs 130-byte HID descriptor
- More complex implementation

---

### Option 2: **Wine HID Passthrough** (Simple)

Modify the userspace driver to NOT claim the USB device exclusively.

**How it works:**
```
1. Userspace driver detaches kernel driver
2. Opens device for communication
3. Does NOT claim interface
4. Wine can still access the device via hidraw
5. Both coexist (carefully!)
```

**Implementation:**
```c
// In t500rs_usb.c, modify:
int usb_device_open(void) {
    // ...
    
    // OLD (blocks Wine):
    ret = libusb_claim_interface(usb_handle, INTERFACE);
    
    // NEW (allows Wine):
    ret = libusb_detach_kernel_driver(usb_handle, INTERFACE);
    // Skip claiming - let hidraw handle it
    
    // Use interrupt transfers in "shared" mode
    // ...
}
```

**Pros:**
- Simple change
- Both Linux and Wine can access device
- No new components

**Cons:**
- Race conditions possible
- Conflicts between driver and Wine
- Unreliable force feedback

---

### Option 3: **Shared Memory IPC** (Hybrid)

Let Wine talk to the userspace driver via shared memory.

**How it works:**
```
┌──────────────────────────────────────────────────────────────┐
│  Wine Game → Windows DLL (tmPID64.dll - CUSTOM VERSION)      │
│      ↓                                                        │
│  Shared Memory Segment: /dev/shm/t500rs_ffb                  │
│      ↓                                                        │
│  t500rs-ffb-modular reads from shm                           │
│      ↓                                                        │
│  Real USB device                                             │
└──────────────────────────────────────────────────────────────┘
```

**Implementation:**
```c
// tmPID64_linux.c (Wine-native DLL)
#include <sys/mman.h>

struct t500rs_shm {
    int force_level;
    int effect_type;
    int effect_id;
    int gain;
    // ...
};

HRESULT WINAPI IDirectInputEffect_SetParameters(...) {
    // Map shared memory
    int fd = shm_open("/t500rs_ffb", O_RDWR, 0666);
    struct t500rs_shm *shm = mmap(...);
    
    // Write effect data
    shm->force_level = effect->magnitude;
    shm->effect_type = effect->type;
    // ...
    
    return DI_OK;
}
```

**Pros:**
- Fast IPC
- No kernel modules needed
- Clean separation

**Cons:**
- Requires custom Wine DLL
- Game must be configured to use native DLL
- More development effort

---

### Option 4: **Socket-Based Bridge** (Most Flexible)

Create a socket server in the userspace driver that Wine DLL connects to.

**How it works:**
```
┌──────────────────────────────────────────────────────────────┐
│  Wine Game → Windows DLL (tmPID64.dll - CUSTOM)              │
│      ↓                                                        │
│  Unix socket: /tmp/t500rs.sock                               │
│      ↓                                                        │
│  t500rs-ffb-modular (socket server)                          │
│      ↓                                                        │
│  Real USB device                                             │
└──────────────────────────────────────────────────────────────┘
```

**Implementation:**
```c
// In userspace driver - add socket server
int socket_server_init(void) {
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = {
        .sun_family = AF_UNIX,
        .sun_path = "/tmp/t500rs.sock"
    };
    bind(sock_fd, &addr, sizeof(addr));
    listen(sock_fd, 5);
    // Accept Wine connections...
}

// Wine DLL
HRESULT WINAPI IDirectInputEffect_SetParameters(...) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    connect(sock, "/tmp/t500rs.sock");
    
    // Send effect data as JSON/binary
    struct effect_msg msg = { ... };
    send(sock, &msg, sizeof(msg), 0);
}
```

**Pros:**
- Network-transparent
- Easy debugging
- Works across containers/VMs
- No kernel dependencies

**Cons:**
- Latency (small)
- Requires custom Wine DLL
- Protocol design needed

---

## 🎯 Recommended Solution: **UHID Proxy**

### Why UHID?

1. **Perfect Wine Compatibility**
   - Wine sees exact T500RS device (VID:044F PID:B65E)
   - No Wine code changes
   - Works with Windows drivers in Wine

2. **Proper HID Emulation**
   - Emulates complete HID device
   - Handles all HID reports correctly
   - Clean architecture

3. **Future-Proof**
   - Works with kernel driver later
   - Compatible with all games
   - Standard Linux approach

### Implementation Plan

**Phase 1: Create UHID Proxy (2-3 days)**

1. Capture 130-byte HID descriptor
2. Create UHID device creation code
3. Implement report forwarding
4. Test with simple game

**Phase 2: Driver Integration (1-2 days)**

1. Add IPC between proxy and driver
2. Unix socket or D-Bus
3. Forward input/output reports
4. Handle force feedback commands

**Phase 3: Wine Testing (1 day)**

1. Test with Assetto Corsa
2. Test with Automobilista 2
3. Test with F1 games
4. Document configuration

---

## 📝 UHID Proxy Implementation

### Code Structure

```
t500rs-wine-bridge/
├── src/
│   ├── uhid_device.c          # UHID device creation
│   ├── hid_descriptor.c       # 130-byte descriptor
│   ├── ipc_server.c           # Unix socket server
│   ├── report_handler.c       # HID report processing
│   └── main.c                 # Main daemon
├── wine/
│   └── tmPID64_passthrough.c  # Optional Wine DLL
├── systemd/
│   └── t500rs-wine-bridge.service
└── README.md
```

### UHID Device Creation

```c
// uhid_device.c
#include <linux/uhid.h>
#include <fcntl.h>

// HID Report Descriptor (130 bytes from device)
static uint8_t hid_descriptor[130] = {
    // TODO: Capture from real device
    // For now, use minimal descriptor
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x04,  // Usage (Joystick)
    0xA1, 0x01,  // Collection (Application)
    // ... more bytes ...
};

int uhid_create_device(void) {
    int fd = open("/dev/uhid", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("Failed to open /dev/uhid");
        return -1;
    }
    
    struct uhid_event ev = {0};
    ev.type = UHID_CREATE2;
    strcpy((char*)ev.u.create2.name, "T500RS Racing Wheel");
    ev.u.create2.rd_data = hid_descriptor;
    ev.u.create2.rd_size = sizeof(hid_descriptor);
    ev.u.create2.bus = BUS_USB;
    ev.u.create2.vendor = 0x044F;
    ev.u.create2.product = 0xB65E;
    ev.u.create2.version = 0x0100;
    ev.u.create2.country = 0;
    
    if (write(fd, &ev, sizeof(ev)) < 0) {
        perror("Failed to create UHID device");
        close(fd);
        return -1;
    }
    
    printf("UHID device created: VID:044F PID:B65E\n");
    return fd;
}

int uhid_send_input_report(int fd, const uint8_t *data, size_t len) {
    struct uhid_event ev = {0};
    ev.type = UHID_INPUT2;
    ev.u.input2.size = len;
    memcpy(ev.u.input2.data, data, len);
    
    return write(fd, &ev, sizeof(ev));
}
```

### IPC with Userspace Driver

```c
// ipc_server.c
#include <sys/socket.h>
#include <sys/un.h>

#define SOCKET_PATH "/tmp/t500rs_bridge.sock"

struct ffb_message {
    uint8_t report_id;
    uint8_t effect_type;
    uint8_t effect_id;
    int16_t magnitude;
    uint16_t duration;
    uint8_t gain;
    uint8_t data[256];
} __attribute__((packed));

int ipc_server_init(void) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    
    struct sockaddr_un addr = {
        .sun_family = AF_UNIX,
        .sun_path = SOCKET_PATH
    };
    
    unlink(SOCKET_PATH);
    bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(sock, 1);
    
    printf("IPC server listening on %s\n", SOCKET_PATH);
    return sock;
}

int ipc_forward_to_driver(int driver_sock, const struct ffb_message *msg) {
    // Forward Wine FF command to userspace driver
    return send(driver_sock, msg, sizeof(*msg), 0);
}
```

### Driver Modification

```c
// Add to t500rs_main.c
#include <sys/socket.h>
#include <sys/un.h>

static int bridge_sock = -1;

int bridge_client_init(void) {
    bridge_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    
    struct sockaddr_un addr = {
        .sun_family = AF_UNIX,
        .sun_path = "/tmp/t500rs_bridge.sock"
    };
    
    if (connect(bridge_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_INFO("Wine bridge not running (this is OK for native Linux)");
        bridge_sock = -1;
        return 0;  // Not an error
    }
    
    LOG_INFO("Connected to Wine bridge");
    return 0;
}

// In force update loop
void send_input_to_bridge(const uint8_t *report, size_t len) {
    if (bridge_sock >= 0) {
        send(bridge_sock, report, len, MSG_DONTWAIT);
    }
}
```

---

## 🚀 Quick Start Implementation

### Step 1: Test Current State

```bash
# Start userspace driver
sudo ./userspace/t500rs-ffb-modular &

# Check what Wine sees
WINEDEBUG=+dinput wine control joy.cpl

# Expected: No T500RS device shown
```

### Step 2: Capture HID Descriptor

```bash
# While driver is running, device is claimed
# Need to stop driver first
sudo pkill t500rs-ffb

# Capture descriptor
sudo cat /sys/class/hidraw/hidraw*/device/report_descriptor > t500rs_descriptor.bin
hexdump -C t500rs_descriptor.bin

# Or from USB capture
sudo tshark -i usbmon2 -Y "usb.setup.wValue == 0x2200" -w descriptor_capture.pcap
```

### Step 3: Build UHID Proxy

```bash
# Create project
mkdir -p t500rs-wine-bridge/src
cd t500rs-wine-bridge

# Create uhid_proxy.c (minimal version)
cat > src/uhid_proxy.c << 'EOF'
#include <linux/uhid.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

int main() {
    int fd = open("/dev/uhid", O_RDWR);
    if (fd < 0) {
        perror("Cannot open /dev/uhid");
        return 1;
    }
    
    // Minimal HID descriptor (will replace with real one)
    uint8_t desc[] = {
        0x05, 0x01, 0x09, 0x04, 0xA1, 0x01,  // Joystick
        // ... TODO: Add full 130 bytes
        0xC0  // End Collection
    };
    
    struct uhid_event ev = {0};
    ev.type = UHID_CREATE2;
    strcpy((char*)ev.u.create2.name, "T500RS Racing Wheel (Wine Bridge)");
    ev.u.create2.rd_data = desc;
    ev.u.create2.rd_size = sizeof(desc);
    ev.u.create2.bus = BUS_USB;
    ev.u.create2.vendor = 0x044F;
    ev.u.create2.product = 0xB65E;
    
    write(fd, &ev, sizeof(ev));
    
    printf("UHID device created. Press Ctrl+C to exit.\n");
    sleep(999999);
    
    ev.type = UHID_DESTROY;
    write(fd, &ev, sizeof(ev));
    close(fd);
    return 0;
}
EOF

# Compile
gcc -o uhid_proxy src/uhid_proxy.c

# Test
sudo ./uhid_proxy &
lsusb | grep 044f
# Should now show the device!
```

### Step 4: Test with Wine

```bash
# With uhid_proxy running
WINEDEBUG=+dinput wine control joy.cpl

# Should now see "T500RS Racing Wheel"
```

---

## 📊 Comparison Matrix

| Solution | Complexity | Wine Compat | Latency | Kernel Req | Dev Time |
|----------|------------|-------------|---------|------------|----------|
| UHID Proxy | Medium | ⭐⭐⭐⭐⭐ | Low | UHID module | 3-4 days |
| Shared Memory | Medium | ⭐⭐⭐⭐☆ | Very Low | None | 2-3 days |
| Socket Bridge | Low | ⭐⭐⭐⭐☆ | Low | None | 2-3 days |
| Passthrough | Low | ⭐⭐☆☆☆ | Low | None | 1 day |

---

## 🎯 Immediate Action Plan

### Today (4 hours)

1. **Capture HID descriptor** (1 hour)
   ```bash
   sudo pkill t500rs-ffb
   sudo cat /sys/class/hidraw/hidraw*/device/report_descriptor > descriptor.bin
   ```

2. **Create minimal UHID proxy** (2 hours)
   - Basic UHID device creation
   - Use captured descriptor
   - Test Wine detection

3. **Test with one game** (1 hour)
   - Assetto Corsa or similar
   - Verify device shows up
   - Test basic input (no FF yet)

### Tomorrow (8 hours)

1. **Add IPC layer** (4 hours)
   - Unix socket between proxy and driver
   - Forward input reports
   - Forward output reports

2. **Integrate with userspace driver** (2 hours)
   - Modify driver to connect to proxy
   - Send input data to proxy
   - Receive FF commands from proxy

3. **Test force feedback** (2 hours)
   - Full game testing
   - Verify FF works
   - Debug issues

### Day 3 (4 hours)

1. **Polish and document** (2 hours)
2. **Create systemd service** (1 hour)
3. **Write user guide** (1 hour)

---

## 📚 References

- Linux UHID documentation: `/usr/src/linux/Documentation/hid/uhid.rst`
- Wine HID implementation: `wine/dlls/winebus.sys/`
- Userspace driver: `userspace/src/`

---

**Status:** Ready to implement UHID proxy solution

**Next Step:** Capture HID descriptor and create minimal UHID proxy

**END OF ANALYSIS**
