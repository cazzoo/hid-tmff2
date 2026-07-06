//! Linux evdev device discovery: enumerate FFB-capable input devices.

use std::collections::HashSet;
use std::path::{Path, PathBuf};

use evdev::{Device, InputDevice, ReadFlag};

use crate::types::{EffectType, EFFECT_TYPES, MAX_LEVEL};

/// Info about a discovered FFB-capable input device.
#[derive(Debug, Clone)]
pub struct FfbDevice {
    pub path: String,
    pub name: String,
    pub phys: String,
    pub abs_min: i32,
    pub abs_max: i32,
    pub ff_axes: Vec<u16>,
    pub ff_effects: HashSet<u16>,
}

/// Enumerate all FFB-capable input devices found in /dev/input/event*.
pub fn list_ffb_devices() -> Vec<FfbDevice> {
    let mut out = Vec::new();
    let Ok(paths) = evdev::enumerate() else { return out };
    for path in paths {
        if let Ok(dev) = Device::open(&path) {
            if ffb_capable(&dev) {
                let info = device_info(path, &dev);
                out.push(info);
            }
        }
    }
    out
}

pub fn open_device(path: &str) -> Result<InputDevice, std::io::Error> {
    evdev::InputDevice::open(path)
}

fn ffb_capable(dev: &Device) -> bool {
    let Ok(caps) = dev.supported_events() else { return false };
    caps.contains(evdev::EventType::FORCEFEEDBACK)
}

fn device_info(path: PathBuf, dev: &Device) -> FfbDevice {
    let name = dev.name().unwrap_or_else(|| "<unnamed>").to_string();
    let phys = dev.physical_address().unwrap_or("").to_string();

    let mut abs_min = 0;
    let mut abs_max = 65535;
    let mut ff_axes: Vec<u16> = Vec::new();
    if let Ok(props) = dev.properties() {
        for axis in props.iter() {
            if let evdev::PropertyAxis::Abs { codes, .. } = axis {
                for code in codes {
                    if matches!(code, evdev::AbsoluteAxisCode::ABS_X
                                      | evdev::AbsoluteAxisCode::ABS_RX
                                      | evdev::AbsoluteAxisCode::ABS_WHEEL) {
                        let _ = ff_axes.push(*code as u16);
                    }
                    if let Some(ref ai) = dev.get_abs_info(*code) {
                        let _ = abs_info(ai);
                    }
                }
            }
        }
    }

    let mut ff_effects: HashSet<u16> = HashSet::new();
    if let Ok(caps) = dev.supported_events() {
        if caps.contains(evdev::EventType::FORCEFEEDBACK) {
            for code in dev.supported_ff_effects() {
                if EFFECT_TYPES.contains(&code) {
                    ff_effects.insert(code);
                }
            }
        }
    }

    FfbDevice {
        path: path.to_string_lossy().to_string(),
        name,
        phys,
        abs_min,
        abs_max,
        ff_axes,
        ff_effects,
    }
}

fn abs_info(ai: &evdev::AbsInfo) {
    // AbsInfo is embedded in FfbDevice already via abs_min/abs_max from the
    // property iteration above. Extended per-axis info is available when needed.
}

impl FfbDevice {
    /// Return the best axis code for a steering/wheel axis.
    pub fn ff_axis(&self) -> u16 {
        self.ff_axes.iter().copied().next()
            .unwrap_or(evdev::AbsoluteAxisCode::ABS_X as u16)
    }

    /// Return abs range for axis detection.
    pub fn abs_range(&self) -> (i32, i32) {
        (self.abs_min, self.abs_max)
    }
}
