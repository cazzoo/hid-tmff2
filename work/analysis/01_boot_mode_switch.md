# 01 — Boot-mode-switch sequence (capture 2 only)

> The T500RS (like all modern Thrustmaster wheels) enumerates first as a generic
> "Thrustmaster FFB Wheel" in **boot mode** (PID `0xb65d`), then a vendor control
> request from the host switches it into its **advanced mode** with the wheel-specific
> PID (`0xb65e` for the standard rim, `0xb662` for the F1 rim).
>
> **In Linux this is performed by `hid-tminit` / mainline `hid-thrustmaster`. It is
> NOT the responsibility of our `hid-tmt500rs.c`.** This document captures the exact
> wire sequence so we can verify that `hid-tminit` is doing the right thing.

## Timeline (capture 2)

| Frame | t (s) | Event |
|-------|-------|-------|
| 84 | 8.726 | First DEVICE descriptor reply from `2.3.0`: **`0x044f:0xb65d`** (boot mode) |
| 85–88 | 8.73–8.73 | GET_DESCRIPTOR DEVICE + CONFIGURATION (standard enumeration) |
| 89 | 8.738 | SET_CONFIGURATION (`00 09 ...`) |
| 91 | 8.799 | SET_INTERFACE (`00 0b 0000 ...`) — alt setting 0 |
| 93 | 8.916 | **Vendor IN `0xc1 0x56` wValue=0 wLength=8** — GET_WHEEL_INFO |
| 94 | 8.918 | Reply: `56 2b 00 00 ...` (model byte = `0x2b`) |
| 102 | 9.000 | **Vendor IN `0xc1 0x47` wValue=0 wLength=8** — GET_FW_INFO |
| 102 reply | 9.000 | Reply: `47 00 07 00 00 00 03 00` (fw version encoded) |
| 104 | 9.062 | **Vendor IN `0xc1 0x42`** wLength=3 — Reply: `42 e8 03` |
| 106 | 9.116 | **Vendor IN `0xc1 0x4e`** wLength=2 — Reply: `4e 14` |
| 108 | 9.184 | **Vendor IN `0xc1 0x56`** again — Reply: `56 2b 00 00` |
| 135–137 | 8.97–9.0 | GET_DESCRIPTOR BOS / STRING possibly |
| 145 | 9.184 | Final pre-switch vendor IN |
| **147** | **9.186** | **🔴 THE SWITCH: `bmRequestType=0x41 bRequest=0x53 wValue=0x0003 wLength=0`** |
| 151 | 9.602 | Device re-enumerates: DEVICE descriptor now reports **`0x044f:0xb662`** (advanced mode, F1 rim) |
| 152+ | 9.604 | Standard enumeration of the new PID, FFB init begins |

The PID transition takes **≈416 ms** from the `0x53` request to the first descriptor of
the new PID. The device physically disconnects and reconnects on the bus.

## The switch request, byte-by-byte (frame 147 raw)

```
offset 28 (0x1c): 41   bmRequestType = Vendor OUT to interface
offset 29 (0x1d): 53   bRequest      = 0x53 ("switch to advanced mode")
offset 30 (0x1e): 03   wValue LSB    = 0x03
offset 31 (0x1f): 00   wValue MSB    = 0x00  → wValue = 0x0003
offset 32 (0x20): 00   wIndex LSB    = 0x00
offset 33 (0x21): 00   wIndex MSB    = 0x00  → wIndex = 0x0000
offset 34 (0x22): 00   wLength LSB   = 0x00
offset 35 (0x23): 00   wLength MSB   = 0x00  → wLength = 0  (NO data phase)
```

So the entire switch command is an 8-byte SETUP packet with no data stage:

