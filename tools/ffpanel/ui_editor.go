// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ui_editor.go — the editor + live monitor screen: parameter rows
 * (gated by effect type), the expected-force bar (driver-parity math),
 * the [i] device-sign probe, [u] re-upload, and the [g]/[a]
 * gain/autocenter sliders.
 */
package main

import (
	"fmt"
	"strconv"
	"strings"
	"time"

	"github.com/charmbracelet/bubbles/textinput"
	tea "github.com/charmbracelet/bubbletea"
)

// rowsFor returns the editable parameter rows for an effect type.
// Rumble gets no direction/envelope rows and constant gets no envelope
// rows: the driver ignores them (rumble is converted to a direction-
// forced sine; constant envelopes are zeroed upstream), and greyed
// no-op rows would look like driver bugs (plan §5).
func rowsFor(kind string) []paramRow {
	env := []paramRow{
		{"attack", func(p *EffectParams) int { return p.Attack },
			func(p *EffectParams, v int) { p.Attack = v }, 0, 65535, "ms"},
		{"attack level", func(p *EffectParams) int { return p.AttackLevel },
			func(p *EffectParams, v int) { p.AttackLevel = v }, 0, 32767, ""},
		{"fade", func(p *EffectParams) int { return p.Fade },
			func(p *EffectParams, v int) { p.Fade = v }, 0, 65535, "ms"},
		{"fade level", func(p *EffectParams) int { return p.FadeLevel },
			func(p *EffectParams, v int) { p.FadeLevel = v }, 0, 32767, ""},
	}
	dir := paramRow{"direction", func(p *EffectParams) int { return p.Direction },
		func(p *EffectParams, v int) { p.Direction = v }, 0, 65535, ""}
	common := []paramRow{
		{"duration", func(p *EffectParams) int { return p.Duration },
			func(p *EffectParams, v int) { p.Duration = v }, 0, 65535, "ms"},
		{"count", func(p *EffectParams) int { return p.Count },
			func(p *EffectParams, v int) { p.Count = v }, 1, 65535, "×"},
		{"delay", func(p *EffectParams) int { return p.Delay },
			func(p *EffectParams, v int) { p.Delay = v }, 0, 65535, "ms"},
	}

	switch kind {
	case "constant":
		return append([]paramRow{
			{"level", func(p *EffectParams) int { return p.Magnitude },
				func(p *EffectParams, v int) { p.Magnitude = v }, -32767, 32767, ""},
			dir,
		}, common...)
	case "ramp":
		rows := append([]paramRow{
			{"start", func(p *EffectParams) int { return p.Start },
				func(p *EffectParams, v int) { p.Start = v }, -32767, 32767, ""},
			{"end", func(p *EffectParams) int { return p.End },
				func(p *EffectParams, v int) { p.End = v }, -32767, 32767, ""},
			dir,
		}, common...)
		return append(rows, env...)
	case "rumble":
		return append([]paramRow{
			{"strong", func(p *EffectParams) int { return p.Strong },
				func(p *EffectParams, v int) { p.Strong = v }, 0, 65535, ""},
			{"weak", func(p *EffectParams) int { return p.Weak },
				func(p *EffectParams, v int) { p.Weak = v }, 0, 65535, ""},
		}, common...)
	default: // periodic waveforms
		rows := append([]paramRow{
			{"period", func(p *EffectParams) int { return p.Period },
				func(p *EffectParams, v int) { p.Period = v }, 1, 65535, "ms"},
			{"magnitude", func(p *EffectParams) int { return p.Magnitude },
				func(p *EffectParams, v int) { p.Magnitude = v }, -32767, 32767, ""},
			{"offset", func(p *EffectParams) int { return p.Offset },
				func(p *EffectParams, v int) { p.Offset = v }, -32767, 32767, ""},
			{"phase", func(p *EffectParams) int { return p.Phase },
				func(p *EffectParams, v int) { p.Phase = v }, 0, 65535, "cd"},
			dir,
		}, common...)
		return append(rows, env...)
	}
}

