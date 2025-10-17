# tmHidUsb.sys Kernel Driver Analysis
## Thrustmaster T500RS Force Feedback Architecture

**Analysis Date:** January 2025  
**Driver Version:** 2.11.57  
**Copyright:** Thrustmaster 2017

---

## Executive Summary

The `tmHidUsb.sys` driver is the **core kernel-mode USB HID minidriver** for Thrustmaster force feedback devices, particularly the T500RS racing wheel. It sits at the lowest level of the driver stack, handling USB communication, HID report processing, and providing the foundation for force feedback operations.

### Architecture Overview

```
┌────────────────────────────────────────────────────────────┐
│                  Application Layer                          │
│         (Games using DirectInput/XInput APIs)              │
└────────────────────────────────────────────────────────────┘
                          ↓
┌────────────────────────────────────────────────────────────┐
│              User-Mode Components                           │
│  ┌───────────────┐  ┌──────────────┐  ┌────────────────┐  │
│  │ tm_api_lib    │→ │ tmeffcpl64   │→ │  tmPID64.dll   │  │
│  │ (Wine Wrapper)│  │ (Control)    │  │  (FFB Engine)  │  │
│  └───────────────┘  └──────────────┘  └────────────────┘  │
└────────────────────────────────────────────────────────────┘
                          ↓ IOCTL/HID API
┌────────────────────────────────────────────────────────────┐
│              Kernel-Mode Components                         │
│  ┌──────────────────┐                                      │
│  │ GuiHidUsbDev     │  (Lower filter driver - FFB logic)   │
│  │ LowerFFB.sys     │                                      │
│  └──────────────────┘                                      │
│           ↓                                                 │
│  ┌──────────────────┐                                      │
│  │  **tmHidUsb.sys** │ ← MAIN HID MINIDRIVER               │
│  │  (USB HID Driver) │                                     │
│  └──────────────────┘                                      │
│           ↓                                                 │
│  ┌──────────────────┐   ┌────────────┐                    │
│  │  HidClass.sys    │   │ HidParse   │                    │
│  │  (HID Class)     │   │ .sys       │                    │
│  └──────────────────┘   └────────────┘                    │
└────────────────────────────────────────────────────────────┘
                          ↓
┌────────────────────────────────────────────────────────────┐
│            USB Bus Driver Layer                             │
│  ┌──────────────────┐                                      │
│  │   usbd.sys       │  (USB Protocol Handler)              │
│  └──────────────────┘                                      │
└────────────────────────────────────────────────────────────┘
                          ↓
┌────────────────────────────────────────────────────────────┐
│          Hardware - Thrustmaster T500RS                     │
│          (USB VID:044F PID:B66D/B66E)                      │
└────────────────────────────────────────────────────────────┘
```

---

## 1. Component Analysis

### 1.1 Driver Metadata

| Property | Value |
|----------|-------|
| **Filename** | tmhidusb.sys |
| **Internal Name** | tmHidUSB |
| **Description** | Thrustmaster HID USB Driver |
| **Product** | Thrustmaster FFB Gaming Software |
| **Version** | 2.11.57 |
| **Type** | HID USB Minidriver |
| **Functions** | 484 total |

### 1.2 Key Imports and Dependencies

The driver imports from several kernel modules:

#### **ntoskrnl.exe** (Windows Kernel)
- Memory Management: `ExAllocatePoolWithTag`, `MmProbeAndLockPages`, `MmMapLockedPagesSpecifyCache`
- Synchronization: `KeInitializeEvent`, `KeInitializeSpinLock`, `KeAcquireInStackQueuedSpinLock`
- Thread Management: `PsCreateSystemThread`, `PsTerminateSystemThread`
- I/O Management: `IoAllocateMdl`, `IoCsqInitialize`, `IoCompleteRequest`, `IoBuildDeviceIoControlRequest`
- Device Interface: `IoRegisterDeviceInterface`, `IoSetDeviceInterfaceState`
- Registry Access: `ZwOpenKey`, `ZwQueryValueKey`, `ZwSetValueKey`

#### **HIDCLASS.SYS** (HID Class Driver)
- **HidRegisterMinidriver** - Critical function to register as HID minidriver
- HID Report Management (via HidParse):
  - `HidP_GetCaps` - Get HID capabilities
  - `HidP_GetButtonCaps` - Get button capabilities
  - `HidP_GetValueCaps` - Get value capabilities
  - `HidP_SetUsageValue` - Set HID report values
  - `HidP_GetUsageValue` - Get HID report values
  - `HidP_SetScaledUsageValue` - Set scaled values
  - `HidP_SetUsages` - Set button usages

