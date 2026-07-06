//! Driver-agnostic live gain settings via sysfs.

use std::collections::HashSet;
use std::fs;
use std::path::{Path, PathBuf};

pub const KNOWN_GAIN_ATTRS: &[&str] = &[
    "spring_level", "damper_level", "friction_level", "gain",
];

#[derive(Debug, Clone, serde::Serialize)]
pub struct GainInfo {
    pub key:      String,
    pub name:     String,
    pub value:    u32,
    pub min:      u32,
    pub max:      u32,
    pub unit:     String,
    pub path:     String,
    pub writable: bool,
}

fn attr_meta(name: &str) -> (&str, &str, u32, u32, &str) {
    match name {
        "spring_level"   => ("spring_level", "Spring force level",   0, 100, "%"),
        "damper_level"   => ("damper_level", "Damper force level",   0, 100, "%"),
        "friction_level" => ("friction_level", "Friction force level",0, 100, "%"),
        "gain"           => ("gain", "Master gain", 0, 65535, ""),
        _                => (name, name, 0, 65535, ""),
    }
}

fn event_syspath(path: &str) -> Option<PathBuf> {
    Path::new(path).file_name().and_then(|n| {
        let s = Path::new("/sys/class/input").join(n);
        if s.is_dir() { Some(s) } else { None }
    })
}

fn read_attr(path: &Path) -> Option<u32> {
    fs::read_to_string(path)
        .ok()
        .and_then(|s| s.trim().parse().ok())
}

fn is_writable(path: &Path) -> bool {
    path.metadata()
        .map(|m| {
            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;
                let mode = m.permissions().mode();
                (mode & 0o222) != 0
            }
            #[cfg(not(unix))]
            {
                let _ = m;
                false
            }
        })
        .unwrap_or(false)
}

/// Probe the device at `event_path` for known gain sysfs attributes.
pub fn probe(event_path: &str) -> Vec<GainInfo> {
    let sysp = match event_syspath(event_path) {
        Some(p) => p,
        None => return vec![],
    };

    let mut seen = HashSet::new();
    let mut results = Vec::new();

    for attr in KNOWN_GAIN_ATTRS.iter().copied() {
        if !seen.insert(attr) { continue; }
        let path = find_attr(&sysp, attr);
        let val  = path.as_ref().and_then(read_attr);
        let writ = path.as_ref().map(is_writable).unwrap_or(false);
        if let (Some(p), Some(v)) = (path, val) {
            let (key, name, mn, mx, unit) = attr_meta(attr);
            results.push(GainInfo {
                key:      key.into(),
                name:     name.into(),
                value:    v,
                min:      mn,
                max:      mx,
                unit:     unit.into(),
                path:     p.to_string_lossy().into(),
                writable: writ,
            });
        }
    }
    results
}

fn find_attr(sysp: &Path, attr: &str) -> Option<PathBuf> {
    // Direct location
    let direct = sysp.join(attr);
    if direct.is_file() { return Some(direct); }

    // Under device/
    let dev = sysp.join("device").join(attr);
    if dev.is_file() { return Some(dev); }

    // Recursively search up to 3 levels in the tree as a last resort
    let mut candidates: Vec<PathBuf> = Vec::new();
    let mut root = sysp.to_path_buf();
    for _ in 0..4 {
        candidates.push(root.join(attr));
        if !root.pop() { break; }
    }
    candidates.into_iter().find(|c| c.is_file()).and_then(|p| {
        let _ = fs::read(&p); // ensure readable
        Some(p)
    })
}

/// Write `value` to the `key` gain attribute. Returns true on success.
pub fn set_gain(event_path: &str, key: &str, value: u32) -> bool {
    let sysp = match event_syspath(event_path) { Some(p) => p, None => return false };
    let path = match find_attr(&sysp, key) { Some(p) => p, None => return false };

    // Clamp to known range
    let (_kn, _name, mn, mx, _unit) = KNOWN_GAIN_ATTRS
        .iter()
        .find(|k| **k == key)
        .map(|k| attr_meta(k))
        .unwrap_or((key, key, 0, 65535, ""));
    let clamped = value.clamp(mn, mx);

    fs::write(&path, clamped.to_string()).is_ok()
}
