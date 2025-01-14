#!/bin/bash

# Build script for TMT500RS hardware validation test

# Check for required tools
if ! command -v gcc &> /dev/null; then
    echo "Error: gcc is required but not installed"
    exit 1
fi

# Set compiler flags
CFLAGS="-Wall -Wextra -O2"
LDFLAGS=""

# Source and output files
SRC="ff_combo_test.c"
OUT="ff_combo_test"

# Build the test program
echo "Building hardware validation test..."
gcc $CFLAGS $SRC -o $OUT $LDFLAGS

if [ $? -eq 0 ]; then
    echo "Build successful!"
    echo "Usage: ./ff_combo_test /dev/input/eventX"
    echo "Note: You may need to run with sudo for device access"
else
    echo "Build failed!"
    exit 1
fi

# Make the test program executable
chmod +x $OUT

# Create logs directory if it doesn't exist
mkdir -p logs

echo "Setup complete!" 