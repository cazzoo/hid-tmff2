// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ui_model.go — ffpanel root model (Bubble Tea Elm architecture):
 * screen state machine [devices] -> [effect type] -> [editor+monitor],
 * the 16 ms monitor tick, and the 30 ms update debounce.
 */
package main

import (
	"fmt"
	"strconv"
	"time"

	"github.com/charmbracelet/bubbles/textinput"
	tea "github.com/charmbracelet/bubbletea"
)

type screen int

const (
	screenDevices screen = iota
	screenType
	screenEditor
)

const (
	overlayNone       = 0
	overlayGain       = 1
	overlayAutocenter = 2
)

type tickMsg time.Time

func tick() tea.Cmd {
	return tea.Tick(16*time.Millisecond, func(t time.Time) tea.Msg {
		return tickMsg(t)
	})
}

// paramRow is one editable editor row; only rows valid for the effect
// type exist (no greyed no-ops: rumble has no direction/envelope, the
// driver zeroes constant envelopes, etc.).
type paramRow struct {
	label string
	get   func(*EffectParams) int
	set   func(*EffectParams, int)
	min   int
	max   int
	unit  string
}

func clamp(v, min, max int) int {
	if v < min {
		return min
	}
	if v > max {
		return max
	}
	return v
}

type model struct {
	cfg Config

	// devices screen
	devs       []DeviceInfo
	devCursor  int
	dev        *Device
	rescanHint string

	// effect-type screen
	types      []string
	typeCursor int

	// editor screen
	rows      []paramRow
	rowCursor int
	p         EffectParams
	input     textinput.Model
	editing   bool

	uploaded  bool
	playing   bool
	playStart time.Time
	playFx    Fx
	lastLevel int

	signFlipped bool // [i] device-sign probe

	overlay    int
	overlayVal int
	gainPct    int
	acPct      int

	dirty     bool
	changedAt time.Time

	// arrow-key hold tracking: repeated key events from the terminal's
	// auto-repeat grow the step logarithmically (see accelMult).
	holdKey   string
	holdSince time.Time
	holdLast  time.Time

	lastElapsed float64 // frozen elapsed shown after stop/expiry

	status string
	err    error
	width  int
	height int
}

func newModel() model {
	ti := textinput.New()
	ti.Placeholder = "value"
	ti.CharLimit = 8
	ti.Width = 10

	m := model{
		input: ti,
	}
	m.cfg = LoadConfig()
	m.devs = ScanDevices()
	m.signFlipped = m.cfg.DeviceSignFlipped
	m.gainPct = 100
	m.acPct = 0
	m.status = "pick a force-feedback device"
	return m
}

func (m model) Init() tea.Cmd {
	return tick()
}

func (m *model) setStatus(s string) {
	m.status = s
	m.err = nil
}

// selectDevice opens the chosen event node (O_RDWR) and moves to the
// effect-type screen. Non-FF devices are not selectable.
func (m *model) selectDevice() {
	if m.devCursor >= len(m.devs) {
		return
	}
	info := m.devs[m.devCursor]
	if !info.HasFF {
		m.setStatus(info.Name + " has no force feedback")
		return
	}
	dev, err := Open(info.Path)
	if err != nil {
		m.err = err
		return
	}
	if m.dev != nil {
		_ = m.dev.Close()
	}
	m.dev = dev
	m.cfg.LastDevice = info.Path
	_ = m.cfg.Save()

	m.types = nil
	for _, t := range allEffectTypes {
		if info.SupportsType(t) {
			m.types = append(m.types, t)
		}
	}
	if len(m.types) == 0 {
		m.err = fmt.Errorf("%s: device advertises EV_FF but no usable effect types", info.Path)
		return
	}
	m.typeCursor = 0
	for i, t := range m.types {
		if m.cfg.Defaults != nil && m.cfg.Defaults.Kind == t {
			m.typeCursor = i
			break
		}
	}
	m.setStatus("choose an effect type")
}

var allEffectTypes = []string{
	"constant", "sine", "square", "triangle", "sawup", "sawdown",
	"ramp", "rumble",
}

