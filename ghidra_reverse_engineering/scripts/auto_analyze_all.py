#!/usr/bin/env python3
"""
Automated Ghidra MCP Analysis Script
Systematically analyzes all T500RS driver components
"""

import requests
import json
import time
from pathlib import Path

BASE_URL = "http://localhost:8193"
OUTPUT_DIR = Path("/home/caz/Documents/hid-tmff2/ghidra_reverse_engineering")

# Key functions to analyze in tmpid.dll
TMPID_KEY_FUNCTIONS = [
    {"pattern": "SetPeriodic", "priority": 1},
    {"pattern": "SetConstant", "priority": 1},
    {"pattern": "SetEnvelope", "priority": 1},
    {"pattern": "SetCondition", "priority": 1},
    {"pattern": "SetEffect", "priority": 1},
    {"pattern": "EffectOperation", "priority": 1},
    {"pattern": "DeviceControl", "priority": 2},
    {"pattern": "DeviceGain", "priority": 2},
    {"pattern": "Start", "priority": 2},
    {"pattern": "Stop", "priority": 2},
    {"pattern": "Write", "priority": 2},
    {"pattern": "GetFirmwareVersion", "priority": 3},
    {"pattern": "GetWheelID", "priority": 3},
    {"pattern": "SetDeviceMode", "priority": 3},
    {"pattern": "GetButtonCaps", "priority": 3},
    {"pattern": "GetValueCaps", "priority": 3},
]

def get_strings_with_pattern(pattern):
    """Get all strings matching a pattern"""
    url = f"{BASE_URL}/strings"
    params = {"filter": pattern, "limit": 500}
    response = requests.get(url, params=params)
    if response.status_code == 200:
        data = response.json()
        if isinstance(data, dict):
            return data.get("result", [])
    return []

def find_function_by_string_ref(string_addr):
    """Find functions that reference a string"""
    url = f"{BASE_URL}/xrefs"
    params = {"to_addr": string_addr, "limit": 50}
    response = requests.get(url, params=params)
    if response.status_code == 200:
        data = response.json()
        refs = data.get("result", {}).get("result", {}).get("references", [])
        funcs = set()
        for ref in refs:
            if "from_function" in ref:
                funcs.add(ref["from_function"]["address"])
        return list(funcs)
    return []

def decompile_function(addr):
    """Decompile a function at given address"""
    url = f"{BASE_URL}/functions/{addr}/decompile"
    response = requests.get(url)
    if response.status_code == 200:
        data = response.json()
        return data.get("result", {})
    return None

def get_function_xrefs(addr, direction="to"):
    """Get cross-references for a function"""
    url = f"{BASE_URL}/xrefs"
    params = {"to_addr" if direction == "to" else "from_addr": addr, "limit": 100}
    response = requests.get(url, params=params)
    if response.status_code == 200:
        data = response.json()
        return data.get("result", {}).get("result", {}).get("references", [])
    return []

def analyze_function(func_name, func_addr):
    """Comprehensive analysis of a single function"""
    print(f"Analyzing {func_name} at {func_addr}...")
    
    analysis = {
        "name": func_name,
        "address": func_addr,
        "decompiled_code": None,
        "callers": [],
        "callees": [],
        "analysis_notes": []
    }
    
    # Decompile
    decomp = decompile_function(func_addr)
    if decomp:
        analysis["decompiled_code"] = decomp.get("decompiled_code", "")
    
    # Get callers (who calls this function)
    xrefs_to = get_function_xrefs(func_addr, "to")
    analysis["callers"] = [
        {"from": ref["from_addr"], "function": ref.get("from_function", {}).get("name", "unknown")}
        for ref in xrefs_to
    ]
    
    # Get callees (what this function calls)
    xrefs_from = get_function_xrefs(func_addr, "from")
    analysis["callees"] = [
        {"to": ref["to_addr"], "symbol": ref.get("to_symbol", "unknown")}
        for ref in xrefs_from
    ]
    
    return analysis

def save_analysis(filename, data):
    """Save analysis to markdown file"""
    output_path = OUTPUT_DIR / "analysis" / filename
    output_path.parent.mkdir(parents=True, exist_ok=True)
    
    with open(output_path, 'w') as f:
        f.write(f"# {data['name']} Analysis\\n\\n")
        f.write(f"**Address**: `{data['address']}`\\n\\n")
        
        f.write(f"## Callers ({len(data['callers'])})\\n")
        for caller in data['callers'][:20]:  # Limit to first 20
            f.write(f"- `{caller['from']}` ({caller['function']})\\n")
        
        f.write(f"\\n## Callees ({len(data['callees'])})\\n")
        for callee in data['callees'][:20]:
            f.write(f"- `{callee['to']}` ({callee['symbol']})\\n")
        
        f.write(f"\\n## Decompiled Code\\n")
        f.write(f"```c\\n{data['decompiled_code']}\\n```\\n")
    
    print(f"  Saved to {output_path}")

def main_tmpid_analysis():
    """Main analysis routine for tmpid.dll"""
    print("Starting tmpid.dll comprehensive analysis...")
    print("=" * 60)
    
    results = []
    
    for func_info in TMPID_KEY_FUNCTIONS:
        pattern = func_info["pattern"]
        priority = func_info["priority"]
        
        print(f"\\n[Priority {priority}] Searching for functions matching '{pattern}'...")
        
        # Find strings matching pattern
        strings = get_strings_with_pattern(pattern)
        print(f"  Found {len(strings)} matching strings")
        
        for string_data in strings[:5]:  # Limit to first 5 matches
            string_addr = string_data["address"]
            string_val = string_data["value"]
            
            # Find functions that reference this string
            func_addrs = find_function_by_string_ref(string_addr)
            print(f"  String '{string_val}' referenced by {len(func_addrs)} functions")
            
            for func_addr in func_addrs[:2]:  # Analyze first 2 functions per string
                analysis = analyze_function(f"{pattern}_{func_addr}", func_addr)
                results.append(analysis)
                
                # Save individual analysis
                filename = f"tmpid_{pattern}_{func_addr}.md"
                save_analysis(filename, analysis)
                
                time.sleep(0.5)  # Rate limiting
    
    # Save summary
    summary_path = OUTPUT_DIR / "findings" / "tmpid_analysis_summary.json"
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    with open(summary_path, 'w') as f:
        json.dump(results, f, indent=2)
    
    print(f"\\nAnalysis complete! Analyzed {len(results)} functions")
    print(f"Summary saved to {summary_path}")

if __name__ == "__main__":
    main_tmpid_analysis()
