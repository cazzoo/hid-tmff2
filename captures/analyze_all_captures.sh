#!/bin/bash
# Systematic analysis of all T500RS Windows captures

CAPTURES_DIR="."
OUTPUT_FILE="WINDOWS_CAPTURE_ANALYSIS.md"

echo "# T500RS Windows Capture Analysis" > "$OUTPUT_FILE"
echo "**Generated:** $(date)" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"
echo "---" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

# Function to extract USB OUT packets from a pcapng file
analyze_capture() {
    local file="$1"
    local name=$(basename "$file" .pcapng)
    
    echo "" >> "$OUTPUT_FILE"
    echo "## $name" >> "$OUTPUT_FILE"
    echo "" >> "$OUTPUT_FILE"
    
    # Extract USB INTERRUPT OUT transfers (endpoint 0x01)
    # Filter: usb.transfer_type == 0x01 (INTERRUPT) AND direction == OUT
    local packets=$(tshark -r "$file" -Y "usb.endpoint_address == 0x01" -T fields -e frame.number -e usb.capdata 2>/dev/null)
    
    if [ -z "$packets" ]; then
        echo "*No USB OUT packets found*" >> "$OUTPUT_FILE"
        return
    fi
    
    echo "\`\`\`" >> "$OUTPUT_FILE"
    echo "Frame | USB Data" >> "$OUTPUT_FILE"
    echo "------|----------" >> "$OUTPUT_FILE"
    
    while IFS=$'\t' read -r frame data; do
        if [ -n "$data" ]; then
            # Remove colons from hex data
            clean_data=$(echo "$data" | tr -d ':')
            # Get first byte (command type)
            cmd=$(echo "$clean_data" | cut -c1-2)
            echo "$frame | $clean_data (cmd: 0x$cmd)" >> "$OUTPUT_FILE"
        fi
    done <<< "$packets"
    
    echo "\`\`\`" >> "$OUTPUT_FILE"
    
    # Decode the commands
    echo "" >> "$OUTPUT_FILE"
    echo "**Decoded Commands:**" >> "$OUTPUT_FILE"
    echo "" >> "$OUTPUT_FILE"
    
    while IFS=$'\t' read -r frame data; do
        if [ -n "$data" ]; then
            clean_data=$(echo "$data" | tr -d ':')
            # Decode using Python script
            decoded=$(echo "$clean_data" | python3 -c "
import sys
data = bytes.fromhex(sys.stdin.read().strip())
if len(data) == 0:
    print('Empty')
    sys.exit(0)

cmd = data[0]
cmd_names = {
    0x01: 'Duration/Control',
    0x02: 'Upload Envelope',
    0x03: 'Constant Force',
    0x04: 'Periodic/Ramp',
    0x05: 'Condition',
    0x41: 'Start/Stop',
    0x42: 'Initialize'
}

name = cmd_names.get(cmd, f'Unknown (0x{cmd:02x})')

if cmd == 0x03 and len(data) >= 4:
    level = data[3]
    direction = 'RIGHT' if level < 0x80 else 'LEFT'
    magnitude = level if level < 0x80 else (256 - level)
    print(f'{name}: level={level} ({direction}, mag={magnitude})')
elif cmd == 0x41 and len(data) >= 4:
    action = 'START' if data[2] == 0x41 else 'STOP'
    print(f'{name}: {action}')
elif cmd == 0x02 and len(data) >= 9:
    attack_len = int.from_bytes(data[2:5], 'little')
    attack_lvl = data[5] if len(data) > 5 else 0
    fade_len = int.from_bytes(data[6:9], 'little')
    print(f'{name}: attack={attack_len}ms@{attack_lvl}, fade={fade_len}ms')
elif cmd == 0x01 and len(data) >= 6:
    effect_type = data[2]
    duration = int.from_bytes(data[4:6], 'little')
    type_names = {0x00: 'Constant', 0x20: 'Square', 0x21: 'Triangle', 0x22: 'Sine', 0x23: 'SawUp', 0x24: 'SawDown', 0x40: 'Spring', 0x41: 'Damper'}
    type_name = type_names.get(effect_type, f'0x{effect_type:02x}')
    print(f'{name}: type={type_name}, duration={duration}ms')
else:
    print(f'{name}: {data.hex()}')
" 2>/dev/null)
            
            if [ -n "$decoded" ]; then
                echo "- Frame $frame: $decoded" >> "$OUTPUT_FILE"
            fi
        fi
    done <<< "$packets"
}

# Analyze device initialization captures
echo "# Part 1: Device Initialization" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

for file in "$CAPTURES_DIR"/device_init*.pcapng; do
    if [ -f "$file" ]; then
        analyze_capture "$file"
    fi
done

# Analyze constant force test
echo "" >> "$OUTPUT_FILE"
echo "---" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"
echo "# Part 2: Constant Force Test" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

analyze_capture "$CAPTURES_DIR/device_const_force_pos.pcapng"

# Analyze settings changes
echo "" >> "$OUTPUT_FILE"
echo "---" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"
echo "# Part 3: Settings Adjustments" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

for file in "$CAPTURES_DIR"/device_settings_*.pcapng; do
    if [ -f "$file" ]; then
        analyze_capture "$file"
    fi
done

# Analyze control panel effects
echo "" >> "$OUTPUT_FILE"
echo "---" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"
echo "# Part 4: Control Panel Effects" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

for file in "$CAPTURES_DIR"/ctl_panel_*.pcapng; do
    if [ -f "$file" ]; then
        analyze_capture "$file"
    fi
done

echo "" >> "$OUTPUT_FILE"
echo "---" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"
echo "# Analysis Complete" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"
echo "Total captures analyzed: $(ls -1 "$CAPTURES_DIR"/{device_,ctl_panel_}*.pcapng 2>/dev/null | wc -l)" >> "$OUTPUT_FILE"

echo "Analysis complete! Results saved to $OUTPUT_FILE"

