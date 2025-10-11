/* Test multi-effect mixing
 * Tests if multiple simultaneous effects are combined correctly
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
    struct ff_effect effect1, effect2;
    struct input_event play;
    int effect_id1 = -1, effect_id2 = -1;
    char device_path[256];

    printf("========================================\n");
    printf("T500RS Multi-Effect Mixing Test\n");
    printf("========================================\n\n");

    /* Find device */
    if (find_device(device_path, sizeof(device_path)) < 0) {
        fprintf(stderr, "ERROR: T500RS device not found!\n\n");
        fprintf(stderr, "Make sure:\n");
        fprintf(stderr, "  1. The wheel is connected\n");
        fprintf(stderr, "  2. The driver is running: sudo ./run.sh\n");
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

    /* Test 1: Two forces in same direction (should add up) */
    printf("Test 1: Two Forces in Same Direction\n");
    printf("Playing LEFT force (10000) + LEFT force (10000)\n");
    printf("Should feel like STRONG LEFT force (combined ~20000)\n\n");

    /* Effect 1: LEFT force */
    memset(&effect1, 0, sizeof(effect1));
    effect1.type = FF_CONSTANT;
    effect1.id = -1;
    effect1.u.constant.level = 10000;  /* LEFT */
    effect1.direction = 0;
    effect1.replay.length = 3000;  /* 3 seconds */
    effect1.replay.delay = 0;

    if (ioctl(fd, EVIOCSFF, &effect1) < 0) {
        perror("Failed to upload effect 1");
        close(fd);
        return 1;
    }
    effect_id1 = effect1.id;

    /* Effect 2: LEFT force */
    memset(&effect2, 0, sizeof(effect2));
    effect2.type = FF_CONSTANT;
    effect2.id = -1;
    effect2.u.constant.level = 10000;  /* LEFT */
    effect2.direction = 0;
    effect2.replay.length = 3000;  /* 3 seconds */
    effect2.replay.delay = 0;

    if (ioctl(fd, EVIOCSFF, &effect2) < 0) {
        perror("Failed to upload effect 2");
        close(fd);
        return 1;
    }
    effect_id2 = effect2.id;

    /* Play both effects */
    memset(&play, 0, sizeof(play));
    play.type = EV_FF;
    play.code = effect_id1;
    play.value = 1;
    write(fd, &play, sizeof(play));

    play.code = effect_id2;
    write(fd, &play, sizeof(play));

    printf("Playing both effects for 3 seconds...\n\n");
    sleep(3);

    /* Stop both effects */
    play.code = effect_id1;
    play.value = 0;
    write(fd, &play, sizeof(play));
    play.code = effect_id2;
    write(fd, &play, sizeof(play));

    ioctl(fd, EVIOCRMFF, effect_id1);
    ioctl(fd, EVIOCRMFF, effect_id2);
    sleep(1);

    /* Test 2: Two forces in opposite directions (should cancel) */
    printf("\nTest 2: Two Forces in Opposite Directions\n");
    printf("Playing LEFT force (15000) + RIGHT force (-15000)\n");
    printf("Should feel NEUTRAL (forces cancel out)\n\n");

    /* Effect 1: LEFT force */
    memset(&effect1, 0, sizeof(effect1));
    effect1.type = FF_CONSTANT;
    effect1.id = -1;
    effect1.u.constant.level = 15000;  /* LEFT */
    effect1.direction = 0;
    effect1.replay.length = 3000;
    effect1.replay.delay = 0;

    if (ioctl(fd, EVIOCSFF, &effect1) < 0) {
        perror("Failed to upload effect 1");
        close(fd);
        return 1;
    }
    effect_id1 = effect1.id;

    /* Effect 2: RIGHT force */
    memset(&effect2, 0, sizeof(effect2));
    effect2.type = FF_CONSTANT;
    effect2.id = -1;
    effect2.u.constant.level = -15000;  /* RIGHT */
    effect2.direction = 0;
    effect2.replay.length = 3000;
    effect2.replay.delay = 0;

    if (ioctl(fd, EVIOCSFF, &effect2) < 0) {
        perror("Failed to upload effect 2");
        close(fd);
        return 1;
    }
    effect_id2 = effect2.id;

    /* Play both effects */
    play.code = effect_id1;
    play.value = 1;
    write(fd, &play, sizeof(play));

    play.code = effect_id2;
    write(fd, &play, sizeof(play));

    printf("Playing both effects for 3 seconds...\n\n");
    sleep(3);

    /* Stop both effects */
    play.code = effect_id1;
    play.value = 0;
    write(fd, &play, sizeof(play));
    play.code = effect_id2;
    write(fd, &play, sizeof(play));

    ioctl(fd, EVIOCRMFF, effect_id1);
    ioctl(fd, EVIOCRMFF, effect_id2);
    sleep(1);

    /* Test 3: Unbalanced forces */
    printf("\nTest 3: Unbalanced Forces\n");
    printf("Playing LEFT force (20000) + RIGHT force (-5000)\n");
    printf("Should feel MODERATE LEFT force (combined ~15000)\n\n");

    /* Effect 1: Strong LEFT */
    memset(&effect1, 0, sizeof(effect1));
    effect1.type = FF_CONSTANT;
    effect1.id = -1;
    effect1.u.constant.level = 20000;  /* Strong LEFT */
    effect1.direction = 0;
    effect1.replay.length = 3000;
    effect1.replay.delay = 0;

    if (ioctl(fd, EVIOCSFF, &effect1) < 0) {
        perror("Failed to upload effect 1");
        close(fd);
        return 1;
    }
    effect_id1 = effect1.id;

    /* Effect 2: Weak RIGHT */
    memset(&effect2, 0, sizeof(effect2));
    effect2.type = FF_CONSTANT;
    effect2.id = -1;
    effect2.u.constant.level = -5000;  /* Weak RIGHT */
    effect2.direction = 0;
    effect2.replay.length = 3000;
    effect2.replay.delay = 0;

    if (ioctl(fd, EVIOCSFF, &effect2) < 0) {
        perror("Failed to upload effect 2");
        close(fd);
        return 1;
    }
    effect_id2 = effect2.id;

    /* Play both effects */
    play.code = effect_id1;
    play.value = 1;
    write(fd, &play, sizeof(play));

    play.code = effect_id2;
    write(fd, &play, sizeof(play));

    printf("Playing both effects for 3 seconds...\n\n");
    sleep(3);

    /* Stop both effects */
    play.code = effect_id1;
    play.value = 0;
    write(fd, &play, sizeof(play));
    play.code = effect_id2;
    write(fd, &play, sizeof(play));

    /* Cleanup */
    ioctl(fd, EVIOCRMFF, effect_id1);
    ioctl(fd, EVIOCRMFF, effect_id2);
    close(fd);

    printf("\n========================================\n");
    printf("Test Complete\n");
    printf("========================================\n\n");
    printf("Results:\n");
    printf("  Test 1: Did forces add up? (stronger than single)\n");
    printf("  Test 2: Did forces cancel? (neutral/weak)\n");
    printf("  Test 3: Did forces combine correctly? (moderate left)\n\n");
    printf("If mixing works correctly, you should have felt:\n");
    printf("  - Test 1: Strong combined force\n");
    printf("  - Test 2: Little to no force (cancellation)\n");
    printf("  - Test 3: Moderate force in dominant direction\n\n");

    return 0;
}

