//! Linux uinput Force-Feedback (FF) protocol wrappers.
//!
//! Provides struct definitions and ioctl wrappers for the passthrough proxy.
//! Reference: linux/input.h, linux/uinput.h, Documentation/input/uinput.rst

use std::io::{self, Read, Write};
use std::os::unix::io::{AsRawFd, RawFd};
use std::{mem, ptr};

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
#[derive(Debug, Clone, Copy)]
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

// ---------------------------------------------------------------------------
// High-level protocol operations (real device)
// ---------------------------------------------------------------------------

/// Upload an effect to the real FFB device. Kernel assigns and writes back `id`.
pub fn upload_effect(real_fd: RawFd, mut effect: FfEffect) -> io::Result<i32> {
    raw_ioctl_ptr(real_fd, EVIOCSFF, &mut effect as *mut FfEffect as *mut libc::c_void)
        .map(|_| effect.id)
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
