# ffpanel — interactive force-feedback control panel

`ffpanel` is the lab instrument for the hid-tmff2 driver family: pick an
event device, pick an effect, tweak parameters live, start/stop, and
watch the **expected force** at the wheel — an interactive terminal UI
(Go + [Bubble Tea]) plus a scriptable one-shot mode. It replaces the
retired `tools/ffctl.c` with a single codebase: `ffpanel play ...` is
arg-compatible with the old C tool (an `ln -s ffpanel ffctl` keeps old
scripts working).

Two hard rules from this repo's history (`work/analysis/13_periodic_wedge.md`):

1. **It speaks only the Linux FF UAPI** — EVIOCSFF/EVIOCRMFF/EV_FF and
   the EVIOCG* queries. It never touches raw HID; wire safety is the
   driver's job, so the tool cannot re-create the firmware wedge.
2. **It must not lie.** The expected-force monitor mirrors the driver's
   synthesis math 1:1 (direction projection, waveform sampling,
   envelopes, the rumble→sine conversion) — every function in
   `synth.go` is a port pinned to its C source, and
   [`parity/vectors.txt`](parity/vectors.txt) is the golden-vector
   contract (427 cases, asserted within ±1 by `go test`).

## Build

```
cd tools/ffpanel
go build -o ffpanel .
go test ./...        # golden vectors + ff_effect layout pins
```

Dependencies (bubbletea, lipgloss, bubbles/textinput) are pinned and
vendored — the module builds offline.

## Usage

```
sudo ./ffpanel                                    # interactive TUI
sudo ./ffpanel list                               # devices + FF caps
sudo ./ffpanel play /dev/input/event26 sine --period 2000 --duration 5000
sudo ./ffpanel play /dev/input/event26 constant --direction 16384
sudo ./ffpanel play event26 rumble --strong 20000 --weak 10000
```

`play` accepts the same flags as the old `ffctl` (`--period`,
`--magnitude`, `--direction`, `--duration`, `--count`, `--delay`,
`--attack/--attack-level/--fade/--fade-level`, `--start/--end`,
`--strong/--weak`); the device argument may be omitted when
`~/.config/ffpanel.json` still carries the last device used by the
TUI. Root is normally required (O_RDWR on the event node); a udev rule
(`MODE="0660", GROUP="input"` + group membership) avoids sudo.

### The TUI

```
[devices] ──enter──> [effect type] ──enter──> [editor+monitor] ──q──> quit
```

- **devices**: scanned `/dev/input/event*` with name, vendor:product
  and the EVIOCGBIT(EV_FF) capability summary; non-FF devices greyed.
- **effect type**: constant / sine / square / triangle / saw up /
  saw down / ramp / rumble — filtered to what the device advertises.
- **editor+monitor**: `↑/↓` select a parameter, `←/→` adjust (`shift`
  ×10, `+`/`-` also work), `enter` types an exact value, `space`
  play/stop, `i` toggles the sign convention, `u` forces a re-upload,
  `g`/`a` gain/autocenter sliders, `esc` back, `q` quit. Holding an
  arrow key accelerates the step gently: ×10 after 0.6 s, ×100 after
  2.6 s, capped — a tap is still a precise ±1/±10. Below the expected
  force, a real-time **wheel position** bar (EV_ABS, range-accurate)
  shows where the wheel actually is while the force pushes it.

Parameter edits ride the driver's *update* path — a plain `EVIOCSFF`
on the existing effect id after a 30 ms debounce (never erase+create,
which would churn effect ids and slot-0 state). One exception mirrors
the kernel: **count is a property of the play event** (the `EV_FF`
value), so editing it while playing does not change the running
playback — the monitor keeps modeling the running count and the next
`space` play picks up the new one. Gain/autocenter use
the FF_GAIN/FF_AUTOCENTER pseudo-effects with the fftest convention
(level = pct × 65535 / 100). Quitting stops playback, erases every
uploaded id and closes the fd — no residual torque; the editor also
defaults to a 30 s dead-man duration so even a `kill -9` leaves no
endless force.

The `i` probe is the M0 direction-inversion diagnosis
(`.sisyphus/plans/ffpanel.md` §1): `ffpanel play constant --direction
16384` should push the wheel **right**, `--direction 49152` should
pull **left**. If both feel opposite, the device wire sign is
inverted — press `i` so the monitor matches reality, record the
finding (`~/.config/ffpanel.json` persists it), and take the D1
driver decision with that evidence.

