#!/bin/bash
# Analyze T500RS USB capture to extract protocol

echo "=========================================="
echo "T500RS USB Capture Analysis"
echo "=========================================="
echo ""

if [ -z "$1" ]; then
    echo "Usage: $0 <capture_file.pcapng>"
    echo ""
    echo "Available captures:"
    ls -lh captures/*.pcapng 2>/dev/null
    exit 1
fi

CAPTURE_FILE="$1"

if [ ! -f "$CAPTURE_FILE" ]; then
    echo "✗ File not found: $CAPTURE_FILE"
    exit 1
fi

echo "Analyzing: $CAPTURE_FILE"
echo ""

# Check if tshark is available
if ! command -v tshark &> /dev/null; then
    echo "✗ tshark not found. Install with: sudo pacman -S wireshark-cli"
    exit 1
fi

echo "=========================================="
echo "1. Packet Summary"
echo "=========================================="
echo ""

TOTAL=$(tshark -r "$CAPTURE_FILE" 2>/dev/null | wc -l)
echo "Total packets: $TOTAL"

SET_REPORT=$(tshark -r "$CAPTURE_FILE" -Y "usb.setup.bRequest == 0x09" 2>/dev/null | wc -l)
echo "SET_REPORT (0x09): $SET_REPORT"

GET_REPORT=$(tshark -r "$CAPTURE_FILE" -Y "usb.setup.bRequest == 0x01" 2>/dev/null | wc -l)
echo "GET_REPORT (0x01): $GET_REPORT"

INTERRUPT_OUT=$(tshark -r "$CAPTURE_FILE" -Y "usb.transfer_type == 0x01 && usb.endpoint_address.direction == 0" 2>/dev/null | wc -l)
echo "INTERRUPT OUT: $INTERRUPT_OUT"

echo ""
echo "=========================================="
echo "2. SET_REPORT Commands (Force Feedback)"
echo "=========================================="
echo ""

if [ $SET_REPORT -gt 0 ]; then
    echo "Extracting SET_REPORT data..."
    echo ""
    echo "Frame | Report ID | Data"
    echo "------|-----------|-----"
    
    tshark -r "$CAPTURE_FILE" -Y "usb.setup.bRequest == 0x09" \
        -T fields -e frame.number -e usb.setup.wValue -e usb.capdata 2>/dev/null | \
        while IFS=$'\t' read -r frame wvalue data; do
            # Extract report ID from wValue (low byte)
            report_id=$((wvalue & 0xFF))
            printf "%5s | %9d | %s\n" "$frame" "$report_id" "$data"
        done
    
    echo ""
    echo "Detailed SET_REPORT analysis:"
    echo ""
    
    # Save to file for detailed analysis
    ANALYSIS_FILE="${CAPTURE_FILE%.pcapng}_analysis.txt"
    tshark -r "$CAPTURE_FILE" -Y "usb.setup.bRequest == 0x09" -V 2>/dev/null > "$ANALYSIS_FILE"
    echo "✓ Detailed analysis saved to: $ANALYSIS_FILE"
else
    echo "⚠ No SET_REPORT commands found"
fi

echo ""
echo "=========================================="
echo "3. INTERRUPT OUT Transfers"
echo "=========================================="
echo ""

if [ $INTERRUPT_OUT -gt 0 ]; then
    echo "Extracting INTERRUPT OUT data..."
    echo ""
    echo "Frame | Endpoint | Data"
    echo "------|----------|-----"
    
    tshark -r "$CAPTURE_FILE" \
        -Y "usb.transfer_type == 0x01 && usb.endpoint_address.direction == 0" \
        -T fields -e frame.number -e usb.endpoint_address -e usb.capdata 2>/dev/null | \
        head -20 | \
        while IFS=$'\t' read -r frame endpoint data; do
            printf "%5s | %8s | %s\n" "$frame" "$endpoint" "$data"
        done
    
    if [ $INTERRUPT_OUT -gt 20 ]; then
        echo "... (showing first 20 of $INTERRUPT_OUT)"
    fi
else
    echo "⚠ No INTERRUPT OUT transfers found"
fi

echo ""
echo "=========================================="
echo "4. Protocol Pattern Detection"
echo "=========================================="
echo ""

# Extract unique data patterns
echo "Looking for command patterns..."
echo ""

if [ $SET_REPORT -gt 0 ]; then
    echo "Unique SET_REPORT data patterns:"
    tshark -r "$CAPTURE_FILE" -Y "usb.setup.bRequest == 0x09" \
        -T fields -e usb.capdata 2>/dev/null | sort | uniq -c | sort -rn | head -10
    
    echo ""
    echo "First few bytes of each command:"
    tshark -r "$CAPTURE_FILE" -Y "usb.setup.bRequest == 0x09" \
        -T fields -e usb.capdata 2>/dev/null | \
        while read data; do
            # Show first 8 bytes
            echo "$data" | cut -d':' -f1-8
        done | sort | uniq -c | sort -rn | head -10
fi

echo ""
echo "=========================================="
echo "5. Initialization Sequence"
echo "=========================================="
echo ""

echo "First 10 SET_REPORT commands (likely initialization):"
echo ""
tshark -r "$CAPTURE_FILE" -Y "usb.setup.bRequest == 0x09" \
    -T fields -e frame.time_relative -e usb.capdata 2>/dev/null | head -10 | \
    while IFS=$'\t' read -r time data; do
        printf "Time: %8s | Data: %s\n" "$time" "$data"
    done

echo ""
echo "=========================================="
echo "6. Report Structure Analysis"
echo "=========================================="
echo ""

# Analyze data lengths
echo "Data length distribution:"
tshark -r "$CAPTURE_FILE" -Y "usb.setup.bRequest == 0x09" \
    -T fields -e usb.capdata 2>/dev/null | \
    awk -F':' '{print NF}' | sort | uniq -c | sort -rn

echo ""
echo "=========================================="
echo "Summary & Recommendations"
echo "=========================================="
echo ""

if [ $SET_REPORT -gt 0 ]; then
    echo "✓ Found $SET_REPORT SET_REPORT commands"
    echo ""
    echo "Next steps:"
    echo "1. Review the data patterns above"
    echo "2. Identify command structure (report ID, command type, parameters)"
    echo "3. Compare with our current implementation"
    echo "4. Look for:"
    echo "   - Initialization commands"
    echo "   - Force feedback effect commands"
    echo "   - Effect start/stop commands"
    echo ""
    echo "Detailed analysis saved to: $ANALYSIS_FILE"
    echo ""
    echo "To view in Wireshark:"
    echo "  wireshark $CAPTURE_FILE"
elif [ $INTERRUPT_OUT -gt 0 ]; then
    echo "✓ Found $INTERRUPT_OUT INTERRUPT OUT transfers"
    echo ""
    echo "The device may use INTERRUPT transfers instead of SET_REPORT."
    echo "Review the INTERRUPT OUT data above."
else
    echo "⚠ No outgoing USB data found"
    echo ""
    echo "Possible issues:"
    echo "1. Capture was too short"
    echo "2. No force feedback was triggered"
    echo "3. Wrong USB device captured"
    echo ""
    echo "Try capturing again with force feedback active."
fi

echo ""

