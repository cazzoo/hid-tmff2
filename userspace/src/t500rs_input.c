/**
 * @file t500rs_input.c
 * @brief Input handling for steering, pedals, buttons, and D-pad
 * 
 * This module handles all input from the T500RS device including:
 * - Steering wheel (16-bit precision)
 * - Pedals (throttle, brake, clutch)
 * - Buttons (16 buttons)
 * - D-pad (8 directions)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <linux/uinput.h>
#include "../include/t500rs_input.h"
#include "../include/t500rs_common.h"
#include "../include/t500rs_bridge.h"

/* USB endpoints */
#define EP_IN   0x82

/* Input thread */
static pthread_t input_thread = 0;
static int input_thread_running = 0;

/* External running flag (from main) */
extern int running;

/* Pedal inversion flags (from config) */
static int invert_throttle = 0;
static int invert_brake = 0;
static int invert_clutch = 0;

/* ============================================================================
 * uinput Device Management
 * ============================================================================ */

int input_device_create(void)
{
    struct uinput_setup usetup;
    struct uinput_abs_setup abs_setup;
    int i;

    LOG_INFO("Setting up uinput device...");

    uinput_fd = open("/dev/uinput", O_RDWR | O_NONBLOCK);
    if (uinput_fd < 0) {
        LOG_ERROR("Failed to open /dev/uinput: %s", strerror(errno));
        return -1;
    }

    /* Enable event types */
    ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(uinput_fd, UI_SET_EVBIT, EV_ABS);
    ioctl(uinput_fd, UI_SET_EVBIT, EV_FF);

    /* Enable buttons (0-31) */
    for (i = BTN_JOYSTICK; i < BTN_JOYSTICK + 32; i++) {
        ioctl(uinput_fd, UI_SET_KEYBIT, i);
    }

    /* Enable axes */
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_X);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_Y);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_Z);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_RZ);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_HAT0X);  /* D-pad X */
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_HAT0Y);  /* D-pad Y */

    /* Enable force feedback effects */
    ioctl(uinput_fd, UI_SET_FFBIT, FF_CONSTANT);
    ioctl(uinput_fd, UI_SET_FFBIT, FF_SPRING);
    ioctl(uinput_fd, UI_SET_FFBIT, FF_DAMPER);
    ioctl(uinput_fd, UI_SET_FFBIT, FF_FRICTION);
    ioctl(uinput_fd, UI_SET_FFBIT, FF_INERTIA);
    ioctl(uinput_fd, UI_SET_FFBIT, FF_PERIODIC);
    ioctl(uinput_fd, UI_SET_FFBIT, FF_SINE);
    ioctl(uinput_fd, UI_SET_FFBIT, FF_SQUARE);
    ioctl(uinput_fd, UI_SET_FFBIT, FF_TRIANGLE);
    ioctl(uinput_fd, UI_SET_FFBIT, FF_SAW_UP);
    ioctl(uinput_fd, UI_SET_FFBIT, FF_SAW_DOWN);
    ioctl(uinput_fd, UI_SET_FFBIT, FF_RAMP);
    ioctl(uinput_fd, UI_SET_FFBIT, FF_RUMBLE);
    ioctl(uinput_fd, UI_SET_FFBIT, FF_GAIN);
    ioctl(uinput_fd, UI_SET_FFBIT, FF_AUTOCENTER);

    /* Setup device info */
    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = VENDOR_ID;
    usetup.id.product = PRODUCT_ID;
    usetup.id.version = 1;
    strcpy(usetup.name, "T500RS Force Feedback Wheel");
    usetup.ff_effects_max = MAX_EFFECTS;

    if (ioctl(uinput_fd, UI_DEV_SETUP, &usetup) < 0) {
        LOG_ERROR("UI_DEV_SETUP failed: %s", strerror(errno));
        close(uinput_fd);
        return -1;
    }

    /* Setup axes ranges */
    memset(&abs_setup, 0, sizeof(abs_setup));
    abs_setup.code = ABS_X;
    abs_setup.absinfo.minimum = -32768;
    abs_setup.absinfo.maximum = 32767;
    ioctl(uinput_fd, UI_ABS_SETUP, &abs_setup);

    abs_setup.code = ABS_Y;
    abs_setup.absinfo.minimum = 0;
    abs_setup.absinfo.maximum = 255;
    ioctl(uinput_fd, UI_ABS_SETUP, &abs_setup);

    abs_setup.code = ABS_Z;
    ioctl(uinput_fd, UI_ABS_SETUP, &abs_setup);

    abs_setup.code = ABS_RZ;
    ioctl(uinput_fd, UI_ABS_SETUP, &abs_setup);

    /* D-pad axes */
    abs_setup.code = ABS_HAT0X;
    abs_setup.absinfo.minimum = -1;
    abs_setup.absinfo.maximum = 1;
    ioctl(uinput_fd, UI_ABS_SETUP, &abs_setup);

    abs_setup.code = ABS_HAT0Y;
    ioctl(uinput_fd, UI_ABS_SETUP, &abs_setup);

    /* Create device */
    if (ioctl(uinput_fd, UI_DEV_CREATE) < 0) {
        LOG_ERROR("UI_DEV_CREATE failed: %s", strerror(errno));
        close(uinput_fd);
        return -1;
    }

    /* Give kernel time to create the device */
    sleep(1);

    /* Send initial sync event */
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    write(uinput_fd, &ev, sizeof(ev));

    LOG_INFO("uinput device created successfully");
    LOG_INFO("Check dmesg for device path: dmesg | grep 'T500RS'");
    
    /* Copy pedal inversion settings from global config */
    invert_throttle = config.invert_throttle;
    invert_brake = config.invert_brake;
    invert_clutch = config.invert_clutch;
    
    return 0;
}

