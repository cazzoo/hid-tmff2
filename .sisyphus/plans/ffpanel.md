# ffpanel — interactive FF control panel (Go + Bubble Tea): plan

**Status:** draft v2 (self-reviewed — Momus agent unavailable, API-key infra
failure) · **Repo:** hid-tmff2 · **Supersedes:** none (extends `tools/ffctl.c`)

## 0. Context

`tools/ffctl.c` proved the workflow: explicit effect parameters are mandatory
for trustworthy hardware conclusions (fftest's scanf leaves unknowns — the
"feels like rumble" false alarm). The next step is a real instrument: an
interactive terminal UI — pick event device, pick effect, tweak parameters
live, start/stop, watch the expected force — in Go on
`github.com/charmbracelet/bubbletea`.

Two hard constraints from this repo's history (`work/analysis/13_periodic_wedge.md`):

1. **The tool speaks only Linux FF UAPI.** Wire safety is the driver's job —
   the tool never touches raw HID, so it cannot re-create the wedge.
2. **The tool must not lie.** Every predicted value mirrors driver math
   exactly (direction projection, waveform sampling, envelopes, rumble
   conversion), or its indicator is worse than none.

## 1. M0 — direction-inversion diagnosis (TODAY, zero new code)

User report: ffctl's bar shows R while the wheel pulls left (or vice versa).

Analysis:

- UAPI: direction 0 = forward (sin→0 ⇒ zero force), 16384 = east (sin→+1 ⇒
  full positive projection). The driver projects with
  `fixp_sin16(direction * 360 / 0x10000) / 0x7fff` — the same family
  convention as the T300RS backend and upstream Logitech FF drivers.
- The device-side meaning of a positive `03 0e` / `04 0e` level (right-push vs
  left-pull) was **never established**: C1/C2 pcaps contain the levels, but the
  game's intent at those instants is not recoverable.
- Therefore exactly one of {UAPI expectation, device wire sign} is flipped.
  The C tool's bar implements the former; the driver streams the latter.

**Procedure (2 min, existing binary):**

```
gcc -O2 -Wall -o ffctl tools/ffctl.c -lm
sudo ./ffctl /dev/input/eventXX constant --direction 16384   # UAPI says: push RIGHT
sudo ./ffctl /dev/input/eventXX constant --direction 49152   # UAPI says: pull LEFT
```

- **Both feel opposite** ⇒ device wire sign inverted ⇒ execute D1.
- **16384 pushes right** ⇒ inversion is game-side (in-game "invert FFB"
  setting or game convention) ⇒ no driver change; record finding in
  `work/analysis/14_direction_sign.md`.

**D1 (conditional driver fix):** negate the projection result in
`t500rs_scale_const_with_direction()` and `t500rs_synth_dir_project()`
(`src/tmt500rs/hid-tmt500rs.c`) so positive projected force always means
rightward on the wire; update `docs/T500RS_FFBEFFECTS.md` §7 direction row;
add `work/analysis/14_direction_sign.md` with the M0 evidence. Commit only
after M0 confirmation **and** an in-game sanity check (no invert-FF option
enabled in the game).

## 2. Goals / non-goals

**Goals**

- Interactive device → effect → live-tweak → play/stop workflow, single binary.
- Live force monitor identical in math to the driver (bar + side + %).
- Device-sign probe (M0 built into the UI) to close D1 reproducibly.
- Gain and autocenter control (FF_GAIN / FF_AUTOCENTER pseudo-effects, same
  approach as fftest).
- Capability-gated UI (reads EVIOCGBIT; only offers what the driver advertises).

**Non-goals (v1)**

