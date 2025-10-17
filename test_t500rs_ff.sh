#!/bin/bash
# T500RS Force Feedback Test Script

DEVICE="/dev/input/event262"

echo "=========================================="
echo "T500RS Force Feedback Test"
echo "=========================================="
echo ""
echo "Device: $DEVICE"
echo ""
echo "This will test basic force feedback effects."
echo "You should feel the wheel respond to each effect."
echo ""
echo "Press Ctrl+C to stop at any time."
echo ""
read -p "Press Enter to start testing..."

echo ""
echo "Running fftest on $DEVICE"
echo "Follow the on-screen prompts to test different effects."
echo ""

sudo fftest $DEVICE

