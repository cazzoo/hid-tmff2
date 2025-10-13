/*
 * Test input reading from T500RS
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <glob.h>

int find_t500rs_device(char *path, size_t size) {
    glob_t globbuf;
    glob("/dev/input/event*", 0, NULL, &globbuf);

    for (size_t i = 0; i < globbuf.gl_pathc; i++) {
        int fd = open(globbuf.gl_pathv[i], O_RDONLY);
        if (fd < 0) continue;

        char name[256] = {0};
        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0) {
            // Look for the uinput device
            if (strstr(name, "T500RS Force Feedback Wheel")) {
                strncpy(path, globbuf.gl_pathv[i], size - 1);
                close(fd);
                globfree(&globbuf);
                return 1;
            }
        }
        close(fd);
    }
    globfree(&globbuf);
    return 0;
}

int main() {
    int fd;
    struct input_event ev;
    char device_path[256];

    printf("========================================\n");
    printf("T500RS Input Test\n");
    printf("========================================\n\n");

    // Find the T500RS uinput device
    if (!find_t500rs_device(device_path, sizeof(device_path))) {
        printf("❌ T500RS uinput device not found!\n");
        printf("\nMake sure the driver is running: sudo ./run.sh\n");
        printf("Then check: ./list_input_devices\n");
        return 1;
    }

    printf("✅ Found device: %s\n", device_path);

    // Open the device
    fd = open(device_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }

    printf("✅ Opened successfully\n");
    printf("\nNow turn the wheel, press pedals, or press buttons...\n");
    printf("Press Ctrl+C to stop\n\n");
    printf("%-10s %-10s %-10s %s\n", "Type", "Code", "Value", "Description");
    printf("%-10s %-10s %-10s %s\n", "----", "----", "-----", "-----------");
    
    while (1) {
        ssize_t bytes = read(fd, &ev, sizeof(ev));
        
        if (bytes == sizeof(ev)) {
            // Only show ABS and KEY events
            if (ev.type == EV_ABS || ev.type == EV_KEY) {
                const char *desc = "";
                
                if (ev.type == EV_ABS) {
                    if (ev.code == ABS_X) desc = "Steering";
                    else if (ev.code == ABS_Y) desc = "Throttle";
                    else if (ev.code == ABS_Z) desc = "Brake";
                    else if (ev.code == ABS_RZ) desc = "Clutch";
                    else desc = "Unknown axis";
                } else if (ev.type == EV_KEY) {
                    if (ev.value == 1) desc = "Button PRESSED";
                    else if (ev.value == 0) desc = "Button RELEASED";
                }
                
                printf("%-10d %-10d %-10d %s\n", ev.type, ev.code, ev.value, desc);
                fflush(stdout);
            }
        } else if (bytes < 0) {
            // No data available (non-blocking)
            usleep(10000);  // Sleep 10ms
        }
    }
    
    close(fd);
    return 0;
}

