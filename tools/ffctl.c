// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ffctl - upload and play one Linux force-feedback effect with explicit
 * command-line parameters. fftest's interactive scanf prompts are unusable
 * on some setups, and failed input leaves its variables uninitialized
 * (i.e. unknown period/magnitude). Every parameter here is explicit.
 *
 * Build:  gcc -O2 -Wall -o ffctl tools/ffctl.c -lm
 *
 * While an effect plays, a live slider shows the expected force at the
 * wheel: handle on the left = pull left, right = push right. It mirrors
 * the driver's synthesis math exactly (direction projection, waveform
 * sampling, envelope shaping, and the parent's rumble -> sine/50ms
 * conversion), so the handle is a prediction of the wire, not a sketch.
 * At the 16 ms render rate a 20 Hz rumble aliases into side-flicker -
 * that is expected.
 *
 * Examples:
 *   sudo ./ffctl /dev/input/event26 sine --period 2000 --duration 5000
 *   sudo ./ffctl /dev/input/event26 square --period 300 --magnitude 12000
 *   sudo ./ffctl /dev/input/event26 constant --magnitude 20000
 *   sudo ./ffctl /dev/input/event26 ramp --start -10000 --end 10000 \
 *       --attack 1000 --attack-level 16000 --fade 1000 --fade-level 16000
 *   sudo ./ffctl /dev/input/event26 rumble --strong 20000 --weak 10000
 *
 * Direction: hid-tmff2 projects forces with sin(direction * 360 / 65536),
 * so direction 0 (north) produces ZERO force on these wheels. The default
 * here is 16384 (90 degrees = full force along the wheel axis).
 *
 * Frequency reference for periodic effects (cycles per second = 1000/period):
 *   2000 ms -> 0.5 Hz  slow wobble   | 200 ms ->  5 Hz coarse rumble
 *   1000 ms -> 1 Hz    gentle swing  | 100 ms -> 10 Hz buzz
 *    500 ms -> 2 Hz    fast swing    |  50 ms -> 20 Hz fine rumble
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int fd = -1;
static int effect_id = -1;

static void stop_and_erase(void)
{
	struct input_event stop;

	if (fd < 0 || effect_id < 0)
		return;

	memset(&stop, 0, sizeof(stop));
	stop.type = EV_FF;
	stop.code = effect_id;
	stop.value = 0;
	if (write(fd, &stop, sizeof(stop)) < 0)
		perror("stop effect");
	if (ioctl(fd, EVIOCRMFF, effect_id) < 0)
		perror("erase effect");
	effect_id = -1;
}

static void on_signal(int sig)
{
	(void)sig;
	write(STDOUT_FILENO, "\n", 1);
	stop_and_erase();
	_exit(0);
}

static void msleep(long ms)
{
	struct timespec ts = {
		.tv_sec = ms / 1000,
		.tv_nsec = (ms % 1000) * 1000000,
	};

	nanosleep(&ts, NULL);
}

static long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Mirror of t500rs_synth_dir_project()/t500rs_scale_const_with_direction():
 * the driver projects with fixp_sin16(direction * 360 / 0x10000) / 0x7fff -
 * integer-degree truncation included - so a direction of 0 yields zero
 * force and 16384 (90 deg) yields full force. */
static double dir_project(double level, long direction)
{
	long deg = direction * 360 / 65536;

	return level * sin(deg * M_PI / 180.0);
}

/* Mirror of t500rs_synth_envelope(): attack ramps attack_level -> full,
 * fade falls to fade_level over the final fade_length. */
static double apply_envelope(double sample, const struct ff_envelope *env,
			     long ti_ms, long length_ms)
{
	double scale = 32767.0;

	if (length_ms && ti_ms > length_ms)
		ti_ms = length_ms;

	if (env->attack_length && ti_ms < env->attack_length) {
		scale = env->attack_level +
			(32767.0 - env->attack_level) * ti_ms /
				env->attack_length;
	} else if (env->fade_length && length_ms) {
		long fade_from = length_ms > env->fade_length ?
				 length_ms - env->fade_length : 0;

		if (ti_ms > fade_from)
			scale = env->fade_level +
				(32767.0 - env->fade_level) * (length_ms - ti_ms) /
					(length_ms - fade_from);
	}
	return sample * scale / 32767.0;
}

/* Expected device stream level (-127..+127) at t_ms into playback,
 * mirroring t500rs_synth_sample(). Negative = force left, positive =
 * force right. Rumble mirrors the parent's tmff2_convert_rumble():
 * a sine with period 50 ms, magnitude strong/3 + weak/6, direction
 * forced to 16384. */
