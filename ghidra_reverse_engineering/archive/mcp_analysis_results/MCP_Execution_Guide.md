# T500RS MCP Analysis Execution Guide

## AI Agent MCP Tool Execution Sequence

### Step 1: tmPID64.DLL

**Priority:** 1 | **Port:** 8195
**Description:** Core PID/Force Feedback Library

**MCP Tool Sequence:**
1. `instances_use(port=8195)`
2. `data_list_strings(limit=200)`
3. `functions_list(limit=100)`
4. `functions_decompile()` for key functions
5. `xrefs_list()` for critical strings
6. Detailed analysis of force feedback protocols

**Expected Key Findings:**
- **Critical Functions:** HidD_SetFeature, HidD_GetFeature, HidP_SetScaledUsageValue
- **Ff Protocols:** Feature reports, Effect parameters, Force magnitude
- **Linux Mapping:** Core FF implementation - maps to Linux ff_effect structures

### Step 2: tmeffcpl64.dll

**Priority:** 2 | **Port:** 8193
**Description:** Force Feedback Control Panel

**MCP Tool Sequence:**
1. `instances_use(port=8193)`
2. `data_list_strings(limit=200)`
3. `functions_list(limit=100)`
4. `functions_decompile()` for key functions
5. `xrefs_list()` for critical strings
6. Detailed analysis of force feedback protocols

**Expected Key Findings:**
- **Critical Functions:** tm_api_force_config_effect, tm_api_force_set_effect_state
- **Ff Protocols:** Effect configuration, State management, Device control
- **Linux Mapping:** Control interface - maps to Linux FF ioctls

### Step 3: tm_api_lib_x64.dll

**Priority:** 2 | **Port:** 8200
**Description:** Public API Library

**MCP Tool Sequence:**
1. `instances_use(port=8200)`
2. `data_list_strings(limit=200)`
3. `functions_list(limit=100)`
4. `functions_decompile()` for key functions
5. `xrefs_list()` for critical strings
6. Detailed analysis of force feedback protocols

**Expected Key Findings:**
- **Critical Functions:** Device enumeration, Effect creation, Public API
- **Ff Protocols:** High-level API, Application interface, Device management
- **Linux Mapping:** Wine DLL target - wraps Linux FF API for Windows apps

### Step 4: tmJoycpl.exe

**Priority:** 3 | **Port:** 8199
**Description:** Joystick Control Panel

**MCP Tool Sequence:**
1. `instances_use(port=8199)`
2. `data_list_strings(limit=200)`
3. `functions_list(limit=100)`
4. `xrefs_list()` for important strings
5. Analysis of device configuration interfaces

**Expected Key Findings:**
- **Critical Functions:** Test protocols, Configuration UI, Device calibration
- **Ff Protocols:** Testing interface, Configuration storage, User controls
- **Linux Mapping:** Reference for testing - shows effect parameters and ranges

### Step 5: GuiHidUsbDevLowerFFB.sys

**Priority:** 4 | **Port:** 8196
**Description:** Low-level USB HID FFB Driver

**MCP Tool Sequence:**
1. `instances_use(port=8196)`
2. `data_list_strings(limit=200)`
3. `functions_list(limit=100)`
4. Basic string and function analysis
5. Focus on device management protocols

**Expected Key Findings:**
- **General:** Device support component

### Step 6: tmHidUsb.sys

**Priority:** 4 | **Port:** 8194
**Description:** Main USB HID Driver

**MCP Tool Sequence:**
1. `instances_use(port=8194)`
2. `data_list_strings(limit=200)`
3. `functions_list(limit=100)`
4. Basic string and function analysis
5. Focus on device management protocols

**Expected Key Findings:**
- **General:** Device support component

### Step 7: tmResetMin.sys

**Priority:** 5 | **Port:** 8197
**Description:** Device Reset Driver

**MCP Tool Sequence:**
1. `instances_use(port=8197)`
2. `data_list_strings(limit=200)`
3. `functions_list(limit=100)`
4. Basic string and function analysis
5. Focus on device management protocols

**Expected Key Findings:**
- **General:** Device support component

### Step 8: tmInstall.exe

**Priority:** 5 | **Port:** 8198
**Description:** Installation Program

**MCP Tool Sequence:**
1. `instances_use(port=8198)`
2. `data_list_strings(limit=200)`
3. `functions_list(limit=100)`
4. Basic string and function analysis
5. Focus on device management protocols

**Expected Key Findings:**
- **General:** Device support component

