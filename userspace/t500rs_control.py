#!/usr/bin/env python3
"""
T500RS Control Panel v2.0 - Complete GUI
"""
import sys, os, struct, glob, json, subprocess, select
from pathlib import Path
from datetime import datetime

try:
    from PyQt5.QtWidgets import *
    from PyQt5.QtCore import Qt, QTimer
    from PyQt5.QtGui import QFont
except ImportError:
    print("Install PyQt5: sudo apt install python3-pyqt5")
    sys.exit(1)

# Constants
EV_FF = 0x15
FF_GAIN = 0x60
FF_AUTOCENTER = 0x61
FF_CONSTANT = 0x50
FF_PERIODIC = 0x51
FF_SPRING = 0x52
FF_DAMPER = 0x53

# Per-effect-type gain codes (custom)
FF_GAIN_CONSTANT = 0x70
FF_GAIN_PERIODIC = 0x71
FF_GAIN_SPRING = 0x72
FF_GAIN_DAMPER = 0x73
FF_GAIN_FRICTION = 0x74
FF_GAIN_INERTIA = 0x75
FF_ROTATION_ANGLE = 0x76

CONFIG_DIR = Path.home() / '.config' / 't500rs-control'
PROFILES_FILE = CONFIG_DIR / 'profiles.json'
SETTINGS_FILE = CONFIG_DIR / 'settings.json'

ROTATION_ANGLES = {90: 0x01, 180: 0x02, 360: 0x03, 500: 0x04, 900: 0x05, 1080: 0x06}

