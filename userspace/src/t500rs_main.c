/**
 * @file t500rs_main.c
 * @brief Main program for T500RS userspace driver
 * 
 * This module contains the main event loop, initialization, cleanup,
 * and signal handling for the T500RS force feedback driver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/uinput.h>
#include "../include/t500rs_common.h"
#include "../include/t500rs_usb.h"
#include "../include/t500rs_input.h"
#include "../include/t500rs_effects.h"
#include "../include/t500rs_force.h"

/* ============================================================================
 * Global State
 * ============================================================================ */

libusb_context *usb_ctx = NULL;
libusb_device_handle *usb_handle = NULL;
int uinput_fd = -1;
int running = 1;
struct effect_state effects[MAX_EFFECTS];
pthread_mutex_t effects_lock = PTHREAD_MUTEX_INITIALIZER;
struct t500rs_config config = {
    .invert_throttle = 1,
    .invert_brake = 1,
    .invert_clutch = 1,
    /* Advanced FF settings - all enabled by default */
    .enable_force_smoothing = 1,
    .enable_multi_effect_mixing = 1,
    .enable_dynamic_update_rate = 1
};
uint16_t current_gain = 0xffff;  /* Default: maximum */

FILE *log_file = NULL;

/* ============================================================================
 * Signal Handling
 * ============================================================================ */

static void signal_handler(int sig)
{
    LOG_INFO("Received signal %d, shutting down...", sig);
    running = 0;
}

/* ============================================================================
 * Cleanup
 * ============================================================================ */

static void cleanup(void)
{
    LOG_INFO("Cleaning up...");

    /* Stop threads */
    running = 0;
    input_thread_stop();
    force_thread_stop();

    /* Stop all active effects */
    pthread_mutex_lock(&effects_lock);
    for (int i = 0; i < MAX_EFFECTS; i++) {
        if (effects[i].active) {
            stop_effect(i);
            effects[i].active = 0;
            effects[i].is_ramp = 0;
            effects[i].is_constant = 0;
            effects[i].is_periodic = 0;
        }
    }
    pthread_mutex_unlock(&effects_lock);

    /* Send stop commands for ALL effect slots */
    if (usb_handle) {
        unsigned char buf[4];
        for (int i = 0; i < MAX_EFFECTS; i++) {
            buf[0] = 0x41;
            buf[1] = i;
            buf[2] = 0x00;
            buf[3] = 0x01;
            usb_send(buf, 4);
        }

        /* Clear any ramp state */
        memset(buf, 0, sizeof(buf));
        buf[0] = 0x04;
        buf[1] = 0x0e;
        usb_send(buf, 9);
    }

    /* Destroy uinput device */
    input_device_destroy();

    /* Close USB device */
    usb_device_close();

    /* Close log file */
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }

    LOG_INFO("Cleanup complete");
}

/* ============================================================================
 * Force Feedback Event Handlers
 * ============================================================================ */

static int handle_ff_upload(struct uinput_ff_upload *upload)
{
    int ret = 0;
    int id = upload->effect.id;

    pthread_mutex_lock(&effects_lock);

    if (id < 0 || id >= MAX_EFFECTS) {
        LOG_ERROR("Invalid effect ID: %d", id);
        upload->retval = -EINVAL;
        pthread_mutex_unlock(&effects_lock);
        return -1;
    }

    /* Store effect */
    effects[id].effect = upload->effect;
    effects[id].active = 0;

    /* Upload to device */
    switch (upload->effect.type) {
    case FF_CONSTANT:
        ret = upload_constant_effect(id, &upload->effect);
        break;
    case FF_SPRING:
    case FF_DAMPER:
    case FF_FRICTION:
    case FF_INERTIA:
        ret = upload_condition_effect(id, &upload->effect);
        break;
    case FF_PERIODIC:
        ret = upload_periodic_effect(id, &upload->effect);
        break;
    case FF_RAMP:
#if ENABLE_RAMP_EFFECTS
        ret = upload_ramp_effect(id, &upload->effect);
#else
        LOG_ERROR("Ramp effects disabled (firmware limitation)");
        ret = -ENOSYS;
#endif
        break;
    case FF_RUMBLE:
        /* Convert rumble to constant force */
        LOG_DEBUG("Rumble effect - converting to constant force");
        upload->effect.type = FF_CONSTANT;
        upload->effect.u.constant.level = upload->effect.u.rumble.strong_magnitude / 2;
        ret = upload_constant_effect(id, &upload->effect);
        break;
    default:
        LOG_ERROR("Unsupported effect type: %d", upload->effect.type);
        ret = -EINVAL;
        break;
    }

    upload->retval = ret;
    pthread_mutex_unlock(&effects_lock);

    return ret;
}

