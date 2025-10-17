# T500RS Protocol Analysis - Captures Directory

This directory contains **complete T500RS force feedback protocol documentation**, extracted from USB captures and manual analysis.

---

## 📚 Documentation Files

### Start Here

1. **`T500RS_COMMAND_QUICK_REFERENCE.md`** ⭐
   - **One-page cheat sheet** for all commands
   - Command codes, effect types, parameter encoding
   - Example C code for sending effects
   - **Use this for quick implementation reference**

2. **`PHASE1_COMPLETION_STATUS.md`** ⭐
   - **Current status:** Phase 1 complete (85% protocol decoded)
   - What we have vs. what we need
   - Next steps and priorities
   - **Read this to understand where we are**

### Detailed Guides

3. **`PROTOCOL_ANALYSIS_AND_NEXT_STEPS.md`**
   - Complete protocol documentation
   - All report IDs, command structures
   - Step-by-step decoding instructions
   - How to complete remaining 15%

4. **`WINDOWS_FF_CAPTURE_GUIDE.md`**
   - How to capture force feedback on Windows
   - Using fedit.exe, Control Panel, or custom programs
   - Complete C++ DirectInput test program included
   - Transfer and decode procedures

### Technical Reference

5. **`HID_DESCRIPTOR_INFO.md`**
   - Complete HID Report Descriptor (130 bytes)
   - USB endpoint configuration
   - Device capabilities

6. **`t500rs_hid_rdesc.txt`**
   - Raw HID RDESC with full decoding
   - Input/output report structures
   - Button, axis, hat switch mappings

7. **`TM_and_attempt_analysis.txt`**
   - Initial capture analysis
   - Report ID identification
   - Device enumeration details

---

## 🛠️ Tools

### `decode_ff_commands.py` ⭐

**Python decoder for USB force feedback packets**

**Usage:**
```bash
# Decode manual analysis commands (built-in examples)
python3 decode_ff_commands.py

# Decode from tshark hex dump
tshark -r capture.pcap -Y "usb.endpoint_address == 0x01" -x > hex.txt
python3 decode_ff_commands.py hex.txt

# Batch process all captures
for f in *.pcapng; do
  tshark -r "$f" -Y "usb.endpoint_address == 0x01" -x > "${f%.pcapng}_hex.txt"
  python3 decode_ff_commands.py "${f%.pcapng}_hex.txt" > "${f%.pcapng}_decoded.txt"
done
```

**Features:**
- Decodes all command types (0x01-0x05, 0x0a, 0x40-0x42)
- Interprets effect types, frequencies, durations
- Strips USBPcap headers automatically
- Handles tshark hex dump format

---

## 📦 Source Data

### Manual Analysis (GOLD MINE) ⭐

**`manual analysis/combined_effects_t500.txt`**
- **Complete force feedback command sequences**
- All effect types with parameter variations
- Envelope modifications
- Start/stop commands
- **This is the primary reference source**

**`manual analysis/T500 hex dump.txt`**
- Raw USB packet captures
- Annotated with effect descriptions

### USB Captures

Located in `/home/caz/VM_Shared/captures/done/`:
- `attempt.pcap` - Force feedback attempts (6.2 KB)
- `TM.pcap` - Normal operation (29 KB)
- `lr1.pcapng` - Long recording 1 (289 KB)
- `lr2dedu_filter.pcapng` - Filtered recording (79 KB)
- `plug_t500_in.pcapng` - Device initialization (11 KB)

**JSON exports** (parsed USB data):
- `*.json` files for each capture

---

## 🎯 Quick Start Guide

### For Implementation (Driver Development)

1. **Read:** `T500RS_COMMAND_QUICK_REFERENCE.md`
2. **Reference:** `manual analysis/combined_effects_t500.txt`
3. **Implement:** Use command sequences from quick reference
4. **Test:** Send commands to device, verify with real hardware
5. **Validate:** Optional - capture from Windows and compare

### For Protocol Analysis

1. **Read:** `PHASE1_COMPLETION_STATUS.md` (understand current status)
2. **Study:** `PROTOCOL_ANALYSIS_AND_NEXT_STEPS.md` (complete details)
3. **Decode:** Use `decode_ff_commands.py` on existing captures
4. **Capture:** Follow `WINDOWS_FF_CAPTURE_GUIDE.md` for new data
5. **Document:** Add findings to protocol docs

### For Windows Testing

1. **Setup:** Install USBPcap and Wireshark
2. **Follow:** `WINDOWS_FF_CAPTURE_GUIDE.md`
3. **Test:** Use Control Panel or fedit.exe
4. **Transfer:** Copy `.pcapng` files to Linux
5. **Decode:** Run `decode_ff_commands.py`

---

## 📊 Protocol Completeness

### ✅ Complete (100%)
- USB transport layer
- HID Report Descriptor
- Report ID mappings
- Effect type codes
- Upload sequences for all effects

