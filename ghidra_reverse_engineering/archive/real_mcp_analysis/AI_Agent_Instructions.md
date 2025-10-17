# AI Agent MCP Execution Instructions

## Overview

Execute the following MCP tool calls systematically to perform
comprehensive reverse engineering of T500RS force feedback drivers.

## Execution Sequence

### 1. tmPID64.DLL

**Port:** 8195
**Priority:** 1
**Critical:** True
**Description:** Core PID/Force Feedback Library

**MCP Tool Calls:**

1. **instances_use**
   - Parameters: `{'port': 8195}`
   - Purpose: Switch to tmPID64.DLL
   - MCP Call: `call_mcp_tool(name='instances_use', input={'port': 8195})`

2. **data_list_strings**
   - Parameters: `{'limit': 500}`
   - Purpose: Extract all strings for protocol analysis
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'limit': 500})`

3. **functions_list**
   - Parameters: `{'limit': 200}`
   - Purpose: Get complete function list
   - MCP Call: `call_mcp_tool(name='functions_list', input={'limit': 200})`

4. **data_list_strings**
   - Parameters: `{'filter': 'force', 'limit': 100}`
   - Purpose: Find force_feedback strings containing 'force'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'force', 'limit': 100})`

5. **data_list_strings**
   - Parameters: `{'filter': 'feedback', 'limit': 100}`
   - Purpose: Find force_feedback strings containing 'feedback'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'feedback', 'limit': 100})`

6. **data_list_strings**
   - Parameters: `{'filter': 'ffb', 'limit': 100}`
   - Purpose: Find force_feedback strings containing 'ffb'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'ffb', 'limit': 100})`

7. **data_list_strings**
   - Parameters: `{'filter': 'hid', 'limit': 100}`
   - Purpose: Find hid_communication strings containing 'hid'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'hid', 'limit': 100})`

8. **data_list_strings**
   - Parameters: `{'filter': 'feature', 'limit': 100}`
   - Purpose: Find hid_communication strings containing 'feature'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'feature', 'limit': 100})`

9. **data_list_strings**
   - Parameters: `{'filter': 'report', 'limit': 100}`
   - Purpose: Find hid_communication strings containing 'report'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'report', 'limit': 100})`

10. **data_list_strings**
   - Parameters: `{'filter': 'device', 'limit': 100}`
   - Purpose: Find device_management strings containing 'device'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'device', 'limit': 100})`

11. **data_list_strings**
   - Parameters: `{'filter': 'enum', 'limit': 100}`
   - Purpose: Find device_management strings containing 'enum'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'enum', 'limit': 100})`

12. **data_list_strings**
   - Parameters: `{'filter': 'open', 'limit': 100}`
   - Purpose: Find device_management strings containing 'open'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'open', 'limit': 100})`

13. **data_list_strings**
   - Parameters: `{'filter': 'tm_api', 'limit': 100}`
   - Purpose: Find api_functions strings containing 'tm_api'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'tm_api', 'limit': 100})`

14. **data_list_strings**
   - Parameters: `{'filter': 'api', 'limit': 100}`
   - Purpose: Find api_functions strings containing 'api'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'api', 'limit': 100})`

15. **data_list_strings**
   - Parameters: `{'filter': 'export', 'limit': 100}`
   - Purpose: Find api_functions strings containing 'export'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'export', 'limit': 100})`

16. **data_list_strings**
   - Parameters: `{'filter': 'struct', 'limit': 100}`
   - Purpose: Find data_structures strings containing 'struct'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'struct', 'limit': 100})`

17. **data_list_strings**
   - Parameters: `{'filter': 'typedef', 'limit': 100}`
   - Purpose: Find data_structures strings containing 'typedef'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'typedef', 'limit': 100})`

18. **data_list_strings**
   - Parameters: `{'filter': 'size', 'limit': 100}`
   - Purpose: Find data_structures strings containing 'size'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'size', 'limit': 100})`

**Expected Findings:**
- **Protocols:** HID feature reports, Force feedback commands, Effect parameters
- **Structures:** Effect data, Device state, HID report formats
- **Functions:** HidD_SetFeature, HidD_GetFeature, Effect creation, Parameter encoding

