#!/bin/bash
# Test T500RS with detailed debug output

echo "=========================================="
echo "T500RS Debug Test"
echo "=========================================="
echo ""

if [ "$EUID" -ne 0 ]; then 
    echo "Please run: sudo ./test_with_debug.sh"
    exit 1
fi

echo "This script will help you reload the driver with debug output."
echo ""
echo "INSTRUCTIONS:"
echo "1. Unplug the T500RS USB cable"
echo "2. Press ENTER"
read -p "Press ENTER when unplugged..."

echo ""
echo "Removing old module..."
rmmod hid_tmff_new 2>/dev/null
rmmod hid_tminit_new 2>/dev/null
sleep 2

echo "Loading new modules..."
cd deps/hid-tminit
insmod ./hid-tminit-new.ko
cd ../..
insmod ./hid_tmff_new.ko

echo ""
echo "Modules loaded. Now:"
echo "3. Plug in the T500RS USB cable"
echo "4. Press ENTER"
read -p "Press ENTER when plugged in..."

sleep 5

echo ""
echo "Checking device status..."
echo "=========================================="
dmesg | tail -40 | grep -i "t500\|tmff\|thrust"
echo "=========================================="

echo ""
echo "Finding active device..."

# Find the most recent event device that actually exists
DEVICE=""
for event_path in /dev/input/event*; do
    if [ -e "$event_path" ]; then
        # Check if this is a Thrustmaster device
        INFO=$(udevadm info "$event_path" 2>/dev/null | grep -i "thrustmaster\|044f:b65e")
        if [ -n "$INFO" ]; then
            DEVICE="$event_path"
            echo "✓ Found active device: $DEVICE"
            break
        fi
    fi
done

# Fallback: use the latest from /proc
if [ -z "$DEVICE" ]; then
    DEVICE_NAME=$(cat /proc/bus/input/devices | grep -A 10 "Thrustmaster" | grep "Handlers" | tail -1 | grep -o "event[0-9]*" | tail -1)
    if [ -n "$DEVICE_NAME" ]; then
        DEVICE="/dev/input/$DEVICE_NAME"
        if [ -e "$DEVICE" ]; then
            echo "✓ Found device: $DEVICE"
        else
            echo "⚠ Device $DEVICE doesn't exist (stale entry)"
            DEVICE=""
        fi
    fi
fi

# Manual entry if still not found
if [ -z "$DEVICE" ]; then
    echo "⚠ Device not found automatically"
    echo ""
    echo "Available devices:"
    cat /proc/bus/input/devices | grep -A 10 "Thrustmaster"
    echo ""
    read -p "Enter full device path (e.g., /dev/input/event262): " DEVICE
fi

echo ""
echo "=========================================="
echo "Device ready: $DEVICE"
echo "=========================================="
echo ""
echo "Now open a SECOND terminal and run:"
echo "  sudo dmesg -w | grep -i tmff"
echo ""
echo "Then come back here and press ENTER to run fftest..."
read -p "Press ENTER when ready..."

echo ""
echo "Running fftest..."
echo "Try effect 1 (constant force) and watch the other terminal!"
echo ""
fftest "$DEVICE"

echo ""
echo "Test complete!"

