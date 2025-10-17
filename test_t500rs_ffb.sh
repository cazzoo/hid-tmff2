#!/bin/bash
# Test T500RS force feedback

# Auto-detect device
DEVICE=$(cat /proc/bus/input/devices | grep -A5 "TRS Racing" | grep "Handlers.*event" | tail -1 | grep -o "event[0-9]*" | head -1)
DEVICE="/dev/input/$DEVICE"

echo "🎯 Testing T500RS Force Feedback"
echo "=================================="
echo "Device: $DEVICE"
echo ""
echo "This will play a constant force effect for 5 seconds."
echo "You should FEEL the wheel push left or right!"
echo ""
echo "Press Ctrl+C to stop."
echo ""

# Play constant force effect (effect 1) for 5 seconds
(echo "1"; sleep 5; echo "-1") | sudo fftest "$DEVICE"

