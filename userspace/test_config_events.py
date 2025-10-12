#!/usr/bin/env python3
"""
Test script to verify configuration events are being sent correctly
"""

import sys
import os
import struct
import glob
import fcntl
import time

# Custom event codes (0xC0-0xCF range to avoid effect ID conflicts)
FF_TOGGLE_SMOOTHING = 0xC0
FF_TOGGLE_MIXING = 0xC1
FF_TOGGLE_DYNAMIC_RATE = 0xC2
FF_GET_CONFIG = 0xC3

# Event types
EV_FF = 0x15

def find_device():
    """Find T500RS device"""
    for event_file in glob.glob('/dev/input/event*'):
        try:
            with open(event_file, 'rb') as f:
                name_buf = bytearray(256)
                fcntl.ioctl(f, 0x80ff4506, name_buf)
                name = name_buf.split(b'\x00')[0].decode('utf-8', errors='ignore')
                
                if 'T500RS' in name or 'Force Feedback Wheel' in name:
                    print(f"Found T500RS: {event_file} ({name})")
                    return event_file
        except (IOError, OSError):
            continue
    return None

def send_event(device_path, event_code, value):
    """Send a force feedback event"""
    # struct input_event (24 bytes on 64-bit)
    event = struct.pack('llHHi', 0, 0, EV_FF, event_code, value)
    
    print(f"Sending event to {device_path}:")
    print(f"  Type: EV_FF (0x{EV_FF:02x})")
    print(f"  Code: 0x{event_code:02x} ({event_code})")
    print(f"  Value: {value}")
    print(f"  Event bytes: {event.hex()}")
    
    try:
        with open(device_path, 'wb') as f:
            f.write(event)
            f.flush()
        print("  ✓ Event sent successfully")
        return True
    except Exception as e:
        print(f"  ✗ Error: {e}")
        return False

def main():
    device = find_device()
    if not device:
        print("ERROR: T500RS device not found!")
        sys.exit(1)
    
    print("\n" + "="*60)
    print("Testing Configuration Events")
    print("="*60)
    print("\nMake sure the driver is running and check its output!")
    print("You should see log messages for each event.\n")
    
    # Test each configuration event
    tests = [
        ("Get Config", FF_GET_CONFIG, 1),
        ("Disable Force Smoothing", FF_TOGGLE_SMOOTHING, 0),
        ("Enable Force Smoothing", FF_TOGGLE_SMOOTHING, 1),
        ("Disable Multi-Effect Mixing", FF_TOGGLE_MIXING, 0),
        ("Enable Multi-Effect Mixing", FF_TOGGLE_MIXING, 1),
        ("Disable Dynamic Update Rate", FF_TOGGLE_DYNAMIC_RATE, 0),
        ("Enable Dynamic Update Rate", FF_TOGGLE_DYNAMIC_RATE, 1),
        ("Get Config Again", FF_GET_CONFIG, 1),
    ]
    
    for i, (name, code, value) in enumerate(tests, 1):
        print(f"\n[{i}/{len(tests)}] {name}")
        print("-" * 60)
        send_event(device, code, value)
        time.sleep(0.5)  # Wait a bit between events
    
    print("\n" + "="*60)
    print("Test complete!")
    print("="*60)
    print("\nCheck the driver output to verify events were received.")

if __name__ == '__main__':
    main()