```c
/* Pseudo-code equivalent */
usb_control_msg(dev,
    /* bmRequestType = */ 0x41,   // USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE
    /* bRequest      = */ 0x53,
    /* wValue        = */ 0x0003,
    /* wIndex        = */ 0x0000,
    /* data          = */ NULL,
    /* wLength       = */ 0,
    USB_CTRL_SET_TIMEOUT);
```

## Pre-switch identification vendor requests

These precede the switch and are how the host identifies which wheel is attached
(so it can pick the right advanced-mode PID to switch into):

| Frame | bmRequestType | bRequest | wValue | wLength | Reply | Meaning (inferred) |
|-------|---------------|----------|--------|---------|-------|--------------------|
| 93 | `0xc1` | `0x56` | 0 | 8 | `56 2b 00 00 ...` | GET_MODEL_INFO — model byte = **`0x2b`** (F1 rim / T500RS family) |
| 102 | `0xc1` | `0x47` | 0 | 8 | `47 00 07 00 00 00 03 00` | GET_FW_INFO — version fields |
| 104 | `0xc1` | `0x42` | 0 | 3 | `42 e8 03` | unknown |
| 106 | `0xc1` | `0x4e` | 0 | 2 | `4e 14` | unknown |
| 108 | `0xc1` | `0x56` | 0 | 8 | `56 2b 00 00 ...` | GET_MODEL_INFO (repeat) |

> **The model byte `0x2b` maps the F1 attachment to advanced-mode PID `0xb662`.**
> A standard rim likely returns a different model byte mapping to `0xb65e`.

## Cross-reference with mainline kernel `hid-thrustmaster.c`

The mainline `drivers/hid/hid-thrustmaster.c` performs exactly this `0x53` switch.
The model-byte → advanced-PID table (abridged from mainline kernel) is:

| Model byte | Advanced PID | Wheel |
|------------|--------------|-------|
| `0x01` | `0xb663` | T300RS |
| `0x02` | `0xb66a` | T300 Ferrari Alcantara |
| ... | ... | ... |
| `0x0b` | `0xb677` | T248 |
| `0x14` | `0xb680` | T-GT |
| ... | ... | TX / TS-XW / TS-PC / T128 / T598 |
| **`0x2b`** | **`0xb662`** | **T500RS w/ F1 rim** (matches capture 2) |
| (likely `0x2a` or similar) | **`0xb65e`** | T500RS standard rim (capture 1, advanced from boot) |

**Mainline `hid-thrustmaster` is therefore the canonical boot-mode handler.** Our
`hid-tmff2` repo explicitly relies on `hid-tminit` (the scarburato fork) or mainline
`hid-thrustmaster` to do this — see `docs/STRUCTURE.md` and `README.md`:

> Until the updated `hid-tminit` is upstreamed, you might want to blacklist the
> kernel module `hid-thrustmaster`.

## Why this matters for our driver

Our `hid-tmt500rs.c` **never performs the boot-mode switch** and never matches the
boot-mode PID `0xb65d`. The probe table only contains the advanced-mode PIDs:

```c
/* Expect something like this in hid-tmff2.c (the parent driver's HID ID table): */
{ HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, 0xb65e) }, /* T500RS std rim */
{ HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, 0xb662) }, /* T500RS F1 rim  */
```

**Operational consequence:** if a user has blacklisted `hid-thrustmaster` (per the
README's optional step) but `hid-tminit` is also absent, the wheel stays stuck in
`0xb65d` boot mode forever and `hid-tmt500rs` never probes.

### Action items (informational — not in our driver's scope)

1. Document the **mandatory dependency** on `hid-tminit` / `hid-thrustmaster` for T500RS
   in `README.md` (currently only mentioned for TX/TS-XW).
2. The `0x2b` model byte for the **F1 attachment** should be added to whichever
   `hid-tminit` table is in use; verify whether it's already in mainline.
3. Consider matching the boot-mode PID `0xb65d` in our ID table and refusing to bind
   with a clear `hid_err` ("bootloader mode — load hid-tminit/hid-thrustmaster").