static int handle_ff_erase(struct uinput_ff_erase *erase)
{
    int id = erase->effect_id;

    pthread_mutex_lock(&effects_lock);

    if (id < 0 || id >= MAX_EFFECTS) {
        erase->retval = -EINVAL;
        pthread_mutex_unlock(&effects_lock);
        return -1;
    }

    /* Stop if active */
    if (effects[id].active) {
        stop_effect(id);
        effects[id].active = 0;
    }

    erase->retval = 0;
    pthread_mutex_unlock(&effects_lock);

    return 0;
}

/* ============================================================================
 * Main Event Loop
 * ============================================================================ */

static void process_uinput_events(void)
{
    struct input_event ev;
    struct uinput_ff_upload upload;
    struct uinput_ff_erase erase;
    ssize_t n;

    while (running) {
        n = read(uinput_fd, &ev, sizeof(ev));
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                usleep(10000);  /* Sleep 10ms to avoid busy loop */
                continue;
            }
            LOG_ERROR("Failed to read uinput event: %s", strerror(errno));
            break;
        }

        if (n != sizeof(ev))
            continue;

        switch (ev.type) {
        case EV_UINPUT:
            switch (ev.code) {
            case UI_FF_UPLOAD:
                memset(&upload, 0, sizeof(upload));
                upload.request_id = ev.value;
                if (ioctl(uinput_fd, UI_BEGIN_FF_UPLOAD, &upload) < 0) {
                    LOG_ERROR("UI_BEGIN_FF_UPLOAD failed");
                    break;
                }
                handle_ff_upload(&upload);
                if (ioctl(uinput_fd, UI_END_FF_UPLOAD, &upload) < 0) {
                    LOG_ERROR("UI_END_FF_UPLOAD failed");
                }
                break;

            case UI_FF_ERASE:
                memset(&erase, 0, sizeof(erase));
                erase.request_id = ev.value;
                if (ioctl(uinput_fd, UI_BEGIN_FF_ERASE, &erase) < 0) {
                    LOG_ERROR("UI_BEGIN_FF_ERASE failed");
                    break;
                }
                handle_ff_erase(&erase);
                if (ioctl(uinput_fd, UI_END_FF_ERASE, &erase) < 0) {
                    LOG_ERROR("UI_END_FF_ERASE failed");
                }
                break;
            }
            break;

        case EV_FF:
            /* Effect play/stop */
            LOG_INFO("EV_FF event: code=0x%02x (%d), value=%d", ev.code, ev.code, ev.value);

            /* Handle special codes */
            if (ev.code == FF_GAIN) {
                LOG_INFO("Gain control: %d (%.1f%%)", ev.value, (ev.value * 100.0) / 65535);
                set_gain(ev.value);
                break;
            }

            if (ev.code == FF_AUTOCENTER) {
                LOG_INFO("Autocenter control: %d (%.1f%%)", ev.value, (ev.value * 100.0) / 65535);
                set_autocenter(ev.value);
                break;
            }

            /* Advanced FF configuration controls */
            if (ev.code == FF_TOGGLE_SMOOTHING) {
                config.enable_force_smoothing = (ev.value != 0);
                LOG_INFO("Force smoothing: %s", config.enable_force_smoothing ? "ENABLED" : "DISABLED");
                break;
            }

            if (ev.code == FF_TOGGLE_MIXING) {
                config.enable_multi_effect_mixing = (ev.value != 0);
                LOG_INFO("Multi-effect mixing: %s", config.enable_multi_effect_mixing ? "ENABLED" : "DISABLED");
                break;
            }

            if (ev.code == FF_TOGGLE_DYNAMIC_RATE) {
                config.enable_dynamic_update_rate = (ev.value != 0);
                LOG_INFO("Dynamic update rate: %s", config.enable_dynamic_update_rate ? "ENABLED" : "DISABLED");
                break;
            }

            if (ev.code == FF_GET_CONFIG) {
                LOG_INFO("=== Current Configuration ===");
                LOG_INFO("Force smoothing: %s", config.enable_force_smoothing ? "ENABLED" : "DISABLED");
                LOG_INFO("Multi-effect mixing: %s", config.enable_multi_effect_mixing ? "ENABLED" : "DISABLED");
                LOG_INFO("Dynamic update rate: %s", config.enable_dynamic_update_rate ? "ENABLED" : "DISABLED");
                LOG_INFO("============================");
                break;
            }

            /* Normal effect play/stop */
            pthread_mutex_lock(&effects_lock);
            if (ev.code < MAX_EFFECTS) {
                if (ev.value > 0) {
                    LOG_DEBUG("Playing effect %d", ev.code);

                    /* Initialize ramp state if this is a ramp effect */
                    if (effects[ev.code].effect.type == FF_RAMP) {
                        effects[ev.code].is_ramp = 1;
                        effects[ev.code].ramp_start_level = effects[ev.code].effect.u.ramp.start_level;
                        effects[ev.code].ramp_end_level = effects[ev.code].effect.u.ramp.end_level;
                        effects[ev.code].ramp_duration_ms = effects[ev.code].effect.replay.length;
                        clock_gettime(CLOCK_MONOTONIC, &effects[ev.code].start_time);
                    }

                    start_effect(ev.code);
                    effects[ev.code].active = 1;
                } else {
                    LOG_DEBUG("Stopping effect %d", ev.code);
                    stop_effect(ev.code);
                    effects[ev.code].active = 0;
                    effects[ev.code].is_ramp = 0;
                    effects[ev.code].is_constant = 0;
                    effects[ev.code].is_periodic = 0;
                }
            }
            pthread_mutex_unlock(&effects_lock);
            break;
        }
    }
}