void input_device_destroy(void)
{
    if (uinput_fd >= 0) {
        ioctl(uinput_fd, UI_DEV_DESTROY);
        close(uinput_fd);
        uinput_fd = -1;
    }
}

/* ============================================================================
 * Input Processing
 * ============================================================================ */

void input_process_report(const unsigned char *buf, int len)
{
    struct input_event ev;
    
    if (len < 15 || buf[0] != 0x07) {
        return;  /* Invalid report */
    }
    
    memset(&ev, 0, sizeof(ev));
    gettimeofday((struct timeval *)&ev.time, NULL);
    ev.type = EV_ABS;
    
    /* Steering - bytes 1-2 (little-endian 16-bit) */
    uint16_t steering_raw = buf[1] | (buf[2] << 8);
    int16_t steering = (int16_t)steering_raw - 32768;
    ev.code = ABS_X;
    ev.value = steering;
    write(uinput_fd, &ev, sizeof(ev));

    /* Brake - bytes 3-4 (hardware has throttle/brake swapped!) */
    uint16_t brake = buf[3] | (buf[4] << 8);
    int brake_scaled = (brake * 255) / 1023;
    if (invert_brake) {
        brake_scaled = 255 - brake_scaled;
    }
    ev.code = ABS_Z;
    ev.value = brake_scaled;
    write(uinput_fd, &ev, sizeof(ev));

    /* Throttle - bytes 5-6 (hardware has throttle/brake swapped!) */
    uint16_t throttle = buf[5] | (buf[6] << 8);
    int throttle_scaled = (throttle * 255) / 1023;
    if (invert_throttle) {
        throttle_scaled = 255 - throttle_scaled;
    }
    ev.code = ABS_Y;
    ev.value = throttle_scaled;
    write(uinput_fd, &ev, sizeof(ev));

    /* Clutch - bytes 7-8 */
    uint16_t clutch = buf[7] | (buf[8] << 8);
    int clutch_scaled = (clutch * 255) / 1023;
    if (invert_clutch) {
        clutch_scaled = 255 - clutch_scaled;
    }
    ev.code = ABS_RZ;
    ev.value = clutch_scaled;
    write(uinput_fd, &ev, sizeof(ev));

    /* D-pad - byte 14 */
    uint8_t dpad = buf[14];
    int hat_x = 0, hat_y = 0;

    switch (dpad) {
        case 0x0F: /* Centered */ break;
        case 0x00: hat_y = -1; break;  /* Up */
        case 0x01: hat_x = 1; hat_y = -1; break;  /* Up-Right */
        case 0x02: hat_x = 1; break;  /* Right */
        case 0x03: hat_x = 1; hat_y = 1; break;  /* Down-Right */
        case 0x04: hat_y = 1; break;  /* Down */
        case 0x05: hat_x = -1; hat_y = 1; break;  /* Down-Left */
        case 0x06: hat_x = -1; break;  /* Left */
        case 0x07: hat_x = -1; hat_y = -1; break;  /* Up-Left */
        default: break;  /* Unknown, treat as centered */
    }

    ev.code = ABS_HAT0X;
    ev.value = hat_x;
    write(uinput_fd, &ev, sizeof(ev));

    ev.code = ABS_HAT0Y;
    ev.value = hat_y;
    write(uinput_fd, &ev, sizeof(ev));

    /* Buttons - bytes 11-13 */
    static uint32_t last_buttons = 0;
    uint32_t buttons = buf[11] | (buf[12] << 8) | (buf[13] << 16);

    if (buttons != last_buttons) {
        for (int bit = 0; bit < 24; bit++) {
            int old_state = (last_buttons >> bit) & 1;
            int new_state = (buttons >> bit) & 1;

            if (old_state != new_state) {
                ev.type = EV_KEY;
                ev.code = BTN_JOYSTICK + bit;
                ev.value = new_state;
                write(uinput_fd, &ev, sizeof(ev));
            }
        }
        last_buttons = buttons;
    }

    /* Send sync event */
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    write(uinput_fd, &ev, sizeof(ev));
}

