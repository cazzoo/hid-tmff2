# FFB Visualizer — Comprehensive Implementation Plan

A **driver-agnostic** real-time visualizer for Linux Force Feedback (FFB) wheels.
It is *not* tied to the `hid-tmff2` driver — it works with any wheel that
exposes FFB through the standard Linux input/evdev FF API.

---

## 1. Goal

Live-display, exhaustively, everything the kernel FF subsystem is applying to a
wheel: net torque over time (FFB scope graph), direction, instantaneous
strength, the list of currently-playing effects with their types/parameters,
wheel position & velocity, and live-editable per-force-type gains.

## 2. Why this requires a *proxy* (the core design decision)

The Linux FF API does **not** let a third party observe effect parameters:

| Operation | How the game issues it            | Observable to others? |
|-----------|-----------------------------------|------------------------|
| Upload    | `ioctl(EVIOCSFF, &ff_effect)`    | **No** — stored in kernel, not emitted |
| Play/Stop | `write(EV_FF event)`             | Yes — readable as input events |
| Gain      | `write(EV_FF FF_GAIN)`           | Yes — readable as input events |
| Erase     | `ioctl(EVIOCRMFF, id)`           | No |

So a read-only observer can see *that* effect #3 is playing, but never its
type/strength/direction. To display **exhaustive** FF data we must sit between
the game and the real device and intercept the upload ioctl. Hence the
architecture is a **uinput passthrough proxy**.

## 3. Architecture

```
                ┌─────────────┐   EV_ABS/EV_KEY    ┌──────────────┐
  REAL WHEEL ──▶│  real evdev │ ─────────────────▶ │  uinput dev  │──▶ GAME
  /dev/input/   │  (read fd)  │                    │  (virtual)   │
  eventN        └─────────────┘                    └──────────────┘
                       ▲                                  │ EV_FF (play/stop/gain)
                       │   EV_FF forwarded               │ EV_UINPUT (upload/erase)
                       │   via write()                   ▼
                       │                          ┌───────────────────────┐
                       └──────────────────────────│   FFB VISUALIZER      │
                                                  │  proxy + force model  │
                                                  │  aiohttp + websocket  │
                                                  └───────────┬───────────┘
                                                    HTTP/WS   │
                                                  ┌───────────▼───────────┐
                                                  │   Browser dashboard   │
                                                  │ scope / direction /   │
                                                  │ strength / effects /  │
                                                  │ gain sliders          │
                                                  └───────────────────────┘
```

**Data flow:**
1. The proxy opens the **real** wheel (`/dev/input/eventN`) read+write.
2. It creates a **virtual** device via `/dev/uinput` that mirrors the real
   device's capabilities (axes, buttons, FF bits, etc.).
3. The **game is pointed at the virtual device** (its new event node, shown in
   the UI). The user must select the virtual device in their game.
4. Real→virtual: axis/button events read from the real fd are **forwarded into
   the virtual device** so the game sees the wheel moving.
5. Virtual→real: FF requests issued by the game on the virtual device are
   intercepted by the proxy owner (see §4) and **forwarded to the real device**.
6. The proxy feeds intercepted effect parameters + play/stop state into a
   **force model** (§5) that computes the instantaneous net torque at ~60 Hz.
7. An **aiohttp** server broadcasts the model state over **WebSocket** and
   serves the browser dashboard.

### Two run modes

* **`--proxy` (default):** full passthrough proxy as above. Captures everything.
  Requires the game to target the virtual device. Requires RW on
  `/dev/input/eventN` and `/dev/uinput`.
* **`--observe`:** read-only fallback. Opens the real device read-only, shows
  wheel position/velocity and play/stop/gain events, but **cannot** show effect
  parameters (they are not observable read-only). Always works, no setup. Used
  when the user just wants a wheel position/scope without reconfiguring the game.

## 4. The uinput FF request/response protocol (critical path)

This is the hardest, most error-prone part. It is exactly how `libevdev`-based
proxies work. Per `Documentation/input/uinput.rst`:

The proxy owns the uinput fd and reads input events from it. Three FF-related
things arrive:

1. **Upload** — arrives as an `input_event` with
   `type=EV_UINPUT, code=UI_FF_UPLOAD`. The proxy then:
   - `ioctl(uinput_fd, UI_BEGIN_FF_UPLOAD, &uinput_ff_upload)` with
     `.request_id = event.value` → kernel fills `.effect` (and `.old`).
   - Uploads `.effect` to the **real** device: `ioctl(real_fd, EVIOCSFF, &eff)`
     → receives assigned `effect_id`.
   - Sets `uinput_ff_upload.effect_id` and `.retval`.
   - `ioctl(uinput_fd, UI_END_FF_UPLOAD, &uinput_ff_upload)` to complete.

2. **Erase** — `EV_UINPUT / UI_FF_ERASE`; handled with
   `UI_BEGIN_FF_ERASE` / `UI_END_FF_ERASE`, forwarding `EVIOCRMFF`.

3. **Play/Stop/Gain** — arrive as ordinary `EV_FF` input events on the uinput fd
   (code = effect id, value = repeat count; or code = `FF_GAIN`/`FF_AUTOCENTER`
   with the level). The proxy forwards them to the real device by **writing the
   same `EV_FF` event** to the real fd.