### 2. tmeffcpl64.dll

**Port:** 8193
**Priority:** 2
**Critical:** True
**Description:** Force Feedback Control Panel

**MCP Tool Calls:**

1. **instances_use**
   - Parameters: `{'port': 8193}`
   - Purpose: Switch to tmeffcpl64.dll
   - MCP Call: `call_mcp_tool(name='instances_use', input={'port': 8193})`

2. **data_list_strings**
   - Parameters: `{'limit': 500}`
   - Purpose: Extract all strings for protocol analysis
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'limit': 500})`

3. **functions_list**
   - Parameters: `{'limit': 200}`
   - Purpose: Get complete function list
   - MCP Call: `call_mcp_tool(name='functions_list', input={'limit': 200})`

4. **data_list_strings**
   - Parameters: `{'filter': 'force', 'limit': 100}`
   - Purpose: Find force_feedback strings containing 'force'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'force', 'limit': 100})`

5. **data_list_strings**
   - Parameters: `{'filter': 'feedback', 'limit': 100}`
   - Purpose: Find force_feedback strings containing 'feedback'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'feedback', 'limit': 100})`

6. **data_list_strings**
   - Parameters: `{'filter': 'ffb', 'limit': 100}`
   - Purpose: Find force_feedback strings containing 'ffb'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'ffb', 'limit': 100})`

7. **data_list_strings**
   - Parameters: `{'filter': 'hid', 'limit': 100}`
   - Purpose: Find hid_communication strings containing 'hid'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'hid', 'limit': 100})`

8. **data_list_strings**
   - Parameters: `{'filter': 'feature', 'limit': 100}`
   - Purpose: Find hid_communication strings containing 'feature'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'feature', 'limit': 100})`

9. **data_list_strings**
   - Parameters: `{'filter': 'report', 'limit': 100}`
   - Purpose: Find hid_communication strings containing 'report'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'report', 'limit': 100})`

10. **data_list_strings**
   - Parameters: `{'filter': 'device', 'limit': 100}`
   - Purpose: Find device_management strings containing 'device'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'device', 'limit': 100})`

11. **data_list_strings**
   - Parameters: `{'filter': 'enum', 'limit': 100}`
   - Purpose: Find device_management strings containing 'enum'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'enum', 'limit': 100})`

12. **data_list_strings**
   - Parameters: `{'filter': 'open', 'limit': 100}`
   - Purpose: Find device_management strings containing 'open'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'open', 'limit': 100})`

13. **data_list_strings**
   - Parameters: `{'filter': 'tm_api', 'limit': 100}`
   - Purpose: Find api_functions strings containing 'tm_api'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'tm_api', 'limit': 100})`

14. **data_list_strings**
   - Parameters: `{'filter': 'api', 'limit': 100}`
   - Purpose: Find api_functions strings containing 'api'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'api', 'limit': 100})`

15. **data_list_strings**
   - Parameters: `{'filter': 'export', 'limit': 100}`
   - Purpose: Find api_functions strings containing 'export'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'export', 'limit': 100})`

16. **data_list_strings**
   - Parameters: `{'filter': 'struct', 'limit': 100}`
   - Purpose: Find data_structures strings containing 'struct'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'struct', 'limit': 100})`

17. **data_list_strings**
   - Parameters: `{'filter': 'typedef', 'limit': 100}`
   - Purpose: Find data_structures strings containing 'typedef'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'typedef', 'limit': 100})`

18. **data_list_strings**
   - Parameters: `{'filter': 'size', 'limit': 100}`
   - Purpose: Find data_structures strings containing 'size'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'size', 'limit': 100})`

**Expected Findings:**
- **Protocols:** tm_api functions, Configuration storage, Device enumeration
- **Structures:** Device info, Effect configs, Registry data
- **Functions:** tm_api_*, Registry access, UI callbacks, Device control

### 3. tm_api_lib_x64.dll

**Port:** 8200
**Priority:** 2
**Critical:** True
**Description:** Public API Library

**MCP Tool Calls:**