// startEditor builds parameter rows for the chosen type and enters the
// editor with dead-man defaults (30 s duration unless explicitly
// infinite — plan §7: no residual torque on crash/kill -9).
func (m *model) startEditor(kind string) {
	p := DefaultParams(kind)
	p.Duration = 30000
	if m.cfg.Defaults != nil && m.cfg.Defaults.Kind == kind {
		saved := *m.cfg.Defaults
		saved.Kind = kind
		p = saved
	}
	m.p = p
	m.rows = rowsFor(kind)
	m.rowCursor = 0
	m.playing = false
	m.uploaded = false
	m.dirty = false
	m.setStatus("adjust parameters, [space] plays")
}

// paramsChanged marks the effect dirty; the next tick after the 30 ms
// debounce runs a plain EVIOCSFF update on the existing id (never
// erase+create — that churns ids and slot-0 state).
func (m *model) paramsChanged() {
	m.dirty = true
	m.changedAt = time.Now()
}

func (m *model) applyUpdate(now time.Time) {
	if !m.dirty || !m.uploaded || m.dev == nil {
		return
	}
	if now.Sub(m.changedAt) < 30*time.Millisecond {
		return
	}
	m.dirty = false
	if err := m.dev.Update(&m.p); err != nil {
		m.err = err
		return
	}
	if m.playing {
		m.refreshPlayFx()
	}
	m.setStatus(fmt.Sprintf("updated id=%d", m.dev.effectID))
}

// refreshPlayFx rebuilds the monitor model from the edited params,
// preserving the running playback's repeat count: EVIOCSFF rewrites
// the effect table, but the running count comes from the EV_FF play
// event and cannot be updated mid-play (the driver sets e->count only
// in its play callback). A fresh [space] play picks up the new count.
func (m *model) refreshPlayFx() {
	running := m.playFx.Count
	m.playFx = m.p.ToFx()
	m.playFx.Count = running
}

// togglePlay is [space]: first press uploads + plays (value = count),
// later presses stop / restart.
func (m *model) togglePlay() {
	if m.dev == nil {
		return
	}
	if m.playing {
		if err := m.dev.Stop(); err != nil {
			m.err = err
			return
		}
		m.playing = false
		m.setStatus(fmt.Sprintf("stopped id=%d · last stream level %d",
			m.dev.effectID, m.lastLevel))
		return
	}
	if !m.uploaded {
		id, err := m.dev.Upload(&m.p)
		if err != nil {
			m.err = err
			return
		}
		m.uploaded = true
		m.dirty = false
		m.dev.effectID = id
	}
	if err := m.dev.Play(m.p.Count); err != nil {
		m.err = err
		return
	}
	m.playing = true
	m.playStart = time.Now()
	m.playFx = m.p.ToFx()
	m.setStatus(fmt.Sprintf("uploaded id=%d · playing", m.dev.effectID))
}

// expired reports whether the current playback window has fully
// elapsed: constants expire on (delay+length)*count, everything else
// on delay + length*count (mirrors fxSample's gating).
func (m *model) expired(now time.Time) bool {
	if !m.playing {
		return false
	}
	fx := m.playFx
	if fx.LengthMs == 0 {
		return false
	}
	var total uint64
	if fx.Type == "constant" {
		total = uint64(fx.DelayMs+fx.LengthMs) * uint64(fx.Count)
	} else {
		total = uint64(fx.DelayMs) + uint64(fx.LengthMs)*uint64(fx.Count)
	}
	return uint64(now.Sub(m.playStart).Milliseconds()) >= total
}

// forceReupload is [u]: erase + recreate + replay from scratch.
func (m *model) forceReupload() {
	if m.dev == nil {
		return
	}
	if _, err := m.dev.Reupload(&m.p); err != nil {
		m.err = err
		return
	}
	m.uploaded = true
	m.dirty = false
	// a re-upload implies a fresh play so the effect is observable
	if err := m.dev.Play(m.p.Count); err != nil {
		m.err = err
		return
	}
	m.playing = true
	m.playStart = time.Now()
	m.playFx = m.p.ToFx()
	m.setStatus(fmt.Sprintf("re-uploaded id=%d · playing", m.dev.effectID))
}

// shutdown stops and erases every uploaded effect and closes the fd —
// the no-residual-torque acceptance criterion.
func (m *model) shutdown() {
	if m.dev != nil {
		if err := m.dev.Close(); err != nil && m.err == nil {
			m.err = err
		}
		m.dev = nil
	}
	m.cfg.DeviceSignFlipped = m.signFlipped
	_ = m.cfg.Save()
}

// saveDefaults persists the current editor parameters as next-run
// defaults (config persistence, plan M4).
func (m *model) saveDefaults() {
	saved := m.p
	m.cfg.Defaults = &saved
	_ = m.cfg.Save()
}

