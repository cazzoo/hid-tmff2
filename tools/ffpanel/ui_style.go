// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ui_style.go — lipgloss styles for the ffpanel TUI.
 */
package main

import "github.com/charmbracelet/lipgloss"

var (
	stTitle = lipgloss.NewStyle().
		Bold(true).
		Foreground(lipgloss.Color("15")).
		Background(lipgloss.Color("62")).
		Padding(0, 1)

	stHeader = lipgloss.NewStyle().
		Foreground(lipgloss.Color("252"))

	stSelected = lipgloss.NewStyle().
		Bold(true).
		Foreground(lipgloss.Color("15")).
		Background(lipgloss.Color("62"))

	stDim = lipgloss.NewStyle().
		Foreground(lipgloss.Color("241"))

	stFF = lipgloss.NewStyle().
		Foreground(lipgloss.Color("47"))

	stErr = lipgloss.NewStyle().
		Foreground(lipgloss.Color("203"))

	stOk = lipgloss.NewStyle().
		Foreground(lipgloss.Color("48"))

	stWarn = lipgloss.NewStyle().
		Foreground(lipgloss.Color("214"))

	stBox = lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(lipgloss.Color("62")).
		Padding(0, 1)

	stMonitor = lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(lipgloss.Color("89")).
		Padding(0, 1)

	stCursor = lipgloss.NewStyle().
		Foreground(lipgloss.Color("212")).
		Bold(true)

	stLabel = lipgloss.NewStyle().
		Width(15)

	stValue = lipgloss.NewStyle().
		Foreground(lipgloss.Color("86"))

	stHelp = lipgloss.NewStyle().
		Foreground(lipgloss.Color("241"))
)
