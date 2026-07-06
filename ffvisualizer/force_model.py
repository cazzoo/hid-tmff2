"""Force model: turns uploaded effect parameters + play state into an
instantaneous net torque along the wheel axis for visualization.

This models the *intent* of the FF commands (the same math the kernel
ff-memless uses), NOT a measurement of motor current. See PLAN.md §5.
"""
from __future__ import annotations

import math
import time
from dataclasses import dataclass, field

# FF effect type codes (linux/input.h).
FF_CONSTANT = 0x00
FF_RAMP = 0x03
FF_SPRING = 0x40
FF_FRICTION = 0x41
FF_DAMPER = 0x42
FF_INERTIA = 0x43
FF_RUMBLE = 0x50
FF_PERIODIC = 0x05

# Periodic waveforms (linux/input.h).
FF_SQUARE = 0x58
FF_TRIANGLE = 0x59
FF_SINE = 0x5A
FF_SAW_UP = 0x5B
FF_SAW_DOWN = 0x5C

# FF "command" events delivered as EV_FF.
FF_GAIN = 0x60
FF_AUTOCENTER = 0x61

TYPE_NAMES = {
    FF_CONSTANT: "Constant",
    FF_PERIODIC: "Periodic",
    FF_RAMP: "Ramp",
    FF_SPRING: "Spring",
    FF_DAMPER: "Damper",
    FF_FRICTION: "Friction",
    FF_INERTIA: "Inertia",
    FF_RUMBLE: "Rumble",
}

WAVE_NAMES = {
    FF_SQUARE: "Square", FF_TRIANGLE: "Triangle", FF_SINE: "Sine",
    FF_SAW_UP: "Saw Up", FF_SAW_DOWN: "Saw Down",
}

MAX_LEVEL = 32767.0  # |level| / |magnitude| / saturation reference.


def dir_proj(direction: int) -> float:
    """Project a magnitude onto the wheel's signed axis.

    direction is the Linux FF heading (0..0xFFFF). math.sin/cos take RADIANS
    (the kernel's fixp_sin16 takes degrees — do not mix them).

    Visualization convention (an FFB scope): direction 0 (north) = full
    positive force, 0x8000 (south) = full negative force, via cos. This is
    the intuitive left/right reading users expect from a monitor. Note this
    differs from some drivers (e.g. tmff2) which project onto the east axis
    with sin; here we choose the human-intuitive mapping for display.
    """
    return math.cos(direction * 2.0 * math.pi / 0x10000)


def _norm(v: float) -> float:
    return max(-1.0, min(1.0, v / MAX_LEVEL))


def _wave(waveform: int, ph: float) -> float:
    """Periodic waveform value in [-1, 1] for phase ph in [0,1)."""
    if waveform == FF_SINE:
        return math.sin(2 * math.pi * ph)
    if waveform == FF_SQUARE:
        return 1.0 if (ph % 1.0) < 0.5 else -1.0
    if waveform == FF_TRIANGLE:
        p = ph % 1.0
        return 4 * p - 1 if p < 0.5 else 3 - 4 * p
    if waveform == FF_SAW_UP:
        return 2 * (ph % 1.0) - 1
    if waveform == FF_SAW_DOWN:
        return 1 - 2 * (ph % 1.0)
    return math.sin(2 * math.pi * ph)


def _envelope_factor(env: dict, age_ms: float, length_ms: float) -> float:
    """Attack/fade scaling in [0,1] applied to a magnitude over time."""
    al = env.get("attack_length", 0) or 0
    alev = (env.get("attack_level", 0) or 0) / MAX_LEVEL
    fl = env.get("fade_length", 0) or 0
    flev = (env.get("fade_level", 0) or 0) / MAX_LEVEL
    if al > 0 and age_ms < al:
        f = age_ms / al
        return flev if False else f  # attack from 0? use level interp
    # Simpler, robust model: ramp attack from attack_level to 1, fade from 1
    # to fade_level near the end.
    mag = 1.0
    if al > 0 and age_ms < al:
        mag = alev + (1.0 - alev) * (age_ms / al)
    if fl > 0 and length_ms > 0:
        remain = length_ms - age_ms
        if 0 <= remain < fl:
            mag *= flev + (1.0 - flev) * (remain / fl)
    return max(0.0, min(1.0, mag))


