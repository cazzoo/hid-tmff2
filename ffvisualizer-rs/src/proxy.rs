//! FFB passthrough proxy + read-only observer.
//!
//! Modes:
//!   observer – open real device read-only, show position + EV_FF play/stop/gain.
//!              Cannot inspect effect parameters (they are invisible read-only).
//!   proxy     – create a virtual uinput device mirroring the real one, intercept
//!               all FF ioctl requests via the uinput control fd, forward
//!               everything both ways, and feed the force model.
//!               Requires the game to point at the virtual device event node.
//!
//! Runs on a dedicated blocking thread; sample() is async-safe.

use std::collections::{HashMap, HashSet};
use std::os::unix::io::RawFd;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

use evdev::Device;
use tracing::{debug, error, info, warn};

use crate::input::supported_ff_effect_codes;
use crate::model::ForceModel;
use crate::protocol::{
    begin_ff_erase, begin_ff_upload, end_ff_erase, end_ff_upload, erase_effect,
    poll_fds, upload_effect, effect_to_params as proto_effect_to_params, PollFd, RawFdStream,
    EV_UINPUT, UI_DEV_CREATE, UI_DEV_SETUP, UI_SET_ABSBIT, UI_SET_EVBIT, UI_SET_FFBIT,
    UI_SET_KEYBIT, UinputSetup, UINPUT_MAX_NAME_SIZE,
};
use crate::types::{
    FF_AUTOCENTER as FF_AUTOCENTER_CODE, FF_GAIN as FF_GAIN_CODE,
};

// Linux input.h event types
const EV_SYN: u16 = 0x00;
const EV_KEY: u16 = 0x01;
const EV_ABS: u16 = 0x03;
const EV_FF: u16 = 0x15;

// uinput FF command codes for EV_UINPUT
const UI_FF_UPLOAD: u16 = 1;
const UI_FF_ERASE: u16 = 2;

// ===========================================================================
// Axis helpers
// ===========================================================================

struct AxisInfo {
    code: u16,
    min: i32,
    max: i32,
}

/// EVIOCGABS(ax): _IOWR('E', 0x40 + (ax), sizeof(input_absinfo))
const fn eviocgabs(axis: u16) -> u32 {
    let nr = 0x40u32 + axis as u32;
    // _IOWR = dir=3, then (dir<<30)|(type<<8)|(nr<<16)|(size<<16)
    (3u32 << 30)
        | ((b'E' as u32) << 8)
        | (nr << 16)
        | (std::mem::size_of::<libc::input_absinfo>() as u32) << 16
}

fn abs_info(fd: RawFd, axis: u16) -> Option<AxisInfo> {
    let mut ai: libc::input_absinfo = unsafe { std::mem::zeroed() };
    let r =
        unsafe { libc::ioctl(fd, eviocgabs(axis) as libc::c_ulong, &mut ai as *mut _ as *mut libc::c_void) };
    if r < 0 {
        return None;
    }
    Some(AxisInfo {
        code: axis,
        min: ai.minimum,
        max: ai.maximum,
    })
}

fn detect_axis(fd: RawFd, dev: &Device) -> AxisInfo {
    for candidate in [0x03u16, 0x00, 0x08] {
        // ABS_RX, ABS_X, ABS_WHEEL
        if let Some(ai) = abs_info(fd, candidate) {
            return ai;
        }
    }
    let axes = dev.absolute_axes_supported();
    for code in 0..0x3fu16 {
        if axes.contains(evdev::AbsoluteAxis::from_bits_truncate(1u64 << code)) {
            if let Some(ai) = abs_info(fd, code) {
                return ai;
            }
        }
    }
    AxisInfo {
        code: 0x00,
        min: 0,
        max: 65535,
    }
}

fn norm_axis(value: i32, min: i32, max: i32) -> f32 {
    let span = (max - min).max(1) as f32;
    (((value - min) as f32) / span * 2.0 - 1.0).clamp(-1.0, 1.0)
}

/// Write one raw input_event (libc layout, field `type_`) to an fd.
fn write_input_event(fd: RawFd, type_: u16, code: u16, value: i32) -> std::io::Result<()> {
    let ev = libc::input_event {
        time: libc::timeval {
            tv_sec: 0,
            tv_usec: 0,
        },
        type_,
        code,
        value,
    };
    let buf = unsafe {
        std::slice::from_raw_parts(
            &ev as *const _ as *const u8,
            std::mem::size_of::<libc::input_event>(),
        )
    };
    let n = unsafe { libc::write(fd, buf.as_ptr() as *const libc::c_void, buf.len() as _) };
    if n < 0 {
        Err(std::io::Error::last_os_error())
    } else {
        Ok(())
    }
}

