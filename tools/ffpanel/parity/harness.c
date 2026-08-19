// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Golden-vector harness for the ffpanel parity contract.
 *
 * Each *_c() function below is a VERBATIM copy of the corresponding driver
 * function (source pinned in each comment). If the driver math changes,
 * re-copy the body here and regenerate the vectors:
 *
 *   gcc -O2 -Wall -Wextra -std=gnu11 -o /tmp/harness tools/ffpanel/parity/harness.c
 *   /tmp/harness > tools/ffpanel/parity/vectors.txt
 *
 * vectors.txt is the contract the Go port (tools/ffpanel/synth.go) is tested
 * against, within +/-1. Line format (CSV, one sample per line):
 *
 *   type,mag,offset,phase_cd,period_ms,direction,atk_len,atk_lvl,fade_len,
 *   fade_lvl,delay_ms,length_ms,count,start,end,strong,weak,t_ms,level
 *
 * type: constant|sine|square|triangle|sawup|sawdown|ramp|rumble
 * t_ms is elapsed time since PLAY (includes the delay window).
 * level is the expected device stream byte, -127..127 (s8).
 *
 * Pipeline mirrored from t500rs_synth_work(): sample -> dir_project ->
 * clamp +-32767 -> s8. Native constant path (t500rs_send_constant_packet)
 * is scale_const_with_direction(), mathematically the same pipeline.
 *
 * Rumble mirrors the parent's tmff2_convert_rumble() (src/hid-tmff2.c):
 * converted at upload to sine periodic, period 50, magnitude strong/3 +
 * weak/6 (integer division), direction forced 16384.
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "fixp_arith.h"

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef int64_t s64;

/* Verbatim: src/tmt500rs/hid-tmt500rs.c t500rs_scale_const_level_s8() */
static s8 scale_const_level_s8(int level)
{
	if (level > 32767)
		level = 32767;
	if (level < -32767)
		level = -32767;

	return (s8)((level * 127LL) / 32767);
}

/* Verbatim: src/tmt500rs/hid-tmt500rs.c t500rs_synth_dir_project() */
static int dir_project(int level, u16 direction)
{
	return (int)(((s64)level * fixp_sin16(direction * 360 / 0x10000)) /
		     0x7fff);
}

struct env {
	u16 attack_length;
	u16 attack_level;
	u16 fade_length;
	u16 fade_level;
};

/* Verbatim (struct inlined): t500rs_synth_effect / t500rs_synth_envelope() */
static int envelope(int sample, const struct env *env, u32 t_ms, u32 len_ms)
{
	int scale = 32767;
	u32 fade_from = 0;

	if (len_ms && t_ms > len_ms)
		t_ms = len_ms;

	if (env->attack_length && t_ms < env->attack_length) {
		scale = env->attack_level +
			((32767 - env->attack_level) * (int)t_ms) /
				(int)env->attack_length;
	} else if (env->fade_length && len_ms) {
		fade_from = len_ms > env->fade_length ?
			    len_ms - env->fade_length : 0;
		if (t_ms > fade_from)
			scale = env->fade_level +
				((32767 - env->fade_level) *
				 (int)(len_ms - t_ms)) /
					(int)(len_ms - fade_from);
	}

	return (int)(((s64)sample * scale) / 32767);
}

struct fx {
	const char *type;
	int magnitude;
	int offset;
	u32 phase_cd;
	u32 period_ms;
	u16 direction;
	struct env env;
	u32 delay_ms;
	u32 length_ms;
	u32 count;
	int start_level, end_level;
};

/* Verbatim: t500rs_synth_sample() (field accesses inlined via struct fx).
 * Returns the OS-scale, direction-projected contribution at elapsed t,
 * or 0 inside the delay window / after expiry. */
