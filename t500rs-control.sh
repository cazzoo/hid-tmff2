#!/bin/bash
# T500RS Control GUI Launcher
# This script launches the T500RS control GUI with proper permissions

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GUI_SCRIPT="$SCRIPT_DIR/t500rs-control-gui.py"

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "T500RS Control requires root permissions to write to sysfs."
    echo "Relaunching with sudo..."
    exec sudo "$GUI_SCRIPT"
else
    exec "$GUI_SCRIPT"
fi

