// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ui_devices.go — device list and effect-type selection screens.
 */
package main

import (
	"fmt"
	"strings"

	tea "github.com/charmbracelet/bubbletea"
)

func (m model) updateDevices(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	switch msg.String() {
	case "up", "k":
		if m.devCursor > 0 {
			m.devCursor--
		}
	case "down", "j":
		if m.devCursor < len(m.devs)-1 {
			m.devCursor++
		}
	case "r":
		m.devs = ScanDevices()
		if m.devCursor >= len(m.devs) {
			m.devCursor = 0
		}
		m.setStatus(fmt.Sprintf("rescanned: %d devices", len(m.devs)))
	case "enter", " ":
		m.selectDevice()
	}
	return &m, nil
}

func (m model) updateType(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	switch msg.String() {
	case "up", "k":
		if m.typeCursor > 0 {
			m.typeCursor--
		}
	case "down", "j":
		if m.typeCursor < len(m.types)-1 {
			m.typeCursor++
		}
	case "enter", " ":
		if m.typeCursor < len(m.types) {
			m.startEditor(m.types[m.typeCursor])
		}
	}
	return &m, nil
}

func (m model) viewDevices() string {
	var b strings.Builder
	b.WriteString(stTitle.Render(" ffpanel — devices "))
	b.WriteString("\n\n")

	if len(m.devs) == 0 {
		b.WriteString(stDim.Render(
			"no /dev/input/event* nodes — is the wheel plugged in? (opening needs root)"))
		b.WriteString("\n")
	} else {
		for i, d := range m.devs {
			cursor := "  "
			if i == m.devCursor {
				cursor = stCursor.Render("▸ ")
			}
			id := fmt.Sprintf("%04x:%04x", d.ID.Vendor, d.ID.Product)
			name := d.Name
			if name == "" {
				name = "?"
			}
			var line string
			if d.HasFF {
				line = fmt.Sprintf("%-18s %-9s %-28s %s",
					d.Path, id, name, stFF.Render(strings.Join(d.FFCaps, " ")))
			} else {
				line = stDim.Render(fmt.Sprintf("%-18s %-9s %-28s no force feedback",
					d.Path, id, name))
			}
			b.WriteString(cursor + line + "\n")
		}
	}
	b.WriteString("\n")
	b.WriteString(stHelp.Render(
		"  ↑/↓ select · enter open · r rescan · q quit"))
	b.WriteString("\n")
	m.writeStatus(&b)
	return b.String()
}

func (m model) viewType() string {
	var b strings.Builder
	name := m.dev.Info.Name
	b.WriteString(stTitle.Render(
		fmt.Sprintf(" ffpanel — %s (%s) ", name, m.dev.Info.Path)))
	b.WriteString("\n\n")
	b.WriteString(stHeader.Render("effect type:"))
	b.WriteString("\n")
	for i, t := range m.types {
		cursor := "  "
		if i == m.typeCursor {
			cursor = stCursor.Render("▸ ")
		}
		b.WriteString(cursor + prettyType(t) + "\n")
	}
	b.WriteString("\n")
	b.WriteString(stHelp.Render(
		"  ↑/↓ select · enter edit & play · esc back · q quit"))
	b.WriteString("\n")
	m.writeStatus(&b)
	return b.String()
}

func prettyType(t string) string {
	switch t {
	case "sawup":
		return "saw up"
	case "sawdown":
		return "saw down"
	}
	return t
}

func (m model) writeStatus(b *strings.Builder) {
	b.WriteString("\n")
	if m.err != nil {
		b.WriteString(stErr.Render("status: " + m.err.Error()))
	} else {
		b.WriteString(stDim.Render("status: " + m.status))
	}
	b.WriteString("\n")
}
