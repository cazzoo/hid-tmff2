#!/bin/bash
# Find the active T500RS device

echo "=========================================="
echo "T500RS Active Device Finder"
echo "=========================================="
echo ""

# Method 1: Find by checking which event device actually exists and works
echo "Method 1: Checking active event devices..."
echo ""

FOUND=0
for event in /dev/input/event*; do
    if [ -e "$event" ]; then
        # Check if this is a Thrustmaster device
        INFO=$(udevadm info "$event" 2>/dev/null | grep -i "thrustmaster\|044f:b65e")
        if [ -n "$INFO" ]; then
            # Check if device is actually accessible
            if timeout 0.1 cat "$event" >/dev/null 2>&1; then
                echo "✓ Active T500RS found: $event"
                
                # Get device name
                NAME=$(udevadm info "$event" 2>/dev/null | grep "ID_MODEL=" | cut -d'=' -f2)
                echo "  Name: $NAME"
                
                # Get handlers
                HANDLERS=$(cat /proc/bus/input/devices | grep -B5 "$(basename $event)" | grep "Handlers" | awk '{for(i=2;i<=NF;i++) print $i}')
                echo "  Handlers: $HANDLERS"
                
                FOUND=1
                ACTIVE_EVENT="$event"
                break
            fi
        fi
    fi
done

if [ $FOUND -eq 0 ]; then
    echo "⚠ No active T500RS found using method 1"
    echo ""
fi

# Method 2: Find the most recent event device for T500RS
echo ""
echo "Method 2: Finding most recent T500RS device..."
echo ""

# Get all Thrustmaster entries from /proc/bus/input/devices
LATEST_EVENT=$(cat /proc/bus/input/devices | grep -A 10 "Thrustmaster" | grep "Handlers" | tail -1 | grep -o "event[0-9]*" | tail -1)

if [ -n "$LATEST_EVENT" ]; then
    echo "✓ Latest T500RS event: /dev/input/$LATEST_EVENT"
    
    # Verify it exists
    if [ -e "/dev/input/$LATEST_EVENT" ]; then
        echo "  Status: Device exists"
        ACTIVE_EVENT="/dev/input/$LATEST_EVENT"
    else
        echo "  Status: Device file missing (stale entry)"
    fi
else
    echo "⚠ No T500RS found in /proc/bus/input/devices"
fi

# Method 3: Use lsusb to verify device is actually connected
echo ""
echo "Method 3: USB verification..."
echo ""

USB_INFO=$(lsusb | grep -i "044f:b65e\|044f:b65d")
if [ -n "$USB_INFO" ]; then
    echo "✓ T500RS connected via USB:"
    echo "  $USB_INFO"
else
    echo "⚠ T500RS not found on USB bus"
fi

# Summary
echo ""
echo "=========================================="
echo "Summary"
echo "=========================================="
echo ""

if [ -n "$ACTIVE_EVENT" ] && [ -e "$ACTIVE_EVENT" ]; then
    echo "✓✓✓ Active T500RS Device: $ACTIVE_EVENT"
    echo ""
    echo "Use this device for testing:"
    echo "  fftest $ACTIVE_EVENT"
    echo ""
    
    # Check if it has force feedback
    if [ -e "$ACTIVE_EVENT" ]; then
        FF_EFFECTS=$(cat /sys/class/input/$(basename $ACTIVE_EVENT)/device/device/capabilities/ff 2>/dev/null)
        if [ -n "$FF_EFFECTS" ] && [ "$FF_EFFECTS" != "0" ]; then
            echo "✓ Force feedback capabilities detected"
        else
            echo "⚠ No force feedback capabilities"
        fi
    fi
else
    echo "✗ No active T500RS device found"
    echo ""
    echo "Troubleshooting:"
    echo "1. Check if device is plugged in: lsusb | grep -i thrustmaster"
    echo "2. Check if driver is loaded: lsmod | grep tmff"
    echo "3. Check kernel messages: dmesg | tail -20 | grep -i tmff"
fi

echo ""