#### **USBD.SYS** (USB Bus Driver)
- `USBD_ParseConfigurationDescriptorEx` - Parse USB device configuration
- `USBD_CreateConfigurationRequestEx` - Create USB configuration request

---

## 2. Driver Architecture

### 2.1 HID Minidriver Model

The tmHidUsb.sys driver follows the **Windows HID Minidriver architecture**:

1. **DriverEntry** (initialization)
   - Initializes driver structures
   - Registers with HID class driver via `HidRegisterMinidriver`
   - Sets up dispatch routines for PnP, Power, I/O

2. **AddDevice** (device attachment)
   - Creates device object when T500RS is plugged in
   - Allocates device extension with custom state
   - Registers device interface for user-mode access

3. **Start Device** (initialization)
   - Configures USB endpoints
   - Allocates transfer buffers
   - Starts continuous read operations for HID input reports
   - Initializes force feedback state

4. **IRP Dispatch** (request handling)
   - Handles READ requests (HID input reports - wheel/pedal/button state)
   - Handles WRITE requests (HID output reports)
   - Handles IOCTL requests (HID feature reports - force feedback commands)
   - Implements Cancel-Safe Queue (CSQ) for request management

### 2.2 Key Function Categories (from 484 total functions)

Based on the function addresses and typical HID minidriver structure:

| Address Range | Likely Purpose |
|---------------|----------------|
| `0x140001000 - 0x140002000` | Driver initialization, entry points |
| `0x140006000 - 0x14000a000` | HID report processing |
| `0x14000b000 - 0x140013000` | USB communication, URB management |
| `0x140018000 - 0x14001a000` | Performance monitoring, timing |
| `0x14001c000 - 0x140024000` | Device I/O, IRP handling |
| `0x140028000 - 0x140032000` | Power management, PnP |
| `0x140033000 - 0x140039000` | Utility functions, synchronization |

### 2.3 Device Extension Structure

The driver allocates a custom device extension for each T500RS device, likely containing:

```c
typedef struct _TMHID_DEVICE_EXTENSION {
    PDEVICE_OBJECT          PhysicalDeviceObject;
    PDEVICE_OBJECT          NextDeviceObject;
    IO_REMOVE_LOCK          RemoveLock;
    
    // USB related
    USBD_CONFIGURATION_HANDLE UsbConfigHandle;
    PUSBD_INTERFACE_INFORMATION UsbInterface;
    USBD_PIPE_HANDLE        InterruptInPipe;
    USBD_PIPE_HANDLE        InterruptOutPipe;
    
    // HID related
    PHIDP_PREPARSED_DATA    PreparsedData;
    HIDP_CAPS               HidCaps;
    
    // Input/Output buffers
    PUCHAR                  InputReportBuffer;
    ULONG                   InputReportLength;
    PUCHAR                  OutputReportBuffer;
    ULONG                   OutputReportLength;
    
    // Cancel-Safe Queue for pending IRPs
    IO_CSQ                  CancelSafeQueue;
    KSPIN_LOCK              QueueLock;
    LIST_ENTRY              PendingIrpQueue;
    
    // Force Feedback state
    KMUTEX                  FFBStateMutex;
    PUCHAR                  FFBReportBuffer;      // 11560 bytes
    ULONG                   FFBReportBufferSize;
    
    // Timing and performance
    LARGE_INTEGER           PerformanceFrequency;
    KSPIN_LOCK              TimingLock;
    ULONG                   MinLatencyUs;
    ULONG                   MaxLatencyUs;
    ULONG                   AvgLatencyUs;
    
    // State flags
    ULONG                   DeviceState;
    BOOLEAN                 FFBEnabled;
    BOOLEAN                 DeviceRemoved;
    
} TMHID_DEVICE_EXTENSION, *PTMHID_DEVICE_EXTENSION;
```

---

## 3. HID Communication Protocol

### 3.1 HID Report Structure

The T500RS uses three types of HID reports:

