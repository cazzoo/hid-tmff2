#!/bin/bash
# Automated T500RS USB Traffic Capture using tshark
# Captures Windows driver traffic via VM passthrough

echo "=========================================="
echo "T500RS USB Traffic Capture"
echo "=========================================="
echo ""
echo "This script captures USB traffic from the"
echo "WINDOWS driver (where force feedback works!)"
echo ""

if [ "$EUID" -ne 0 ]; then
    echo "This script needs sudo access."
    echo "Please run: sudo ./capture_t500rs_usb.sh"
    exit 1
fi

# Check for Windows VM mode
WINDOWS_MODE=0
if [[ "$1" == "--windows-vm" ]] || [[ "$1" == "-w" ]]; then
    WINDOWS_MODE=1
fi

# Check if tshark is installed
if ! command -v tshark &> /dev/null; then
    echo "tshark is not installed."
    echo ""
    echo "Install with:"
    echo "  sudo pacman -S wireshark-cli"
    echo ""
    read -p "Install now? (y/n): " INSTALL
    if [[ "$INSTALL" =~ ^[Yy]$ ]]; then
        pacman -S wireshark-cli
    else
        exit 1
    fi
fi

# Load usbmon module
echo "Step 1: Loading usbmon module..."
modprobe usbmon
if [ $? -ne 0 ]; then
    echo "✗ Failed to load usbmon"
    exit 1
fi
echo "✓ usbmon loaded"

# Make usbmon accessible
chmod 644 /dev/usbmon* 2>/dev/null

echo ""
echo "Step 2: Finding T500RS USB bus..."

# Find T500RS
USB_INFO=$(lsusb | grep -i "044f:b65e\|044f:b65d")
if [ -z "$USB_INFO" ]; then
    echo "✗ T500RS not found on USB bus"
    echo ""
    echo "Please ensure T500RS is plugged in and run again."
    exit 1
fi

echo "✓ Found: $USB_INFO"

# Extract bus and device number
BUS=$(echo "$USB_INFO" | awk '{print $2}')
DEVICE=$(echo "$USB_INFO" | awk '{print $4}' | tr -d ':')

echo "  Bus: $BUS"
echo "  Device: $DEVICE"

# Determine usbmon interface (remove leading zeros from bus number)
BUS_NUM=$(echo $BUS | sed 's/^0*//')
USBMON_IF="usbmon${BUS_NUM}"

echo ""
echo "Step 3: Verifying capture setup..."

# Check if usbmon interface exists
if ! ip link show "$USBMON_IF" >/dev/null 2>&1; then
    # Try alternative check
    if ! ls /sys/class/net/$USBMON_IF >/dev/null 2>&1; then
        echo "⚠ Warning: Cannot verify $USBMON_IF interface"
        echo "Proceeding anyway..."
    fi
fi

# Test tshark can access the interface
echo "Testing tshark access..."
TEST_OUTPUT=$(timeout 2 tshark -i "$USBMON_IF" -c 1 2>&1)
TEST_RESULT=$?

if [ $TEST_RESULT -eq 124 ] || [ $TEST_RESULT -eq 0 ]; then
    echo "✓ tshark can access $USBMON_IF"
elif echo "$TEST_OUTPUT" | grep -q "permission denied\|Operation not permitted"; then
    echo "✗ Permission denied for $USBMON_IF"
    echo ""
    echo "Fixing permissions..."
    chmod 644 /dev/usbmon* 2>/dev/null

    # Try again
    timeout 2 tshark -i "$USBMON_IF" -c 1 >/dev/null 2>&1
    if [ $? -eq 124 ] || [ $? -eq 0 ]; then
        echo "✓ Permissions fixed, tshark can now access $USBMON_IF"
    else
        echo "✗ Still cannot access $USBMON_IF"
        echo ""
        echo "Try adding yourself to wireshark group:"
        echo "  sudo usermod -a -G wireshark $SUDO_USER"
        echo "  Then log out and back in"
        exit 1
    fi
else
    echo "✗ tshark cannot access $USBMON_IF"
    echo "Error: $TEST_OUTPUT"
    echo ""
    echo "Available interfaces:"
    tshark -D 2>/dev/null | grep usbmon
    exit 1
fi

echo ""
echo "Capture configuration:"
echo "  Interface: $USBMON_IF"
echo "  Bus: $BUS"
echo "  Device: $DEVICE"
echo ""

# Create captures directory
mkdir -p captures
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

