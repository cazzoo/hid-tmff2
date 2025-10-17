#!/bin/bash
# T500RS Wine Bridge - Quick Start Script
# This script helps you get Wine/Proton compatibility working

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BRIDGE_DIR="$SCRIPT_DIR/t500rs-wine-bridge"

echo "============================================"
echo "T500RS Wine Bridge Quick Start"
echo "============================================"
echo ""

# Step 1: Stop userspace driver
echo "[1/5] Stopping userspace driver to release USB device..."
sudo pkill -9 t500rs-ffb-modular 2>/dev/null || true
sleep 2

# Step 2: Capture HID descriptor
echo "[2/5] Capturing HID descriptor..."
HIDRAW_DEVICE=$(ls /sys/class/hidraw/*/device/uevent 2>/dev/null | while read f; do
    if grep -q "HID_NAME=.*T500.*\|HID_ID=0003:0000044F:0000B65E" "$f" 2>/dev/null; then
        dirname "$f"
        break
    fi
done)

if [ -z "$HIDRAW_DEVICE" ]; then
    echo "ERROR: T500RS device not found!"
    echo "Make sure the wheel is plugged in (PID should be b65e or b65d)"
    echo ""
    echo "Current USB devices:"
    lsusb | grep -i thrust || echo "  No Thrustmaster devices found"
    exit 1
fi

echo "Found device: $HIDRAW_DEVICE"

if [ -f "$HIDRAW_DEVICE/report_descriptor" ]; then
    sudo cat "$HIDRAW_DEVICE/report_descriptor" > "$SCRIPT_DIR/captures/t500rs_hid_descriptor.bin" 2>/dev/null || {
        echo "WARNING: Could not read HID descriptor"
        echo "Using minimal descriptor instead"
    }
    
    if [ -f "$SCRIPT_DIR/captures/t500rs_hid_descriptor.bin" ]; then
        SIZE=$(stat -c%s "$SCRIPT_DIR/captures/t500rs_hid_descriptor.bin")
        echo "HID descriptor captured: $SIZE bytes"
        echo "Saved to: captures/t500rs_hid_descriptor.bin"
    fi
fi

# Step 3: Create bridge directory
echo "[3/5] Creating Wine bridge project..."
mkdir -p "$BRIDGE_DIR/src"

# Step 4: Generate minimal UHID proxy
echo "[4/5] Generating minimal UHID proxy..."

cat > "$BRIDGE_DIR/src/uhid_proxy.c" << 'EOF'
/*
 * T500RS Wine Bridge - Minimal UHID Proxy
 * 
 * This creates a UHID device that Wine/Proton can see as a real T500RS.
 * 
 * Usage: sudo ./uhid_proxy
 */

#include <linux/uhid.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

static int uhid_fd = -1;
static int running = 1;

void signal_handler(int sig) {
    printf("\nShutting down...\n");
    running = 0;
}

// Minimal HID descriptor for testing
// TODO: Replace with real 130-byte descriptor from device
static unsigned char hid_descriptor[] = {
    // USB HID Report Descriptor - Minimal Joystick
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x04,        // Usage (Joystick)
    0xA1, 0x01,        // Collection (Application)
    
    // Steering axis (X)
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x30,        //   Usage (X)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0xFF,  //   Logical Maximum (65535)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    
    // Pedals (Y, Z, RZ)
    0x09, 0x31,        //   Usage (Y) - Throttle
    0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    
    0x09, 0x32,        //   Usage (Z) - Brake
    0x81, 0x02,        //   Input (Data,Var,Abs)
    
    0x09, 0x35,        //   Usage (Rz) - Clutch
    0x81, 0x02,        //   Input (Data,Var,Abs)
    
    // Buttons (16 buttons)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x10,        //   Usage Maximum (Button 16)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x10,        //   Report Count (16)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    
    0xC0               // End Collection
};

int uhid_create(void) {
    uhid_fd = open("/dev/uhid", O_RDWR | O_CLOEXEC);
    if (uhid_fd < 0) {
        perror("Cannot open /dev/uhid");
        printf("\nMake sure UHID module is loaded: sudo modprobe uhid\n");
        return -1;
    }
    
    struct uhid_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = UHID_CREATE2;
    
    strcpy((char*)ev.u.create2.name, "T500RS Racing Wheel (Wine Bridge)");
    ev.u.create2.rd_data = hid_descriptor;
    ev.u.create2.rd_size = sizeof(hid_descriptor);
    ev.u.create2.bus = BUS_USB;
    ev.u.create2.vendor = 0x044F;
    ev.u.create2.product = 0xB65E;
    ev.u.create2.version = 0x0100;
    ev.u.create2.country = 0;
    
    if (write(uhid_fd, &ev, sizeof(ev)) < 0) {
        perror("Cannot create UHID device");
        close(uhid_fd);
        return -1;
    }
    
    printf("✓ UHID device created successfully!\n");
    printf("  VID:PID = 044f:b65e\n");
    printf("  Name: T500RS Racing Wheel (Wine Bridge)\n");
    printf("\n");
    printf("Check with: lsusb | grep 044f\n");
    printf("Test Wine: WINEDEBUG=+dinput wine control joy.cpl\n");
    printf("\n");
    printf("Press Ctrl+C to stop.\n");
    
    return 0;
}

void uhid_destroy(void) {
    if (uhid_fd < 0)
        return;
    
    struct uhid_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = UHID_DESTROY;
    write(uhid_fd, &ev, sizeof(ev));
    
    close(uhid_fd);
    uhid_fd = -1;
    
    printf("UHID device destroyed.\n");
}

int main(void) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("T500RS Wine Bridge - Minimal UHID Proxy\n");
    printf("========================================\n\n");
    
    if (uhid_create() < 0) {
        return 1;
    }
    
    // Just wait for signal
    while (running) {
        sleep(1);
    }
    
    uhid_destroy();
    return 0;
}
EOF

# Step 5: Compile
echo "[5/5] Compiling UHID proxy..."
cd "$BRIDGE_DIR"
gcc -o uhid_proxy src/uhid_proxy.c -Wall
cd "$SCRIPT_DIR"

echo ""
echo "============================================"
echo "Setup complete!"
echo "============================================"
echo ""
echo "Next steps:"
echo ""
echo "1. Load UHID kernel module:"
echo "   sudo modprobe uhid"
echo ""
echo "2. Start the Wine bridge:"
echo "   cd t500rs-wine-bridge"
echo "   sudo ./uhid_proxy"
echo ""
echo "3. In another terminal, test with Wine:"
echo "   WINEDEBUG=+dinput wine control joy.cpl"
echo ""
echo "4. You should see the T500RS device!"
echo ""
echo "Notes:"
echo "- This is a minimal version (input only, no FF yet)"
echo "- The HID descriptor is simplified"
echo "- Full implementation needs IPC with userspace driver"
echo ""
echo "See WINE_COMPATIBILITY_ANALYSIS.md for details"
echo ""