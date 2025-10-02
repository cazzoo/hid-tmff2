/*
 * Comprehensive Force Feedback Test for T500RS
 * Tests all effect types and force levels
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <linux/input.h>
#include <sys/ioctl.h>

void print_test_header(const char *test_name)
{
    printf("\n========================================\n");
    printf("%s\n", test_name);
    printf("========================================\n");
}

int upload_and_play_constant(int fd, int force_level)
{
    struct ff_effect effect;
    struct input_event play;
    
    memset(&effect, 0, sizeof(effect));
    effect.type = FF_CONSTANT;
    effect.id = -1;
    effect.u.constant.level = force_level;
    effect.direction = 0x4000;  /* 90 degrees */
    effect.u.constant.envelope.attack_length = 0;
    effect.u.constant.envelope.attack_level = 0;
    effect.u.constant.envelope.fade_length = 0;
    effect.u.constant.envelope.fade_level = 0;
    effect.trigger.button = 0;
    effect.trigger.interval = 0;
    effect.replay.length = 2000;  /* 2 seconds */
    effect.replay.delay = 0;
    
    if (ioctl(fd, EVIOCSFF, &effect) < 0) {
        perror("Upload constant effect failed");
        return -1;
    }
    
    printf("Uploaded constant effect (ID=%d, force=%d)\n", effect.id, force_level);
    
    /* Play effect */
    memset(&play, 0, sizeof(play));
    play.type = EV_FF;
    play.code = effect.id;
    play.value = 1;
    
    if (write(fd, &play, sizeof(play)) != sizeof(play)) {
        perror("Play effect failed");
        return -1;
    }
    
    printf("Playing for 2 seconds...\n");
    printf(">>> DO YOU FEEL THE FORCE? <<<\n");
    sleep(2);
    
    /* Stop effect */
    play.value = 0;
    write(fd, &play, sizeof(play));
    
    /* Remove effect */
    ioctl(fd, EVIOCRMFF, effect.id);
    
    return 0;
}

int upload_and_play_spring(int fd, int strength)
{
    struct ff_effect effect;
    struct input_event play;
    
    memset(&effect, 0, sizeof(effect));
    effect.type = FF_SPRING;
    effect.id = -1;
    effect.u.condition[0].right_saturation = 0x7fff;
    effect.u.condition[0].left_saturation = 0x7fff;
    effect.u.condition[0].right_coeff = strength;
    effect.u.condition[0].left_coeff = strength;
    effect.u.condition[0].deadband = 0;
    effect.u.condition[0].center = 0;
    effect.trigger.button = 0;
    effect.trigger.interval = 0;
    effect.replay.length = 2000;
    effect.replay.delay = 0;
    
    if (ioctl(fd, EVIOCSFF, &effect) < 0) {
        perror("Upload spring effect failed");
        return -1;
    }
    
    printf("Uploaded spring effect (ID=%d, strength=%d)\n", effect.id, strength);
    
    /* Play effect */
    memset(&play, 0, sizeof(play));
    play.type = EV_FF;
    play.code = effect.id;
    play.value = 1;
    
    if (write(fd, &play, sizeof(play)) != sizeof(play)) {
        perror("Play effect failed");
        return -1;
    }
    
    printf("Playing for 2 seconds...\n");
    printf(">>> TRY TURNING THE WHEEL <<<\n");
    sleep(2);
    
    /* Stop effect */
    play.value = 0;
    write(fd, &play, sizeof(play));
    
    /* Remove effect */
    ioctl(fd, EVIOCRMFF, effect.id);
    
    return 0;
}

int upload_and_play_damper(int fd, int strength)
{
    struct ff_effect effect;
    struct input_event play;
    
    memset(&effect, 0, sizeof(effect));
    effect.type = FF_DAMPER;
    effect.id = -1;
    effect.u.condition[0].right_saturation = 0x7fff;
    effect.u.condition[0].left_saturation = 0x7fff;
    effect.u.condition[0].right_coeff = strength;
    effect.u.condition[0].left_coeff = strength;
    effect.u.condition[0].deadband = 0;
    effect.u.condition[0].center = 0;
    effect.trigger.button = 0;
    effect.trigger.interval = 0;
    effect.replay.length = 2000;
    effect.replay.delay = 0;
    
    if (ioctl(fd, EVIOCSFF, &effect) < 0) {
        perror("Upload damper effect failed");
        return -1;
    }
    
    printf("Uploaded damper effect (ID=%d, strength=%d)\n", effect.id, strength);
    
    /* Play effect */
    memset(&play, 0, sizeof(play));
    play.type = EV_FF;
    play.code = effect.id;
    play.value = 1;
    
    if (write(fd, &play, sizeof(play)) != sizeof(play)) {
        perror("Play effect failed");
        return -1;
    }
    
    printf("Playing for 2 seconds...\n");
    printf(">>> TRY TURNING THE WHEEL QUICKLY <<<\n");
    sleep(2);
    
    /* Stop effect */
    play.value = 0;
    write(fd, &play, sizeof(play));
    
    /* Remove effect */
    ioctl(fd, EVIOCRMFF, effect.id);
    
    return 0;
}