// ===========================================================================
// UInput builder via raw ioctls (evdev 0.10 has no UInput wrapper)
// ===========================================================================

/// Run one uinput setup step, logging which ioctl failed on error.
fn set_step<F>(fd: RawFd, name: &str, f: F) -> std::io::Result<()>
where
    F: FnOnce() -> std::io::Result<()>,
{
    let _ = fd;
    match f() {
        Ok(_) => Ok(()),
        Err(e) => {
            warn!(step = name, errno = e.raw_os_error(), error = %e, "uinput setup step failed");
            Err(e)
        }
    }
}

#[derive(Debug, Default)]
struct UinputBuilder {
    name: String,
    ff_bits: HashSet<u16>,
    ev_bits: HashSet<u16>,
    abs_bits: HashSet<u16>,
    key_bits: HashSet<u16>,
}

impl UinputBuilder {
    fn new(name: impl Into<String>) -> Self {
        Self {
            name: name.into(),
            ..Self::default()
        }
    }
    fn set_ff(mut self, b: impl IntoIterator<Item = u16>) -> Self {
        self.ff_bits.extend(b);
        self
    }
    fn set_ev(mut self, b: impl IntoIterator<Item = u16>) -> Self {
        self.ev_bits.extend(b);
        self
    }
    fn set_abs(mut self, b: impl IntoIterator<Item = u16>) -> Self {
        self.abs_bits.extend(b);
        self
    }
    fn set_key(mut self, b: impl IntoIterator<Item = u16>) -> Self {
        self.key_bits.extend(b);
        self
    }

    /// Build the virtual device. Returns (uinput_control_fd, /dev/input/eventN).
    fn build(self) -> std::io::Result<(RawFd, String)> {
        let c_path = b"/dev/uinput\0";
        let fd = unsafe {
            libc::open(
                c_path.as_ptr() as *const libc::c_char,
                libc::O_RDWR | libc::O_NONBLOCK,
            )
        };
        if fd < 0 {
            let e = std::io::Error::last_os_error();
            if e.raw_os_error() == Some(libc::ENOENT) {
                return Err(std::io::Error::new(
                    std::io::ErrorKind::NotFound,
                    "/dev/uinput not found — `modprobe uinput` and retry",
                ));
            }
            return Err(e);
        }

        let setup_ok = (|| -> std::io::Result<()> {
            // Capability bits MUST be set BEFORE UI_DEV_SETUP: UI_DEV_SETUP
            // (and the legacy uinput_user_dev write) transitions the fd out of
            // the UINPUT_NEW state, after which UI_SET_*BIT returns EINVAL.
            // EV_SYN (0) must NOT be set via UI_SET_EVBIT (kernel rejects it).
            for b in &self.ev_bits {
                if *b == EV_SYN {
                    continue;
                }
                set_step(fd, &format!("UI_SET_EVBIT({b})"), || {
                    crate::protocol::ui_set_bit(fd, UI_SET_EVBIT, *b as i32)
                })?;
            }
            for b in &self.abs_bits {
                set_step(fd, &format!("UI_SET_ABSBIT({b})"), || {
                    crate::protocol::ui_set_bit(fd, UI_SET_ABSBIT, *b as i32)
                })?;
            }
            for b in &self.key_bits {
                set_step(fd, &format!("UI_SET_KEYBIT({b})"), || {
                    crate::protocol::ui_set_bit(fd, UI_SET_KEYBIT, *b as i32)
                })?;
            }
            for b in &self.ff_bits {
                set_step(fd, &format!("UI_SET_FFBIT({b})"), || {
                    crate::protocol::ui_set_bit(fd, UI_SET_FFBIT, *b as i32)
                })?;
            }

            // UI_DEV_SETUP with name + id + max effects (after the SET calls).
            let mut setup = UinputSetup {
                id: libc::input_id {
                    bustype: 0x03,
                    vendor: 0x0001,
                    product: 0x0001,
                    version: 0x0001,
                },
                name: [0u8; UINPUT_MAX_NAME_SIZE],
                ff_effects_max: 64,
            };
            let name_bytes = self.name.as_bytes();
            let n = name_bytes.len().min(UINPUT_MAX_NAME_SIZE - 1);
            setup.name[..n].copy_from_slice(&name_bytes[..n]);
            set_step(fd, "UI_DEV_SETUP", || {
                crate::protocol::raw_ioctl_ptr(
                    fd,
                    UI_DEV_SETUP,
                    &mut setup as *mut UinputSetup as *mut libc::c_void,
                )
                .map(|_| ())
            })?;
            Ok(())
        })();

        if let Err(e) = setup_ok {
            unsafe {
                libc::close(fd);
            }
            return Err(e);
        }

        // UI_DEV_CREATE
        if let Err(e) = crate::protocol::raw_ioctl(fd, UI_DEV_CREATE) {
            unsafe {
                libc::close(fd);
            }
            return Err(e);
        }

        // Wait for the kernel to create the event node
        unsafe {
            libc::usleep(200_000);
        } // 200 ms

        let ev_name = find_virtual_event_name(&self.name).ok_or_else(|| {
            std::io::Error::new(
                std::io::ErrorKind::NotFound,
                format!(
                    "virtual event node for '{}' not found — run: cat /proc/bus/input/devices",
                    self.name
                ),
            )
        })?;

        Ok((fd, ev_name))
    }
}

