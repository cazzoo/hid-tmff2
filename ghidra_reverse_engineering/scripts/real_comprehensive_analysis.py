#!/usr/bin/env python3
"""
Real Comprehensive T500RS Driver Analysis
Uses actual Ghidra MCP APIs on port 8192 to analyze all loaded programs with REAL decompiled code
"""

import json
import time
import requests
from pathlib import Path
from typing import Dict, List, Optional

OUTPUT_DIR = Path("/home/caz/Documents/hid-tmff2/ghidra_reverse_engineering")
BASE_URL = "http://localhost:8192"  # Correct MCP port

# All T500RS driver programs we expect to find in Ghidra project
EXPECTED_PROGRAMS = [
    # Core drivers
    "tmpid.dll",
    "tmeffcpl.dll", 
    "tmeffcpl64.dll",
    
    # Kernel drivers
    "tmHidUsb.sys",
    "GuiHidUsbDevLowerFFB.sys", 
    "tmhidusb.sys",
    
    # Installation utilities
    "tmInstall.exe",
    "tmInstallHelper.exe", 
    "TMRegCln.exe",
    "tmJoycpl.exe",
    
    # SDK libraries
    "tm_api_lib_x64.dll",
    "tm_api_lib_x86.dll",
    
    # Additional drivers
    "tmResetMin.sys",
    "tmwbulk.sys",
    
    # Firmware update tools
    "TmRimUpdate64.dll",
    "TmRimUpdate.dll", 
    "GuiSTDFUDevUpdate64.dll",
    "GuiSTDFUDevUpdate.dll",
    
    # System libraries
    "hid.dll",
    "dinput.dll"
]

# Analysis patterns for each program type
ANALYSIS_PATTERNS = {
    "tmpid.dll": {
        "patterns": ["SetPeriodic", "SetConstant", "SetEnvelope", "SetCondition", "SetEffect", 
                    "EffectOperation", "DeviceControl", "DeviceGain", "Start", "Stop", "Write", "Read"],
        "priority": 1,
        "type": "core_userspace_driver"
    },
    "tmeffcpl.dll": {
        "patterns": ["Configure", "Panel", "Settings", "Registry", "Profile", "Calibrate", "Test", "Property"],
        "priority": 1, 
        "type": "control_panel"
    },
    "tmeffcpl64.dll": {
        "patterns": ["Configure", "Panel", "Settings", "Registry", "Profile", "Calibrate"],
        "priority": 1,
        "type": "control_panel_64"
    },
    "tmHidUsb.sys": {
        "patterns": ["DriverEntry", "AddDevice", "PnP", "Power", "IRP", "HID", "USB", "IoControl"],
        "priority": 1,
        "type": "kernel_hid_driver"
    },
    "GuiHidUsbDevLowerFFB.sys": {
        "patterns": ["FFB", "Force", "Feedback", "Effect", "DriverEntry", "IRP", "IoControl", "HID"],
        "priority": 1,
        "type": "kernel_ffb_driver" 
    },
    "tmInstall.exe": {
        "patterns": ["Install", "Setup", "Registry", "Service", "Driver", "Version", "Hardware"],
        "priority": 2,
        "type": "installer"
    },
    "tm_api_lib_x64.dll": {
        "patterns": ["API", "Initialize", "GetDevice", "SendCommand", "GetState", "SetEffect", "Connect"],
        "priority": 2,
        "type": "sdk_api_64"
    },
    "tm_api_lib_x86.dll": {
        "patterns": ["API", "Initialize", "GetDevice", "SendCommand", "GetState", "SetEffect"],
        "priority": 2, 
        "type": "sdk_api_32"
    }
}

