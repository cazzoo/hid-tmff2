#!/usr/bin/env python3
"""
Simple script to systematically analyze each T500RS program loaded in Ghidra
Opens each program and gets real decompiled code
"""

import requests
import json
import time
from pathlib import Path

BASE_URL = "http://localhost:8192"
OUTPUT_DIR = Path("/home/caz/Documents/hid-tmff2/ghidra_reverse_engineering")

# Programs to try opening (adjust these names based on what's actually in your project)
PROGRAMS_TO_TRY = [
    "tmpid.dll",
    "tmeffcpl.dll", 
    "tmHidUsb.sys",
    "GuiHidUsbDevLowerFFB.sys",
    "tmInstall.exe",
    "tm_api_lib_x64.dll",
    "tm_api_lib_x86.dll",
    "hid.dll",
    "dinput.dll"
]

def try_open_program(program_name):
    """Try to open a program in Ghidra"""
    print(f"🔄 Attempting to open: {program_name}")
    
    # Try different program name formats
    name_variants = [
        program_name,
        f"T500RS:/{program_name}",
        program_name.split('.')[0],  # Without extension
        f"T500RS/{program_name}",
    ]
    
    for name in name_variants:
        try:
            response = requests.post(f"{BASE_URL}/program/open", 
                                   json={"program": name}, 
                                   timeout=30)
            print(f"  Trying name: {name} -> Status: {response.status_code}")
            
            if response.status_code == 200:
                print(f"✅ Successfully opened: {program_name} (as {name})")
                return True
                
        except Exception as e:
            print(f"  Error with {name}: {e}")
            continue
    
    print(f"❌ Could not open: {program_name}")
    return False

def get_program_info():
    """Get current program information"""
    try:
        response = requests.get(f"{BASE_URL}/program", timeout=10)
        if response.status_code == 200:
            return response.json()
    except:
        pass
    return None

def get_functions_list():
    """Get list of functions in current program"""
    try:
        response = requests.get(f"{BASE_URL}/functions", 
                              params={"limit": 50}, timeout=15)
        if response.status_code == 200:
            data = response.json()
            return data.get("result", [])
    except Exception as e:
        print(f"Error getting functions: {e}")
    return []

def decompile_function(address):
    """Decompile a specific function"""
    try:
        response = requests.get(f"{BASE_URL}/functions/{address}/decompile", timeout=30)
        if response.status_code == 200:
            data = response.json()
            result = data.get("result", {})
            return result.get("decompiled_code", "")
    except Exception as e:
        print(f"Error decompiling {address}: {e}")
    return None

def analyze_current_program():
    """Analyze the currently open program"""
    print("📊 Analyzing current program...")
    
    # Get program info
    program_info = get_program_info()
    if not program_info:
        print("❌ No program info available")
        return None
    
    program_name = program_info.get("program_name", "unknown")
    print(f"📁 Program: {program_name}")
    
    # Get functions
    functions = get_functions_list()
    print(f"🔧 Found {len(functions)} functions")
    
    analysis = {
        "program_name": program_name,
        "program_info": program_info,
        "functions_count": len(functions),
        "decompiled_functions": {}
    }
    
    # Decompile top 10 functions
    for i, func in enumerate(functions[:10]):
        func_addr = func.get("address")
        func_name = func.get("name", f"func_{func_addr}")
        
        print(f"  🔍 Decompiling {i+1}/10: {func_name} at {func_addr}")
        
        code = decompile_function(func_addr)
        if code:
            analysis["decompiled_functions"][func_addr] = {
                "name": func_name,
                "code": code,
                "size": len(code)
            }
            print(f"    ✅ Decompiled {len(code)} characters")
        else:
            print(f"    ❌ Failed to decompile")
        
        time.sleep(0.5)  # Be gentle with the API
    
    return analysis

def save_analysis(analysis):
    """Save analysis to file"""
    if not analysis:
        return
        
    program_name = analysis["program_name"]
    safe_name = program_name.replace("/", "_").replace(":", "_")
    
    # Save markdown report
    output_path = OUTPUT_DIR / "analysis" / f"REAL_{safe_name}_detailed.md"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    
    with open(output_path, 'w') as f:
        f.write(f"# Real Analysis: {program_name}\n\n")
        f.write(f"**Analysis Date**: {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"**Total Functions**: {analysis['functions_count']}\n")
        f.write(f"**Decompiled Functions**: {len(analysis['decompiled_functions'])}\n\n")
        
        f.write("## Program Information\n")
        f.write(f"```json\n{json.dumps(analysis['program_info'], indent=2)}\n```\n\n")
        
        f.write("## Decompiled Functions\n\n")
        for addr, func_data in analysis["decompiled_functions"].items():
            f.write(f"### {func_data['name']} - Address: `{addr}`\n\n")
            f.write(f"**Size**: {func_data['size']} characters\n\n")
            f.write("```c\n")
            f.write(func_data['code'])
            f.write("\n```\n\n")
    
    print(f"💾 Saved analysis: {output_path}")
    
    # Save JSON
    json_path = OUTPUT_DIR / "analysis" / f"REAL_{safe_name}_detailed.json"
    with open(json_path, 'w') as f:
        json.dump(analysis, f, indent=2, default=str)

def main():
    """Main analysis loop"""
    print("🚀 T500RS Real Program Analysis")
    print("="*50)
    
    # Test connection
    try:
        response = requests.get(f"{BASE_URL}/program", timeout=5)
        print("✅ Connected to Ghidra MCP server")
    except:
        print("❌ Cannot connect to Ghidra MCP server")
        return
    
    all_analyses = {}
    
    # Try each program
    for program_name in PROGRAMS_TO_TRY:
        print(f"\n{'='*60}")
        print(f"PROCESSING: {program_name}")
        print(f"{'='*60}")
        
        # Try to open program
        if try_open_program(program_name):
            # Analyze it
            analysis = analyze_current_program()
            if analysis:
                all_analyses[program_name] = analysis
                save_analysis(analysis)
                print(f"✅ Successfully analyzed {program_name}")
            else:
                print(f"❌ Failed to analyze {program_name}")
        else:
            print(f"⚠️ Skipping {program_name} - could not open")
        
        time.sleep(2)  # Pause between programs
    
    # Summary
    print(f"\n🎉 ANALYSIS COMPLETE!")
    print(f"Successfully analyzed {len(all_analyses)} programs:")
    for name, analysis in all_analyses.items():
        func_count = len(analysis["decompiled_functions"])
        print(f"  - {name}: {func_count} functions decompiled")
    
    return all_analyses

if __name__ == "__main__":
    main()