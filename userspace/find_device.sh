#!/bin/bash
#
# Find the T500RS FFB virtual device
#

echo "=========================================="
echo "Finding T500RS FFB Device"
echo "=========================================="
echo ""

# Method 1: Check dmesg for recent device creation
echo "[Method 1] Checking dmesg..."
DEVICE=$(dmesg | grep "input.*T500RS" | tail -1 | grep -oP '/dev/input/event\d+')
if [ -n "$DEVICE" ]; then
    echo "✅ Found in dmesg: $DEVICE"
    echo ""
fi

# Method 2: Check /proc/bus/input/devices
echo "[Method 2] Checking /proc/bus/input/devices..."
echo ""

# Find all Thrustmaster devices
grep -A 10 "Thrustmaster" /proc/bus/input/devices | while read line; do
    if [[ $line == N:* ]]; then
        NAME=$(echo "$line" | cut -d'"' -f2)
        echo "Device: $NAME"
    elif [[ $line == H:* ]]; then
        HANDLERS=$(echo "$line" | grep -oP 'event\d+')
        if [ -n "$HANDLERS" ]; then
            echo "  Handler: /dev/input/$HANDLERS"
            
            # Check if it has force feedback
            if [ -e "/sys/class/input/$HANDLERS/device/capabilities/ff" ]; then
                FF=$(cat /sys/class/input/$HANDLERS/device/capabilities/ff)
                if [ "$FF" != "0" ]; then
                    echo "  ✅ HAS FORCE FEEDBACK! (ff=$FF)"
                    DEVICE="/dev/input/$HANDLERS"
                fi
            fi
        fi
        echo ""
    fi
done

# Method 3: List all event devices with FF capability
echo "[Method 3] Checking all event devices for FF capability..."
echo ""

for dev in /dev/input/event*; do
    # Get device name
    NAME=$(udevadm info --query=property --name=$dev 2>/dev/null | grep "^ID_MODEL=" | cut -d'=' -f2)
    
    # Check if it has FF capability
    EVENT=$(basename $dev)
    if [ -e "/sys/class/input/$EVENT/device/capabilities/ff" ]; then
        FF=$(cat /sys/class/input/$EVENT/device/capabilities/ff)
        if [ "$FF" != "0" ]; then
            # Get full name from device
            FULLNAME=$(cat /sys/class/input/$EVENT/device/name 2>/dev/null)
            echo "$dev - $FULLNAME (FF: $FF)"
            
            # Check if it's our T500RS FFB device
            if [[ "$FULLNAME" == *"T500RS"*"FFB"* ]]; then
                echo "  ✅ THIS IS THE T500RS FFB DEVICE!"
                DEVICE="$dev"
            fi
        fi
    fi
done

echo ""
echo "=========================================="
echo "RESULT"
echo "=========================================="
echo ""

if [ -n "$DEVICE" ]; then
    echo "✅ T500RS FFB Device: $DEVICE"
    echo ""
    echo "Test with:"
    echo "  sudo fftest $DEVICE"
    echo ""
else
    echo "❌ T500RS FFB device not found!"
    echo ""
    echo "Troubleshooting:"
    echo "1. Make sure the driver is running (sudo ./run.sh)"
    echo "2. Wait 2 seconds after starting"
    echo "3. Check dmesg: dmesg | tail -20"
    echo "4. Check if uinput module is loaded: lsmod | grep uinput"
    echo ""
fi

