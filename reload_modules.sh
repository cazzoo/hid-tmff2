#!/bin/bash
# Development script to reload T500RS driver modules
# Usage: sudo ./reload_modules.sh

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}T500RS Driver Module Reload Script${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then 
    echo -e "${RED}ERROR: Please run as root (use sudo)${NC}"
    exit 1
fi

# Step 1: Clean up T500RS device and sysfs files
echo -e "${YELLOW}[1/6] Cleaning up T500RS device...${NC}"

# Find T500RS HID device in sysfs
T500RS_DEVICE=$(find /sys/bus/hid/devices/ -name "*044F:B65E*" 2>/dev/null | head -1)

if [ -n "$T500RS_DEVICE" ]; then
    DEVICE_NAME=$(basename "$T500RS_DEVICE")
    echo "  - Found HID device: $DEVICE_NAME"

    # Manually remove leftover sysfs files (from failed probe)
    echo "  - Removing leftover sysfs files..."
    for attr in gain range spring_level damper_level friction_level alternate_modes; do
        if [ -e "$T500RS_DEVICE/$attr" ]; then
            rm -f "$T500RS_DEVICE/$attr" 2>/dev/null || true
        fi
    done

    # Unbind from current driver if bound
    if [ -e "$T500RS_DEVICE/driver" ]; then
        CURRENT_DRIVER=$(basename $(readlink "$T500RS_DEVICE/driver"))
        echo "  - Unbinding from $CURRENT_DRIVER..."
        echo "$DEVICE_NAME" > "$T500RS_DEVICE/driver/unbind" 2>/dev/null || true
        sleep 0.5
    fi

    echo -e "${GREEN}  ✓ Device cleaned up${NC}"
else
    echo "  - T500RS HID device not found"
fi
echo ""

# Step 2: Unload modules
echo -e "${YELLOW}[2/6] Unloading modules...${NC}"

# Try to unload in reverse dependency order
if lsmod | grep -q "hid_tmff_new"; then
    echo "  - Removing hid_tmff_new..."
    modprobe -r hid_tmff_new || {
        echo -e "${RED}  WARNING: Failed to remove hid_tmff_new (may be in use)${NC}"
        echo "  Trying to force removal..."
        rmmod -f hid_tmff_new 2>/dev/null || true
    }
else
    echo "  - hid_tmff_new not loaded"
fi

if lsmod | grep -q "usb_tminit_new"; then
    echo "  - Removing usb_tminit_new..."
    modprobe -r usb_tminit_new || true
else
    echo "  - usb_tminit_new not loaded"
fi

if lsmod | grep -q "hid_tminit_new"; then
    echo "  - Removing hid_tminit_new..."
    modprobe -r hid_tminit_new || true
else
    echo "  - hid_tminit_new not loaded"
fi

echo -e "${GREEN}  ✓ Modules unloaded${NC}"
echo ""

# Step 3: Wait a moment for cleanup
echo -e "${YELLOW}[3/6] Waiting for cleanup...${NC}"
sleep 2
echo -e "${GREEN}  ✓ Done${NC}"
echo ""

# Step 4: Load init modules
echo -e "${YELLOW}[4/6] Loading init modules...${NC}"
modprobe hid_tminit_new && echo -e "${GREEN}  ✓ hid_tminit_new loaded${NC}" || echo -e "${RED}  ✗ Failed to load hid_tminit_new${NC}"
modprobe usb_tminit_new && echo -e "${GREEN}  ✓ usb_tminit_new loaded${NC}" || echo -e "${RED}  ✗ Failed to load usb_tminit_new${NC}"
echo ""

# Step 5: Wait for init to complete
echo -e "${YELLOW}[5/6] Waiting for device initialization...${NC}"
sleep 3
echo -e "${GREEN}  ✓ Done${NC}"
echo ""

# Step 6: Load main driver (from local build directory)
echo -e "${YELLOW}[6/6] Loading main driver from local build...${NC}"

# Get the directory where this script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Load the locally built module
if [ -f "$SCRIPT_DIR/hid_tmff_new.ko" ]; then
    echo "  - Loading $SCRIPT_DIR/hid_tmff_new.ko with params: $*"
    insmod "$SCRIPT_DIR/hid_tmff_new.ko" "$@" && echo -e "${GREEN}  ✓ hid_tmff_new loaded from local build${NC}" || {
        echo -e "${RED}  ✗ Failed to load hid_tmff_new${NC}"
        exit 1
    }
else
    echo -e "${RED}  ✗ Module not found: $SCRIPT_DIR/hid_tmff_new.ko${NC}"
    echo "  Run 'make' first to build the module"
    exit 1
fi
echo ""

# Show loaded modules
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Loaded Modules:${NC}"
echo -e "${BLUE}========================================${NC}"
lsmod | grep -E "(hid_tmff|tminit)" || echo "No modules loaded"
echo ""

# Show recent kernel messages
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Recent Kernel Messages (last 30 lines):${NC}"
echo -e "${BLUE}========================================${NC}"
dmesg | tail -30
echo ""

# Check for T500RS device
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}T500RS Device Status:${NC}"
echo -e "${BLUE}========================================${NC}"
if dmesg | tail -50 | grep -q "T500RS"; then
    echo -e "${GREEN}✓ T500RS detected in kernel logs${NC}"
    dmesg | tail -50 | grep "T500RS" | tail -5
else
    echo -e "${YELLOW}⚠ No T500RS messages in recent kernel logs${NC}"
fi
echo ""

# Check for errors
if dmesg | tail -50 | grep -qi "error\|fail\|bug\|oops"; then
    echo -e "${RED}⚠ WARNING: Errors detected in kernel log!${NC}"
    dmesg | tail -50 | grep -i "error\|fail\|bug\|oops" | tail -10
    echo ""
fi

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Module reload complete!${NC}"
echo -e "${GREEN}========================================${NC}"

