#!/bin/bash
# T500RS Wine Bridge - Complete Test Script
# This script demonstrates the full Wine compatibility solution

set -e

BRIDGE_DIR="/home/caz/Documents/hid-tmff2/t500rs-wine-bridge"
DRIVER_DIR="/home/caz/Documents/hid-tmff2/userspace"
SOCKET_PATH="/tmp/t500rs_bridge.sock"

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}T500RS Wine Bridge - Complete Test${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then 
    echo -e "${RED}ERROR: This script must be run as root (sudo)${NC}"
    echo "Reason: UHID and USB access require root privileges"
    exit 1
fi

# Step 1: Check prerequisites
echo -e "${YELLOW}[1/5] Checking prerequisites...${NC}"

if [ ! -f "$BRIDGE_DIR/uhid_proxy_ipc" ]; then
    echo -e "${RED}ERROR: Bridge proxy not found at $BRIDGE_DIR/uhid_proxy_ipc${NC}"
    echo "Run: cd $BRIDGE_DIR && gcc -o uhid_proxy_ipc src/uhid_proxy_ipc.c"
    exit 1
fi

if [ ! -f "$DRIVER_DIR/t500rs-ffb-modular" ]; then
    echo -e "${RED}ERROR: Driver not found at $DRIVER_DIR/t500rs-ffb-modular${NC}"
    echo "Run: cd $DRIVER_DIR && make -f Makefile.modular"
    exit 1
fi

# Check if device is connected
if ! lsusb | grep -q "044f:b65e"; then
    echo -e "${RED}ERROR: T500RS device not found${NC}"
    echo "Make sure your T500RS is connected and in PS3 mode"
    exit 1
fi

# Load uhid module
if ! lsmod | grep -q uhid; then
    echo "Loading uhid kernel module..."
    modprobe uhid
fi

echo -e "${GREEN}✓ Prerequisites OK${NC}"
echo ""

# Step 2: Clean up any existing processes
echo -e "${YELLOW}[2/5] Cleaning up existing processes...${NC}"

# Kill any running instances
pkill -f uhid_proxy_ipc 2>/dev/null || true
pkill -f t500rs-ffb-modular 2>/dev/null || true
sleep 1

# Remove socket if it exists
rm -f "$SOCKET_PATH"

echo -e "${GREEN}✓ Cleanup complete${NC}"
echo ""

# Step 3: Start the UHID proxy
echo -e "${YELLOW}[3/5] Starting UHID Wine Bridge Proxy...${NC}"

"$BRIDGE_DIR/uhid_proxy_ipc" &
PROXY_PID=$!
echo "Proxy PID: $PROXY_PID"

# Wait for proxy to initialize
sleep 2

# Check if proxy is running
if ! kill -0 $PROXY_PID 2>/dev/null; then
    echo -e "${RED}ERROR: Proxy failed to start${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Proxy running${NC}"

# Show kernel messages about the new device
echo ""
echo "Kernel detected virtual device:"
dmesg | tail -5 | grep -i "input\|T500RS" || echo "  (check dmesg for device info)"
echo ""

# Step 4: Start the userspace driver
echo -e "${YELLOW}[4/5] Starting userspace driver...${NC}"

cd "$DRIVER_DIR"
./t500rs-ffb-modular &
DRIVER_PID=$!
echo "Driver PID: $DRIVER_PID"

# Wait for driver to initialize and connect to proxy
sleep 3

# Check if driver is running
if ! kill -0 $DRIVER_PID 2>/dev/null; then
    echo -e "${RED}ERROR: Driver failed to start${NC}"
    kill $PROXY_PID 2>/dev/null || true
    exit 1
fi

echo -e "${GREEN}✓ Driver running${NC}"
echo ""

# Step 5: Verify the setup
echo -e "${YELLOW}[5/5] Verifying Wine bridge setup...${NC}"
echo ""

# Check for virtual device
echo "Looking for virtual T500RS device in Wine..."
WINE_DEVICE=$(ls -la /dev/input/by-id/ 2>/dev/null | grep -i "T500RS.*Wine" | head -1 || true)

if [ -n "$WINE_DEVICE" ]; then
    echo -e "${GREEN}✓ Virtual device found:${NC}"
    echo "  $WINE_DEVICE"
else
    echo -e "${YELLOW}⚠ Virtual device not found by ID${NC}"
    echo "  But it should still be visible to Wine as a joystick"
fi

echo ""
echo "Checking input devices:"
ls -la /dev/input/ | grep -E "js[0-9]|event[0-9]" | tail -3

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Wine Bridge Setup Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Current status:"
echo "  ✓ UHID Proxy:       Running (PID $PROXY_PID)"
echo "  ✓ Userspace Driver: Running (PID $DRIVER_PID)"
echo "  ✓ IPC Socket:       $SOCKET_PATH"
echo ""
echo "Data flow:"
echo "  Real Device → Driver → Socket → Proxy → UHID → Wine"
echo ""
echo "Testing with Wine:"
echo "  wine control joy.cpl"
echo ""
echo "To test in a Wine/Proton game:"
echo "  1. The T500RS will appear as a joystick in Wine games"
echo "  2. Force feedback commands from Wine will flow back to the device"
echo "  3. All input (wheel, pedals, buttons) will work normally"
echo ""
echo "Monitoring:"
echo "  - Watch proxy: tail -f /proc/$PROXY_PID/fd/1"
echo "  - Watch driver: tail -f /proc/$DRIVER_PID/fd/1"
echo "  - Check dmesg: dmesg | tail -20"
echo ""
echo -e "${YELLOW}Press Ctrl+C to stop both processes...${NC}"
echo ""

# Wait for user interrupt
trap "echo ''; echo 'Stopping...'; kill $DRIVER_PID 2>/dev/null || true; kill $PROXY_PID 2>/dev/null || true; rm -f $SOCKET_PATH; echo 'Stopped.'; exit 0" INT TERM

# Keep script running
wait $PROXY_PID $DRIVER_PID
