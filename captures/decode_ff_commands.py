#!/usr/bin/env python3
"""
T500RS Force Feedback Command Decoder
Decodes USB packets from Wireshark/tshark captures
"""

import sys
import re
from typing import Dict, List, Tuple

# Command type mapping from manual analysis
CMD_MAP = {
    0x01: "Duration/Control",
    0x02: "Upload Envelope",
    0x03: "Constant Force Param",
    0x04: "Periodic/Ramp Param",
    0x05: "Condition Param",
    0x06: "Status Query",
    0x0a: "Vendor FF Command",
    0x40: "Control/Status",
    0x41: "Start/Stop Control",
    0x42: "Initialization/Reset"
}

# Effect type codes (byte 2 in command 0x01)
EFFECT_TYPE_MAP = {
    0x00: "Constant Force",
    0x20: "Square Wave",
    0x21: "Triangle Wave",
    0x22: "Sine Wave",
    0x23: "Sawtooth Up",
    0x24: "Sawtooth Down (Ramp)",
    0x40: "Spring",
    0x41: "Damper/Friction/Inertia"
}

def parse_hex_packet(line: str) -> bytes:
    """Extract hex bytes from tshark output line"""
    # Match format: "0000  1b 00 e0 65 ..."
    match = re.search(r'([0-9a-f]{2}(?:\s+[0-9a-f]{2})+)', line, re.IGNORECASE)
    if match:
        hex_str = match.group(1).replace(' ', '')
        return bytes.fromhex(hex_str)
    return b''

def strip_usbpcap_header(data: bytes) -> bytes:
    """Remove 27-byte USBPcap header"""
    if len(data) > 27:
        return data[27:]
    return data

def decode_command_01(data: bytes) -> str:
    """Decode command 0x01: Duration/Control"""
    if len(data) < 15:
        return "Incomplete packet"
    
    effect_type = data[2]
    effect_name = EFFECT_TYPE_MAP.get(effect_type, f"Unknown (0x{effect_type:02x})")
    duration_ms = int.from_bytes(data[4:6], 'little')
    
    return (f"Effect Type: {effect_name}, "
            f"Duration: {duration_ms}ms, "
            f"Raw: {data.hex()}")

def decode_command_02(data: bytes) -> str:
    """Decode command 0x02: Upload Envelope"""
    if len(data) < 9:
        return "Incomplete packet"
    
    # Envelope structure (from manual analysis)
    attack_len = int.from_bytes(data[2:5], 'little')  # 24-bit LE
    attack_level = data[5]
    fade_len = int.from_bytes(data[6:9], 'little')  # 24-bit LE
    # Note: fade level is in byte 9 (not always present)
    
    return (f"Attack: {attack_len}ms @ level {attack_level}/127, "
            f"Fade: {fade_len}ms, "
            f"Raw: {data.hex()}")

def decode_command_03(data: bytes) -> str:
    """Decode command 0x03: Constant Force Parameter"""
    if len(data) < 4:
        return "Incomplete packet"
    
    level = data[3]
    direction = "LEFT" if level < 0x80 else "RIGHT"
    magnitude = level if level < 0x80 else (256 - level)
    
    return (f"Level: {level} ({direction}, magnitude {magnitude}), "
            f"Raw: {data.hex()}")

def decode_command_04(data: bytes) -> str:
    """Decode command 0x04: Periodic/Ramp Parameter"""
    if len(data) < 8:
        return "Incomplete packet"
    
    # Can be periodic (frequency) or ramp (start/end levels)
    param1 = data[3]
    param2 = data[4]
    freq = int.from_bytes(data[6:8], 'little')
    
    if freq > 0:
        freq_hz = freq / 100.0
        return (f"Periodic: Frequency {freq_hz}Hz, "
                f"Params: 0x{param1:02x} 0x{param2:02x}, "
                f"Raw: {data.hex()}")
    else:
        return (f"Ramp: Start 0x{param1:02x}, End 0x{param2:02x}, "
                f"Raw: {data.hex()}")

def decode_command_05(data: bytes) -> str:
    """Decode command 0x05: Condition Parameter"""
    if len(data) < 11:
        return "Incomplete packet"
    
    pos_coef = data[3]
    neg_coef = data[4]
    pos_sat = data[9]
    neg_sat = data[10]
    
    # Second packet (0x05 0x1c) has center/deadband
    if data[1] == 0x1c and len(data) >= 9:
        center = int.from_bytes(data[3:5], 'little', signed=True)
        deadband = data[8]
        return (f"Center: {center}, Deadband: {deadband}, "
                f"Raw: {data.hex()}")
    
    return (f"Pos Coef: {pos_coef}, Neg Coef: {neg_coef}, "
            f"Pos Sat: {pos_sat}, Neg Sat: {neg_sat}, "
            f"Raw: {data.hex()}")

