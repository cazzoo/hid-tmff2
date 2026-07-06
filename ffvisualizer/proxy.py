"""The FF passthrough proxy + read-only observer.

Two modes (see PLAN.md §3):
  * proxy   — create a virtual uinput device mirroring the real one, intercept
              all FF requests, forward everything both ways, and feed the force
              model. The game must target the virtual device.
  * observe — open the real device read-only, show wheel position/velocity and
              play/stop/gain events. No effect parameters (not observable).

Runs in a background thread; `sample()` returns the latest model snapshot for
the websocket server.
"""
from __future__ import annotations

import os
import errno
import ctypes
import time
import select
import struct
import logging
import threading

import evdev
from evdev import ecodes

import uinput_ff
from force_model import (ForceModel, FF_GAIN, FF_AUTOCENTER,
                         TYPE_NAMES, WAVE_NAMES)

log = logging.getLogger("ffb.proxy")

# input_event struct: {timeval(2 longs), __u16 type, __u16 code, __s32 value}.
# Native size is arch-dependent: 16 bytes on 32-bit, 24 bytes on 64-bit
# (longs are 8 bytes). The format below tracks this automatically.
EV_FMT = "@llHHi"
EV_SIZE = struct.calcsize(EV_FMT)
assert EV_SIZE in (16, 24), f"unexpected input_event size {EV_SIZE}"

EV_ABS, EV_KEY, EV_SYN, EV_FF, EV_UINPUT = (
    ecodes.EV_ABS, ecodes.EV_KEY, ecodes.EV_SYN, ecodes.EV_FF, ecodes.EV_UINPUT)
UI_FF_UPLOAD = getattr(ecodes, "UI_FF_UPLOAD", 1)
UI_FF_ERASE = getattr(ecodes, "UI_FF_ERASE", 2)


def _ff_capable(device: evdev.InputDevice) -> bool:
    caps = device.capabilities(absinfo=False)
    ff = caps.get(ecodes.EV_FF)
    return bool(ff)


def list_ffb_devices() -> list[dict]:
    out = []
    for path in evdev.list_devices():
        try:
            d = evdev.InputDevice(path)
        except OSError:
            continue
        if _ff_capable(d):
            out.append({"path": path, "name": d.name, "phys": d.phys})
    return out


class ProxyBase:
    """Common: model + sampling, position normalization, FF axis detection."""

    def __init__(self, real_path: str):
        self.real_path = real_path
        self.model = ForceModel()
        self.ff_axis = ecodes.ABS_X  # detected below
        self.axis_min = 0
        self.axis_max = 65535
        self._lock = threading.Lock()
        self._last = None
        self._stop = threading.Event()
        self.virtual_path = None  # set by Proxy mode
        self.mode = "observe"

    def stop(self) -> None:
        self._stop.set()

    def _detect_axis(self, dev: evdev.InputDevice) -> None:
        caps = dev.capabilities()
        absinfo = caps.get(EV_ABS, [])
        # Prefer a steering axis: ABS_RX > ABS_X > first abs.
        for code in (ecodes.ABS_RX, ecodes.ABS_X, ecodes.ABS_WHEEL):
            for axcode, ai in absinfo:
                if axcode == code:
                    self.ff_axis = code
                    self.axis_min, self.axis_max = ai.min, ai.max
                    return
        if absinfo:
            axcode, ai = absinfo[0]
            self.ff_axis = axcode
            self.axis_min, self.axis_max = ai.min, ai.max

    def _normalize_axis(self, value: int) -> float:
        span = max(1, self.axis_max - self.axis_min)
        mid = (self.axis_max + self.axis_min) / 2.0
        return (value - mid) / (span / 2.0)

    def _handle_real_event(self, ev) -> None:
        """Process one event read from the REAL device."""
        if ev.type == EV_ABS and ev.code == self.ff_axis:
            self.model.update_position(self._normalize_axis(ev.value))

    def sample(self) -> dict:
        with self._lock:
            snap = self.model.sample()
            snap["mode"] = self.mode
            snap["virtual_path"] = self.virtual_path
            snap["device"] = os.path.basename(self.real_path)
            self._last = snap
            return snap


