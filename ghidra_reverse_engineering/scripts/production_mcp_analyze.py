#!/usr/bin/env python3
"""
Production MCP Analysis Script for T500RS tmpid.dll
Uses real MCP API calls through a simple interface
"""

import json
import time
import os
import sys
from pathlib import Path

# Add the parent directory to the path so we can create a simple MCP interface
sys.path.insert(0, str(Path(__file__).parent.parent))

OUTPUT_DIR = Path("/home/caz/Documents/hid-tmff2/ghidra_reverse_engineering")

# Analysis targets for tmpid.dll
ANALYSIS_TARGETS = [
    {"pattern": "SetPeriodic", "priority": 1, "type": "effect_control", "expected": "Force feedback periodic effect setup"},
    {"pattern": "SetConstant", "priority": 1, "type": "effect_control", "expected": "Force feedback constant effect setup"},
    {"pattern": "SetEnvelope", "priority": 1, "type": "effect_control", "expected": "Force feedback envelope configuration"},
    {"pattern": "SetCondition", "priority": 1, "type": "effect_control", "expected": "Force feedback condition effect setup"},
    {"pattern": "SetEffect", "priority": 1, "type": "effect_control", "expected": "General effect configuration"},
    {"pattern": "EffectOperation", "priority": 1, "type": "effect_control", "expected": "Effect lifecycle management"},
    {"pattern": "DeviceControl", "priority": 2, "type": "device_mgmt", "expected": "Device-level control operations"},
    {"pattern": "DeviceGain", "priority": 2, "type": "device_mgmt", "expected": "Overall force feedback gain control"},
    {"pattern": "Start", "priority": 2, "type": "lifecycle", "expected": "Start/initialization routines"},
    {"pattern": "Stop", "priority": 2, "type": "lifecycle", "expected": "Stop/cleanup routines"},
    {"pattern": "Write", "priority": 2, "type": "io_operations", "expected": "HID/USB write operations"},
    {"pattern": "Read", "priority": 2, "type": "io_operations", "expected": "HID/USB read operations"},
    {"pattern": "GetFirmwareVersion", "priority": 3, "type": "device_info", "expected": "Firmware version detection"},
    {"pattern": "GetWheelID", "priority": 3, "type": "device_info", "expected": "Device identification"},
    {"pattern": "SetDeviceMode", "priority": 3, "type": "device_config", "expected": "Device mode configuration"},
    {"pattern": "Initialize", "priority": 2, "type": "lifecycle", "expected": "System initialization"},
    {"pattern": "ProcessReport", "priority": 2, "type": "hid_interface", "expected": "HID report processing"},
]

