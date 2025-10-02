#!/bin/bash
#
# Test script for T500RS FFB driver
# This script starts the driver and runs basic tests
#

set -e

echo "=========================================="
echo "T500RS Force Feedback Driver - Test Script"
echo "=========================================="
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Error: This script must be run as root"
    echo "Usage: sudo ./test_driver.sh"
    exit 1
fi

# Check if device is connected
echo "[1/5] Checking for T500RS..."
if ! lsusb -d 044f:b65e > /dev/null 2>&1; then
    echo "❌ T500RS not found!"
    echo "   Please connect the device and try again."
    exit 1
fi
echo "✅ T500RS detected"
echo ""

# Build if needed
echo "[2/5] Checking build..."
if [ ! -f "t500rs-ffb" ]; then
    echo "Building driver..."
    make
fi
echo "✅ Driver built"
echo ""

# Unload kernel driver
echo "[3/5] Preparing system..."
if lsmod | grep -q hid_tmff_new; then
    echo "Unloading kernel driver..."
    rmmod hid_tmff_new || true
fi

if ! lsmod | grep -q uinput; then
    echo "Loading uinput module..."
    modprobe uinput
fi
echo "✅ System ready"
echo ""

# Start driver in background
echo "[4/5] Starting driver..."
./t500rs-ffb > /tmp/t500rs-ffb.log 2>&1 &
DRIVER_PID=$!

# Wait for initialization
sleep 2

# Check if driver is running
if ! kill -0 $DRIVER_PID 2>/dev/null; then
    echo "❌ Driver failed to start!"
    echo "   Check /tmp/t500rs-ffb.log for errors"
    cat /tmp/t500rs-ffb.log
    exit 1
fi
echo "✅ Driver started (PID: $DRIVER_PID)"
echo ""

# Find the device
echo "[5/5] Finding virtual device..."
sleep 1

DEVICE=$(dmesg | grep "T500RS (FFB)" | tail -1 | grep -oP '/dev/input/event\d+' || echo "")

if [ -z "$DEVICE" ]; then
    echo "❌ Virtual device not found!"
    echo "   Check /tmp/t500rs-ffb.log for errors"
    kill $DRIVER_PID
    exit 1
fi

echo "✅ Virtual device created: $DEVICE"
echo ""

echo "=========================================="
echo "SUCCESS! Driver is running"
echo "=========================================="
echo ""
echo "Driver PID: $DRIVER_PID"
echo "Device: $DEVICE"
echo "Log file: /tmp/t500rs-ffb.log"
echo ""
echo "To test force feedback:"
echo "  fftest $DEVICE"
echo ""
echo "To stop the driver:"
echo "  sudo kill $DRIVER_PID"
echo ""
echo "To view logs:"
echo "  tail -f /tmp/t500rs-ffb.log"
echo ""
echo "=========================================="
echo ""

# Ask if user wants to run fftest
read -p "Run fftest now? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    if command -v fftest &> /dev/null; then
        echo "Starting fftest..."
        echo "Try uploading and playing a constant force effect!"
        echo ""
        fftest $DEVICE
    else
        echo "fftest not found. Install with:"
        echo "  sudo pacman -S linuxconsole  # Arch"
        echo "  sudo apt-get install joystick  # Ubuntu"
    fi
fi

# Cleanup
echo ""
echo "Stopping driver..."
kill $DRIVER_PID
wait $DRIVER_PID 2>/dev/null || true

echo "✅ Test complete!"