def decode_command_41(data: bytes) -> str:
    """Decode command 0x41: Start/Stop Control"""
    if len(data) < 4:
        return "Incomplete packet"
    
    action = "START" if data[2] == 0x41 else "STOP"
    
    return f"{action} Effect, Raw: {data.hex()}"

def decode_command_42(data: bytes) -> str:
    """Decode command 0x42: Initialization/Reset"""
    return f"Initialize/Reset, Raw: {data.hex()}"

def decode_command_0a(data: bytes) -> str:
    """Decode command 0x0a: Vendor FF Command"""
    if len(data) < 4:
        return "Incomplete packet"
    
    cmd = data[1]
    param = int.from_bytes(data[2:4], 'little')
    
    return (f"Vendor Command: 0x{cmd:02x}, Param: 0x{param:04x} ({param}), "
            f"Raw: {data.hex()}")

def decode_packet(packet_data: bytes) -> str:
    """Main decoder function"""
    if len(packet_data) == 0:
        return "Empty packet"
    
    # Strip USBPcap header if present
    hid_data = strip_usbpcap_header(packet_data)
    
    if len(hid_data) < 1:
        return "No HID data"
    
    cmd_type = hid_data[0]
    cmd_name = CMD_MAP.get(cmd_type, f"Unknown (0x{cmd_type:02x})")
    
    # Decode based on command type
    decoders = {
        0x01: decode_command_01,
        0x02: decode_command_02,
        0x03: decode_command_03,
        0x04: decode_command_04,
        0x05: decode_command_05,
        0x0a: decode_command_0a,
        0x41: decode_command_41,
        0x42: decode_command_42
    }
    
    decoder = decoders.get(cmd_type)
    if decoder:
        details = decoder(hid_data)
    else:
        details = f"Raw: {hid_data.hex()}"
    
    return f"[{cmd_name}] {details}"

def process_capture_file(filename: str):
    """Process a tshark hex dump file"""
    print(f"Processing {filename}...")
    print("=" * 80)
    
    packet_lines = []
    packet_num = 0
    
    with open(filename, 'r') as f:
        for line in f:
            line = line.strip()
            
            # Start of new packet
            if line.startswith('Frame '):
                if packet_lines:
                    # Process previous packet
                    packet_data = b''.join(parse_hex_packet(l) for l in packet_lines)
                    if packet_data:
                        packet_num += 1
                        decoded = decode_packet(packet_data)
                        print(f"\nPacket {packet_num}:")
                        print(f"  {decoded}")
                
                packet_lines = []
            
            # Hex data line
            elif line.startswith('0000 ') or line.startswith('0010 ') or line.startswith('0020 '):
                packet_lines.append(line)
        
        # Process last packet
        if packet_lines:
            packet_data = b''.join(parse_hex_packet(l) for l in packet_lines)
            if packet_data:
                packet_num += 1
                decoded = decode_packet(packet_data)
                print(f"\nPacket {packet_num}:")
                print(f"  {decoded}")
    
    print("\n" + "=" * 80)
    print(f"Total packets decoded: {packet_num}")

def decode_manual_analysis():
    """Decode the manual analysis commands"""
    print("\n" + "=" * 80)
    print("DECODING MANUAL ANALYSIS COMMANDS")
    print("=" * 80)
    
    # Examples from combined_effects_t500.txt
    commands = [
        ("UPLOAD CONSTANT", [
            "02 1c 00 00 00 00 00 00 00",
            "03 0e 00 00",
            "01 00 00 40 69 23 00 ff ff 0e 00 1c 00 00 00"
        ]),
        ("UPLOAD SINE", [
            "02 1c 00 00 00 00 00 00 00",
            "04 0e 00 00 00 00 e8 03",
            "01 00 22 40 17 25 00 ff ff 0e 00 1c 00 00 00"
        ]),
        ("UPLOAD SPRING", [
            "05 0e 00 64 64 00 00 00 00 54 54",
            "05 1c 00 00 00 00 00 00 00 46 54",
            "01 00 40 40 17 25 00 ff ff 0e 00 1c 00 00 00"
        ]),
        ("START EFFECT", ["41 00 41 01"]),
        ("STOP EFFECT", ["41 00 00 01"])
    ]
    
    for effect_name, packets in commands:
        print(f"\n{effect_name}:")
        for i, hex_str in enumerate(packets, 1):
            data = bytes.fromhex(hex_str.replace(' ', ''))
            decoded = decode_packet(data)
            print(f"  Packet {i}: {decoded}")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        process_capture_file(sys.argv[1])
    else:
        # Decode manual analysis commands
        decode_manual_analysis()
        
        # Check if we have capture files
        import os
        capture_file = "attempt_ff_full.txt"
        if os.path.exists(capture_file):
            process_capture_file(capture_file)
        else:
            print(f"\nTo decode a capture file: {sys.argv[0]} <tshark_hex_dump.txt>")