### The direction-0 trap

hid-tmff2 projects forces with `sin(direction·360/65536)` — integer
degrees — so **direction 0 (north) produces zero force** on these
wheels and `direction 1` also truncates to zero. The default here is
16384 (90°, full force along the wheel axis). The editor warns on
direction 0.

### The wire-sign finding (M0) — indicator default

Hardware-confirmed 2026-08-20: the wheel pulls opposite to the UAPI
projection for **streamed** effects (`work/analysis/14_direction_sign.md`).
Root cause: the driver's `04 0e` stream channel is sign-inverted
relative to the native `03` param channel (your constant test ran
native-mode; ramps/periodics — and constants played after any
periodic upload in the same boot — rode the inverted stream). The
driver now negates at the stream write, so both channels carry UAPI
semantics.

The indicator displays the **hardware sign** — `L` on screen means the
wheel is pushed left, matching what you feel. Press `i` to compare
against the raw UAPI projection; the choice persists in
`~/.config/ffpanel.json`. The one-shot `play` bar uses the hardware
sign too.

Note on ramps: with count > 1 the level snaps end → start at each
iteration boundary (FF semantics) — the wheel kicks at that moment by
design; use count 1 for smooth single sweeps.

## Parity contract

| Go (`synth.go`)    | C source                                                     |
|--------------------|--------------------------------------------------------------|
| `Sin16`            | `include/linux/fixp-arith.h` `fixp_sin16` (vendored copy in `parity/fixp_arith.h`) |
| `dirProject`       | `t500rs_synth_dir_project()` / `t500rs_scale_const_with_direction()` |
| `envelope`         | `t500rs_synth_envelope()`                                    |
| `fxSample`         | `t500rs_synth_sample()` (count>1 restarts, delay gating, constant expiry) |
| `RumbleConvert`    | `tmff2_rewrite_rumble()` (`src/hid-tmff2.c`): sine, 50 ms, `strong/3 + weak/6`, direction 16384 |
| `ScaleS8`          | `t500rs_scale_const_level_s8()`                              |

Regenerate the golden vectors whenever driver math changes — drift
becomes a failing test, not a lie:

```
gcc -O2 -Wall -Wextra -std=gnu11 -o /tmp/harness tools/ffpanel/parity/harness.c
/tmp/harness > tools/ffpanel/parity/vectors.txt
cd tools/ffpanel && go test ./...
```

`parity/harness.c` holds verbatim copies of the driver functions; if
you edit the driver math, re-copy the bodies there first. Two pinned
quirks the Go port reproduces: constants are **not** delay-gated (they
expire on `(delay+length)×count` but play at full level during the
delay window), and `direction=1` projects to zero via integer-degree
truncation.

The editor also gates parameter rows by effect type instead of
rendering greyed no-ops: rumble has no direction/envelope (it is
converted to a direction-forced sine), and constant envelopes are
zeroed by the driver, so those rows simply do not exist.

## Layout

```
tools/ffpanel/
  go.mod, go.sum, vendor/   pinned + vendored deps (offline builds)
  main.go                   entry: TUI by default; `play`/`list` subcommands
  cli.go                    one-shot mode (ffctl-compatible flags + live bar)
  input_linux.go            discovery, caps, ioctls, upload/update/play/erase,
                            gain/autocenter pseudo-effects
  synth.go                  driver-parity prediction (see table above)
  synth_test.go             golden-vector tests
  input_linux_test.go       ff_effect byte-layout pins (C-derived goldens)
  ui_model.go               root model, screen state machine, tick + debounce
  ui_devices.go             device list + effect-type screens
  ui_editor.go              editor + live monitor screen
  ui_style.go               lipgloss styles
  config.go                 ~/.config/ffpanel.json persistence
  parity/                   golden-vector harness (C, test fixture only)
```

(The plan's `ui/` package is flattened into `ui_*.go` files: the Go
rule of one package per directory makes a separate TUI package unable
to import the root's device/synth types without duplicating them —
one codebase beats a subdirectory.)

[Bubble Tea]: https://github.com/charmbracelet/bubbletea
