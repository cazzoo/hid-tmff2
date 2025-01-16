# USB Communication Analysis

This directory contains detailed analysis of USB captures for the TMT500RS wheel. The analysis files are generated from USB packet captures (.pcapng files) and provide insights into the device's communication patterns.

## Files Overview

### Device Initialization Analysis (`device_init_detailed.txt`)
Contains USB traffic during device initialization phase, showing:
- Frame number (packet sequence)
- Source and destination addresses
- USB transfer type (Control, Interrupt, etc.)
- Endpoint address
- Device address and Bus ID

Key patterns to look for:
- Control transfers for device configuration (0x02)
- Interrupt endpoint setup (0x01)
- Device mode initialization
- Bulk data transfers (0x02)
- IN/OUT endpoint configuration (0x80/0x00)

### Plug-in Analysis (`plug_t500_in_detailed.txt`)
Contains USB traffic when the device is first plugged in, showing:
- Frame number (packet sequence)
- Source and destination addresses
- USB transfer type (Control, Interrupt, etc.)
- Endpoint address
- Device address and Bus ID

Key patterns to look for:
- Initial enumeration sequence
- Device descriptor requests
- Configuration settings
- Interface and endpoint setup

### Constant Force Analysis (`t500rs_constant_force_detailed.txt`)
Contains USB traffic during constant force feedback operation, showing:
- Frame number (packet sequence)
- Source and destination addresses
- USB transfer type
- Endpoint address
- Device address and Bus ID
- Force feedback commands in hex

Key patterns to look for:
- Force magnitude changes (look for patterns in the capdata field)
- Command structure:
  - `4100xxxx`: Control commands
  - `030exxxx`: Force feedback commands
- Timing between commands
- Response patterns from the device

## Understanding the Fields

1. `frame.number`: Sequential packet number in the capture
2. `usb.src`: Source address of the USB packet
3. `usb.dst`: Destination address of the USB packet
4. `usb.transfer_type`: Type of USB transfer
   - 0x02: Bulk Transfer
   - 0x01: Interrupt Transfer
   - 0x00: Control Transfer
5. `usb.endpoint_address`: USB endpoint number and direction
   - 0x80: IN endpoint (device to host)
   - 0x00: OUT endpoint (host to device)
   - 0x82: IN endpoint 2 (often used for status)
6. `usb.device_address`: Address assigned to the device on the USB bus
7. `usb.bus_id`: USB bus identifier
8. `usb.capdata`: The actual data payload in hexadecimal (when available)

## Common USB Endpoints

- Endpoint 0: Control transfers (device configuration)
- Endpoint 1: Usually IN endpoint for status reports
- Endpoint 2: Usually OUT endpoint for commands
- Higher endpoints: Depend on device configuration

## Transfer Types

0x02: Bulk Transfer
- Used for reliable data transfer
- No guaranteed timing
- Common in device initialization

0x01: Interrupt Transfer
- Used for periodic, small data transfers
- Guaranteed latency
- Common in force feedback and status reports

0x00: Control Transfer
- Used for device configuration
- Setup packets and device management
- Always on endpoint 0

## Usage Tips

1. Look for patterns in the captured data:
   - Repeated commands during force feedback
   - Initialization sequences
   - Status report formats

2. Pay attention to transfer types:
   - Control transfers (0x02) for device setup
   - Interrupt transfers (0x01) for force feedback
   - Bulk transfers (0x02) for data transmission

3. Note the timing between packets:
   - How frequently force updates are sent
   - Response times from the device

4. Command Analysis:
   - First byte often indicates command type
   - Following bytes contain parameters
   - Look for patterns in similar commands

## Notes

- All captured data is in hexadecimal format
- Endpoint addresses include direction (IN/OUT)
- Transfer types indicate how the device communicates
- Frame numbers help track packet sequence
- Device addresses and bus IDs help track multiple USB devices
- Captured data field is currently only available in the constant force analysis 