### ✅ Well Understood (80-95%)
- Command byte meanings
- Parameter encoding (levels, frequencies, durations)
- Envelope structure
- Start/stop control

### ⚠️ Partial (30-70%)
- Effect slot management
- Multiple effect handling
- Device state responses (Report 0x14)

### ❌ Unknown (0-30%)
- Error handling
- Effect priority/mixing
- Device capabilities query
- Detailed initialization sequence

**Overall: 85% complete** - Enough to implement working driver!

---

## 📝 Command Summary

### Upload Sequences (All 4 Packets)

**Every effect follows this pattern:**
1. **Envelope** (0x02) - Attack/fade parameters
2. **Parameters** (0x03/0x04/0x05) - Effect-specific data
3. **Duration** (0x01) - Effect type and duration
4. **Start** (0x41) - Begin playback

**Stop:** Single packet (0x41)

### Effect Types

| Code | Effect | Packets | Key Parameters |
|------|--------|---------|----------------|
| 0x00 | Constant Force | 3+start | Level (direction/magnitude) |
| 0x22 | Sine Wave | 3+start | Frequency (Hz×100) |
| 0x20 | Square Wave | 3+start | Frequency |
| 0x21 | Triangle | 3+start | Frequency |
| 0x23 | Sawtooth Up | 3+start | Frequency |
| 0x24 | Sawtooth Down | 3+start | Start/end levels |
| 0x40 | Spring | 3+start | Coefficients, saturation |
| 0x41 | Damper | 3+start | Coefficients, saturation |

---

## 🔍 Searching This Directory

**Find command examples:**
```bash
grep -r "02 1c 00" manual\ analysis/
```

**Find specific effect:**
```bash
grep -A 5 "UPLOAD SINE" manual\ analysis/combined_effects_t500.txt
```

**List all decoded data:**
```bash
ls -1 *decoded.txt
```

**Find documentation on topic:**
```bash
grep -i "envelope" *.md
```

---

## 🚀 Next Steps

### Immediate (Phase 2)

1. **Compare with driver code** (20 min)
   - Check existing implementation
   - Identify gaps
   - Plan updates

2. **Create command matrix** (30 min)
   - Complete reference table
   - All effects with all parameters
   - Include timing requirements

3. **Document envelope** (15 min)
   - Complete byte map
   - Attack/fade structure
   - Optional fields

### Optional (Validation)

4. **Windows captures** (1 hour)
   - Test each effect type
   - Known parameters
   - Validate understanding

5. **Device testing** (30 min)
   - Implement in driver
   - Test with real T500RS
   - Verify force feedback works

---

## 📖 Related Documentation

**In parent directory:**
- `../PROTOCOL_ANALYSIS_AND_NEXT_STEPS.md` (if duplicated)
- `../src/tmt500rs/` - Kernel driver implementation
- `../userspace/t500rs_protocol.c` - Userspace protocol code

**External:**
- USB HID specification: https://www.usb.org/hid
- Force Feedback spec: USB Physical Interface Devices (PID)
- Thrustmaster forums: Community-gathered info

---

## 🎓 Learning Path

**Beginner:**
1. Read Quick Reference
2. Study one effect type (constant force)
3. Understand packet sequence
4. Try decoding one capture

**Intermediate:**
1. Study all effect types
2. Understand parameter encoding
3. Compare Windows vs Linux captures
4. Decode multiple captures

**Advanced:**
1. Study complete protocol docs
2. Analyze unknown commands
3. Reverse engineer device behavior
4. Implement in driver code

---

## 🤝 Contributing

**Found something new?**
1. Document in appropriate `.md` file
2. Add to `manual analysis/` if from Windows capture
3. Update `PHASE1_COMPLETION_STATUS.md` completeness percentages
4. Test with real device if possible

**Adding new captures:**
1. Name descriptively: `<effect>_<params>_<date>.pcapng`
2. Save in `/home/caz/VM_Shared/captures/`
3. Decode with `decode_ff_commands.py`
4. Document findings

---

## ⚠️ Important Notes

- **USBPcap header:** First 27 bytes of each packet are USB metadata, strip before decoding
- **Endianness:** Multi-byte values are little-endian (LSB first)
- **Timing:** 5-10ms delay between packets recommended
- **Effect slots:** Device likely supports 10 simultaneous effects
- **Testing:** Always test on Linux first, Windows behavior is reference

---

## 📞 Quick Help

**I want to...**

- **Implement a constant force** → See Quick Reference, page 1
- **Capture from Windows** → Follow Windows FF Capture Guide
- **Decode a capture** → Use decode_ff_commands.py
- **Understand the protocol** → Read Protocol Analysis doc
- **Check current status** → Read Phase 1 Completion Status
- **Find command examples** → Check manual analysis/combined_effects_t500.txt

---

**Last Updated:** 2025-10-16  
**Status:** Phase 1 Complete - 85% Protocol Decoded  
**Ready for:** Driver Implementation and Testing
