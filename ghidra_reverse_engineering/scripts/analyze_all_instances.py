#!/usr/bin/env python3
"""
Multi-Instance T500RS Driver Analysis
=====================================

This script analyzes all loaded T500RS programs across multiple Ghidra instances
for comprehensive reverse engineering to support Linux force feedback driver development.

Focus Areas:
- Force feedback protocols and commands
- Device communication interfaces  
- Configuration and testing capabilities
- Wine/Linux compatibility structures
"""

import json
import requests
import time
from pathlib import Path

# Configuration
OUTPUT_DIR = Path("analysis_results_real")
OUTPUT_DIR.mkdir(exist_ok=True)

# Known T500RS program instances
INSTANCES = [
    {"port": 8193, "program": "tmeffcpl64.dll", "description": "Force Feedback Control Panel"},
    {"port": 8195, "program": "tmPID64.DLL", "description": "Core PID/Force Feedback Library"},
    {"port": 8196, "program": "GuiHidUsbDevLowerFFB.sys", "description": "Low-level USB HID FFB Driver"},
    {"port": 8194, "program": "tmHidUsb.sys", "description": "Main USB HID Driver"},
    {"port": 8199, "program": "tmJoycpl.exe", "description": "Joystick Control Panel"},
    {"port": 8200, "program": "tm_api_lib_x64.dll", "description": "Public API Library"},
    {"port": 8197, "program": "tmResetMin.sys", "description": "Device Reset Driver"},
    {"port": 8198, "program": "tmInstall.exe", "description": "Installation Program"}
]

class GhidraMCP:
    def __init__(self, url):
        self.url = url
        self.session = requests.Session()
    
    def call_api(self, endpoint, data=None):
        """Call MCP API endpoint via HTTP POST"""
        try:
            response = self.session.post(f"{self.url}/{endpoint}", 
                                       json=data if data else {},
                                       timeout=30)
            if response.status_code == 200:
                result = response.json()
                return result.get('result', result)
            else:
                print(f"  ❌ API Error {response.status_code}: {response.text}")
                return None
        except Exception as e:
            print(f"  ❌ Connection error: {e}")
            return None

