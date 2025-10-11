/* Test constant force direction encoding
 * Tests if 0x5e and 0x3f direction flags are correct
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
    printf("T500RS Direction Test\n");
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

    /* Test 1: Positive force (should pull LEFT or RIGHT) */
    printf("Test 1: Positive force (+16000)\n");
    printf("This should pull the wheel in ONE direction\n");
    printf("Note which direction it pulls...\n\n");

    memset(&effect, 0, sizeof(effect));
    effect.type = FF_CONSTANT;
    effect.id = -1;
    effect.u.constant.level = 16000;  /* Positive force */
    effect.direction = 0;
    effect.replay.length = 2000;  /* 2 seconds */
    effect.replay.delay = 0;

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

    printf("Playing positive force for 2 seconds...\n");
    sleep(2);

    /* Stop effect */
    play.value = 0;
    write(fd, &play, sizeof(play));
    sleep(1);

    /* Test 2: Negative force (should pull in OPPOSITE direction) */
    printf("\nTest 2: Negative force (-16000)\n");
    printf("This should pull the wheel in the OPPOSITE direction\n");
    printf("Note which direction it pulls...\n\n");

    /* Remove old effect */
    ioctl(fd, EVIOCRMFF, effect_id);

    /* Upload negative force */
    memset(&effect, 0, sizeof(effect));
    effect.type = FF_CONSTANT;
    effect.id = -1;
    effect.u.constant.level = -16000;  /* Negative force */
    effect.direction = 0;
    effect.replay.length = 2000;  /* 2 seconds */
    effect.replay.delay = 0;

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

    printf("Playing negative force for 2 seconds...\n");
    sleep(2);

    /* Stop effect */
    play.value = 0;
    write(fd, &play, sizeof(play));

    /* Cleanup */
    ioctl(fd, EVIOCRMFF, effect_id);
    close(fd);

    printf("\n========================================\n");
    printf("Test Complete\n");
    printf("========================================\n\n");
    printf("Results:\n");
    printf("  Positive force (+16000): Which direction did it pull?\n");
    printf("  Negative force (-16000): Which direction did it pull?\n\n");
    printf("Expected: They should pull in OPPOSITE directions\n\n");
    printf("If the directions are REVERSED from what you expect:\n");
    printf("  Edit t500rs-ffb.c, function send_force_update()\n");
    printf("  Swap 0x5e and 0x3f in the direction assignment\n\n");

    return 0;
}

