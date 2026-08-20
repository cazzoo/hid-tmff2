// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * cli.go — `ffpanel play` one-shot mode, replacing tools/ffctl.c
 * verbatim: the same flags, the same live expected-force bar, the same
 * Ctrl+C stop+erase semantics, on the same parity math the TUI uses
 * (one codebase).
 *
 *   ffpanel play [device] <sine|square|triangle|sawup|sawdown|
 *                         constant|ramp|rumble> [key value ...]
 *
 * <device> may be omitted when ~/.config/ffpanel.json carries a
 * last_device from a previous TUI session.
 */
package main

import (
	"fmt"
	"os"
	"os/signal"
	"strconv"
	"strings"
	"syscall"
	"time"
)

const cliUsage = `usage: ffpanel play [device] <sine|square|triangle|sawup|sawdown|
                            constant|ramp|rumble> [key value ...]
       ffpanel list
       ffpanel (interactive TUI when run with no arguments)

  --period N       waveform cycle in ms, periodic only (default 1000)
  --magnitude N    level 0-32767; also constant level (default 20000)
  --direction N    0-65535; 0 = zero force here, default 16384
  --duration N     play time in ms, 0 = until Ctrl+C (default 3000)
  --count N        play repetitions (default 1)
  --delay N        start delay ms (default 0)
  --attack N       envelope attack ms (default 0)
  --attack-level N 0-32767 (default 0)
  --fade N         envelope fade ms (default 0)
  --fade-level N   0-32767 (default 0)
  --start N        ramp start level (default -10000)
  --end N          ramp end level (default 10000)
  --strong N       rumble strong magnitude (default 20000)
  --weak N         rumble weak magnitude (default 10000)

Direction: hid-tmff2 projects forces with sin(direction * 360 / 65536),
so direction 0 (north) produces ZERO force on these wheels. The default
here is 16384 (90 degrees = full force along the wheel axis).
The bar shows the HARDWARE sign (M0-verified: matches what the wheel
does; see work/analysis/14_direction_sign.md).
`

var validKinds = map[string]bool{
	"sine": true, "square": true, "triangle": true, "sawup": true,
	"sawdown": true, "constant": true, "ramp": true, "rumble": true,
}

func isDeviceArg(s string) bool {
	return strings.HasPrefix(s, "/dev/")
}

// argInt mirrors ffctl's key/value scan: pairs after the type argument.
func argInt(args []string, name string, fallback int) (int, error) {
	for i := 0; i+1 < len(args); i += 2 {
		if args[i] == name {
			v, err := strconv.ParseInt(args[i+1], 0, 64)
			if err != nil {
				return 0, fmt.Errorf("%s: %v", name, err)
			}
			return int(v), nil
		}
	}
	return fallback, nil
}

