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

use std::collections::{HashSet, HashMap};
use std::os::unix::io::RawFd;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

use evdev::{AbsoluteAxisCode, Device, EventType, InputDevice, FFEffect};
use tracing::{debug, error, info, warn};

use crate::model::ForceModel;
use crate::protocol::{
    begin_ff_erase, begin_ff_upload, end_ff_erase, end_ff_upload,
    erase_effect, poll_fds, upload_effect, effect_to_params as proto_effect_to_params,
    FfEffect, PollFd, RawFdStream, EV_UINPUT,
};
use crate::types::{EffectType, FF_AUTOCENTER as FF_AUTOCENTER_CODE, FF_GAIN as FF_GAIN_CODE};

// Linux input.h
const EV_KEY: u16 = 0x01;
const EV_REL: u16 = 0x02;
const EV_ABS: u16 = 0x03;
const EV_MSC: u16 = 0x04;
const EV_SYN: u16 = 0x00;
const EV_FF:  u16 = 0x03;

// uinput FF command codes for EV_UINPUT
const UI_FF_UPLOAD: u16 = 1;
const UI_FF_ERASE:  u16 = 2;

const BUS_USB: u16 = 0x03;

// ===========================================================================
// UInput builder via raw syscalls (evdev 0.10 has no UInput wrapper)
// ===========================================================================

#[derive(Debug, Default)]
struct UinputBuilder {
    name:    String,
    vendor:  u16,
    product: u16,
    version: u16,
    bustype: u16,
    ff_bits:  HashSet<u16>,
    ev_bits:  HashSet<u16>,
    abs_bits: HashSet<u16>,
    key_bits:HashSet<u16>,
}

impl UinputBuilder {
    fn new(name: impl Into<String>) -> Self {
        Self {
            name: name.into(),
            bustype: BUS_USB,
            ..Self::default()
        }
    }
    fn set_ff(mut self, b: impl IntoIterator<Item=u16>) -> Self { self.ff_bits.extend(b); self }
    fn set_ev(mut self, b: impl IntoIterator<Item=u16>) -> Self { self.ev_bits.extend(b); self }
    fn set_abs(mut self, b: impl IntoIterator<Item=u16>) -> Self { self.abs_bits.extend(b); self }
    fn set_key(mut self, b: impl IntoIterator<Item=u16>) -> Self { self.key_bits.extend(b); self }

    /// Build the virtual device.  Returns (uinput_control_fd, device_name_eventN).
    fn build(self) -> std::io::Result<(RawFd, String)> {
        unsafe {
            let fd = libc::open(
                b"/dev/uinput\0" as *const u8 as *const libc::c_char,
                libc::O_WRONLY | libc::O_NONBLOCK,
            );
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

            // ioctl request: _IOW('U', nr, size)
            fn iow(fd: RawFd, nr: u8, v: u32) -> std::io::Result<()> {
                let req = (2u64 << 30) | ((b'U' as u64) << 8) | ((nr as u64) << 16) | (4u64 << 16);
                if unsafe { libc::ioctl(fd, req, v) } < 0 {
                    Err(std::io::Error::last_os_error())
                } else {
                    Ok(())
                }
            }

            // Set properties
            iow(fd, 0x00, ((self.vendor  as u32) << 16) | self.product  as u32)?;
            iow(fd, 0x01, self.version as u32)?;
            iow(fd, 0x03, self.bustype as u32)?;

            // Device name (NUL-terminated, max 64 bytes)
            let name_bytes = format!("{}\0", self.name);
            let req = (2u64 << 30) | ((b'U' as u64) << 8) | (0x02u64 << 16) | (name_bytes.len() as u64);
            if unsafe { libc::ioctl(fd, req, name_bytes.as_ptr() as *mut libc::c_void) } < 0 {
                let e = std::io::Error::last_os_error();
                unsafe { libc::close(fd); }
                return Err(e);
            }

            // Capability bits
            for b in &self.ev_bits  { iow(fd, 0x20, *b as u32)?; }
            for b in &self.abs_bits { iow(fd, 0x3A, *b as u32)?; }
            for b in &self.key_bits { iow(fd, 0x33, *b as u32)?; }
            for b in &self.ff_bits  { iow(fd, 0x75, *b as u32)?; }

            // Create the device
            let create_req = (2u64 << 30) | ((b'U' as u64) << 8) | (0x01u64 << 16);
            if unsafe { libc::ioctl(fd, create_req, 0) } < 0 {
                let e = std::io::Error::last_os_error();
                unsafe { libc::close(fd); }
                return Err(e);
            }

            // Wait for the kernel to create the event node
            unsafe { libc::usleep(200_000); } // 200 ms

            // Find the event node by scanning /sys/class/input/eventN/device/name
            let ev_name = find_virtual_event_name(&self.name).ok_or_else(|| {
                std::io::Error::new(
                    std::io::ErrorKind::NotFound,
                    format!("virtual event node for '{}' not found — run: cat /proc/bus/input/devices", self.name),
                )
            })?;

            Ok((fd, ev_name))
        }
    }
}

