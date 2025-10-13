#!/usr/bin/env python3
"""
Record and Replay Force Feedback Events

This tool can record FF events from a game and replay them later
for debugging without needing to run the game.

Usage:
    # Record from game
    sudo python3 record_replay_ff.py record output.ff

    # Replay recorded session
    sudo python3 record_replay_ff.py replay output.ff [device]
"""

import sys
import struct
import glob
import fcntl
import time
import json

# Event types
EV_FF = 0x15
EV_UINPUT = 0x0101

def find_device():
    """Find T500RS device"""
    for event_file in glob.glob('/dev/input/event*'):
        try:
            with open(event_file, 'rb') as f:
                name_buf = bytearray(256)
                fcntl.ioctl(f, 0x80ff4506, name_buf)
                name = name_buf.split(b'\x00')[0].decode('utf-8', errors='ignore')
                
                if 'T500RS' in name or 'Force Feedback Wheel' in name:
                    return event_file
        except (IOError, OSError):
            continue
    return None

def record_events(output_file, device_path=None):
    """Record FF events to a file"""
    if not device_path:
        device_path = find_device()
    
    if not device_path:
        print("ERROR: T500RS device not found!")
        return 1
    
    print(f"Recording from: {device_path}")
    print("Press Ctrl+C to stop recording")
    print()
    
    events = []
    start_time = None
    
    try:
        with open(device_path, 'rb') as f:
            while True:
                data = f.read(24)
                if len(data) != 24:
                    continue
                
                sec, usec, ev_type, ev_code, ev_value = struct.unpack('llHHi', data)
                
                # Only record FF and UINPUT events
                if ev_type not in [EV_FF, EV_UINPUT]:
                    continue
                
                # Record timestamp relative to start
                if start_time is None:
                    start_time = (sec, usec)
                    rel_time = 0.0
                else:
                    rel_time = (sec - start_time[0]) + (usec - start_time[1]) / 1000000.0
                
                event = {
                    'time': rel_time,
                    'type': ev_type,
                    'code': ev_code,
                    'value': ev_value
                }
                events.append(event)
                
                # Show what we're recording
                if ev_type == EV_FF:
                    if ev_code == 0x60:  # FF_GAIN
                        print(f"[{rel_time:.3f}s] GAIN: {ev_value}")
                    elif ev_code == 0x61:  # FF_AUTOCENTER
                        print(f"[{rel_time:.3f}s] AUTOCENTER: {ev_value}")
                    elif ev_value > 0:
                        print(f"[{rel_time:.3f}s] START effect {ev_code}")
                    else:
                        print(f"[{rel_time:.3f}s] STOP effect {ev_code}")
                
    except KeyboardInterrupt:
        print(f"\n\nRecorded {len(events)} events")
        
        # Save to file
        with open(output_file, 'w') as f:
            json.dump({
                'device': device_path,
                'duration': events[-1]['time'] if events else 0,
                'event_count': len(events),
                'events': events
            }, f, indent=2)
        
        print(f"Saved to: {output_file}")
        return 0

def replay_events(input_file, device_path=None):
    """Replay FF events from a file"""
    # Load events
    with open(input_file, 'r') as f:
        data = json.load(f)
    
    events = data['events']
    print(f"Loaded {len(events)} events")
    print(f"Duration: {data['duration']:.1f}s")
    print()
    
    if not device_path:
        device_path = find_device()
    
    if not device_path:
        print("ERROR: T500RS device not found!")
        return 1
    
    print(f"Replaying to: {device_path}")
    print("Press Ctrl+C to stop")
    print()
    
    try:
        with open(device_path, 'wb') as f:
            start_time = time.time()
            
            for event in events:
                # Wait until the right time
                target_time = start_time + event['time']
                now = time.time()
                if now < target_time:
                    time.sleep(target_time - now)
                
                # Send event
                ev_data = struct.pack('llHHi', 0, 0, 
                                     event['type'], event['code'], event['value'])
                f.write(ev_data)
                f.flush()
                
                # Show what we're replaying
                if event['type'] == EV_FF:
                    if event['code'] == 0x60:
                        print(f"[{event['time']:.3f}s] GAIN: {event['value']}")
                    elif event['code'] == 0x61:
                        print(f"[{event['time']:.3f}s] AUTOCENTER: {event['value']}")
                    elif event['value'] > 0:
                        print(f"[{event['time']:.3f}s] START effect {event['code']}")
                    else:
                        print(f"[{event['time']:.3f}s] STOP effect {event['code']}")
        
        print("\nReplay complete!")
        return 0
        
    except KeyboardInterrupt:
        print("\nReplay stopped")
        return 0

def main():
    if len(sys.argv) < 3:
        print("Usage:")
        print("  Record: sudo python3 record_replay_ff.py record output.ff")
        print("  Replay: sudo python3 record_replay_ff.py replay input.ff [device]")
        return 1
    
    mode = sys.argv[1]
    filename = sys.argv[2]
    device = sys.argv[3] if len(sys.argv) > 3 else None
    
    if mode == 'record':
        return record_events(filename, device)
    elif mode == 'replay':
        return replay_events(filename, device)
    else:
        print(f"Unknown mode: {mode}")
        return 1

if __name__ == '__main__':
    sys.exit(main())

