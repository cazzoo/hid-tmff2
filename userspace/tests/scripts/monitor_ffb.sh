#\!/bin/bash

echo "=== T500RS FFB Event Monitor ==="
echo ""

# Find T500RS device (works without by-id symlinks)
DEVICE=""
for dev in /dev/input/event*; do 
    name=$(cat "/sys/class/input/$(basename $dev)/device/name" 2>/dev/null)
    if echo "$name" | grep -qi "t500\|thrust"; then
        DEVICE="$dev"
        echo "Found T500RS: $dev"
        echo "Device name: $name"
        break
    fi
done

if [ -z "$DEVICE" ]; then
    echo "❌ T500RS not found\!"
    echo ""
    echo "Checking USB:"
    lsusb | grep -i thrust
    echo ""
    echo "Available input devices:"
    for dev in /dev/input/event*; do 
        name=$(cat "/sys/class/input/$(basename $dev)/device/name" 2>/dev/null)
        [ -n "$name" ] && echo "  $dev: $name"
    done
    exit 1
fi

echo ""
echo "Monitoring FFB events on: $DEVICE"
echo "Start the game and drive around..."
echo "Press Ctrl+C to stop"
echo ""
echo "Looking for:"
echo "  - EV_FF events (force feedback)"
echo "  - FF_UPLOAD (effect upload)"
echo "  - FF_GAIN (gain control)"
echo "  - FF_ERASE (effect removal)"
echo ""

# Monitor with color highlighting if possible
if command -v grep --color=auto >/dev/null 2>&1; then
    sudo evtest "$DEVICE" | grep --color=auto -E "EV_FF|FF_|UPLOAD|ERASE|GAIN|type 21"
else
    sudo evtest "$DEVICE" | grep -E "EV_FF|FF_|UPLOAD|ERASE|GAIN|type 21"
fi
