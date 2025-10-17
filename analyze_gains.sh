#!/bin/bash

analyze() {
    echo "=== $1 ==="
    tshark -r "$2" -Y "usb.endpoint_address.direction == 0" -T fields -e usb.capdata 2>&1 | \
        grep -v "^$" | \
        while read data; do
            if [ ${#data} -eq 4 ]; then
                byte0="${data:0:2}"
                byte1="${data:2:2}"
                b0=$((16#$byte0))
                b1=$((16#$byte1))
                echo "  $byte0 $byte1  (dec: $b0 $b1)"
            fi
        done | sort -u
    echo ""
}

analyze "GLOBAL FORCE 60% -> 20%" "captures/device_settings_globalforce_60_to_20.pcapng"
analyze "PERIODIC FORCE 100% -> 60%" "captures/device_settings_periodicforce_100_to_60.pcapng"
analyze "SPRING FORCE 100% -> 30%" "captures/device_settings_springforce_100_to_30.pcapng"
analyze "DAMPER FORCE 100% -> 10%" "captures/device_settings_damperforces_100_to_10.pcapng"
analyze "AUTOCENTER 12% -> 55%" "captures/device_settings_globalautocenter_from_12_to_55.pcapng"
