#!/usr/bin/env python3
"""
Interactive T500RS Program Analysis
===================================

This script analyzes the currently loaded program in Ghidra via MCP.
You need to manually open each program in Ghidra, then run this script.

Usage:
1. Open Ghidra and load a program (e.g., tmpid.dll)
2. Run this script
3. Repeat for each program you want to analyze
"""

import json
import requests
import time
from pathlib import Path

# Configuration
GHIDRA_MCP_URL = "http://localhost:8192"
OUTPUT_DIR = Path("analysis_results_real")
OUTPUT_DIR.mkdir(exist_ok=True)

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
                return response.json()
            else:
                print(f"  ❌ API Error {response.status_code}: {response.text}")
                return None
        except Exception as e:
            print(f"  ❌ Connection error: {e}")
            return None

def analyze_current_program():
    """Analyze the currently loaded program in Ghidra"""
    
    print("🔍 T500RS Interactive Program Analysis")
    print("=" * 60)
    
    mcp = GhidraMCP(GHIDRA_MCP_URL)
    
    # Test connection
    result = mcp.call_api("functions/list", {"limit": 1})
    if not result:
        print("❌ Could not connect to Ghidra MCP server")
        print("   Make sure Ghidra is running with MCP server on port 8192")
        return
    
    print("✅ Connected to Ghidra MCP server")
    
    # Check if a program is loaded
    functions = result.get("functions", [])
    if not functions:
        print("❌ No program currently loaded in Ghidra")
        print("   Please open a program in Ghidra first, then run this script")
        return
    
    # Get program info by checking first function
    first_func = functions[0]
    program_name = input("\n📝 Enter the program name for this analysis: ").strip()
    if not program_name:
        program_name = "unknown_program"
    
    print(f"\n🎯 Analyzing program: {program_name}")
    print("=" * 60)
    
    # Get all functions
    print("📋 Getting function list...")
    all_functions = mcp.call_api("functions/list", {"limit": 1000})
    if not all_functions:
        print("❌ Failed to get function list")
        return
    
    functions = all_functions.get("functions", [])
    print(f"   Found {len(functions)} functions")
    
    # Get strings
    print("📋 Getting strings...")
    strings_result = mcp.call_api("data/list_strings", {"limit": 1000})
    strings = strings_result.get("strings", []) if strings_result else []
    print(f"   Found {len(strings)} strings")
    
    # Analysis results
    analysis = {
        "program": program_name,
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "functions": {
            "total": len(functions),
            "analyzed": 0,
            "details": []
        },
        "strings": {
            "total": len(strings),
            "relevant": []
        },
        "key_findings": []
    }
    
    # Analyze key strings for relevance
    print("🔍 Analyzing strings for relevance...")
    relevant_keywords = [
        "force", "feedback", "ffb", "effect", "periodic", "constant",
        "spring", "damper", "friction", "inertia", "ramp", "custom",
        "hid", "usb", "device", "wheel", "pedal", "clutch", "gear",
        "thrustmaster", "t500", "t300", "error", "fail", "init"
    ]
    
    for string_data in strings:
        string_value = string_data.get("value", "").lower()
        if any(keyword in string_value for keyword in relevant_keywords):
            analysis["strings"]["relevant"].append({
                "address": string_data.get("address"),
                "value": string_data.get("value"),
                "length": string_data.get("length")
            })
    
    print(f"   Found {len(analysis['strings']['relevant'])} relevant strings")
    
    # Analyze top functions
    print("🔍 Analyzing top functions...")
    MAX_FUNCTIONS = 20
    
    for i, func in enumerate(functions[:MAX_FUNCTIONS]):
        func_name = func.get("name", "")
        func_addr = func.get("address", "")
        
        print(f"   [{i+1}/{min(MAX_FUNCTIONS, len(functions))}] {func_name}")
        
        # Get function details
        func_details = mcp.call_api("functions/get", {"name": func_name})
        if not func_details:
            continue
        
        # Get decompiled code
        decompiled = mcp.call_api("functions/decompile", {"name": func_name})
        decompiled_code = ""
        if decompiled and "decompiled_code" in decompiled:
            decompiled_code = decompiled["decompiled_code"]
        
        # Get variables
        variables = mcp.call_api("functions/get_variables", {"name": func_name})
        var_list = variables.get("variables", []) if variables else []
        
        # Get cross-references
        xrefs = mcp.call_api("xrefs/list", {"to_addr": func_addr, "limit": 50})
        xref_list = xrefs.get("references", []) if xrefs else []
        
        func_analysis = {
            "name": func_name,
            "address": func_addr,
            "size": func_details.get("size", 0),
            "decompiled_code": decompiled_code,
            "variables": var_list,
            "cross_references": len(xref_list),
            "is_relevant": False
        }
        
        # Check relevance
        code_lower = decompiled_code.lower() if decompiled_code else ""
        name_lower = func_name.lower()
        
        if (any(keyword in name_lower for keyword in relevant_keywords) or
            any(keyword in code_lower for keyword in relevant_keywords)):
            func_analysis["is_relevant"] = True
            analysis["key_findings"].append(f"Relevant function: {func_name}")
        
        analysis["functions"]["details"].append(func_analysis)
        analysis["functions"]["analyzed"] += 1
    
    # Save detailed results
    output_file = OUTPUT_DIR / f"{program_name}_analysis.json"
    with open(output_file, 'w') as f:
        json.dump(analysis, f, indent=2)
    
    # Create summary report
    summary_file = OUTPUT_DIR / f"{program_name}_summary.md"
    with open(summary_file, 'w') as f:
        f.write(f"# {program_name} Analysis Report\n\n")
        f.write(f"**Analysis Date:** {analysis['timestamp']}\n\n")
        
        f.write("## Overview\n\n")
        f.write(f"- **Total Functions:** {analysis['functions']['total']}\n")
        f.write(f"- **Analyzed Functions:** {analysis['functions']['analyzed']}\n")
        f.write(f"- **Total Strings:** {analysis['strings']['total']}\n")
        f.write(f"- **Relevant Strings:** {len(analysis['strings']['relevant'])}\n")
        f.write(f"- **Key Findings:** {len(analysis['key_findings'])}\n\n")
        
        if analysis['key_findings']:
            f.write("## Key Findings\n\n")
            for finding in analysis['key_findings']:
                f.write(f"- {finding}\n")
            f.write("\n")
        
        if analysis['strings']['relevant']:
            f.write("## Relevant Strings\n\n")
            for string in analysis['strings']['relevant'][:20]:  # Top 20
                f.write(f"- `{string['address']}`: \"{string['value']}\"\n")
            f.write("\n")
        
        f.write("## Top Functions\n\n")
        relevant_funcs = [f for f in analysis['functions']['details'] if f['is_relevant']]
        if relevant_funcs:
            f.write("### Relevant Functions\n\n")
            for func in relevant_funcs:
                f.write(f"#### {func['name']} (`{func['address']}`)\n\n")
                f.write(f"- **Size:** {func['size']} bytes\n")
                f.write(f"- **Variables:** {len(func['variables'])}\n")
                f.write(f"- **Cross-references:** {func['cross_references']}\n")
                
                if func['decompiled_code']:
                    f.write("\n**Decompiled Code:**\n```c\n")
                    f.write(func['decompiled_code'][:1000])  # Truncate if too long
                    if len(func['decompiled_code']) > 1000:
                        f.write("\n... (truncated)")
                    f.write("\n```\n\n")
    
    print(f"\n🎉 Analysis Complete!")
    print(f"📄 Detailed results: {output_file}")
    print(f"📋 Summary report: {summary_file}")
    print(f"🔍 Found {len([f for f in analysis['functions']['details'] if f['is_relevant']])} relevant functions")

if __name__ == "__main__":
    analyze_current_program()