def analyze_program(instance_info):
    """Analyze a single program instance"""
    
    port = instance_info["port"]
    program = instance_info["program"]
    description = instance_info["description"]
    
    print(f"\n{'='*80}")
    print(f"🔍 ANALYZING: {program} (Port {port})")
    print(f"📋 Description: {description}")
    print(f"{'='*80}")
    
    mcp = GhidraMCP(f"http://localhost:{port}")
    
    # Test connection
    functions_result = mcp.call_api("functions", {"limit": 1})
    if not functions_result:
        print(f"❌ Could not connect to {program} on port {port}")
        return None
    
    print("✅ Connected successfully")
    
    # Get all functions
    print("📋 Getting function list...")
    all_functions_result = mcp.call_api("functions", {"limit": 2000})
    if not all_functions_result:
        print("❌ Failed to get function list")
        return None
    
    functions = all_functions_result if isinstance(all_functions_result, list) else []
    print(f"   Found {len(functions)} functions")
    
    # Get strings  
    print("📋 Getting strings...")
    strings_result = mcp.call_api("data/strings", {"limit": 2000})
    strings = strings_result if isinstance(strings_result, list) else []
    print(f"   Found {len(strings)} strings")
    
    # Analysis structure
    analysis = {
        "program": program,
        "port": port,
        "description": description,
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "functions": {
            "total": len(functions),
            "analyzed": 0,
            "relevant": [],
            "key_exports": []
        },
        "strings": {
            "total": len(strings),
            "relevant": []
        },
        "protocols": {
            "force_feedback": [],
            "usb_hid": [],
            "device_communication": []
        },
        "key_findings": [],
        "linux_compatibility": {
            "ff_api_mappings": [],
            "wine_integration": [],
            "ioctl_equivalents": []
        }
    }
    
    # Enhanced keywords for T500RS/Linux compatibility analysis
    ff_keywords = [
        "force", "feedback", "ffb", "effect", "periodic", "constant", "spring", 
        "damper", "friction", "inertia", "ramp", "custom", "sine", "square", "triangle"
    ]
    
    device_keywords = [
        "hid", "usb", "device", "wheel", "pedal", "clutch", "gear", "axis",
        "thrustmaster", "t500", "t300", "pid", "report", "endpoint"
    ]
    
    control_keywords = [
        "ioctl", "createfile", "writedata", "readdata", "control", "config", 
        "test", "calibrate", "reset", "enable", "disable", "start", "stop"
    ]
    
    all_keywords = ff_keywords + device_keywords + control_keywords
    
    # Analyze strings for relevance
    print("🔍 Analyzing strings for Linux FF compatibility...")
    for string_data in strings:
        string_value = string_data.get("value", "").lower() if isinstance(string_data, dict) else str(string_data).lower()
        address = string_data.get("address", "") if isinstance(string_data, dict) else ""
        
        if any(keyword in string_value for keyword in all_keywords):
            category = "general"
            if any(kw in string_value for kw in ff_keywords):
                category = "force_feedback" 
            elif any(kw in string_value for kw in device_keywords):
                category = "device_communication"
            elif any(kw in string_value for kw in control_keywords):
                category = "control_interface"
                
            analysis["strings"]["relevant"].append({
                "address": address,
                "value": string_data.get("value", string_value) if isinstance(string_data, dict) else string_data,
                "category": category
            })
    
    print(f"   Found {len(analysis['strings']['relevant'])} relevant strings")
    
    # Analyze key functions
    print("🔍 Analyzing functions for Linux compatibility...")
    MAX_FUNCTIONS = 50  # Analyze more functions for comprehensive coverage
    
    for i, func in enumerate(functions[:MAX_FUNCTIONS]):
        if isinstance(func, dict):
            func_name = func.get("name", "")
            func_addr = func.get("address", "")
        else:
            func_name = str(func)
            func_addr = ""
        
        if not func_name:
            continue
            
        print(f"   [{i+1}/{min(MAX_FUNCTIONS, len(functions))}] {func_name}")
        
        # Get function details
        func_details = mcp.call_api(f"functions/{func_addr or func_name}", {})
        
        # Get decompiled code
        decompiled = mcp.call_api(f"functions/{func_addr or func_name}/decompile", {})
        decompiled_code = ""
        if decompiled:
            decompiled_code = decompiled.get("decompiled_code", decompiled.get("code", ""))
        
        # Get variables
        variables_result = mcp.call_api(f"functions/{func_addr or func_name}/variables", {})
        variables = variables_result if isinstance(variables_result, list) else []
        
        # Check for exports/public functions
        is_export = not func_name.startswith("FUN_") and not func_name.startswith("thunk_")
        
        func_analysis = {
            "name": func_name,
            "address": func_addr,
            "size": func_details.get("size", 0) if func_details else 0,
            "is_export": is_export,
            "variables": len(variables),
            "relevance_score": 0,
            "categories": [],
            "decompiled_code": decompiled_code,
            "linux_notes": []
        }
        
        # Analyze relevance for Linux FF implementation
        code_lower = decompiled_code.lower() if decompiled_code else ""
        name_lower = func_name.lower()
        
        # Score relevance
        if any(kw in name_lower for kw in ff_keywords):
            func_analysis["relevance_score"] += 10
            func_analysis["categories"].append("force_feedback")
            
        if any(kw in name_lower for kw in device_keywords):
            func_analysis["relevance_score"] += 8
            func_analysis["categories"].append("device_communication")
            
        if any(kw in name_lower for kw in control_keywords):
            func_analysis["relevance_score"] += 6
            func_analysis["categories"].append("control_interface")
            
        if any(kw in code_lower for kw in all_keywords):
            func_analysis["relevance_score"] += 3
        
        # Special handling for key function patterns
        if any(pattern in name_lower for pattern in ["seteffect", "createeffect", "playeffect", "stopeffect"]):
            func_analysis["relevance_score"] += 15
            func_analysis["linux_notes"].append("Maps to Linux FF_EFFECT ioctls")
            
        if any(pattern in name_lower for pattern in ["devicecontrol", "sendreport", "getreport"]):
            func_analysis["relevance_score"] += 12
            func_analysis["linux_notes"].append("Maps to hidraw or uinput interfaces")
            
        if is_export and func_analysis["relevance_score"] > 0:
            analysis["functions"]["key_exports"].append(func_analysis)
            
        if func_analysis["relevance_score"] >= 5:
            analysis["functions"]["relevant"].append(func_analysis)
            analysis["key_findings"].append(f"Relevant function: {func_name} (score: {func_analysis['relevance_score']})")
        
        analysis["functions"]["analyzed"] += 1
    
    # Sort by relevance
    analysis["functions"]["relevant"].sort(key=lambda x: x["relevance_score"], reverse=True)
    analysis["functions"]["key_exports"].sort(key=lambda x: x["relevance_score"], reverse=True)
    
    return analysis