static double expected_level(const struct ff_effect *e, const char *type,
			     long t_ms)
{
	double sample, proj;
	long length = e->replay.length;
	long ti = length ? t_ms % length : t_ms;

	if (!strcmp(type, "constant")) {
		sample = e->u.constant.level;
	} else if (!strcmp(type, "ramp")) {
		sample = e->u.ramp.start_level +
			 (double)(e->u.ramp.end_level -
				  e->u.ramp.start_level) *
				 ti / (length ? length : 1);
		sample = apply_envelope(sample, &e->u.ramp.envelope, ti,
					length);
	} else if (!strcmp(type, "rumble")) {
		double mag = e->u.rumble.strong_magnitude / 3 +
			     e->u.rumble.weak_magnitude / 6;
		double pos = fmod(t_ms * 256.0 / 50.0, 256.0);

		return dir_project(mag * sin(pos * 2.0 * M_PI / 256.0),
				   16384) * 127.0 / 32767.0;
	} else {
		double pos = fmod(ti * 256.0 / e->u.periodic.period, 256.0);
		double mag = e->u.periodic.magnitude;

		if (!strcmp(type, "sine"))
			sample = mag * sin(pos * 2.0 * M_PI / 256.0);
		else if (!strcmp(type, "square"))
			sample = pos < 128 ? mag : -mag;
		else if (!strcmp(type, "triangle"))
			sample = pos < 128 ?
				 -mag + 2 * mag * pos / 128 :
				 3 * mag - 2 * mag * pos / 128;
		else if (!strcmp(type, "sawup"))
			sample = -mag + 2 * mag * pos / 255;
		else
			sample = mag - 2 * mag * pos / 255;

		sample += e->u.periodic.offset;
		sample = apply_envelope(sample, &e->u.periodic.envelope, ti,
					length);
	}

	proj = dir_project(sample, e->direction);
	if (proj > 32767)
		proj = 32767;
	if (proj < -32767)
		proj = -32767;
	return proj * 127.0 / 32767.0;
}

#define BAR_HALF 14

static void render_bar(const struct ff_effect *e, const char *type,
		       long t_ms)
{
	char bar[BAR_HALF * 2 + 4];
	double lvl = expected_level(e, type, t_ms);
	int idx = (int)lround(lvl / 127.0 * BAR_HALF);
	char side = lvl < -0.5 ? 'L' : lvl > 0.5 ? 'R' : '-';
	double hz = e->type == FF_PERIODIC ?
			    1000.0 / e->u.periodic.period :
		    e->type == FF_RUMBLE ? 20.0 : 0.0;

	memset(bar, '-', sizeof(bar) - 1);
	bar[0] = '[';
	bar[BAR_HALF + 1] = '|';
	bar[BAR_HALF * 2 + 2] = ']';
	bar[BAR_HALF * 2 + 3] = '\0';
	bar[BAR_HALF + 1 + idx] = 'O';

	printf("\r%-9s %6.2fHz %7.2fs %s  %c %5.1f%% ", type, hz,
	       t_ms / 1000.0, bar, side, fabs(lvl) / 127.0 * 100.0);
	fflush(stdout);
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s <event-device> <sine|square|triangle|sawup|sawdown|\n"
		"                        constant|ramp|rumble> [key value ...]\n"
		"\n"
		"  --period N       waveform cycle in ms, periodic only (default 1000)\n"
		"  --magnitude N    level 0-32767; also constant level (default 20000)\n"
		"  --direction N    0-65535; 0 = zero force here, default 16384\n"
		"  --duration N     play time in ms, 0 = until Ctrl+C (default 3000)\n"
		"  --count N        play repetitions (default 1)\n"
		"  --delay N        start delay ms (default 0)\n"
		"  --attack N       envelope attack ms (default 0)\n"
		"  --attack-level N 0-32767 (default 0)\n"
		"  --fade N         envelope fade ms (default 0)\n"
		"  --fade-level N   0-32767 (default 0)\n"
		"  --start N        ramp start level (default -10000)\n"
		"  --end N          ramp end level (default 10000)\n"
		"  --strong N       rumble strong magnitude (default 20000)\n"
		"  --weak N         rumble weak magnitude (default 10000)\n",
		prog);
}

static long arg_int(int argc, char **argv, const char *name, long fallback)
{
	int i;

	for (i = 3; i + 1 < argc; i += 2)
		if (!strcmp(argv[i], name))
			return strtol(argv[i + 1], NULL, 0);
	return fallback;
}

static void set_envelope(struct ff_envelope *env, long attack, long alevel,
			 long fade, long flevel)
{
	env->attack_length = (__u16)attack;
	env->attack_level = (__u16)alevel;
	env->fade_length = (__u16)fade;
	env->fade_level = (__u16)flevel;
}