1. **instances_use**
   - Parameters: `{'port': 8200}`
   - Purpose: Switch to tm_api_lib_x64.dll
   - MCP Call: `call_mcp_tool(name='instances_use', input={'port': 8200})`

2. **data_list_strings**
   - Parameters: `{'limit': 500}`
   - Purpose: Extract all strings for protocol analysis
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'limit': 500})`

3. **functions_list**
   - Parameters: `{'limit': 200}`
   - Purpose: Get complete function list
   - MCP Call: `call_mcp_tool(name='functions_list', input={'limit': 200})`

4. **data_list_strings**
   - Parameters: `{'filter': 'force', 'limit': 100}`
   - Purpose: Find force_feedback strings containing 'force'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'force', 'limit': 100})`

5. **data_list_strings**
   - Parameters: `{'filter': 'feedback', 'limit': 100}`
   - Purpose: Find force_feedback strings containing 'feedback'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'feedback', 'limit': 100})`

6. **data_list_strings**
   - Parameters: `{'filter': 'ffb', 'limit': 100}`
   - Purpose: Find force_feedback strings containing 'ffb'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'ffb', 'limit': 100})`

7. **data_list_strings**
   - Parameters: `{'filter': 'hid', 'limit': 100}`
   - Purpose: Find hid_communication strings containing 'hid'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'hid', 'limit': 100})`

8. **data_list_strings**
   - Parameters: `{'filter': 'feature', 'limit': 100}`
   - Purpose: Find hid_communication strings containing 'feature'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'feature', 'limit': 100})`

9. **data_list_strings**
   - Parameters: `{'filter': 'report', 'limit': 100}`
   - Purpose: Find hid_communication strings containing 'report'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'report', 'limit': 100})`

10. **data_list_strings**
   - Parameters: `{'filter': 'device', 'limit': 100}`
   - Purpose: Find device_management strings containing 'device'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'device', 'limit': 100})`

11. **data_list_strings**
   - Parameters: `{'filter': 'enum', 'limit': 100}`
   - Purpose: Find device_management strings containing 'enum'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'enum', 'limit': 100})`

12. **data_list_strings**
   - Parameters: `{'filter': 'open', 'limit': 100}`
   - Purpose: Find device_management strings containing 'open'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'open', 'limit': 100})`

13. **data_list_strings**
   - Parameters: `{'filter': 'tm_api', 'limit': 100}`
   - Purpose: Find api_functions strings containing 'tm_api'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'tm_api', 'limit': 100})`

14. **data_list_strings**
   - Parameters: `{'filter': 'api', 'limit': 100}`
   - Purpose: Find api_functions strings containing 'api'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'api', 'limit': 100})`

15. **data_list_strings**
   - Parameters: `{'filter': 'export', 'limit': 100}`
   - Purpose: Find api_functions strings containing 'export'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'export', 'limit': 100})`

16. **data_list_strings**
   - Parameters: `{'filter': 'struct', 'limit': 100}`
   - Purpose: Find data_structures strings containing 'struct'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'struct', 'limit': 100})`

17. **data_list_strings**
   - Parameters: `{'filter': 'typedef', 'limit': 100}`
   - Purpose: Find data_structures strings containing 'typedef'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'typedef', 'limit': 100})`

18. **data_list_strings**
   - Parameters: `{'filter': 'size', 'limit': 100}`
   - Purpose: Find data_structures strings containing 'size'
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'filter': 'size', 'limit': 100})`

**Expected Findings:**
- **Protocols:** Public API, DirectInput bridge, Application interface
- **Structures:** API parameters, Device handles, Effect descriptors
- **Functions:** Export functions, SetupAPI calls, Device enumeration, DirectInput

### 4. tmJoycpl.exe

**Port:** 8199
**Priority:** 3
**Critical:** False
**Description:** Joystick Control Panel

**MCP Tool Calls:**

1. **instances_use**
   - Parameters: `{'port': 8199}`
   - Purpose: Switch to tmJoycpl.exe
   - MCP Call: `call_mcp_tool(name='instances_use', input={'port': 8199})`

