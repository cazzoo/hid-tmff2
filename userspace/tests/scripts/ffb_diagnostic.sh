#\!/bin/bash

echo "=== T500RS FFB Diagnostic ==="
echo ""

# 1. Find T500RS device (works even without by-id symlinks)
echo "1. Finding T500RS Device:"
T500_DEVICE=""
T500_NAME=""
for dev in /dev/input/event*; do 
    name=$(cat "/sys/class/input/$(basename $dev)/device/name" 2>/dev/null)
    if echo "$name" | grep -qi "t500\|thrust"; then
        echo "   ✅ Found: $dev"
        echo "      Name: $name"
        T500_DEVICE="$dev"
        T500_NAME="$name"
        
        # Check if it has FF capabilities
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
        else
            echo "      ⚠️  No by-id symlink (this is OK)"
        fi
    fi
done

if [ -z "$T500_DEVICE" ]; then
    echo "   ❌ T500RS device NOT found\!"
    echo ""
    echo "   Checking USB connection:"
    if lsusb | grep -qi "044f.*thrustmaster\|044f.*b65e"; then
        echo "   ✅ T500RS detected on USB"
        echo "   ❌ But no input device found - driver may not be running"
    else
        echo "   ❌ T500RS NOT detected on USB"
        echo "      Check USB cable and power"
    fi
    exit 1
fi
echo ""

# 2. Check driver is running
echo "2. Driver Status:"
if pgrep -f t500rs-ffb > /dev/null; then
    DRIVER_PID=$(pgrep -f t500rs-ffb)
    echo "   ✅ Driver is running (PID: $DRIVER_PID)"
    
    # Check which device driver is using
    if [ -n "$DRIVER_PID" ]; then
        DRIVER_FDS=$(ls -la /proc/$DRIVER_PID/fd 2>/dev/null | grep "/dev/input/event" | awk '{print $NF}')
        if [ -n "$DRIVER_FDS" ]; then
            echo "      Driver using: $DRIVER_FDS"
        fi
    fi
else
    echo "   ❌ Driver is NOT running"
    echo "      Start with: sudo ~/Documents/hid-tmff2/userspace/t500rs-ffb-modular"
fi
echo ""

# 3. Check device permissions
echo "3. Device Permissions:"
ls -la "$T500_DEVICE"
OWNER=$(stat -c '%U' "$T500_DEVICE" 2>/dev/null)
GROUP=$(stat -c '%G' "$T500_DEVICE" 2>/dev/null)
echo "   Owner: $OWNER, Group: $GROUP"
if [ "$GROUP" = "input" ]; then
    echo "   ✅ Device in 'input' group (good for udev rules)"
fi
echo ""

# 4. Check FFB capabilities in detail
echo "4. FFB Capabilities:"
if command -v evtest >/dev/null 2>&1; then
    echo "   Testing with evtest..."
    
    # Test specific FF types
    for ff_type in FF_CONSTANT FF_PERIODIC FF_SPRING FF_DAMPER; do
        if timeout 1 sudo evtest "$T500_DEVICE" --query EV_FF $ff_type 2>/dev/null | grep -q "supported"; then
            echo "   ✅ $ff_type supported"
        else
            echo "   ❌ $ff_type NOT supported"
        fi
    done
else
    echo "   ⚠️  evtest not installed (optional)"
    echo "      Install with: sudo apt-get install evtest"
fi
echo ""

# 5. Check for active effects
echo "5. Active Effects:"
EFFECTS_DIR="/sys/class/input/$(basename $T500_DEVICE)/device"
if [ -d "$EFFECTS_DIR" ]; then
    # Count effect slots
    EFFECT_COUNT=$(ls -1 "$EFFECTS_DIR" 2>/dev/null | grep -c "^effect" || echo "0")
    echo "   Effect slots available: $EFFECT_COUNT"
fi
echo ""

# 6. Check Wine/Proton (if applicable)
if [ -n "$WINEPREFIX" ]; then
    echo "6. Wine/Proton Status:"
    echo "   WINEPREFIX: $WINEPREFIX"
    if [ -d "$WINEPREFIX/drive_c/windows/system32" ]; then
        echo "   ✅ Wine prefix exists"
        
        # Check for Thrustmaster drivers
        if [ -d "$WINEPREFIX/drive_c/Program Files/Thrustmaster" ] || \
           [ -d "$WINEPREFIX/drive_c/Program Files (x86)/Thrustmaster" ]; then
            echo "   ✅ Thrustmaster drivers installed in prefix"
        else
            echo "   ⚠️  Thrustmaster drivers NOT found in prefix"
            echo "      Install with: ./tools/install_windows_drivers.sh"
        fi
    else
        echo "   ❌ Wine prefix NOT found"
    fi
    echo ""
fi

# 7. Summary and recommendations
echo "=== Summary ==="
echo ""

if [ -z "$T500_DEVICE" ]; then
    echo "❌ CRITICAL: T500RS device not found"
    echo "   1. Check USB connection"
    echo "   2. Check device is powered on"
    echo "   3. Run: lsusb | grep -i thrust"
elif \! pgrep -f t500rs-ffb > /dev/null; then
    echo "❌ CRITICAL: Driver not running"
    echo "   Start driver: sudo ~/Documents/hid-tmff2/userspace/t500rs-ffb-modular"
else
    echo "✅ Device found: $T500_DEVICE"
    echo "✅ Driver running"
    echo ""
    echo "Ready to test\!"
fi

echo ""
echo "=== Testing Commands ==="
echo ""
echo "1. Test FFB manually:"
echo "   cd ~/Documents/hid-tmff2/userspace/tests"
echo "   make"
echo "   sudo ./c/test_all_effects"
echo ""
echo "2. Monitor FFB events from game:"
echo "   sudo evtest $T500_DEVICE | grep -E 'EV_FF|UPLOAD|GAIN'"
echo ""
echo "3. Interactive event monitor:"
echo "   sudo evtest $T500_DEVICE"
echo ""
echo "4. Use fftest (if installed):"
echo "   sudo fftest $T500_DEVICE"
echo ""

# Export device for other scripts
if [ -n "$T500_DEVICE" ]; then
    echo "=== Environment ==="
    echo "export T500_DEVICE=$T500_DEVICE"
    echo ""
fi