int main(int argc, char **argv)
{
	struct ff_effect e;
	struct input_event play;
	long period, magnitude, direction, duration, count, delay;
	long attack, alevel, fade, flevel, strong, weak, start, end;
	const char *dev, *type;

	if (argc < 3 || (argc - 3) % 2 != 0) {
		usage(argv[0]);
		return 1;
	}
	dev = argv[1];
	type = argv[2];

	period = arg_int(argc, argv, "--period", 1000);
	magnitude = arg_int(argc, argv, "--magnitude", 20000);
	direction = arg_int(argc, argv, "--direction", 16384);
	duration = arg_int(argc, argv, "--duration", 3000);
	count = arg_int(argc, argv, "--count", 1);
	delay = arg_int(argc, argv, "--delay", 0);
	attack = arg_int(argc, argv, "--attack", 0);
	alevel = arg_int(argc, argv, "--attack-level", 0);
	fade = arg_int(argc, argv, "--fade", 0);
	flevel = arg_int(argc, argv, "--fade-level", 0);
	strong = arg_int(argc, argv, "--strong", 20000);
	weak = arg_int(argc, argv, "--weak", 10000);
	start = arg_int(argc, argv, "--start", -10000);
	end = arg_int(argc, argv, "--end", 10000);

	if (period < 1)
		period = 1;

	memset(&e, 0, sizeof(e));
	e.id = -1;
	e.direction = (__u16)direction;
	e.replay.length = (__u16)duration;
	e.replay.delay = (__u16)delay;

	if (!strcmp(type, "sine") || !strcmp(type, "square") ||
	    !strcmp(type, "triangle") || !strcmp(type, "sawup") ||
	    !strcmp(type, "sawdown")) {
		e.type = FF_PERIODIC;
		if (!strcmp(type, "sine"))
			e.u.periodic.waveform = FF_SINE;
		else if (!strcmp(type, "square"))
			e.u.periodic.waveform = FF_SQUARE;
		else if (!strcmp(type, "triangle"))
			e.u.periodic.waveform = FF_TRIANGLE;
		else if (!strcmp(type, "sawup"))
			e.u.periodic.waveform = FF_SAW_UP;
		else
			e.u.periodic.waveform = FF_SAW_DOWN;
		e.u.periodic.period = (__u16)period;
		e.u.periodic.magnitude = (__u16)magnitude;
		set_envelope(&e.u.periodic.envelope, attack, alevel, fade,
			     flevel);
	} else if (!strcmp(type, "constant")) {
		e.type = FF_CONSTANT;
		e.u.constant.level = (__s16)magnitude;
		set_envelope(&e.u.constant.envelope, attack, alevel, fade,
			     flevel);
	} else if (!strcmp(type, "ramp")) {
		e.type = FF_RAMP;
		if (!e.replay.length)
			e.replay.length = 1000;
		e.u.ramp.start_level = (__s16)start;
		e.u.ramp.end_level = (__s16)end;
		set_envelope(&e.u.ramp.envelope, attack, alevel, fade, flevel);
	} else if (!strcmp(type, "rumble")) {
		e.type = FF_RUMBLE;
		e.u.rumble.strong_magnitude = (__u16)strong;
		e.u.rumble.weak_magnitude = (__u16)weak;
	} else {
		usage(argv[0]);
		return 1;
	}

	fd = open(dev, O_RDWR);
	if (fd < 0) {
		perror(dev);
		return 1;
	}

	if (ioctl(fd, EVIOCSFF, &e) < 0) {
		perror("EVIOCSFF (effect upload)");
		close(fd);
		return 1;
	}
	effect_id = e.id;

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	if (e.type == FF_PERIODIC)
		printf("uploaded %s: id=%d period=%ldms (%.2f Hz) magnitude=%ld direction=%ld\n",
		       type, e.id, period, 1000.0 / period, magnitude,
		       direction);
	else
		printf("uploaded %s: id=%d duration=%ums\n", type, e.id,
		       e.replay.length);

	memset(&play, 0, sizeof(play));
	play.type = EV_FF;
	play.code = e.id;
	play.value = count;
	if (write(fd, &play, sizeof(play)) < 0) {
		perror("play (EV_FF write)");
		stop_and_erase();
		close(fd);
		return 1;
	}

	if (e.replay.length) {
		long total = (long)e.replay.length * count;
		long t0 = now_ms();

		printf("playing %ldms of effective time... (Ctrl+C stops)\n",
		       total);
		for (;;) {
			long t = now_ms() - t0;

			if (t >= total)
				break;
			render_bar(&e, type, t);
			msleep(16);
		}
		printf("\n");
	} else {
		long t0 = now_ms();

		puts("playing (infinite)... Ctrl+C to stop");
		for (;;) {
			render_bar(&e, type, now_ms() - t0);
			msleep(16);
		}
	}

	stop_and_erase();
	close(fd);
	return 0;
}