/* ============================================================================
 * Main Function
 * ============================================================================ */

int main(int argc, char **argv)
{
    int ret;

    (void)argc;  /* Unused */
    (void)argv;  /* Unused */

    printf("========================================\n");
    printf("T500RS Force Feedback Driver\n");
    printf("========================================\n\n");

    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Initialize effects array */
    memset(effects, 0, sizeof(effects));

    /* Open USB device */
    ret = usb_device_open();
    if (ret != 0) {
        LOG_ERROR("Failed to open USB device");
        cleanup();
        return 1;
    }

    /* Create uinput device */
    ret = input_device_create();
    if (ret < 0) {
        LOG_ERROR("uinput setup failed");
        cleanup();
        return 1;
    }

    LOG_INFO("========================================");
    LOG_INFO("T500RS Force Feedback Driver Running");
    LOG_INFO("========================================");
    LOG_INFO("Device: /dev/input/eventX (check dmesg for exact number)");
    LOG_INFO("Press Ctrl+C to stop");
    LOG_INFO("");

    /* Start force update thread */
    ret = force_thread_start();
    if (ret != 0) {
        LOG_ERROR("Failed to start force update thread");
        cleanup();
        return 1;
    }

    /* Start input reading thread */
    ret = input_thread_start();
    if (ret != 0) {
        LOG_ERROR("Failed to start input reading thread");
        cleanup();
        return 1;
    }

    /* Main event loop */
    process_uinput_events();

    /* Cleanup */
    cleanup();

    LOG_INFO("Driver stopped");
    return 0;
}

