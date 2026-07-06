"""Driver-agnostic live gain settings via sysfs.

The `spring_level` / `damper_level` / `friction_level` / `gain` attributes are
driver-specific (e.g. the tmff2 driver exposes them as device attributes). This
module probes generically: given an input event device, it locates writable
sysfs attributes of known names and exposes/edits whatever exists. With tmff2
it finds all four; with another driver it finds whatever is available.
"""
from __future__ import annotations

import os
import glob

# Known attribute names and their value ranges/meaning.
KNOWN_GAINS = [
    # key,        display name,    min,  max,   unit
    ("spring_level",   "Spring force level",   0, 100, "%"),
    ("damper_level",   "Damper force level",   0, 100, "%"),
    ("friction_level", "Friction force level", 0, 100, "%"),
    ("gain",           "Master gain",          0, 65535, ""),
]


def _device_syspath(event_path: str) -> str:
    """Resolve /dev/input/eventN -> /sys/class/input/eventN."""
    name = os.path.basename(event_path)  # eventN
    sysp = os.path.join("/sys/class/input", name)
    if os.path.isdir(sysp):
        return sysp
    return ""


def _find_attr(syspath: str, attr: str) -> str:
    """Search the device syspath (and the bound input device subtree) for attr."""
    # Device attributes live directly under the event device syspath, or under
    # the parent input device. Try a few candidate locations.
    candidates = [
        os.path.join(syspath, attr),
        os.path.join(syspath, "device", attr),
        os.path.join(syspath, "device", "device", attr),
    ]
    # Also search the input device root and its parent (the HID/usb device).
    root = os.path.realpath(syspath)
    for _ in range(4):
        candidates.append(os.path.join(root, attr))
        root = os.path.dirname(root)
    for c in candidates:
        if os.path.isfile(c) and os.access(c, os.R_OK):
            return c
    # Fallback: glob the whole subtree (best-effort).
    for c in glob.glob(os.path.join(syspath, "**", attr), recursive=True):
        if os.access(c, os.R_OK):
            return c
    return ""


def probe(event_path: str) -> dict:
    """Return {key: {name, value, min, max, unit, path, writable}} for gains
    that exist on the device. Missing gains are simply omitted."""
    out = {}
    syspath = _device_syspath(event_path)
    if not syspath:
        return out
    for key, name, mn, mx, unit in KNOWN_GAINS:
        path = _find_attr(syspath, key)
        if not path:
            continue
        try:
            with open(path) as f:
                val = int(f.read().strip())
        except (OSError, ValueError):
            continue
        out[key] = {
            "name": name,
            "value": val,
            "min": mn,
            "max": mx,
            "unit": unit,
            "path": path,
            "writable": os.access(path, os.W_OK),
        }
    return out


def set_value(event_path: str, key: str, value: int) -> bool:
    gains = probe(event_path)
    info = gains.get(key)
    if not info or not info.get("writable"):
        return False
    value = max(info["min"], min(info["max"], value))
    try:
        with open(info["path"], "w") as f:
            f.write(str(value))
    except OSError:
        return False
    return True