class T500RSControl(QMainWindow):
    def __init__(self):
        super().__init__()
        self.device_fd = None
        self.device_path = None
        self.input_fd = None  # Separate fd for reading input events
        self.dpad_x = 0  # D-pad X axis
        self.dpad_y = 0  # D-pad Y axis
        self.profiles = {}
        self.settings = {
            'last_gain': 100, 'last_autocenter': 0, 'last_rotation': 1080,
            'constant_gain': 100, 'periodic_gain': 100, 'spring_gain': 100,
            'damper_gain': 100, 'friction_gain': 100, 'inertia_gain': 100,
            'combined_pedals': False
        }

        CONFIG_DIR.mkdir(parents=True, exist_ok=True)
        self.load_profiles()
        self.load_settings()

        self.init_ui()
        self.find_device()

        # Timer for reading device input
        self.input_timer = QTimer()
        self.input_timer.timeout.connect(self.read_device_input)
        self.input_timer.start(50)  # 20Hz

        # Device check timer
        self.device_timer = QTimer()
        self.device_timer.timeout.connect(self.check_device)
        self.device_timer.start(5000)

    def init_ui(self):
        self.setWindowTitle('T500RS Control Panel v2.0')
        self.setGeometry(100, 100, 900, 750)

        widget = QWidget()
        self.setCentralWidget(widget)
        layout = QVBoxLayout()
        widget.setLayout(layout)

        # Status
        self.status_label = QLabel('Looking for T500RS...')
        self.status_label.setStyleSheet('padding: 10px; background: #f0f0f0; border-radius: 5px; font-weight: bold;')
        layout.addWidget(self.status_label)

        # Tabs
        tabs = QTabWidget()
        tabs.addTab(self.create_device_test_tab(), "📊 Device Test")
        tabs.addTab(self.create_ffb_test_tab(), "🎮 FFB Test")
        tabs.addTab(self.create_force_adjustment_tab(), "⚙️ Force Adjustment")
        tabs.addTab(self.create_profile_tab(), "💾 Profiles")
        layout.addWidget(tabs)

        self.statusBar().showMessage('Ready')

    def create_device_test_tab(self):
        widget = QWidget()
        layout = QVBoxLayout()
        widget.setLayout(layout)

        info = QLabel('Real-time device input monitoring')
        info.setStyleSheet('color: gray; padding: 5px;')
        layout.addWidget(info)

        # Steering
        wheel_group = QGroupBox('Steering Wheel')
        wheel_layout = QVBoxLayout()
        wheel_group.setLayout(wheel_layout)

        self.wheel_label = QLabel('Position: 0 (Center)')
        self.wheel_label.setFont(QFont('Arial', 11, QFont.Bold))
        wheel_layout.addWidget(self.wheel_label)

        self.wheel_bar = QProgressBar()
        self.wheel_bar.setRange(0, 65535)
        self.wheel_bar.setValue(32768)
        self.wheel_bar.setTextVisible(False)
        wheel_layout.addWidget(self.wheel_bar)

        layout.addWidget(wheel_group)

        # Pedals
        pedals_group = QGroupBox('Pedals')
        pedals_layout = QVBoxLayout()
        pedals_group.setLayout(pedals_layout)

        self.combined_pedals_check = QCheckBox('Combined Pedals Mode')
        self.combined_pedals_check.setChecked(self.settings.get('combined_pedals', False))
        pedals_layout.addWidget(self.combined_pedals_check)

        for name in ['Throttle', 'Brake', 'Clutch']:
            hlayout = QHBoxLayout()
            hlayout.addWidget(QLabel(f'{name}:'))
            bar = QProgressBar()
            bar.setRange(0, 100)
            hlayout.addWidget(bar)
            label = QLabel('0%')
            label.setMinimumWidth(40)
            hlayout.addWidget(label)
            pedals_layout.addLayout(hlayout)
            setattr(self, f'{name.lower()}_bar', bar)
            setattr(self, f'{name.lower()}_label', label)

        layout.addWidget(pedals_group)

        # D-pad
        dpad_group = QGroupBox('D-Pad')
        dpad_layout = QVBoxLayout()
        dpad_group.setLayout(dpad_layout)

        # D-pad visual display
        dpad_grid = QGridLayout()

        # Create D-pad buttons (visual indicators)
        self.dpad_buttons = {}
        dpad_positions = {
            'up': (0, 1),
            'down': (2, 1),
            'left': (1, 0),
            'right': (1, 2),
            'center': (1, 1)
        }

        for direction, (row, col) in dpad_positions.items():
            btn = QLabel('●' if direction == 'center' else '○')
            btn.setAlignment(Qt.AlignCenter)
            btn.setStyleSheet('padding: 10px; background: #ddd; border-radius: 5px; font-size: 16px;')
            btn.setMinimumSize(40, 40)
            dpad_grid.addWidget(btn, row, col)
            self.dpad_buttons[direction] = btn

        dpad_layout.addLayout(dpad_grid)

        # D-pad value display
        self.dpad_label = QLabel('Position: Center')
        self.dpad_label.setAlignment(Qt.AlignCenter)
        dpad_layout.addWidget(self.dpad_label)

        layout.addWidget(dpad_group)

        # Buttons
        buttons_group = QGroupBox('Buttons')
        buttons_layout = QGridLayout()
        buttons_group.setLayout(buttons_layout)

        self.button_labels = []
        for i in range(16):
            label = QLabel(f'{i+1}')
            label.setStyleSheet('padding: 8px; background: #ddd; border-radius: 3px;')
            label.setAlignment(Qt.AlignCenter)
            label.setMinimumWidth(40)
            buttons_layout.addWidget(label, i // 8, i % 8)
            self.button_labels.append(label)

        layout.addWidget(buttons_group)

        # Output display
        self.device_output = QTextEdit()
        self.device_output.setReadOnly(True)
        self.device_output.setMaximumHeight(100)
        self.device_output.setStyleSheet('font-family: monospace; font-size: 9px;')
        layout.addWidget(QLabel('Raw Events:'))
        layout.addWidget(self.device_output)

        layout.addStretch()
        return widget




    def create_ffb_test_tab(self):
        widget = QWidget()
        layout = QVBoxLayout()
        widget.setLayout(layout)

        info = QLabel('Test various force feedback effects (2-3 seconds each)')
        info.setStyleSheet('color: gray; padding: 5px;')
        layout.addWidget(info)

        effects_group = QGroupBox('Test Effects')
        effects_layout = QGridLayout()
        effects_group.setLayout(effects_layout)

        effects = [
            ('Flat Spot', 'periodic 100 50'),
            ('Engine Start', 'periodic 200 100'),
            ('Flat Tire', 'periodic 150 80'),
            ('Gravel/Rumble', 'periodic 50 120'),
            ('Road Texture', 'periodic 300 60'),
            ('Crash Impact', 'constant 20000'),
            ('Curb Hit', 'constant 12000'),
            ('Understeer', 'spring 8000'),
        ]

        for i, (name, cmd) in enumerate(effects):
            btn = QPushButton(name)
            btn.clicked.connect(lambda checked, c=cmd: self.run_test_effect(c))
            btn.setMinimumHeight(50)
            effects_layout.addWidget(btn, i // 2, i % 2)

        layout.addWidget(effects_group)

        stop_btn = QPushButton('⛔ STOP ALL EFFECTS')
        stop_btn.setStyleSheet('background: #f44336; color: white; padding: 15px; font-size: 14px;')
        stop_btn.clicked.connect(self.stop_all_effects)
        layout.addWidget(stop_btn)

        layout.addStretch()
        return widget


    def create_force_adjustment_tab(self):
        widget = QWidget()
        layout = QVBoxLayout()
        widget.setLayout(layout)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll_widget = QWidget()
        scroll_layout = QVBoxLayout()
        scroll_widget.setLayout(scroll_layout)

        # Overall gain
        overall_group = QGroupBox('Overall Force Feedback Gain')
        overall_layout = QVBoxLayout()
        overall_group.setLayout(overall_layout)

        self.overall_gain_label = QLabel(f'Master Gain: {self.settings["last_gain"]}%')
        self.overall_gain_label.setFont(QFont('Arial', 11, QFont.Bold))
        overall_layout.addWidget(self.overall_gain_label)

        self.overall_gain_slider = QSlider(Qt.Horizontal)
        self.overall_gain_slider.setRange(0, 100)
        self.overall_gain_slider.setValue(self.settings['last_gain'])
        self.overall_gain_slider.setTickPosition(QSlider.TicksBelow)
        self.overall_gain_slider.setTickInterval(10)
        self.overall_gain_slider.valueChanged.connect(self.on_overall_gain_changed)
        overall_layout.addWidget(self.overall_gain_slider)

        scroll_layout.addWidget(overall_group)

        # Autocenter
        ac_group = QGroupBox('Autocenter')
        ac_layout = QVBoxLayout()
        ac_group.setLayout(ac_layout)

        self.ac_label = QLabel(f'Autocenter: {self.settings["last_autocenter"]}%')
        self.ac_label.setFont(QFont('Arial', 11, QFont.Bold))
        ac_layout.addWidget(self.ac_label)

        self.ac_slider = QSlider(Qt.Horizontal)
        self.ac_slider.setRange(0, 100)
        self.ac_slider.setValue(self.settings['last_autocenter'])
        self.ac_slider.setTickPosition(QSlider.TicksBelow)
        self.ac_slider.setTickInterval(10)
        self.ac_slider.valueChanged.connect(self.on_autocenter_changed)
        ac_layout.addWidget(self.ac_slider)

        scroll_layout.addWidget(ac_group)

        # Per-effect gains
        per_effect_group = QGroupBox('Per-Effect-Type Gains')
        per_effect_layout = QVBoxLayout()
        per_effect_group.setLayout(per_effect_layout)

        per_effect_info = QLabel('Adjust gain for each effect type independently')
        per_effect_info.setStyleSheet('color: green; font-size: 9px;')
        per_effect_layout.addWidget(per_effect_info)

        self.effect_gain_sliders = {}
        effect_types = [
            ('constant_gain', 'Constant Forces'),
            ('periodic_gain', 'Periodic Forces'),
            ('spring_gain', 'Spring Forces'),
            ('damper_gain', 'Damper Forces'),
            ('friction_gain', 'Friction Forces'),
            ('inertia_gain', 'Inertia Forces'),
        ]

        for key, name in effect_types:
            hlayout = QHBoxLayout()

            label = QLabel(f'{name}: {self.settings[key]}%')
            label.setMinimumWidth(180)
            hlayout.addWidget(label)

            slider = QSlider(Qt.Horizontal)
            slider.setRange(0, 100)
            slider.setValue(self.settings[key])
            slider.setTickPosition(QSlider.TicksBelow)
            slider.setTickInterval(10)
            slider.valueChanged.connect(lambda v, k=key, l=label, n=name: self.on_effect_gain_changed(k, v, l, n))
            hlayout.addWidget(slider)

            per_effect_layout.addLayout(hlayout)
            self.effect_gain_sliders[key] = slider

        scroll_layout.addWidget(per_effect_group)

        # Rotation angle
        rotation_group = QGroupBox('Rotation Angle')
        rotation_layout = QVBoxLayout()
        rotation_group.setLayout(rotation_layout)

        self.rotation_label = QLabel(f'Steering Range: {self.settings["last_rotation"]}°')
        self.rotation_label.setFont(QFont('Arial', 11, QFont.Bold))
        rotation_layout.addWidget(self.rotation_label)

        # T500RS only supports discrete angles - use slider with snap
        self.rotation_slider = QSlider(Qt.Horizontal)
        self.rotation_slider.setRange(0, 5)  # 6 positions

        # Map slider position to actual angles
        self.rotation_angles = [90, 180, 360, 500, 900, 1080]

        # Set current position
        current_angle = self.settings['last_rotation']
        if current_angle in self.rotation_angles:
            self.rotation_slider.setValue(self.rotation_angles.index(current_angle))
        else:
            self.rotation_slider.setValue(5)  # Default to 1080

        self.rotation_slider.setTickPosition(QSlider.TicksBelow)
        self.rotation_slider.setTickInterval(1)
        self.rotation_slider.valueChanged.connect(self.on_rotation_slider_changed)
        rotation_layout.addWidget(self.rotation_slider)

        rotation_info = QLabel('Supported: 90° | 180° | 360° | 500° | 900° | 1080°')
        rotation_info.setStyleSheet('color: #2196F3; font-size: 9px; font-weight: bold;')
        rotation_info.setAlignment(Qt.AlignCenter)
        rotation_layout.addWidget(rotation_info)

        rotation_note = QLabel('Note: T500RS only supports these 6 discrete angles')
        rotation_note.setStyleSheet('color: gray; font-size: 8px;')
        rotation_note.setAlignment(Qt.AlignCenter)
        rotation_layout.addWidget(rotation_note)

        scroll_layout.addWidget(rotation_group)

        # Reset button
        reset_btn = QPushButton('Reset All to Defaults (100%)')
        reset_btn.clicked.connect(self.reset_to_defaults)
        scroll_layout.addWidget(reset_btn)

        scroll_layout.addStretch()
        scroll.setWidget(scroll_widget)
        layout.addWidget(scroll)

        return widget

    def create_profile_tab(self):
        widget = QWidget()
        layout = QVBoxLayout()
        widget.setLayout(layout)

        self.profile_list = QListWidget()
        self.profile_list.itemDoubleClicked.connect(self.load_profile)
        layout.addWidget(self.profile_list)

        btn_layout = QHBoxLayout()
        for text, func in [('Save', self.save_profile), ('Load', self.load_profile),
                           ('Delete', self.delete_profile), ('Duplicate', self.duplicate_profile)]:
            btn = QPushButton(text)
            btn.clicked.connect(func)
            btn_layout.addWidget(btn)
        layout.addLayout(btn_layout)

        export_layout = QHBoxLayout()
        for text, func in [('Export', self.export_profiles), ('Import', self.import_profiles)]:
            btn = QPushButton(text)
            btn.clicked.connect(func)
            export_layout.addWidget(btn)

        factory_btn = QPushButton('Factory Reset')
        factory_btn.setStyleSheet('background: #ff9800; color: white;')
        factory_btn.clicked.connect(self.factory_reset)
        export_layout.addWidget(factory_btn)

        layout.addLayout(export_layout)
        self.update_profile_list()

        return widget

    # Device methods
    def find_device(self):
        import fcntl
        ffb_device = None
        input_device = None

        # Find both devices
        for path in sorted(glob.glob('/dev/input/event*')):
            try:
                with open(path, 'rb') as f:
                    buf = bytearray(256)
                    fcntl.ioctl(f.fileno(), 0x80ff4506, buf)
                    name = buf.split(b'\x00')[0].decode('utf-8')

                    # FFB device (our uinput device)
                    if 'T500RS Force Feedback Wheel' in name:
                        ffb_device = (path, name)

                    # Input device (kernel HID device)
                    elif 'Thrustmaster' in name and 'Racing wheel' in name:
                        input_device = (path, name)
                    elif 'T500RS' in name or 'T500 RS' in name:
                        if not ffb_device or 'Force Feedback Wheel' not in name:
                            input_device = (path, name)
            except:
                continue

        # Prefer FFB device for both if available, otherwise use separate devices
        if ffb_device:
            path, name = ffb_device
            self.device_path = path
            self.status_label.setText(f'✅ FFB: {path} - {name}')
            self.status_label.setStyleSheet('padding: 10px; background: #d4edda; border-radius: 5px;')

            # Open for writing (FF events)
            if self.device_fd:
                os.close(self.device_fd)
            self.device_fd = os.open(path, os.O_RDWR)

            # Use input device if available, otherwise use same device
            if input_device:
                input_path, input_name = input_device
                print(f"Using separate input device: {input_path} - {input_name}")
                self.status_label.setText(f'✅ FFB: {path} | Input: {input_path}')
            else:
                input_path = path
                print(f"Using same device for input and FFB: {path}")

            # Close old input_fd if exists
            if hasattr(self, 'input_fd') and self.input_fd:
                try:
                    os.close(self.input_fd)
                except:
                    pass

            # Open for reading (input events)
            try:
                self.input_fd = os.open(input_path, os.O_RDONLY | os.O_NONBLOCK)
                print(f"Opened input device: {input_path}")
            except Exception as e:
                print(f"Failed to open input device: {e}")
                self.input_fd = None

            return True

        self.status_label.setText('❌ Device not found! Run: sudo ./run.sh')
        self.status_label.setStyleSheet('padding: 10px; background: #f8d7da; border-radius: 5px;')
        return False
    
    def check_device(self):
        if not self.device_fd:
            self.find_device()

    def update_dpad_display(self):
        """Update D-pad visual display based on current X/Y values"""
        if not hasattr(self, 'dpad_x'):
            self.dpad_x = 0
        if not hasattr(self, 'dpad_y'):
            self.dpad_y = 0

        # Reset all buttons to inactive
        for btn in self.dpad_buttons.values():
            btn.setStyleSheet('padding: 10px; background: #ddd; border-radius: 5px; font-size: 16px;')

        # Determine direction and highlight
        direction_text = []

        if self.dpad_y == -1:  # Up
            self.dpad_buttons['up'].setStyleSheet('padding: 10px; background: #4CAF50; color: white; border-radius: 5px; font-size: 16px;')
            direction_text.append('Up')
        elif self.dpad_y == 1:  # Down
            self.dpad_buttons['down'].setStyleSheet('padding: 10px; background: #4CAF50; color: white; border-radius: 5px; font-size: 16px;')
            direction_text.append('Down')

        if self.dpad_x == -1:  # Left
            self.dpad_buttons['left'].setStyleSheet('padding: 10px; background: #4CAF50; color: white; border-radius: 5px; font-size: 16px;')
            direction_text.append('Left')
        elif self.dpad_x == 1:  # Right
            self.dpad_buttons['right'].setStyleSheet('padding: 10px; background: #4CAF50; color: white; border-radius: 5px; font-size: 16px;')
            direction_text.append('Right')

        if self.dpad_x == 0 and self.dpad_y == 0:  # Center
            self.dpad_buttons['center'].setStyleSheet('padding: 10px; background: #4CAF50; color: white; border-radius: 5px; font-size: 16px;')
            direction_text.append('Center')

        # Update label
        if direction_text:
            self.dpad_label.setText(f'Position: {"-".join(direction_text)}')
        else:
            self.dpad_label.setText('Position: Center')

    def read_device_input(self):
        """Read input events directly from device"""
        if not hasattr(self, 'input_fd') or self.input_fd is None:
            return

        try:
            events_read = 0
            while events_read < 100:  # Max 100 events per cycle
                try:
                    # Read one input_event structure (24 bytes)
                    event_data = os.read(self.input_fd, 24)
                    if len(event_data) < 24:
                        break

                    # Parse: struct input_event { timeval time; __u16 type; __u16 code; __s32 value; }
                    tv_sec, tv_usec, ev_type, ev_code, ev_value = struct.unpack('llHHi', event_data)

                    # Debug: show raw events
                    if ev_type in [1, 3]:  # Only show ABS and KEY events
                        event_str = f'T:{ev_type} C:{ev_code} V:{ev_value}'
                        current = self.device_output.toPlainText()
                        lines = current.split('\n')[-4:]  # Keep last 4 lines
                        lines.append(event_str)
                        self.device_output.setPlainText('\n'.join(lines))

                    # Update UI based on event type
                    if ev_type == 3:  # EV_ABS
                        if ev_code == 0:  # ABS_X - Steering
                            # Value is typically -32768 to 32767
                            normalized = ev_value + 32768  # Convert to 0-65535
                            self.wheel_bar.setValue(normalized)
                            percent = int((normalized * 100) / 65535)
                            self.wheel_label.setText(f'Position: {ev_value} ({percent}%)')

                        elif ev_code == 1:  # ABS_Y - Throttle
                            # Value range depends on device
                            if ev_value > 255:
                                percent = int((ev_value * 100) / 65535)
                            else:
                                percent = int((ev_value * 100) / 255)
                            self.throttle_bar.setValue(percent)
                            self.throttle_label.setText(f'{percent}%')

                        elif ev_code == 2:  # ABS_Z - Brake
                            if ev_value > 255:
                                percent = int((ev_value * 100) / 65535)
                            else:
                                percent = int((ev_value * 100) / 255)
                            self.brake_bar.setValue(percent)
                            self.brake_label.setText(f'{percent}%')

                        elif ev_code == 5:  # ABS_RZ - Clutch
                            if ev_value > 255:
                                percent = int((ev_value * 100) / 65535)
                            else:
                                percent = int((ev_value * 100) / 255)
                            self.clutch_bar.setValue(percent)
                            self.clutch_label.setText(f'{percent}%')

                        elif ev_code == 16:  # ABS_HAT0X - D-pad X
                            if not hasattr(self, 'dpad_x'):
                                self.dpad_x = 0
                            self.dpad_x = ev_value
                            self.update_dpad_display()

                        elif ev_code == 17:  # ABS_HAT0Y - D-pad Y
                            if not hasattr(self, 'dpad_y'):
                                self.dpad_y = 0
                            self.dpad_y = ev_value
                            self.update_dpad_display()

                    elif ev_type == 1:  # EV_KEY - Buttons
                        # Button codes start at 0x120 (288)
                        if ev_code >= 288:
                            btn_num = ev_code - 288
                            if btn_num < len(self.button_labels):
                                if ev_value == 1:  # Pressed
                                    self.button_labels[btn_num].setStyleSheet('padding: 8px; background: #4CAF50; color: white; border-radius: 3px;')
                                else:  # Released
                                    self.button_labels[btn_num].setStyleSheet('padding: 8px; background: #ddd; border-radius: 3px;')

                    events_read += 1

                except BlockingIOError:
                    break
                except Exception as e:
                    print(f"Error reading event: {e}")
                    break

        except Exception as e:
            print(f"Error in read_device_input: {e}")
    

    
    def send_event(self, code, value):
        if not self.device_fd:
            return False
        try:
            event = struct.pack('llHHi', 0, 0, EV_FF, code, value)
            os.write(self.device_fd, event)
            return True
        except:
            return False
    
    # Control handlers
    def on_overall_gain_changed(self, value):
        self.overall_gain_label.setText(f'Master Gain: {value}%')
        self.send_event(FF_GAIN, int((value * 65535) / 100))
        self.settings['last_gain'] = value
        self.save_settings()
        self.statusBar().showMessage(f'Gain: {value}%', 2000)
    
    def on_autocenter_changed(self, value):
        self.ac_label.setText(f'Autocenter: {value}%')
        self.send_event(FF_AUTOCENTER, int((value * 65535) / 100))
        self.settings['last_autocenter'] = value
        self.save_settings()
        self.statusBar().showMessage(f'Autocenter: {value}%', 2000)
    
    def on_effect_gain_changed(self, key, value, label, name):
        label.setText(f'{name}: {value}%')
        self.settings[key] = value
        self.save_settings()

        # Send to driver
        gain_value = int((value * 65535) / 100)
        code_map = {
            'constant_gain': FF_GAIN_CONSTANT,
            'periodic_gain': FF_GAIN_PERIODIC,
            'spring_gain': FF_GAIN_SPRING,
            'damper_gain': FF_GAIN_DAMPER,
            'friction_gain': FF_GAIN_FRICTION,
            'inertia_gain': FF_GAIN_INERTIA,
        }

        if key in code_map:
            self.send_event(code_map[key], gain_value)
            self.statusBar().showMessage(f'{name}: {value}%', 2000)
    
    def on_rotation_slider_changed(self, index):
        # Map slider position to actual angle
        if 0 <= index < len(self.rotation_angles):
            angle = self.rotation_angles[index]
            self.rotation_label.setText(f'Steering Range: {angle}°')
            self.settings['last_rotation'] = angle
            self.save_settings()

            # Send to driver
            self.send_event(FF_ROTATION_ANGLE, angle)
            self.statusBar().showMessage(f'Rotation: {angle}°', 2000)
    
    def reset_to_defaults(self):
        self.overall_gain_slider.setValue(100)
        self.ac_slider.setValue(0)
        for slider in self.effect_gain_sliders.values():
            slider.setValue(100)
        self.statusBar().showMessage('Reset to defaults', 2000)
    
    # FFB test methods
    def run_test_effect(self, cmd):
        parts = cmd.split()
        effect_type = parts[0]
        
        if effect_type == 'constant':
            level = int(parts[1]) if len(parts) > 1 else 16384
            subprocess.Popen(f'cd /home/caz/Documents/hid-tmff2/userspace && echo "1" | sudo ./test_all_effects', shell=True)
        elif effect_type == 'periodic':
            subprocess.Popen(f'cd /home/caz/Documents/hid-tmff2/userspace && echo "11" | sudo ./test_all_effects', shell=True)
        elif effect_type == 'spring':
            subprocess.Popen(f'cd /home/caz/Documents/hid-tmff2/userspace && echo "6" | sudo ./test_all_effects', shell=True)
        
        self.statusBar().showMessage(f'Testing {effect_type} effect...', 3000)
    
    def stop_all_effects(self):
        subprocess.Popen('cd /home/caz/Documents/hid-tmff2/userspace && sudo ./emergency_reset.sh', shell=True)
        self.statusBar().showMessage('All effects stopped', 3000)
    
    # Profile methods
    def load_profiles(self):
        if PROFILES_FILE.exists():
            try:
                with open(PROFILES_FILE) as f:
                    self.profiles = json.load(f)
            except:
                self.profiles = {}
    
    def save_profiles(self):
        with open(PROFILES_FILE, 'w') as f:
            json.dump(self.profiles, f, indent=2)
    
    def load_settings(self):
        if SETTINGS_FILE.exists():
            try:
                with open(SETTINGS_FILE) as f:
                    self.settings.update(json.load(f))
            except:
                pass
    
    def save_settings(self):
        with open(SETTINGS_FILE, 'w') as f:
            json.dump(self.settings, f, indent=2)
    
    def update_profile_list(self):
        self.profile_list.clear()
        for name in sorted(self.profiles.keys()):
            profile = self.profiles[name]
            created = profile.get('created', 'Unknown')
            self.profile_list.addItem(f'{name} ({created})')
    
    def save_profile(self):
        name, ok = QInputDialog.getText(self, 'Save Profile', 'Profile name:')
        if ok and name:
            self.profiles[name] = {
                'gain': self.overall_gain_slider.value(),
                'autocenter': self.ac_slider.value(),
                'rotation': self.settings['last_rotation'],
                'constant_gain': self.settings['constant_gain'],
                'periodic_gain': self.settings['periodic_gain'],
                'spring_gain': self.settings['spring_gain'],
                'damper_gain': self.settings['damper_gain'],
                'friction_gain': self.settings['friction_gain'],
                'inertia_gain': self.settings['inertia_gain'],
                'combined_pedals': self.combined_pedals_check.isChecked(),
                'created': datetime.now().strftime('%Y-%m-%d %H:%M')
            }
            self.save_profiles()
            self.update_profile_list()
            self.statusBar().showMessage(f'Saved: {name}', 3000)
    
    def load_profile(self):
        item = self.profile_list.currentItem()
        if item:
            name = item.text().split(' (')[0]
            profile = self.profiles.get(name)
            if profile:
                self.overall_gain_slider.setValue(profile['gain'])
                self.ac_slider.setValue(profile.get('autocenter', 0))

                # Set rotation (map to slider position)
                rotation = profile.get('rotation', 1080)
                if rotation in self.rotation_angles:
                    self.rotation_slider.setValue(self.rotation_angles.index(rotation))
                else:
                    self.rotation_slider.setValue(5)  # Default to 1080

                # Set per-effect gains
                for key in ['constant_gain', 'periodic_gain', 'spring_gain', 'damper_gain', 'friction_gain', 'inertia_gain']:
                    if key in profile and key in self.effect_gain_sliders:
                        self.effect_gain_sliders[key].setValue(profile[key])

                self.combined_pedals_check.setChecked(profile.get('combined_pedals', False))
                self.statusBar().showMessage(f'Loaded: {name}', 3000)
    
    def delete_profile(self):
        item = self.profile_list.currentItem()
        if item:
            name = item.text().split(' (')[0]
            reply = QMessageBox.question(self, 'Delete', f'Delete "{name}"?')
            if reply == QMessageBox.Yes:
                del self.profiles[name]
                self.save_profiles()
                self.update_profile_list()
    
    def duplicate_profile(self):
        item = self.profile_list.currentItem()
        if item:
            name = item.text().split(' (')[0]
            new_name, ok = QInputDialog.getText(self, 'Duplicate', 'New name:', text=f'{name} (copy)')
            if ok and new_name:
                self.profiles[new_name] = self.profiles[name].copy()
                self.profiles[new_name]['created'] = datetime.now().strftime('%Y-%m-%d %H:%M')
                self.save_profiles()
                self.update_profile_list()
    
    def export_profiles(self):
        filename, _ = QFileDialog.getSaveFileName(self, 'Export', 'profiles.json', 'JSON (*.json)')
        if filename:
            with open(filename, 'w') as f:
                json.dump(self.profiles, f, indent=2)
            QMessageBox.information(self, 'Export', f'Exported to {filename}')
    
    def import_profiles(self):
        filename, _ = QFileDialog.getOpenFileName(self, 'Import', '', 'JSON (*.json)')
        if filename:
            with open(filename) as f:
                imported = json.load(f)
            self.profiles.update(imported)
            self.save_profiles()
            self.update_profile_list()
            QMessageBox.information(self, 'Import', f'Imported {len(imported)} profiles')
    
    def factory_reset(self):
        reply = QMessageBox.warning(self, 'Factory Reset',
                                    'Delete all profiles and reset settings?\nContinue?',
                                    QMessageBox.Yes | QMessageBox.No)
        if reply == QMessageBox.Yes:
            self.profiles = {}
            self.save_profiles()
            self.reset_to_defaults()
            self.update_profile_list()
            QMessageBox.information(self, 'Reset', 'Factory reset complete')
    
    def closeEvent(self, event):
        if self.input_fd:
            try:
                os.close(self.input_fd)
            except:
                pass
        if self.device_fd:
            os.close(self.device_fd)
        event.accept()

def main():
    if os.geteuid() != 0:
        print("Run with: sudo python3 t500rs_control.py")
        sys.exit(1)
    
    app = QApplication(sys.argv)
    window = T500RSControl()
    window.show()
    sys.exit(app.exec_())

if __name__ == '__main__':
    main()
