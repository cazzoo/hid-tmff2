#!/usr/bin/env python3
"""
T500RS Force Feedback Test - Working Python Version
Mirrors the C implementation exactly
"""
import struct
import fcntl
import os
import sys
import time
import glob

# IOCTLs
EVIOCSFF = 0x40304580  # Upload effect
EVIOCRMFF = 0x40044581  # Remove effect

# Event types
EV_FF = 0x15

# Effect types (from linux/input.h)
FF_RUMBLE = 0x50
FF_PERIODIC = 0x51
FF_CONSTANT = 0x52
FF_SPRING = 0x53
FF_FRICTION = 0x54
FF_DAMPER = 0x55
FF_INERTIA = 0x56
FF_RAMP = 0x57

def create_ff_effect_constant(level, duration_ms=2000):
    """
    Create struct ff_effect for constant force
    
    struct ff_effect {
        __u16 type;                 // offset 0, size 2
        __s16 id;                   // offset 2, size 2
        __u16 direction;            // offset 4, size 2
        struct ff_trigger trigger;  // offset 6, size 4 (2x __u16)
        struct ff_replay replay;    // offset 10, size 4 (2x __u16)
        // padding to offset 16
        union {
            struct ff_constant_effect constant; // __s16 level + struct ff_envelope
        } u;
    };
    Total: 48 bytes
    """
    effect = bytearray(48)
    
    # Pack fields exactly as C struct
    # type (u16 at offset 0)
    struct.pack_into('H', effect, 0, FF_CONSTANT)
    
    # id (s16 at offset 2) - set to -1 for auto-assign
    struct.pack_into('h', effect, 2, -1)
    
    # direction (u16 at offset 4) - 0x4000 = 90 degrees (same as C)
    struct.pack_into('H', effect, 4, 0x4000)
    
    # trigger.button (u16 at offset 6)
    struct.pack_into('H', effect, 6, 0)
    
    # trigger.interval (u16 at offset 8)
    struct.pack_into('H', effect, 8, 0)
    
    # replay.length (u16 at offset 10)
    struct.pack_into('H', effect, 10, duration_ms)
    
    # replay.delay (u16 at offset 12)
    struct.pack_into('H', effect, 12, 0)
    
    # Padding bytes 14-15 (implicit, already 0)
    
    # u.constant.level (s16 at offset 16)
    struct.pack_into('h', effect, 16, level)
    
    # u.constant.envelope (4x u16 at offset 18)
    struct.pack_into('HHHH', effect, 18, 0, 0, 0, 0)
    
    return effect

def upload_effect(fd, effect_bytes):
    """Upload effect and return assigned ID"""
    # Create mutable buffer for ioctl
    buf = bytearray(effect_bytes)
    
    # Upload effect - ioctl will modify buf to set the ID
    try:
        fcntl.ioctl(fd, EVIOCSFF, buf)
    except Exception as e:
        print(f"Failed to upload effect: {e}")
        return -1
    
    # Extract assigned ID from offset 2
    effect_id = struct.unpack_from('h', buf, 2)[0]
    return effect_id

def play_effect(fd, effect_id):
    """Send EV_FF event to play effect"""
    # struct input_event: 
    # - struct timeval time (8 bytes on 64-bit: long sec, long usec)
    # - __u16 type
    # - __u16 code  
    # - __s32 value
    # Total: 24 bytes on 64-bit
    event = struct.pack('llHHi', 0, 0, EV_FF, effect_id, 1)
    written = os.write(fd, event)
    return written == len(event)

def stop_effect(fd, effect_id):
    """Send EV_FF event to stop effect"""
    event = struct.pack('llHHi', 0, 0, EV_FF, effect_id, 0)
    os.write(fd, event)

def find_device():
    """Find T500RS device"""
    for path in sorted(glob.glob('/dev/input/event*')):
        try:
            with open(path, 'rb') as f:
                buf = bytearray(256)
                fcntl.ioctl(f.fileno(), 0x80ff4506, buf)  # EVIOCGNAME
                name = buf.split(b'\x00')[0].decode('utf-8')
                if 'T500RS' in name or ('T500' in name and 'Force Feedback' in name):
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
        device = sys.argv[1]
    else:
        device = find_device()
        if not device:
            print("❌ Could not find T500RS device")
            print("Usage: sudo ./test_ff_working.py [/dev/input/eventX]")
            return 1
    
    print(f"Using device: {device}")
    print()
    
    # Open device
    try:
        fd = os.open(device, os.O_RDWR)
    except Exception as e:
        print(f"❌ Failed to open device: {e}")
        print("Make sure to run with sudo!")
        return 1
    
    print("✅ Device opened")
    print()
    
    try:
        # Test 1: Weak force
        print("=" * 50)
        print("Test 1: Weak Constant Force (4096)")
        print("=" * 50)
        effect = create_ff_effect_constant(4096, 2000)
        effect_id = upload_effect(fd, effect)
        if effect_id < 0:
            print("Failed to upload effect")
            return 1
        print(f"Uploaded effect ID: {effect_id}")
        print("Playing for 2 seconds...")
        print(">>> DO YOU FEEL THE FORCE? <<<")
        play_effect(fd, effect_id)
        time.sleep(2)
        stop_effect(fd, effect_id)
        print()
        time.sleep(1)
        
        # Test 2: Strong force
        print("=" * 50)
        print("Test 2: Strong Constant Force (16384)")
        print("=" * 50)
        effect = create_ff_effect_constant(16384, 2000)
        effect_id = upload_effect(fd, effect)
        print(f"Uploaded effect ID: {effect_id}")
        print("Playing for 2 seconds...")
        print(">>> STRONGER FORCE! <<<")
        play_effect(fd, effect_id)
        time.sleep(2)
        stop_effect(fd, effect_id)
        print()
        time.sleep(1)
        
        # Test 3: MAXIMUM force
        print("=" * 50)
        print("Test 3: MAXIMUM Constant Force (32767)")
        print("=" * 50)
        effect = create_ff_effect_constant(32767, 3000)
        effect_id = upload_effect(fd, effect)
        print(f"Uploaded effect ID: {effect_id}")
        print("Playing for 3 seconds...")
        print(">>> MAXIMUM FORCE! HOLD THE WHEEL! <<<")
        play_effect(fd, effect_id)
        time.sleep(3)
        stop_effect(fd, effect_id)
        print()
        
        print("=" * 50)
        print("✅ All tests complete!")
        print("=" * 50)
        
    finally:
        os.close(fd)
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
