// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ui_model_test.go — regression tests for the monitor's playback-state
 * model: count>1 iteration windows, rumble play-event properties, the
 * mid-play count-preservation rule, and the arrow-hold acceleration.
 */
package main

import (
	"testing"
	"time"
)

// TestRumbleFxPropagatesPlayProps pins the count>1 monitor fix: the
// rumble Fx must carry the effect's delay and the play-event count,
// not RumbleConvert's vector-pinning defaults (count=1, delay=0).
// With the defaults the monitor expired a 3-iteration rumble at the
// start of iteration 2: "elapsed 0" while the wheel kept rumbling and
// [space] re-played instead of stopping.
func TestRumbleFxPropagatesPlayProps(t *testing.T) {
	p := DefaultParams("rumble")
	p.Duration = 3000
	p.Delay = 250
	p.Count = 3
	fx := p.ToFx()
	if fx.Count != 3 || fx.DelayMs != 250 {
		t.Fatalf("rumble fx count=%d delay=%d, want 3/250", fx.Count, fx.DelayMs)
	}
	if fx.PeriodMs != 50 || fx.Direction != 16384 {
		t.Fatalf("rumble conversion shape wrong: %+v", fx)
	}
}

// TestExpiredAcrossIterations: iteration 2 of a count>1 playback must
// NOT read as expired; only past delay + length*count (constants:
// (delay+length)*count) may.
func TestExpiredAcrossIterations(t *testing.T) {
	m := model{playing: true}

	p := DefaultParams("sine")
	p.Duration = 3000
	p.Count = 3
	m.playFx = p.ToFx()

	m.playStart = time.Now().Add(-3100 * time.Millisecond)
	if m.expired(time.Now()) {
		t.Fatal("sine iteration 2 of 3 wrongly expired")
	}
	m.playStart = time.Now().Add(-9500 * time.Millisecond)
	if !m.expired(time.Now()) {
		t.Fatal("sine past delay+length*count should be expired")
	}

	r := DefaultParams("rumble")
	r.Duration = 1000
	r.Count = 3
	m.playFx = r.ToFx()
	m.playStart = time.Now().Add(-1500 * time.Millisecond)
	if m.expired(time.Now()) {
		t.Fatal("rumble iteration 2 of 3 wrongly expired")
	}
	m.playStart = time.Now().Add(-3500 * time.Millisecond)
	if !m.expired(time.Now()) {
		t.Fatal("rumble past total should be expired")
	}

	c := DefaultParams("constant")
	c.Duration = 1000
	c.Delay = 500
	c.Count = 2
	m.playFx = c.ToFx()
	m.playStart = time.Now().Add(-2000 * time.Millisecond)
	if m.expired(time.Now()) {
		t.Fatal("constant inside (delay+length)*count wrongly expired")
	}
	m.playStart = time.Now().Add(-3100 * time.Millisecond)
	if !m.expired(time.Now()) {
		t.Fatal("constant past (delay+length)*count should be expired")
	}
}

// TestRefreshPlayFxKeepsRunningCount pins the mid-play rule: EVIOCSFF
// updates the effect table but cannot change the running playback's
// repeat count, so the monitor keeps modeling the old count until a
// new EV_FF play.
func TestRefreshPlayFxKeepsRunningCount(t *testing.T) {
	p := DefaultParams("sine")
	p.Period = 777
	p.Duration = 4000
	p.Count = 5
	m := model{p: p}
	m.playFx = p.ToFx()
	m.playFx.Count = 2 // what the running playback actually uses

	m.refreshPlayFx()

	if m.playFx.Count != 2 {
		t.Fatalf("running count clobbered: got %d, want 2", m.playFx.Count)
	}
	if m.playFx.PeriodMs != 777 || m.playFx.LengthMs != 4000 {
		t.Fatalf("table fields not refreshed: %+v", m.playFx)
	}
}

// TestAccelMult pins the hold curve: 1x for taps (<400 ms), one decade
// per additional 1.2 s of holding, capped at 1000x.
func TestAccelMult(t *testing.T) {
	for _, tc := range []struct {
		held time.Duration
		want int
	}{
		{0, 1}, {300 * time.Millisecond, 1},
		{400 * time.Millisecond, 10}, {500 * time.Millisecond, 10},
		{1 * time.Second, 10},
		{1600 * time.Millisecond, 100},
		{2800 * time.Millisecond, 1000},
		{10 * time.Second, 1000},
	} {
		if got := accelMult(tc.held); got != tc.want {
			t.Errorf("accelMult(%v) = %d, want %d", tc.held, got, tc.want)
		}
	}
}