// runPlay is the one-shot mode; it returns the process exit code.
func runPlay(argv []string) int {
	devPath := ""
	if len(argv) > 0 && isDeviceArg(argv[0]) {
		devPath = argv[0]
		argv = argv[1:]
	}
	if len(argv) < 1 {
		fmt.Fprint(os.Stderr, cliUsage)
		return 1
	}
	kind := argv[0]
	if !validKinds[kind] {
		fmt.Fprintf(os.Stderr, "ffpanel: unknown effect type %q\n\n%s", kind, cliUsage)
		return 1
	}
	rest := argv[1:]
	if len(rest)%2 != 0 {
		fmt.Fprint(os.Stderr, cliUsage)
		return 1
	}
	if devPath == "" {
		cfg := LoadConfig()
		if cfg.LastDevice == "" {
			fmt.Fprintln(os.Stderr, "ffpanel: no device given and no last_device in ~/.config/ffpanel.json")
			return 1
		}
		devPath = cfg.LastDevice
	}

	p := DefaultParams(kind)
	var err error
	grab := func(name string, dst *int) bool {
		if *dst, err = argInt(rest, name, *dst); err != nil {
			fmt.Fprintf(os.Stderr, "ffpanel: %v\n", err)
			return false
		}
		return true
	}
	for _, a := range []struct {
		name string
		dst  *int
	}{
		{"--period", &p.Period}, {"--magnitude", &p.Magnitude},
		{"--direction", &p.Direction}, {"--duration", &p.Duration},
		{"--count", &p.Count}, {"--delay", &p.Delay},
		{"--attack", &p.Attack}, {"--attack-level", &p.AttackLevel},
		{"--fade", &p.Fade}, {"--fade-level", &p.FadeLevel},
		{"--start", &p.Start}, {"--end", &p.End},
		{"--strong", &p.Strong}, {"--weak", &p.Weak},
	} {
		if !grab(a.name, a.dst) {
			return 1
		}
	}
	if p.Period < 1 {
		p.Period = 1
	}
	if p.Kind == "ramp" && p.Duration == 0 {
		p.Duration = 1000 // ffctl: a ramp needs a length to sweep
	}

	dev, err := Open(devPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "ffpanel: %v\n", err)
		return 1
	}
	defer dev.Close()

	id, err := dev.Upload(&p)
	if err != nil {
		fmt.Fprintf(os.Stderr, "ffpanel: %v\n", err)
		return 1
	}

	cfg := LoadConfig()
	cfg.LastDevice = devPath
	_ = cfg.Save()

	switch p.Kind {
	case "rumble", "constant", "ramp":
		fmt.Printf("uploaded %s: id=%d duration=%dms\n", p.Kind, id, p.Duration)
	default:
		fmt.Printf("uploaded %s: id=%d period=%dms (%.2f Hz) magnitude=%d direction=%d\n",
			p.Kind, id, p.Period, 1000.0/float64(p.Period), p.Magnitude, p.Direction)
	}

	if err := dev.Play(p.Count); err != nil {
		fmt.Fprintf(os.Stderr, "ffpanel: %v\n", err)
		return 1
	}

	sigc := make(chan os.Signal, 1)
	signal.Notify(sigc, syscall.SIGINT, syscall.SIGTERM)

	fx := p.ToFx()
	t0 := time.Now()
	finite := p.Duration != 0
	total := time.Duration(p.Duration) * time.Duration(p.Count) * time.Millisecond

	if finite {
		fmt.Printf("playing %dms of effective time... (Ctrl+C stops)\n",
			int(total.Milliseconds()))
	} else {
		fmt.Println("playing (infinite)... Ctrl+C to stop")
	}

	ticker := time.NewTicker(16 * time.Millisecond)
	defer ticker.Stop()
	for {
		select {
		case <-sigc:
			fmt.Println()
			return 0 // deferred Close() stops + erases
		case <-ticker.C:
			elapsed := time.Since(t0)
			if finite && elapsed >= total {
				fmt.Println()
				return 0
			}
			renderCLIBar(&p, &fx, elapsed.Milliseconds())
		}
	}
}

// renderCLIBar prints the live bar: type, Hz, elapsed, handle position,
// side, percent. Side follows the hardware sign convention (default,
// M0-verified — L = wheel pushed left), matching the TUI.
func renderCLIBar(p *EffectParams, fx *Fx, tMs int64) {
	const barHalf = 14
	var bar [barHalf*2 + 4]byte
	for i := range bar {
		bar[i] = '-'
	}
	bar[0] = '['
	bar[barHalf+1] = '|'
	bar[barHalf*2+2] = ']'

	lvl := float64(-StreamLevel(fx, uint64(tMs)))
	idx := int(round(lvl / 127.0 * float64(barHalf)))
	if idx < -barHalf {
		idx = -barHalf
	}
	if idx > barHalf {
		idx = barHalf
	}
	bar[barHalf+1+idx] = 'O'

	side := byte('-')
	if lvl < -0.5 {
		side = 'L'
	} else if lvl > 0.5 {
		side = 'R'
	}
	hz := 0.0
	switch p.Kind {
	case "rumble":
		hz = 20.0
	default:
		if fx.PeriodMs != 0 {
			hz = 1000.0 / float64(fx.PeriodMs)
		}
	}
	pct := absF(lvl) / 127.0 * 100.0

	fmt.Printf("\r%-9s %6.2fHz %7.2fs %s  %c %5.1f%% ",
		p.Kind, hz, float64(tMs)/1000.0, string(bar[:]), side, pct)
	os.Stdout.Sync()
}

func round(v float64) float64 {
	if v < 0 {
		return -round(-v)
	}
	i := int(v)
	if v-float64(i) >= 0.5 {
		return float64(i + 1)
	}
	return float64(i)
}

func absF(v float64) float64 {
	if v < 0 {
		return -v
	}
	return v
}

// runList prints the scanned devices with FF caps (script helper; the
// same discovery the TUI uses).
func runList() int {
	devs := ScanDevices()
	if len(devs) == 0 {
		fmt.Println("no /dev/input/event* nodes")
		return 0
	}
	for _, d := range devs {
		state := "no force feedback"
		if d.HasFF {
			state = strings.Join(d.FFCaps, " ")
		}
		fmt.Printf("%s  %04x:%04x  %-32s  %s\n", d.Path,
			d.ID.Vendor, d.ID.Product, d.Name, state)
	}
	return 0
}
