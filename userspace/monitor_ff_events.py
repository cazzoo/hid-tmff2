#!/usr/bin/env python3
"""
Monitor Force Feedback Events from Games

This tool monitors all FF-related events sent by games to the T500RS device.
It helps debug why games might not be triggering force feedback.

Usage:
    python3 monitor_ff_events.py [device]
"""

import sys
import struct
import glob
import fcntl
import time

# Event types
EV_FF = 0x15
EV_UINPUT = 0x0101

# FF codes
FF_GAIN = 0x60
FF_AUTOCENTER = 0x61

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

def monitor_events(device_path):
    """Monitor events from the device"""
    print(f"\nMonitoring events from: {device_path}")
    print("="*80)
    print("Waiting for events from game...")
    print("(Start your game and trigger force feedback)")
    print("="*80)
    print()
    
    event_count = 0
    ff_event_count = 0
    upload_count = 0
    erase_count = 0
    
    with open(device_path, 'rb') as f:
        while True:
            try:
                # Read event (24 bytes on 64-bit)
                data = f.read(24)
                if len(data) != 24:
                    continue
                
                # Unpack: struct input_event
                sec, usec, ev_type, ev_code, ev_value = struct.unpack('llHHi', data)
                
                # Only count FF and UINPUT events, ignore input events
                if ev_type not in [EV_FF, EV_UINPUT]:
                    continue

                event_count += 1

                # Filter for FF-related events
                if ev_type == EV_FF:
                    ff_event_count += 1
                    timestamp = f"{sec}.{usec:06d}"
                    
                    # Decode event
                    if ev_code == FF_GAIN:
                        percent = (ev_value * 100.0) / 65535
                        print(f"[{timestamp}] FF_GAIN: {ev_value} ({percent:.1f}%)")
                    elif ev_code == FF_AUTOCENTER:
                        percent = (ev_value * 100.0) / 65535
                        print(f"[{timestamp}] FF_AUTOCENTER: {ev_value} ({percent:.1f}%)")
                    elif ev_value > 0:
                        print(f"[{timestamp}] START effect {ev_code} (value={ev_value})")
                    elif ev_value == 0:
                        print(f"[{timestamp}] STOP effect {ev_code}")
                    else:
                        print(f"[{timestamp}] EV_FF: code={ev_code}, value={ev_value}")
                
                elif ev_type == EV_UINPUT:
                    if ev_code == 0:  # UI_FF_UPLOAD
                        upload_count += 1
                        print(f"[{sec}.{usec:06d}] UPLOAD effect (request_id={ev_value})")
                    elif ev_code == 1:  # UI_FF_ERASE
                        erase_count += 1
                        print(f"[{sec}.{usec:06d}] ERASE effect (request_id={ev_value})")
                
                # Show stats every 100 events
                if event_count % 100 == 0:
                    print(f"\n[Stats] Total: {event_count}, FF: {ff_event_count}, Upload: {upload_count}, Erase: {erase_count}\n")
                
            except KeyboardInterrupt:
                print("\n\n" + "="*80)
                print("Monitoring stopped")
                print("="*80)
                print(f"Total events: {event_count}")
                print(f"FF events: {ff_event_count}")
                print(f"Upload events: {upload_count}")
                print(f"Erase events: {erase_count}")
                print("="*80)
                break
            except Exception as e:
                print(f"Error: {e}")
                break

def main():
    device_path = sys.argv[1] if len(sys.argv) > 1 else None
    
    if not device_path:
        device_path = find_device()
    
    if not device_path:
        print("ERROR: T500RS device not found!")
        print("\nAvailable devices:")
        for event_file in glob.glob('/dev/input/event*'):
            try:
                with open(event_file, 'rb') as f:
                    name_buf = bytearray(256)
                    fcntl.ioctl(f, 0x80ff4506, name_buf)
                    name = name_buf.split(b'\x00')[0].decode('utf-8', errors='ignore')
                    print(f"  {event_file}: {name}")
            except:
                pass
        sys.exit(1)
    
    try:
        monitor_events(device_path)
    except PermissionError:
        print(f"\nERROR: Permission denied reading {device_path}")
        print("Try running with sudo:")
        print(f"  sudo python3 {sys.argv[0]} {device_path}")
        sys.exit(1)
    except Exception as e:
        print(f"\nERROR: {e}")
        sys.exit(1)

if __name__ == '__main__':
    main()

