/*
 * Test program for T500RS gain and autocenter control
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
int find_t500rs_device(char *device, size_t size)
{
    DIR *dir;
    struct dirent *entry;
    char path[256];
    char name[256];
    int fd;

    dir = opendir("/dev/input");
    if (!dir) {
        perror("opendir /dev/input");
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) != 0)
            continue;

        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
        fd = open(path, O_RDWR);
        if (fd < 0)
            continue;

        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
            close(fd);
            continue;
        }

        if (strstr(name, "T500RS") != NULL) {
            snprintf(device, size, "%s", path);
            close(fd);
            closedir(dir);
            return 0;
        }

        close(fd);
    }

    closedir(dir);
    return -1;
}

/* Set gain */
int set_gain(int fd, int gain_percent)
{
    struct input_event ev;
    
    /* Convert percentage to 0-65535 range */
    int gain_value = (gain_percent * 65535) / 100;
    
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_FF;
    ev.code = FF_GAIN;
    ev.value = gain_value;
    
    if (write(fd, &ev, sizeof(ev)) != sizeof(ev)) {
        perror("Failed to set gain");
        return -1;
    }
    
    printf("✅ Gain set to %d%% (value: %d)\n", gain_percent, gain_value);
    return 0;
}

/* Set autocenter */
int set_autocenter(int fd, int autocenter_percent)
{
    struct input_event ev;
    
    /* Convert percentage to 0-65535 range */
    int autocenter_value = (autocenter_percent * 65535) / 100;
    
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_FF;
    ev.code = FF_AUTOCENTER;
    ev.value = autocenter_value;
    
    if (write(fd, &ev, sizeof(ev)) != sizeof(ev)) {
        perror("Failed to set autocenter");
        return -1;
    }
    
    printf("✅ Autocenter set to %d%% (value: %d)\n", autocenter_percent, autocenter_value);
    return 0;
}

/* Upload and play a constant force for testing */
int test_constant_force(int fd, int level)
{
    struct ff_effect effect;
    struct input_event play;
    
    memset(&effect, 0, sizeof(effect));
    effect.type = FF_CONSTANT;
    effect.id = -1;
    effect.u.constant.level = level;
    effect.direction = 0x4000;  /* 90 degrees */
    effect.replay.length = 2000;  /* 2 seconds */
    effect.replay.delay = 0;
    
    if (ioctl(fd, EVIOCSFF, &effect) < 0) {
        perror("Upload effect failed");
        return -1;
    }
    
    printf("Playing constant force (level=%d) for 2 seconds...\n", level);
    
    /* Play effect */
    memset(&play, 0, sizeof(play));
    play.type = EV_FF;
    play.code = effect.id;
    play.value = 1;
    
    if (write(fd, &play, sizeof(play)) != sizeof(play)) {
        perror("Play effect failed");
        return -1;
    }
    
    sleep(2);
    
    /* Stop effect */
    play.value = 0;
    write(fd, &play, sizeof(play));
    
    /* Remove effect */
    ioctl(fd, EVIOCRMFF, effect.id);
    
    return 0;
}

int main(void)
{
    char device[256];
    int fd;
    int choice;
    
    printf("========================================\n");
    printf("T500RS Gain & Autocenter Test\n");
    printf("========================================\n\n");
    
    /* Find device */
    printf("Looking for T500RS device...\n");
    if (find_t500rs_device(device, sizeof(device)) < 0) {
        fprintf(stderr, "❌ T500RS device not found!\n");
        fprintf(stderr, "Make sure the driver is running: sudo ./run.sh\n");
        return 1;
    }
    
    printf("✅ Found device: %s\n\n", device);
    
    /* Open device */
    fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }
    
    while (1) {
        printf("\n========================================\n");
        printf("Test Menu:\n");
        printf("========================================\n");
        printf("Gain Control:\n");
        printf("  1. Set gain to 25%%\n");
        printf("  2. Set gain to 50%%\n");
        printf("  3. Set gain to 75%%\n");
        printf("  4. Set gain to 100%%\n\n");
        printf("Autocenter Control:\n");
        printf("  5. Autocenter OFF (0%%)\n");
        printf("  6. Autocenter weak (25%%)\n");
        printf("  7. Autocenter medium (50%%)\n");
        printf("  8. Autocenter strong (75%%)\n");
        printf("  9. Autocenter maximum (100%%)\n\n");
        printf("Test Force:\n");
        printf(" 10. Test constant force (with current gain)\n\n");
        printf(" -1. Exit\n");
        printf("========================================\n");
        printf("Enter choice: ");
        
        if (scanf("%d", &choice) != 1) {
            while (getchar() != '\n');
            printf("Invalid input!\n");
            continue;
        }
        
        if (choice == -1) {
            printf("\nExiting...\n");
            break;
        }
        
        printf("\n");
        
        switch (choice) {
        case 1:
            set_gain(fd, 25);
            break;
        case 2:
            set_gain(fd, 50);
            break;
        case 3:
            set_gain(fd, 75);
            break;
        case 4:
            set_gain(fd, 100);
            break;
        case 5:
            set_autocenter(fd, 0);
            printf(">>> Release the wheel - it should NOT self-center <<<\n");
            break;
        case 6:
            set_autocenter(fd, 25);
            printf(">>> Release the wheel - it should gently self-center <<<\n");
            break;
        case 7:
            set_autocenter(fd, 50);
            printf(">>> Release the wheel - it should moderately self-center <<<\n");
            break;
        case 8:
            set_autocenter(fd, 75);
            printf(">>> Release the wheel - it should strongly self-center <<<\n");
            break;
        case 9:
            set_autocenter(fd, 100);
            printf(">>> Release the wheel - it should VERY strongly self-center <<<\n");
            break;
        case 10:
            printf("Testing constant force with current gain setting...\n");
            test_constant_force(fd, 16384);  /* Medium force */
            break;
        default:
            printf("Invalid choice!\n");
        }
    }
    
    /* Reset to defaults before exiting */
    printf("\nResetting to defaults...\n");
    set_gain(fd, 100);
    set_autocenter(fd, 0);
    
    close(fd);
    return 0;
}

