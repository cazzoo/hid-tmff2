# T500RS USB Capture Guide

## Why We Need This

**Current Status**:
- ✅ Device detection works
- ✅ Driver loads successfully
- ❌ Force feedback doesn't work (error -32 EPIPE)

**Solution**: Capture Windows driver USB traffic to see the correct protocol.

## 🚀 Quick Start (Choose Your Method)

### ⭐ Method 1: Automated with Windows VM (EASIEST - 5 minutes)

**Requirements**: Windows VM with Thrustmaster drivers installed

```bash
sudo ./capture_t500rs_usb.sh --windows-vm
```

**That's it!** The script will:
1. Set up USB monitoring
2. Guide you through starting Windows VM
3. Capture all USB traffic
4. Automatically analyze the results

---

### Method 2: Manual Capture (10 minutes)

**Requirements**: Same as Method 1

```bash
# Find T500RS bus
lsusb | grep -i thrustmaster  # Note the bus number (e.g., Bus 002)

# Start capture
sudo tshark -i usbmon2 -w captures/windows.pcapng  # Replace 2 with your bus

# In another terminal: Start Windows VM, pass USB, test force feedback
# When done: Press Ctrl+C to stop capture

# Analyze
./analyze_capture.sh captures/windows.pcapng
```

---

### Method 3: Dual Boot (15 minutes)

**Requirements**: Windows installed on same machine

See "Dual Boot Method" section below.

---

## Detailed Instructions

### Method 1: Automated Capture (RECOMMENDED)

**This is the best method** - fully automated with our scripts!

#### Prerequisites (One-Time Setup)

**Install Tools:**
```bash
sudo pacman -S wireshark-cli usbutils
```

**Setup Windows VM** (if you don't have one):
- Install VirtualBox or virt-manager
- Create Windows 10 VM (4GB RAM, 40GB disk)
- Install Thrustmaster drivers in Windows
- Enable USB passthrough for T500RS (044f:b65e)
- Test that force feedback works in Windows

#### Run the Capture

```bash
sudo ./capture_t500rs_usb.sh --windows-vm
```

**Follow the prompts:**
1. Script finds T500RS and starts capture
2. Start your Windows VM
3. Pass T500RS USB device to VM (in VM menu)
4. In Windows: Test force feedback (see "What to Test" below)
5. Press ENTER to stop capture
6. Script automatically analyzes and shows results

---

### Method 2: Manual Capture

If the automated script doesn't work:

```bash
# 1. Find T500RS
lsusb | grep -i thrustmaster  # Note bus number

# 2. Start capture
sudo modprobe usbmon
sudo tshark -i usbmon2 -w captures/windows.pcapng  # Replace 2 with your bus

# 3. In Windows VM: Test force feedback for 1-2 minutes

# 4. Stop capture (Ctrl+C)

# 5. Analyze
./analyze_capture.sh captures/windows.pcapng
```

---

### Method 3: Dual Boot

If you have Windows dual-boot (simplest but requires reboot):

```bash
# In Linux:
lsusb | grep -i thrustmaster  # Note bus number
sudo modprobe usbmon
sudo tshark -i usbmon2 -w captures/dualboot.pcapng &

# Reboot to Windows (keep T500RS plugged in)
# Test force feedback in Windows
# Reboot back to Linux

# Analyze
./analyze_capture.sh captures/dualboot.pcapng
```

**Note**: This may miss some initialization since device resets on reboot.

---

## What to Test in Windows

### Minimum Test (30 seconds)
1. Open Thrustmaster Control Panel
2. Click "Test Forces" or similar
3. Feel the wheel respond
4. Done!

### Ideal Test (2 minutes)
1. Open Thrustmaster Control Panel
2. Test constant force effect
3. Test spring effect
4. Adjust force strength slider
5. Done!

### Complete Test (5 minutes)
1. All of the above
2. Plus: Open a game with force feedback
3. Drive around and feel different effects

**Any of these is enough!** Even 30 seconds of testing will capture the protocol.

---

## After Capture

### Automatic Analysis

The `analyze_capture.sh` script will show you:
- Number of USB commands captured
- Command patterns and structure
- Report IDs and data format
- Initialization sequence

### What We're Looking For

The capture will reveal:
- **Report ID**: Which report Windows uses (we think it's 10)
- **Command Structure**: Exact byte format
- **Initialization**: Any special setup commands
- **Effect Encoding**: How force strength is encoded

### Share Results

Share the analysis output and I'll:
1. Identify the correct protocol
2. Update the Linux driver code
3. Test until force feedback works!

---

## Troubleshooting

### "No USB traffic captured"
- Verify T500RS is passed to VM (check Windows Device Manager)
- Make sure you tested force feedback in Windows
- Try unplugging/replugging in VM

### "Device not found"
```bash
lsusb | grep -i thrustmaster  # Verify device is connected
```

### "Capture file empty"
- Capture for at least 30 seconds
- Make sure force feedback was tested in Windows
- Check that USB passthrough is working

### "tshark not installed"
```bash
sudo pacman -S wireshark-cli
```

---

## Expected Results

After successful capture and analysis, you'll see output like:

```
Total packets: 234
SET_REPORT (0x09): 15
INTERRUPT OUT: 0

SET_REPORT Commands:
Frame | Report ID | Data
------|-----------|-----
  123 |        10 | 0e:01:20:00:00:00:00:00:00:00:00:00:00:00
  125 |        10 | 0e:01:41:00:00:00:00:00:00:00:00:00:00:00
  127 |        10 | 0e:02:7f:00:00:00:00:00:00:00:00:00:00:00
```

This shows us the **exact protocol** Windows uses!

---

## Time Estimate

- **VM Setup** (one-time): 30 minutes
- **Capture**: 5 minutes
- **Analysis**: Automatic
- **Implementation**: 1-2 hours (I'll do this)
- **Testing**: 30 minutes

**Total to working force feedback**: ~2-3 hours after capture

---

## Quick Reference

```bash
# Automated (easiest)
sudo ./capture_t500rs_usb.sh --windows-vm

# Manual
sudo tshark -i usbmon2 -w captures/windows.pcapng

# Analyze
./analyze_capture.sh captures/windows.pcapng

# Find device
./find_t500rs.sh
```

---

## Summary

1. **Choose a method** (automated is easiest)
2. **Run the capture** (5-15 minutes)
3. **Share the analysis** with me
4. **I implement the protocol** (1-2 hours)
5. **Force feedback works!** 🎉

The capture is the final piece of the puzzle!

