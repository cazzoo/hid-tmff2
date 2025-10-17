#!/bin/bash
# T500RS Control GUI Installation Script

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="/usr/local/bin"
DESKTOP_DIR="$HOME/.local/share/applications"

echo "=== T500RS Control GUI Installation ==="
echo ""

# Check for Python and GTK
echo "Checking dependencies..."
if ! command -v python3 &> /dev/null; then
    echo "✗ Python 3 not found!"
    echo "  Install with: sudo pacman -S python"
    exit 1
fi
echo "✓ Python 3 found"

if ! python3 -c "import gi" 2>/dev/null; then
    echo "✗ PyGObject not found!"
    echo "  Install with: sudo pacman -S python-gobject gtk3"
    exit 1
fi
echo "✓ PyGObject found"

if ! python3 -c "import gi; gi.require_version('Gtk', '3.0'); from gi.repository import Gtk" 2>/dev/null; then
    echo "✗ GTK 3 not found!"
    echo "  Install with: sudo pacman -S gtk3"
    exit 1
fi
echo "✓ GTK 3 found"

echo ""
echo "All dependencies satisfied!"
echo ""

# Ask for installation type
echo "Installation options:"
echo "  1) System-wide (requires sudo, installs to /usr/local/bin)"
echo "  2) User-only (no sudo, desktop launcher only)"
echo "  3) Skip installation (just run from current directory)"
echo ""
read -p "Choose option [1-3]: " choice

case $choice in
    1)
        echo ""
        echo "Installing system-wide..."
        
        # Copy GUI script
        sudo cp "$SCRIPT_DIR/t500rs-control-gui.py" "$INSTALL_DIR/t500rs-control"
        sudo chmod +x "$INSTALL_DIR/t500rs-control"
        echo "✓ Installed to $INSTALL_DIR/t500rs-control"
        
        # Create desktop file with system path
        mkdir -p "$DESKTOP_DIR"
        cat > "$DESKTOP_DIR/t500rs-control.desktop" << EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=T500RS Control
Comment=Control panel for Thrustmaster T500RS force feedback settings
Exec=sudo $INSTALL_DIR/t500rs-control
Icon=input-gaming
Terminal=false
Categories=Game;Settings;
Keywords=thrustmaster;t500rs;wheel;force;feedback;ffb;
EOF
        chmod +x "$DESKTOP_DIR/t500rs-control.desktop"
        echo "✓ Created desktop launcher"
        
        # Update desktop database
        if command -v update-desktop-database &> /dev/null; then
            update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
        fi
        
        echo ""
        echo "✓ Installation complete!"
        echo ""
        echo "You can now run:"
        echo "  - From terminal: sudo t500rs-control"
        echo "  - From app menu: Search for 'T500RS Control'"
        ;;
        
    2)
        echo ""
        echo "Installing desktop launcher only..."
        
        # Create desktop file with local path
        mkdir -p "$DESKTOP_DIR"
        cat > "$DESKTOP_DIR/t500rs-control.desktop" << EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=T500RS Control
Comment=Control panel for Thrustmaster T500RS force feedback settings
Exec=sudo $SCRIPT_DIR/t500rs-control-gui.py
Icon=input-gaming
Terminal=false
Categories=Game;Settings;
Keywords=thrustmaster;t500rs;wheel;force;feedback;ffb;
EOF
        chmod +x "$DESKTOP_DIR/t500rs-control.desktop"
        echo "✓ Created desktop launcher"
        
        # Update desktop database
        if command -v update-desktop-database &> /dev/null; then
            update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
        fi
        
        echo ""
        echo "✓ Installation complete!"
        echo ""
        echo "You can now run:"
        echo "  - From terminal: sudo $SCRIPT_DIR/t500rs-control-gui.py"
        echo "  - From app menu: Search for 'T500RS Control'"
        ;;
        
    3)
        echo ""
        echo "Skipping installation."
        echo ""
        echo "You can run the GUI with:"
        echo "  sudo $SCRIPT_DIR/t500rs-control-gui.py"
        ;;
        
    *)
        echo "Invalid choice. Exiting."
        exit 1
        ;;
esac

echo ""
echo "=== Setup Complete ==="
echo ""
echo "Note: The GUI requires root permissions to write to sysfs."
echo "      Always run with 'sudo' or it will prompt for password."
echo ""
echo "Optional: Set up udev rules to avoid needing sudo:"
echo "  sudo nano /etc/udev/rules.d/99-t500rs.rules"
echo "  Add: SUBSYSTEM==\"hid\", ATTRS{idVendor}==\"044f\", ATTRS{idProduct}==\"b65e\", MODE=\"0666\""
echo "  sudo udevadm control --reload-rules && sudo udevadm trigger"
echo ""

