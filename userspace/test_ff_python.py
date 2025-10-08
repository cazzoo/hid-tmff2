#!/usr/bin/env python3
"""
T500RS Force Feedback Test - Python Version
Tests force feedback using proper struct ff_effect format
"""
import struct
import fcntl
import os
import sys
import time

# Effect types
FF_CONSTANT = 0x50
FF_SPRING = 0x52
FF_PERIODIC = 0x51
FF_SINE = 0x58

# Event types
EV_FF = 0x15

# IOCTLs
EVIOCSFF = 0x40304580  # Upload effect
EVIOCRMFF = 0x40044581  # Remove effect

def create_constant_effect(level, duration_ms=2000):
    """Create a constant force effect using proper struct ff_effect layout"""
    # struct ff_effect is 48 bytes
    effect = bytearray(48)
    
    # u16 type
    struct.pack_into('H', effect, 0, FF_CONSTANT)
    # s16 id (-1 = auto-assign)
    struct.pack_into('h', effect, 2, -1)
    # u16 direction (0x4000 = 90 degrees)
    struct.pack_into('H', effect, 4, 0x4000)
    
    # struct ff_trigger (button, interval) at offset 6
    struct.pack_into('HH', effect, 6, 0, 0)
    
    # struct ff_replay (length, delay) at offset 10
    struct.pack_into('HH', effect, 10, duration_ms, 0)
    
    # union starts at offset 16
    # For FF_CONSTANT: s16 level at offset 16
    struct.pack_into('h', effect, 16, level)
    
    # struct ff_envelope at offset 18 (attack_length, attack_level, fade_length, fade_level)
    struct.pack_into('HHHH', effect, 18, 0, 0, 0, 0)
    
    return effect

def upload_effect(fd, effect_data):
    """Upload an effect and return its ID"""
    result = bytearray(effect_data)
    fcntl.ioctl(fd, EVIOCSFF, result)
    # Extract the assigned ID
    effect_id = struct.unpack_from('h', result, 2)[0]
    return effect_id

def play_effect(fd, effect_id):
    """Play an effect"""
    # struct input_event: timeval(16 bytes) + type(2) + code(2) + value(4) = 24 bytes
    event = struct.pack('llHHi', 0, 0, EV_FF, effect_id, 1)
    os.write(fd, event)

def stop_effect(fd, effect_id):
    """Stop an effect"""
    event = struct.pack('llHHi', 0, 0, EV_FF, effect_id, 0)
    os.write(fd, event)

def remove_effect(fd, effect_id):
    """Remove an effect from device"""
    try:
        # EVIOCRMFF expects just the effect ID
        result = fcntl.ioctl(fd, EVIOCRMFF, effect_id)
    except OSError:
        # Some devices don't support removal, that's okay
        pass

def find_t500rs_device():
    """Find T500RS device automatically"""
    import glob
    
    for path in sorted(glob.glob('/dev/input/event*')):
        try:
            with open(path, 'rb') as f:
                # EVIOCGNAME
                buf = bytearray(256)
                fcntl.ioctl(f.fileno(), 0x80ff4506, buf)
                name = buf.split(b'\x00')[0].decode('utf-8')
                
                if 'T500RS Force Feedback Wheel' in name or 'T500' in name:
                    return path
        except:
            continue
    
    return None

def main():
    print("=" * 50)
    print("T500RS Force Feedback Test - Python")
    print("=" * 50)
    print()
    
    # Find device
    if len(sys.argv) > 1:
        device_path = sys.argv[1]
        print(f"Using device: {device_path}")
    else:
        device_path = find_t500rs_device()
        if not device_path:
            print("❌ Could not find T500RS device")
            print()
            print("Run with device path:")
            print(f"  sudo {sys.argv[0]} /dev/input/eventX")
            return 1
        print(f"✅ Found device: {device_path}")
    
    print()
    
    # Open device
    try:
        fd = os.open(device_path, os.O_RDWR)
    except Exception as e:
        print(f"❌ Failed to open device: {e}")
        print("Make sure to run with sudo!")
        return 1
    
    print("✅ Device opened")
    print()
    
    try:
        # Test 1: Weak constant force
        print("=" * 50)
        print("Test 1: Weak Constant Force (4096)")
        print("=" * 50)
        effect = create_constant_effect(4096, 2000)
        effect_id = upload_effect(fd, effect)
        print(f"Uploaded effect ID: {effect_id}")
        print("Playing for 2 seconds...")
        print(">>> DO YOU FEEL THE FORCE? <<<")
        play_effect(fd, effect_id)
        time.sleep(2)
        stop_effect(fd, effect_id)
        remove_effect(fd, effect_id)
        print()
        time.sleep(1)
        
        # Test 2: Strong constant force
        print("=" * 50)
        print("Test 2: Strong Constant Force (16384)")
        print("=" * 50)
        effect = create_constant_effect(16384, 2000)
        effect_id = upload_effect(fd, effect)
        print(f"Uploaded effect ID: {effect_id}")
        print("Playing for 2 seconds...")
        print(">>> FEEL THE STRONGER FORCE! <<<")
        play_effect(fd, effect_id)
        time.sleep(2)
        stop_effect(fd, effect_id)
        remove_effect(fd, effect_id)
        print()
        time.sleep(1)
        
        # Test 3: MAXIMUM constant force
        print("=" * 50)
        print("Test 3: MAXIMUM Constant Force (32767)")
        print("=" * 50)
        effect = create_constant_effect(32767, 3000)
        effect_id = upload_effect(fd, effect)
        print(f"Uploaded effect ID: {effect_id}")
        print("Playing for 3 seconds...")
        print(">>> HOLD THE WHEEL FIRMLY! MAXIMUM FORCE! <<<")
        play_effect(fd, effect_id)
        time.sleep(3)
        stop_effect(fd, effect_id)
        remove_effect(fd, effect_id)
        print()
        
        print("=" * 50)
        print("✅ All tests complete!")
        print("=" * 50)
        
    finally:
        os.close(fd)
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
