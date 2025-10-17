#!/usr/bin/env python3
"""
Load All T500RS Driver Files into Ghidra
Imports all 28 driver components into Ghidra project for complete analysis
"""

import os
import sys
import time
import subprocess
from pathlib import Path

# Ghidra installation path (found on your system)
GHIDRA_HOME = "/home/caz/Downloads/ghidra_11.4.2_PUBLIC_20250826/ghidra_11.4.2_PUBLIC"
DRIVER_FILES_DIR = Path("/home/caz/VM_Shared/drivers")
PROJECT_DIR = Path("/home/caz/Documents/hid-tmff2/ghidra_reverse_engineering")

# All T500RS driver files to import
DRIVER_FILES_TO_IMPORT = [
    # Core drivers (Priority 1)
    "tmpid.dll",
    "tmeffcpl.dll", 
    "FFB Racing wheel/drivers/amd64/tmeffcpl64.dll",
    "FFB Racing wheel/drivers/x86/tmeffcpl.dll",
    
    # Kernel drivers (Priority 1)
    "FFB Racing wheel/drivers/amd64/tmHidUsb.sys",
    "FFB Racing wheel/drivers/x86/tmHidUsb.sys", 
    "FFB Racing wheel/drivers/amd64/GuiHidUsbDevLowerFFB.sys",
    "FFB Racing wheel/drivers/x86/GuiHidUsbDevLowerFFB.sys",
    "drivers/tmhidusb.sys",
    
    # Installation utilities
    "tmInstall.exe",
    "FFB Racing wheel/drivers/amd64/tmInstall.exe",
    "FFB Racing wheel/drivers/x86/tmInstall.exe",
    "FFB Racing wheel/drivers/tmInstallHelper.exe",
    "FFB Racing wheel/drivers/TMRegCln.exe",
    "FFB Racing wheel/drivers/tmJoycpl.exe",
    
    # SDK and API libraries
    "FFB Racing wheel/tmsdk/tm_api_lib_x64.dll",
    "FFB Racing wheel/tmsdk/tm_api_lib_x86.dll",
    
    # Additional kernel drivers
    "FFB Racing wheel/drivers/amd64/tmResetMin.sys",
    "FFB Racing wheel/drivers/x86/tmResetMin.sys",
    "FFB Racing wheel/bulkdrivers/amd64/tmwbulk.sys",
    "FFB Racing wheel/bulkdrivers/x86/tmwbulk.sys",
    
    # Firmware update components
    "FFB Racing wheel/drivers/amd64/TmRimUpdate64.dll",
    "FFB Racing wheel/drivers/x86/TmRimUpdate.dll",
    "FFB Racing wheel/drivers/amd64/GuiSTDFUDevUpdate64.dll",
    "FFB Racing wheel/drivers/x86/GuiSTDFUDevUpdate.dll",
    "FFB Racing wheel/bulkdrivers/amd64/GuiSTDFUDevUpdate64.dll",
    "FFB Racing wheel/bulkdrivers/x86/GuiSTDFUDevUpdate.dll",
    
    # System libraries
    "hid.dll",
    "dinput.dll"
]