fn find_virtual_event_name(dev_name: &str) -> Option<String> {
    let sysp = std::path::Path::new("/sys/class/input");
    let mut entries: Vec<_> = std::fs::read_dir(sysp).ok()?.filter_map(|e| e.ok()).collect();
    // Sort by mtime descending (newest first — our device was just created)
    entries.sort_by(|a, b| {
        let at = a
            .metadata()
            .and_then(|m| m.modified())
            .unwrap_or(std::time::UNIX_EPOCH);
        let bt = b
            .metadata()
            .and_then(|m| m.modified())
            .unwrap_or(std::time::UNIX_EPOCH);
        bt.cmp(&at)
    });
    for entry in entries {
        let file_name = entry.file_name();
        let fname = match file_name.to_str() {
            Some(s) => s,
            None => continue,
        };
        // Only accept event device nodes (eventN), not jsN/mice symlinks that
        // point at the same input device.
        if !fname.starts_with("event") {
            continue;
        }
        let nf = entry.path().join("device").join("name");
        if let Ok(name) = std::fs::read_to_string(nf) {
            if name.trim() == dev_name {
                return Some(fname.to_string());
            }
        }
    }
    None
}

// ===========================================================================
// Proxy state and backend
// ===========================================================================

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum ProxyMode {
    #[default]
    Observe,
    Proxy,
}

impl std::fmt::Display for ProxyMode {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ProxyMode::Observe => write!(f, "observe"),
            ProxyMode::Proxy => write!(f, "proxy"),
        }
    }
}

#[derive(Debug, Default)]
pub struct ProxyState {
    pub mode: ProxyMode,
    pub virtual_path: Option<String>,
    pub real_path: String,
}

struct ProxyInner {
    state: Arc<Mutex<ProxyState>>,
    model: Arc<Mutex<ForceModel>>,
    real_fd: Mutex<RawFd>,
}

pub struct ProxyBackend {
    inner: Arc<ProxyInner>,
}

impl ProxyBackend {
    pub fn new(real_path: String, observe: bool) -> Self {
        let state = Arc::new(Mutex::new(ProxyState {
            mode: if observe {
                ProxyMode::Observe
            } else {
                ProxyMode::Proxy
            },
            virtual_path: None,
            real_path,
        }));
        let model = Arc::new(Mutex::new(ForceModel::new()));
        // The real_fd must be set by run_proxy()/run_observer() before use.
        Self {
            inner: Arc::new(ProxyInner {
                state,
                model,
                real_fd: Mutex::new(-1),
            }),
        }
    }

    /// Start a blocking thread. Returns immediately.
    pub fn start(self: Arc<Self>) {
        let observe = {
            let s = self.inner.state.lock().unwrap();
            matches!(s.mode, ProxyMode::Observe)
        };
        thread::spawn(move || {
            if observe {
                self.run_observer();
            } else {
                self.run_proxy();
            }
        });
    }

    pub fn stop(&self) {
        // Graceful stop via a flag would go here; currently not observable
        // until the process exits.
    }

    pub fn state_arc(&self) -> Arc<Mutex<ProxyState>> {
        Arc::clone(&self.inner.state)
    }

