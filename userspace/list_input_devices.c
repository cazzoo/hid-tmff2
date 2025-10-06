/*
 * List all input devices and show which one is the T500RS
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <glob.h>

int main() {
    glob_t globbuf;
    
    printf("========================================\n");
    printf("Input Devices List\n");
    printf("========================================\n\n");
    
    if (glob("/dev/input/event*", 0, NULL, &globbuf) != 0) {
        printf("No input devices found\n");
        return 1;
    }
    
    for (size_t i = 0; i < globbuf.gl_pathc; i++) {
        int fd = open(globbuf.gl_pathv[i], O_RDONLY);
        if (fd < 0) {
            printf("%s: Cannot open\n", globbuf.gl_pathv[i]);
            continue;
        }
        
        char name[256] = {0};
        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0) {
            printf("%s:\n", globbuf.gl_pathv[i]);
            printf("  Name: %s\n", name);
            
            // Check if it's T500RS
            if (strstr(name, "T500RS") || strstr(name, "T500 RS") ||
                strstr(name, "Thrustmaster") || strstr(name, "Force Feedback Wheel")) {
                printf("  *** THIS IS THE T500RS! ***\n");

                // Identify which one
                if (strstr(name, "Force Feedback Wheel")) {
                    printf("  *** UINPUT DEVICE (use this one!) ***\n");
                } else {
                    printf("  *** KERNEL HID DEVICE (ignore this) ***\n");
                }
                
                // Get capabilities
                unsigned long evbit[EV_MAX/sizeof(long) + 1];
                memset(evbit, 0, sizeof(evbit));
                if (ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), evbit) >= 0) {
                    printf("  Capabilities:\n");
                    if (evbit[0] & (1 << EV_KEY)) printf("    - Buttons (EV_KEY)\n");
                    if (evbit[0] & (1 << EV_ABS)) printf("    - Axes (EV_ABS)\n");
                    if (evbit[0] & (1 << EV_REL)) printf("    - Relative (EV_REL)\n");
                    if (evbit[0] & (1 << EV_FF)) printf("    - Force Feedback (EV_FF)\n");
                }
                
                // Get axis info
                if (evbit[0] & (1 << EV_ABS)) {
                    unsigned long absbit[ABS_MAX/sizeof(long) + 1];
                    memset(absbit, 0, sizeof(absbit));
                    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbit)), absbit) >= 0) {
                        printf("  Axes:\n");
                        if (absbit[0] & (1 << ABS_X)) printf("    - ABS_X (Steering)\n");
                        if (absbit[0] & (1 << ABS_Y)) printf("    - ABS_Y (Throttle)\n");
                        if (absbit[0] & (1 << ABS_Z)) printf("    - ABS_Z (Brake)\n");
                        if (absbit[0] & (1 << ABS_RZ)) printf("    - ABS_RZ (Clutch)\n");
                    }
                }
            }
            printf("\n");
        }
        
        close(fd);
    }
    
    globfree(&globbuf);
    
    printf("========================================\n");
    printf("To test input from a device:\n");
    printf("  sudo evtest /dev/input/eventX\n");
    printf("========================================\n");
    
    return 0;
}