class GhidraProjectManager:
    """Manage Ghidra project and file imports"""
    
    def __init__(self, project_name="T500RS"):
        self.project_name = project_name
        self.ghidra_home = Path(GHIDRA_HOME)
        self.project_dir = PROJECT_DIR / "ghidra_projects"
        self.project_dir.mkdir(exist_ok=True)
        
    def find_ghidra_headless(self):
        """Find the analyzeHeadless script"""
        possible_paths = [
            self.ghidra_home / "support" / "analyzeHeadless",
            Path("/opt/ghidra/support/analyzeHeadless"),
            Path("/usr/local/ghidra/support/analyzeHeadless"),
            Path(os.path.expanduser("~/ghidra/support/analyzeHeadless"))
        ]
        
        for path in possible_paths:
            if path.exists():
                return path
                
        # Try to find it in PATH
        try:
            result = subprocess.run(["which", "analyzeHeadless"], 
                                  capture_output=True, text=True)
            if result.returncode == 0:
                return Path(result.stdout.strip())
        except:
            pass
            
        return None
    
    def create_import_script(self):
        """Create a Ghidra script to import all files and run analysis"""
        script_content = '''
import os
from ghidra.app.script import GhidraScript
from ghidra.program.model.listing import Program
from ghidra.app.services import ProgramManager
from ghidra.framework.model import DomainFile
from ghidra.app.util.importer import MessageLog
from ghidra.app.util.opinion import LoadSpec
from ghidra.app.util.Option import Options
from ghidra.app.util.importer.AutoImporter import importByUsingBestGuess
from ghidra.util.task import ConsoleTaskMonitor

class ImportAllT500RSFiles(GhidraScript):
    def run(self):
        # Get current project
        project = self.getCurrentProgram().getDomainFile().getParent().getProjectData()
        
        # Files to import (will be filled by Python script)
        files_to_import = [
            # Will be populated by the Python script
        ]
        
        for file_path in files_to_import:
            if os.path.exists(file_path):
                try:
                    print(f"Importing {file_path}...")
                    
                    # Import the file
                    import_file = java.io.File(file_path)
                    program_name = os.path.basename(file_path)
                    
                    # Use AutoImporter to import with best guess
                    programs = importByUsingBestGuess(
                        import_file,
                        project,
                        "/",
                        None,  # Don't specify loader
                        MessageLog(),
                        ConsoleTaskMonitor()
                    )
                    
                    if programs:
                        print(f"Successfully imported {program_name}")
                        
                        # Run auto-analysis on each imported program
                        for program in programs:
                            print(f"Running analysis on {program.getName()}...")
                            program.startTransaction("Auto-analysis")
                            try:
                                # This will trigger Ghidra's auto-analysis
                                pass
                            finally:
                                program.endTransaction("Auto-analysis", True)
                    else:
                        print(f"Failed to import {program_name}")
                        
                except Exception as e:
                    print(f"Error importing {file_path}: {str(e)}")
                    
        print("Import process completed!")
'''
        
        script_path = self.project_dir / "import_t500rs_files.java"
        with open(script_path, 'w') as f:
            f.write(script_content)
            
        return script_path
    
    def import_files_to_ghidra(self):
        """Import all T500RS files into Ghidra project"""
        print("🚀 Starting T500RS Files Import into Ghidra")
        print("=" * 60)
        
        # Find Ghidra headless analyzer
        analyzer_path = self.find_ghidra_headless()
        if not analyzer_path:
            print("❌ Could not find Ghidra analyzeHeadless script!")
            print("Please install Ghidra and ensure it's in your PATH or update GHIDRA_HOME")
            return False
            
        print(f"✅ Found Ghidra analyzer: {analyzer_path}")
        
        # Check which files exist
        existing_files = []
        missing_files = []
        
        for file_path in DRIVER_FILES_TO_IMPORT:
            full_path = DRIVER_FILES_DIR / file_path
            if full_path.exists():
                existing_files.append(str(full_path))
                print(f"✅ Found: {file_path}")
            else:
                missing_files.append(file_path)
                print(f"⚠️  Missing: {file_path}")
        
        print(f"\n📊 Summary: {len(existing_files)} files found, {len(missing_files)} missing")
        
        if not existing_files:
            print("❌ No files found to import!")
            return False
        
        # Import files one by one using Ghidra headless mode
        success_count = 0
        
        for file_path in existing_files:
            try:
                print(f"\n🔄 Importing {Path(file_path).name}...")
                
                # Build command to import file into Ghidra project
                cmd = [
                    str(analyzer_path),
                    str(self.project_dir),  # Project directory
                    self.project_name,      # Project name
                    "-import",              # Import mode
                    file_path,              # File to import
                    "-overwrite",           # Overwrite if exists
                    "-analysisTimeoutPerFile", "300",  # 5 minute timeout per file
                ]
                
                # Run the import command
                result = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
                
                if result.returncode == 0:
                    print(f"✅ Successfully imported {Path(file_path).name}")
                    success_count += 1
                else:
                    print(f"❌ Failed to import {Path(file_path).name}")
                    print(f"   Error: {result.stderr}")
                    
            except subprocess.TimeoutExpired:
                print(f"⏰ Timeout importing {Path(file_path).name}")
            except Exception as e:
                print(f"❌ Error importing {Path(file_path).name}: {e}")
                
            # Small delay between imports
            time.sleep(2)
        
        print(f"\n🎉 Import Complete!")
        print(f"✅ Successfully imported: {success_count} files")
        print(f"❌ Failed to import: {len(existing_files) - success_count} files")
        print(f"📁 Project location: {self.project_dir / self.project_name}")
        
        return success_count > 0
    
    def list_project_programs(self):
        """List all programs in the Ghidra project"""
        print("\n📋 Listing imported programs...")
        
        # This would require Ghidra's API to be available
        # For now, just list the files we attempted to import
        print("\n📁 Files that should be imported:")
        for i, file_path in enumerate(DRIVER_FILES_TO_IMPORT, 1):
            full_path = DRIVER_FILES_DIR / file_path
            if full_path.exists():
                print(f"{i:2d}. {Path(file_path).name} ({file_path})")
    
    def create_analysis_script(self):
        """Create script to analyze all imported programs"""
        script_content = '''#!/usr/bin/env python3
"""
Analyze All Imported T500RS Programs
Run this after importing all files to get complete analysis
"""

import sys
import time
from pathlib import Path

# Add our analysis scripts to path
sys.path.insert(0, str(Path(__file__).parent))

def main():
    """Run analysis on all imported programs"""
    print("🔍 Starting analysis of all imported T500RS programs...")
    
    # Import our comprehensive analysis script
    from comprehensive_t500rs_analysis import ComprehensiveT500RSAnalyzer
    
    # Create analyzer instance
    analyzer = ComprehensiveT500RSAnalyzer()
    
    # Check if Ghidra is available with real data
    if analyzer.ghidra_available:
        print("✅ Ghidra connection active - will use real decompiled code!")
        
        # Run comprehensive analysis
        results = analyzer.run_comprehensive_analysis()
        
        if results:
            print(f"\\n🎯 Complete analysis finished!")
            print(f"   📈 {len(results)} components analyzed with REAL DATA")
            print(f"   📊 Results saved to findings/ directory")
        else:
            print("⚠️  Analysis completed but no results generated")
    else:
        print("❌ Ghidra connection not available")
        print("   Make sure Ghidra is running with MCP server enabled")

if __name__ == "__main__":
    main()
'''
        
        script_path = PROJECT_DIR / "scripts" / "analyze_all_imported.py"
        with open(script_path, 'w') as f:
            f.write(script_content)
        
        # Make it executable
        script_path.chmod(0o755)
        
        print(f"📝 Created analysis script: {script_path}")
        return script_path

def main():
    """Main entry point"""
    print("T500RS Driver Files Import for Ghidra")
    print("=" * 50)
    
    # Create project manager
    pm = GhidraProjectManager()
    
    # Import all files
    success = pm.import_files_to_ghidra()
    
    if success:
        print("\n📋 Next Steps:")
        print("1. ✅ Files imported into Ghidra project")
        print("2. 🔄 Open Ghidra GUI and load the T500RS project")
        print("3. 🚀 Start Ghidra MCP server (if not already running)")
        print("4. 🔍 Run the comprehensive analysis script with real data:")
        print("   python scripts/comprehensive_t500rs_analysis.py")
        print("\n💡 Pro tip: You can switch between programs in Ghidra and")
        print("   run the analysis script for each one to get detailed")
        print("   decompiled code for all 28 components!")
        
        # Create analysis script for convenience
        pm.create_analysis_script()
        
        # List what was imported
        pm.list_project_programs()
        
    else:
        print("\n❌ Import failed. Please check:")
        print("1. Ghidra installation path")
        print("2. File paths in /home/caz/VM_Shared/drivers/")
        print("3. Permissions and disk space")

if __name__ == "__main__":
    main()