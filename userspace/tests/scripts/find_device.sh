#!/bin/bash
# Find T500RS Force Feedback device

echo "Searching for T500RS Force Feedback Wheel..."
echo ""

for dev in /dev/input/event*; do
    if [ -e "$dev" ]; then
        name=$(cat "/sys/class/input/$(basename $dev)/device/name" 2>/dev/null)
        if echo "$name" | grep -qi "t500\|force.*feedback.*wheel"; then
            echo "✅ Found: $dev"
            echo "   Name: $name"
            echo ""
            echo "$dev"
            exit 0
        fi
    fi
done

echo "❌ T500RS device not found"
echo ""
echo "Make sure:"
echo "  1. The wheel is plugged in"
echo "  2. The driver is running: sudo ./t500rs-ffb"
echo "  3. Check: lsusb | grep -i thrust"
echo ""
exit 1
