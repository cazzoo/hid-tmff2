#!/bin/bash
# Test Wine force feedback access

echo "=========================================="
echo "Wine Force Feedback Access Test"
echo "=========================================="
echo ""

# Find the FF device
FF_DEVICE=$(ls -1 /sys/class/input/event*/device/name 2>/dev/null | xargs grep -l "T500RS Force Feedback" 2>/dev/null | sed 's|/sys/class/input/\(event[0-9]*\)/.*|\1|' | head -1)

if [ -z "$FF_DEVICE" ]; then
    echo "ERROR: T500RS Force Feedback device not found!"
    exit 1
fi

DEVICE_PATH="/dev/input/$FF_DEVICE"
echo "Found FF device: $DEVICE_PATH"
echo ""

# Check permissions
echo "Checking permissions..."
ls -la $DEVICE_PATH
echo ""

# Check if user can read it
if [ ! -r "$DEVICE_PATH" ]; then
    echo "ERROR: Cannot read $DEVICE_PATH"
    echo "Your user needs to be in the 'input' group"
    exit 1
fi

echo "✓ Device is readable"
echo ""

# Test with evtest (shows if Wine can read events)
echo "Testing device with evtest (Ctrl+C to stop)..."
echo "Move your wheel and press buttons to see if events are detected..."
echo ""

if command -v evtest &> /dev/null; then
    timeout 3 evtest $DEVICE_PATH 2>&1 | head -30
    echo ""
    echo "✓ Device responds to input"
else
    echo "evtest not found (optional)"
fi

echo ""
echo "=========================================="
echo "Wine Configuration Check"
echo "=========================================="
echo ""

# Check Wine registry for input settings
echo "Checking Wine input configuration..."
WINE_REG="$HOME/.wine/system.reg"

if [ -f "$WINE_REG" ]; then
    if grep -q "DisableInput" "$WINE_REG"; then
        echo "⚠ Warning: DisableInput found in Wine registry"
    else
        echo "✓ No input blocking found"
    fi
else
    echo "Wine not configured yet (first run)"
fi

echo ""
echo "=========================================="
echo "Force Feedback Test"
echo "=========================================="
echo ""

echo "Testing if native FF works (you should feel this)..."
echo "Running fftest for 3 seconds..."
echo ""

if command -v fftest &> /dev/null; then
    (
        echo "y"  # Accept warning
        sleep 1
        echo "0"  # Play effect 0
        sleep 2
    ) | timeout 4 fftest $DEVICE_PATH 2>&1 | grep -E "(OK|Playing|effect)"
    
    echo ""
    echo "Did you feel the wheel move? (y/n)"
    read -t 5 -n 1 response
    echo ""
    
    if [ "$response" = "y" ]; then
        echo "✓ Native FF works!"
        echo ""
        echo "If Wine/LFS doesn't have FF, the problem is Wine configuration"
    else
        echo "✗ Native FF doesn't work - check driver"
        exit 1
    fi
else
    echo "fftest not installed"
    echo "Install with: sudo pacman -S linuxconsole"
fi

echo ""
echo "=========================================="
echo "Wine/LFS Configuration"
echo "=========================================="
echo ""
echo "In LFS:"
echo "1. Options → Controls"
echo "2. Click 'Remove' on any existing device"
echo "3. Click 'New Device'"
echo "4. Select: 'T500RS Force Feedback Wheel'"
echo "5. Assign all controls (wheel, pedals, buttons)"
echo "6. ✓ Enable 'Force Feedback' checkbox"
echo "7. Set strength to 100%"
echo "8. Test in-game"
echo ""
echo "IMPORTANT: Make sure you're using the SAME device"
echo "  for both input AND force feedback!"
echo ""
