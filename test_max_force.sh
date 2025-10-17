#!/bin/bash
# Test T500RS with MAXIMUM force

DEVICE=$(cat /proc/bus/input/devices | grep -A5 "TRS Racing" | grep Handlers | tail -1 | awk '{print $NF}')
DEVICE="/dev/input/$DEVICE"

echo "🎯 Testing T500RS with MAXIMUM FORCE"
echo "====================================="
echo "Device: $DEVICE"
echo ""
echo "⚠️  WARNING: This will apply MAXIMUM force!"
echo "⚠️  HOLD THE WHEEL FIRMLY!"
echo ""
echo "Testing for 3 seconds..."
echo ""

# Use ffcfstress to apply constant force at maximum level
sudo ffcfstress -d "$DEVICE" -m 32767 -t 3000

echo ""
echo "Test complete. Did you feel ANY force?"

