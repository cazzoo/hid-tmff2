#!/bin/bash
# Disable USB autosuspend for T500RS to prevent power management crashes

echo "Disabling USB autosuspend for T500RS..."

# Find the T500RS USB device
DEVICE=$(lsusb | grep "044f:b65e" | awk '{print $2"/"$4}' | sed 's/://g')

if [ -z "$DEVICE" ]; then
    echo "❌ T500RS not found (044f:b65e)"
    exit 1
fi

echo "Found T500RS at: $DEVICE"

# Disable autosuspend
AUTOSUSPEND_PATH="/sys/bus/usb/devices/$DEVICE/power/autosuspend"
CONTROL_PATH="/sys/bus/usb/devices/$DEVICE/power/control"

if [ -f "$CONTROL_PATH" ]; then
    echo "on" | sudo tee "$CONTROL_PATH" > /dev/null
    echo "✅ USB autosuspend disabled"
else
    echo "❌ Could not find power control path"
    exit 1
fi

echo "✅ T500RS USB power management disabled"