2. **data_list_strings**
   - Parameters: `{'limit': 500}`
   - Purpose: Extract all strings for protocol analysis
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'limit': 500})`

3. **functions_list**
   - Parameters: `{'limit': 200}`
   - Purpose: Get complete function list
   - MCP Call: `call_mcp_tool(name='functions_list', input={'limit': 200})`

**Expected Findings:**
- **General:** Device support functions

### 5. GuiHidUsbDevLowerFFB.sys

**Port:** 8196
**Priority:** 4
**Critical:** False
**Description:** Low-level USB HID FFB Driver

**MCP Tool Calls:**

1. **instances_use**
   - Parameters: `{'port': 8196}`
   - Purpose: Switch to GuiHidUsbDevLowerFFB.sys
   - MCP Call: `call_mcp_tool(name='instances_use', input={'port': 8196})`

2. **data_list_strings**
   - Parameters: `{'limit': 500}`
   - Purpose: Extract all strings for protocol analysis
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'limit': 500})`

3. **functions_list**
   - Parameters: `{'limit': 200}`
   - Purpose: Get complete function list
   - MCP Call: `call_mcp_tool(name='functions_list', input={'limit': 200})`

**Expected Findings:**
- **General:** Device support functions

### 6. tmHidUsb.sys

**Port:** 8194
**Priority:** 4
**Critical:** False
**Description:** Main USB HID Driver

**MCP Tool Calls:**

1. **instances_use**
   - Parameters: `{'port': 8194}`
   - Purpose: Switch to tmHidUsb.sys
   - MCP Call: `call_mcp_tool(name='instances_use', input={'port': 8194})`

2. **data_list_strings**
   - Parameters: `{'limit': 500}`
   - Purpose: Extract all strings for protocol analysis
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'limit': 500})`

3. **functions_list**
   - Parameters: `{'limit': 200}`
   - Purpose: Get complete function list
   - MCP Call: `call_mcp_tool(name='functions_list', input={'limit': 200})`

**Expected Findings:**
- **General:** Device support functions

### 7. tmResetMin.sys

**Port:** 8197
**Priority:** 5
**Critical:** False
**Description:** Device Reset Driver

**MCP Tool Calls:**

1. **instances_use**
   - Parameters: `{'port': 8197}`
   - Purpose: Switch to tmResetMin.sys
   - MCP Call: `call_mcp_tool(name='instances_use', input={'port': 8197})`

2. **data_list_strings**
   - Parameters: `{'limit': 500}`
   - Purpose: Extract all strings for protocol analysis
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'limit': 500})`

3. **functions_list**
   - Parameters: `{'limit': 200}`
   - Purpose: Get complete function list
   - MCP Call: `call_mcp_tool(name='functions_list', input={'limit': 200})`

**Expected Findings:**
- **General:** Device support functions

### 8. tmInstall.exe

**Port:** 8198
**Priority:** 5
**Critical:** False
**Description:** Installation Program

**MCP Tool Calls:**

1. **instances_use**
   - Parameters: `{'port': 8198}`
   - Purpose: Switch to tmInstall.exe
   - MCP Call: `call_mcp_tool(name='instances_use', input={'port': 8198})`

2. **data_list_strings**
   - Parameters: `{'limit': 500}`
   - Purpose: Extract all strings for protocol analysis
   - MCP Call: `call_mcp_tool(name='data_list_strings', input={'limit': 500})`

3. **functions_list**
   - Parameters: `{'limit': 200}`
   - Purpose: Get complete function list
   - MCP Call: `call_mcp_tool(name='functions_list', input={'limit': 200})`

**Expected Findings:**
- **General:** Device support functions

## Critical Analysis Points

Focus on extracting:
1. **HID Feature Report Structures** - Protocol format and IDs
2. **Force Feedback Parameter Encoding** - How effects are structured
3. **Device Communication Sequences** - Initialization and control flows
4. **API Function Signatures** - Wine integration requirements
5. **Data Structure Layouts** - Memory organization and field offsets

## Result Processing

After each analysis phase:
1. Save raw MCP results to JSON files
2. Extract protocol specifications from decompiled code
3. Document Linux FF API mapping requirements
4. Generate comprehensive technical documentation

