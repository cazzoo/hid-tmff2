//! Linux uinput Force-Feedback (FF) protocol wrappers.
//!
//! Provides struct definitions and ioctl wrappers for the passthrough proxy.
//! Reference: linux/input.h, linux/uinput.h, Documentation/input/uinput.rst

use std::io::{self, Read, Write};
use std::os::unix::io::{AsRawFd, RawFd};
use std::mem;

// ---------------------------------------------------------------------------
// Linux UAPI structs – repr(C) and byte layout must match <linux/input.h>.
// ---------------------------------------------------------------------------

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct FfReplay {
    pub length: u16,
    pub delay:  u16,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct FfTrigger {
    pub button:   u16,
    pub interval: u16,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct FfEnvelope {
    pub attack_length:  u16,
    pub attack_level:   u16,
    pub fade_length:    u16,
    pub fade_level:     u16,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct FfConstantEffect {
    pub level:    i16,
    pub envelope: FfEnvelope,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct FfRampEffect {
    pub start_level: i16,
    pub end_level:   i16,
    pub envelope:    FfEnvelope,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct FfPeriodicEffect {
    pub waveform:    u16,
    pub period:      u16,
    pub magnitude:   i16,
    pub offset:      i16,
    pub phase:       u16,
    pub envelope:    FfEnvelope,
    pub custom_len:  u32,
    pub custom_data: *mut i8,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct FfConditionEffect {
    pub right_saturation: u16,
    pub left_saturation:  u16,
    pub right_coeff:      i16,
    pub left_coeff:       i16,
    pub deadband:         u16,
    pub center:           i16,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct FfRumbleEffect {
    pub strong_magnitude: u16,
    pub weak_magnitude:   u16,
}

// Union of all effect-specific payloads.
#[repr(C)]
#[derive(Clone, Copy)]
pub union FfEffectUnion {
    pub constant:  FfConstantEffect,
    pub ramp:      FfRampEffect,
    pub periodic:  FfPeriodicEffect,
    pub condition: [FfConditionEffect; 2],
    pub rumble:    FfRumbleEffect,
}

impl Default for FfEffectUnion {
    fn default() -> Self { unsafe { mem::zeroed() } }
}

impl std::fmt::Debug for FfEffectUnion {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("<ff_effect_union>")
    }
}

unsafe impl Send for FfEffectUnion {}
unsafe impl Sync for FfEffectUnion {}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct FfEffect {
    pub effect_type: u16,
    pub id:          i16,
    pub direction:   u16,
    pub trigger:     FfTrigger,
    pub replay:      FfReplay,
    pub u:           FfEffectUnion,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct UinputFfUpload {
    pub request_id: u32,
    pub retval:     i32,
    pub effect:     FfEffect,
    pub old:        FfEffect,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct UinputFfErase {
    pub request_id: u32,
    pub retval:     i32,
    pub effect_id:  u32,
}

// ---------------------------------------------------------------------------
// ioctl number computation
// ---------------------------------------------------------------------------

const _IOC_NRBITS:   u32 = 8;
const _IOC_TYPEBITS: u32 = 8;
const _IOC_SIZEBITS: u32 = 14;
const _IOC_DIRBITS:  u32 = 2;
const _IOC_NRSHIFT:  u32 = 0;
const _IOC_TYPESHIFT:u32 = _IOC_NRSHIFT + _IOC_NRBITS;
const _IOC_SIZESHIFT:u32 = _IOC_TYPESHIFT + _IOC_TYPEBITS;
const _IOC_DIRSHIFT: u32 = _IOC_SIZESHIFT + _IOC_SIZEBITS;

const IOC_NONE: u32 = 0x0;
const IOC_WRITE: u32 = 0x1;
const IOC_READ:  u32 = 0x2;

const fn _ioc(dir: u32, io_type: u32, nr: u32, size: u32) -> u32 {
    (dir << _IOC_DIRSHIFT)
        | (io_type << _IOC_TYPESHIFT)
        | (nr << _IOC_NRSHIFT)
        | (size << _IOC_SIZESHIFT)
}

const fn _iow(io_type: u8, nr: u8, size: usize) -> u32 {
    _ioc(IOC_WRITE, io_type as u32, nr as u32, size as u32)
}

const fn _ior(io_type: u8, nr: u8, size: usize) -> u32 {
    _ioc(IOC_READ, io_type as u32, nr as u32, size as u32)
}

const fn _iowr(io_type: u8, nr: u8, size: usize) -> u32 {
    _ioc(IOC_READ | IOC_WRITE, io_type as u32, nr as u32, size as u32)
}

const fn _io(io_type: u8, nr: u8, size: usize) -> u32 {
    _ioc(IOC_NONE, io_type as u32, nr as u32, size as u32)
}

/// EV_FF – ForceFeedback event type (input-event-codes.h).
pub const EV_FF: u16 = 0x15;
/// EV_UINPUT – synthetic event type used only by the uinput control fd.
pub const EV_UINPUT: u16 = 0x0101;

/// EVIOCSFF – upload an ff_effect to the real device; kernel writes back assigned id.
pub const EVIOCSFF: u32 = _iow(b'E', 0x80, mem::size_of::<FfEffect>());

/// EVIOCRMFF – erase effect by id from the real device.
pub const EVIOCRMFF: u32 = _iow(b'E', 0x81, mem::size_of::<i32>());

/// EVIOCGEFFECTS – read the max supported effect count.
pub const EVIOCGEFFECTS: u32 = _ior(b'E', 0x84, mem::size_of::<i32>());

/// UI_BEGIN_FF_UPLOAD – start an FF upload handshake (uinput fd).
pub const UI_BEGIN_FF_UPLOAD: u32 = _iowr(b'U', 200, mem::size_of::<UinputFfUpload>());

/// UI_END_FF_UPLOAD – complete an FF upload handshake (uinput fd).
pub const UI_END_FF_UPLOAD: u32 = _iowr(b'U', 201, mem::size_of::<UinputFfUpload>());

/// UI_BEGIN_FF_ERASE – start an FF erase handshake (uinput fd).
pub const UI_BEGIN_FF_ERASE: u32 = _iowr(b'U', 202, mem::size_of::<UinputFfErase>());

/// UI_END_FF_ERASE – complete an FF erase handshake (uinput fd).
pub const UI_END_FF_ERASE: u32 = _iowr(b'U', 203, mem::size_of::<UinputFfErase>());

/// Maximum length of a uinput device name.
pub const UINPUT_MAX_NAME_SIZE: usize = 80;

/// `struct uinput_setup` – name + identity + max FF effects (UI_DEV_SETUP).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct UinputSetup {
    pub id: libc::input_id,
    pub name: [u8; UINPUT_MAX_NAME_SIZE],
    pub ff_effects_max: u32,
}

/// UI_DEV_SETUP – set device parameters before UI_DEV_CREATE.
pub const UI_DEV_SETUP: u32 = _iow(b'U', 3, mem::size_of::<UinputSetup>());

/// UI_DEV_CREATE – instantiate the virtual device.
pub const UI_DEV_CREATE: u32 = _io(b'U', 1, 0);
/// UI_DEV_DESTROY – tear down the virtual device.
pub const UI_DEV_DESTROY: u32 = _io(b'U', 2, 0);

/// UI_SET_EVBIT – enable an event type bit (e.g. EV_ABS, EV_FF).
pub const UI_SET_EVBIT: u32 = _iow(b'U', 100, 4);
/// UI_SET_KEYBIT – enable a key/button bit.
pub const UI_SET_KEYBIT: u32 = _iow(b'U', 101, 4);
/// UI_SET_ABSBIT – enable an absolute axis bit.
pub const UI_SET_ABSBIT: u32 = _iow(b'U', 103, 4);
/// UI_SET_FFBIT – enable a force-feedback effect-type bit.
pub const UI_SET_FFBIT: u32 = _iow(b'U', 107, 4);

/// Set a single capability bit on a uinput control fd. The bit code is passed
/// directly as the ioctl argument (see `raw_ioctl_int`).
pub fn ui_set_bit(fd: RawFd, req: u32, bit: i32) -> io::Result<()> {
    raw_ioctl_int(fd, req, bit).map(|_| ())
}

/// Query the FF effect-type bits the real device supports (EVIOCGBIT(EV_FF, ...)).
pub fn supported_ff_effects(fd: RawFd) -> Vec<u16> {
    let mut buf = [0u8; 16];
    let req = _ior(b'E', 0x20 + (EV_FF as u8), buf.len());
    if raw_ioctl_ptr(fd, req, buf.as_mut_ptr() as *mut libc::c_void).is_err() {
        return Vec::new();
    }
    let mut out = Vec::new();
    for i in 0..(buf.len() * 8) {
        if buf[i / 8] & (1u8 << (i % 8)) != 0 {
            out.push(i as u16);
        }
    }
    out
}

/// Convert a kernel `ff_effect` (read during a UI_BEGIN_FF_UPLOAD handshake)
/// into the model's `EffectParams`.
pub fn effect_to_params(effect: &FfEffect) -> crate::types::EffectParams {
    use crate::types::{EffectParams, EffectType, Waveform};

    let et = EffectType::from_code(effect.effect_type).unwrap_or(EffectType::Constant);
    let mut p = EffectParams::default();
    p.id = effect.id as i32;
    p.effect_type = et;
    p.direction = effect.direction;
    p.length = effect.replay.length as u32;
    p.delay = effect.replay.delay as u32;

    let env = match et {
        EffectType::Constant => {
            let c = unsafe { effect.u.constant };
            p.level = Some(c.level);
            c.envelope
        }
        EffectType::Ramp => {
            let r = unsafe { effect.u.ramp };
            p.start_level = Some(r.start_level);
            p.end_level = Some(r.end_level);
            r.envelope
        }
        EffectType::Periodic => {
            let per = unsafe { effect.u.periodic };
            let wf = match per.waveform {
                0x58 => Waveform::Square,
                0x59 => Waveform::Triangle,
                0x5a => Waveform::Sine,
                0x5b => Waveform::SawUp,
                0x5c => Waveform::SawDown,
                _ => Waveform::Unknown,
            };
            p.waveform = Some(wf);
            p.period = Some(per.period);
            p.magnitude = Some(per.magnitude);
            p.offset = Some(per.offset);
            p.phase = Some(per.phase);
            per.envelope
        }
        EffectType::Spring | EffectType::Damper | EffectType::Friction | EffectType::Inertia => {
            let c = unsafe { effect.u.condition[0] };
            p.right_saturation = Some(c.right_saturation);
            p.left_saturation = Some(c.left_saturation);
            p.right_coeff = Some(c.right_coeff);
            p.left_coeff = Some(c.left_coeff);
            p.deadband = Some(c.deadband);
            p.center = Some(c.center);
            FfEnvelope::default()
        }
        EffectType::Rumble => {
            let rum = unsafe { effect.u.rumble };
            p.strong_magnitude = Some(rum.strong_magnitude);
            p.weak_magnitude = Some(rum.weak_magnitude);
            FfEnvelope::default()
        }
    };

    p.attack_length = env.attack_length;
    p.attack_level = env.attack_level;
    p.fade_length = env.fade_length;
    p.fade_level = env.fade_level;
    p
}

// ---------------------------------------------------------------------------
// Error type
// ---------------------------------------------------------------------------

#[derive(Debug, thiserror::Error)]
pub enum ProtocolError {
    #[error("ioctl {0} failed: {1}")]
    Ioctl(&'static str, io::Error),
    #[error("read error on raw fd")]
    Read(#[from] io::Error),
}

// ---------------------------------------------------------------------------
// Linux ioctl syscall wrapper (avoids depending on nix's ioctl macro internals)
// ---------------------------------------------------------------------------

/// Raw ioctl wrapper. `arg` is passed directly to the kernel.
pub fn raw_ioctl(fd: RawFd, request: u32) -> io::Result<i32> {
    let ret = unsafe { libc::ioctl(fd, request as libc::c_ulong) };
    if ret < 0 { Err(io::Error::last_os_error()) } else { Ok(ret) }
}

pub fn raw_ioctl_ptr(fd: RawFd, request: u32, arg: *mut libc::c_void) -> io::Result<i32> {
    let ret = unsafe { libc::ioctl(fd, request as libc::c_ulong, arg) };
    if ret < 0 { Err(io::Error::last_os_error()) } else { Ok(ret) }
}

/// Raw ioctl that passes an integer argument directly (NOT a pointer).
///
/// The uinput UI_SET_*BIT ioctls are declared `_IOW('U', nr, int)` but the
/// kernel takes the bit code directly in `arg` rather than dereferencing it
/// — confirmed by stracing evdev, which passes e.g. `ioctl(fd, UI_SET_EVBIT,
/// 0x15)`. Passing a pointer here makes the kernel read a bogus code and
/// return EINVAL.
pub fn raw_ioctl_int(fd: RawFd, request: u32, arg: i32) -> io::Result<i32> {
    let ret = unsafe { libc::ioctl(fd, request as libc::c_ulong, arg as libc::c_int) };
    if ret < 0 { Err(io::Error::last_os_error()) } else { Ok(ret) }
}

// ---------------------------------------------------------------------------
// High-level protocol operations (real device)
// ---------------------------------------------------------------------------

/// Upload an effect to the real FFB device. Kernel assigns and writes back `id`.
pub fn upload_effect(real_fd: RawFd, mut effect: FfEffect) -> io::Result<i32> {
    raw_ioctl_ptr(real_fd, EVIOCSFF, &mut effect as *mut FfEffect as *mut libc::c_void)
        .map(|_| effect.id as i32)
}

/// Erase a previously-uploaded effect from the real device.
pub fn erase_effect(real_fd: RawFd, effect_id: i32) -> io::Result<()> {
    let id = effect_id;
    raw_ioctl_ptr(real_fd, EVIOCRMFF, &id as *const i32 as *mut libc::c_void)
        .map(|_| ())
}

/// Query the maximum number of effects the real device supports.
pub fn max_effects(real_fd: RawFd) -> io::Result<i32> {
    let mut n = 0i32;
    raw_ioctl_ptr(real_fd, EVIOCGEFFECTS, &mut n as *mut i32 as *mut libc::c_void)
        .map(|_| n)
}

// ---------------------------------------------------------------------------
// High-level protocol operations (uinput FF handshake)
// ---------------------------------------------------------------------------

pub fn begin_ff_upload(uinput_fd: RawFd, request_id: u32) -> io::Result<UinputFfUpload> {
    let mut u = UinputFfUpload { request_id, ..Default::default() };
    raw_ioctl_ptr(uinput_fd, UI_BEGIN_FF_UPLOAD, &mut u as *mut UinputFfUpload as *mut libc::c_void)
        .map(|_| u)
}

pub fn end_ff_upload(uinput_fd: RawFd, u: &mut UinputFfUpload) -> io::Result<()> {
    raw_ioctl_ptr(uinput_fd, UI_END_FF_UPLOAD, u as *mut UinputFfUpload as *mut libc::c_void)
        .map(|_| ())
}

pub fn begin_ff_erase(uinput_fd: RawFd, request_id: u32) -> io::Result<UinputFfErase> {
    let mut e = UinputFfErase { request_id, ..Default::default() };
    raw_ioctl_ptr(uinput_fd, UI_BEGIN_FF_ERASE, &mut e as *mut UinputFfErase as *mut libc::c_void)
        .map(|_| e)
}

pub fn end_ff_erase(uinput_fd: RawFd, e: &mut UinputFfErase) -> io::Result<()> {
    raw_ioctl_ptr(uinput_fd, UI_END_FF_ERASE, e as *mut UinputFfErase as *mut libc::c_void)
        .map(|_| ())
}

// ===========================================================================
// RawFdStream – Read from a raw FD without interpreting the data
// ===========================================================================

/// A `std::io::Read` / `Write` wrapper around a raw Unix file descriptor.
/// Used to read raw `input_event` bytes from the uinput fd and write raw
/// `input_event` bytes to the real fd.
#[derive(Debug)]
pub struct RawFdStream {
    fd: RawFd,
}

impl RawFdStream {
    pub fn new(fd: RawFd) -> Self { Self { fd } }
    pub fn fd(&self) -> RawFd { self.fd }
}

impl Read for RawFdStream {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let n = unsafe { libc::read(self.fd, buf.as_mut_ptr() as *mut libc::c_void, buf.len() as _) };
        if n < 0 { Err(io::Error::last_os_error()) } else { Ok(n as usize) }
    }
}

impl Write for RawFdStream {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        let n = unsafe { libc::write(self.fd, buf.as_ptr() as *const libc::c_void, buf.len() as _) };
        if n < 0 { Err(io::Error::last_os_error()) } else { Ok(n as usize) }
    }
    fn flush(&mut self) -> io::Result<()> { Ok(()) }
}

impl AsRawFd for RawFdStream {
    fn as_raw_fd(&self) -> RawFd { self.fd }
}

/// Blocking poll on multiple FDs using `libc::poll`.
/// Returns a Vec of the indices of FDs that have events ready.
#[repr(C)]
#[derive(Debug)]
pub struct PollFd {
    pub fd:      RawFd,
    pub events:  libc::c_short,
    pub revents: libc::c_short,
}

impl PollFd {
    pub fn new_read(fd: RawFd) -> Self {
        Self { fd, events: libc::POLLIN, revents: 0 }
    }
}

pub fn poll_fds(fds: &mut [PollFd], timeout_ms: i32) -> io::Result<Vec<usize>> {
    let mut native: Vec<libc::pollfd> = fds
        .iter_mut()
        .map(|p| libc::pollfd {
            fd:      p.fd,
            events:  p.events,
            revents: 0,
        })
        .collect();
    let n = unsafe {
        libc::poll(native.as_mut_ptr(), native.len() as _, timeout_ms)
    };
    if n < 0 { return Err(io::Error::last_os_error()); }
    for (i, nf) in native.iter().enumerate() {
        fds[i].revents = nf.revents;
    }
    let mut ready = Vec::new();
    for (i, fd) in fds.iter().enumerate() {
        if fd.revents & libc::POLLIN != 0 {
            ready.push(i);
        }
    }
    Ok(ready)
}