fn find_virtual_event_name(dev_name: &str) -> Option<String> {
    let sysp = std::path::Path::new("/sys/class/input");
    let mut entries: Vec<_> = std::fs::read_dir(sysp)
        .ok()?
        .filter_map(|e| e.ok())
        .collect();
    // Sort by mtime descending (newest first — our device was just created)
    entries.sort_by(|a, b| {
        let at = a.metadata().and_then(|m| m.modified()).unwrap_or(std::time::UNIX_EPOCH);
        let bt = b.metadata().and_then(|m| m.modified()).unwrap_or(std::time::UNIX_EPOCH);
        bt.cmp(&at)
    });
    for entry in entries {
        let nf = entry.path().join("device").join("name");
        if let Ok(name) = std::fs::read_to_string(nf) {
            if name.trim() == dev_name {
                return Some(entry.file_name().to_string_lossy().into_owned());
            }
        }
    }
    None
}

// ===========================================================================
// Axis helpers
// ===========================================================================

struct AxisInfo {
    code: u16,
    min:  i32,
    max:  i32,
}

fn detect_axis(dev: &Device) -> AxisInfo {
    for candidate in [
        AbsoluteAxisCode::ABS_RX,
        AbsoluteAxisCode::ABS_X,
        AbsoluteAxisCode::ABS_WHEEL,
    ] {
        if let Some(ai) = dev.get_abs_info(candidate) {
            return AxisInfo {
                code: candidate as u16,
                min: ai.min(),
                max: ai.max(),
            };
        }
    }
    for ax in dev.supported_abs_axes() {
        if let Some(ai) = dev.get_abs_info(ax) {
            return AxisInfo {
                code: ax as u16,
                min: ai.min(),
                max: ai.max(),
            };
        }
    }
    AxisInfo { code: AbsoluteAxisCode::ABS_X as u16, min: 0, max: 32767 }
}

fn norm_axis(value: i32, min: i32, max: i32) -> f32 {
    let span = (max - min).max(1) as f32;
    (((value - min) as f32) / span * 2.0 - 1.0).clamp(-1.0, 1.0)
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
        match self { ProxyMode::Observe => write!(f, "observe"), ProxyMode::Proxy => write!(f, "proxy") }
    }
}

#[derive(Debug, Default)]
pub struct ProxyState {
    pub mode:         ProxyMode,
    pub virtual_path: Option<String>,
    pub real_path:    String,
}

// ===========================================================================
// Inner state shared between the ProxyBackend handle and the background thread.
// ===========================================================================

struct ProxyInner {
    state:   Arc<Mutex<ProxyState>>,
    model:   Arc<Mutex<ForceModel>>,
    real_fd: RawFd,
}

// ===========================================================================
// ProxyBackend — public handle
// ===========================================================================

pub struct ProxyBackend {
    inner: Arc<ProxyInner>,
}

