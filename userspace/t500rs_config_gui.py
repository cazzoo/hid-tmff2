#!/usr/bin/env python3
"""
T500RS Advanced Force Feedback Configuration GUI

This tool allows runtime configuration of advanced force feedback features:
- Force smoothing (exponential smoothing to prevent sudden jumps)
- Multi-effect mixing (how multiple effects are combined)
- Dynamic update rate (adaptive update frequency based on force changes)

Usage:
    python3 t500rs_config_gui.py [device]

If device is not specified, it will auto-detect the T500RS device.
"""

import sys
import os
import struct
import fcntl
import glob
import tkinter as tk
from tkinter import ttk, messagebox

# Custom event codes (must match driver)
# Using 0xC0-0xCF range to avoid conflicts with effect IDs (0-63)
FF_TOGGLE_SMOOTHING = 0xC0
FF_TOGGLE_MIXING = 0xC1
FF_TOGGLE_DYNAMIC_RATE = 0xC2
FF_GET_CONFIG = 0xC3

# Event types
EV_FF = 0x15

class T500RSConfig:
    def __init__(self, device_path=None):
        self.device_path = device_path or self.find_device()
        self.device_fd = None
        
        if not self.device_path:
            raise RuntimeError("T500RS device not found!")
        
        print(f"Using device: {self.device_path}")
        
    def find_device(self):
        """Auto-detect T500RS device"""
        # Look for T500RS in /dev/input/event*
        for event_file in glob.glob('/dev/input/event*'):
            try:
                with open(event_file, 'rb') as f:
                    # EVIOCGNAME - get device name
                    name_buf = bytearray(256)
                    fcntl.ioctl(f, 0x80ff4506, name_buf)
                    name = name_buf.split(b'\x00')[0].decode('utf-8', errors='ignore')
                    
                    if 'T500RS' in name or 'Force Feedback Wheel' in name:
                        print(f"Found T500RS: {event_file} ({name})")
                        return event_file
            except (IOError, OSError):
                continue
        
        return None
    
    def open_device(self):
        """Open device for writing events"""
        if self.device_fd is None:
            self.device_fd = os.open(self.device_path, os.O_WRONLY | os.O_NONBLOCK)
    
    def close_device(self):
        """Close device"""
        if self.device_fd is not None:
            os.close(self.device_fd)
            self.device_fd = None
    
    def send_event(self, event_code, value):
        """Send a force feedback event to the device"""
        self.open_device()
        
        # struct input_event {
        #     struct timeval time;  // 16 bytes on 64-bit
        #     __u16 type;           // 2 bytes
        #     __u16 code;           // 2 bytes
        #     __s32 value;          // 4 bytes
        # };
        
        # Pack the event (time is set to 0, kernel will fill it)
        event = struct.pack('llHHi', 0, 0, EV_FF, event_code, value)
        
        try:
            os.write(self.device_fd, event)
            print(f"Sent event: code=0x{event_code:02x}, value={value}")
            return True
        except OSError as e:
            print(f"Error sending event: {e}")
            return False
    
    def toggle_smoothing(self, enabled):
        """Toggle force smoothing"""
        return self.send_event(FF_TOGGLE_SMOOTHING, 1 if enabled else 0)
    
    def toggle_mixing(self, enabled):
        """Toggle multi-effect mixing"""
        return self.send_event(FF_TOGGLE_MIXING, 1 if enabled else 0)
    
    def toggle_dynamic_rate(self, enabled):
        """Toggle dynamic update rate"""
        return self.send_event(FF_TOGGLE_DYNAMIC_RATE, 1 if enabled else 0)
    
    def get_config(self):
        """Request current configuration (check driver logs)"""
        return self.send_event(FF_GET_CONFIG, 1)


