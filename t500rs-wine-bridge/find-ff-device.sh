#!/bin/bash
# Find the correct T500RS device for force feedback in Wine

echo "=========================================="
echo "T500RS Device Finder for Wine/LFS"
echo "=========================================="
echo ""

echo "Searching for T500RS devices..."
echo ""

# Find all T500RS related event devices
T500RS_DEVICES=$(ls -1 /sys/class/input/event*/device/name 2>/dev/null | xargs grep -l "T500RS" 2>/dev/null | sed 's|/sys/class/input/\(event[0-9]*\)/.*|\1|')

if [ -z "$T500RS_DEVICES" ]; then
    echo "ERROR: No T500RS devices found!"
    echo "Make sure the driver is running."
    exit 1
fi

echo "Found T500RS devices:"
echo ""

for EVENT in $T500RS_DEVICES; do
    NAME=$(cat /sys/class/input/$EVENT/device/name 2>/dev/null)
    DEVICE="/dev/input/$EVENT"
    
    echo "Device: $DEVICE"
    echo "  Name: $NAME"
    
    # Check for force feedback capability
    if [ -e "/sys/class/input/$EVENT/device/capabilities/ff" ]; then
        FF_CAPS=$(cat /sys/class/input/$EVENT/device/capabilities/ff)
        if [ "$FF_CAPS" != "0" ]; then
            echo "  Force Feedback: YES ✓"
            echo "  FF Capabilities: 0x$FF_CAPS"
            echo ""
            echo "  ** USE THIS DEVICE IN LFS FOR FORCE FEEDBACK **"
        else
            echo "  Force Feedback: NO (Input only)"
        fi
    else
        echo "  Force Feedback: NO (Input only)"
    fi
    
    echo ""
done

echo "=========================================="
echo "How to configure LFS:"
echo "=========================================="
echo ""
echo "1. In LFS, go to: Options → Controls"
echo "2. Select the device with FF capability (marked ✓ above)"
echo "3. Map your controls (steering, pedals, buttons)"
echo "4. Enable Force Feedback checkbox"
echo "5. Test - you should feel forces!"
echo ""
echo "IMPORTANT:"
echo "- Use 'T500RS Force Feedback Wheel' for both input AND force feedback"
echo "- Do NOT use 'T500RS Wine Bridge' for force feedback (input only)"
echo ""

# Test force feedback if requested
if [ "$1" == "--test" ]; then
    echo "=========================================="
    echo "Force Feedback Test"
    echo "=========================================="
    echo ""
    
    # Find the FF device
    for EVENT in $T500RS_DEVICES; do
        DEVICE="/dev/input/$EVENT"
        FF_CAPS=$(cat /sys/class/input/$EVENT/device/capabilities/ff 2>/dev/null || echo "0")
        
        if [ "$FF_CAPS" != "0" ]; then
            echo "Testing force feedback on $DEVICE..."
            echo ""
            
            # Check if fftest is available
            if command -v fftest &> /dev/null; then
                echo "Running fftest (press Ctrl+C to stop)..."
                sudo fftest $DEVICE
            else
                echo "fftest not found. Install with: sudo pacman -S linuxconsole"
                echo ""
                echo "You can manually test with:"
                echo "  sudo fftest $DEVICE"
            fi
            break
        fi
    done
fi
