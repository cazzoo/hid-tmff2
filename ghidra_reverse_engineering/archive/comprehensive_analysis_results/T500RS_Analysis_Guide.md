# T500RS Linux Driver Development Analysis Guide

## Analysis Workflow

### Step 1: Analyze tmPID64.DLL

**Priority Level:** 1

**Description:** Core PID/Force Feedback Library - Most important for FF protocols

**Analysis Commands:**
1. Switch to instance: Use port 8195
2. Get strings: Extract relevant force feedback strings
3. Get functions: List all functions and identify exports
4. Decompile key functions: Focus on FF-related functions
5. Cross-reference analysis: Find string usage in functions
6. Extract protocols: Document communication patterns

**Key Focus Areas:**
- Force feedback effect creation and management
- PID (Physical Interface Device) protocol implementation
- DirectInput to HID translation
- Effect parameter structures

### Step 2: Analyze tmeffcpl64.dll

**Priority Level:** 2

**Description:** Force Feedback Control Panel - Configuration and testing interfaces

**Analysis Commands:**
1. Switch to instance: Use port 8193
2. Get strings: Extract relevant force feedback strings
3. Get functions: List all functions and identify exports
4. Decompile key functions: Focus on FF-related functions
5. Cross-reference analysis: Find string usage in functions
6. Extract protocols: Document communication patterns

**Key Focus Areas:**
- Public API function signatures
- Configuration parameters and registry settings
- Device enumeration and initialization
- Wine DLL integration points

### Step 3: Analyze tm_api_lib_x64.dll

**Priority Level:** 2

**Description:** Public API Library - Wine integration target

**Analysis Commands:**
1. Switch to instance: Use port 8200
2. Get strings: Extract relevant force feedback strings
3. Get functions: List all functions and identify exports
4. Decompile key functions: Focus on FF-related functions
5. Cross-reference analysis: Find string usage in functions
6. Extract protocols: Document communication patterns

**Key Focus Areas:**
- Public API function signatures
- Configuration parameters and registry settings
- Device enumeration and initialization
- Wine DLL integration points

### Step 4: Analyze tmJoycpl.exe

**Priority Level:** 3

**Description:** Joystick Control Panel - Device configuration and testing

**Analysis Commands:**
1. Switch to instance: Use port 8199
2. Get strings: Extract relevant force feedback strings
3. Get functions: List all functions and identify exports
4. Decompile key functions: Focus on FF-related functions
5. Cross-reference analysis: Find string usage in functions
6. Extract protocols: Document communication patterns

**Key Focus Areas:**
- Device testing and calibration interfaces
- Configuration UI command mappings
- Force feedback test protocols
- Device capability detection

### Step 5: Analyze GuiHidUsbDevLowerFFB.sys

**Priority Level:** 4

**Description:** Low-level USB HID FFB Driver - Kernel protocols

**Analysis Commands:**
1. Switch to instance: Use port 8196
2. Get strings: Extract relevant force feedback strings
3. Get functions: List all functions and identify exports
4. Decompile key functions: Focus on FF-related functions
5. Cross-reference analysis: Find string usage in functions
6. Extract protocols: Document communication patterns

**Key Focus Areas:**
- USB HID communication protocols
- Kernel-level device interfaces
- Device state management
- Low-level command structures

### Step 6: Analyze tmHidUsb.sys

**Priority Level:** 4

**Description:** Main USB HID Driver - Device communication

**Analysis Commands:**
1. Switch to instance: Use port 8194
2. Get strings: Extract relevant force feedback strings
3. Get functions: List all functions and identify exports
4. Decompile key functions: Focus on FF-related functions
5. Cross-reference analysis: Find string usage in functions
6. Extract protocols: Document communication patterns

**Key Focus Areas:**
- USB HID communication protocols
- Kernel-level device interfaces
- Device state management
- Low-level command structures

### Step 7: Analyze tmResetMin.sys

**Priority Level:** 5

**Description:** Device Reset Driver - State management

**Analysis Commands:**
1. Switch to instance: Use port 8197
2. Get strings: Extract relevant force feedback strings
3. Get functions: List all functions and identify exports
4. Decompile key functions: Focus on FF-related functions
5. Cross-reference analysis: Find string usage in functions
6. Extract protocols: Document communication patterns

**Key Focus Areas:**
- USB HID communication protocols
- Kernel-level device interfaces
- Device state management
- Low-level command structures

### Step 8: Analyze tmInstall.exe

**Priority Level:** 5

**Description:** Installation Program - System integration

**Analysis Commands:**
1. Switch to instance: Use port 8198
2. Get strings: Extract relevant force feedback strings
3. Get functions: List all functions and identify exports
4. Decompile key functions: Focus on FF-related functions
5. Cross-reference analysis: Find string usage in functions
6. Extract protocols: Document communication patterns

**Key Focus Areas:**
- USB HID communication protocols
- Kernel-level device interfaces
- Device state management
- Low-level command structures

## Linux Force Feedback API Reference

```c
// Linux FF effect types to map from Windows
#define FF_RUMBLE     0x50
#define FF_PERIODIC   0x51
#define FF_CONSTANT   0x52
#define FF_SPRING     0x53
#define FF_FRICTION   0x54
#define FF_DAMPER     0x55
#define FF_INERTIA    0x56
#define FF_RAMP       0x57

// Periodic effect subtypes
#define FF_SQUARE     0x58
#define FF_TRIANGLE   0x59
#define FF_SINE       0x5a
#define FF_SAW_UP     0x5b
#define FF_SAW_DOWN   0x5c
#define FF_CUSTOM     0x5d
```

## Wine Integration Strategy

1. **DLL Wrapper Approach:**
   - Create Wine DLL that wraps tm_api_lib_x64.dll functions
   - Translate Windows calls to Linux FF API calls
   - Use HIDRAW or UHID for device communication

2. **DirectInput Bridge:**
   - Implement IDirectInputEffect interface
   - Map DIEFFECT structures to ff_effect structures
   - Handle device enumeration through Linux input subsystem

3. **Configuration Translation:**
   - Map Windows registry settings to Linux config files
   - Translate device-specific parameters
   - Implement device capability detection

