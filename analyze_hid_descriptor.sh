#!/bin/bash
#
# T500RS HID Descriptor Analysis Script
# Extracts and analyzes the HID descriptor to find report IDs
#

set -e

VENDOR_ID="044f"
PRODUCT_ID="b65e"
OUTPUT_DIR="hid_analysis"

echo "=========================================="
echo "T500RS HID Descriptor Analysis"
echo "=========================================="
echo ""

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Check if device is connected
echo "[1/6] Checking if T500RS is connected..."
if ! lsusb -d ${VENDOR_ID}:${PRODUCT_ID} > /dev/null 2>&1; then
    echo "❌ ERROR: T500RS not found!"
    echo "   Please connect the device and try again."
    exit 1
fi
echo "✅ Device found"
echo ""

# Get full lsusb output
echo "[2/6] Getting device information..."
sudo lsusb -v -d ${VENDOR_ID}:${PRODUCT_ID} > "$OUTPUT_DIR/lsusb_full.txt" 2>&1
echo "✅ Saved to $OUTPUT_DIR/lsusb_full.txt"
echo ""

# Extract HID Report Descriptor
echo "[3/6] Extracting HID Report Descriptor..."
sudo lsusb -v -d ${VENDOR_ID}:${PRODUCT_ID} 2>&1 | \
    sed -n '/Report Descriptor/,/^[^ ]/p' | \
    head -n -1 > "$OUTPUT_DIR/hid_descriptor.txt"

if [ -s "$OUTPUT_DIR/hid_descriptor.txt" ]; then
    echo "✅ Saved to $OUTPUT_DIR/hid_descriptor.txt"
else
    echo "⚠️  Could not extract HID descriptor from lsusb"
fi
echo ""

# Try usbhid-dump if available
echo "[4/6] Trying usbhid-dump..."
if command -v usbhid-dump &> /dev/null; then
    sudo usbhid-dump -d ${VENDOR_ID}:${PRODUCT_ID} -e descriptor > "$OUTPUT_DIR/hid_descriptor_raw.txt" 2>&1 || true
    if [ -s "$OUTPUT_DIR/hid_descriptor_raw.txt" ]; then
        echo "✅ Saved to $OUTPUT_DIR/hid_descriptor_raw.txt"
    else
        echo "⚠️  usbhid-dump produced no output"
    fi
else
    echo "⚠️  usbhid-dump not installed (optional)"
    echo "   Install with: sudo apt-get install usbutils"
fi
echo ""

# Extract interface information
echo "[5/6] Extracting interface information..."
sudo lsusb -v -d ${VENDOR_ID}:${PRODUCT_ID} 2>&1 | \
    grep -E "bNumInterfaces|bInterfaceNumber|bInterfaceClass|bInterfaceSubClass|bInterfaceProtocol|iInterface" \
    > "$OUTPUT_DIR/interfaces.txt"
echo "✅ Saved to $OUTPUT_DIR/interfaces.txt"
echo ""

# Analyze and summarize
echo "[6/6] Analyzing HID descriptor..."
echo ""
echo "=========================================="
echo "ANALYSIS RESULTS"
echo "=========================================="
echo ""

# Count interfaces
NUM_INTERFACES=$(grep "bNumInterfaces" "$OUTPUT_DIR/interfaces.txt" | awk '{print $2}' | head -1)
echo "Number of interfaces: $NUM_INTERFACES"
echo ""

# Show interface classes
echo "Interface classes:"
grep "bInterfaceClass" "$OUTPUT_DIR/interfaces.txt" | while read line; do
    class=$(echo "$line" | awk '{print $2}')
    desc=$(echo "$line" | cut -d' ' -f3-)
    echo "  - Class $class: $desc"
done
echo ""

# Try to find report IDs in descriptor
echo "Searching for Report IDs in HID descriptor..."
if [ -s "$OUTPUT_DIR/hid_descriptor.txt" ]; then
    # Look for "Report ID" lines
    REPORT_IDS=$(grep -i "Report ID" "$OUTPUT_DIR/hid_descriptor.txt" | grep -oP '\(\K[0-9]+' || echo "")
    
    if [ -n "$REPORT_IDS" ]; then
        echo "✅ Found Report IDs:"
        echo "$REPORT_IDS" | sort -u | while read id; do
            echo "  - Report ID: $id (0x$(printf '%02x' $id))"
        done
    else
        echo "⚠️  No Report IDs found in descriptor"
        echo "   This might mean:"
        echo "   - Device uses single report (no ID)"
        echo "   - Descriptor parsing failed"
        echo "   - Need to check raw descriptor"
    fi