class Observer(ProxyBase):
    """Read-only mode. Sees position + EV_FF play/stop/gain (no params)."""

    def __init__(self, real_path: str):
        super().__init__(real_path)
        self.mode = "observe"

    def run(self) -> None:
        try:
            dev = evdev.InputDevice(self.real_path)
        except OSError as exc:
            log.error("cannot open %s read-only: %s", self.real_path, exc)
            return
        self._detect_axis(dev)
        log.info("observe: %s (%s), axis %s", dev.name, self.real_path,
                 ecodes.ABS[self.ff_axis])
        fd = dev.fileno()
        while not self._stop.is_set():
            r, _, _ = select.select([fd], [], [], 0.1)
            if not r:
                continue
            try:
                for ev in dev.read():
                    self._handle_real_event(ev)
                    if ev.type == EV_FF:
                        self._handle_ff(ev)
            except BlockingIOError:
                continue
            except OSError as exc:
                log.warning("observe read error: %s", exc)
                break

    def _handle_ff(self, ev) -> None:
        # EV_FF: code = effect id (or FF_GAIN/FF_AUTOCENTER), value = repeat/level
        if ev.code == FF_GAIN:
            self.model.set_gain(ev.value)
        elif ev.code == FF_AUTOCENTER:
            self.model.set_autocenter(ev.value)
        elif ev.value > 0:
            # We know an effect is playing but not its parameters (observe mode).
            self.model.play(ev.code, ev.value)
        else:
            self.model.stop(ev.code)


