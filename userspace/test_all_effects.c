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

int upload_and_play_sine(int fd, int magnitude, int period)
{
    struct ff_effect effect;
    struct input_event play;

    memset(&effect, 0, sizeof(effect));
    effect.type = FF_PERIODIC;
    effect.id = -1;
    effect.u.periodic.waveform = FF_SINE;
    effect.u.periodic.period = period;  /* milliseconds */
    effect.u.periodic.magnitude = magnitude;
    effect.u.periodic.offset = 0;
    effect.u.periodic.phase = 0;
    effect.u.periodic.envelope.attack_length = 0;
    effect.u.periodic.envelope.attack_level = 0;
    effect.u.periodic.envelope.fade_length = 0;
    effect.u.periodic.envelope.fade_level = 0;
    effect.trigger.button = 0;
    effect.trigger.interval = 0;
    effect.replay.length = 3000;  /* 3 seconds */
    effect.replay.delay = 0;
    effect.direction = 0x4000;

    if (ioctl(fd, EVIOCSFF, &effect) < 0) {
        perror("Upload sine effect failed");
        return -1;
    }

    printf("Uploaded sine effect (ID=%d, magnitude=%d, period=%dms)\n",
           effect.id, magnitude, period);

    /* Play effect */
    memset(&play, 0, sizeof(play));
    play.type = EV_FF;
    play.code = effect.id;
    play.value = 1;

    if (write(fd, &play, sizeof(play)) != sizeof(play)) {
        perror("Play effect failed");
        return -1;
    }

    printf("Playing for 3 seconds...\n");
    printf(">>> DO YOU FEEL VIBRATION/OSCILLATION? <<<\n");
    sleep(3);

    /* Stop effect */
    play.value = 0;
    write(fd, &play, sizeof(play));

    /* Remove effect */
    ioctl(fd, EVIOCRMFF, effect.id);

    return 0;
}

int upload_and_play_square(int fd, int magnitude, int period)
{
    struct ff_effect effect;
    struct input_event play;

    memset(&effect, 0, sizeof(effect));
    effect.type = FF_PERIODIC;
    effect.id = -1;
    effect.u.periodic.waveform = FF_SQUARE;
    effect.u.periodic.period = period;
    effect.u.periodic.magnitude = magnitude;
    effect.u.periodic.offset = 0;
    effect.u.periodic.phase = 0;
    effect.u.periodic.envelope.attack_length = 0;
    effect.u.periodic.envelope.attack_level = 0;
    effect.u.periodic.envelope.fade_length = 0;
    effect.u.periodic.envelope.fade_level = 0;
    effect.trigger.button = 0;
    effect.trigger.interval = 0;
    effect.replay.length = 3000;
    effect.replay.delay = 0;
    effect.direction = 0x4000;

    if (ioctl(fd, EVIOCSFF, &effect) < 0) {
        perror("Upload square effect failed");
        return -1;
    }

    printf("Uploaded square wave effect (ID=%d, magnitude=%d, period=%dms)\n",
           effect.id, magnitude, period);

    /* Play effect */
    memset(&play, 0, sizeof(play));
    play.type = EV_FF;
    play.code = effect.id;
    play.value = 1;

    if (write(fd, &play, sizeof(play)) != sizeof(play)) {
        perror("Play effect failed");
        return -1;
    }

    printf("Playing for 3 seconds...\n");
    printf(">>> SQUARE WAVE - SHARP ON/OFF VIBRATION <<<\n");
    sleep(3);

    /* Stop effect */
    play.value = 0;
    write(fd, &play, sizeof(play));

    /* Remove effect */
    ioctl(fd, EVIOCRMFF, effect.id);

    return 0;
}

