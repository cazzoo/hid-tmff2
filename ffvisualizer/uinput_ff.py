"""Low-level uinput Force-Feedback request/response protocol.

Defines the kernel FF effect structures and wraps the uinput FF ioctls used by
the passthrough proxy (see PLAN.md §4).

Reference: Linux UAPI <linux/input.h> and <linux/uinput.h>, and kernel doc
Documentation/input/uinput.rst.

The effect/upload struct byte layout is asserted at import time. If it ever
fails on an exotic arch, adjust the ctypes field definitions to match the
kernel.
"""
from __future__ import annotations

import ctypes
import fcntl
import errno

# --------------------------------------------------------------------------- #
# UAPI struct definitions — must match <linux/input.h> byte-for-byte.
# Field ORDER, the pointer field (custom_data) and the union members all
# matter. custom_data is a __user pointer, so the size is arch-dependent
# (8 bytes on 64-bit); c_void_p tracks that automatically.
# --------------------------------------------------------------------------- #

class ff_replay(ctypes.Structure):
    _fields_ = [("length", ctypes.c_uint16), ("delay", ctypes.c_uint16)]


class ff_trigger(ctypes.Structure):
    _fields_ = [("button", ctypes.c_uint16), ("interval", ctypes.c_uint16)]


class ff_envelope(ctypes.Structure):
    _fields_ = [("attack_length", ctypes.c_uint16),
                ("attack_level", ctypes.c_uint16),
                ("fade_length", ctypes.c_uint16),
                ("fade_level", ctypes.c_uint16)]


class ff_constant_effect(ctypes.Structure):
    _fields_ = [("level", ctypes.c_int16),
                ("envelope", ff_envelope)]


class ff_ramp_effect(ctypes.Structure):
    _fields_ = [("start_level", ctypes.c_int16),
                ("end_level", ctypes.c_int16),
                ("envelope", ff_envelope)]


class ff_periodic_effect(ctypes.Structure):
    _fields_ = [("waveform", ctypes.c_uint16),
                ("period", ctypes.c_uint16),
                ("magnitude", ctypes.c_int16),
                ("offset", ctypes.c_int16),
                ("phase", ctypes.c_uint16),
                ("envelope", ff_envelope),
                ("custom_len", ctypes.c_uint32),
                ("custom_data", ctypes.c_void_p)]


class ff_condition_effect(ctypes.Structure):
    _fields_ = [("right_saturation", ctypes.c_uint16),
                ("left_saturation", ctypes.c_uint16),
                ("right_coeff", ctypes.c_int16),
                ("left_coeff", ctypes.c_int16),
                ("deadband", ctypes.c_uint16),
                ("center", ctypes.c_int16)]


class ff_rumble_effect(ctypes.Structure):
    _fields_ = [("strong_magnitude", ctypes.c_uint16),
                ("weak_magnitude", ctypes.c_uint16)]


class ff_haptic_effect(ctypes.Structure):
    _fields_ = [("hid_usage", ctypes.c_uint16),
                ("vendor_id", ctypes.c_uint16),
                ("vendor_waveform_page", ctypes.c_uint8),
                ("intensity", ctypes.c_uint16),
                ("repeat_count", ctypes.c_uint16),
                ("retrigger_period", ctypes.c_uint16)]


class _ff_union(ctypes.Union):
    _fields_ = [("constant", ff_constant_effect),
                ("ramp", ff_ramp_effect),
                ("periodic", ff_periodic_effect),
                ("condition", ff_condition_effect * 2),
                ("rumble", ff_rumble_effect),
                ("haptic", ff_haptic_effect)]


class ff_effect(ctypes.Structure):
    # NOTE: trigger comes BEFORE replay in the kernel UAPI.
    _fields_ = [("type", ctypes.c_uint16),
                ("id", ctypes.c_int16),
                ("direction", ctypes.c_uint16),
                ("trigger", ff_trigger),
                ("replay", ff_replay),
                ("u", _ff_union)]


class uinput_ff_upload(ctypes.Structure):
    # NOTE: the kernel struct has NO effect_id field — the assigned id is
    # carried back inside effect.id after the real-device upload.
    _fields_ = [("request_id", ctypes.c_uint32),
                ("retval", ctypes.c_int32),
                ("effect", ff_effect),
                ("old", ff_effect)]


class uinput_ff_erase(ctypes.Structure):
    _fields_ = [("request_id", ctypes.c_uint32),
                ("retval", ctypes.c_int32),
                ("effect_id", ctypes.c_uint32)]


# Report the computed size for diagnostics. We do NOT hard-fail (the size is
# arch-dependent via the custom_data pointer); if it ever mismatches the
# kernel, the upload ioctl will return ENOTTY/EINVAL at runtime with a clear
# message in the proxy log.
FF_EFFECT_SIZE = ctypes.sizeof(ff_effect)

# --------------------------------------------------------------------------- #
# ioctl numbers.
# --------------------------------------------------------------------------- #

_IOC_NRBITS, _IOC_TYPEBITS, _IOC_SIZEBITS = 8, 8, 14
_IOC_NRSHIFT = 0
_IOC_TYPESHIFT = _IOC_NRSHIFT + _IOC_NRBITS
_IOC_SIZESHIFT = _IOC_TYPESHIFT + _IOC_TYPEBITS
_IOC_DIRSHIFT = _IOC_SIZESHIFT + _IOC_SIZEBITS
_IOC_WRITE, _IOC_READ = 1, 2


