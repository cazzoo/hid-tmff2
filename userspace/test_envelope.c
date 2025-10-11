/* Test envelope support (attack and fade)
 * Tests if envelope parameters work correctly
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <dirent.h>

/* Find T500RS device */
static int find_device(char *device_path, size_t path_size)
{
    DIR *dir;
    struct dirent *entry;
    char path[256];
    char name[256];
    int fd;

    dir = opendir("/dev/input");
    if (!dir) {
        perror("Failed to open /dev/input");
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) != 0)
            continue;

        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
        fd = open(path, O_RDONLY);
        if (fd < 0)
            continue;

        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0) {
            if (strstr(name, "T500RS") || strstr(name, "T500 RS")) {
                close(fd);
                snprintf(device_path, path_size, "%s", path);
                closedir(dir);
                printf("Found device: %s (%s)\n", path, name);
                return 0;
            }
        }
        close(fd);
    }

    closedir(dir);
    return -1;
}

int main(int argc, char **argv)
{
    int fd;
    struct ff_effect effect;
    struct input_event play;
    int effect_id = -1;
    char device_path[256];

    printf("========================================\n");
    printf("T500RS Envelope Test\n");
    printf("========================================\n\n");

    /* Find device */
    if (find_device(device_path, sizeof(device_path)) < 0) {
        fprintf(stderr, "ERROR: T500RS device not found!\n\n");
        fprintf(stderr, "Make sure:\n");
        fprintf(stderr, "  1. The wheel is connected\n");
        fprintf(stderr, "  2. The driver is running: sudo ./run.sh\n");
        fprintf(stderr, "  3. Check available devices: ./list_input_devices\n\n");
        return 1;
    }

    /* Open device */
    fd = open(device_path, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        printf("\nTry: sudo %s\n", argv[0]);
        return 1;
    }

    printf("Device opened successfully\n\n");

    /* Test 1: Attack envelope */
    printf("Test 1: Attack Envelope\n");
    printf("Force will ramp up from 0%% to 100%% over 2 seconds\n");
    printf("You should feel the force gradually increase...\n\n");

    memset(&effect, 0, sizeof(effect));
    effect.type = FF_CONSTANT;
    effect.id = -1;
    effect.u.constant.level = 20000;  /* Strong force */
    effect.direction = 0;
    effect.replay.length = 5000;  /* 5 seconds total */
    effect.replay.delay = 0;
    
    /* Attack: ramp from 0% to 100% over 2 seconds */
    effect.u.constant.envelope.attack_length = 2000;  /* 2 seconds */
    effect.u.constant.envelope.attack_level = 0;      /* Start at 0% */
    effect.u.constant.envelope.fade_length = 0;
    effect.u.constant.envelope.fade_level = 0;

    if (ioctl(fd, EVIOCSFF, &effect) < 0) {
        perror("Failed to upload effect");
        close(fd);
        return 1;
    }
    effect_id = effect.id;

    /* Play effect */
    memset(&play, 0, sizeof(play));
    play.type = EV_FF;
    play.code = effect_id;
    play.value = 1;

    if (write(fd, &play, sizeof(play)) != sizeof(play)) {
        perror("Failed to play effect");
        close(fd);
        return 1;
    }

    printf("Playing attack envelope for 5 seconds...\n");
    printf("  0-2s: Force ramping up (attack)\n");
    printf("  2-5s: Full force\n\n");
    sleep(5);

    /* Stop effect */
    play.value = 0;
    write(fd, &play, sizeof(play));
    ioctl(fd, EVIOCRMFF, effect_id);
    sleep(1);

    /* Test 2: Fade envelope */
    printf("\nTest 2: Fade Envelope\n");
    printf("Force will ramp down from 100%% to 0%% over last 2 seconds\n");
    printf("You should feel the force gradually decrease...\n\n");

    memset(&effect, 0, sizeof(effect));
    effect.type = FF_CONSTANT;
    effect.id = -1;
    effect.u.constant.level = 20000;  /* Strong force */
    effect.direction = 0;
    effect.replay.length = 5000;  /* 5 seconds total */
    effect.replay.delay = 0;
    
    /* Fade: ramp from 100% to 0% over last 2 seconds */
    effect.u.constant.envelope.attack_length = 0;
    effect.u.constant.envelope.attack_level = 0;
    effect.u.constant.envelope.fade_length = 2000;  /* 2 seconds */
    effect.u.constant.envelope.fade_level = 0;      /* End at 0% */

    if (ioctl(fd, EVIOCSFF, &effect) < 0) {
        perror("Failed to upload effect");
        close(fd);
        return 1;
    }
    effect_id = effect.id;

    /* Play effect */
    memset(&play, 0, sizeof(play));
    play.type = EV_FF;
    play.code = effect_id;
    play.value = 1;

    if (write(fd, &play, sizeof(play)) != sizeof(play)) {
        perror("Failed to play effect");
        close(fd);
        return 1;
    }

    printf("Playing fade envelope for 5 seconds...\n");
    printf("  0-3s: Full force\n");
    printf("  3-5s: Force ramping down (fade)\n\n");
    sleep(5);

    /* Stop effect */
    play.value = 0;
    write(fd, &play, sizeof(play));
    ioctl(fd, EVIOCRMFF, effect_id);
    sleep(1);

    /* Test 3: Both attack and fade */
    printf("\nTest 3: Attack + Fade Envelope\n");
    printf("Force will ramp up over 1.5s, stay full for 2s, then ramp down over 1.5s\n");
    printf("You should feel smooth ramp up and down...\n\n");

    memset(&effect, 0, sizeof(effect));
    effect.type = FF_CONSTANT;
    effect.id = -1;
    effect.u.constant.level = 20000;  /* Strong force */
    effect.direction = 0;
    effect.replay.length = 5000;  /* 5 seconds total */
    effect.replay.delay = 0;
    
    /* Attack + Fade */
    effect.u.constant.envelope.attack_length = 1500;  /* 1.5 seconds */
    effect.u.constant.envelope.attack_level = 0;      /* Start at 0% */
    effect.u.constant.envelope.fade_length = 1500;    /* 1.5 seconds */
    effect.u.constant.envelope.fade_level = 0;        /* End at 0% */

    if (ioctl(fd, EVIOCSFF, &effect) < 0) {
        perror("Failed to upload effect");
        close(fd);
        return 1;
    }
    effect_id = effect.id;

    /* Play effect */
    memset(&play, 0, sizeof(play));
    play.type = EV_FF;
    play.code = effect_id;
    play.value = 1;

    if (write(fd, &play, sizeof(play)) != sizeof(play)) {
        perror("Failed to play effect");
        close(fd);
        return 1;
    }

    printf("Playing attack+fade envelope for 5 seconds...\n");
    printf("  0.0-1.5s: Force ramping up (attack)\n");
    printf("  1.5-3.5s: Full force\n");
    printf("  3.5-5.0s: Force ramping down (fade)\n\n");
    sleep(5);

    /* Stop effect */
    play.value = 0;
    write(fd, &play, sizeof(play));
    ioctl(fd, EVIOCRMFF, effect_id);

    /* Cleanup */
    close(fd);

    printf("\n========================================\n");
    printf("Test Complete\n");
    printf("========================================\n\n");
    printf("Results:\n");
    printf("  Test 1: Did force ramp up smoothly? (attack)\n");
    printf("  Test 2: Did force ramp down smoothly? (fade)\n");
    printf("  Test 3: Did force ramp up and down smoothly?\n\n");
    printf("If envelopes work correctly, you should have felt:\n");
    printf("  - Smooth gradual increase in force (attack)\n");
    printf("  - Smooth gradual decrease in force (fade)\n");
    printf("  - No sudden jumps or steps\n\n");

    return 0;
}

