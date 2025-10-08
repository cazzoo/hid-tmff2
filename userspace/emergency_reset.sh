#!/bin/bash
# Emergency reset for T500RS - stops all effects and resets the wheel

echo "🚨 Emergency Reset for T500RS"
echo "==============================="
echo ""

# Find the device
DEVICE=""
for dev in /dev/input/event*; do
    if [ -e "$dev" ]; then
        name=$(cat "/sys/class/input/$(basename $dev)/device/name" 2>/dev/null)
        if echo "$name" | grep -qi "t500\|force.*feedback.*wheel"; then
            DEVICE="$dev"
            break
        fi
    fi
done

if [ -z "$DEVICE" ]; then
    echo "❌ T500RS device not found"
    echo "Make sure the driver is running: sudo ./t500rs-ffb"
    exit 1
fi

echo "Found device: $DEVICE"
echo ""

# Check if we need sudo
if [ ! -w "$DEVICE" ]; then
    echo "⚠️  Need sudo permissions to access device"
    exec sudo "$0" "$@"
fi

echo "Stopping all force feedback effects..."

# Stop all effects by sending EV_FF events with value=0
for effect_id in {0..15}; do
    # Use Python to send the stop event
    python3 - <<EOF
import struct
import os

dev = os.open("$DEVICE", os.O_RDWR | os.O_NONBLOCK)
# EV_FF (0x15), effect_id, value=0 (stop)
event = struct.pack('llHHi', 0, 0, 0x15, $effect_id, 0)
os.write(dev, event)
os.close(dev)
EOF
done

echo "✅ All effects stopped"
echo ""
echo "The wheel should now be in its default state."