static int sample(const struct fx *e, u64 elapsed)
{
	u64 total;
	u32 t;
	int s;

	if (elapsed < e->delay_ms)
		return 0;

	t = (u32)(elapsed - e->delay_ms);

	if (e->length_ms) {
		total = (u64)e->length_ms * e->count;

		if ((u64)t >= total)
			return 0;
	}

	if (!strcmp(e->type, "constant")) {
		/* Mirrors the synth_work constant path: expiry on
		 * (delay+length)*count, no delay-window gating, no envelope
		 * (native constant envelopes are forced to zeros upstream). */
		if (e->length_ms) {
			u64 total = (u64)(e->delay_ms + e->length_ms) *
				    e->count;

			if (elapsed >= total)
				return 0;
		}
		return dir_project(e->magnitude, e->direction);
	}

	if (!strcmp(e->type, "ramp")) {
		u32 len = e->length_ms ? e->length_ms : 1;
		u32 tc = e->length_ms ? (t % e->length_ms) :
					(t < len ? t : len);
		s64 frac = tc >= len ? 32767 : (s64)tc * 32767 / len;

		s = (int)(e->start_level +
			  ((s64)(e->end_level - e->start_level) * frac) /
				  32767);
		s = envelope(s, &e->env, tc, len);
	} else {
		u32 ti = e->length_ms ? (t % e->length_ms) : t;
		u32 pos = (((u64)ti * 256) / e->period_ms +
			   ((u64)e->phase_cd * 256) / 36000) & 0xff;
		int mag = e->magnitude;

		if (!strcmp(e->type, "square")) {
			s = pos < 128 ? mag : -mag;
		} else if (!strcmp(e->type, "triangle")) {
			s = pos < 128 ?
				    -mag + (2 * mag * (int)pos) / 128 :
				    3 * mag - (2 * mag * (int)pos) / 128;
		} else if (!strcmp(e->type, "sawup")) {
			s = (int)(-mag + ((s64)2 * mag * pos) / 255);
		} else if (!strcmp(e->type, "sawdown")) {
			s = (int)(mag - ((s64)2 * mag * pos) / 255);
		} else { /* sine, rumble (rumble arrives pre-converted) */
			s = (int)(((s64)mag *
				   fixp_sin16((int)pos * 360 / 256)) /
				  0x7fff);
		}

		s += e->offset;
		s = envelope(s, &e->env, ti, e->length_ms);
	}

	return dir_project(s, e->direction);
}

/* Full pipeline: t500rs_synth_work() clamp + s8 conversion. */
static int stream_level(const struct fx *e, u64 t)
{
	int total = sample(e, t);

	if (total > 32767)
		total = 32767;
	else if (total < -32767)
		total = -32767;
	return scale_const_level_s8(total);
}

static void emit(const struct fx *e, u64 t)
{
	printf("%s,%d,%d,%" PRIu32 ",%" PRIu32 ",%" PRIu16
	       ",%" PRIu16 ",%" PRIu16 ",%" PRIu16 ",%" PRIu16
	       ",%" PRIu32 ",%" PRIu32 ",%" PRIu32
	       ",%d,%d,0,0,%" PRIu64 ",%d\n",
	       e->type, e->magnitude, e->offset, e->phase_cd, e->period_ms,
	       e->direction, e->env.attack_length, e->env.attack_level,
	       e->env.fade_length, e->env.fade_level, e->delay_ms,
	       e->length_ms, e->count, e->start_level, e->end_level, t,
	       stream_level(e, t));
}

static void emit_rumble(int strong, int weak, u32 length_ms, u64 t)
{
	struct fx r = {
		.type = "sine", /* parent converts rumble to sine@50ms/16384 */
		.magnitude = strong / 3 + weak / 6,
		.period_ms = 50,
		.direction = 16384,
		.length_ms = length_ms,
		.count = 1,
	};

	printf("rumble,%d,0,0,50,%" PRIu16
	       ",0,0,0,0,0,%" PRIu32 ",1,0,0,%d,%d,%" PRIu64 ",%d\n",
	       r.magnitude, r.direction, length_ms, strong, weak,
	       t, stream_level(&r, t));
}