@dataclass
class Playing:
    effect_id: int
    repeat: int          # requested repeat count (0 = until stopped)
    value: int           # last EV_FF value
    start: float         = field(default_factory=time.monotonic)
    last_value_ms: float = 0.0  # for periodic phase tracking


@dataclass
class ForceModel:
    effects: dict = field(default_factory=dict)   # id -> effect dict
    playing: dict = field(default_factory=dict)   # id -> Playing
    gain: float = 1.0
    autocenter: float = 0.0
    position: float = 0.0      # normalized -1..1
    velocity: float = 0.0      # normalized -1..1 /s (approx)
    _last_pos: float = 0.0
    _last_pos_t: float = 0.0

    # --- mutators (called by the proxy) ---
    def set_effect(self, effect_id: int, edict: dict) -> None:
        edict = dict(edict)
        edict["id"] = effect_id
        self.effects[effect_id] = edict

    def erase_effect(self, effect_id: int) -> None:
        self.effects.pop(effect_id, None)
        self.playing.pop(effect_id, None)

    def play(self, effect_id: int, value: int) -> None:
        if value <= 0:
            self.playing.pop(effect_id, None)
            return
        prev = self.playing.get(effect_id)
        self.playing[effect_id] = Playing(
            effect_id=effect_id, repeat=value if value < 0xFFFF else 0,
            value=value)

    def stop(self, effect_id: int) -> None:
        self.playing.pop(effect_id, None)

    def set_gain(self, value: int) -> None:
        self.gain = max(0.0, min(1.0, value / 0xFFFF))

    def set_autocenter(self, value: int) -> None:
        self.autocenter = max(0.0, min(1.0, value / 0xFFFF))

    def update_position(self, normalized: float) -> None:
        now = time.monotonic()
        if self._last_pos_t:
            dt = max(1e-3, now - self._last_pos_t)
            # velocity in normalized units/sec; clamp to a sane range.
            self.velocity = max(-2.0, min(2.0,
                                (normalized - self._last_pos) / dt))
        self._last_pos = normalized
        self._last_pos_t = now
        self.position = max(-1.0, min(1.0, normalized))

    # --- evaluation ---
    def _contribution(self, edict: dict, age_ms: float, p: Playing) -> float:
        """Signed, normalized (-1..1) contribution of one playing effect."""
        t = edict.get("type")
        length = edict.get("length", 0) or 0
        delay = edict.get("delay", 0) or 0
        env = edict.get("envelope", {})
        eff_len = length if length else 0xFFFF
        # Within the delay window the effect has not started yet.
        if age_ms < delay:
            return 0.0

        if t == FF_CONSTANT:
            return _norm(edict.get("level", 0)) * dir_proj(edict["direction"])

        if t == FF_PERIODIC:
            period = edict.get("period", 0) or 0
            mag = _norm(edict.get("magnitude", 0))
            off = _norm(edict.get("offset", 0))
            phase0 = (edict.get("phase", 0) or 0) / 36000.0  # 0..35999 -> 0..1
            ph = phase0
            if period > 0:
                ph = (phase0 + (age_ms - delay) / period) % 1.0
            wave = _wave(edict.get("waveform", FF_SINE), ph)
            envf = _envelope_factor(env, age_ms - delay, eff_len)
            return (off + mag * wave * envf) * dir_proj(edict["direction"])

        if t == FF_RAMP:
            s = _norm(edict.get("start_level", 0))
            e = _norm(edict.get("end_level", 0))
            frac = (age_ms - delay) / eff_len if eff_len else 1.0
            frac = max(0.0, min(1.0, frac))
            envf = _envelope_factor(env, age_ms - delay, eff_len)
            return (s + (e - s) * frac) * envf * dir_proj(edict["direction"])

        if t == FF_SPRING:
            center = _norm(edict.get("center", 0))
            deadband = _norm(edict.get("deadband", 0))
            err = (center - self.position)
            if abs(err) < deadband:
                return 0.0
            coeff = _norm(edict.get("right_coeff", 0)) if err > 0 \
                else _norm(edict.get("left_coeff", 0))
            sat = edict.get("right_saturation", MAX_LEVEL) / MAX_LEVEL if err > 0 \
                else edict.get("left_saturation", MAX_LEVEL) / MAX_LEVEL
            return max(-sat, min(sat, coeff * (err - deadband)))

        if t == FF_DAMPER:
            coeff = _norm(edict.get("right_coeff", 0))
            sat = edict.get("right_saturation", MAX_LEVEL) / MAX_LEVEL
            return max(-sat, min(sat, -coeff * self.velocity))

        if t == FF_FRICTION:
            # Coulomb-style: force opposes motion, magnitude = coeff, clamped.
            coeff = _norm(edict.get("right_coeff", 0))
            sat = edict.get("right_saturation", MAX_LEVEL) / MAX_LEVEL
            if abs(self.velocity) < 0.01:
                return 0.0
            return max(-sat, min(sat, -coeff * math.copysign(1.0, self.velocity)))

        if t == FF_INERTIA:
            coeff = _norm(edict.get("right_coeff", 0))
            sat = edict.get("right_saturation", MAX_LEVEL) / MAX_LEVEL
            # Acceleration unknown; approximate inertia as opposing velocity
            # change — fall back to a mild velocity term.
            return max(-sat, min(sat, -0.5 * coeff * self.velocity))

        if t == FF_RUMBLE:
            # Combine strong+weak as a constant-ish vibration; rough.
            strong = _norm(edict.get("strong_magnitude", 0))
            weak = _norm(edict.get("weak_magnitude", 0))
            return (strong + weak * 0.5) * 0.5 * dir_proj(edict["direction"])

        return 0.0

    def sample(self) -> dict:
        """Compute the instantaneous state. Called ~60 Hz."""
        now = time.monotonic()
        comps = []
        playing_list = []
        net = 0.0
        last_dir = 0.0
        for eid, p in list(self.playing.items()):
            edict = self.effects.get(eid)
            if not edict:
                continue
            age_ms = (now - p.start) * 1000.0
            length = edict.get("length", 0) or 0
            # Auto-expire non-infinite effects whose single iteration elapsed.
            if length and age_ms > length + (edict.get("delay", 0) or 0):
                if p.repeat and p.repeat != 0xFFFF:
                    # Single-shot for simplicity in the model.
                    self.playing.pop(eid, None)
                    continue
            c = self._contribution(edict, age_ms, p)
            net += c
            t = edict.get("type")
            last_dir = edict.get("direction", 0)
            comps.append({
                "id": eid,
                "type": t,
                "name": TYPE_NAMES.get(t, hex(t)),
                "contrib": round(c, 4),
                "repeat": p.repeat,
            })
            playing_list.append({
                "id": eid,
                "type": t,
                "name": TYPE_NAMES.get(t, hex(t)),
                "direction": edict.get("direction", 0),
                "age_ms": round(age_ms),
                "length_ms": length,
                "repeat": p.repeat,
                "waveform": WAVE_NAMES.get(edict.get("waveform")),
                "level": edict.get("level"),
                "magnitude": edict.get("magnitude"),
                "start_level": edict.get("start_level"),
                "end_level": edict.get("end_level"),
                "center": edict.get("center"),
            })

        net = max(-1.0, min(1.0, net)) * self.gain
        return {
            "ts": time.time(),
            "torque": round(net, 4),
            "strength": round(abs(net), 4),
            "direction_deg": round(last_dir * 360.0 / 0x10000, 1),
            "position": round(self.position, 4),
            "velocity": round(self.velocity, 4),
            "gain": round(self.gain, 4),
            "autocenter": round(self.autocenter, 4),
            "components": comps,
            "playing": playing_list,
        }