if [ $WINDOWS_MODE -eq 1 ]; then
    echo "=========================================="
    echo "Windows VM Capture Mode"
    echo "=========================================="
    echo ""
    echo "This will capture USB traffic from Windows driver."
    echo ""
    echo "Prerequisites:"
    echo "  - Windows VM installed (VirtualBox/virt-manager)"
    echo "  - Thrustmaster drivers installed in Windows"
    echo "  - USB passthrough configured for T500RS"
    echo ""
    echo "Instructions:"
    echo "  1. Press ENTER to start capture"
    echo "  2. Start your Windows VM"
    echo "  3. Pass T500RS USB to the VM"
    echo "  4. In Windows: Test force feedback"
    echo "     - Open Thrustmaster Control Panel"
    echo "     - OR use fedit.exe"
    echo "     - OR play a game with force feedback"
    echo "  5. Test for 1-2 minutes"
    echo "  6. Press ENTER here to stop capture"
    echo ""
    read -p "Press ENTER when ready to start capture..."

    WINDOWS_FILE="captures/t500rs_windows_${TIMESTAMP}.pcapng"

    echo ""
    echo "=========================================="
    echo "Capture Started!"
    echo "=========================================="
    echo ""
    echo "Capturing to: $WINDOWS_FILE"
    echo ""
    echo "NOW:"
    echo "  1. Start your Windows VM"
    echo "  2. Pass T500RS USB device to VM"
    echo "  3. Test force feedback in Windows"
    echo "  4. Try different effects"
    echo ""
    echo "Press ENTER when done testing..."
    echo ""

    # Start capture in background (capture ALL USB traffic on this bus)
    # We'll filter later since device address may change when passed to VM
    tshark -i "$USBMON_IF" -w "$WINDOWS_FILE" 2>/dev/null &
    CAPTURE_PID=$!

    echo "Capture PID: $CAPTURE_PID"
    sleep 2

    # Verify capture is running
    if ! ps -p $CAPTURE_PID > /dev/null 2>&1; then
        echo "✗ Capture failed to start"
        echo "Checking tshark errors..."
        tshark -i "$USBMON_IF" -w /tmp/test.pcapng 2>&1 | head -10
        exit 1
    fi
    echo "✓ Capture running"

    # Wait for user to finish testing
    read -p ""

    # Stop capture
    echo "Stopping capture..."
    kill -INT $CAPTURE_PID 2>/dev/null
    sleep 2

    # Force kill if still running
    if ps -p $CAPTURE_PID > /dev/null 2>&1; then
        kill -9 $CAPTURE_PID 2>/dev/null
    fi

    wait $CAPTURE_PID 2>/dev/null

    echo ""
    echo "✓ Capture stopped"

    # Check if file was created
    if [ -f "$WINDOWS_FILE" ]; then
        SIZE=$(stat -c%s "$WINDOWS_FILE" 2>/dev/null || stat -f%z "$WINDOWS_FILE" 2>/dev/null)

        if [ "$SIZE" -gt 100 ]; then
            echo "✓ Windows capture saved: $WINDOWS_FILE ($SIZE bytes)"
        else
            echo "⚠ Capture file is too small ($SIZE bytes)"
            echo "This usually means no USB traffic was captured."
            echo ""
            echo "Possible issues:"
            echo "  1. T500RS was not passed to Windows VM"
            echo "  2. No force feedback was tested in Windows"
            echo "  3. Wrong USB bus monitored"
            echo ""
            echo "Try again and make sure to:"
            echo "  - Pass T500RS USB to VM"
            echo "  - Test force feedback in Windows"
            exit 1
        fi

        echo ""
        echo "=========================================="
        echo "Quick Analysis"
        echo "=========================================="
        echo ""

        # Analyze the capture
        ./analyze_capture.sh "$WINDOWS_FILE"
        chmod 777 "$WINDOWS_FILE"
        chown $USER:$USER "$WINDOWS_FILE"
    else
        echo "⚠ Capture file not created"
    fi

    exit 0
fi

echo "=========================================="
echo "Linux Testing Mode"
echo "=========================================="
echo ""
echo "NOTE: This captures Linux driver traffic."
echo "For REAL protocol, use: sudo ./capture_t500rs_usb.sh --windows-vm"
echo ""
echo "We'll capture two scenarios:"
echo "  1. Initialization (device plug-in)"
echo "  2. Force feedback test (Linux driver)"
echo ""

# Scenario 1: Initialization
echo "=========================================="
echo "Scenario 1: Initialization Capture"
echo "=========================================="
echo ""
echo "This will capture the device initialization sequence."
echo ""
echo "Instructions:"
echo "  1. Press ENTER to start capture"
echo "  2. Unplug the T500RS"
echo "  3. Wait 3 seconds"
echo "  4. Plug it back in"
echo "  5. Wait for driver to initialize (5 seconds)"
echo "  6. Capture will stop automatically"
echo ""
read -p "Press ENTER to start initialization capture..."

INIT_FILE="captures/t500rs_init_${TIMESTAMP}.pcapng"

echo ""
echo "Starting capture... (will run for 15 seconds)"
echo "NOW: Unplug the device, wait 3 seconds, then plug it back in!"
echo ""