class ConfigGUI:
    def __init__(self, root, config):
        self.root = root
        self.config = config
        
        self.root.title("T500RS Advanced Force Feedback Configuration")
        self.root.geometry("600x400")
        self.root.resizable(False, False)
        
        # Create main frame
        main_frame = ttk.Frame(root, padding="20")
        main_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        
        # Title
        title = ttk.Label(main_frame, text="T500RS Advanced FF Configuration", 
                         font=('Arial', 16, 'bold'))
        title.grid(row=0, column=0, columnspan=2, pady=(0, 20))
        
        # Device info
        device_label = ttk.Label(main_frame, text=f"Device: {config.device_path}",
                                font=('Arial', 10))
        device_label.grid(row=1, column=0, columnspan=2, pady=(0, 20))
        
        # Force Smoothing
        row = 2
        self.create_toggle_section(main_frame, row, 
            "Force Smoothing",
            "Exponential smoothing (0.3 factor) to prevent sudden force jumps.\n"
            "Disable for more direct/responsive feel.",
            self.config.toggle_smoothing,
            default=True)
        
        # Multi-Effect Mixing
        row += 3
        self.create_toggle_section(main_frame, row,
            "Multi-Effect Mixing",
            "Advanced mixing of multiple simultaneous effects (clamped addition).\n"
            "Disable to use only the strongest effect.",
            self.config.toggle_mixing,
            default=True)
        
        # Dynamic Update Rate
        row += 3
        self.create_toggle_section(main_frame, row,
            "Dynamic Update Rate",
            "Adaptive update frequency (25Hz-100Hz) based on force changes.\n"
            "Disable for fixed 50Hz update rate.",
            self.config.toggle_dynamic_rate,
            default=True)
        
        # Buttons
        button_frame = ttk.Frame(main_frame)
        button_frame.grid(row=row+3, column=0, columnspan=2, pady=(20, 0))
        
        ttk.Button(button_frame, text="Get Current Config", 
                  command=self.get_config).pack(side=tk.LEFT, padx=5)
        ttk.Button(button_frame, text="Reset to Defaults", 
                  command=self.reset_defaults).pack(side=tk.LEFT, padx=5)
        ttk.Button(button_frame, text="Close", 
                  command=self.root.quit).pack(side=tk.LEFT, padx=5)
        
        # Status bar
        self.status_var = tk.StringVar(value="Ready")
        status_bar = ttk.Label(main_frame, textvariable=self.status_var, 
                              relief=tk.SUNKEN, anchor=tk.W)
        status_bar.grid(row=row+4, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=(20, 0))
        
    def create_toggle_section(self, parent, row, title, description, callback, default=True):
        """Create a toggle section with title, description, and checkbox"""
        # Title
        title_label = ttk.Label(parent, text=title, font=('Arial', 12, 'bold'))
        title_label.grid(row=row, column=0, sticky=tk.W, pady=(10, 5))
        
        # Description
        desc_label = ttk.Label(parent, text=description, wraplength=550, 
                              justify=tk.LEFT, foreground='gray')
        desc_label.grid(row=row+1, column=0, columnspan=2, sticky=tk.W, pady=(0, 5))
        
        # Checkbox
        var = tk.BooleanVar(value=default)
        checkbox = ttk.Checkbutton(parent, text="Enabled", variable=var,
                                   command=lambda: self.on_toggle(title, var.get(), callback))
        checkbox.grid(row=row, column=1, sticky=tk.E)
        
        # Store variable for reset
        setattr(self, f"{title.lower().replace(' ', '_').replace('-', '_')}_var", var)
        
        # Send initial state
        callback(default)
    
    def on_toggle(self, name, enabled, callback):
        """Handle toggle change"""
        if callback(enabled):
            status = "ENABLED" if enabled else "DISABLED"
            self.status_var.set(f"{name}: {status}")
            print(f"{name}: {status}")
        else:
            self.status_var.set(f"Error setting {name}")
    
    def get_config(self):
        """Request current configuration"""
        if self.config.get_config():
            self.status_var.set("Configuration requested - check driver logs")
            messagebox.showinfo("Configuration", 
                              "Current configuration has been logged.\n"
                              "Check the driver output/logs to see the current settings.")
        else:
            self.status_var.set("Error requesting configuration")
    
    def reset_defaults(self):
        """Reset all settings to defaults"""
        # All defaults are True (enabled)
        self.force_smoothing_var.set(True)
        self.multi_effect_mixing_var.set(True)
        self.dynamic_update_rate_var.set(True)
        
        # Send to driver
        self.config.toggle_smoothing(True)
        self.config.toggle_mixing(True)
        self.config.toggle_dynamic_rate(True)
        
        self.status_var.set("Reset to defaults (all enabled)")
        print("Reset to defaults")


def main():
    # Get device path from command line or auto-detect
    device_path = sys.argv[1] if len(sys.argv) > 1 else None
    
    try:
        # Create configuration interface
        config = T500RSConfig(device_path)
        
        # Create GUI
        root = tk.Tk()
        app = ConfigGUI(root, config)
        
        # Run
        root.mainloop()
        
        # Cleanup
        config.close_device()
        
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)


if __name__ == '__main__':
    main()