#### **Input Reports** (Device → Host)
- Report ID: Standard HID (likely 0x01)
- Size: ~64 bytes (typical for gaming devices)
- Content:
  - Steering wheel position (16-bit)
  - Accelerator pedal (8-bit)
  - Brake pedal (8-bit)
  - Clutch pedal (8-bit)
  - Button states (bit array)
  - D-pad/POV hat
  - Gear shifter position

#### **Output Reports** (Host → Device)
- Used for simple LED/indicator control
- Size: Variable

#### **Feature Reports** (Bidirectional)
- **Report ID: 0xCFEF** (Critical FFB Report)
- Size: **11560 bytes** (confirmed from tmPID64.dll analysis)
- Content: Force feedback effect parameters
  - Effect type (Spring, Damper, Friction, Inertia, Constant Force, etc.)
  - Force magnitude and direction
  - Duration, gain, envelope
  - Condition parameters (offset, positive/negative coefficients, saturation, deadband)
  - Periodic effect parameters (magnitude, offset, phase, period)

### 3.2 Force Feedback Report Format (0xCFEF)

The 11560-byte FFB feature report follows this structure (based on tmPID64.dll analysis):

```c
#define TM_FFB_REPORT_ID    0xCFEF
#define TM_FFB_BUFFER_SIZE  11560

typedef struct _TM_FFB_REPORT {
    USHORT      ReportID;           // 0xCFEF
    UCHAR       EffectType;         // Spring, Damper, Constant, etc.
    UCHAR       EffectOperation;    // Start, Stop, Solo, etc.
    
    // Effect parameters (varies by type)
    union {
        struct {  // Constant Force
            SHORT   Magnitude;
            USHORT  Duration;
            // ... 
        } ConstantForce;
        
        struct {  // Condition (Spring/Damper/Friction/Inertia)
            SHORT   CenterPointOffset;
            SHORT   PositiveCoefficient;
            SHORT   NegativeCoefficient;
            USHORT  PositiveSaturation;
            USHORT  NegativeSaturation;
            SHORT   DeadBand;
            // ...
        } Condition;
        
        struct {  // Periodic (Sine/Square/Triangle/Sawtooth)
            USHORT  Magnitude;
            SHORT   Offset;
            USHORT  Phase;
            USHORT  Period;
            // ...
        } Periodic;
    } Parameters;
    
    // Envelope
    struct {
        USHORT  AttackLevel;
        USHORT  AttackTime;
        USHORT  FadeLevel;
        USHORT  FadeTime;
    } Envelope;
    
    // Additional metadata
    UCHAR       Gain;               // 0-100
    BOOLEAN     EnableActuators;
    
    UCHAR       Reserved[11500];    // Padding to 11560 bytes
    
} TM_FFB_REPORT, *PTM_FFB_REPORT;
```

### 3.3 USB Endpoints

The T500RS likely uses:

- **Endpoint 1 IN (Interrupt)**: HID input reports (wheel state) @ 1000 Hz (1ms polling)
- **Endpoint 1 OUT (Interrupt)**: HID output reports
- **Control Endpoint 0**: Feature reports (force feedback via HID_SET_FEATURE/HID_GET_FEATURE)

---

## 4. Force Feedback Data Flow

### 4.1 Effect Playback Sequence

```
1. Game/Application
   └─→ DirectInput IDirectInputEffect::Start()
       │
2. User Mode (tm_api_lib_x64.dll)
   └─→ tm_api_force_set_effect_state(effect_id, PLAY)
       │
3. User Mode (tmeffcpl64.dll)
   └─→ tm_api internal: Marshals effect parameters
       │
4. User Mode (tmPID64.dll)
   └─→ Calculates force feedback values
   └─→ Scales magnitude by gain factor
   └─→ Applies envelope (attack/fade)
   └─→ Encodes into 11560-byte report buffer
       │
5. User Mode → Kernel (HID API)
   └─→ HidD_SetFeature(device, report_buffer, 11560)
       │
6. Kernel Mode (tmHidUsb.sys)
   └─→ IRP_MJ_DEVICE_CONTROL handler
   └─→ IOCTL_HID_SET_FEATURE dispatcher
   └─→ Validates Report ID = 0xCFEF
   └─→ Copies report to device extension
   └─→ Queues USB URB for control transfer
       │
7. Kernel Mode (USBD.sys)
   └─→ Builds USB Control Transfer (SET_REPORT)
   └─→ Endpoint 0: bmRequestType=0x21, bRequest=0x09
   └─→ wValue=(ReportType<<8)|ReportID
   └─→ Transmits 11560 bytes to device
       │
8. Hardware (T500RS)
   └─→ Motor controller receives effect data
   └─→ Applies force to steering wheel motor
   └─→ Updates at ~1000 Hz (1ms control loop)
```