impl ProxyBackend {
    pub fn new(real_path: String, observe: bool) -> Self {
        let state = Arc::new(Mutex::new(ProxyState {
            mode: if observe { ProxyMode::Observe } else { ProxyMode::Proxy },
            virtual_path: None,
            real_path,
        }));
        let model = Arc::new(Mutex::new(ForceModel::new()));
        // The real_fd must be set by run_proxy() before the event loop.
        Self {
            inner: Arc::new(ProxyInner { state, model, real_fd: -1 }),
        }
    }

    /// Start a blocking thread.  Returns immediately.
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

    pub fn model_arc(&self) -> Arc<Mutex<ForceModel>> {
        Arc::clone(&self.inner.model)
    }

    /// Gather a snapshot of the force-model state (async-safe, acquires a read lock).
    pub fn sample(&self) -> crate::types::SampleSnapshot {
        self.inner.model.lock().unwrap().sample()
    }
}

// ===========================================================================
// Observer run path
// ===========================================================================

impl ProxyBackend {
    fn run_observer(self: Arc<Self>) {
        let path = { self.inner.state.lock().unwrap().real_path.clone() };
        info!(path = %path, "observer: opening device read-only");
        let Ok(dev) = InputDevice::open(&path) else {
            error!(path = %path, "observer: cannot open device");
            return;
        };
        info!(dev_name = %dev.name().unwrap_or("?"), "observer: opened");

        let AxisInfo { code: ff_axis, min, max } = detect_axis(&dev);

        loop {
            match dev.read_events() {
                Ok(events) => {
                    for ev in events {
                        if ev.event_type() == EV_ABS && ev.code == ff_axis {
                            let norm = norm_axis(ev.value as i32, min, max);
                            self.inner.model.lock().unwrap().update_position(norm);
                        }
                        if ev.event_type() == EV_FF {
                            self.handle_ev_ff(ev.code as u16, ev.value as u16);
                        }
                    }
                }
                Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                    thread::sleep(Duration::from_millis(10));
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
        let path = { self.inner.state.lock().unwrap().real_path.clone() };
        info!(path = %path, "proxy: opening real device");

        let Ok(real_dev) = InputDevice::open(&path) else {
            error!(path = %path, "proxy: cannot open real device");
            return;
        };
        let real_fd = real_dev.as_raw_fd();
        info!(dev_name = %real_dev.name().unwrap_or("?"), "proxy: real device opened");

        // Update real_fd on the inner so other methods can use it
        // SAFETY: we hold Arc and run the proxy thread; no other thread
        // modifies real_fd.
        { self.inner.real_fd = real_fd; }

        let AxisInfo { code: ff_axis, min, max } = detect_axis(&real_dev);

        // Collect capabilities to mirror on the virtual device
        let mut abs_bits: Vec<u16> = Vec::new();
        let mut key_bits: Vec<u16> = Vec::new();
        let mut ff_bits: Vec<u16> = Vec::new();

        for ax in real_dev.supported_abs_axes() { abs_bits.push(ax as u16); }
        for k in real_dev.keys_supported().ones() { key_bits.push(k as u16); }
        // FF effect codes the device supports
        for eff in real_dev.supported_ff_effects() {
            let c = eff as u16;
            if c < 0x60 { ff_bits.push(c); } // 0..0x5F covers all known FF types
        }

        info!("proxy: creating virtual uinput device…");

        // ioctl for setting ev/rel bits (UI_SET_EVBIT / UI_SET_RELBIT etc.)
        let builder = UinputBuilder::new("FFB Visualizer Proxy")
            .set_ev(0..0u16)   // EV_SYN/EV_FF don't go into uinput device's EVENT set
            .set_abs(abs_bits)
            .set_key(key_bits)
            .set_ff(ff_bits);

        let (uinput_fd, virt_ev_name) = match builder.build() {
            Ok(p) => p,
            Err(e) => {
                let errno = e.raw_os_error();
                error!(?e, errno, "proxy: uinput setup FAILED");
                match errno {
                    Some(2)  => error!("  /dev/uinput not found — run: modprobe uinput"),
                    Some(1) | Some(13) => error!("  need CAP_SYS_ADMIN on /dev/uinput (run as root)"),
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
        let Ok(mut virt_dev) = InputDevice::open(&virt_ev_path) else {
            error!(path = %virt_ev_path, "proxy: cannot open virtual device");
            unsafe { libc::close(uinput_fd); }
            return;
        };
        let virt_fd = virt_dev.as_raw_fd();

        // UInput fd wrappers (for reading EV_UINPUT and writing EVIOCSFF)
        let mut ui_reader = RawFdStream::new(uinput_fd);
        let mut ui_writer = RawFdStream::new(uinput_fd);

        let mut id_map: HashMap<i32, i32> = HashMap::new(); // virtual_id -> real_id

        info!("proxy: entering event loop");

        // Poll arrays
        let mut pfds = vec![
            PollFd::new_read(real_fd),
            PollFd::new_read(virt_fd),
            PollFd::new_read(uinput_fd),
        ];

        // Pre-allocated buffer for raw input_events from the uinput fd
        let ev_size  = std::mem::size_of::<libc::input_event>();
        let ui_buf   = vec![0u8; ev_size * 32];
        let mut ui_buf_all = ui_buf; // owned by the pump loop

        loop {
            if self.stop_requested() {
                info!("proxy: shutting down");
                break;
            }
            if let Ok(ready) = poll_fds(&mut pfds, 50) {
                for i in ready {
                    match i {
                        0 => self.pump_real(&real_dev, &mut virt_dev, ff_axis, min, max),
                        1 => self.pump_virtual_ev(&mut virt_dev),
                        2 => {
                            self.pump_uinput(&mut ui_reader, &mut ui_writer, uinput_fd,
                                             &mut id_map, &mut ui_buf_all);
                        }
                        _ => {}
                    }
                }
            }
        }
    }

    // ---- pump: real → virtual ----

    fn pump_real(
        &self,
        real: &Device,
        virt: &mut InputDevice,
        ff_axis: u16,
        axis_min: i32,
        axis_max: i32,
    ) {
        match real.read_events() {
            Ok(events) => {
                for ev in events {
                    if ev.event_type() == EV_ABS && ev.code == ff_axis {
                        let norm = norm_axis(ev.value as i32, axis_min, axis_max);
                        self.inner.model.lock().unwrap().update_position(norm);
                    }
                    match ev.event_type() {
                        EV_ABS | EV_KEY | EV_SYN => {
                            let _ = virt.write_event(
                                evdev::InputEvent::new(ev.event_type().into(),
                                                       ev.code, ev.value),
                            );
                            virt.sync();
                        }
                        _ => {}
                    }
                }
            }
            Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {}
            Err(e) => warn!(?e, "real read error"),
        }
    }

    // ---- pump: virtual → real (game EV_FF play/stop/gain) ----

    fn pump_virtual_ev(&self, virt_dev: &mut InputDevice) {
        match virt_dev.read_events() {
            Ok(events) => {
                for ev in events {
                    if ev.event_type() == EV_FF {
                        // Feed the force model so the dashboard reflects it immediately
                        self.handle_ev_ff(ev.code as u16, ev.value as u16);
                        // Forward as raw EV_FF to the real device
                        let evt = libc::input_event {
                            time:  libc::timeval { tv_sec: 0, tv_usec: 0 },
                            type_:  EV_FF,
                            code:   ev.code,
                            value:  ev.value,
                        };
                        let buf = std::slice::from_raw_parts(
                            &evt as *const _ as *const u8,
                            std::mem::size_of::<libc::input_event>(),
                        );
                        unsafe {
                            libc::write(self.inner.real_fd,
                                        buf.as_ptr() as *const libc::c_void,
                                        buf.len() as _);
                        }
                    }
                }
            }
            Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {}
            Err(e) => warn!(?e, "virtual read error"),
        }
    }

    // ---- pump: uinput control fd (EV_UINPUT FF upload/erase) ----

    fn pump_uinput(
        &self,
        ui_reader: &mut RawFdStream,
        ui_writer: &mut dyn std::io::Write,
        uinput_fd: RawFd,
        id_map: &mut HashMap<i32, i32>,
        ui_buf: &mut [u8],
    ) {
        let ev_size = std::mem::size_of::<libc::input_event>();
        loop {
            let n = match ui_reader.read(ui_buf) {
                Ok(0)  => break,
                Ok(n)  => n,
                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                Err(e)  => { warn!(?e, "uinput fd error"); break; }
            };
            let n_evts = n / ev_size;
            for i in 0..n_evts {
                let bytes = &ui_buf[i * ev_size..(i+1) * ev_size];
                self.dispatch_uinput_evt(ui_writer, id_map, bytes, uinput_fd);
            }
            if n % ev_size != 0 { break; }
            // Drain eagerly (events might be queued)
        }
    }

    /// Parse one raw input_event read from the uinput fd.
    fn dispatch_uinput_evt(
        &self,
        ui_writer: &mut dyn std::io::Write,
        id_map:    &mut HashMap<i32, i32>,
        bytes:     &[u8],
        uinput_fd: RawFd,
    ) {
        let ev_size = std::mem::size_of::<libc::input_event>();
        if bytes.len() < ev_size { return; }
        let ev = unsafe { &*(bytes.as_ptr() as *const libc::input_event) };
        if ev.type_ != EV_UINPUT { return; }
        let code  = ev.code as u16;
        let req_i = ev.value as u32;

        match code {
            UI_FF_UPLOAD => self.do_upload(ui_writer, id_map, req_i, uinput_fd),
            UI_FF_ERASE  => self.do_erase(ui_writer, id_map, req_i, uinput_fd),
            _ => {}
        }
    }

    /// Handle one FF upload handshake (UI_BEGIN/UI_END_FF_UPLOAD).
    fn do_upload(
        self: &Arc<Self>,
        ui_writer: &mut dyn std::io::Write,
        id_map:    &mut HashMap<i32, i32>,
        req_i:     u32,
        uinput_fd: RawFd,
    ) {
        let mut u = match begin_ff_upload(uinput_fd, req_i) {
            Ok(u) => u,
            Err(e) => { warn!(?e, "UI_BEGIN_FF_UPLOAD failed"); return; }
        };
        let v_id = u.effect.id as i32;

        // Copy the effect and allocate — clear id first
        let mut eff = u.effect.clone();
        eff.id = -1;

        match upload_effect(self.inner.real_fd, eff.clone()) {
            Ok(real_id) => {
                id_map.insert(v_id, real_id);
                id_map.insert(real_id, v_id);

                // Feed into the force model
                let params = proto_effect_to_params(&u.effect);
                self.inner.model.lock().unwrap().set_effect(real_id, params);
                debug!(v_id, real_id, "upload OK");

                u.retval   = 0;
                u.effect.id = v_id; // preserve the virtual id the game knows
            }
            Err(e) => {
                warn!(v_id, ?e, "EVIOCSFF failed on real device");
                u.retval = -(e.raw_os_error().unwrap_or(1)) as i32;
            }
        }

        if let Err(e) = end_ff_upload(uinput_fd, &mut u) {
            warn!(?e, "UI_END_FF_UPLOAD");
        }
    }

    /// Handle one FF erase handshake (UI_BEGIN/UI_END_FF_ERASE).
    fn do_erase(
        self: &Arc<Self>,
        _ui_writer: &mut dyn std::io::Write,
        id_map:    &mut HashMap<i32, i32>,
        req_i:     u32,
        uinput_fd: RawFd,
    ) {
        let mut e = match begin_ff_erase(uinput_fd, req_i) {
            Ok(e) => e,
            Err(err) => { warn!(?err, "UI_BEGIN_FF_ERASE"); return; }
        };
        let v_id = e.effect_id as i32;

        if let Some(real_id) = id_map.remove(&v_id) {
            id_map.remove(&real_id);
            let _ = erase_effect(self.inner.real_fd, real_id);
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
            FF_GAIN_CODE       => m.set_gain(value),
            FF_AUTOCENTER_CODE => m.set_autocenter(value),
            _ if value > 0     => m.play(code as i32, value),
            _                  => m.stop(code as i32),
        }
    }

    fn stop_requested(&self) -> bool { false }
}
