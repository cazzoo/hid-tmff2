// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * synth.go — driver-parity force prediction for ffpanel.
 *
 * Every function here is a 1:1 port of the driver's own math (the C
 * originals are pinned per-function below; the executable contract is
 * tools/ffpanel/parity/vectors.txt, generated from verbatim copies of
 * the same C sources — see parity/harness.c). If the driver math
 * changes, regenerate the vectors and fix this file until the golden
 * test passes again: the tool must not lie.
 *
 * Pipeline mirrored from t500rs_synth_work() (src/tmt500rs/hid-tmt500rs.c):
 * sample -> dir_project -> clamp +/-32767 -> s8. The native constant path
 * (t500rs_send_constant_packet -> t500rs_scale_const_with_direction) is
 * mathematically the same pipeline.
 */
package main

// sinTable is verbatim from the kernel-internal header
// include/linux/fixp-arith.h (Copyright (c) 2002 Johann Deneux
// <johann.deneux@gmail.com>, GPL-2.0 — same license family as this repo).
// The vendored copy used by the parity harness is parity/fixp_arith.h.
var sinTable = [91]int32{
	0x00000000, 0x023be165, 0x04779632, 0x06b2f1d2, 0x08edc7b6, 0x0b27eb5c,
	0x0d61304d, 0x0f996a26, 0x11d06c96, 0x14060b67, 0x163a1a7d, 0x186c6ddd,
	0x1a9cd9ac, 0x1ccb3236, 0x1ef74bf2, 0x2120fb82, 0x234815ba, 0x256c6f9e,
	0x278dde6e, 0x29ac379f, 0x2bc750e8, 0x2ddf003f, 0x2ff31bdd, 0x32037a44,
	0x340ff241, 0x36185aee, 0x381c8bb5, 0x3a1c5c56, 0x3c17a4e7, 0x3e0e3ddb,
	0x3fffffff, 0x41ecc483, 0x43d464fa, 0x45b6bb5d, 0x4793a20f, 0x496af3e1,
	0x4b3c8c11, 0x4d084650, 0x4ecdfec6, 0x508d9210, 0x5246dd48, 0x53f9be04,
	0x55a6125a, 0x574bb8e5, 0x58ea90c2, 0x5a827999, 0x5c135399, 0x5d9cff82,
	0x5f1f5ea0, 0x609a52d1, 0x620dbe8a, 0x637984d3, 0x64dd894f, 0x6639b039,
	0x678dde6d, 0x68d9f963, 0x6a1de735, 0x6b598ea1, 0x6c8cd70a, 0x6db7a879,
	0x6ed9eba0, 0x6ff389de, 0x71046d3c, 0x720c8074, 0x730baeec, 0x7401e4bf,
	0x74ef0ebb, 0x75d31a5f, 0x76adf5e5, 0x777f903b, 0x7847d908, 0x7906c0af,
	0x79bc384c, 0x7a6831b8, 0x7b0a9f8c, 0x7ba3751c, 0x7c32a67c, 0x7cb82884,
	0x7d33f0c8, 0x7da5f5a3, 0x7e0e2e31, 0x7e6c924f, 0x7ec11aa3, 0x7f0bc095,
	0x7f4c7e52, 0x7f834ecf, 0x7fb02dc4, 0x7fd317b3, 0x7fec09e1, 0x7ffb025e,
	0x7fffffff,
}

// Sin16 mirrors fixp_sin16() from include/linux/fixp-arith.h: fold degrees
// into [0,360), then table lookup with 180/negative folding, >>16.
func Sin16(deg int) int {
	deg = ((deg % 360) + 360) % 360
	negative := false
	if deg > 180 {
		negative = true
		deg -= 180
	}
	if deg > 90 {
		deg = 180 - deg
	}
	ret := sinTable[deg]
	if negative {
		ret = -ret
	}
	return int(ret >> 16)
}

// dirProject mirrors t500rs_synth_dir_project() /
// t500rs_scale_const_with_direction() (src/tmt500rs/hid-tmt500rs.c).
// The direction is truncated to whole integer degrees FIRST
// (direction*360/0x10000 in integer arithmetic) — a float shortcut
// drifts ~0.003 deg and flips boundary golden vectors.
func dirProject(level int, direction uint16) int {
	deg := int(direction) * 360 / 0x10000
	return int((int64(level) * int64(Sin16(deg))) / 0x7fff)
}

// Envelope mirrors struct ff_envelope (uapi).
type Envelope struct {
	AttackLength uint16
	AttackLevel  uint16
	FadeLength   uint16
	FadeLevel    uint16
}

// Fx mirrors t500rs_synth_effect as inlined in the parity harness
// (parity/harness.c struct fx). Type is one of constant|sine|square|
// triangle|sawup|sawdown|ramp; rumble arrives pre-converted via
// RumbleConvert.
type Fx struct {
	Type       string
	Magnitude  int
	Offset     int
	PhaseCd    uint32
	PeriodMs   uint32
	Direction  uint16
	Env        Envelope
	DelayMs    uint32
	LengthMs   uint32
	Count      uint32
	StartLevel int
	EndLevel   int
}