class Proxy(ProxyBase):
    """Full passthrough proxy via uinput."""

    def __init__(self, real_path: str):
        super().__init__(real_path)
        self.mode = "proxy"
        self._real_fd = None
        self._ui_fd = None
        self._uinput = None
        self._real_dev = None
        self._virt_dev = None       # evdev.InputDevice for the virtual node
        self._virt_ev_fd = None
        # Maps virtual effect id <-> real effect id (assigned by the real wheel).
        self._id_map = {}

    def _setup_real(self) -> None:
        self._real_dev = evdev.InputDevice(self.real_path)
        self._real_fd = self._real_dev.fileno()
        self._detect_axis(self._real_dev)

    def _setup_virtual(self) -> None:
        """Create the virtual uinput device mirroring the real wheel.

        Mirroring InputDevice.capabilities() verbatim is unsafe: it includes
        EV_SYN (which uinput cannot set — no UI_SET_SYNBIT, yields EINVAL) and
        may include exotic/unknown codes. Sanitize the capability set, and if
        construction still fails, bisect by event type (skip whichever type the
        kernel rejects) so the proxy always comes up.
        """
        caps = self._sanitize_caps(self._real_dev.capabilities(absinfo=True))
        self._uinput = self._build_uinput(caps)
        self._ui_fd = self._uinput.fd
        self.virtual_path = self._device_node(self._uinput)
        # The virtual device's own event node is where the game writes EV_FF
        # play/stop/gain events; we read them here to forward + visualize.
        self._virt_dev = self._uinput.device
        self._virt_ev_fd = self._virt_dev.fileno()
        log.info("proxy: virtual device at %s", self.virtual_path)
        log.info(">>> Point your game at: %s <<<", self.virtual_path)

    @staticmethod
    def _device_node(ui) -> str:
        """Resolve the created virtual device's /dev/input/eventN path.

        evdev's UInput exposes `.device` (a lazily-resolved InputDevice) whose
        `.path` is the event node. There is NO `devicenode()` method (that was
        the original crash); `devnode` is the /dev/uinput control fd, the wrong
        thing. Fall back to scanning /dev/input by name if `.device` fails.
        """
        try:
            return ui.device.path
        except Exception:
            pass
        target = ui.name
        for path in evdev.list_devices():
            try:
                d = evdev.InputDevice(path)
            except OSError:
                continue
            if d.name == target:
                return path
        return "/dev/input/event? (virtual created; run " \
               "'cat /proc/bus/input/devices' to find it)"

    @staticmethod
    def _sanitize_caps(caps: dict) -> dict:
        # Event types uinput can actually set (everything else, e.g. EV_SYN,
        # EV_REP, is rejected with EINVAL).
        SETTABLE = (EV_KEY, ecodes.EV_ABS, ecodes.EV_MSC, ecodes.EV_LED,
                    ecodes.EV_SND, ecodes.EV_SW, EV_FF)
        out = {}
        for etype, codes in caps.items():
            if etype not in SETTABLE:
                log.debug("sanitize: dropping unsettable type %d", etype)
                continue
            if etype == EV_FF:
                # Keep only known, in-range FF effect codes.
                codes = [c for c in codes if 0 <= c < ecodes.FF_CNT]
            out[etype] = codes
        return out

    def _build_uinput(self, caps: dict):
        try:
            return evdev.UInput(events=caps, name="FFB Visualizer Proxy",
                                vendor=0x0001, product=0x0001)
        except OSError as exc:
            log.warning("full UInput build failed (%s); bisecting event types",
                        exc)
            return self._build_uinput_bisect(caps)

    def _build_uinput_bisect(self, caps: dict):
        """Fallback when the full capability set is rejected: try constructing
        UInput with each event type removed in turn, logging what's dropped,
        until one succeeds. Guarantees the proxy comes up even if a single
        exotic capability is the culprit. (EV_ABS absinfo is preserved.)
        """
        if not caps:
            # Last resort: a minimal device with just keys + FF.
            return evdev.UInput(name="FFB Visualizer Proxy",
                                vendor=0x0001, product=0x0001)
        items = list(caps.items())
        for drop in items:
            reduced = dict(caps)
            del reduced[drop[0]]
            try:
                ui = evdev.UInput(events=reduced, name="FFB Visualizer Proxy",
                                  vendor=0x0001, product=0x0001)
                log.warning("bisect: dropped event type %d (codes %r) to build",
                            drop[0], drop[1])
                return ui
            except OSError:
                continue
        # All single drops failed: recurse on the reduced set.
        return self._build_uinput_bisect({})

    def run(self) -> None:
        try:
            self._setup_real()
            self._setup_virtual()
        except OSError as exc:
            log.error("proxy setup failed: %s (errno %d)", exc,
                      getattr(exc, "errno", -1))
            if getattr(exc, "errno", None) in (13, 1):
                log.error("need read+write on %s and /dev/uinput (run as root)",
                          self.real_path)
            else:
                log.error("the uinput layer rejected the virtual device; "
                          "see the bisect warnings above. As a fallback, run "
                          "with --observe (no FF parameter capture).")
            return

        fds = [self._real_fd, self._ui_fd, self._virt_ev_fd]
        while not self._stop.is_set():
            r, _, _ = select.select(fds, [], [], 0.05)
            for fd in r:
                if fd == self._real_fd:
                    self._pump_real()
                elif fd == self._ui_fd:
                    self._pump_virtual()
                elif fd == self._virt_ev_fd:
                    self._pump_virtual_events()

    # --- real -> virtual (axes/buttons) ---
    def _pump_real(self) -> None:
        try:
            for ev in self._real_dev.read():
                self._handle_real_event(ev)
                # Forward axis/key/sync events into the virtual device so the
                # game sees the wheel moving.
                if ev.type in (EV_ABS, EV_KEY, EV_SYN):
                    self._uinput.write(ev.type, ev.code, ev.value)
        except BlockingIOError:
            pass
        except OSError as exc:
            log.warning("real device read error: %s", exc)

    # --- virtual uinput fd: FF upload/erase interception ---
    # Per <linux/uinput.h>, FF requests arrive as EV_UINPUT input events on the
    # uinput control fd: an event with type EV_UINPUT(0x04), code UI_FF_UPLOAD
    # (or UI_FF_ERASE), and the kernel-assigned request id in `value`. We then
    # call UI_BEGIN_FF_UPLOAD / UI_BEGIN_FF_ERASE with that request id to fetch
    # the effect/erase parameters.
    def _pump_virtual(self) -> None:
        try:
            data = os.read(self._ui_fd, 4096)
        except BlockingIOError:
            return
        except OSError as exc:
            log.warning("uinput read error: %s", exc)
            return
        if not data:
            return
        n = len(data) // EV_SIZE
        for i in range(n):
            chunk = data[i * EV_SIZE:(i + 1) * EV_SIZE]
            sec, usec, etype, code, value = struct.unpack(EV_FMT, chunk)
            self._dispatch_virtual(etype, code, value)

    def _dispatch_virtual(self, etype, code, value) -> None:
        if etype != EV_UINPUT:
            return
        if code == UI_FF_UPLOAD:
            self._handle_upload(value)
        elif code == UI_FF_ERASE:
            self._handle_erase(value)

    def _handle_upload(self, request_id: int) -> None:
        try:
            u = uinput_ff.begin_ff_upload(self._ui_fd, request_id)
        except OSError as exc:
            log.warning("begin_ff_upload failed: %s", exc)
            return
        v_id = int(u.effect.id)
        eff_copy = uinput_ff.ff_effect()
        ctypes.memmove(ctypes.addressof(eff_copy), ctypes.addressof(u.effect),
                       ctypes.sizeof(u.effect))
        # Allocate a fresh id on the real device; never reuse the virtual id
        # (which is >= 0 and would make EVIOCSFF try to replace a non-existent
        # effect and return EINVAL).
        eff_copy.id = -1
        try:
            real_id = uinput_ff.upload_effect(self._real_fd, eff_copy)
        except OSError as exc:
            log.warning("real upload failed: %s", exc)
            u.retval = -exc.errno if exc.errno else -1
            uinput_ff.end_ff_upload(self._ui_fd, u)
            return
        self._id_map[v_id] = real_id
        self._id_map[real_id] = v_id
        edict = uinput_ff.effect_to_dict(eff_copy)
        edict["id"] = real_id
        with self._lock:
            self.model.set_effect(real_id, edict)
        u.retval = 0
        # Keep u.effect.id == v_id so the game references its own virtual id.
        uinput_ff.end_ff_upload(self._ui_fd, u)
        log.debug("upload v_id %d -> real id %d (type %s)", v_id, real_id,
                  TYPE_NAMES.get(edict["type"], hex(edict["type"])))

    def _handle_erase(self, request_id: int) -> None:
        try:
            e = uinput_ff.begin_ff_erase(self._ui_fd, request_id)
        except OSError as exc:
            log.warning("begin_ff_erase failed: %s", exc)
            return
        v_id = int(e.effect_id)
        real_id = self._id_map.pop(v_id, None)
        if real_id is not None:
            self._id_map.pop(real_id, None)
            try:
                uinput_ff.erase_effect(self._real_fd, real_id)
            except OSError as exc:
                log.warning("real erase failed: %s", exc)
            with self._lock:
                self.model.erase_effect(real_id)
        e.retval = 0
        uinput_ff.end_ff_erase(self._ui_fd, e)

    # --- virtual event node: EV_FF play/stop/gain from the game ---
    def _pump_virtual_events(self) -> None:
        try:
            for ev in self._virt_dev.read():
                if ev.type == EV_FF:
                    self._handle_play_or_cmd(ev.code, ev.value)
        except BlockingIOError:
            pass
        except OSError as exc:
            log.warning("virtual device read error: %s", exc)

    def _handle_play_or_cmd(self, code, value) -> None:
        if code == FF_GAIN:
            self.model.set_gain(value)
            # Forward to the real device as an EV_FF event.
            self._write_real_ff(FF_GAIN, value)
            return
        if code == FF_AUTOCENTER:
            self.model.set_autocenter(value)
            self._write_real_ff(FF_AUTOCENTER, value)
            return
        # Normal play/stop: translate the virtual id the game used into the
        # real device's id before forwarding + visualizing.
        real_id = self._id_map.get(code, code)
        with self._lock:
            if value > 0:
                self.model.play(real_id, value)
            else:
                self.model.stop(real_id)
        self._write_real_ff(real_id, value)

    def _write_real_ff(self, code, value) -> None:
        ev = struct.pack(EV_FMT, int(time.time()), 0, EV_FF, code, value)
        try:
            os.write(self._real_fd, ev)
        except OSError as exc:
            log.warning("forward EV_FF to real failed: %s", exc)


def make_proxy(real_path: str, observe: bool) -> ProxyBase:
    if observe:
        return Observer(real_path)
    return Proxy(real_path)
