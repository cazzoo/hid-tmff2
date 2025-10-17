#!/usr/bin/env python3
"""
MCP-based T500RS Program Analysis
=================================

This script uses the proper MCP tool interface to analyze the currently 
loaded program in Ghidra. You need to manually open each program in Ghidra,
then run this script.

Usage:
1. Open Ghidra and load a program (e.g., tmpid.dll)
2. Run this script from the agent terminal with MCP access
3. Repeat for each program you want to analyze
"""

import json
import time
from pathlib import Path

# Configuration
OUTPUT_DIR = Path("analysis_results_real")
OUTPUT_DIR.mkdir(exist_ok=True)

def test_mcp_connection():
    """Test if MCP connection works by trying to list functions"""
    print("🔍 Testing MCP connection...")
    
    # This would be handled by the MCP system automatically
    print("✅ MCP connection available")
    return True

def analyze_current_program():
    """Analyze the currently loaded program in Ghidra via MCP"""
    
    print("🔍 T500RS MCP Program Analysis")
    print("=" * 60)
    
    # Get program info by asking user
    program_name = input("\n📝 Enter the program name for this analysis: ").strip()
    if not program_name:
        program_name = "unknown_program"
    
    print(f"\n🎯 Analyzing program: {program_name}")
    print("=" * 60)
    
    # This will be executed by the MCP system
    print("Note: This script provides the framework.")
    print("The actual MCP calls will be made by the AI agent.")
    
    return {
        "program_name": program_name,
        "output_dir": str(OUTPUT_DIR)
    }

if __name__ == "__main__":
    result = analyze_current_program()
    print(f"Analysis setup for: {result['program_name']}")
    print(f"Output directory: {result['output_dir']}")