func (m model) updateEditor(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	switch msg.String() {
	case "up", "k", "tab":
		if m.rowCursor > 0 {
			m.rowCursor--
		}
	case "down", "j", "shift+tab", "btab":
		if m.rowCursor < len(m.rows)-1 {
			m.rowCursor++
		}
	case "left", "-":
		m.nudge(-1, 1, "left")
	case "right", "+", "=":
		m.nudge(1, 1, "right")
	case "shift+left":
		m.nudge(-1, 10, "left")
	case "shift+right":
		m.nudge(1, 10, "right")
	case "enter":
		row := m.rows[m.rowCursor]
		m.input.SetValue(strconv.Itoa(row.get(&m.p)))
		m.editing = true
		m.input.Focus()
		return &m, textinput.Blink
	case " ":
		m.togglePlay()
	case "i":
		m.signFlipped = !m.signFlipped
		m.cfg.DeviceSignFlipped = m.signFlipped
		_ = m.cfg.Save()
		if m.signFlipped {
			m.setStatus("monitor: device-sign INVERTED for display (persisted)")
		} else {
			m.setStatus("monitor: device-sign as reported by UAPI math (persisted)")
		}
	case "u":
		m.forceReupload()
	case "g":
		m.overlay = overlayGain
		m.overlayVal = m.gainPct
	case "a":
		m.overlay = overlayAutocenter
		m.overlayVal = m.acPct
	}
	return &m, nil
}

// nudge adjusts the selected row by dir*step. Terminal auto-repeat
// delivers a stream of identical KeyMsgs while the key is held; the
// step grows with hold duration (accelMult), so a held shift+arrow
// sweeps the value range logarithmically instead of line-by-line.
func (m *model) nudge(dir, base int, keyID string) {
	now := time.Now()
	if m.holdKey != keyID || now.Sub(m.holdLast) > 350*time.Millisecond {
		m.holdKey = keyID
		m.holdSince = now
	}
	m.holdLast = now
	step := base * accelMult(now.Sub(m.holdSince))
	row := m.rows[m.rowCursor]
	row.set(&m.p, clamp(row.get(&m.p)+dir*step, row.min, row.max))
	m.paramsChanged()
}

// accelMult returns the hold-time step multiplier: 1x for the first
// 400 ms (a tap is a precise single step), then one more decade every
// 1.2 s of continuous holding, capped at 1000x. Exponential step
// growth = logarithmic value traversal: 0 -> 65535 needs ~7 held
// steps at full acceleration instead of thousands.
func accelMult(held time.Duration) int {
	if held < 400*time.Millisecond {
		return 1
	}
	decades := int((held-400*time.Millisecond)/(1200*time.Millisecond)) + 1
	if decades > 3 {
		decades = 3
	}
	mult := 1
	for i := 0; i < decades; i++ {
		mult *= 10
	}
	return mult
}

const barHalf = 14

// barLine renders the expected-force handle like the C tool: handle
// left = pull left, right = push right; | marks center.
func barLine(lvl float64) string {
	var bar [barHalf*2 + 3]byte
	for i := range bar {
		bar[i] = '-'
	}
	bar[0] = '['
	bar[barHalf+1] = '|'
	bar[barHalf*2+2] = ']'
	idx := int(lround(lvl / 127.0 * float64(barHalf)))
	if idx < -barHalf {
		idx = -barHalf
	}
	if idx > barHalf {
		idx = barHalf
	}
	bar[barHalf+1+idx] = 'O'
	return string(bar[:])
}

func lround(v float64) float64 {
	if v < 0 {
		return -lround(-v)
	}
	return float64(int(v + 0.5))
}

