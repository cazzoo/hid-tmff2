# 08 — tshark recipes (reusable filters)

The exact tshark commands used to produce `00_capture_inventory.md` and friends.
All verified working against Wireshark 4.6.6 (TShark 4.6.6) on USBPcap captures.

## Field name reference

| Field | Meaning |
|-------|---------|
| `usb.src`, `usb.dst` | `Bus.Device.Endpoint` (e.g. `1.6.1`) |
| `usb.endpoint_address` | Endpoint number + direction bit |
| `usb.transfer_type` | `0x01`=interrupt, `0x02`=control, `0x03`=bulk |
| `usb.urb_type` / `usb.urb_function` | URB function code |
| `usb.data_len` | Payload byte count |
| `usbhid.data` | **The actual HID payload bytes (hex)** — this is the key field |
| `usb.bDescriptorType` | `0x01`=device, `0x02`=config, `0x04`=interface, `0x05`=endpoint, `0x21`=HID, `0x22`=HID report |
| `usb.idVendor`, `usb.idProduct`, `usb.bcdDevice` | Device identification |
| `usb.bInterfaceClass`, `usb.bInterfaceSubClass`, `usb.bInterfaceProtocol` | Interface class info (`0x03/0x00/0x00` = plain HID) |
| `usb.endpoint_address`, `usb.bEndpointAddress`, `usb.wMaxPacketSize` | Endpoint info |
| `usb.setup.bmRequestType`, `usb.setup.bRequest`, `usb.setup.wValue`, `usb.setup.wIndex`, `usb.setup.wLength` | SETUP packet fields |
| `frame.time_relative` | Seconds since capture start |
| `frame.number` | 1-indexed frame counter |

> ⚠️ **Gotcha:** `usb.capdata` and `usb.data_fragment` are NOT the right fields for HID
> payloads in tshark 4.x. Use **`usbhid.data`**. If the field is empty, the dissector
> didn't recognise the frame as HID — try `-V` (verbose) to see what's there.

## Common pitfalls

### `usb.dst contains "1.6.1"` matches nothing

The `contains` operator expects a string, but `usb.dst` parses as a hierarchical
identifier. Use **exact equality** (`usb.dst == "1.6.1"`) or pre-filter on the
endpoint address:

```bash
# Either of these works:
tshark -r f.pcapng -Y 'usb.dst == "1.6.1"' ...
tshark -r f.pcapng -Y 'usb.endpoint_address == 0x01 && usb.data_len > 0' ...
```

### Verbose `-V` reveals the right field name

When unsure what field holds the data:

```bash
tshark -r f.pcapng -Y 'frame.number == 55' -V | grep -E '(HID|Data|Fragment)'
```

Look for `HID Data: 4204` — the field name is `usbhid.data` and the value is the
hex payload.

## Recipes

### 1. Capture summary (replaces `capinfos`)

```bash
capinfos capture.pcapng
```

### 2. List all USB devices in a capture

```bash
tshark -r capture.pcapng -Y 'usb.bDescriptorType == 0x01 && usb.idVendor' \
  -T fields -e frame.number -e usb.src -e usb.dst \
       -e usb.idVendor -e usb.idProduct -e usb.bcdDevice -e usb.bNumInterfaces \
  | sort -u
```

### 3. List all HID interfaces and their endpoints

```bash
tshark -r capture.pcapng -Y 'usb.bDescriptorType == 0x04' \
  -T fields -e frame.number -e usb.src -e usb.bInterfaceNumber \
       -e usb.bInterfaceClass -e usb.bInterfaceSubClass -e usb.bInterfaceProtocol \
       -e usb.bEndpointAddress -e usb.wMaxPacketSize
```

### 4. Extract ALL OUT (host → device) HID payloads, sorted by report ID

```bash
tshark -r capture.pcapng \
  -Y 'usb.endpoint_address == 0x01 && usb.data_len > 0' \
  -T fields -e frame.number -e frame.time_relative -e usbhid.data \
  > /tmp/out.txt

# Frequency by report ID (first byte)
awk 'NF==3 {print substr($3,1,2)}' /tmp/out.txt | sort | uniq -c | sort -rn

# Frequency by 3-byte prefix
awk 'NF==3 {print substr($3,1,6)}' /tmp/out.txt | sort | uniq -c | sort -rn | head -20
```

### 5. Extract ALL IN (device → host) HID payloads

```bash
# endpoint 0x82 is IN interrupt for T500RS
tshark -r capture.pcapng \
  -Y 'usb.endpoint_address == 0x82 && usb.data_len > 0' \
  -T fields -e frame.number -e frame.time_relative -e usbhid.data \
  > /tmp/in.txt

awk 'NF==3 {print substr($3,1,6)}' /tmp/in.txt | sort | uniq -c | sort -rn | head
```

### 6. Decode a 15-byte `0x01` main upload by hand

```bash
awk '$3 ~ /^01/ {
  printf "f%s t=%s | id=%s eff_id=%s type=%s ctl=%s dur=%s%s delay=%s%s b8=%s psub=0x%s%s esub=0x%s%s\n", \
    $1, $2, \
    substr($3,1,2), substr($3,3,2), substr($3,5,2), substr($3,7,2), \
    substr($3,11,2), substr($3,9,2), \
    substr($3,15,2), substr($3,13,2), \
    substr($3,17,2), \
    substr($3,21,2), substr($3,19,2), \
    substr($3,25,2), substr($3,23,2)
}' /tmp/out.txt
```

This decodes the LE u16 fields with the high byte first (because the hex string is
byte-ordered, but LE displays low-byte first).

### 7. Find boot-mode-switch vendor requests (capture 2 only)

```bash
tshark -r capture2.pcapng \
  -Y 'frame.time_relative <= 12 && usb.setup.bmRequestType >= 0x40' \
  -T fields -e frame.number -e frame.time_relative -e usb.src -e usb.dst \
       -e usb.setup.bmRequestType -e usb.setup.bRequest \
       -e usb.setup.wValue -e usb.setup.wIndex -e usb.setup.wLength \
       -e usbhid.data
```

The `bmRequestType >= 0x40` filter catches vendor (0x40-) and reserved (0x80-)
request types. The boot-mode-switch is `bmRequestType=0x41 bRequest=0x53`.

### 8. Show a single frame as raw hex (to verify SETUP bytes)

```bash
tshark -r capture.pcapng -Y 'frame.number == 147' -x
```

The 8-byte SETUP packet starts after the 27-byte USBPcap header (offset 0x1c).

### 9. Time-bucketed packet rate

```bash
# Packets per minute for the 0x04 stream (constant-force updates)
awk '$3 ~ /^04/ {bucket=int($2/60); print bucket}' /tmp/out.txt \
  | sort -n | uniq -c
```

### 10. Confirm a struct layout assumption

To verify the driver's struct `t500rs_pkt_r04_periodic_ramp` matches reality,
extract all unique 8-byte `0x04` bodies and inspect manually:

```bash
awk '$3 ~ /^04/ {print substr($3,1,16)}' /tmp/out.txt \
  | sort | uniq -c | sort -rn | head -10
```

Compare each byte position to the struct in `hid-tmt500rs.h:151-159`.

## Generating visual timelines

For a quick visual scan of when each report type fires:

```bash
# Produce a CSV of (time, report_id) for plotting
awk 'NF==3 {print $2 "," substr($3,1,2)}' /tmp/out.txt > /tmp/timeline.csv

# Then in gnuplot / Excel / Python:
# histogram of report_id over time, or scatter of (time, id)
```