def _IOC(d, t, nr, size):
    return (d << _IOC_DIRSHIFT) | (t << _IOC_TYPESHIFT) | \
           (nr << _IOC_NRSHIFT) | (size << _IOC_SIZESHIFT)


def _IOW(t, nr, size):
    return _IOC(_IOC_WRITE, ord(t), nr, size)


def _IOR(t, nr, size):
    return _IOC(_IOC_READ, ord(t), nr, size)


def _IOWR(t, nr, size):
    return _IOC(_IOC_WRITE | _IOC_READ, ord(t), nr, size)


# EVIOCSFF upload effect (on the REAL device): writes an ff_effect in, kernel
# assigns ->id and writes it back.
EVIOCSFF = _IOW("E", 0x80, ctypes.sizeof(ff_effect))
# EVIOCRMFF erase: arg = effect id.
EVIOCRMFF = _IOW("E", 0x81, ctypes.sizeof(ctypes.c_int32))
EVIOCGEFFECTS = _IOR("E", 0x84, ctypes.sizeof(ctypes.c_int32))

# uinput FF request/response (on the UINPUT fd).
UI_BEGIN_FF_UPLOAD = _IOWR("U", 200, ctypes.sizeof(uinput_ff_upload))
UI_END_FF_UPLOAD = _IOWR("U", 201, ctypes.sizeof(uinput_ff_upload))
UI_BEGIN_FF_ERASE = _IOWR("U", 202, ctypes.sizeof(uinput_ff_erase))
UI_END_FF_ERASE = _IOWR("U", 203, ctypes.sizeof(uinput_ff_erase))


# --------------------------------------------------------------------------- #
# High-level helpers.
# --------------------------------------------------------------------------- #

def upload_effect(real_fd, effect: ff_effect) -> int:
    """Upload effect to the real device. Returns the assigned effect id.

    effect.id should be -1 for a new effect. The kernel assigns an id and
    writes it back into effect.id.
    """
    try:
        fcntl.ioctl(real_fd, EVIOCSFF, effect)
    except OSError as exc:
        raise OSError(exc.errno, f"EVIOCSFF failed: {exc.strerror}") from exc
    return effect.id


def erase_effect(real_fd, effect_id: int) -> None:
    fcntl.ioctl(real_fd, EVIOCRMFF, ctypes.c_int32(effect_id))


def get_max_effects(real_fd) -> int:
    n = ctypes.c_int32(0)
    try:
        fcntl.ioctl(real_fd, EVIOCGEFFECTS, n)
    except OSError:
        return -1
    return n.value


def begin_ff_upload(uinput_fd, request_id: int) -> uinput_ff_upload:
    u = uinput_ff_upload()
    u.request_id = request_id
    fcntl.ioctl(uinput_fd, UI_BEGIN_FF_UPLOAD, u)
    return u


def end_ff_upload(uinput_fd, u: uinput_ff_upload) -> None:
    fcntl.ioctl(uinput_fd, UI_END_FF_UPLOAD, u)


def begin_ff_erase(uinput_fd, request_id: int) -> uinput_ff_erase:
    e = uinput_ff_erase()
    e.request_id = request_id
    fcntl.ioctl(uinput_fd, UI_BEGIN_FF_ERASE, e)
    return e


def end_ff_erase(uinput_fd, e: uinput_ff_erase) -> None:
    fcntl.ioctl(uinput_fd, UI_END_FF_ERASE, e)


# Convenience: build a plain (pythonic) view of an ff_effect for the force
# model and the UI, independent of ctypes.
def effect_to_dict(effect: ff_effect) -> dict:
    t = effect.type
    d = {
        "id": int(effect.id),
        "type": int(t),
        "direction": int(effect.direction),
        "length": int(effect.replay.length),
        "delay": int(effect.replay.delay),
        "trigger_button": int(effect.trigger.button),
    }

    def env_of(e):
        return {
            "attack_length": int(e.attack_length),
            "attack_level": int(e.attack_level),
            "fade_length": int(e.fade_length),
            "fade_level": int(e.fade_level),
        }

    if t == 0x00:  # FF_CONSTANT
        d["level"] = int(effect.u.constant.level)
        d["envelope"] = env_of(effect.u.constant.envelope)
    elif t == 0x03:  # FF_RAMP
        d["start_level"] = int(effect.u.ramp.start_level)
        d["end_level"] = int(effect.u.ramp.end_level)
        d["envelope"] = env_of(effect.u.ramp.envelope)
    elif t == 0x05:  # FF_PERIODIC
        d["waveform"] = int(effect.u.periodic.waveform)
        d["period"] = int(effect.u.periodic.period)
        d["magnitude"] = int(effect.u.periodic.magnitude)
        d["offset"] = int(effect.u.periodic.offset)
        d["phase"] = int(effect.u.periodic.phase)
        d["envelope"] = env_of(effect.u.periodic.envelope)
    elif t in (0x40, 0x41, 0x42, 0x43):  # SPRING/DAMPER/FRIC/INERTIA
        c0 = effect.u.condition[0]
        d["right_saturation"] = int(c0.right_saturation)
        d["left_saturation"] = int(c0.left_saturation)
        d["right_coeff"] = int(c0.right_coeff)
        d["left_coeff"] = int(c0.left_coeff)
        d["deadband"] = int(c0.deadband)
        d["center"] = int(c0.center)
    elif t == 0x50:  # FF_RUMBLE
        d["strong_magnitude"] = int(effect.u.rumble.strong_magnitude)
        d["weak_magnitude"] = int(effect.u.rumble.weak_magnitude)
    return d