int upload_and_play_ramp(int fd, int start_level, int end_level)
{
    struct ff_effect effect;
    struct input_event play;

    memset(&effect, 0, sizeof(effect));
    effect.type = FF_RAMP;
    effect.id = -1;
    effect.u.ramp.start_level = start_level;
    effect.u.ramp.end_level = end_level;
    effect.u.ramp.envelope.attack_length = 0;
    effect.u.ramp.envelope.attack_level = 0;
    effect.u.ramp.envelope.fade_length = 0;
    effect.u.ramp.envelope.fade_level = 0;
    effect.trigger.button = 0;
    effect.trigger.interval = 0;
    effect.replay.length = 8000;  /* 8 seconds for gradual ramp */
    effect.replay.delay = 0;
    effect.direction = 0x4000;

    if (ioctl(fd, EVIOCSFF, &effect) < 0) {
        perror("Upload ramp effect failed");
        return -1;
    }

    printf("Uploaded ramp effect (ID=%d, start=%d, end=%d)\n",
           effect.id, start_level, end_level);

    /* Play effect */
    memset(&play, 0, sizeof(play));
    play.type = EV_FF;
    play.code = effect.id;
    play.value = 1;

    if (write(fd, &play, sizeof(play)) != sizeof(play)) {
        perror("Play effect failed");
        return -1;
    }

    printf("Playing for 8 seconds...\n");
    printf(">>> FORCE SHOULD GRADUALLY CHANGE - HOLD THE WHEEL STEADY <<<\n");
    sleep(8);

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

void show_menu(void)
{
    printf("\n========================================\n");
    printf("T500RS Force Feedback Test Menu\n");
    printf("========================================\n\n");
    printf("Constant Force Tests:\n");
    printf("  1. Weak Constant Force\n");
    printf("  2. Medium Constant Force\n");
    printf("  3. Strong Constant Force\n");
    printf("  4. Maximum Constant Force\n");
    printf("  5. Negative Constant Force\n\n");
    printf("Spring Tests:\n");
    printf("  6. Weak Spring\n");
    printf("  7. Medium Spring\n");
    printf("  8. Strong Spring\n\n");
    printf("Damper Tests:\n");
    printf("  9. Weak Damper\n");
    printf(" 10. Strong Damper\n\n");
    printf("Periodic Tests:\n");
    printf(" 11. Sine Wave - Slow (500ms, gentle)\n");
    printf(" 12. Sine Wave - Medium (200ms, gentle)\n");
    printf(" 13. Sine Wave - Fast (100ms, gentle)\n");
    printf(" 14. Square Wave (200ms, gentle)\n\n");
    printf("Ramp Tests:\n");
    printf(" 15. Ramp - Weak to Strong (8s)\n");
    printf(" 16. Ramp - Strong to Weak (8s)\n\n");
    printf("Special:\n");
    printf("  0. Run ALL tests\n");
    printf(" -1. Exit\n\n");
    printf("Enter test number: ");
}

void run_test(int fd, int test_num)
{
    switch (test_num) {
    case 1:
        print_test_header("Test 1: Weak Constant Force (4096)");
        upload_and_play_constant(fd, 4096);
        break;
    case 2:
        print_test_header("Test 2: Medium Constant Force (8192)");
        upload_and_play_constant(fd, 8192);
        break;
    case 3:
        print_test_header("Test 3: Strong Constant Force (16384)");
        upload_and_play_constant(fd, 16384);
        break;
    case 4:
        print_test_header("Test 4: MAXIMUM Constant Force (32767)");
        upload_and_play_constant(fd, 32767);
        break;
    case 5:
        print_test_header("Test 5: Negative Constant Force (-16384)");
        upload_and_play_constant(fd, -16384);
        break;
    case 6:
        print_test_header("Test 6: Weak Spring (4096)");
        upload_and_play_spring(fd, 4096);
        break;
    case 7:
        print_test_header("Test 7: Medium Spring (8192)");
        upload_and_play_spring(fd, 8192);
        break;
    case 8:
        print_test_header("Test 8: Strong Spring (16384)");
        upload_and_play_spring(fd, 16384);
        break;
    case 9:
        print_test_header("Test 9: Weak Damper (4096)");
        upload_and_play_damper(fd, 4096);
        break;
    case 10:
        print_test_header("Test 10: Strong Damper (16384)");
        upload_and_play_damper(fd, 16384);
        break;
    case 11:
        print_test_header("Test 11: Sine Wave - Slow (500ms period, weak)");
        upload_and_play_sine(fd, 4096, 500);  /* Reduced magnitude, slower */
        break;
    case 12:
        print_test_header("Test 12: Sine Wave - Medium (200ms period, weak)");
        upload_and_play_sine(fd, 4096, 200);  /* Reduced magnitude */
        break;
    case 13:
        print_test_header("Test 13: Sine Wave - Fast (100ms period, weak)");
        upload_and_play_sine(fd, 4096, 100);  /* Reduced magnitude */
        break;
    case 14:
        print_test_header("Test 14: Square Wave (200ms period, weak)");
        upload_and_play_square(fd, 4096, 200);  /* Reduced magnitude, slower */
        break;
    case 15:
        print_test_header("Test 15: Ramp - Weak to Strong (8 seconds)");
        upload_and_play_ramp(fd, 2048, 16384);  /* Weak to strong ramp */
        break;
    case 16:
        print_test_header("Test 16: Ramp - Strong to Weak (8 seconds)");
        upload_and_play_ramp(fd, 16384, 2048);  /* Strong to weak ramp */
        break;
    default:
        printf("Invalid test number!\n");
        return;
    }
}

int main(int argc, char **argv)
{
    int fd;
    char device[256] = "";
    char *auto_device;
    int choice;
    int interactive = 1;

    printf("========================================\n");
    printf("T500RS Force Feedback Comprehensive Test\n");
    printf("========================================\n\n");

    /* Check for --all flag */
    if (argc > 1 && strcmp(argv[1], "--all") == 0) {
        interactive = 0;
    }

    if (argc > 1 && strcmp(argv[1], "--all") != 0) {
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
            printf("       %s --all  (run all tests)\n", argv[0]);
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

    if (interactive) {
        /* Interactive menu mode */
        while (1) {
            show_menu();
            if (scanf("%d", &choice) != 1) {
                /* Clear input buffer */
                while (getchar() != '\n');
                printf("Invalid input!\n");
                continue;
            }

            if (choice == -1) {
                printf("\nExiting...\n");
                break;
            }

            if (choice == 0) {
                /* Run all tests */
                printf("\n========================================\n");
                printf("Running ALL tests...\n");
                printf("========================================\n\n");

                for (int i = 1; i <= 16; i++) {
                    run_test(fd, i);
                    sleep(1);
                }

                printf("\n========================================\n");
                printf("All Tests Complete!\n");
                printf("========================================\n");
            } else if (choice >= 1 && choice <= 16) {
                run_test(fd, choice);
            } else {
                printf("Invalid test number! Choose 0-16 or -1 to exit.\n");
            }
        }
    } else {
        /* Run all tests automatically */
        printf("\n========================================\n");
        printf("Running ALL tests automatically...\n");
        printf("========================================\n\n");

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
    sleep(1);

    /* Test 11: Sine wave - slow */
    print_test_header("Test 11: Sine Wave - Slow (100ms period)");
    upload_and_play_sine(fd, 16384, 100);
    sleep(1);

    /* Test 12: Sine wave - medium */
    print_test_header("Test 12: Sine Wave - Medium (50ms period)");
    upload_and_play_sine(fd, 16384, 50);
    sleep(1);

    /* Test 13: Sine wave - fast */
    print_test_header("Test 13: Sine Wave - Fast (20ms period)");
    upload_and_play_sine(fd, 16384, 20);
    sleep(1);

    /* Test 14: Square wave */
    print_test_header("Test 14: Square Wave (50ms period)");
    upload_and_play_square(fd, 16384, 50);
    sleep(1);

    /* Test 15: Ramp - increasing */
    print_test_header("Test 15: Ramp - Weak to Strong");
    upload_and_play_ramp(fd, 4096, 24576);
    sleep(1);

        /* Test 16: Ramp - decreasing */
        print_test_header("Test 16: Ramp - Strong to Weak");
        upload_and_play_ramp(fd, 24576, 4096);

        printf("\n========================================\n");
        printf("Test Complete!\n");
        printf("========================================\n\n");

        printf("Summary:\n");
        printf("- Constant force: Should feel resistance in one direction\n");
        printf("- Spring: Should feel centering force when you turn\n");
        printf("- Damper: Should feel resistance when turning quickly\n");
        printf("- Sine wave: Should feel smooth vibration/oscillation\n");
        printf("- Square wave: Should feel sharp on/off vibration\n");
        printf("- Ramp: Should feel gradually changing force\n\n");
    }

    close(fd);
    return 0;
}

