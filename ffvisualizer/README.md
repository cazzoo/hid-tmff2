# FFB Visualizer

A **driver-agnostic** real-time dashboard for Linux Force-Feedback wheels.
It visualizes everything the kernel FF subsystem applies to the wheel: a live
torque scope, direction, strength, the active effects with their parameters,
wheel position/velocity, and live-editable per-force-type gains.

It works with **any** wheel that exposes FFB through the standard Linux
input/evdev FF API — it is **not** tied to the `hid-tmff2` driver.

See `PLAN.md` for the full architecture.

---

## Quick start

```bash
cd ffvisualizer
./run.sh                 # proxy mode (needs sudo / RW on input + uinput)
# or
./run.sh --observe       # read-only, no root needed if you can read the device
```

Then open <http://127.0.0.1:8000/>.

### Proxy mode (full data)

The visualizer creates a **virtual device** that mirrors your wheel. Point your
**game at the virtual device** (its `/dev/input/eventN` node is printed at
startup and shown in the UI top bar). The visualizer intercepts every FF upload
and forwards it to the real wheel while displaying it.

```bash
sudo ./run.sh --device /dev/input/event12
# >>> Point your game at: /dev/input/event20
```

Requires: read+write on `/dev/input/eventN` and `/dev/uinput`. Either run as
root, or add your user to the `input` group and set a udev rule for `uinput`
(`KERNEL=="uinput", GROUP="input", MODE="0660"`).

### Observe mode (no setup)

Opens the real device read-only. Shows wheel position/velocity and the
play/stop/gain events, but **not** effect parameters (those are not observable
read-only — see `PLAN.md §2`). Great as a zero-config sanity check.

```bash
./run.sh --observe
```

---

## CLI options

```
--device /dev/input/eventN   pick a specific device (else auto-detected)
--observe                    read-only mode (no proxy, no effect params)
--host 0.0.0.0               bind host (default 127.0.0.1)
--port 8080                  bind port (default 8000)
-v                           verbose logging
```

## What the dashboard shows

- **Torque scope** — signed net force over ~15 s (blue), overlaid with wheel
  position (green) and gain (purple).
- **Strength** gauge, **Direction** bar (torque sign), **Net torque**,
  **Wheel position/velocity**.
- **Active effects** table — id, type, waveform, direction, level/magnitude,
  age, length, repeat, and each effect's modeled contribution.
- **Live gain settings** — `spring_level` / `damper_level` / `friction_level` /
  `gain` sliders, exposed generically via the driver's sysfs attributes. With
  `hid-tmff2` all four appear and are adjustable live; other drivers expose
  whatever they provide.

## Important: this is a *model*, not a measurement

The kernel computes forces inside the driver/device; motor current is not
readable from userspace. The torque shown is reconstructed from the effect
parameters (the same math the kernel `ff-memless` uses) — it represents the
**intent** of the FF commands, not a sensor reading of the motor.

## REST / WebSocket

- `GET  /ws` — pushes FF state JSON at ~60 Hz (see `PLAN.md §6`).
- `GET  /api/devices` — FFB-capable input devices.
- `GET  /api/gains` · `POST /api/gains` — read/set driver gain sysfs values.

## Files

| File            | Role |
|-----------------|------|
| `uinput_ff.py`  | ctypes `ff_effect` + uinput FF ioctl wrappers (the FF proxy protocol) |
| `force_model.py`| playing-state + instantaneous torque computation |
| `sysfs_gains.py`| driver-agnostic gain sysfs probe + live write |
| `proxy.py`      | real↔virtual forwarding, FF interception, proxy + observe modes |
| `server.py`     | aiohttp HTTP + WebSocket |
| `main.py`       | CLI entry |
| `web/`          | dashboard (vanilla JS, canvas scope, no build step) |

## Status / caveats

Not yet validated against a live FFB game session. The uinput FF handshake
(`UI_BEGIN/END_FF_UPLOAD`) and `ff_effect` layout are the critical-path items
that need on-hardware confirmation — see `PLAN.md §9`. `--observe` mode is safe
to test immediately.

## Troubleshooting

**`proxy setup failed: [Errno 22] Invalid argument`** — the uinput layer
rejected the virtual device. The proxy already strips unsettable capability
bits (`EV_SYN`, out-of-range FF codes) and falls back to building with a
reduced set; if it still fails, run with `sudo ./run.sh` (proxy mode needs RW
on `/dev/input/eventN` and `/dev/uinput`) or use `./run.sh --observe`.

**Permission errors after a `sudo` run** — if `__pycache__` / `.venv` got
root-owned, fix ownership:
```bash
sudo chown -R $USER:$USER ffvisualizer
```
`run.sh` now creates the venv as the invoking user and only elevates for the
proxy process itself.
