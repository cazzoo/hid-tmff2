#!/usr/bin/env python3
"""
Enhanced Automated Ghidra MCP Analysis Script
Systematically analyzes T500RS driver components using Ghidra MCP APIs directly
"""

import json
import time
from pathlib import Path
from typing import Dict, List, Optional, Set
import subprocess
import sys

OUTPUT_DIR = Path("/home/caz/Documents/hid-tmff2/ghidra_reverse_engineering")

# Key functions to analyze in tmpid.dll
TMPID_KEY_FUNCTIONS = [
    {"pattern": "SetPeriodic", "priority": 1, "expected_type": "effect_control"},
    {"pattern": "SetConstant", "priority": 1, "expected_type": "effect_control"},
    {"pattern": "SetEnvelope", "priority": 1, "expected_type": "effect_control"},
    {"pattern": "SetCondition", "priority": 1, "expected_type": "effect_control"},
    {"pattern": "SetEffect", "priority": 1, "expected_type": "effect_control"},
    {"pattern": "EffectOperation", "priority": 1, "expected_type": "effect_control"},
    {"pattern": "DeviceControl", "priority": 2, "expected_type": "device_mgmt"},
    {"pattern": "DeviceGain", "priority": 2, "expected_type": "device_mgmt"},
    {"pattern": "Start", "priority": 2, "expected_type": "lifecycle"},
    {"pattern": "Stop", "priority": 2, "expected_type": "lifecycle"},
    {"pattern": "Write", "priority": 2, "expected_type": "io_operations"},
    {"pattern": "Read", "priority": 2, "expected_type": "io_operations"},
    {"pattern": "GetFirmwareVersion", "priority": 3, "expected_type": "device_info"},
    {"pattern": "GetWheelID", "priority": 3, "expected_type": "device_info"},
    {"pattern": "SetDeviceMode", "priority": 3, "expected_type": "device_config"},
    {"pattern": "GetButtonCaps", "priority": 3, "expected_type": "hid_interface"},
    {"pattern": "GetValueCaps", "priority": 3, "expected_type": "hid_interface"},
    {"pattern": "Initialize", "priority": 2, "expected_type": "lifecycle"},
    {"pattern": "Cleanup", "priority": 2, "expected_type": "lifecycle"},
    {"pattern": "ProcessReport", "priority": 2, "expected_type": "hid_interface"},
]