    #[allow(dead_code)]
    pub fn model_arc(&self) -> Arc<Mutex<ForceModel>> {
        Arc::clone(&self.inner.model)
    }

    /// Gather a snapshot of the force-model state (async-safe, acquires a lock).
    pub fn sample(&self) -> crate::types::SampleSnapshot {
        self.inner.model.lock().unwrap().sample()
    }
}

// ===========================================================================
// Observer run path
// ===========================================================================

impl ProxyBackend {
    fn run_observer(self: Arc<Self>) {
        let path = {
            self.inner.state.lock().unwrap().real_path.clone()
        };
        info!(path = %path, "observer: opening device read-only");
        let dev = match Device::open(&path) {
            Ok(d) => d,
            Err(e) => {
                error!(path = %path, error = ?e, "observer: cannot open device");
                return;
            }
        };
        info!(dev_name = %dev.name().to_string_lossy(), "observer: opened");
        let fd = dev.fd();
        *self.inner.real_fd.lock().unwrap() = fd;

        let AxisInfo {
            code: ff_axis,
            min,
            max,
        } = detect_axis(fd, &dev);
        let mut dev = dev;

        loop {
            match dev.events() {
                Ok(events) => {
                    for ev in events {
                        let t = ev._type;
                        if t == EV_ABS && ev.code == ff_axis {
                            let norm = norm_axis(ev.value, min, max);
                            self.inner.model.lock().unwrap().update_position(norm);
                        }
                        if t == EV_FF {
                            self.handle_ev_ff(ev.code, ev.value as u16);
                        }
                    }
                }
                Err(e) => {
                    warn!(?e, "observer read error");
                    thread::sleep(Duration::from_millis(50));
                }
            }
        }
    }
}

// ===========================================================================
// Proxy run path
// ===========================================================================

impl ProxyBackend {
    fn run_proxy(self: Arc<Self>) {
        let path = {
            self.inner.state.lock().unwrap().real_path.clone()
        };
        info!(path = %path, "proxy: opening real device");

        let real_dev = match Device::open(&path) {
            Ok(d) => d,
            Err(e) => {
                error!(path = %path, error = ?e, "proxy: cannot open real device");
                return;
            }
        };
        let real_fd = real_dev.fd();
        *self.inner.real_fd.lock().unwrap() = real_fd;
        info!(
            dev_name = %real_dev.name().to_string_lossy(),
            "proxy: real device opened"
        );

        let AxisInfo {
            code: ff_axis,
            min,
            max,
        } = detect_axis(real_fd, &real_dev);

        // Collect capabilities to mirror on the virtual device
        let mut abs_bits: Vec<u16> = Vec::new();
        let mut key_bits: Vec<u16> = Vec::new();
        let mut ff_bits: Vec<u16> = Vec::new();
        let mut ev_bits: Vec<u16> = Vec::new();

        for code in 0..0x3fu16 {
            if real_dev
                .absolute_axes_supported()
                .contains(evdev::AbsoluteAxis::from_bits_truncate(1u64 << code))
            {
                abs_bits.push(code);
            }
        }
        let keys = real_dev.keys_supported();
        for code in 0..0x2ffu16 {
            if keys.contains(code as usize) {
                key_bits.push(code);
            }
        }
        for eff in supported_ff_effect_codes(real_fd) {
            if eff < 0x60 {
                ff_bits.push(eff);
            }
        }
        if !ff_bits.is_empty() {
            ev_bits.push(EV_FF);
        }
        if !abs_bits.is_empty() {
            ev_bits.push(EV_ABS);
        }
        if !key_bits.is_empty() {
            ev_bits.push(EV_KEY);
        }
        ev_bits.push(EV_SYN);

        info!("proxy: creating virtual uinput device…");

        let builder = UinputBuilder::new("FFB Visualizer Proxy")
            .set_ev(ev_bits)
            .set_abs(abs_bits)
            .set_key(key_bits)
            .set_ff(ff_bits);

        let (uinput_fd, virt_ev_name) = match builder.build() {
            Ok(p) => p,
            Err(e) => {
                let errno = e.raw_os_error();
                error!(?e, errno, "proxy: uinput setup FAILED");
                match errno {
                    Some(2) => error!("  /dev/uinput not found — run: modprobe uinput"),
                    Some(1) | Some(13) => {
                        error!("  need CAP_SYS_ADMIN on /dev/uinput (run as root)")
                    }
                    _ => error!("  Check dmesg for uinput errors; try --observe as fallback."),
                }
                return;
            }
        };

        let virt_ev_path = format!("/dev/input/{virt_ev_name}");
        info!(virt_path = %virt_ev_path, "proxy: virtual device ready");
        info!(">>> Point your game at: {virt_ev_path} <<<");

        {
            let mut s = self.inner.state.lock().unwrap();
            s.virtual_path = Some(virt_ev_path.clone());
        }

        // Open virtual event node for reading game EV_FF events
        let virt_fd = match crate::input::open_fd(&virt_ev_path) {
            Ok(fd) => fd,
            Err(e) => {
                error!(path = %virt_ev_path, error = ?e, "proxy: cannot open virtual device");
                unsafe {
                    libc::close(uinput_fd);
                }
                return;
            }
        };

        // UInput fd wrappers (for reading EV_UINPUT and writing EVIOCSFF)
        let mut ui_reader = RawFdStream::new(uinput_fd);

        let mut id_map: HashMap<i32, i32> = HashMap::new(); // virtual_id -> real_id
        let mut real_dev = real_dev;

        info!("proxy: entering event loop");

        let mut pfds = vec![
            PollFd::new_read(real_fd),
            PollFd::new_read(virt_fd),
            PollFd::new_read(uinput_fd),
        ];

        // Pre-allocated buffer for raw input_events from the uinput fd
        let ev_size = std::mem::size_of::<libc::input_event>();
        let mut ui_buf = vec![0u8; ev_size * 32];

        loop {
            if let Ok(ready) = poll_fds(&mut pfds, 50) {
                for i in ready {
                    match i {
                        0 => self.pump_real(&mut real_dev, virt_fd, ff_axis, min, max),
                        1 => self.pump_virtual_ev(virt_fd),
                        2 => self.pump_uinput(&mut ui_reader, uinput_fd, &mut id_map, &mut ui_buf),
                        _ => {}
                    }
                }
            }
        }
    }