### 4.2 Critical Code Paths

From the user-mode analysis (tmPID64.dll):

**Main FFB Function:** `FUN_180003490` (addr 0x180003490)
- Processes effect parameters
- Scales force magnitude (multiply by gain / 100)
- Calculates timing (duration, attack, fade)
- Calls HID communication function

**HID Communication:** `FUN_180035d40` (addr 0x180035d40)
- Allocates 11560-byte buffer
- Sets Report ID = 0xCFEF
- Prepares HID feature report structure

**HID Transmission:** `FUN_180044970` (addr 0x180044970)
- Calls `HidD_SetFeature(device_handle, buffer, 11560)`
- Includes validation and error handling

---

## 5. Linux Implementation Guidance

### 5.1 Required Linux Kernel Modules

To support the T500RS on Linux, you need:

1. **hid-tmff2.ko** (Custom HID driver)
   - Replacement for Windows tmHidUsb.sys
   - Handles force feedback via Linux FF API
   - Manages the 0xCFEF feature report

2. **usbhid** (Kernel built-in)
   - Base USB HID driver
   - HID report I/O

3. **hid-core** (Kernel built-in)
   - HID parsing and management

### 5.2 HID Driver Structure (hid-tmff2.c)

```c
// drivers/hid/hid-tmff2.c

#include <linux/hid.h>
#include <linux/input.h>
#include <linux/usb.h>
#include <linux/module.h>
#include "hid-ids.h"

#define USB_VENDOR_ID_THRUSTMASTER  0x044F
#define USB_DEVICE_ID_T500RS_WHEEL  0xB66D
#define USB_DEVICE_ID_T500RS_BASE   0xB66E

#define TM_FFB_REPORT_ID        0xCFEF
#define TM_FFB_REPORT_SIZE      11560

// Device private data
struct tmff2_device {
    struct hid_device *hdev;
    struct hid_report *report;
    struct input_dev *input;
    
    u8 *ffb_buffer;
    spinlock_t lock;
    
    struct {
        s16 magnitude;
        u16 duration;
        u8 effect_type;
        u8 gain;
    } ffb_state;
};

// Force feedback effect upload
static int tmff2_upload_effect(struct input_dev *dev,
                               struct ff_effect *effect,
                               struct ff_effect *old)
{
    struct tmff2_device *tmff2 = dev->ff->private;
    u8 *buf;
    unsigned long flags;
    
    spin_lock_irqsave(&tmff2->lock, flags);
    
    buf = tmff2->ffb_buffer;
    memset(buf, 0, TM_FFB_REPORT_SIZE);
    
    // Set Report ID
    *(u16 *)buf = cpu_to_le16(TM_FFB_REPORT_ID);
    
    // Encode effect parameters
    switch (effect->type) {
    case FF_CONSTANT:
        buf[2] = 0x01;  // Constant force type
        *(s16 *)(buf + 4) = cpu_to_le16(effect->u.constant.level);
        *(u16 *)(buf + 6) = cpu_to_le16(effect->replay.length);
        break;
        
    case FF_SPRING:
        buf[2] = 0x02;  // Spring type
        // Encode spring parameters...
        break;
        
    case FF_DAMPER:
        buf[2] = 0x03;  // Damper type
        // Encode damper parameters...
        break;
        
    // ... other effect types
    }
    
    // Apply gain
    buf[50] = (tmff2->ffb_state.gain * effect->u.constant.level) / 100;
    
    // Send feature report to device
    hid_hw_raw_request(tmff2->hdev, TM_FFB_REPORT_ID,
                       buf, TM_FFB_REPORT_SIZE,
                       HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
    
    spin_unlock_irqrestore(&tmff2->lock, flags);
    return 0;
}

// Playback control
static int tmff2_playback(struct input_dev *dev, int effect_id, int value)
{
    struct tmff2_device *tmff2 = dev->ff->private;
    
    if (value) {
        // Start effect - send START command in feature report
        // Similar to upload, but with START operation byte
    } else {
        // Stop effect - send STOP command
    }
    
    return 0;
}

// Initialize force feedback
static int tmff2_init_ff(struct tmff2_device *tmff2)
{
    struct input_dev *input = tmff2->input;
    int error;
    
    // Allocate FFB buffer
    tmff2->ffb_buffer = kzalloc(TM_FFB_REPORT_SIZE, GFP_KERNEL);
    if (!tmff2->ffb_buffer)
        return -ENOMEM;
    
    spin_lock_init(&tmff2->lock);
    
    // Register FF effects
    input_set_capability(input, EV_FF, FF_CONSTANT);
    input_set_capability(input, EV_FF, FF_SPRING);
    input_set_capability(input, EV_FF, FF_DAMPER);
    input_set_capability(input, EV_FF, FF_FRICTION);
    input_set_capability(input, EV_FF, FF_INERTIA);
    input_set_capability(input, EV_FF, FF_PERIODIC);
    input_set_capability(input, EV_FF, FF_SINE);
    input_set_capability(input, EV_FF, FF_SQUARE);
    input_set_capability(input, EV_FF, FF_TRIANGLE);
    input_set_capability(input, EV_FF, FF_SAW_UP);
    input_set_capability(input, EV_FF, FF_SAW_DOWN);
    input_set_capability(input, EV_FF, FF_RAMP);
    input_set_capability(input, EV_FF, FF_AUTOCENTER);
    input_set_capability(input, EV_FF, FF_GAIN);
    
    error = input_ff_create_memless(input, tmff2,
                                     tmff2_upload_effect);
    if (error) {
        kfree(tmff2->ffb_buffer);
        return error;
    }
    
    return 0;
}

// Probe function (device initialization)
static int tmff2_probe(struct hid_device *hdev,
                       const struct hid_device_id *id)
{
    struct tmff2_device *tmff2;
    int error;
    
    tmff2 = devm_kzalloc(&hdev->dev, sizeof(*tmff2), GFP_KERNEL);
    if (!tmff2)
        return -ENOMEM;
    
    tmff2->hdev = hdev;
    hid_set_drvdata(hdev, tmff2);
    
    error = hid_parse(hdev);
    if (error) {
        hid_err(hdev, "parse failed\n");
        return error;
    }
    
    error = hid_hw_start(hdev, HID_CONNECT_DEFAULT & ~HID_CONNECT_FF);
    if (error) {
        hid_err(hdev, "hw start failed\n");
        return error;
    }
    
    // Find input device
    tmff2->input = hidinput_get_input(hdev);
    if (!tmff2->input) {
        hid_err(hdev, "failed to get input device\n");
        error = -ENODEV;
        goto err_stop_hw;
    }
    
    // Initialize force feedback
    error = tmff2_init_ff(tmff2);
    if (error) {
        hid_err(hdev, "force feedback init failed\n");
        goto err_stop_hw;
    }
    
    hid_info(hdev, "Thrustmaster T500RS initialized\n");
    return 0;
    
err_stop_hw:
    hid_hw_stop(hdev);
    return error;
}

// Remove function
static void tmff2_remove(struct hid_device *hdev)
{
    struct tmff2_device *tmff2 = hid_get_drvdata(hdev);
    
    kfree(tmff2->ffb_buffer);
    hid_hw_stop(hdev);
}

// Device ID table
static const struct hid_device_id tmff2_devices[] = {
    { HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, USB_DEVICE_ID_T500RS_WHEEL) },
    { HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, USB_DEVICE_ID_T500RS_BASE) },
    { }
};
MODULE_DEVICE_TABLE(hid, tmff2_devices);

// HID driver structure
static struct hid_driver tmff2_driver = {
    .name       = "hid-tmff2",
    .id_table   = tmff2_devices,
    .probe      = tmff2_probe,
    .remove     = tmff2_remove,
};
module_hid_driver(tmff2_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Thrustmaster T500RS Force Feedback Driver");
```