func (m model) viewEditor() string {
	var b strings.Builder
	name := m.dev.Info.Name
	if name == "" {
		name = m.dev.Info.Path
	}

	fx := m.p.ToFx()
	hz := 0.0
	switch m.p.Kind {
	case "rumble":
		hz = 20.0
	default:
		if fx.PeriodMs != 0 {
			hz = 1000.0 / float64(fx.PeriodMs)
		}
	}
	hzStr := ""
	if hz > 0 {
		hzStr = fmt.Sprintf("   %.2f Hz", hz)
	}

	b.WriteString(stTitle.Render(fmt.Sprintf(" ffpanel — %s ", name)) +
		stHeader.Render(fmt.Sprintf("  %s%s", prettyType(m.p.Kind), hzStr)))
	b.WriteString("\n\n")

	// parameter rows
	for i, row := range m.rows {
		cursor := "  "
		style := stLabel
		if i == m.rowCursor {
			cursor = stCursor.Render("▸ ")
			style = stLabel.Bold(true)
		}
		val := fmt.Sprintf("%d", row.get(&m.p))
		if row.unit != "" {
			val += " " + row.unit
		}
		extra := ""
		if row.label == "direction" {
			extra = stDim.Render(fmt.Sprintf("  (%d°)", row.get(&m.p)*360/65536))
			if row.get(&m.p) == 0 {
				extra += stWarn.Render("  zero force!")
			}
		}
		if row.label == "duration" && row.get(&m.p) == 0 {
			extra = stWarn.Render("  infinite")
		}
		if m.editing && i == m.rowCursor {
			val = m.input.View()
		}
		b.WriteString(fmt.Sprintf("  %s%s◂ %s ▸%s\n", cursor,
			style.Render(row.label), stValue.Render(val), extra))
	}

	// monitor
	b.WriteString("\n")
	lvlLabel := "expected force"
	if m.signFlipped {
		lvlLabel = "expected force (device-sign inverted)"
	}
	var lvl float64
	elapsed := 0.0
	if m.playing {
		tMs := time.Since(m.playStart).Milliseconds()
		if tMs < 0 {
			tMs = 0
		}
		lvl = float64(displayLevel(&m.playFx, uint64(tMs), m.signFlipped))
		elapsed = float64(tMs) / 1000.0
	} else if m.uploaded {
		// frozen at the moment playback stopped/expired — not a
		// misleading "0.0s"
		elapsed = m.lastElapsed
	}
	side := "-"
	if lvl < -0.5 {
		side = "L"
	} else if lvl > 0.5 {
		side = "R"
	}
	pct := 0.0
	if lvl < 0 {
		pct = -lvl
	} else {
		pct = lvl
	}
	pct = pct / 127.0 * 100.0

	mon := fmt.Sprintf("%s\n  %s  %s   %s  %.1f%%   elapsed %.1fs",
		lvlLabel,
		barLine(lvl), side, stateWord(m), pct, elapsed)
	b.WriteString(stMonitor.Render(mon))
	b.WriteString("\n\n")

	if m.overlay != overlayNone {
		title := "gain"
		if m.overlay == overlayAutocenter {
			title = "autocenter"
		}
		b.WriteString(stBox.Render(fmt.Sprintf(
			"%s: %3d%%  %s  ←/→ adjust (applied live) · esc close",
			title, m.overlayVal, slider10(m.overlayVal))))
		b.WriteString("\n\n")
	}

	b.WriteString(stHelp.Render(
		"  ↑/↓ row · ←/→ ±1 (shift ±10; hold to accelerate ×10/1.2s) · enter exact value\n" +
			"  space play/stop · i device-sign probe · u force re-upload\n" +
			"  g gain · a autocenter · esc back (stops+erases) · q quit (stops+erases)"))
	b.WriteString("\n")
	m.writeStatus(&b)
	return b.String()
}

func stateWord(m model) string {
	switch {
	case m.playing:
		return stOk.Render("playing")
	case m.uploaded:
		return stHeader.Render("stopped")
	default:
		return stDim.Render("idle")
	}
}

func slider10(pct int) string {
	n := (pct + 5) / 10
	if n > 10 {
		n = 10
	}
	return "[" + strings.Repeat("█", n) + strings.Repeat("·", 10-n) + "]"
}

// displayLevel computes the monitor level, applying the [i] probe's
// sign flip on top of the parity math.
func displayLevel(fx *Fx, tMs uint64, flipped bool) int {
	l := StreamLevel(fx, tMs)
	if flipped {
		return -l
	}
	return l
}
