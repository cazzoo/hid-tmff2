// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * config.go — persisted ffpanel state at ~/.config/ffpanel.json:
 * the last device used, the M0 device-sign finding, and parameter
 * defaults. Tolerant of missing/corrupt files (defaults on any error).
 */
package main

import (
	"encoding/json"
	"os"
	"path/filepath"
)

// Config is the on-disk state.
type Config struct {
	LastDevice        string `json:"last_device,omitempty"`
	DeviceSignFlipped bool   `json:"device_sign_flipped,omitempty"`
	Defaults          *EffectParams `json:"defaults,omitempty"`
}

// ConfigPath returns ~/.config/ffpanel.json (XDG_CONFIG_HOME honored).
func ConfigPath() (string, error) {
	dir := os.Getenv("XDG_CONFIG_HOME")
	if dir == "" {
		home, err := os.UserHomeDir()
		if err != nil {
			return "", err
		}
		dir = filepath.Join(home, ".config")
	}
	return filepath.Join(dir, "ffpanel.json"), nil
}

// LoadConfig reads the config, returning zero-value defaults on any
// error (first run, unreadable file, stale schema).
func LoadConfig() Config {
	var c Config
	path, err := ConfigPath()
	if err != nil {
		return c
	}
	b, err := os.ReadFile(path)
	if err != nil {
		return c
	}
	_ = json.Unmarshal(b, &c)
	return c
}

// Save writes the config atomically (tmp + rename).
func (c Config) Save() error {
	path, err := ConfigPath()
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	b, err := json.MarshalIndent(c, "", "  ")
	if err != nil {
		return err
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, append(b, '\n'), 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}
