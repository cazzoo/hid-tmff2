#!/bin/bash
#
# Install Thrustmaster T500RS Windows drivers into Wine/Proton prefix
#
# This helps games running under Wine/Proton properly recognize the wheel
# and configure force feedback correctly.
#
# Usage:
#   ./install_windows_drivers.sh [STEAM_APP_ID]
#
# Examples:
#   ./install_windows_drivers.sh 805550    # Assetto Corsa Competizione
#   ./install_windows_drivers.sh 1066890   # Automobilista 2
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "========================================="
echo "T500RS Windows Driver Installer"
echo "========================================="
echo

# Check if Steam is installed
if [ ! -d "$HOME/.steam" ] && [ ! -d "$HOME/.local/share/Steam" ]; then
    echo -e "${RED}ERROR: Steam not found!${NC}"
    echo "This script is for installing drivers into Steam/Proton prefixes."
    exit 1
fi

# Find Steam directory
if [ -d "$HOME/.steam/steam" ]; then
    STEAM_DIR="$HOME/.steam/steam"
elif [ -d "$HOME/.local/share/Steam" ]; then
    STEAM_DIR="$HOME/.local/share/Steam"
else
    echo -e "${RED}ERROR: Could not find Steam directory${NC}"
    exit 1
fi

echo -e "${GREEN}Found Steam directory: $STEAM_DIR${NC}"
echo

# Get Steam App ID
if [ -z "$1" ]; then
    echo "Please enter the Steam App ID of your game:"
    echo "  - Assetto Corsa Competizione: 805550"
    echo "  - Automobilista 2: 1066890"
    echo "  - F1 22: 1692250"
    echo "  - F1 23: 2108330"
    echo "  - iRacing: (not on Steam)"
    echo
    read -p "Steam App ID: " STEAM_APP_ID
else
    STEAM_APP_ID="$1"
fi

# Find the game's prefix
COMPAT_DATA="$STEAM_DIR/steamapps/compatdata/$STEAM_APP_ID"

if [ ! -d "$COMPAT_DATA" ]; then
    echo -e "${RED}ERROR: Game prefix not found at: $COMPAT_DATA${NC}"
    echo
    echo "Make sure:"
    echo "  1. You've run the game at least once"
    echo "  2. The Steam App ID is correct"
    echo "  3. The game uses Proton (not native Linux)"
    exit 1
fi

PREFIX="$COMPAT_DATA/pfx"
echo -e "${GREEN}Found game prefix: $PREFIX${NC}"
echo

# Download Thrustmaster drivers
DRIVER_URL="https://ts.thrustmaster.com/download/accessories/pc/t500rs/T500RS_Drivers_2014.exe"
DRIVER_FILE="/tmp/T500RS_Drivers_2014.exe"

echo "Downloading Thrustmaster T500RS drivers..."
if command -v wget &> /dev/null; then
    wget -O "$DRIVER_FILE" "$DRIVER_URL" || {
        echo -e "${RED}ERROR: Failed to download drivers${NC}"
        echo "You can manually download from: $DRIVER_URL"
        exit 1
    }
elif command -v curl &> /dev/null; then
    curl -L -o "$DRIVER_FILE" "$DRIVER_URL" || {
        echo -e "${RED}ERROR: Failed to download drivers${NC}"
        echo "You can manually download from: $DRIVER_URL"
        exit 1
    }
else
    echo -e "${RED}ERROR: Neither wget nor curl found${NC}"
    echo "Please install wget or curl, or manually download:"
    echo "  $DRIVER_URL"
    exit 1
fi

echo -e "${GREEN}Downloaded drivers to: $DRIVER_FILE${NC}"
echo

# Install drivers using Wine
echo "Installing drivers into Wine prefix..."
echo -e "${YELLOW}NOTE: You may see some error dialogs - this is normal!${NC}"
echo "      Just click through them and let the installer finish."
echo

# Set Wine prefix
export WINEPREFIX="$PREFIX"

# Try to find Wine or Proton's wine
WINE_BIN=""
if command -v wine &> /dev/null; then
    WINE_BIN="wine"
elif [ -f "$STEAM_DIR/steamapps/common/Proton - Experimental/files/bin/wine" ]; then
    WINE_BIN="$STEAM_DIR/steamapps/common/Proton - Experimental/files/bin/wine"
elif [ -f "$STEAM_DIR/steamapps/common/Proton 8.0/files/bin/wine" ]; then
    WINE_BIN="$STEAM_DIR/steamapps/common/Proton 8.0/files/bin/wine"
else
    echo -e "${YELLOW}WARNING: Could not find Wine binary${NC}"
    echo "Trying system wine..."
    WINE_BIN="wine"
fi

echo "Using Wine: $WINE_BIN"
echo

# Run installer
"$WINE_BIN" "$DRIVER_FILE" || {
    echo -e "${YELLOW}WARNING: Installer exited with error (this might be normal)${NC}"
}

echo
echo -e "${GREEN}=========================================${NC}"
echo -e "${GREEN}Installation Complete!${NC}"
echo -e "${GREEN}=========================================${NC}"
echo
echo "Next steps:"
echo "  1. Restart the game"
echo "  2. Go to controller settings"
echo "  3. The wheel should now be properly recognized"
echo "  4. Recalibrate if needed"
echo
echo "If the game still doesn't recognize the wheel:"
echo "  - Make sure the userspace driver is running"
echo "  - Check that /dev/input/eventX exists for the wheel"
echo "  - Try running: dmesg | grep T500RS"
echo

# Cleanup
rm -f "$DRIVER_FILE"

