// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * input_linux_test.go — pins the 48-byte struct ff_effect marshalling
 * against byte images produced by C with the system uapi header
 * (gcc + include/linux/input.h, x86-64). If the kernel layout ever
 * changes on some platform, this fails loudly instead of uploading
 * garbage parameters.
 */
package main

import (
	"encoding/hex"
	"testing"
)

// TestMarshalLayout: golden images generated with C:
//
//	struct ff_effect e; memset(&e,0,sizeof(e)); <fields>; dump bytes.
func TestMarshalLayout(t *testing.T) {
	// periodic sine: id 7, dir 16384, replay 30000/250, sine period
	// 2000, magnitude -20000, offset 1234, phase 9000,
	// envelope 100/8000/200/4000
	per := EffectParams{
		Kind: "sine", Period: 2000, Magnitude: -20000, Offset: 1234,
		Phase: 9000, Direction: 16384, Duration: 30000, Delay: 250,
		Attack: 100, AttackLevel: 8000, Fade: 200, FadeLevel: 4000,
	}
	want := "510007000040000000003075fa0000005a00d007e0b1d20428236400401fc800a00f0000000000000000000000000000"
	if got := hexImage(imageOf(per, 7)); got != want {
		t.Errorf("periodic marshal:\n got %s\nwant %s", got, want)
	}

	// constant: id -1, dir 49152, length 5000, level -12345,
	// envelope 1/2/3/4 (marshalled even though the driver zeroes it)
	cst := EffectParams{
		Kind: "constant", Magnitude: -12345, Direction: 49152,
		Duration: 5000, Attack: 1, AttackLevel: 2, Fade: 3, FadeLevel: 4,
	}
	want = "5200ffff00c000000000881300000000c7cf010002000300040000000000000000000000000000000000000000000000"
	if got := hexImage(imageOf(cst, -1)); got != want {
		t.Errorf("constant marshal:\n got %s\nwant %s", got, want)
	}

	// ramp: id 3, dir 1, replay 1000/50, start -1000, end 2000,
	// envelope 10/20/30/40
	rmp := EffectParams{
		Kind: "ramp", Direction: 1, Duration: 1000, Delay: 50,
		Start: -1000, End: 2000, Attack: 10, AttackLevel: 20, Fade: 30,
		FadeLevel: 40,
	}
	want = "57000300010000000000e8033200000018fcd0070a0014001e0028000000000000000000000000000000000000000000"
	if got := hexImage(imageOf(rmp, 3)); got != want {
		t.Errorf("ramp marshal:\n got %s\nwant %s", got, want)
	}

	// rumble: id 5, length 3000, strong 20000, weak 10000
	rmb := EffectParams{
		Kind: "rumble", Duration: 3000, Strong: 20000, Weak: 10000,
	}
	want = "50000500000000000000b80b00000000204e102700000000000000000000000000000000000000000000000000000000"
	if got := hexImage(imageOf(rmb, 5)); got != want {
		t.Errorf("rumble marshal:\n got %s\nwant %s", got, want)
	}

	// gain pseudo-effect: id -1, everything else zero
	gain := EffectParams{Kind: "gain"}
	want = "6000ffff0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
	if got := hexImage(imageOf(gain, -1)); got != want {
		t.Errorf("gain marshal:\n got %s\nwant %s", got, want)
	}
}

func hexImage(b []byte) string { return hex.EncodeToString(b) }

func imageOf(p EffectParams, id int16) []byte {
	b := p.Marshal(id)
	return b[:]
}
