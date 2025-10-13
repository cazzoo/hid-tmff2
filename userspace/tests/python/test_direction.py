#!/usr/bin/env python3
"""
Test script to determine ACTUAL force direction behavior on T500RS
"""
import sys, os, struct, fcntl, glob, time

# Find T500RS Force Feedback device
device_path = None
for path in sorted(glob.glob('/dev/input/event*')):
    try:
        with open(path, 'rb') as f:
            buf = bytearray(256)
            fcntl.ioctl(f.fileno(), 0x80ff4506, buf)
            name = buf.split(b'\x00')[0].decode('utf-8')
            
            if 'T500RS Force Feedback Wheel' in name:
                device_path = path
                print(f"✅ Found: {path} - {name}")
                break
    except:
        continue

if not device_path:
    print("❌ T500RS Force Feedback device not found!")
    print("Make sure the t500rs-ffb driver is running!")
    sys.exit(1)

# Open device
fd = os.open(device_path, os.O_RDWR)
print(f"✅ Opened {device_path}\n")

# Create FF_CONSTANT effect structure
def create_constant_effect(level):
    """Create FF_CONSTANT effect with given level"""
    effect = bytearray(48)
    struct.pack_into('H', effect, 0, 0x52)  # FF_CONSTANT = 0x52
    struct.pack_into('h', effect, 2, -1)    # auto-assign ID
    struct.pack_into('H', effect, 4, 0x4000)  # direction (unused by T500RS)
    struct.pack_into('HH', effect, 6, 0, 0)   # trigger
    struct.pack_into('HH', effect, 10, 2000, 0)  # replay (2 seconds)
    struct.pack_into('h', effect, 16, level)  # level
    struct.pack_into('HHHH', effect, 18, 0, 0, 0, 0)  # envelope
    return effect

# Test different force levels
tests = [
    (25000, "POSITIVE +25000"),
    (-25000, "NEGATIVE -25000"),
]

print("=" * 70)
print("DIRECTION TEST - Watch your wheel carefully!")
print("=" * 70)
print()

EV_FF = 0x15
EVIOCSFF = 0x40304580

for level, description in tests:
    print(f"📊 Testing: {description}")
    print(f"   Expected based on driver code (buf[2]):")
    if level < 0:
        print(f"   • Negative → buf[2]=0x00 → Should pull RIGHT (according to your tests)")
    else:
        print(f"   • Positive → buf[2]=0x41 → Should pull LEFT (according to your tests)")
    
    # Upload effect
    buf = bytearray(create_constant_effect(level))
    fcntl.ioctl(fd, EVIOCSFF, buf)
    effect_id = struct.unpack_from('h', buf, 2)[0]
    print(f"   ✅ Uploaded effect ID: {effect_id}")
    
    # Play effect
    event = struct.pack('llHHi', 0, 0, EV_FF, effect_id, 1)
    os.write(fd, event)
    print(f"   ▶️  Playing effect...")
    print()
    
    input(f"   ❓ WHICH WAY DID THE WHEEL PULL? (Press Enter to continue)")
    
    # Stop effect
    event = struct.pack('llHHi', 0, 0, EV_FF, effect_id, 0)
    os.write(fd, event)
    print(f"   ⏹️  Stopped effect\n")
    
    # Try to remove (for cleanup)
    try:
        EVIOCRMFF = 0x40044581
        effect_id_buf = struct.pack('h', effect_id) + b'\x00' * 46
        fcntl.ioctl(fd, EVIOCRMFF, effect_id_buf)
    except:
        pass
    
    print("-" * 70)
    print()

os.close(fd)

print("=" * 70)
print("TEST COMPLETE!")
print("=" * 70)
print()
print("Please report:")
print("1. For POSITIVE +25000: Did wheel pull LEFT or RIGHT?")
print("2. For NEGATIVE -25000: Did wheel pull LEFT or RIGHT?")
print()