/* Find T500RS FFB device automatically */
char* find_t500rs_device(void)
{
    static char device[256];
    DIR *dir;
    struct dirent *entry;
    char path[512];
    char name[256];
    FILE *fp;

    /* Scan /sys/class/input/eventX/device/name */
    dir = opendir("/sys/class/input");
    if (!dir) {
        return NULL;
    }

    while ((entry = readdir(dir)) != NULL) {
        /* Only check eventX entries */
        if (strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }

        /* Read device name */
        snprintf(path, sizeof(path), "/sys/class/input/%s/device/name", entry->d_name);
        fp = fopen(path, "r");
        if (!fp) {
            continue;
        }

        if (fgets(name, sizeof(name), fp)) {
            /* Check if it's our T500RS FFB device */
            if (strstr(name, "T500RS") && strstr(name, "FFB")) {
                snprintf(device, sizeof(device), "/dev/input/%s", entry->d_name);
                fclose(fp);
                closedir(dir);
                return device;
            }
        }
        fclose(fp);
    }

    closedir(dir);
    return NULL;
}

int main(int argc, char **argv)
{
    int fd;
    char device[256] = "";
    char *auto_device;

    printf("========================================\n");
    printf("T500RS Force Feedback Comprehensive Test\n");
    printf("========================================\n\n");

    if (argc > 1) {
        /* Device specified on command line */
        strncpy(device, argv[1], sizeof(device) - 1);
        printf("Using specified device: %s\n", device);
    } else {
        /* Try to auto-detect */
        printf("Auto-detecting T500RS FFB device...\n");
        auto_device = find_t500rs_device();
        if (auto_device) {
            strncpy(device, auto_device, sizeof(device) - 1);
            printf("✅ Found device: %s\n", device);
        } else {
            printf("❌ Could not auto-detect device\n");
            printf("\nPlease specify device manually:\n");
            printf("Usage: %s /dev/input/eventX\n", argv[0]);
            printf("\nTo find your device, run:\n");
            printf("  sudo ./find_device.sh\n");
            return 1;
        }
    }

    printf("\nOpening device: %s\n", device);
    fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("Cannot open device");
        printf("\nMake sure:\n");
        printf("1. The driver is running (sudo ./run.sh)\n");
        printf("2. You have permissions (run with sudo)\n");
        printf("3. The device path is correct\n");
        return 1;
    }

    printf("✅ Device opened successfully\n");
    
    /* Test 1: Weak constant force */
    print_test_header("Test 1: Weak Constant Force (4096)");
    upload_and_play_constant(fd, 4096);
    sleep(1);
    
    /* Test 2: Medium constant force */
    print_test_header("Test 2: Medium Constant Force (8192)");
    upload_and_play_constant(fd, 8192);
    sleep(1);
    
    /* Test 3: Strong constant force */
    print_test_header("Test 3: Strong Constant Force (16384)");
    upload_and_play_constant(fd, 16384);
    sleep(1);
    
    /* Test 4: Maximum constant force */
    print_test_header("Test 4: MAXIMUM Constant Force (32767)");
    upload_and_play_constant(fd, 32767);
    sleep(1);
    
    /* Test 5: Negative constant force */
    print_test_header("Test 5: Negative Constant Force (-16384)");
    upload_and_play_constant(fd, -16384);
    sleep(1);
    
    /* Test 6: Weak spring */
    print_test_header("Test 6: Weak Spring (4096)");
    upload_and_play_spring(fd, 4096);
    sleep(1);
    
    /* Test 7: Medium spring */
    print_test_header("Test 7: Medium Spring (8192)");
    upload_and_play_spring(fd, 8192);
    sleep(1);
    
    /* Test 8: Strong spring */
    print_test_header("Test 8: Strong Spring (16384)");
    upload_and_play_spring(fd, 16384);
    sleep(1);
    
    /* Test 9: Weak damper */
    print_test_header("Test 9: Weak Damper (4096)");
    upload_and_play_damper(fd, 4096);
    sleep(1);
    
    /* Test 10: Strong damper */
    print_test_header("Test 10: Strong Damper (16384)");
    upload_and_play_damper(fd, 16384);
    
    printf("\n========================================\n");
    printf("Test Complete!\n");
    printf("========================================\n\n");
    
    printf("Summary:\n");
    printf("- Constant force: Should feel resistance in one direction\n");
    printf("- Spring: Should feel centering force when you turn\n");
    printf("- Damper: Should feel resistance when turning quickly\n\n");
    
    printf("Which effects did you feel?\n");
    printf("1. Constant force: YES / NO\n");
    printf("2. Spring: YES / NO\n");
    printf("3. Damper: YES / NO\n\n");
    
    close(fd);
    return 0;
}