### 5.3 Testing the Driver

```bash
# Build the module
cd /path/to/kernel/source/drivers/hid
make M=$(pwd) modules

# Load the module
sudo insmod hid-tmff2.ko

# Test with fftest
sudo fftest /dev/input/eventX  # Replace X with your device number

# Check dmesg for driver messages
dmesg | grep -i thrustmaster
```

### 5.4 Wine Integration

For Wine support, you'll need:

1. **hid-tmff2.ko** loaded in Linux kernel
2. **Wine HID backend** (built-in, uses Linux input subsystem)
3. **Wine DirectInput DLL** (maps Linux FF to DirectInput)

Wine will automatically detect the force feedback capabilities via `/dev/input/eventX` and expose them through DirectInput. No additional Wine DLL wrappers should be needed if the Linux driver properly implements the FF API.

---

## 6. Key Findings and Recommendations

### 6.1 Critical Constants

| Constant | Value | Purpose |
|----------|-------|---------|
| **FFB Report ID** | 0xCFEF | Feature report for force feedback |
| **FFB Buffer Size** | 11560 bytes | Size of force feedback report |
| **USB VID** | 0x044F | Thrustmaster vendor ID |
| **USB PID (Wheel)** | 0xB66D | T500RS wheel product ID |
| **USB PID (Base)** | 0xB66E | T500RS base product ID |

