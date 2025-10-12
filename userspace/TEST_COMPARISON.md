# T500RS Driver Comparison Testing

## Setup Complete ✅

Two versions of the driver are now available for testing:

### 1. Working Version (Monolithic Driver - Known Good)
**Location:** `/home/caz/Documents/hid-tmff2-working/userspace/`
**Git Commit:** `e213b43` (Add envelope debug logging - envelope working perfectly!)
**Type:** Monolithic driver (t500rs-ffb.c - single file)
**Binary:** `t500rs-ffb` (NOT t500rs-ffb-modular)
**Features:**
- ✅ Force smoothing (always enabled)
- ✅ Multi-effect mixing (always enabled)
- ✅ Dynamic update rate (always enabled)
- ✅ Envelope support
- ❌ No runtime configuration
- ❌ Not modular

### 2. Current Version (Modular Driver - With Runtime Config)
**Location:** `/home/caz/Documents/hid-tmff2/userspace/`
**Git Commit:** `930bf60` (Add extensive debug logging)
**Type:** Modular driver (5 source files + 5 headers)
**Binary:** `t500rs-ffb-modular`
**Features:**
- ✅ Force smoothing (toggleable)
- ✅ Multi-effect mixing (toggleable)
- ✅ Dynamic update rate (toggleable)
- ✅ Runtime configuration via Python GUI
- ✅ Debug logging
- ✅ Modular architecture

## Quick Test Commands

```bash
# Test WORKING version (monolithic driver - known good)
cd ~/Documents/hid-tmff2-working/userspace
sudo ./t500rs-ffb

# Test CURRENT version (modular driver - with config features)
cd ~/Documents/hid-tmff2/userspace
sudo ./t500rs-ffb-modular

# Use config GUI (only works with current version)
cd ~/Documents/hid-tmff2/userspace
python3 t500rs_config_gui.py
```

## Testing Steps

1. **Test working version first** - Verify FF works as expected
2. **Test current version** - Compare behavior
3. **If current version broken** - We need to find what changed
4. **If current version works** - Test the toggle features

## What to Look For

### Working Version Should:
- ✅ Provide continuous force feedback
- ✅ Feel smooth and responsive
- ✅ All effects working properly

### Current Version Should:
- ✅ Work identically to working version (with all features enabled)
- ✅ Allow toggling features via GUI
- ✅ Show debug logs

## Debugging

Check driver logs for:
- `[INFO] Force update thread started`
- `[DEBUG] Sending force: level=0xXX, combined=XXXX, active_effects=X`
- `[INFO] Force smoothing: ENABLED/DISABLED`

---

**Purpose:** Compare working vs current driver to identify issues
**Goal:** Get current version working as well as the working version