// envelope mirrors t500rs_synth_envelope()
// (src/tmt500rs/hid-tmt500rs.c): attack ramps attack_level -> full,
// fade falls to fade_level over the final fade_length; result scaled
// by /32767. Integer truncation throughout, like the driver.
func envelope(sample int, env Envelope, tMs, lenMs uint32) int {
	scale := 32767
	fadeFrom := uint32(0)

	if lenMs != 0 && tMs > lenMs {
		tMs = lenMs
	}

	if env.AttackLength != 0 && tMs < uint32(env.AttackLength) {
		scale = int(env.AttackLevel) +
			((32767-int(env.AttackLevel))*int(tMs))/int(env.AttackLength)
	} else if env.FadeLength != 0 && lenMs != 0 {
		if lenMs > uint32(env.FadeLength) {
			fadeFrom = lenMs - uint32(env.FadeLength)
		}
		if tMs > fadeFrom {
			scale = int(env.FadeLevel) +
				((32767-int(env.FadeLevel))*int(lenMs-tMs))/
					int(lenMs-fadeFrom)
		}
	}

	return int((int64(sample) * int64(scale)) / 32767)
}

// fxSample mirrors t500rs_synth_sample() (src/tmt500rs/hid-tmt500rs.c):
// the OS-scale, direction-projected contribution at elapsed t (ms since
// PLAY, delay window included). Constants are NOT delay-gated (they
// expire on (delay+length)*count but play at full level during the
// delay window) and carry no envelope (the driver zeroes constant
// envelopes upstream). count>1 restarts each iteration; finite effects
// expire at delay + length*count.
func fxSample(e *Fx, elapsed uint64) int {
	var t uint32
	var s int

	if elapsed < uint64(e.DelayMs) {
		return 0
	}

	t = uint32(elapsed - uint64(e.DelayMs))

	if e.LengthMs != 0 {
		total := uint64(e.LengthMs) * uint64(e.Count)
		if uint64(t) >= total {
			return 0
		}
	}

	switch e.Type {
	case "constant":
		if e.LengthMs != 0 {
			total := uint64(e.DelayMs+e.LengthMs) * uint64(e.Count)
			if elapsed >= total {
				return 0
			}
		}
		return dirProject(e.Magnitude, e.Direction)
	case "ramp":
		length := uint32(1)
		if e.LengthMs != 0 {
			length = e.LengthMs
		}
		var tc uint32
		if e.LengthMs != 0 {
			tc = t % e.LengthMs
		} else if t < length {
			tc = t
		} else {
			tc = length
		}
		frac := int64(32767)
		if int64(tc) < int64(length) {
			frac = int64(tc) * 32767 / int64(length)
		}
		s = int(int64(e.StartLevel) +
			(int64(e.EndLevel-e.StartLevel)*frac)/32767)
		s = envelope(s, e.Env, tc, length)
	default: // sine, square, triangle, sawup, sawdown
		ti := t
		if e.LengthMs != 0 {
			ti = t % e.LengthMs
		}
		pos := uint32(((uint64(ti)*256)/uint64(e.PeriodMs) +
			(uint64(e.PhaseCd)*256)/36000) & 0xff)
		mag := e.Magnitude

		switch e.Type {
		case "square":
			if pos < 128 {
				s = mag
			} else {
				s = -mag
			}
		case "triangle":
			if pos < 128 {
				s = -mag + (2*mag*int(pos))/128
			} else {
				s = 3*mag - (2*mag*int(pos))/128
			}
		case "sawup":
			s = int(-int64(mag) + (2*int64(mag)*int64(pos))/255)
		case "sawdown":
			s = int(int64(mag) - (2*int64(mag)*int64(pos))/255)
		default: // sine
			s = int((int64(mag) * int64(Sin16(int(pos)*360/256))) / 0x7fff)
		}

		s += e.Offset
		s = envelope(s, e.Env, ti, e.LengthMs)
	}

	return dirProject(s, e.Direction)
}

// RumbleConvert mirrors the parent driver's tmff2_rewrite_rumble()
// (src/hid-tmff2.c, modeled on ff-core's rumble->periodic conversion):
// a sine with period 50 ms, magnitude strong/3 + weak/6 (integer
// division), direction forced to 16384, no envelope, count 1.
func RumbleConvert(strong, weak int, lengthMs uint32) Fx {
	return Fx{
		Type:      "sine",
		Magnitude: strong/3 + weak/6,
		PeriodMs:  50,
		Direction: 16384,
		LengthMs:  lengthMs,
		Count:     1,
	}
}

// ScaleS8 mirrors t500rs_scale_const_level_s8()
// (src/tmt500rs/hid-tmt500rs.c): clamp +/-32767, then (level*127)/32767.
func ScaleS8(level int) int {
	if level > 32767 {
		level = 32767
	}
	if level < -32767 {
		level = -32767
	}
	return int((int64(level) * 127) / 32767)
}

// StreamLevel is the full t500rs_synth_work() pipeline: sample ->
// clamp +/-32767 -> s8. This is the expected device stream byte at
// t ms since PLAY; negative = force left, positive = force right.
func StreamLevel(e *Fx, t uint64) int {
	return ScaleS8(fxSample(e, t))
}