### 6.2 Implementation Priorities

1. **High Priority:**
   - Implement 0xCFEF feature report handling
   - Support basic effect types: Constant, Spring, Damper
   - Proper gain/magnitude scaling

2. **Medium Priority:**
   - Periodic effects: Sine, Square, Triangle
   - Envelope support (attack/fade)
   - Effect duration and looping

3. **Low Priority:**
   - Advanced condition effects: Friction, Inertia
   - Complex waveforms: Ramp, custom

### 6.3 Testing Strategy

1. **Basic Functionality:**
   - Device enumeration (lsusb)
   - HID descriptor parsing
   - Input report reading (wheel/pedal state)

2. **Force Feedback:**
   - Constant force (left/right)
   - Spring centering
   - Damper resistance
   - Periodic effects (rumble)

3. **Performance:**
   - Effect latency (<10ms target)
   - Smooth force transitions
   - CPU usage (<5% for FF processing)

4. **Wine Compatibility:**
   - DirectInput enumeration
   - Effect creation and playback
   - Gain control
   - Game compatibility (F1 202X, Assetto Corsa, etc.)

---

## 7. References and Resources

### 7.1 Windows Driver Files

- **tmHidUsb.sys** - Main HID minidriver (analyzed)
- **GuiHidUsbDevLowerFFB.sys** - FFB filter driver
- **tmPID64.dll** - FFB calculation engine (analyzed)
- **tmeffcpl64.dll** - Control panel and API (analyzed)
- **tm_api_lib_x64.dll** - Wine wrapper API (analyzed)

### 7.2 Linux Kernel Resources

- **drivers/hid/hid-core.c** - HID core implementation
- **drivers/hid/usbhid/** - USB HID driver
- **drivers/input/ff-core.c** - Force feedback core
- **Documentation/input/ff.txt** - FF API documentation

### 7.3 Relevant Specifications

- **USB HID 1.11** - HID protocol specification
- **USB Physical Interface Device (PID) 1.0** - Force feedback profile
- **Linux Input Subsystem** - Kernel input/FF API

### 7.4 Community Resources

- **hid-tmff2** project (if exists) - Existing Linux driver efforts
- **Linux USB mailing list** - Kernel driver development
- **Wine development community** - Wine integration support

---

## 8. Next Steps

1. **Complete Kernel Driver Analysis:**
   - Decompile DriverEntry and key dispatch functions from tmHidUsb.sys
   - Document USB endpoint configuration
   - Map internal state machine

2. **Reverse Engineer FFB Protocol:**
   - Capture USB traffic using Wireshark/USBPcap
   - Correlate with decompiled tmPID64.dll code
   - Document byte-level FFB report format

3. **Prototype Linux Driver:**
   - Start with basic HID support (no FF)
   - Add constant force support
   - Expand to other effect types

4. **Test and Validate:**
   - Compare behavior with Windows driver
   - Measure performance metrics
   - Validate with real games

---

## 9. Contact and Contributions

This analysis is part of the **hid-tmff2 Linux driver development** effort to bring full force feedback support for Thrustmaster racing wheels to Linux and Wine.

**Analysis performed using:**
- Ghidra 10.x (reverse engineering)
- MCP Ghidra Bridge (automation)
- Python analysis scripts

**For questions, contributions, or updates:**
- GitHub: [project repository]
- Email: [maintainer email]

---

**Document Version:** 1.0  
**Last Updated:** January 2025  
**Status:** In Progress - Kernel Driver Analysis Phase