# Capture for 15 seconds
timeout 15 tshark -i "$USBMON_IF" -f "usb" -w "$INIT_FILE" \
    -Y "usb.device_address == $DEVICE" 2>/dev/null &
CAPTURE_PID=$!

# Wait for capture to complete
wait $CAPTURE_PID 2>/dev/null

if [ -f "$INIT_FILE" ]; then
    SIZE=$(stat -f%z "$INIT_FILE" 2>/dev/null || stat -c%s "$INIT_FILE" 2>/dev/null)
    echo "✓ Initialization capture saved: $INIT_FILE ($SIZE bytes)"
else
    echo "⚠ Capture file not created"
fi

echo ""
echo "Waiting 5 seconds before next capture..."
sleep 5

# Re-detect device (it may have changed address)
USB_INFO=$(lsusb | grep -i "044f:b65e")
if [ -n "$USB_INFO" ]; then
    DEVICE=$(echo "$USB_INFO" | awk '{print $4}' | tr -d ':')
    echo "✓ Device reconnected at address: $DEVICE"
fi

# Scenario 2: Force Feedback Test
echo ""
echo "=========================================="
echo "Scenario 2: Force Feedback Capture"
echo "=========================================="
echo ""
echo "This will capture force feedback commands."
echo ""
echo "Instructions:"
echo "  1. Press ENTER to start capture"
echo "  2. Capture will run for 30 seconds"
echo "  3. Use fftest or our test script to play effects"
echo "  4. Try constant force and spring effects"
echo ""
read -p "Press ENTER to start force feedback capture..."

FF_FILE="captures/t500rs_ff_${TIMESTAMP}.pcapng"

echo ""
echo "Starting capture... (will run for 30 seconds)"
echo "NOW: Run fftest and play some effects!"
echo ""
echo "In another terminal, run:"
echo "  ./find_t500rs.sh  # to find device"
echo "  fftest /dev/input/eventXXX  # to test effects"
echo ""

# Capture for 30 seconds
timeout 30 tshark -i "$USBMON_IF" -f "usb" -w "$FF_FILE" \
    -Y "usb.device_address == $DEVICE" 2>/dev/null &
CAPTURE_PID=$!

# Show countdown
for i in {30..1}; do
    echo -ne "\rCapturing... $i seconds remaining  "
    sleep 1
done
echo ""

wait $CAPTURE_PID 2>/dev/null

if [ -f "$FF_FILE" ]; then
    SIZE=$(stat -f%z "$FF_FILE" 2>/dev/null || stat -c%s "$FF_FILE" 2>/dev/null)
    echo "✓ Force feedback capture saved: $FF_FILE ($SIZE bytes)"
else
    echo "⚠ Capture file not created"
fi

# Summary
echo ""
echo "=========================================="
echo "Capture Complete!"
echo "=========================================="
echo ""
echo "Captured files:"
ls -lh captures/t500rs_*_${TIMESTAMP}.pcapng 2>/dev/null

echo ""
echo "=========================================="
echo "Quick Analysis"
echo "=========================================="
echo ""

if [ -f "$FF_FILE" ]; then
    echo "Analyzing force feedback capture..."
    echo ""
    
    # Count packets
    TOTAL_PACKETS=$(tshark -r "$FF_FILE" 2>/dev/null | wc -l)
    echo "Total packets: $TOTAL_PACKETS"
    
    # Look for SET_REPORT (0x09)
    SET_REPORT=$(tshark -r "$FF_FILE" -Y "usb.setup.bRequest == 0x09" 2>/dev/null | wc -l)
    echo "SET_REPORT packets: $SET_REPORT"
    
    # Look for URB_INTERRUPT out
    INTERRUPT_OUT=$(tshark -r "$FF_FILE" -Y "usb.endpoint_address.direction == 0" 2>/dev/null | wc -l)
    echo "Outgoing packets: $INTERRUPT_OUT"
    
    echo ""
    echo "Extracting SET_REPORT commands..."
    tshark -r "$FF_FILE" -Y "usb.setup.bRequest == 0x09" \
        -T fields -e frame.number -e usb.capdata 2>/dev/null | head -20
fi

echo ""
echo "=========================================="
echo "Next Steps"
echo "=========================================="
echo ""
echo "1. Review captures with:"
echo "   tshark -r $FF_FILE -V | less"
echo ""
echo "2. Filter for SET_REPORT:"
echo "   tshark -r $FF_FILE -Y 'usb.setup.bRequest == 0x09'"
echo ""
echo "3. Extract data:"
echo "   tshark -r $FF_FILE -Y 'usb.setup.bRequest == 0x09' -T fields -e usb.capdata"
echo ""
echo "4. Open in Wireshark for detailed analysis:"
echo "   wireshark $FF_FILE"
echo ""
echo "Files saved in: ./captures/"
echo ""