else
    echo "⚠️  HID descriptor not available"
fi
echo ""

# Check for Output reports
echo "Checking for Output Reports..."
if grep -qi "output" "$OUTPUT_DIR/hid_descriptor.txt" 2>/dev/null; then
    echo "✅ Output reports found in descriptor"
    grep -i "output" "$OUTPUT_DIR/hid_descriptor.txt" | head -5
else
    echo "⚠️  No output reports found"
fi
echo ""

# Check for Feature reports
echo "Checking for Feature Reports..."
if grep -qi "feature" "$OUTPUT_DIR/hid_descriptor.txt" 2>/dev/null; then
    echo "✅ Feature reports found in descriptor"
    grep -i "feature" "$OUTPUT_DIR/hid_descriptor.txt" | head -5
else
    echo "⚠️  No feature reports found"
fi
echo ""

# Get endpoint information
echo "USB Endpoints:"
sudo lsusb -v -d ${VENDOR_ID}:${PRODUCT_ID} 2>&1 | \
    grep -A 5 "Endpoint Descriptor" | \
    grep -E "bEndpointAddress|bmAttributes|wMaxPacketSize" | \
    head -12
echo ""

echo "=========================================="
echo "COMPARISON WITH WINDOWS PROTOCOL"
echo "=========================================="
echo ""

echo "Windows uses these Report IDs (from our analysis):"
echo "  - 0x01 (1)  - Effect parameters (15 bytes)"
echo "  - 0x02 (2)  - Additional parameters (9 bytes)"
echo "  - 0x04 (4)  - More parameters (8 bytes)"
echo "  - 0x0a (10) - Configuration (15 bytes)"
echo "  - 0x40 (64) - Unknown control (4 bytes)"
echo "  - 0x41 (65) - Effect control (4 bytes)"
echo "  - 0x42 (66) - Initialization (2 or 15 bytes)"
echo "  - 0x43 (67) - Unknown (2 bytes)"
echo ""

if [ -s "$OUTPUT_DIR/hid_descriptor.txt" ]; then
    echo "Checking if these IDs are in Linux HID descriptor..."
    for id in 1 2 4 10 64 65 66 67; do
        hex_id=$(printf '0x%02x' $id)
        if grep -q "Report ID.*$id" "$OUTPUT_DIR/hid_descriptor.txt" 2>/dev/null; then
            echo "  ✅ Report ID $id ($hex_id) - FOUND"
        else
            echo "  ❌ Report ID $id ($hex_id) - NOT FOUND"
        fi
    done
fi
echo ""

echo "=========================================="
echo "RECOMMENDATIONS"
echo "=========================================="
echo ""

# Check if we found the reports we need
FOUND_01=$(grep -c "Report ID.*1" "$OUTPUT_DIR/hid_descriptor.txt" 2>/dev/null || echo "0")
FOUND_41=$(grep -c "Report ID.*65" "$OUTPUT_DIR/hid_descriptor.txt" 2>/dev/null || echo "0")

if [ "$FOUND_01" -gt 0 ] && [ "$FOUND_41" -gt 0 ]; then
    echo "✅ GOOD NEWS: Found Report IDs 1 and 65 in descriptor!"
    echo "   → Try using these report IDs in the kernel driver"
    echo "   → Update t500rs_send_buf() to use correct report structure"
    echo ""
elif [ "$FOUND_01" -eq 0 ] && [ "$FOUND_41" -eq 0 ]; then
    echo "⚠️  Report IDs 1 and 65 NOT in descriptor"
    echo "   This explains the error -11!"
    echo ""
    echo "   Next steps:"
    echo "   1. Check for Feature Reports (might enable FF mode)"
    echo "   2. Try libusb approach (bypass HID layer)"
    echo "   3. Look for vendor-specific control transfers"
    echo ""
else
    echo "⚠️  Partial match - some reports found, some missing"
    echo "   → Review the descriptor carefully"
    echo "   → Check if there's a mode switch needed"
    echo ""
fi

echo "Files created in $OUTPUT_DIR/:"
ls -lh "$OUTPUT_DIR/"
echo ""

echo "=========================================="
echo "NEXT STEPS"
echo "=========================================="
echo ""
echo "1. Review: $OUTPUT_DIR/hid_descriptor.txt"
echo "2. Compare with Windows protocol (see T500RS_PROTOCOL.md)"
echo "3. If reports missing: Try libusb approach (see QUICK_START_NEXT_SESSION.md)"
echo "4. If reports found: Update kernel driver to use correct IDs"
echo ""
echo "For detailed guidance, see: NEXT_STEPS_PLAN.md"
echo ""

