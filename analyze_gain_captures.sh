#!/bin/bash

echo "==================================="
echo "T500RS Per-Effect Gain Analysis"
echo "==================================="
echo ""

analyze_capture() {
    local file="$1"
    local desc="$2"
    
    echo "-----------------------------------"
    echo "$desc"
    echo "File: $file"
    echo "-----------------------------------"
    
    # Extract host-to-device USB interrupt transfers
    tshark -r "$file" -Y "usb.src == \"host\"" -T fields -e usb.capdata 2>/dev/null | \
        grep -v "^$" | \
        while read -r data; do
            # Parse the 6-byte packet
            if [ ${#data} -eq 12 ]; then
                byte0="${data:0:2}"
                byte1="${data:2:2}"
                byte2="${data:4:2}"
                byte3="${data:6:2}"
                byte4="${data:8:2}"
                byte5="${data:10:2}"
                
                # Convert to decimal for readability
                b0=$((16#$byte0))
                b1=$((16#$byte1))
                b2=$((16#$byte2))
                b3=$((16#$byte3))
                b4=$((16#$byte4))
                b5=$((16#$byte5))
                
                echo "  $byte0 $byte1 $byte2 $byte3 $byte4 $byte5  (dec: $b0 $b1 $b2 $b3 $b4 $b5)"
            fi
        done | head -20
    
    echo ""
}

# Analyze each capture
analyze_capture "captures/device_settings_globalforce_60_to_20.pcapng" "GLOBAL/MASTER GAIN: 60% -> 20%"
analyze_capture "captures/device_settings_periodicforce_100_to_60.pcapng" "PERIODIC FORCE GAIN: 100% -> 60%"
analyze_capture "captures/device_settings_springforce_100_to_30.pcapng" "SPRING FORCE GAIN: 100% -> 30%"
analyze_capture "captures/device_settings_damperforces_100_to_10.pcapng" "DAMPER FORCE GAIN: 100% -> 10%"
analyze_capture "captures/device_settings_globalautocenter_from_12_to_55.pcapng" "AUTOCENTER STRENGTH: 12% -> 55%"

echo "==================================="
echo "Analysis Complete"
echo "==================================="

