#!/usr/bin/env python3
"""
T500RS Control GUI
Simple graphical interface for controlling T500RS force feedback settings
"""

import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, GLib
import glob
import os
import sys

class T500RSControlGUI(Gtk.Window):
    def __init__(self):
        super().__init__(title="T500RS Force Feedback Control")
        self.set_border_width(10)
        self.set_default_size(600, 700)
        
        # Find device path
        self.device_path = self.find_device()
        
        # Main container
        main_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        self.add(main_box)
        
        # Device status
        status_frame = Gtk.Frame(label="Device Status")
        status_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=5)
        status_box.set_border_width(10)
        status_frame.add(status_box)
        
        if self.device_path:
            status_label = Gtk.Label(label=f"✓ T500RS detected at:\n{self.device_path}")
            status_label.set_line_wrap(True)
        else:
            status_label = Gtk.Label(label="✗ T500RS not detected!\nPlease connect the wheel and restart.")
            status_label.set_line_wrap(True)
        status_box.pack_start(status_label, False, False, 0)
        
        main_box.pack_start(status_frame, False, False, 0)
        
        if not self.device_path:
            self.show_all()
            return
        
        # Scrolled window for settings
        scrolled = Gtk.ScrolledWindow()
        scrolled.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        main_box.pack_start(scrolled, True, True, 0)
        
        settings_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        scrolled.add(settings_box)
        
        # Global Settings
        global_frame = Gtk.Frame(label="Global Settings")
        global_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=5)
        global_box.set_border_width(10)
        global_frame.add(global_box)
        settings_box.pack_start(global_frame, False, False, 0)
        
        self.gain_scale = self.create_scale("Global Gain", 0, 65535, 1000, global_box, "gain")
        self.range_scale = self.create_scale("Rotation Range (degrees)", 270, 1080, 10, global_box, "range")
        
        # Per-Effect Gains
        effect_frame = Gtk.Frame(label="Per-Effect Gains (0-100%)")
        effect_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=5)
        effect_box.set_border_width(10)
        effect_frame.add(effect_box)
        settings_box.pack_start(effect_frame, False, False, 0)
        
        self.constant_gain_scale = self.create_scale("Constant Force", 0, 100, 5, effect_box, "constant_gain")
        self.periodic_gain_scale = self.create_scale("Periodic Effects", 0, 100, 5, effect_box, "periodic_gain")
        self.spring_gain_scale = self.create_scale("Spring Effects", 0, 100, 5, effect_box, "t500rs_spring_gain")
        self.damper_gain_scale = self.create_scale("Damper Effects", 0, 100, 5, effect_box, "t500rs_damper_gain")
        
        # Base Levels
        base_frame = Gtk.Frame(label="Base Effect Levels (0-100%)")
        base_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=5)
        base_box.set_border_width(10)
        base_frame.add(base_box)
        settings_box.pack_start(base_frame, False, False, 0)
        
        self.spring_level_scale = self.create_scale("Spring Level", 0, 100, 5, base_box, "spring_level")
        self.damper_level_scale = self.create_scale("Damper Level", 0, 100, 5, base_box, "damper_level")
        self.friction_level_scale = self.create_scale("Friction Level", 0, 100, 5, base_box, "friction_level")
        
        # Autocenter (FFB API control)
        autocenter_frame = Gtk.Frame(label="Autocenter (Self-Centering Spring)")
        autocenter_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=5)
        autocenter_box.set_border_width(10)
        autocenter_frame.add(autocenter_box)
        settings_box.pack_start(autocenter_frame, False, False, 0)
        
        autocenter_info = Gtk.Label(label="Note: Autocenter is controlled via FFB API, not sysfs.\nUse fftest or games to control autocenter.")
        autocenter_info.set_line_wrap(True)
        autocenter_box.pack_start(autocenter_info, False, False, 0)
        
        # Buttons
        button_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        main_box.pack_start(button_box, False, False, 0)
        
        refresh_btn = Gtk.Button(label="Refresh Values")
        refresh_btn.connect("clicked", self.on_refresh_clicked)
        button_box.pack_start(refresh_btn, True, True, 0)
        
        preset_btn = Gtk.Button(label="Load Preset...")
        preset_btn.connect("clicked", self.on_preset_clicked)
        button_box.pack_start(preset_btn, True, True, 0)
        
        # Load initial values
        self.load_all_values()
        
        self.show_all()
    
    def find_device(self):
        """Find T500RS device path"""
        paths = glob.glob("/sys/bus/hid/devices/0003:044F:B65E.*")
        if paths:
            return paths[0]
        return None
    
    def create_scale(self, label_text, min_val, max_val, step, container, attr_name):
        """Create a labeled scale widget"""
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        
        # Label with current value
        label = Gtk.Label()
        label.set_markup(f"<b>{label_text}</b>: <span color='blue'>Loading...</span>")
        label.set_xalign(0)
        box.pack_start(label, False, False, 0)
        
        # Scale
        adjustment = Gtk.Adjustment(value=min_val, lower=min_val, upper=max_val, 
                                   step_increment=step, page_increment=step*10)
        scale = Gtk.Scale(orientation=Gtk.Orientation.HORIZONTAL, adjustment=adjustment)
        scale.set_digits(0)
        scale.set_value_pos(Gtk.PositionType.RIGHT)
        scale.connect("value-changed", self.on_scale_changed, attr_name, label, label_text)
        box.pack_start(scale, False, False, 0)
        
        container.pack_start(box, False, False, 0)
        
        # Store references
        scale.label_widget = label
        scale.attr_name = attr_name
        scale.label_text = label_text
        
        return scale
    
    def on_scale_changed(self, scale, attr_name, label, label_text):
        """Handle scale value change"""
        value = int(scale.get_value())
        
        # Update label
        if attr_name == "gain":
            percent = int((value / 65535) * 100)
            label.set_markup(f"<b>{label_text}</b>: <span color='blue'>{value}</span> ({percent}%)")
        elif attr_name == "range":
            label.set_markup(f"<b>{label_text}</b>: <span color='blue'>{value}°</span>")
        else:
            label.set_markup(f"<b>{label_text}</b>: <span color='blue'>{value}%</span>")
        
        # Write to sysfs
        self.write_sysfs(attr_name, value)
    
    def read_sysfs(self, attr_name):
        """Read value from sysfs"""
        if not self.device_path:
            return None
        
        path = os.path.join(self.device_path, attr_name)
        try:
            with open(path, 'r') as f:
                return int(f.read().strip())
        except (FileNotFoundError, PermissionError, ValueError) as e:
            print(f"Error reading {attr_name}: {e}")
            return None
    
    def write_sysfs(self, attr_name, value):
        """Write value to sysfs"""
        if not self.device_path:
            return False
        
        path = os.path.join(self.device_path, attr_name)
        try:
            with open(path, 'w') as f:
                f.write(str(value))
            return True
        except (FileNotFoundError, PermissionError) as e:
            print(f"Error writing {attr_name}: {e}")
            dialog = Gtk.MessageDialog(
                transient_for=self,
                flags=0,
                message_type=Gtk.MessageType.ERROR,
                buttons=Gtk.ButtonsType.OK,
                text="Permission Error"
            )
            dialog.format_secondary_text(
                f"Cannot write to {attr_name}.\n"
                f"Please run with sudo:\n"
                f"sudo python3 {sys.argv[0]}"
            )
            dialog.run()
            dialog.destroy()
            return False
    
    def load_all_values(self):
        """Load all values from sysfs"""
        scales = [
            self.gain_scale,
            self.range_scale,
            self.constant_gain_scale,
            self.periodic_gain_scale,
            self.spring_gain_scale,
            self.damper_gain_scale,
            self.spring_level_scale,
            self.damper_level_scale,
            self.friction_level_scale
        ]
        
        for scale in scales:
            value = self.read_sysfs(scale.attr_name)
            if value is not None:
                # Temporarily block signal to avoid writing back
                scale.handler_block_by_func(self.on_scale_changed)
                scale.set_value(value)
                scale.handler_unblock_by_func(self.on_scale_changed)
                
                # Update label
                if scale.attr_name == "gain":
                    percent = int((value / 65535) * 100)
                    scale.label_widget.set_markup(
                        f"<b>{scale.label_text}</b>: <span color='blue'>{value}</span> ({percent}%)"
                    )
                elif scale.attr_name == "range":
                    scale.label_widget.set_markup(
                        f"<b>{scale.label_text}</b>: <span color='blue'>{value}°</span>"
                    )
                else:
                    scale.label_widget.set_markup(
                        f"<b>{scale.label_text}</b>: <span color='blue'>{value}%</span>"
                    )
            else:
                scale.label_widget.set_markup(
                    f"<b>{scale.label_text}</b>: <span color='red'>Error reading value</span>"
                )
    
    def on_refresh_clicked(self, button):
        """Refresh all values from device"""
        self.load_all_values()
    
    def on_preset_clicked(self, button):
        """Show preset selection dialog"""
        dialog = Gtk.Dialog(title="Load Preset", transient_for=self, flags=0)
        dialog.add_buttons(
            Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL,
            Gtk.STOCK_OK, Gtk.ResponseType.OK
        )
        
        box = dialog.get_content_area()
        box.set_spacing(10)
        box.set_border_width(10)
        
        label = Gtk.Label(label="Select a preset configuration:")
        box.pack_start(label, False, False, 0)
        
        # Preset radio buttons
        racing_radio = Gtk.RadioButton(label="Racing Simulation (Realistic)")
        arcade_radio = Gtk.RadioButton(label="Arcade Racing (Easy)", group=racing_radio)
        rally_radio = Gtk.RadioButton(label="Rally/Drift", group=racing_radio)
        
        box.pack_start(racing_radio, False, False, 0)
        box.pack_start(arcade_radio, False, False, 0)
        box.pack_start(rally_radio, False, False, 0)
        
        dialog.show_all()
        response = dialog.run()
        
        if response == Gtk.ResponseType.OK:
            if racing_radio.get_active():
                self.load_preset_racing()
            elif arcade_radio.get_active():
                self.load_preset_arcade()
            elif rally_radio.get_active():
                self.load_preset_rally()
        
        dialog.destroy()
    
    def load_preset_racing(self):
        """Load Racing Simulation preset"""
        self.gain_scale.set_value(65535)  # 100%
        self.range_scale.set_value(900)
        self.constant_gain_scale.set_value(100)
        self.periodic_gain_scale.set_value(100)
        self.spring_gain_scale.set_value(80)
        self.damper_gain_scale.set_value(70)
        self.spring_level_scale.set_value(30)
        self.damper_level_scale.set_value(30)
        self.friction_level_scale.set_value(30)
    
    def load_preset_arcade(self):
        """Load Arcade Racing preset"""
        self.gain_scale.set_value(32768)  # 50%
        self.range_scale.set_value(540)
        self.constant_gain_scale.set_value(80)
        self.periodic_gain_scale.set_value(100)
        self.spring_gain_scale.set_value(60)
        self.damper_gain_scale.set_value(50)
        self.spring_level_scale.set_value(30)
        self.damper_level_scale.set_value(30)
        self.friction_level_scale.set_value(30)
    
    def load_preset_rally(self):
        """Load Rally/Drift preset"""
        self.gain_scale.set_value(49152)  # 75%
        self.range_scale.set_value(540)
        self.constant_gain_scale.set_value(100)
        self.periodic_gain_scale.set_value(100)
        self.spring_gain_scale.set_value(50)
        self.damper_gain_scale.set_value(80)
        self.spring_level_scale.set_value(30)
        self.damper_level_scale.set_value(30)
        self.friction_level_scale.set_value(30)

def main():
    app = T500RSControlGUI()
    app.connect("destroy", Gtk.main_quit)
    Gtk.main()

if __name__ == "__main__":
    main()

