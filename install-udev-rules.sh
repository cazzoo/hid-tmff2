#!/bin/bash
# Install udev rules for Thrustmaster wheels (including T500RS)
# This allows non-root access to wheel settings and proper Proton/Wine support

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UDEV_RULES_FILE="udev/99-thrustmaster.rules"
UDEV_RULES_DIR="/etc/udev/rules.d"
OLD_RULES_FILE="99-thrustmaster-t500rs.rules"

echo "=== Thrustmaster Wheels udev Rules Installation ==="
echo ""

# Check if running as root
if [ "$EUID" -eq 0 ]; then
    echo "Please run this script as a normal user (it will ask for sudo when needed)"
    exit 1
fi

# Check if rules file exists
if [ ! -f "$SCRIPT_DIR/$UDEV_RULES_FILE" ]; then
    echo "Error: $UDEV_RULES_FILE not found in $SCRIPT_DIR"
    exit 1
fi

echo "This script will:"
echo "  1. Remove old 99-thrustmaster-t500rs.rules (if exists)"
echo "  2. Install consolidated 99-thrustmaster.rules (supports all wheels)"
echo "  3. Reload udev rules"
echo "  4. Trigger udev to apply rules"
echo ""
echo "After installation, you can access wheel settings without sudo."
echo "Proton/Wine games will also have proper device access."
echo ""
read -p "Continue? [y/N] " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Installation cancelled."
    exit 0
fi

echo ""
echo "[1/4] Removing old udev rules files..."
# Remove old T500RS-specific rules file if it exists
if [ -f "$UDEV_RULES_DIR/$OLD_RULES_FILE" ]; then
    sudo rm -f "$UDEV_RULES_DIR/$OLD_RULES_FILE"
    echo "✓ Removed old $OLD_RULES_FILE"
else
    echo "⚠ Old $OLD_RULES_FILE not found (this is OK)"
fi

echo ""
echo "[2/4] Installing consolidated udev rules..."
sudo cp "$SCRIPT_DIR/$UDEV_RULES_FILE" "$UDEV_RULES_DIR/"
echo "✓ Installed 99-thrustmaster.rules (supports T300RS, T248, TX, TSXW, TSPC, T500RS)"

echo ""
echo "[3/4] Reloading udev rules..."
sudo udevadm control --reload-rules
echo "✓ udev rules reloaded"

echo ""
echo "[4/4] Triggering udev..."
sudo udevadm trigger
echo "✓ udev triggered"

echo ""
echo "=== Installation Complete ==="
echo ""
echo "The udev rules have been installed successfully!"
echo ""
echo "Next steps:"
echo "  1. Disconnect and reconnect your wheel"
echo "     (or reboot your system)"
echo "  2. Test without sudo:"
echo "     cat /sys/bus/hid/devices/0003:044F:B65E.*/gain"
echo "  3. Run GUI without sudo:"
echo "     ./t500rs-control-gui.py"
echo ""
echo "For Proton/Steam games (Automobilista 2, etc.):"
echo "  1. Find your event device number:"
echo "     cat /proc/bus/input/devices | grep -A 5 'Thrustmaster'"
echo "  2. Add to Steam launch options:"
echo "     SDL_JOYSTICK_DEVICE=/dev/input/eventXX %command%"
echo "     (replace XX with your event number)"
echo ""
echo "If it doesn't work immediately, try:"
echo "  - Unplug the wheel"
echo "  - Wait 5 seconds"
echo "  - Plug it back in"
echo "  - Or reboot your system"
echo ""