class ProductionMCPAnalyzer:
    """Production-ready analyzer using MCP APIs"""
    
    def __init__(self):
        self.results = []
        self.total_analyzed = 0
        self.ghidra_available = self.check_ghidra_connection()
        
    def check_ghidra_connection(self):
        """Check if Ghidra MCP connection is available"""
        try:
            # Try to make a simple API call to test connection
            import requests
            response = requests.get("http://localhost:8193/program", timeout=5)
            return response.status_code == 200
        except:
            return False
    
    def mcp_call(self, tool_name, **params):
        """Make MCP API call - simplified interface"""
        try:
            if not self.ghidra_available:
                print(f"    Warning: Ghidra not available, using mock data for {tool_name}")
                return self._get_mock_data(tool_name, params)
            
            # Real MCP call through HTTP for now (would be replaced with proper MCP client)
            import requests
            base_url = "http://localhost:8193"
            
            if tool_name == "data_list_strings":
                response = requests.get(f"{base_url}/strings", params={
                    "filter": params.get("filter"),
                    "limit": params.get("limit", 100),
                    "offset": params.get("offset", 0)
                })
                return response.json() if response.status_code == 200 else None
                
            elif tool_name == "xrefs_list":
                response = requests.get(f"{base_url}/xrefs", params=params)
                return response.json() if response.status_code == 200 else None
                
            elif tool_name == "functions_decompile":
                addr = params.get("address") or params.get("name")
                response = requests.get(f"{base_url}/functions/{addr}/decompile")
                return response.json() if response.status_code == 200 else None
                
            elif tool_name == "functions_get":
                addr = params.get("address") or params.get("name")
                response = requests.get(f"{base_url}/functions/{addr}")
                return response.json() if response.status_code == 200 else None
                
        except Exception as e:
            print(f"    MCP call failed for {tool_name}: {e}")
            return None
    
    def _get_mock_data(self, tool_name, params):
        """Provide mock data when Ghidra is not available"""
        if tool_name == "data_list_strings":
            pattern = params.get("filter", "")
            if "SetPeriodic" in pattern:
                return {"result": [{"address": "180001d30", "value": '"CPidDevice::SetPeriodic"', "length": 24}]}
            elif "SetConstant" in pattern:
                return {"result": [{"address": "180001c10", "value": '"CPidDevice::SetConstant"', "length": 24}]}
            elif "SetEnvelope" in pattern:
                return {"result": [{"address": "180001b00", "value": '"CPidDevice::SetEnvelope"', "length": 24}]}
            else:
                return {"result": []}
                
        elif tool_name == "xrefs_list":
            addr = params.get("to_addr")
            if addr == "180001d30":  # SetPeriodic
                return {"result": {"references": [{"from_addr": "18000cbbc", "from_function": {"address": "18000cbbc", "name": "FUN_18000cbbc"}}]}}
            elif addr == "180001c10":  # SetConstant  
                return {"result": {"references": [{"from_addr": "18000c890", "from_function": {"address": "18000c890", "name": "FUN_18000c890"}}]}}
            elif addr == "180001b00":  # SetEnvelope
                return {"result": {"references": [{"from_addr": "18000c550", "from_function": {"address": "18000c550", "name": "FUN_18000c550"}}]}}
            else:
                return {"result": {"references": []}}
                
        elif tool_name == "functions_decompile":
            addr = params.get("address", "unknown")
            mock_code = f"""// Mock decompiled function at {addr}
void function_{addr[-4:]}(void) {{
    // Implementation would be here
    HidP_SetUsageValue(report, usage, value);
    return;
}}"""
            return {"result": {"decompiled_code": mock_code}}
        
        return None
    
    def find_strings_for_pattern(self, pattern):
        """Find all strings containing the pattern"""
        print(f"    Searching for strings with pattern '{pattern}'...")
        
        result = self.mcp_call("data_list_strings", filter=pattern, limit=100)
        if result and "result" in result:
            strings = result["result"]
            print(f"    Found {len(strings)} matching strings")
            return strings
        
        print(f"    No strings found for pattern '{pattern}'")
        return []
    
    def find_functions_using_string(self, string_addr):
        """Find functions that reference a specific string"""
        print(f"    Finding functions referencing string at {string_addr}")
        
        result = self.mcp_call("xrefs_list", to_addr=string_addr, limit=50)
        if result and "result" in result and "references" in result["result"]:
            refs = result["result"]["references"]
            functions = []
            for ref in refs:
                if "from_function" in ref and ref["from_function"]:
                    functions.append({
                        "address": ref["from_function"]["address"],
                        "name": ref["from_function"]["name"],
                        "ref_addr": ref["from_addr"]
                    })
            print(f"    Found {len(functions)} referencing functions")
            return functions
        
        print(f"    No functions found referencing string at {string_addr}")
        return []
    
    def analyze_function_detailed(self, func_info, pattern, expected_type, description):
        """Perform detailed analysis of a single function"""
        addr = func_info["address"]
        name = func_info["name"]
        
        print(f"      Analyzing function {name} at {addr}")
        
        analysis = {
            "address": addr,
            "current_name": name,
            "pattern": pattern,
            "type": expected_type,
            "description": description,
            "suggested_name": f"CPidDevice_{pattern}_{addr[-4:]}",
            "decompiled_code": "",
            "size": 0,
            "complexity": 0,
            "hid_calls": [],
            "error_messages": [],
            "constants": [],
            "analysis_notes": [],
            "confidence": "medium"
        }
        
        # Get function details
        func_details = self.mcp_call("functions_get", address=addr)
        if func_details and "result" in func_details:
            details = func_details["result"]
            analysis["size"] = details.get("size", 0)
        
        # Decompile function
        decomp_result = self.mcp_call("functions_decompile", address=addr)
        if decomp_result and "result" in decomp_result:
            code = decomp_result["result"].get("decompiled_code", "")
            analysis["decompiled_code"] = code
            
            # Analyze the code
            analysis.update(self._analyze_decompiled_code(code, pattern))
        
        # Determine confidence based on pattern match and code analysis
        if pattern.lower() in analysis["decompiled_code"].lower():
            analysis["confidence"] = "high"
        elif len(analysis["hid_calls"]) > 0:
            analysis["confidence"] = "medium"
        else:
            analysis["confidence"] = "low"
        
        return analysis
    
    def _analyze_decompiled_code(self, code, pattern):
        """Analyze decompiled code for interesting patterns"""
        analysis = {
            "complexity": 0,
            "hid_calls": [],
            "error_messages": [],
            "constants": [],
            "analysis_notes": []
        }
        
        if not code:
            return analysis
        
        lines = code.split('\n')
        analysis["complexity"] = len(lines)
        
        # Look for HID function calls
        hid_functions = [
            "HidP_SetUsageValue", "HidP_GetUsageValue", "HidP_SetUsages", 
            "HidD_SetFeature", "HidD_GetFeature", "HidD_GetInputReport",
            "WriteFile", "ReadFile", "DeviceIoControl"
        ]
        
        for func in hid_functions:
            if func in code:
                analysis["hid_calls"].append(func)
                analysis["analysis_notes"].append(f"Uses {func}")
        
        # Look for error messages
        for line in lines:
            if "ERROR:" in line or '"ERROR' in line:
                analysis["error_messages"].append(line.strip())
                
        # Look for hex constants
        import re
        hex_constants = re.findall(r'0x[0-9a-fA-F]+', code)
        analysis["constants"] = list(set(hex_constants))[:10]  # Limit to 10
        
        # Pattern-specific analysis
        if pattern == "SetPeriodic" and ("periodic" in code.lower() or "magnitude" in code.lower()):
            analysis["analysis_notes"].append("Likely implements periodic effect control")
        elif pattern == "SetConstant" and "constant" in code.lower():
            analysis["analysis_notes"].append("Likely implements constant force effect")
        elif pattern in ["Write", "Read"] and len(analysis["hid_calls"]) > 0:
            analysis["analysis_notes"].append("Implements HID I/O operations")
        
        return analysis
    
    def save_function_analysis(self, analysis):
        """Save individual function analysis to file"""
        pattern = analysis["pattern"]
        addr = analysis["address"]
        filename = f"function_{pattern}_{addr}.md"
        
        output_path = OUTPUT_DIR / "analysis" / filename
        output_path.parent.mkdir(parents=True, exist_ok=True)
        
        with open(output_path, 'w') as f:
            f.write(f"# {analysis['suggested_name']} Function Analysis\n\n")
            f.write(f"**Address**: `{addr}`\n")
            f.write(f"**Current Name**: `{analysis['current_name']}`\n")
            f.write(f"**Pattern**: {pattern}\n")
            f.write(f"**Type**: {analysis['type']}\n")
            f.write(f"**Description**: {analysis['description']}\n")
            f.write(f"**Confidence**: {analysis['confidence'].upper()}\n")
            f.write(f"**Size**: {analysis['size']} bytes\n")
            f.write(f"**Complexity**: {analysis['complexity']} lines\n\n")
            
            if analysis['analysis_notes']:
                f.write("## Analysis Notes\n")
                for note in analysis['analysis_notes']:
                    f.write(f"- {note}\n")
                f.write("\n")
            
            if analysis['hid_calls']:
                f.write("## HID Function Calls\n")
                for call in analysis['hid_calls']:
                    f.write(f"- `{call}`\n")
                f.write("\n")
            
            if analysis['constants']:
                f.write("## Constants Found\n")
                for const in analysis['constants'][:10]:
                    f.write(f"- `{const}`\n")
                f.write("\n")
            
            if analysis['error_messages']:
                f.write("## Error Messages\n")
                for msg in analysis['error_messages'][:5]:
                    f.write(f"- {msg}\n")
                f.write("\n")
            
            f.write("## Decompiled Code\n")
            f.write(f"```c\n{analysis['decompiled_code']}\n```\n")
        
        print(f"        Saved analysis to {output_path}")
        return output_path
    
    def create_comprehensive_summary(self):
        """Create comprehensive analysis summary"""
        summary_path = OUTPUT_DIR / "findings" / "production_analysis_summary.md"
        summary_path.parent.mkdir(parents=True, exist_ok=True)
        
        with open(summary_path, 'w') as f:
            f.write("# T500RS tmpid.dll Production Analysis Summary\n\n")
            f.write(f"**Analysis Date**: {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
            f.write(f"**Total Functions Analyzed**: {len(self.results)}\n")
            f.write(f"**Ghidra Connection**: {'✓ Active' if self.ghidra_available else '✗ Mock Data'}\n\n")
            
            # Group by confidence
            high_confidence = [r for r in self.results if r['confidence'] == 'high']
            medium_confidence = [r for r in self.results if r['confidence'] == 'medium']
            low_confidence = [r for r in self.results if r['confidence'] == 'low']
            
            f.write("## High Confidence Functions\n")
            f.write("These functions have strong pattern matches and are likely correct identifications.\n\n")
            for func in high_confidence:
                f.write(f"### {func['suggested_name']}\n")
                f.write(f"- **Address**: `{func['address']}`\n")
                f.write(f"- **Type**: {func['type']}\n")
                f.write(f"- **Description**: {func['description']}\n")
                if func['hid_calls']:
                    f.write(f"- **HID Calls**: {', '.join(func['hid_calls'])}\n")
                f.write("\n")
            
            # Group by type
            f.write("## Functions by Type\n\n")
            by_type = {}
            for result in self.results:
                func_type = result['type']
                if func_type not in by_type:
                    by_type[func_type] = []
                by_type[func_type].append(result)
            
            for func_type, functions in by_type.items():
                f.write(f"### {func_type.replace('_', ' ').title()} ({len(functions)} functions)\n")
                for func in functions:
                    f.write(f"- **{func['suggested_name']}** (`{func['address']}`) - {func['confidence']} confidence\n")
                f.write("\n")
            
            # Key findings
            f.write("## Key Findings\n\n")
            total_hid = sum(len(r['hid_calls']) for r in self.results)
            f.write(f"- **Total HID API calls found**: {total_hid}\n")
            f.write(f"- **High confidence functions**: {len(high_confidence)}\n")
            f.write(f"- **Effect control functions**: {len([r for r in self.results if r['type'] == 'effect_control'])}\n")
            f.write(f"- **I/O operation functions**: {len([r for r in self.results if r['type'] == 'io_operations'])}\n")
        
        # Save JSON data
        json_path = OUTPUT_DIR / "findings" / "production_analysis_data.json"
        with open(json_path, 'w') as f:
            json.dump(self.results, f, indent=2, default=str)
        
        print(f"\nSummary report: {summary_path}")
        print(f"JSON data: {json_path}")
        
        return summary_path
    
    def run_production_analysis(self):
        """Run the complete production analysis"""
        print("T500RS tmpid.dll Production Analysis")
        print("=====================================")
        print(f"Ghidra connection: {'✓ Available' if self.ghidra_available else '✗ Using mock data'}")
        print("")
        
        for target in ANALYSIS_TARGETS:
            pattern = target["pattern"]
            priority = target["priority"]
            expected_type = target["type"]
            description = target["expected"]
            
            print(f"[Priority {priority}] Analyzing '{pattern}' ({expected_type})")
            
            # Find strings matching the pattern
            strings = self.find_strings_for_pattern(pattern)
            if not strings:
                print(f"  No strings found for '{pattern}' - skipping")
                continue
            
            # Process each string
            for string_data in strings[:5]:  # Limit to 5 strings per pattern
                string_addr = string_data["address"]
                string_value = string_data["value"]
                
                print(f"  Processing: {string_value}")
                
                # Find functions using this string
                functions = self.find_functions_using_string(string_addr)
                
                for func_info in functions[:3]:  # Limit to 3 functions per string
                    # Analyze the function
                    analysis = self.analyze_function_detailed(
                        func_info, pattern, expected_type, description
                    )
                    analysis["priority"] = priority
                    
                    # Save results
                    self.results.append(analysis)
                    self.save_function_analysis(analysis)
                    self.total_analyzed += 1
                    
                    # Brief pause
                    time.sleep(0.1)
        
        print(f"\n{'='*60}")
        print(f"Production Analysis Complete!")
        print(f"Total functions analyzed: {self.total_analyzed}")
        
        if self.total_analyzed > 0:
            self.create_comprehensive_summary()
            print(f"\nResults available in:")
            print(f"- Individual analyses: {OUTPUT_DIR / 'analysis'}")
            print(f"- Summary reports: {OUTPUT_DIR / 'findings'}")
        
        return self.results

def main():
    """Main entry point"""
    try:
        analyzer = ProductionMCPAnalyzer()
        results = analyzer.run_production_analysis()
        
        print(f"\n✅ Analysis completed successfully!")
        print(f"   {len(results)} functions analyzed")
        print(f"   Results saved to analysis directories")
        
    except KeyboardInterrupt:
        print("\n⚠️  Analysis interrupted by user")
        
    except Exception as e:
        print(f"\n❌ Analysis failed: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()