#!/bin/bash
#
# Quick start script for T500RS FFB driver
#

set -e

echo "=========================================="
echo "T500RS Force Feedback Driver - Quick Start"
echo "=========================================="
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Error: This script must be run as root"
    echo "Usage: sudo ./run.sh"
    exit 1
fi

# Check if driver is built
if [ ! -f "t500rs-ffb" ]; then
    echo "Driver not built. Building now..."
    make
    echo ""
fi

# Check if device is connected
if ! lsusb -d 044f:b65e -d 044f:b65d > /dev/null 2>&1; then
    echo "Error: T500RS not found!"
    echo "Please connect the device and try again."
    exit 1
fi

echo "✅ T500RS detected"
echo ""

# Kill any existing driver instances
if pgrep -x "t500rs-ffb" > /dev/null; then
    echo "Stopping existing driver instances..."
    pkill -9 t500rs-ffb
    sleep 1
fi

# Unload kernel driver if loaded
if lsmod | grep -q hid_tmff_new; then
    echo "Unloading kernel driver..."
    rmmod hid_tmff_new || true
    sleep 1
fi

# Unbind from generic HID if needed
if [ -d "/sys/bus/usb/drivers/usbhid" ]; then
    for dev in /sys/bus/usb/drivers/usbhid/*; do
        if [ -f "$dev/idVendor" ] && [ -f "$dev/idProduct" ]; then
            vendor=$(cat "$dev/idVendor" 2>/dev/null)
            product=$(cat "$dev/idProduct" 2>/dev/null)
            if [ "$vendor" = "044f" ] && [ "$product" = "b65e" ]; then
                devname=$(basename "$dev")
                echo "Unbinding T500RS from usbhid: $devname"
                echo "$devname" > /sys/bus/usb/drivers/usbhid/unbind 2>/dev/null || true
            fi
        fi
    done
fi

# Load uinput module
if ! lsmod | grep -q uinput; then
    echo "Loading uinput module..."
    modprobe uinput
fi

echo "Starting driver..."
echo ""

# Start driver and capture initial output
./t500rs-ffb 2>&1 &
DRIVER_PID=$!

# Give it time to initialize and show output
sleep 3

# Check if driver is still running
if ! kill -0 $DRIVER_PID 2>/dev/null; then
    echo ""
    echo "❌ Driver failed to start!"
    exit 1
fi

# Find the device
echo ""
echo "=========================================="
echo "Finding T500RS FFB Device..."
echo "=========================================="
echo ""

DEVICE=""

# Method 1: Check /sys/class/input (most reliable)
for dev in /sys/class/input/event*/device/name; do
    if [ -f "$dev" ]; then
        NAME=$(cat "$dev" 2>/dev/null)
        if [[ "$NAME" == *"T500RS"*"FFB"* ]]; then
            # Extract event number from path
            EVENT=$(echo "$dev" | grep -oP 'event\d+')
            DEVICE="/dev/input/$EVENT"
            break
        fi
    fi
done

# Method 2: Check dmesg as fallback
if [ -z "$DEVICE" ]; then
    DEVICE=$(dmesg | grep "input.*T500RS.*FFB" | tail -1 | grep -oP '/dev/input/event\d+')
fi

if [ -n "$DEVICE" ]; then
    echo "✅ Device found: $DEVICE"
    echo ""
    echo "=========================================="
    echo "T500RS Force Feedback Driver Running"
    echo "=========================================="
    echo ""
    echo "Device: $DEVICE"
    echo "Driver PID: $DRIVER_PID"
    echo ""
    echo "Test force feedback with:"
    echo "  sudo fftest $DEVICE"
    echo ""
    echo "Or run comprehensive test (auto-detects device):"
    echo "  sudo ./test_all_effects"
    echo ""
    echo "Or specify device manually:"
    echo "  sudo ./test_all_effects $DEVICE"
    echo ""
    echo "Press Ctrl+C to stop the driver"
    echo "=========================================="
    echo ""
else
    echo "⚠️  Device not found!"
    echo ""
    echo "Driver is running (PID: $DRIVER_PID)"
    echo ""
    echo "Troubleshooting:"
    echo "1. Check if driver initialized: ps aux | grep t500rs-ffb"
    echo "2. Check for errors: dmesg | tail -20"
    echo "3. Try finding device: sudo ./find_device.sh"
    echo ""
    echo "Press Ctrl+C to stop the driver"
    echo "=========================================="
    echo ""
fi

# Wait for driver (foreground)
wait $DRIVER_PID

