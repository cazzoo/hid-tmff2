// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * synth_test.go — golden-vector parity test.
 *
 * Asserts that StreamLevel() reproduces every case in
 * tools/ffpanel/parity/vectors.txt within +/-1. The vectors are
 * generated from verbatim copies of the driver's C math
 * (parity/harness.c); regenerate them whenever the driver changes:
 *
 *   gcc -O2 -Wall -Wextra -std=gnu11 -o /tmp/harness tools/ffpanel/parity/harness.c
 *   /tmp/harness > tools/ffpanel/parity/vectors.txt
 */
package main

import (
	"bufio"
	"os"
	"strconv"
	"strings"
	"testing"
)

// TestGoldenVectors walks vectors.txt (format pinned in harness.c) and
// compares each expected level against the Go port. Rumble rows carry
// strong/weak in the start/end columns and are run through
// RumbleConvert first, exactly like the harness's emit_rumble().
func TestGoldenVectors(t *testing.T) {
	f, err := os.Open("parity/vectors.txt")
	if err != nil {
		t.Fatalf("open vectors: %v (run from tools/ffpanel)", err)
	}
	defer f.Close()

	sc := bufio.NewScanner(f)
	n := 0
	for sc.Scan() {
		line := sc.Text()
		if line == "" || line[0] == '#' {
			continue
		}
		n++
		fields := strings.Split(line, ",")
		if len(fields) != 19 {
			t.Fatalf("line %d: %d fields, want 19 (%q)", n, len(fields), line)
		}
		nums := make([]int64, len(fields))
		for i, fs := range fields {
			if i == 0 {
				continue
			}
			v, err := strconv.ParseInt(fs, 10, 64)
			if err != nil {
				t.Fatalf("line %d field %d parse: %v (%q)", n, i, err, line)
			}
			nums[i] = v
		}

		typ := fields[0]
		mag := int(nums[1])
		offset := int(nums[2])
		phase := uint32(nums[3])
		period := uint32(nums[4])
		dir := uint16(nums[5])
		atkLen := uint16(nums[6])
		atkLvl := uint16(nums[7])
		fadeLen := uint16(nums[8])
		fadeLvl := uint16(nums[9])
		delay := uint32(nums[10])
		length := uint32(nums[11])
		count := uint32(nums[12])
		start := int(nums[13])
		end := int(nums[14])
		strong := int(nums[15])
		weak := int(nums[16])
		tms := uint64(nums[17])
		want := int(nums[18])

		var fx Fx
		if typ == "rumble" {
			fx = RumbleConvert(strong, weak, length)
		} else {
			fx = Fx{
				Type:       typ,
				Magnitude:  mag,
				Offset:     offset,
				PhaseCd:    phase,
				PeriodMs:   period,
				Direction:  dir,
				Env:        Envelope{AttackLength: atkLen, AttackLevel: atkLvl, FadeLength: fadeLen, FadeLevel: fadeLvl},
				DelayMs:    delay,
				LengthMs:   length,
				Count:      count,
				StartLevel: start,
				EndLevel:   end,
			}
		}

		got := StreamLevel(&fx, tms)
		if abs(got-want) > 1 {
			t.Errorf("line %d %s t=%d: got %d, want %d (fx=%+v)",
				n, typ, tms, got, want, fx)
		}
	}
	if err := sc.Err(); err != nil {
		t.Fatal(err)
	}
	if n < 400 {
		t.Fatalf("suspiciously few vectors: %d", n)
	}
	t.Logf("checked %d golden vectors", n)
}

// TestDirectionTruncation pins the integer-degree truncation contract
// on the exact boundary directions the harness exercises: direction=1
// projects to zero, 16384 to full.
func TestDirectionTruncation(t *testing.T) {
	full := dirProject(32767, 16384)
	if full != 32767 {
		t.Errorf("dirProject(32767, 16384) = %d, want 32767", full)
	}
	if got := dirProject(32767, 1); got != 0 {
		t.Errorf("dirProject(32767, 1) = %d, want 0 (integer-degree truncation)", got)
	}
	if got := dirProject(32767, 0); got != 0 {
		t.Errorf("dirProject(32767, 0) = %d, want 0 (direction 0 = zero force trap)", got)
	}
}

// TestScaleS8 pins the clamp and the (level*127)/32767 truncation.
func TestScaleS8(t *testing.T) {
	for _, tc := range []struct {
		in, want int
	}{{32767, 127}, {-32767, -127}, {0, 0}, {40000, 127}, {-40000, -127},
		{258, 0}, {259, 1}, {-259, -1}, {16383, 63}, {16384, 63}} {
		if got := ScaleS8(tc.in); got != tc.want {
			t.Errorf("ScaleS8(%d) = %d, want %d", tc.in, got, tc.want)
		}
	}
}

func abs(x int) int {
	if x < 0 {
		return -x
	}
	return x
}

// TestRumbleIntegerDivision pins the strong/3 + weak/6 truncation.
func TestRumbleIntegerDivision(t *testing.T) {
	fx := RumbleConvert(20000, 10000, 0)
	if fx.Magnitude != 20000/3+10000/6 {
		t.Fatalf("rumble magnitude %d, want %d", fx.Magnitude,
			20000/3+10000/6)
	}
	if fx.PeriodMs != 50 || fx.Direction != 16384 || fx.Count != 1 {
		t.Fatalf("rumble conversion shape wrong: %+v", fx)
	}
}