class GhidraMCPAnalyzer:
    def __init__(self):
        self.analysis_results = []
        self.discovered_functions = {}
        self.string_to_functions = {}
        self.function_call_graph = {}
        
    def call_mcp_tool(self, tool_name: str, params: Dict) -> Optional[Dict]:
        """Call MCP tool using subprocess to avoid import issues"""
        try:
            # Create a simple Python script to call the MCP tool
            script_content = f'''
import subprocess
import json
import sys

# Call the MCP tool via the parent process
result = subprocess.run([
    sys.executable, "-c", 
    """
import json
# Simulate MCP call - in real implementation this would use proper MCP client
params = {json.dumps(params)}
tool_name = '{tool_name}'
print(json.dumps({{"tool": tool_name, "params": params, "success": False, "error": "Direct MCP call needed"}}))
"""
], capture_output=True, text=True)

print(result.stdout)
'''
            # For now, let's use a more direct approach with manual API calls
            # This is a placeholder - in the actual implementation we'd use proper MCP calls
            return self._fallback_http_call(tool_name, params)
            
        except Exception as e:
            print(f"Error calling MCP tool {tool_name}: {e}")
            return None
    
    def _fallback_http_call(self, tool_name: str, params: Dict) -> Optional[Dict]:
        """Fallback to HTTP calls for now"""
        import requests
        base_url = "http://localhost:8193"
        
        try:
            if tool_name == "data_list_strings":
                url = f"{base_url}/strings"
                response = requests.get(url, params={
                    "filter": params.get("filter"),
                    "limit": params.get("limit", 2000),
                    "offset": params.get("offset", 0)
                })
                if response.status_code == 200:
                    return response.json()
                    
            elif tool_name == "xrefs_list":
                url = f"{base_url}/xrefs"
                response = requests.get(url, params=params)
                if response.status_code == 200:
                    return response.json()
                    
            elif tool_name == "functions_list":
                url = f"{base_url}/functions"
                response = requests.get(url, params=params)
                if response.status_code == 200:
                    return response.json()
                    
            elif tool_name == "functions_decompile":
                addr = params.get("address") or params.get("name")
                url = f"{base_url}/functions/{addr}/decompile"
                response = requests.get(url)
                if response.status_code == 200:
                    return response.json()
                    
            elif tool_name == "functions_get":
                addr = params.get("address") or params.get("name")
                url = f"{base_url}/functions/{addr}"
                response = requests.get(url)
                if response.status_code == 200:
                    return response.json()
                    
        except Exception as e:
            print(f"HTTP fallback error for {tool_name}: {e}")
            
        return None
    
    def get_strings_with_pattern(self, pattern: str) -> List[Dict]:
        """Get all strings matching a pattern"""
        result = self.call_mcp_tool("data_list_strings", {
            "filter": pattern,
            "limit": 500
        })
        
        if result and "result" in result:
            return result["result"]
        return []
    
    def find_functions_by_string_ref(self, string_addr: str) -> List[Dict]:
        """Find functions that reference a string"""
        result = self.call_mcp_tool("xrefs_list", {
            "to_addr": string_addr,
            "limit": 50
        })
        
        if result and "result" in result:
            xref_data = result["result"]
            if isinstance(xref_data, dict) and "references" in xref_data:
                refs = xref_data["references"]
                functions = []
                for ref in refs:
                    if "from_function" in ref and ref["from_function"]:
                        functions.append({
                            "address": ref["from_function"]["address"],
                            "name": ref["from_function"]["name"],
                            "ref_type": ref.get("refType", "unknown"),
                            "from_addr": ref["from_addr"]
                        })
                return functions
        return []
    
    def get_function_details(self, address: str) -> Optional[Dict]:
        """Get detailed function information"""
        return self.call_mcp_tool("functions_get", {"address": address})
    
    def decompile_function(self, address: str) -> Optional[Dict]:
        """Decompile a function"""
        return self.call_mcp_tool("functions_decompile", {"address": address})
    
    def get_function_xrefs(self, address: str, direction: str = "to") -> List[Dict]:
        """Get cross-references for a function"""
        param_key = "to_addr" if direction == "to" else "from_addr"
        result = self.call_mcp_tool("xrefs_list", {
            param_key: address,
            "limit": 100
        })
        
        if result and "result" in result:
            xref_data = result["result"]
            if isinstance(xref_data, dict) and "references" in xref_data:
                return xref_data["references"]
        return []
    
    def analyze_function_comprehensive(self, func_info: Dict, pattern: str, expected_type: str) -> Dict:
        """Comprehensive analysis of a single function"""
        address = func_info["address"]
        name = func_info["name"]
        
        print(f"  Analyzing function {name} at {address}")
        
        analysis = {
            "pattern": pattern,
            "expected_type": expected_type,
            "address": address,
            "current_name": name,
            "suggested_name": self.suggest_function_name(pattern, address),
            "decompiled_code": "",
            "assembly_code": "",
            "callers": [],
            "callees": [],
            "analysis_notes": [],
            "complexity_score": 0,
            "hid_operations": [],
            "error_handling": [],
            "interesting_constants": []
        }
        
        # Get detailed function info
        func_details = self.get_function_details(address)
        if func_details and "result" in func_details:
            details = func_details["result"]
            analysis.update({
                "size": details.get("size", 0),
                "parameter_count": details.get("parameter_count", 0),
                "local_vars": details.get("local_vars", 0)
            })
        
        # Decompile function
        decomp_result = self.decompile_function(address)
        if decomp_result and "result" in decomp_result:
            code = decomp_result["result"].get("decompiled_code", "")
            analysis["decompiled_code"] = code
            
            # Analyze decompiled code for patterns
            analysis["analysis_notes"].extend(self.analyze_code_patterns(code, pattern))
            analysis["complexity_score"] = self.calculate_complexity_score(code)
            analysis["hid_operations"] = self.extract_hid_operations(code)
            analysis["error_handling"] = self.extract_error_handling(code)
            analysis["interesting_constants"] = self.extract_constants(code)
        
        # Get callers
        callers = self.get_function_xrefs(address, "to")
        analysis["callers"] = [{
            "from_addr": ref["from_addr"],
            "function": ref.get("from_function", {}).get("name", "unknown"),
            "function_addr": ref.get("from_function", {}).get("address", ""),
            "type": ref.get("refType", "unknown")
        } for ref in callers]
        
        # Get callees
        callees = self.get_function_xrefs(address, "from")
        analysis["callees"] = [{
            "to_addr": ref["to_addr"],
            "symbol": ref.get("to_symbol", "unknown"),
            "type": ref.get("refType", "unknown")
        } for ref in callees]
        
        return analysis
    
    def suggest_function_name(self, pattern: str, address: str) -> str:
        """Suggest a meaningful function name based on pattern"""
        # This could be enhanced with more sophisticated naming logic
        return f"CPidDevice_{pattern}_{address[-4:]}"
    
    def analyze_code_patterns(self, code: str, pattern: str) -> List[str]:
        """Analyze decompiled code for interesting patterns"""
        notes = []
        
        if not code:
            return notes
        
        # Look for HID-related operations
        hid_patterns = [
            "HidP_SetUsageValue", "HidP_GetUsageValue", "HidP_SetUsages",
            "HidD_SetFeature", "HidD_GetFeature", "HidD_GetInputReport",
            "WriteFile", "ReadFile", "DeviceIoControl"
        ]
        
        for hid_pattern in hid_patterns:
            if hid_pattern in code:
                notes.append(f"Uses {hid_pattern} - HID operation")
        
        # Look for error handling
        if "ERROR:" in code or "error" in code.lower():
            notes.append("Contains error handling")
            
        # Look for logging
        if "printf" in code or "sprintf" in code or "LogMessage" in code:
            notes.append("Contains logging/debug output")
            
        # Look for effect-related constants
        effect_constants = ["0x01", "0x02", "0x04", "0x08", "EFFECT_", "PID_"]
        for const in effect_constants:
            if const in code:
                notes.append(f"Contains effect constant {const}")
        
        return notes
    
    def calculate_complexity_score(self, code: str) -> int:
        """Calculate a simple complexity score for the function"""
        if not code:
            return 0
            
        score = 0
        score += code.count("if") * 2
        score += code.count("while") * 3
        score += code.count("for") * 3
        score += code.count("switch") * 2
        score += code.count("case") * 1
        score += len(code.split('\n'))  # Lines of code
        
        return score
    
    def extract_hid_operations(self, code: str) -> List[str]:
        """Extract HID operations from code"""
        operations = []
        hid_functions = [
            "HidP_SetUsageValue", "HidP_GetUsageValue", "HidP_SetUsages",
            "HidD_SetFeature", "HidD_GetFeature", "HidD_GetInputReport",
            "WriteFile", "ReadFile", "DeviceIoControl"
        ]
        
        for func in hid_functions:
            if func in code:
                operations.append(func)
        
        return operations
    
    def extract_error_handling(self, code: str) -> List[str]:
        """Extract error handling patterns"""
        errors = []
        lines = code.split('\n')
        
        for line in lines:
            if 'ERROR:' in line or 'error' in line.lower():
                errors.append(line.strip())
                
        return errors[:10]  # Limit to first 10
    
    def extract_constants(self, code: str) -> List[str]:
        """Extract interesting constants"""
        import re
        constants = []
        
        # Find hex constants
        hex_matches = re.findall(r'0x[0-9a-fA-F]+', code)
        constants.extend(hex_matches[:20])  # Limit to first 20
        
        return list(set(constants))  # Remove duplicates
    
    def save_analysis_markdown(self, analysis: Dict):
        """Save individual analysis to markdown"""
        pattern = analysis["pattern"]
        address = analysis["address"]
        filename = f"tmpid_{pattern}_{address}.md"
        output_path = OUTPUT_DIR / "analysis" / filename
        output_path.parent.mkdir(parents=True, exist_ok=True)
        
        with open(output_path, 'w') as f:
            f.write(f"# {analysis['suggested_name']} Analysis\n\n")
            f.write(f"**Pattern**: {pattern}\n")
            f.write(f"**Type**: {analysis['expected_type']}\n")
            f.write(f"**Address**: `{address}`\n")
            f.write(f"**Current Name**: `{analysis['current_name']}`\n")
            f.write(f"**Suggested Name**: `{analysis['suggested_name']}`\n")
            f.write(f"**Complexity Score**: {analysis['complexity_score']}\n\n")
            
            if analysis['analysis_notes']:
                f.write("## Analysis Notes\n")
                for note in analysis['analysis_notes']:
                    f.write(f"- {note}\n")
                f.write("\n")
            
            if analysis['hid_operations']:
                f.write("## HID Operations\n")
                for op in analysis['hid_operations']:
                    f.write(f"- {op}\n")
                f.write("\n")
            
            if analysis['callers']:
                f.write(f"## Callers ({len(analysis['callers'])})\n")
                for caller in analysis['callers'][:20]:
                    f.write(f"- `{caller['from_addr']}` - {caller['function']} ({caller['type']})\n")
                f.write("\n")
            
            if analysis['callees']:
                f.write(f"## Callees ({len(analysis['callees'])})\n")
                for callee in analysis['callees'][:20]:
                    f.write(f"- `{callee['to_addr']}` - {callee['symbol']} ({callee['type']})\n")
                f.write("\n")
            
            if analysis['interesting_constants']:
                f.write("## Interesting Constants\n")
                for const in analysis['interesting_constants'][:20]:
                    f.write(f"- `{const}`\n")
                f.write("\n")
            
            if analysis['error_handling']:
                f.write("## Error Handling\n")
                for error in analysis['error_handling']:
                    f.write(f"- `{error}`\n")
                f.write("\n")
            
            f.write("## Decompiled Code\n")
            f.write(f"```c\n{analysis['decompiled_code']}\n```\n")
        
        print(f"    Saved analysis to {output_path}")
    
    def create_summary_report(self):
        """Create comprehensive summary report"""
        summary_path = OUTPUT_DIR / "findings" / "comprehensive_analysis_summary.md"
        summary_path.parent.mkdir(parents=True, exist_ok=True)
        
        with open(summary_path, 'w') as f:
            f.write("# T500RS tmpid.dll Comprehensive Analysis Summary\n\n")
            f.write(f"**Analysis Date**: {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
            f.write(f"**Total Functions Analyzed**: {len(self.analysis_results)}\n\n")
            
            # Group by type
            by_type = {}
            by_priority = {}
            
            for result in self.analysis_results:
                exp_type = result['expected_type']
                priority = result.get('priority', 'unknown')
                
                if exp_type not in by_type:
                    by_type[exp_type] = []
                by_type[exp_type].append(result)
                
                if priority not in by_priority:
                    by_priority[priority] = []
                by_priority[priority].append(result)
            
            # Summary by type
            f.write("## Functions by Type\n\n")
            for func_type, functions in by_type.items():
                f.write(f"### {func_type.replace('_', ' ').title()} ({len(functions)} functions)\n\n")
                for func in sorted(functions, key=lambda x: x['complexity_score'], reverse=True):
                    f.write(f"- **{func['suggested_name']}** (`{func['address']}`)\n")
                    f.write(f"  - Complexity: {func['complexity_score']}\n")
                    f.write(f"  - Callers: {len(func['callers'])}, Callees: {len(func['callees'])}\n")
                    if func['hid_operations']:
                        f.write(f"  - HID Ops: {', '.join(func['hid_operations'])}\n")
                    f.write("\n")
            
            # High complexity functions
            high_complexity = sorted([r for r in self.analysis_results if r['complexity_score'] > 50], 
                                   key=lambda x: x['complexity_score'], reverse=True)
            
            if high_complexity:
                f.write("## High Complexity Functions\n\n")
                for func in high_complexity[:10]:
                    f.write(f"- **{func['suggested_name']}** (Score: {func['complexity_score']})\n")
                    f.write(f"  - Pattern: {func['pattern']}\n")
                    f.write(f"  - Address: `{func['address']}`\n")
                    if func['analysis_notes']:
                        f.write(f"  - Notes: {'; '.join(func['analysis_notes'][:3])}\n")
                    f.write("\n")
            
            # Functions with most callers (likely important)
            most_called = sorted(self.analysis_results, key=lambda x: len(x['callers']), reverse=True)
            
            if most_called:
                f.write("## Most Referenced Functions\n\n")
                for func in most_called[:10]:
                    if len(func['callers']) > 0:
                        f.write(f"- **{func['suggested_name']}** ({len(func['callers'])} callers)\n")
                        f.write(f"  - Pattern: {func['pattern']}\n")
                        f.write(f"  - Address: `{func['address']}`\n\n")
        
        # Save JSON summary as well
        json_summary_path = OUTPUT_DIR / "findings" / "comprehensive_analysis_summary.json"
        with open(json_summary_path, 'w') as f:
            json.dump(self.analysis_results, f, indent=2)
        
        print(f"Summary report saved to {summary_path}")
        print(f"JSON data saved to {json_summary_path}")
    
    def run_comprehensive_analysis(self):
        """Main analysis routine"""
        print("Starting comprehensive tmpid.dll analysis using Ghidra MCP APIs...")
        print("=" * 80)
        
        total_functions_found = 0
        
        for func_info in TMPID_KEY_FUNCTIONS:
            pattern = func_info["pattern"]
            priority = func_info["priority"]
            expected_type = func_info["expected_type"]
            
            print(f"\n[Priority {priority}] Analyzing '{pattern}' functions ({expected_type})...")
            
            # Find strings matching pattern
            strings = self.get_strings_with_pattern(pattern)
            print(f"  Found {len(strings)} matching strings")
            
            if not strings:
                print(f"  No strings found for pattern '{pattern}' - skipping")
                continue
            
            pattern_functions = set()
            
            for string_data in strings[:10]:  # Analyze up to 10 strings per pattern
                string_addr = string_data["address"]
                string_val = string_data["value"]
                
                # Find functions that reference this string
                functions = self.find_functions_by_string_ref(string_addr)
                print(f"  String '{string_val[:50]}...' referenced by {len(functions)} functions")
                
                for func_info in functions:
                    func_addr = func_info["address"]
                    if func_addr not in pattern_functions:
                        pattern_functions.add(func_addr)
                        
                        try:
                            analysis = self.analyze_function_comprehensive(
                                func_info, pattern, expected_type
                            )
                            analysis["priority"] = priority
                            self.analysis_results.append(analysis)
                            
                            # Save individual analysis
                            self.save_analysis_markdown(analysis)
                            
                            total_functions_found += 1
                            time.sleep(0.2)  # Rate limiting
                            
                        except Exception as e:
                            print(f"    Error analyzing function {func_addr}: {e}")
                            continue
            
            print(f"  Completed analysis of {len(pattern_functions)} unique functions for '{pattern}'")
        
        print(f"\n" + "=" * 80)
        print(f"Analysis complete! Analyzed {total_functions_found} functions")
        
        # Create comprehensive summary
        self.create_summary_report()
        
        return self.analysis_results

def main():
    """Main entry point"""
    analyzer = GhidraMCPAnalyzer()
    
    try:
        results = analyzer.run_comprehensive_analysis()
        print(f"\nSuccess! Analysis complete with {len(results)} functions analyzed.")
        print(f"Check the 'analysis' and 'findings' directories for detailed results.")
        
    except KeyboardInterrupt:
        print("\nAnalysis interrupted by user")
        
    except Exception as e:
        print(f"\nError during analysis: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()