func (m model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height
		return &m, nil

	case tickMsg:
		now := time.Time(msg)
		m.applyUpdate(now)
		if m.playing && m.dev != nil {
			t := uint64(now.Sub(m.playStart).Milliseconds())
			lvl := StreamLevel(&m.playFx, t)
			m.lastElapsed = float64(t) / 1000.0
			if m.expired(now) && lvl == 0 {
				m.playing = false
				m.setStatus(fmt.Sprintf(
					"expired id=%d · last stream level %d",
					m.dev.effectID, m.lastLevel))
			}
			m.lastLevel = lvl
		}
		return &m, tick()

	case tea.KeyMsg:
		if m.editing {
			return m.updateEditing(msg)
		}
		if m.overlay != overlayNone {
			return m.updateOverlay(msg)
		}
		switch msg.String() {
		case "ctrl+c", "q":
			m.shutdown()
			return &m, tea.Quit
		case "esc":
			switch m.screen() {
			case screenEditor:
				m.stopAndLeaveEditor()
				return &m, nil
			case screenType:
				if m.dev != nil {
					_ = m.dev.Close()
					m.dev = nil
				}
				m.setStatus("device closed · pick a device")
				return &m, nil
			}
			return &m, nil
		}
		switch m.screen() {
		case screenDevices:
			return m.updateDevices(msg)
		case screenType:
			return m.updateType(msg)
		case screenEditor:
			return m.updateEditor(msg)
		}
	}
	return &m, nil
}

func (m model) screen() screen {
	switch {
	case m.dev != nil && len(m.rows) > 0:
		return screenEditor
	case m.dev != nil:
		return screenType
	default:
		return screenDevices
	}
}

// stopAndLeaveEditor erases the effect and returns to type selection.
func (m *model) stopAndLeaveEditor() {
	if m.dev != nil {
		_ = m.dev.Stop()
		_ = m.dev.Erase()
		m.uploaded = false
		m.playing = false
	}
	m.saveDefaults()
	m.rows = nil
	m.setStatus("effect erased · choose an effect type")
}

// updateEditing handles the exact-value textinput ([enter] on a row).
func (m model) updateEditing(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	switch msg.String() {
	case "enter":
		if v, err := strconv.Atoi(m.input.Value()); err == nil {
			row := m.rows[m.rowCursor]
			row.set(&m.p, clamp(v, row.min, row.max))
			m.paramsChanged()
			m.setStatus(fmt.Sprintf("%s = %d", row.label, row.get(&m.p)))
		} else {
			m.setStatus("not a number — edit cancelled")
		}
		m.editing = false
		m.input.Blur()
		return &m, nil
	case "esc":
		m.editing = false
		m.input.Blur()
		return &m, nil
	}
	var cmd tea.Cmd
	m.input, cmd = m.input.Update(msg)
	return &m, cmd
}

// updateOverlay handles the gain/autocenter mini-sliders.
func (m model) updateOverlay(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	name := "gain"
	if m.overlay == overlayAutocenter {
		name = "autocenter"
	}
	apply := func() {
		if m.dev == nil {
			return
		}
		var err error
		if m.overlay == overlayGain {
			err = m.dev.SetGain(m.overlayVal)
			m.gainPct = m.overlayVal
		} else {
			err = m.dev.SetAutocenter(m.overlayVal)
			m.acPct = m.overlayVal
		}
		if err != nil {
			m.err = err
		} else {
			m.setStatus(fmt.Sprintf("%s set to %d%%", name, m.overlayVal))
		}
	}
	switch msg.String() {
	case "esc", "enter", "g", "a":
		m.overlay = overlayNone
		return &m, nil
	case "left", "-":
		m.overlayVal = clamp(m.overlayVal-1, 0, 100)
		apply()
	case "right", "+", "=":
		m.overlayVal = clamp(m.overlayVal+1, 0, 100)
		apply()
	case "shift+left":
		m.overlayVal = clamp(m.overlayVal-10, 0, 100)
		apply()
	case "shift+right":
		m.overlayVal = clamp(m.overlayVal+10, 0, 100)
		apply()
	}
	return &m, nil
}

func (m model) View() string {
	switch m.screen() {
	case screenDevices:
		return m.viewDevices()
	case screenType:
		return m.viewType()
	default:
		return m.viewEditor()
	}
}
