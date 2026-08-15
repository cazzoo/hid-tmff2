#!/bin/bash
# Reproducible extractor: regenerate raw_data/*.txt from the community captures.
#
# Usage:
#   ./work/extract.sh
#
# Produces:
#   work/raw_data/cap1_out_reports.txt   (capture 1 OUT packets)
#   work/raw_data/cap1_in_reports.txt    (capture 1 IN packets)
#   work/raw_data/cap2_out_reports.txt   (capture 2 OUT packets, T500RS F1 advanced mode)
#   work/raw_data/cap2_in_all.txt        (capture 2 IN packets, all 2.3.* endpoints)
#
# Requires: tshark (Wireshark) >= 4.0

set -euo pipefail

CAP_DIR="$(dirname "$0")/../comunity-captures"
OUT_DIR="$(dirname "$0")/raw_data"
mkdir -p "$OUT_DIR"

CAP1="$CAP_DIR/T500 win capture.pcapng"
CAP2="$CAP_DIR/t500rs_f1_wheel_rfactor2_f1_1967_bt24_kyalami_1976_online.pcapng"

for c in "$CAP1" "$CAP2"; do
    if [[ ! -f "$c" ]]; then
        echo "Missing capture: $c" >&2
        exit 1
    fi
done

echo "=== Extracting capture 1 OUT reports (host -> 1.6.1) ==="
tshark -r "$CAP1" \
    -Y 'usb.endpoint_address == 0x01 && usb.data_len > 0' \
    -T fields -e frame.number -e frame.time_relative -e usbhid.data \
    > "$OUT_DIR/cap1_out_reports.txt"
echo "  $(wc -l < "$OUT_DIR/cap1_out_reports.txt") packets"

echo "=== Extracting capture 1 IN reports (1.6.0 -> host) ==="
tshark -r "$CAP1" \
    -Y 'usb.src == "1.6.0" && usb.data_len > 0' \
    -T fields -e frame.number -e frame.time_relative -e usbhid.data \
    > "$OUT_DIR/cap1_in_reports.txt"
echo "  $(wc -l < "$OUT_DIR/cap1_in_reports.txt") packets"

echo "=== Extracting capture 2 OUT reports (host -> 2.3.1, T500RS F1 advanced mode) ==="
tshark -r "$CAP2" \
    -Y 'usb.dst == "2.3.1" && usb.data_len > 0' \
    -T fields -e frame.number -e frame.time_relative -e usbhid.data \
    > "$OUT_DIR/cap2_out_reports.txt"
echo "  $(wc -l < "$OUT_DIR/cap2_out_reports.txt") packets"

echo "=== Extracting capture 2 IN reports (2.3.* -> host, all T500RS endpoints) ==="
tshark -r "$CAP2" \
    -Y 'usb.src == "2.3.0" || usb.src == "2.3.2"' \
    -Y 'usb.src contains "2.3." && usb.data_len > 0' \
    -T fields -e frame.number -e frame.time_relative -e usb.src -e usb.dst -e usbhid.data \
    > "$OUT_DIR/cap2_in_all.txt"
echo "  $(wc -l < "$OUT_DIR/cap2_in_all.txt") packets"

echo ""
echo "Done. Frequency summaries:"
echo ""
echo "--- Capture 1 OUT report IDs ---"
awk 'NF==3 {print substr($3,1,2)}' "$OUT_DIR/cap1_out_reports.txt" \
    | sort | uniq -c | sort -rn

echo ""
echo "--- Capture 2 OUT report IDs ---"
awk 'NF==3 {print substr($3,1,2)}' "$OUT_DIR/cap2_out_reports.txt" \
    | sort | uniq -c | sort -rn

echo ""
echo "--- Capture 2 IN report IDs ---"
awk 'NF==5 {print substr($5,1,2)}' "$OUT_DIR/cap2_in_all.txt" \
    | sort | uniq -c | sort -rn
