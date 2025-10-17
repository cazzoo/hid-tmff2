#\!/bin/bash

echo "=== T500RS FFB Diagnostic ==="
echo ""

# 1. Find ALL T500RS-related devices
echo "1. Finding T500RS Devices:"
echo ""
HARDWARE_DEVICE=""
UINPUT_DEVICE=""

for dev in /dev/input/event*; do 
    name=$(cat "/sys/class/input/$(basename $dev)/device/name" 2>/dev/null)
    if echo "$name" | grep -qi "t500\|thrust"; then
        # Check if it's hardware or uinput
        if echo "$name" | grep -qi "force feedback wheel"; then
            echo "   📱 UINPUT Device (created by driver):"
            UINPUT_DEVICE="$dev"
        else
            echo "   🔌 HARDWARE Device (real T500RS):"
            HARDWARE_DEVICE="$dev"
        fi
        
        echo "      Path: $dev"
        echo "      Name: $name"
        
        # Check FF capabilities
        FF_CAP=$(cat "/sys/class/input/$(basename $dev)/device/capabilities/ff" 2>/dev/null)
        if [ -n "$FF_CAP" ] && [ "$FF_CAP" \!= "0" ]; then
            echo "      ✅ Has FF capabilities: 0x$FF_CAP"
        else
            echo "      ❌ No FF capabilities"
        fi
        
        # Check by-id symlink
        BY_ID=$(ls -la /dev/input/by-id/ 2>/dev/null | grep "$(basename $dev)" | awk '{print $9}')
        if [ -n "$BY_ID" ]; then
            echo "      by-id: $BY_ID"
        fi
        echo ""
    fi
done

if [ -z "$HARDWARE_DEVICE" ] && [ -z "$UINPUT_DEVICE" ]; then
    echo "   ❌ No T500RS devices found\!"
    echo ""
    echo "   Checking USB connection:"
    if lsusb | grep -qi "044f.*thrustmaster\|044f.*b65e"; then
        echo "   ✅ T500RS detected on USB"
        echo "   ❌ But no input devices found - driver may not be running"
    else
        echo "   ❌ T500RS NOT detected on USB"
        echo "      Check USB cable and power"
    fi
    exit 1
fi

# 2. Check driver is running
echo "2. Driver Status:"
if pgrep -f t500rs-ffb > /dev/null; then
    DRIVER_PID=$(pgrep -f t500rs-ffb)
    echo "   ✅ Driver is running (PID: $DRIVER_PID)"
    
    # Check which devices driver is using
    if [ -n "$DRIVER_PID" ]; then
        echo "   Driver file descriptors:"
        ls -la /proc/$DRIVER_PID/fd 2>/dev/null | grep "/dev/input/event" | while read line; do
            echo "      $line"
        done
    fi
else
    echo "   ❌ Driver is NOT running"
    echo "      Start with: sudo ~/Documents/hid-tmff2/userspace/t500rs-ffb-modular"
fi
echo ""

# 3. Explain the two devices
echo "3. Device Explanation:"
echo ""
if [ -n "$HARDWARE_DEVICE" ]; then
    echo "   🔌 HARDWARE Device: $HARDWARE_DEVICE"
    echo "      This is the REAL T500RS hardware"
    echo "      Used by: Driver (reads input, sends FF to wheel)"
    echo "      Games should NOT use this directly"
    echo ""
fi

if [ -n "$UINPUT_DEVICE" ]; then
    echo "   📱 UINPUT Device: $UINPUT_DEVICE"
    echo "      This is the VIRTUAL device created by driver"
    echo "      Used by: Games (send FF commands here)"
    echo "      Driver forwards FF to hardware device"
    echo ""
else
    echo "   ❌ UINPUT device NOT found\!"
    echo "      Driver may not be running or failed to create device"
    echo ""
fi

# 4. Check FFB capabilities in detail
echo "4. FFB Capabilities Check:"
echo ""

if [ -n "$UINPUT_DEVICE" ]; then
    echo "   Testing UINPUT device (what games use):"
    if command -v evtest >/dev/null 2>&1; then
        for ff_type in FF_CONSTANT FF_PERIODIC FF_SPRING FF_DAMPER; do
            if timeout 1 sudo evtest "$UINPUT_DEVICE" --query EV_FF $ff_type 2>/dev/null | grep -q "supported"; then
                echo "      ✅ $ff_type supported"
            else
                echo "      ❌ $ff_type NOT supported"
            fi
        done
    else
        echo "      ⚠️  evtest not installed"
    fi
    echo ""
fi

if [ -n "$HARDWARE_DEVICE" ]; then
    echo "   Testing HARDWARE device (for reference):"
    if command -v evtest >/dev/null 2>&1; then
        for ff_type in FF_CONSTANT FF_PERIODIC FF_SPRING FF_DAMPER; do
            if timeout 1 sudo evtest "$HARDWARE_DEVICE" --query EV_FF $ff_type 2>/dev/null | grep -q "supported"; then
                echo "      ✅ $ff_type supported"
            else
                echo "      ❌ $ff_type NOT supported"
            fi
        done
    fi
    echo ""
fi

# 5. Summary and recommendations
echo "=== Summary ==="
echo ""

if [ -z "$HARDWARE_DEVICE" ]; then
    echo "❌ CRITICAL: Hardware device not found"
    echo "   Check USB connection and power"
elif [ -z "$UINPUT_DEVICE" ]; then
    echo "❌ CRITICAL: Uinput device not found"
    echo "   Driver not running or failed to create virtual device"
    echo "   Start driver: sudo ~/Documents/hid-tmff2/userspace/t500rs-ffb-modular"
else
    echo "✅ Hardware device: $HARDWARE_DEVICE"
    echo "✅ Uinput device: $UINPUT_DEVICE"
    echo ""
    echo "🎮 IMPORTANT: Games should use the UINPUT device\!"
    echo "   Wine/Proton games should see: $UINPUT_DEVICE"
    echo "   NOT the hardware device: $HARDWARE_DEVICE"
fi

echo ""
echo "=== Testing Commands ==="
echo ""
echo "1. Test driver works (using uinput device):"
echo "   cd ~/Documents/hid-tmff2/userspace/tests"
echo "   make"
if [ -n "$UINPUT_DEVICE" ]; then
    echo "   sudo fftest $UINPUT_DEVICE"
fi
echo ""
echo "2. Monitor game FFB events:"
if [ -n "$UINPUT_DEVICE" ]; then
    echo "   sudo evtest $UINPUT_DEVICE | grep -E 'EV_FF|UPLOAD|GAIN'"
fi
echo ""
echo "3. Test hardware directly (bypass driver):"
if [ -n "$HARDWARE_DEVICE" ]; then
    echo "   sudo fftest $HARDWARE_DEVICE"
fi
echo ""

# Export devices for other scripts
if [ -n "$UINPUT_DEVICE" ]; then
    echo "=== Environment ==="
    echo "export T500_UINPUT=$UINPUT_DEVICE"
    [ -n "$HARDWARE_DEVICE" ] && echo "export T500_HARDWARE=$HARDWARE_DEVICE"
    echo ""
fi
