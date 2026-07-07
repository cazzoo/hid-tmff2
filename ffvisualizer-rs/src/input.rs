//! Linux evdev device discovery: enumerate FFB-capable input devices.

use std::collections::HashSet;
use std::os::unix::io::RawFd;

use evdev::{AbsoluteAxis, Device, Types};

use crate::types::EFFECT_TYPES;

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
///
/// Wrapped in `catch_unwind` because evdev 0.10 panics ("unexpected abs
/// bits!") when a device reports absolute-axis codes its bitflag type doesn't
/// know about (e.g. the T500RS). A panicking device is skipped rather than
/// crashing the HTTP worker that calls this from /api/devices.
pub fn list_ffb_devices() -> Vec<FfbDevice> {
    let mut paths: Vec<String> = Vec::new();
    if let Ok(entries) = std::fs::read_dir("/dev/input") {
        for e in entries.flatten() {
            if let Some(name) = e.file_name().to_str() {
                if name.starts_with("event") {
                    paths.push(format!("/dev/input/{name}"));
                }
            }
        }
    }
    paths.sort();

    let mut out = Vec::new();
    for path in paths {
        // Temporarily silence the panic hook: evdev 0.10 panics ("unexpected
        // abs bits!") on devices whose abs codes exceed its bitflag, and we'd
        // otherwise spam stderr per call. The panic is caught below.
        let prev_hook = std::panic::take_hook();
        std::panic::set_hook(Box::new(|_| {}));
        let probe = std::panic::catch_unwind(|| {
            match Device::open(&path) {
                Ok(dev) if ffb_capable(&dev) => Some(device_info(&path, &dev)),
                _ => None,
            }
        });
        std::panic::set_hook(prev_hook);
        match probe {
            Ok(Some(info)) => out.push(info),
            Ok(None) | Err(_) => continue,
        }
    }
    out
}

/// Open a raw device fd for the given event node path (O_RDWR, non-blocking).
pub fn open_fd(path: &str) -> std::io::Result<RawFd> {
    let c_path = std::ffi::CString::new(path)
        .map_err(|_| std::io::Error::new(std::io::ErrorKind::InvalidInput, "nul in path"))?;
    let fd = unsafe {
        libc::open(
            c_path.as_ptr(),
            libc::O_RDWR | libc::O_NONBLOCK,
        )
    };
    if fd < 0 {
        Err(std::io::Error::last_os_error())
    } else {
        Ok(fd)
    }
}

fn ffb_capable(dev: &Device) -> bool {
    // Types::FORCEFEEDBACK bit = 1 << 0x15 (0x15 = EV_FF)
    dev.events_supported()
        .contains(Types::from_bits_truncate(1u32 << 0x15))
}

fn device_info(path: &str, dev: &Device) -> FfbDevice {
    let name = dev.name().to_string_lossy().into_owned();
    let phys = dev
        .physical_path()
        .as_ref()
        .map(|c| c.to_string_lossy().into_owned())
        .unwrap_or_default();

    let mut abs_min = 0;
    let mut abs_max = 65535;
    let mut ff_axes: Vec<u16> = Vec::new();

    let axes = dev.absolute_axes_supported();
    for code in [0x00u16, 0x03, 0x08] {
        // ABS_X, ABS_RX, ABS_WHEEL
        if axes.contains(AbsoluteAxis::from_bits_truncate(1u64 << code)) {
            ff_axes.push(code);
        }
    }
    if ff_axes.is_empty() {
        // fall back to the first supported abs axis
        for code in 0..0x3f {
            if axes.contains(AbsoluteAxis::from_bits_truncate(1u64 << code)) {
                ff_axes.push(code);
                break;
            }
        }
    }

    // Try to read absinfo for the preferred axis to seed min/max.
    let fd = dev.fd();
    if let Some(&preferred) = ff_axes.first() {
        if let Some(ai) = abs_info(fd, preferred) {
            abs_min = ai.0;
            abs_max = ai.1;
        }
    }

    let mut ff_effects: HashSet<u16> = HashSet::new();
    for code in supported_ff_effect_codes(fd) {
        if EFFECT_TYPES.contains(&code) {
            ff_effects.insert(code);
        }
    }

    FfbDevice {
        path: path.to_string(),
        name,
        phys,
        abs_min,
        abs_max,
        ff_axes,
        ff_effects,
    }
}

/// Query EVIOCGABS for an axis; returns (min, max).
fn abs_info(fd: RawFd, axis: u16) -> Option<(i32, i32)> {
    let mut ai: libc::input_absinfo = unsafe { std::mem::zeroed() };
    let req = eviocgabs(axis);
    let r = unsafe { libc::ioctl(fd, req as libc::c_ulong, &mut ai as *mut _ as *mut libc::c_void) };
    if r < 0 {
        return None;
    }
    Some((ai.minimum, ai.maximum))
}

/// EVIOCGABS(ax): _IOWR('E', 0x40 + (ax), sizeof(input_absinfo))
const fn eviocgabs(axis: u16) -> u32 {
    let nr = 0x40u32 + axis as u32;
    _iowr(b'E' as u32, nr, std::mem::size_of::<libc::input_absinfo>() as u32)
}

const fn _ioc(dir: u32, ty: u32, nr: u32, size: u32) -> u32 {
    (dir << 30) | (ty << 8) | (nr << 16) | (size << 16)
}
const fn _iowr(ty: u32, nr: u32, size: u32) -> u32 {
    _ioc(3, ty, nr, size)
}

/// Query EVIOCGBIT(EV_FF, ...) for the supported FF effect-type codes.
pub fn supported_ff_effect_codes(fd: RawFd) -> Vec<u16> {
    let mut buf = [0u8; 32];
    // EVIOCGBIT(EV_FF=0x15, n) == _IOW('E', 0x20 + 0x15, n)
    let req = _ioc(2, b'E' as u32, 0x20 + 0x15, buf.len() as u32);
    let r = unsafe {
        libc::ioctl(
            fd,
            req as libc::c_ulong,
            buf.as_mut_ptr() as *mut libc::c_void,
        )
    };
    if r < 0 {
        return Vec::new();
    }
    let nbits = r as usize * 8;
    let mut out = Vec::new();
    for i in 0..nbits {
        if i / 8 < buf.len() && buf[i / 8] & (1u8 << (i % 8)) != 0 {
            out.push(i as u16);
        }
    }
    out
}

impl FfbDevice {
    /// Return the best axis code for a steering/wheel axis.
    pub fn ff_axis(&self) -> u16 {
        self.ff_axes.iter().copied().next().unwrap_or(0x00)
    }

    /// Return abs range for axis detection.
    pub fn abs_range(&self) -> (i32, i32) {
        (self.abs_min, self.abs_max)
    }
}