def generate_reports(all_analyses):
    """Generate comprehensive analysis reports"""
    
    print(f"\n🎉 GENERATING COMPREHENSIVE REPORTS")
    print("="*80)
    
    # Combined analysis
    combined_analysis = {
        "analysis_date": time.strftime("%Y-%m-%d %H:%M:%S"),
        "programs_analyzed": len([a for a in all_analyses if a]),
        "total_functions": sum(a["functions"]["total"] for a in all_analyses if a),
        "total_relevant_functions": sum(len(a["functions"]["relevant"]) for a in all_analyses if a),
        "programs": all_analyses,
        "linux_driver_recommendations": []
    }
    
    # Save individual program analyses
    for analysis in all_analyses:
        if not analysis:
            continue
            
        program = analysis["program"]
        
        # JSON report
        json_file = OUTPUT_DIR / f"{program}_analysis.json"
        with open(json_file, 'w') as f:
            json.dump(analysis, f, indent=2)
        print(f"📄 {program} JSON: {json_file}")
        
        # Markdown summary
        md_file = OUTPUT_DIR / f"{program}_summary.md"
        with open(md_file, 'w') as f:
            f.write(f"# {program} Analysis\n\n")
            f.write(f"**{analysis['description']}**\n\n")
            f.write(f"*Analysis Date:* {analysis['timestamp']}\n\n")
            
            f.write("## Overview\n\n")
            f.write(f"- **Total Functions:** {analysis['functions']['total']}\n")
            f.write(f"- **Relevant Functions:** {len(analysis['functions']['relevant'])}\n")
            f.write(f"- **Key Exports:** {len(analysis['functions']['key_exports'])}\n")
            f.write(f"- **Relevant Strings:** {len(analysis['strings']['relevant'])}\n\n")
            
            if analysis['functions']['key_exports']:
                f.write("## Key Exported Functions (Linux FF Mapping)\n\n")
                for func in analysis['functions']['key_exports'][:10]:
                    f.write(f"### {func['name']} (Score: {func['relevance_score']})\n\n")
                    f.write(f"- **Address:** `{func['address']}`\n")
                    f.write(f"- **Categories:** {', '.join(func['categories'])}\n")
                    if func['linux_notes']:
                        f.write(f"- **Linux Notes:** {'; '.join(func['linux_notes'])}\n")
                    f.write("\n")
                    
                    if func['decompiled_code']:
                        f.write("**Decompiled Code:**\n```c\n")
                        f.write(func['decompiled_code'][:800])
                        if len(func['decompiled_code']) > 800:
                            f.write("\n... (truncated)")
                        f.write("\n```\n\n")
            
            if analysis['strings']['relevant']:
                f.write("## Relevant Strings by Category\n\n")
                categories = {}
                for string in analysis['strings']['relevant']:
                    cat = string['category']
                    if cat not in categories:
                        categories[cat] = []
                    categories[cat].append(string)
                
                for category, strings in categories.items():
                    f.write(f"### {category.replace('_', ' ').title()}\n\n")
                    for string in strings[:15]:
                        f.write(f"- `{string.get('address', 'N/A')}`: \"{string['value']}\"\n")
                    f.write("\n")
        
        print(f"📋 {program} Summary: {md_file}")
    
    # Master report
    master_file = OUTPUT_DIR / "T500RS_Linux_Driver_Analysis.md"
    with open(master_file, 'w') as f:
        f.write("# T500RS Linux Driver Development Analysis\n\n")
        f.write(f"**Comprehensive Reverse Engineering Report**\n\n")
        f.write(f"*Analysis Date:* {combined_analysis['analysis_date']}\n\n")
        
        f.write("## Executive Summary\n\n")
        f.write(f"This analysis covers {combined_analysis['programs_analyzed']} T500RS driver components ")
        f.write(f"with {combined_analysis['total_functions']} total functions and ")
        f.write(f"{combined_analysis['total_relevant_functions']} functions relevant for Linux force feedback implementation.\n\n")
        
        f.write("## Component Overview\n\n")
        f.write("| Component | Functions | Relevant | Key Exports | Purpose |\n")
        f.write("|-----------|-----------|----------|-------------|----------|\n")
        
        for analysis in all_analyses:
            if analysis:
                f.write(f"| {analysis['program']} | {analysis['functions']['total']} | ")
                f.write(f"{len(analysis['functions']['relevant'])} | ")
                f.write(f"{len(analysis['functions']['key_exports'])} | ")
                f.write(f"{analysis['description']} |\n")
        
        f.write("\n## Linux Implementation Strategy\n\n")
        f.write("### Force Feedback API Integration\n\n")
        f.write("The Linux force feedback subsystem (`/dev/input/eventX`) uses the following structure:\n\n")
        f.write("```c\n")
        f.write("struct ff_effect {\n")
        f.write("    __u16 type;     // FF_CONSTANT, FF_PERIODIC, FF_RAMP, etc.\n")
        f.write("    __s16 id;       // Effect ID\n")
        f.write("    __u16 direction; // Direction (0-360 degrees)\n")
        f.write("    struct ff_trigger trigger;\n")
        f.write("    struct ff_replay replay;\n")
        f.write("    union {\n")
        f.write("        struct ff_constant_effect constant;\n")
        f.write("        struct ff_periodic_effect periodic;\n")
        f.write("        struct ff_ramp_effect ramp;\n")
        f.write("        struct ff_condition_effect condition;\n")
        f.write("    } u;\n")
        f.write("};\n")
        f.write("```\n\n")
        
        f.write("### Wine Integration Points\n\n")
        f.write("For Wine compatibility, implement:\n\n")
        f.write("1. **DirectInput Force Feedback Translation**\n")
        f.write("   - Map Windows `DIEFFECT` structures to Linux `ff_effect`\n")
        f.write("   - Implement `IDirectInputEffect` interface\n\n")
        f.write("2. **HID Device Emulation**\n")
        f.write("   - Use `uhid` kernel module for userspace HID devices\n")
        f.write("   - Translate Windows HID reports to Linux HID reports\n\n")
        f.write("3. **Registry/Configuration**\n")
        f.write("   - Map Thrustmaster registry settings to Linux config files\n")
        f.write("   - Implement device detection and enumeration\n\n")
    
    # Combined JSON
    combined_file = OUTPUT_DIR / "T500RS_combined_analysis.json"
    with open(combined_file, 'w') as f:
        json.dump(combined_analysis, f, indent=2)
    
    print(f"\n🎯 Master Report: {master_file}")
    print(f"📊 Combined Analysis: {combined_file}")

def main():
    """Main analysis function"""
    
    print("🚀 T500RS Comprehensive Driver Analysis for Linux FF Development")
    print("="*80)
    
    all_analyses = []
    
    for instance in INSTANCES:
        try:
            analysis = analyze_program(instance)
            all_analyses.append(analysis)
            time.sleep(1)  # Brief pause between instances
        except Exception as e:
            print(f"❌ Failed to analyze {instance['program']}: {e}")
            all_analyses.append(None)
    
    # Generate reports
    generate_reports(all_analyses)
    
    print(f"\n🎉 ANALYSIS COMPLETE!")
    print(f"📁 Results saved to: {OUTPUT_DIR.absolute()}")
    
    # Summary
    successful = len([a for a in all_analyses if a])
    print(f"✅ Successfully analyzed: {successful}/{len(INSTANCES)} programs")
    print(f"🔍 Ready for Linux force feedback driver development!")

if __name__ == "__main__":
    main()