- Raw HID / usbmon integration (driver's domain).
- Multiple simultaneous effects (games cover that; v1 = one effect + monitor).
- Non-Linux platforms; GUI; config file editing UI (defaults only, persisted).

## 3. UX flow (screens, keys)

```
[devices] ──enter──> [effect type] ──enter──> [editor+monitor] ──q──> back / quit
```

- **devices**: scanned `/dev/input/event*`, name + vendor/product (EVIOCGID),
  FF capability summary (EVIOCGBIT(EV_FF)): e.g. `T500RS · constant periodic
  ramp condition rumble gain autocenter`. Non-FF devices greyed out.
- **effect type**: constant / sine / square / triangle / saw-up / saw-down /
  ramp / rumble (filtered by device caps).
- **editor+monitor** (the main screen):

```
  ffpanel — T500RS (event26)                    sine   0.50 Hz
  ─────────────────────────────────────────────────────────────
  period        ◂ 2000 ▸ ms        expected force
  magnitude     ◂ 20000 ▸                    L  ◂──────O|──▸  R
  direction     ◂ 16384 ▸ (90°)              61%  elapsed 1.4s
  duration      ◂ 6000 ▸ ms
  attack/fade   ... envelope rows ...         [space] play/stop
  count         ◂ 1 ▸                         [i] invert device-sign probe
                                             [u] force re-upload
                                             [g] gain  [a] autocenter
                                             [q] quit (stops + erases)
  ─────────────────────────────────────────────────────────────
  status: uploaded id=3 · playing · last stream level -42
```

- ↑/↓ select parameter, ←/→ (and +/-) adjust; `shift+←/→` = ×10 step.
- Parameter change → debounce 30 ms → `EVIOCSFF` update (driver's
  update_effect path already skips unchanged values).
- `space` writes `EV_FF` (value = count); stop = EV_FF value 0.
- `i` flips the monitor's device-sign assumption — the M0 probe, in-UI.
- `g`/`a`: gain/autocenter mini-sliders (FF_GAIN/FF_AUTOCENTER effects,
  level = pct × 65535 / 100, fftest convention).
- Ctrl+C / `q`: stop playback, `EVIOCRMFF` every uploaded id, close fd —
  **no residual torque** (hard acceptance criterion).

## 4. Architecture

- Bubble Tea Elm architecture; one root `tea.Model` with a screen state
  machine; `tea.Tick(16 ms)` drives the monitor; all device I/O behind a small
  `inputLinux` struct (fd, effect ids, caps).
- Device I/O: `golang.org/x/sys/unix` raw ioctls — EVIOCGNAME/EVIOCGID,
  EVIOCGBIT(EV_FF), EVIOCSFF, EVIOCRMFF, plain `write()` for EV_FF play/stop.
  String ioctls via `unsafe` where wrappers are missing.
- Module: `tools/ffpanel/go.mod`, self-contained; deps vendored
  (`go mod vendor`) so the repo builds offline. Deps: bubbletea, lipgloss,
  bubbles (list/textinput only). Pin exact versions.
- Root required (O_RDWR on event node); document udev alternative.

## 5. Parity contract (the tool must not lie)

Go functions port 1:1 from C, with source pinned in comments:

| Go (synth.go)         | C source                                                     |
|-----------------------|--------------------------------------------------------------|
| `Sin16`               | `include/linux/fixp-arith.h` `fixp_sin16` (table+interp; GPL-2.0, same license family — attribution comment) |
| `dirProject`          | `t500rs_synth_dir_project()` / `t500rs_scale_const_with_direction()` |
| `envelope`            | `t500rs_synth_envelope()`                                    |
| `sample` (waveforms)  | `t500rs_synth_sample()` switch (incl. count>1 per-iteration restart, delay window) |
| `rumbleConvert`       | `tmff2_convert_rumble()` (`src/hid-tmff2.c:473-496`): sine, 50 ms, `strong/3 + weak/6`, direction forced 16384 |
| `scaleS8`             | `t500rs_scale_const_level_s8()`                              |

**Golden-vector tests** (`synth_test.go`): a tiny C harness prints expected
levels for a grid of (type, params, t) using the driver's own code compiled
host-side; the Go test asserts equality within ±1. Regenerate the vectors
whenever driver math changes — drift becomes a failing test, not a lie.

**DONE ahead of schedule (2026-08-19):** the harness and vectors exist at
`tools/ffpanel/parity/` — `fixp_arith.h` (vendored kernel sin table +
folding), `harness.c` (verbatim copies of the five driver functions +
427-case grid incl. direction-truncation boundaries 0/1/16383/16384/32768/
49152/65535, count>1 restarts, envelopes, expiry edges, rumble integer
division), `vectors.txt` (the contract). Regenerate:
`gcc -O2 -Wall -std=gnu11 -o /tmp/harness tools/ffpanel/parity/harness.c && /tmp/harness > tools/ffpanel/parity/vectors.txt`.
Writing the harness pinned two driver behaviors the Go port must reproduce:
constants are NOT delay-gated (they expire on `(delay+length)×count` but play
at full level during the delay window), and `direction=1` projects to zero
(integer-degree truncation).

**Parity-review notes (v2 self-review):**

- `linux/fixp-arith.h` is kernel-internal (NOT in uapi headers), so the C
  harness cannot `#include <linux/fixp-arith.h>`. It must vendor a verbatim
  copy of the header from the kernel tree (GPL-2.0, same license family —
  attribution comment required), and the copy's `fixp_sin16` is what both the
  harness and a cross-check against the driver source diff must use.
- The Go `dirProject` must replicate the driver's **integer** truncation
  (`direction * 360 / 0x10000` in integer arithmetic) before any float sin —
  a naive `float64(direction)/65536*360` drifts up to ~0.003° and can flip
  the golden vectors by ±1 at boundary directions. Test directions include
  0, 1, 16383, 16384, 32768, 49152 deliberately.
- Live-tweak path rides the driver's `update_effect`, which sends only
  changed parameter packets (synth types: table rewrite only, no wire traffic
  except the next tick). Do NOT implement re-upload as erase+create in the
  tool — that would churn effect ids and slot-0 state; use plain `EVIOCSFF`
  on the existing id, which is exactly the update path.
- Rumble effects accept neither direction nor envelope — the editor must
  gate parameter rows by effect type, not just render greyed rows (greyed
  rows invite editing no-ops that look like driver bugs).

## 6. Milestones & acceptance criteria

| # | Milestone | Verifiable acceptance |
|---|-----------|----------------------|
| M0 | Direction probe (existing C tool) | Written result: 16384 ⇒ left/right? recorded in `work/analysis/14_direction_sign.md`; D1 executed or explicitly rejected |
| M1 | Prereqs + skeleton | `sudo pacman -S go`; `tools/ffpanel` builds (`go build ./...`, `go vet` clean); device list shows the wheel with correct caps; quit leaves zero torque |
| M2 | Effect lab | Upload sine period 2000 → felt 0.5 Hz wobble; live magnitude tweak audibly/felt changes without re-upload glitch; 5 min of rapid tweaking → no `dmesg` errors |
| M3 | Monitor + parity | Golden-vector tests pass (±1); bar matches C ffctl output on identical params; `i`-probe reproduces M0 finding |
| D1 | Driver sign fix (conditional) | After negation: `--direction 16384` pushes right, `49152` pulls left, in-game torque feels correct without invert options |
| M4 | Polish | Gain/autocenter work (set 50% → weaker forces); config persistence (`~/.config/ffpanel.json`: last device, sign finding, defaults); README |

## 7. Risks & mitigations

- **Math drift** (driver edited, tool stale) → golden-vector tests tied to C
  sources; regeneration step documented.
- **Update spam** while dragging parameters → 30 ms debounce; driver already
  skips unchanged packets.
- **Residual torque on crash/kill -9** → effects have finite duration by
  default in the tool (dead-man: default duration 30 s unless explicitly
  infinite); clean paths erase on exit.
- **Bubble Tea churn** → pinned versions + vendoring.
- **Root + raw terminal over SSH** → works (bubbletea handles raw mode);
  document `sudo -E` / udev rule for passwordless runs.
- **Scope creep toward a game-input stack** → non-goals enforced; v1 is a
  lab instrument.

## 8. File layout

```
tools/ffpanel/
  go.mod, go.sum, vendor/
  main.go            entry: flag dev override, program run, signal safety
  input_linux.go     discovery, caps, ioctl helpers, upload/update/play/erase
  synth.go           parity port (§5)
  synth_test.go      golden vectors
  ui/model.go        root model, screen state machine, tick loop
  ui/devices.go      device list screen
  ui/editor.go       editor + monitor screen
  ui/style.go        lipgloss styles
  README.md          usage, screenshots, the direction-0 zero-force trap
```

The C `tools/ffctl.c` stays (scriptable, zero deps) until M4, then optional
retire — open question 3.

## 9. Open questions

1. **Name**: `ffpanel` (proposed) vs keeping `ffctl` for the Go binary?
2. **M0 result**: does `--direction 16384` push right or pull left on your
   wheel? (Decides D1.)
3. **Retire C ffctl** after parity, or keep both?