    // ---- pump: real → virtual ----

    fn pump_real(
        &self,
        real: &mut Device,
        virt_fd: RawFd,
        ff_axis: u16,
        axis_min: i32,
        axis_max: i32,
    ) {
        match real.events() {
            Ok(events) => {
                for ev in events {
                    let t = ev._type;
                    if t == EV_ABS && ev.code == ff_axis {
                        let norm = norm_axis(ev.value, axis_min, axis_max);
                        self.inner.model.lock().unwrap().update_position(norm);
                    }
                    if matches!(t, EV_ABS | EV_KEY | EV_SYN) {
                        let _ = write_input_event(virt_fd, t, ev.code, ev.value);
                    }
                }
            }
            Err(e) => warn!(?e, "real read error"),
        }
    }

    // ---- pump: virtual → real (game EV_FF play/stop/gain) ----

    fn pump_virtual_ev(&self, virt_fd: RawFd) {
        // Drain the virtual event node. evdev 0.10 doesn't give us a Device
        // wrapper for an arbitrary raw fd, so read raw input_events directly.
        let ev_size = std::mem::size_of::<libc::input_event>();
        let mut buf = vec![0u8; ev_size * 32];
        loop {
            let n = unsafe {
                libc::read(
                    virt_fd,
                    buf.as_mut_ptr() as *mut libc::c_void,
                    buf.len() as _,
                )
            };
            if n <= 0 {
                break;
            }
            let n = n as usize;
            let n_evts = n / ev_size;
            for i in 0..n_evts {
                let off = i * ev_size;
                let bytes = &buf[off..off + ev_size];
                let ev = unsafe { &*(bytes.as_ptr() as *const libc::input_event) };
                if ev.type_ == EV_FF {
                    // Feed the force model so the dashboard reflects it immediately
                    self.handle_ev_ff(ev.code, ev.value as u16);
                    // Forward as raw EV_FF to the real device
                    let real_fd = *self.inner.real_fd.lock().unwrap();
                    let _ = write_input_event(real_fd, EV_FF, ev.code, ev.value);
                }
            }
            if n < buf.len() {
                break;
            }
        }
    }

    // ---- pump: uinput control fd (EV_UINPUT FF upload/erase) ----