class RealT500RSAnalyzer:
    """Real analyzer using actual Ghidra MCP APIs"""
    
    def __init__(self):
        self.base_url = BASE_URL
        self.results = {}
        self.current_program = None
        
    def check_ghidra_connection(self):
        """Check if Ghidra MCP is available"""
        try:
            response = requests.get(f"{self.base_url}/status", timeout=5)
            return response.status_code == 200
        except:
            return False
    
    def list_available_programs(self):
        """List all programs available in the Ghidra project"""
        try:
            # This might not be a direct API, so let's try different approaches
            response = requests.get(f"{self.base_url}/programs", timeout=10)
            if response.status_code == 200:
                return response.json()
        except:
            pass
        
        # Alternative: try to discover programs by attempting to open known ones
        print("🔍 Discovering available programs in Ghidra project...")
        available_programs = []
        
        for program_name in EXPECTED_PROGRAMS:
            if self.try_open_program(program_name):
                available_programs.append(program_name)
                print(f"✅ Found: {program_name}")
            else:
                print(f"⚠️  Not found: {program_name}")
        
        return available_programs
    
    def try_open_program(self, program_name):
        """Try to open a specific program"""
        try:
            # Try different possible program identifiers
            possible_names = [
                program_name,
                program_name.replace(".dll", ""),
                program_name.replace(".exe", ""), 
                program_name.replace(".sys", ""),
                f"T500RS:/{program_name}",
                program_name.split("/")[-1] if "/" in program_name else program_name
            ]
            
            for name in possible_names:
                try:
                    response = requests.post(f"{self.base_url}/program/open", 
                                           json={"program": name}, timeout=30)
                    if response.status_code == 200:
                        self.current_program = program_name
                        return True
                except:
                    continue
                    
        except Exception as e:
            pass
        
        return False
    
    def get_current_program_info(self):
        """Get information about currently loaded program"""
        try:
            response = requests.get(f"{self.base_url}/program", timeout=10)
            if response.status_code == 200:
                return response.json()
        except:
            pass
        return None
    
    def get_strings_with_pattern(self, pattern):
        """Get strings matching a pattern from current program"""
        try:
            response = requests.get(f"{self.base_url}/strings", 
                                  params={"filter": pattern, "limit": 100}, 
                                  timeout=15)
            if response.status_code == 200:
                data = response.json()
                return data.get("result", [])
        except Exception as e:
            print(f"    Error getting strings for '{pattern}': {e}")
        return []
    
    def get_xrefs_for_address(self, address):
        """Get cross-references for an address"""
        try:
            response = requests.get(f"{self.base_url}/xrefs",
                                  params={"to_addr": address, "limit": 50},
                                  timeout=15)
            if response.status_code == 200:
                data = response.json()
                return data.get("result", {}).get("references", [])
        except Exception as e:
            print(f"    Error getting xrefs for {address}: {e}")
        return []
    
    def decompile_function(self, address):
        """Decompile function at address"""
        try:
            response = requests.get(f"{self.base_url}/functions/{address}/decompile", timeout=30)
            if response.status_code == 200:
                data = response.json()
                return data.get("result", {})
        except Exception as e:
            print(f"    Error decompiling function at {address}: {e}")
        return None
    
    def get_function_info(self, address):
        """Get function information"""
        try:
            response = requests.get(f"{self.base_url}/functions/{address}", timeout=15)
            if response.status_code == 200:
                data = response.json()
                return data.get("result", {})
        except Exception as e:
            print(f"    Error getting function info for {address}: {e}")
        return None
    
    def analyze_program_comprehensive(self, program_name):
        """Comprehensively analyze a single program"""
        print(f"\n{'='*80}")
        print(f"🔍 ANALYZING PROGRAM: {program_name}")
        print(f"{'='*80}")
        
        # Try to open the program
        if not self.try_open_program(program_name):
            print(f"❌ Could not open program: {program_name}")
            return None
        
        print(f"✅ Successfully opened: {program_name}")
        
        # Get program information
        program_info = self.get_current_program_info()
        if program_info:
            print(f"📊 Program Info: {program_info}")
        
        # Get analysis patterns for this program
        patterns = ANALYSIS_PATTERNS.get(program_name, {
            "patterns": ["main", "entry", "init", "start", "process"],
            "priority": 3,
            "type": "unknown"
        })
        
        analysis_result = {
            "program_name": program_name,
            "type": patterns["type"],
            "priority": patterns["priority"],
            "program_info": program_info,
            "functions_analyzed": {},
            "strings_found": {},
            "total_functions_found": 0,
            "decompiled_functions": {}
        }
        
        total_functions = 0
        
        # Analyze each pattern
        for pattern in patterns["patterns"]:
            print(f"  🔍 Searching for '{pattern}' functionality...")
            
            # Find strings matching pattern
            strings = self.get_strings_with_pattern(pattern)
            analysis_result["strings_found"][pattern] = len(strings)
            print(f"    Found {len(strings)} strings matching '{pattern}'")
            
            # For each string, find referencing functions
            pattern_functions = []
            
            for string_data in strings[:5]:  # Analyze top 5 strings per pattern
                string_addr = string_data.get("address")
                string_value = string_data.get("value", "")
                
                if string_addr:
                    print(f"    📝 String: {string_value[:50]}... at {string_addr}")
                    
                    # Get cross-references
                    xrefs = self.get_xrefs_for_address(string_addr)
                    print(f"      Found {len(xrefs)} cross-references")
                    
                    # Analyze referencing functions
                    for xref in xrefs[:3]:  # Top 3 xrefs per string
                        if "from_function" in xref and xref["from_function"]:
                            func_addr = xref["from_function"]["address"]
                            func_name = xref["from_function"]["name"]
                            
                            print(f"      🔧 Analyzing function: {func_name} at {func_addr}")
                            
                            # Get function details
                            func_info = self.get_function_info(func_addr)
                            
                            # Decompile function
                            decompiled = self.decompile_function(func_addr)
                            
                            function_analysis = {
                                "address": func_addr,
                                "name": func_name,
                                "pattern": pattern,
                                "string_reference": string_value,
                                "function_info": func_info,
                                "decompiled_code": decompiled.get("decompiled_code", "") if decompiled else "",
                                "size": func_info.get("size", 0) if func_info else 0
                            }
                            
                            pattern_functions.append(function_analysis)
                            
                            # Store decompiled code if available
                            if decompiled and decompiled.get("decompiled_code"):
                                analysis_result["decompiled_functions"][func_addr] = {
                                    "name": func_name,
                                    "code": decompiled["decompiled_code"],
                                    "pattern": pattern
                                }
                                print(f"        ✅ Decompiled {len(decompiled['decompiled_code'])} chars of code")
                            
                            total_functions += 1
                            
                            # Brief pause to avoid overwhelming
                            time.sleep(0.2)
            
            analysis_result["functions_analyzed"][pattern] = pattern_functions
            print(f"    ✅ Completed analysis of '{pattern}': {len(pattern_functions)} functions")
        
        analysis_result["total_functions_found"] = total_functions
        
        print(f"🎉 Program analysis complete: {total_functions} functions analyzed")
        return analysis_result
    
    def save_program_analysis(self, analysis):
        """Save analysis results for a program"""
        program_name = analysis["program_name"]
        safe_name = program_name.replace("/", "_").replace("\\", "_")
        
        # Save detailed analysis
        output_path = OUTPUT_DIR / "analysis" / f"REAL_{safe_name}_analysis.md"
        output_path.parent.mkdir(parents=True, exist_ok=True)
        
        with open(output_path, 'w') as f:
            f.write(f"# Real Analysis: {program_name}\n\n")
            f.write(f"**Type**: {analysis['type']}\n")
            f.write(f"**Priority**: {analysis['priority']}\n")
            f.write(f"**Total Functions Analyzed**: {analysis['total_functions_found']}\n")
            f.write(f"**Analysis Date**: {time.strftime('%Y-%m-%d %H:%M:%S')}\n\n")
            
            if analysis.get("program_info"):
                f.write("## Program Information\n")
                f.write(f"```json\n{json.dumps(analysis['program_info'], indent=2)}\n```\n\n")
            
            # Function analysis by pattern
            f.write("## Functions by Pattern\n\n")
            for pattern, functions in analysis["functions_analyzed"].items():
                if functions:
                    f.write(f"### {pattern.title()} Functions ({len(functions)} found)\n\n")
                    for func in functions:
                        f.write(f"#### {func['name']} - `{func['address']}`\n")
                        f.write(f"- **Pattern**: {func['pattern']}\n")
                        f.write(f"- **Size**: {func['size']} bytes\n")
                        f.write(f"- **String Reference**: {func['string_reference'][:100]}...\n\n")
                        
                        if func.get('decompiled_code'):
                            f.write("**Decompiled Code:**\n")
                            f.write(f"```c\n{func['decompiled_code']}\n```\n\n")
            
            # All decompiled functions section
            if analysis["decompiled_functions"]:
                f.write("## All Decompiled Functions\n\n")
                for addr, func_data in analysis["decompiled_functions"].items():
                    f.write(f"### {func_data['name']} - `{addr}`\n")
                    f.write(f"**Pattern**: {func_data['pattern']}\n\n")
                    f.write(f"```c\n{func_data['code']}\n```\n\n")
        
        print(f"📁 Saved analysis: {output_path}")
        
        # Save JSON data
        json_path = OUTPUT_DIR / "analysis" / f"REAL_{safe_name}_analysis.json"
        with open(json_path, 'w') as f:
            json.dump(analysis, f, indent=2, default=str)
        
        return output_path
    
    def run_complete_real_analysis(self):
        """Run complete analysis of all available programs"""
        print("🚀 REAL T500RS Driver Analysis with Actual Ghidra Data")
        print("="*80)
        
        # Check connection
        if not self.check_ghidra_connection():
            print("❌ Cannot connect to Ghidra MCP server on port 8192")
            print("   Make sure Ghidra is running with the MCP plugin enabled")
            return
        
        print("✅ Connected to Ghidra MCP server on port 8192")
        
        # Discover available programs
        available_programs = self.list_available_programs()
        
        if not available_programs:
            print("❌ No programs found in Ghidra project")
            print("   Make sure T500RS driver files are loaded in Ghidra")
            return
        
        print(f"📁 Found {len(available_programs)} programs to analyze")
        
        # Analyze each program
        for i, program_name in enumerate(available_programs, 1):
            try:
                print(f"\n{'='*60}")
                print(f"Program {i}/{len(available_programs)}: {program_name}")
                print(f"{'='*60}")
                
                analysis = self.analyze_program_comprehensive(program_name)
                
                if analysis:
                    self.results[program_name] = analysis
                    self.save_program_analysis(analysis)
                    
                    print(f"✅ Completed analysis of {program_name}")
                    print(f"   📊 {analysis['total_functions_found']} functions analyzed")
                    print(f"   📝 {len(analysis['decompiled_functions'])} functions decompiled")
                else:
                    print(f"❌ Failed to analyze {program_name}")
                
                # Brief pause between programs
                time.sleep(1)
                
            except Exception as e:
                print(f"❌ Error analyzing {program_name}: {e}")
                continue
        
        # Create master summary
        self.create_master_summary()
        
        print(f"\n🎉 REAL ANALYSIS COMPLETE!")
        print(f"   📈 {len(self.results)} programs analyzed")
        print(f"   📁 Results saved to analysis/ directory")
        
        return self.results
    
    def create_master_summary(self):
        """Create master summary of all analyses"""
        summary_path = OUTPUT_DIR / "findings" / "REAL_COMPLETE_T500RS_ANALYSIS.md"
        summary_path.parent.mkdir(parents=True, exist_ok=True)
        
        with open(summary_path, 'w') as f:
            f.write("# REAL Complete T500RS Driver Analysis\n\n")
            f.write(f"**Analysis Date**: {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
            f.write(f"**Programs Analyzed**: {len(self.results)}\n")
            f.write(f"**Data Source**: Real Ghidra MCP APIs (port 8192)\n\n")
            
            total_functions = sum(r['total_functions_found'] for r in self.results.values())
            total_decompiled = sum(len(r['decompiled_functions']) for r in self.results.values())
            
            f.write("## Summary Statistics\n\n")
            f.write(f"- **Total Functions Analyzed**: {total_functions}\n")
            f.write(f"- **Total Functions Decompiled**: {total_decompiled}\n")
            f.write(f"- **Programs Successfully Analyzed**: {len(self.results)}\n\n")
            
            f.write("## Programs Analyzed\n\n")
            for program_name, analysis in self.results.items():
                f.write(f"### {program_name}\n")
                f.write(f"- **Type**: {analysis['type']}\n")
                f.write(f"- **Priority**: {analysis['priority']}\n")
                f.write(f"- **Functions Found**: {analysis['total_functions_found']}\n")
                f.write(f"- **Functions Decompiled**: {len(analysis['decompiled_functions'])}\n")
                
                if analysis['decompiled_functions']:
                    f.write("- **Key Decompiled Functions**:\n")
                    for addr, func_data in list(analysis['decompiled_functions'].items())[:3]:
                        f.write(f"  - `{func_data['name']}` at `{addr}` ({func_data['pattern']})\n")
                f.write("\n")
        
        print(f"📊 Master summary saved: {summary_path}")

def main():
    """Main entry point"""
    try:
        analyzer = RealT500RSAnalyzer()
        results = analyzer.run_complete_real_analysis()
        
        if results:
            print(f"\n✅ SUCCESS! Real analysis completed with actual decompiled code!")
        else:
            print(f"\n❌ Analysis failed - check Ghidra connection and loaded programs")
            
    except KeyboardInterrupt:
        print("\n⚠️ Analysis interrupted by user")
    except Exception as e:
        print(f"\n❌ Analysis error: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()