> **Risk note:** the `ff_effect` / `uinput_ff_upload` layouts must match the
> kernel UAPI byte-for-byte. They are defined in `uinput_ff.py` via `ctypes`
> with a runtime `sizeof` assertion. This path needs validation on real
> hardware (see §9).

## 5. Force model (what "force applied" means)

The kernel FF core computes forces inside the driver/device; we cannot read the
true motor current from userspace. So the visualizer **models** the applied
torque from the effect parameters — the same math `ff-memless` uses, reimplemented
for display. It is an *approximation* of intent, not a measurement of motor
current (documented as such in the UI).

State:
- `effects: {id: ff_effect}` — from intercepted uploads.
- `playing: {id: {repeat, start_ms, value}}` — from `EV_FF` play events.
- `position`, `velocity` — from `EV_ABS` (the FF axis) and its time derivative.
- `gain` — from `EV_FF FF_GAIN` (0..0xFFFF → 0..1).

Per tick (≈60 Hz), for each playing effect compute its signed contribution along
the wheel axis (direction projected via `sin(dir*360/0x10000)`, matching the
kernel convention):

| Effect | Instantaneous contribution |
|--------|----------------------------|
| Constant | `level · dirProj` |
| Periodic | `(mag·wave(t) + offset) · dirProj`, wave = sine/triangle/square/saw per `waveform` |
| Ramp | lerp(`start`,`end`, age/length) · dirProj |
| Spring | `right/left_coeff · (center − pos)`, clamped to saturation |
| Damper | `coeff · (−velocity)` |
| Friction | opposes velocity, clamped to saturation (Coulomb approximation) |
| Inertia | `coeff · (−accel)` (approx) |

Envelope (attack/fade) applied to the magnitude over time. All contributions are
normalized to `[-1, 1]` (`±32767`), summed, and multiplied by `gain`.

## 6. Data contract (WebSocket → browser)

JSON pushed at ~60 Hz:
```json
{
  "ts": 12345.67,
  "torque": -0.42,            // net signed force, -1..1
  "strength": 0.42,           // |torque|
  "direction_deg": 180.0,
  "position": 0.03,           // normalized wheel pos, -1..1
  "velocity": -0.12,
  "gain": 1.0,
  "components": [             // breakdown per playing effect
    {"id":3,"type":"FF_CONSTANT","name":"Constant","contrib":-0.42,"repeats":1}
  ],
  "playing": [                // currently active effects
    {"id":3,"type":"FF_CONSTANT","name":"Constant","direction":32768,
     "age_ms":120,"length_ms":1000,"repeat":1}
  ]
}
```

REST endpoints:
- `GET  /api/devices` — list FF-capable evdev devices (for selection).
- `GET  /api/gains`   — probed per-force-type gains (best-effort, §7).
- `POST /api/gains`   — set gains live.

## 7. Live gain settings (driver-agnostic)

The `spring_level` / `damper_level` / `friction_level` / `gain` sysfs attributes
are **driver-specific** (the `tmff2` driver exposes them). The visualizer probes
them generically: it walks the device's sysfs (`/sys/class/input/eventN/...` and
the bound driver's module params) and exposes whatever standard-named attributes
exist. With `tmff2` it shows all four; with another driver it shows whatever is
available, and degrades gracefully if none exist. Editing writes back to sysfs
live.

## 8. Frontend (browser dashboard)

Vanilla JS + a hand-written canvas rolling-scope (no bundler, no CDN needed,
works fully offline). Components:
- **FFB scope** — rolling torque graph (signed), last ~6 s.
- **Direction** — horizontal left/right bar + angle.
- **Strength** — vertical bar + numeric gauge.
- **Wheel position** — secondary trace / dial.
- **Active effects table** — id, type, direction, age, parameters.
- **Gain sliders** — spring/damper/friction/gain, live-updating, best-effort.
- **Mode indicator** — proxy vs observe, virtual device node.

## 9. Validation status / honest caveats

Cannot be live-tested against a real FFB session in this environment. Items
needing on-hardware validation:
- `ff_effect` / `uinput_ff_upload` byte layout (asserted at runtime).
- The `UI_BEGIN_FF_UPLOAD` / `UI_END_FF_UPLOAD` handshake timing.
- Force-model accuracy vs. perceived force (it models intent, not motor current).

The `--observe` mode is always safe and is the recommended first test; full
`--proxy` mode is the complete feature set.

## 10. File layout

```
ffvisualizer/
  PLAN.md            this file
  README.md          usage / setup
  requirements.txt   evdev, aiohttp
  run.sh             launcher (sudo for uinput/input RW)
  uinput_ff.py       ctypes ff_effect + uinput FF ioctl wrappers
  force_model.py     playing-state + instantaneous torque computation
  sysfs_gains.py     driver-agnostic gain sysfs probe + live write
  proxy.py           real<->virtual forwarding, FF interception, both modes
  server.py          aiohttp HTTP + websocket
  main.py            CLI entry: device pick, start proxy + server
  web/
    index.html
    app.js
    style.css
```
