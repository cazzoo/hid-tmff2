#!/bin/bash
# Install udev rules for Thrustmaster T500RS
# This allows non-root access to wheel settings

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UDEV_RULES_FILE="99-thrustmaster-t500rs.rules"
UDEV_RULES_DIR="/etc/udev/rules.d"

echo "=== T500RS udev Rules Installation ==="
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
echo "  1. Copy $UDEV_RULES_FILE to $UDEV_RULES_DIR/"
echo "  2. Reload udev rules"
echo "  3. Trigger udev to apply rules"
echo ""
echo "After installation, you can access T500RS settings without sudo."
echo ""
read -p "Continue? [y/N] " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Installation cancelled."
    exit 0
fi

echo ""
echo "[1/3] Copying udev rules file..."
sudo cp "$SCRIPT_DIR/$UDEV_RULES_FILE" "$UDEV_RULES_DIR/"
echo "✓ Copied to $UDEV_RULES_DIR/$UDEV_RULES_FILE"

echo ""
echo "[2/3] Reloading udev rules..."
sudo udevadm control --reload-rules
echo "✓ udev rules reloaded"

echo ""
echo "[3/3] Triggering udev..."
sudo udevadm trigger
echo "✓ udev triggered"

echo ""
echo "=== Installation Complete ==="
echo ""
echo "The udev rules have been installed successfully!"
echo ""
echo "Next steps:"
echo "  1. Disconnect and reconnect your T500RS wheel"
echo "     (or reboot your system)"
echo "  2. Test without sudo:"
echo "     cat /sys/bus/hid/devices/0003:044F:B65E.*/gain"
echo "  3. Run GUI without sudo:"
echo "     ./t500rs-control-gui.py"
echo ""
echo "If it doesn't work immediately, try:"
echo "  - Unplug the wheel"
echo "  - Wait 5 seconds"
echo "  - Plug it back in"
echo "  - Or reboot your system"
echo ""