/* ============================================================================
 * Input Reading Thread
 * ============================================================================ */

static void *input_reading_thread_func(void *arg)
{
    (void)arg;  /* Unused */
    unsigned char buf[64];
    int transferred;
    int ret;
    int disconnect_count = 0;
    int error_count = 0;

    LOG_INFO("Input reading thread started");

    while (running && input_thread_running) {
        /* Read from USB interrupt endpoint */
        ret = libusb_interrupt_transfer(usb_handle, EP_IN, buf, sizeof(buf), &transferred, 100);

        if (ret == LIBUSB_ERROR_TIMEOUT) {
            /* Timeout is normal, just continue */
            continue;
        }

        if (ret < 0) {
            if (ret == LIBUSB_ERROR_NO_DEVICE || ret == LIBUSB_ERROR_IO) {
                /* Device might be disconnected temporarily (mode switch)
                 * Give it some retries before giving up */
                disconnect_count++;

                if (disconnect_count > 50) {  /* 50 * 10ms = 500ms timeout */
                    LOG_INFO("Device disconnected for too long, stopping input thread");
                    break;
                }

                /* Don't spam logs for transient disconnects */
                if (disconnect_count % 10 == 1) {
                    LOG_INFO("Device temporarily unavailable, retrying... (%d)", disconnect_count);
                }

                usleep(10000);
                continue;
            } else {
                /* For other errors, reset disconnect counter */
                disconnect_count = 0;
            }

            if (ret != LIBUSB_ERROR_INTERRUPTED) {
                /* Don't spam on common transient errors */
                error_count++;
                if (error_count % 10 == 1) {
                    LOG_ERROR("USB read failed: %s (count: %d)", libusb_error_name(ret), error_count);
                }
            }
            usleep(10000);
            continue;
        } else {
            /* Successful read - reset counters */
            disconnect_count = 0;
            error_count = 0;
        }

        if (transferred < 15) {
            /* Too short, ignore */
            continue;
        }

        /* Debug: Print first packet */
        static int first_packet = 1;
        if (first_packet) {
            LOG_INFO("First HID packet (15 bytes):");
            fprintf(stderr, "  ");
            for (int i = 0; i < 15; i++) {
                fprintf(stderr, "%02x ", buf[i]);
            }
            fprintf(stderr, "\n");
            first_packet = 0;
        }

        /* Process input report */
        input_process_report(buf, transferred);
        
        /* Forward to Wine bridge if connected */
        if (bridge_is_connected()) {
            bridge_send_input(buf, transferred);
        }
    }

    LOG_INFO("Input reading thread stopped");
    return NULL;
}

int input_thread_start(void)
{
    input_thread_running = 1;

    if (pthread_create(&input_thread, NULL, input_reading_thread_func, NULL) != 0) {
        LOG_ERROR("Failed to create input reading thread");
        input_thread_running = 0;
        return -1;
    }

    LOG_INFO("Input reading thread created");
    return 0;
}

void input_thread_stop(void)
{
    if (input_thread_running) {
        input_thread_running = 0;
        pthread_join(input_thread, NULL);
        LOG_INFO("Input reading thread joined");
    }
}