int main(void)
{
	static const u16 dirs[] = { 0, 1, 16383, 16384, 32768, 49152, 65535 };
	static const int mags[] = { 32767, -32767, 20000, -20000, 1, -1 };
	struct fx e;
	size_t i, j;

	/* --- constant: level x direction grid (delay window too) --- */
	for (i = 0; i < sizeof(mags) / sizeof(mags[0]); i++)
		for (j = 0; j < sizeof(dirs) / sizeof(dirs[0]); j++) {
			e = (struct fx){ .type = "constant",
					 .magnitude = mags[i],
					 .direction = dirs[j] };
			emit(&e, 100);
		}
	e = (struct fx){ .type = "constant", .magnitude = 20000,
			 .direction = 16384, .delay_ms = 500,
			 .length_ms = 1000, .count = 1 };
	emit(&e, 499); /* driver does not gate constants during delay */
	emit(&e, 500);
	emit(&e, 1499);
	emit(&e, 1500); /* expired: (delay+length)*count elapsed */

	/* --- sine: quarter-period walk + direction/mag/phase/period --- */
	e = (struct fx){ .type = "sine", .magnitude = 20000,
			 .period_ms = 2000, .direction = 16384 };
	for (u64 t = 0; t <= 2050; t += 37)
		emit(&e, t);
	for (j = 0; j < sizeof(dirs) / sizeof(dirs[0]); j++) {
		e.direction = dirs[j];
		emit(&e, 500);
	}
	static const int smags[] = { 0, 1, 32767, 12345 };
	for (i = 0; i < 4; i++) {
		e = (struct fx){ .type = "sine", .magnitude = smags[i],
				 .period_ms = 2000, .direction = 16384 };
		emit(&e, 500);
	}
	static const u32 phases[] = { 0, 9000, 18000, 27000, 35999 };
	for (i = 0; i < 5; i++) {
		e = (struct fx){ .type = "sine", .magnitude = 20000,
				 .phase_cd = phases[i], .period_ms = 2000,
				 .direction = 16384 };
		emit(&e, 0);
		emit(&e, 500);
	}
	static const u32 periods[] = { 1, 50, 65535 };
	for (i = 0; i < 3; i++) {
		e = (struct fx){ .type = "sine", .magnitude = 20000,
				 .period_ms = periods[i], .direction = 16384 };
		emit(&e, 0);
		emit(&e, 13);
		emit(&e, 1234);
	}

	/* --- sine with envelope + finite length --- */
	e = (struct fx){ .type = "sine", .magnitude = 20000, .period_ms = 100,
			 .direction = 16384,
			 .env = { 200, 8000, 300, 4000 },
			 .length_ms = 1000, .count = 1 };
	for (u64 t = 0; t <= 1050; t += 53)
		emit(&e, t);

	/* --- count>1 iteration restart: length 400, count 3 --- */
	e = (struct fx){ .type = "sine", .magnitude = 20000, .period_ms = 400,
			 .direction = 16384, .length_ms = 400, .count = 3 };
	emit(&e, 399);
	emit(&e, 400); /* iteration 2 begins: waveform restarts */
	emit(&e, 410);
	emit(&e, 1199);
	emit(&e, 1200); /* expired */

	/* --- other waveforms: sign edges + walk --- */
	static const char *wavs[] = { "square", "triangle", "sawup",
				      "sawdown" };
	for (i = 0; i < 4; i++) {
		e = (struct fx){ .type = wavs[i], .magnitude = 10000,
				 .period_ms = 400, .direction = 16384 };
		for (u64 t = 0; t <= 810; t += 37)
			emit(&e, t);
		/* pos-boundary ts: t where pos crosses 127/128 and 255/0 */
		static const u64 bt[] = { 195, 199, 200, 201, 205, 399, 400 };
		for (j = 0; j < sizeof(bt) / sizeof(bt[0]); j++)
			emit(&e, bt[j]);
	}

	/* --- ramp: sweeps, hold past length, envelope, count restart --- */
	static const int se[3][2] = { { 0, 32767 },
				      { 32767, 0 },
				      { -16384, 16384 } };
	for (i = 0; i < 3; i++) {
		e = (struct fx){ .type = "ramp", .start_level = se[i][0],
				 .end_level = se[i][1], .direction = 16384,
				 .length_ms = 1000, .count = 1 };
		for (u64 t = 0; t <= 1500; t += 61)
			emit(&e, t);
	}
	e = (struct fx){ .type = "ramp", .start_level = -20000,
			 .end_level = 20000, .direction = 16384,
			 .env = { 250, 6000, 250, 12000 },
			 .length_ms = 1000, .count = 2 };
	for (u64 t = 0; t <= 2100; t += 97)
		emit(&e, t);
	e = (struct fx){ .type = "ramp", .start_level = 0, .end_level = 32767,
			 .direction = 16384 }; /* infinite: sweep once, hold */
	emit(&e, 0);
	emit(&e, 1);
	emit(&e, 100000);

	/* --- clamp: waveform + offset overflow --- */
	e = (struct fx){ .type = "sine", .magnitude = 32767, .offset = 32767,
			 .period_ms = 100, .direction = 16384 };
	emit(&e, 25);
	emit(&e, 75);
	e = (struct fx){ .type = "sawdown", .magnitude = 32767,
			 .offset = -32768, .period_ms = 100,
			 .direction = 16384 };
	emit(&e, 0);
	emit(&e, 50);

	/* --- rumble: integer-division conversion pinned --- */
	static const int rb[4][2] = { { 0, 0 }, { 20000, 10000 },
				      { 32767, 0 }, { 1, 2 } };
	for (i = 0; i < 4; i++)
		for (u64 t = 0; t <= 150; t += 13)
			emit_rumble(rb[i][0], rb[i][1], 0, t);
	emit_rumble(20000, 10000, 1000, 999);
	emit_rumble(20000, 10000, 1000, 1000);

	return 0;
}