    fn pump_uinput(
        &self,
        ui_reader: &mut RawFdStream,
        uinput_fd: RawFd,
        id_map: &mut HashMap<i32, i32>,
        ui_buf: &mut [u8],
    ) {
        let ev_size = std::mem::size_of::<libc::input_event>();
        loop {
            let n = match std::io::Read::read(ui_reader, ui_buf) {
                Ok(0) => break,
                Ok(n) => n,
                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                Err(e) => {
                    warn!(?e, "uinput fd error");
                    break;
                }
            };
            let n_evts = n / ev_size;
            for i in 0..n_evts {
                let off = i * ev_size;
                let bytes = &ui_buf[off..off + ev_size];
                self.dispatch_uinput_evt(id_map, bytes, uinput_fd);
            }
            if n % ev_size != 0 {
                break;
            }
        }
    }

    /// Parse one raw input_event read from the uinput fd.
    fn dispatch_uinput_evt(
        &self,
        id_map: &mut HashMap<i32, i32>,
        bytes: &[u8],
        uinput_fd: RawFd,
    ) {
        let ev_size = std::mem::size_of::<libc::input_event>();
        if bytes.len() < ev_size {
            return;
        }
        let ev = unsafe { &*(bytes.as_ptr() as *const libc::input_event) };
        if ev.type_ != EV_UINPUT as u16 {
            return;
        }
        let code = ev.code;
        let req_i = ev.value as u32;

        match code {
            UI_FF_UPLOAD => self.do_upload(id_map, req_i, uinput_fd),
            UI_FF_ERASE => self.do_erase(id_map, req_i, uinput_fd),
            _ => {}
        }
    }

    /// Handle one FF upload handshake (UI_BEGIN/UI_END_FF_UPLOAD).
    fn do_upload(&self, id_map: &mut HashMap<i32, i32>, req_i: u32, uinput_fd: RawFd) {
        let mut u = match begin_ff_upload(uinput_fd, req_i) {
            Ok(u) => u,
            Err(e) => {
                warn!(?e, "UI_BEGIN_FF_UPLOAD failed");
                return;
            }
        };
        let v_id = u.effect.id as i32;

        // Copy the effect and allocate — clear id first
        let mut eff = u.effect;
        eff.id = -1;

        let real_fd = *self.inner.real_fd.lock().unwrap();
        match upload_effect(real_fd, eff) {
            Ok(real_id) => {
                id_map.insert(v_id, real_id);
                id_map.insert(real_id, v_id);

                // Feed into the force model
                let params = proto_effect_to_params(&u.effect);
                self.inner.model.lock().unwrap().set_effect(real_id, params);
                debug!(v_id, real_id, "upload OK");

                u.retval = 0;
                u.effect.id = v_id as i16; // preserve the virtual id the game knows
            }
            Err(e) => {
                warn!(v_id, error = ?e, "EVIOCSFF failed on real device");
                u.retval = -(e.raw_os_error().unwrap_or(1)) as i32;
            }
        }

        if let Err(e) = end_ff_upload(uinput_fd, &mut u) {
            warn!(?e, "UI_END_FF_UPLOAD");
        }
    }

    /// Handle one FF erase handshake (UI_BEGIN/UI_END_FF_ERASE).
    fn do_erase(&self, id_map: &mut HashMap<i32, i32>, req_i: u32, uinput_fd: RawFd) {
        let mut e = match begin_ff_erase(uinput_fd, req_i) {
            Ok(e) => e,
            Err(err) => {
                warn!(?err, "UI_BEGIN_FF_ERASE");
                return;
            }
        };
        let v_id = e.effect_id as i32;

        if let Some(real_id) = id_map.remove(&v_id) {
            id_map.remove(&real_id);
            let real_fd = *self.inner.real_fd.lock().unwrap();
            let _ = erase_effect(real_fd, real_id);
            self.inner.model.lock().unwrap().erase_effect(real_id);
            debug!(v_id, real_id, "erase OK");
        }
        e.retval = 0;
        if let Err(err) = end_ff_erase(uinput_fd, &mut e) {
            warn!(?err, "UI_END_FF_ERASE");
        }
    }

    // ---- EV_FF forwarded to proxy ----

    fn handle_ev_ff(&self, code: u16, value: u16) {
        let mut m = self.inner.model.lock().unwrap();
        match code {
            FF_GAIN_CODE => m.set_gain(value),
            FF_AUTOCENTER_CODE => m.set_autocenter(value),
            _ if value > 0 => m.play(code as i32, value),
            _ => m.stop(code as i32),
        }
    }
}
