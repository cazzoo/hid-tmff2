// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ffctl - upload and play one Linux force-feedback effect with explicit
 * command-line parameters. fftest's interactive scanf prompts are unusable
 * on some setups, and failed input leaves its variables uninitialized
 * (i.e. unknown period/magnitude). Every parameter here is explicit.
 *
 * Build:  gcc -O2 -Wall -o ffctl tools/ffctl.c
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
		printf("playing %ums of effective time... (Ctrl+C stops)\n",
		       e.replay.length * (int)count);
		msleep((long)e.replay.length * count);
	} else {
		puts("playing (infinite)... Ctrl+C to stop");
		for (;;)
			pause();
	}

	stop_and_erase();
	close(fd);
	return 0;
}
