#!/usr/bin/env python3
"""
Test different direction byte values to find which one works for RIGHT direction
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
    print("Make sure the NEW t500rs-ffb driver is running!")
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
    struct.pack_into('HH', effect, 10, 3000, 0)  # replay (3 seconds)
    struct.pack_into('h', effect, 16, level)  # level
    struct.pack_into('HHHH', effect, 18, 0, 0, 0, 0)  # envelope
    return effect

print("=" * 80)
print("DIRECTION BYTE TEST - Testing with NEW driver (buf[2]=0x42 for negative)")
print("=" * 80)
print()
print("The driver has been modified to use buf[2]=0x42 for negative forces")
print("Let's test if this works!")
print()

EV_FF = 0x15
EVIOCSFF = 0x40304580

# Test with both directions
tests = [
    (25000, "POSITIVE +25000 (should pull LEFT)"),
    (-25000, "NEGATIVE -25000 (should pull RIGHT if fix works!)"),
]

for level, description in tests:
    print(f"📊 Testing: {description}")
    print(f"   Force level: {level}")
    print(f"   Expected driver behavior:")
    if level < 0:
        print(f"   • Negative → driver sets buf[2]=0x42 (NEW!)")
    else:
        print(f"   • Positive → driver sets buf[2]=0x41 (known working)")
    
    # Upload effect
    buf = bytearray(create_constant_effect(level))
    fcntl.ioctl(fd, EVIOCSFF, buf)
    effect_id = struct.unpack_from('h', buf, 2)[0]
    print(f"   ✅ Uploaded effect ID: {effect_id}")
    
    # Play effect
    event = struct.pack('llHHi', 0, 0, EV_FF, effect_id, 1)
    os.write(fd, event)
    print(f"   ▶️  Playing effect for 3 seconds...")
    print()
    
    time.sleep(3)
    
    # Stop effect
    event = struct.pack('llHHi', 0, 0, EV_FF, effect_id, 0)
    os.write(fd, event)
    print(f"   ⏹️  Stopped effect\n")
    
    input(f"   ❓ DID YOU FEEL A FORCE? WHICH DIRECTION? (Press Enter to continue)")
    
    # Try to remove (for cleanup)
    try:
        EVIOCRMFF = 0x40044581
        effect_id_buf = struct.pack('h', effect_id) + b'\x00' * 46
        fcntl.ioctl(fd, EVIOCRMFF, effect_id_buf)
    except:
        pass
    
    print("-" * 80)
    print()

os.close(fd)

print("=" * 80)
print("TEST COMPLETE!")
print("=" * 80)
print()
print("RESULTS:")
print("1. POSITIVE +25000: Did wheel pull LEFT?")
print("2. NEGATIVE -25000: Did wheel pull RIGHT (or any force at all)?")
print()
print("If NEGATIVE still doesn't work, we'll try other values:")
print("  • 0x40 (bit flip)")
print("  • 0x43 (bit flip)")
print("  • 0x44, 0x45, 0x46, etc.")
print()
