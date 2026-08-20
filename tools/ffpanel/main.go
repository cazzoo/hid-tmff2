// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ffpanel — interactive FF control panel for the hid-tmff2 driver
 * family (Go + Bubble Tea). Replaces tools/ffctl.c with a single
 * codebase: an interactive TUI by default and a scriptable one-shot
 * mode (`ffpanel play ...`) with the C tool's exact flags.
 *
 * The tool speaks only the Linux FF UAPI and its expected-force
 * monitor mirrors the driver's synthesis math 1:1 (see synth.go and
 * parity/vectors.txt).
 *
 * Build:  cd tools/ffpanel && go build -o ffpanel .
 * Usage:  sudo ./ffpanel                       (interactive TUI)
 *         sudo ./ffpanel play /dev/input/event26 sine --period 2000
 *         sudo ./ffpanel list
 */
package main

import (
	"fmt"
	"os"

	tea "github.com/charmbracelet/bubbletea"
)

func main() {
	if len(os.Args) > 1 {
		switch os.Args[1] {
		case "play":
			os.Exit(runPlay(os.Args[2:]))
		case "list":
			os.Exit(runList())
		case "-h", "--help", "help":
			fmt.Print(cliUsage)
			return
		case "-v", "--version":
			fmt.Println("ffpanel 1.0")
			return
		default:
			fmt.Fprintf(os.Stderr,
				"ffpanel: unknown subcommand %q\n\n%s", os.Args[1], cliUsage)
			os.Exit(1)
		}
	}

	if os.Geteuid() != 0 {
		fmt.Fprintln(os.Stderr,
			"ffpanel: warning: effect upload/play needs O_RDWR on the event node (usually root)")
	}

	p := tea.NewProgram(newModel(),
		tea.WithAltScreen(),
		tea.WithOutput(os.Stderr),
	)
	if _, err := p.Run(); err != nil {
		fmt.Fprintf(os.Stderr, "ffpanel: %v\n", err)
		os.Exit(1)
	}
}
