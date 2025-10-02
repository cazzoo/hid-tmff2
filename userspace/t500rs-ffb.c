/*
 * T500RS Force Feedback Userspace Driver
 *
 * Copyright (C) 2025
 *
 * This program handles force feedback for the Thrustmaster T500RS
 * racing wheel using libusb and uinput.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <libusb-1.0/libusb.h>
#include <pthread.h>

#define VENDOR_ID  0x044f
#define PRODUCT_ID 0xb65e
#define EP_OUT     0x01
#define EP_IN      0x82
#define INTERFACE  0

#define MAX_EFFECTS 16

/* Global state */
static libusb_context *usb_ctx = NULL;
static libusb_device_handle *usb_handle = NULL;
static int uinput_fd = -1;
static int running = 1;

/* Effect state */
struct effect_state {
    int active;
    struct ff_effect effect;
};

static struct effect_state effects[MAX_EFFECTS];
static pthread_mutex_t effects_lock = PTHREAD_MUTEX_INITIALIZER;

/* Logging */
#define LOG_INFO(fmt, ...) fprintf(stdout, "[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) fprintf(stdout, "[DEBUG] " fmt "\n", ##__VA_ARGS__)

/* Signal handler */
static void signal_handler(int sig)
{
    LOG_INFO("Received signal %d, shutting down...", sig);
    running = 0;
}

/* USB communication */
static int usb_send(unsigned char *data, int len)
{
    int ret, transferred;
    char hex_str[128];
    int pos = 0;

    /* Build hex string for debug */
    for (int i = 0; i < len && pos < sizeof(hex_str) - 3; i++) {
        pos += snprintf(hex_str + pos, sizeof(hex_str) - pos, "%02x ", data[i]);
    }

    LOG_DEBUG("USB SEND: [%s]", hex_str);

    ret = libusb_interrupt_transfer(usb_handle, EP_OUT, data, len, &transferred, 1000);
    if (ret < 0) {
        LOG_ERROR("USB transfer failed: %s", libusb_error_name(ret));
        return ret;
    }

    if (transferred != len) {
        LOG_ERROR("USB transfer incomplete: %d/%d bytes", transferred, len);
        return -1;
    }

    return 0;
}

/* Device initialization */
static int t500rs_initialize(void)
{
    unsigned char buf[15];
    int ret;

    LOG_INFO("Initializing T500RS...");

    /* Report 0x42 - Init */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x42;
    buf[1] = 0x01;
    ret = usb_send(buf, 15);
    if (ret) return ret;
    usleep(40000);

    /* Report 0x0a - Config 1 */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x0a;
    buf[1] = 0x04;
    buf[2] = 0x90;
    buf[3] = 0x03;
    ret = usb_send(buf, 15);
    if (ret) return ret;
    usleep(4000);

    /* Report 0x0a - Config 2 */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x0a;
    buf[1] = 0x04;
    buf[2] = 0x12;
    buf[3] = 0x10;
    ret = usb_send(buf, 15);
    if (ret) return ret;
    usleep(4000);

    /* Report 0x0a - Config 3 */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x0a;
    buf[1] = 0x04;
    buf[2] = 0x00;
    buf[3] = 0x06;
    ret = usb_send(buf, 15);
    if (ret) return ret;
    usleep(64000);

    /* Report 0x40 */
    buf[0] = 0x40;
    buf[1] = 0x11;
    buf[2] = 0x55;
    buf[3] = 0xd5;
    ret = usb_send(buf, 4);
    if (ret) return ret;
    usleep(10000);

    /* Report 0x42 short */
    buf[0] = 0x42;
    buf[1] = 0x04;
    ret = usb_send(buf, 2);
    if (ret) return ret;
    usleep(8000);

    /* Report 0x40 */
    buf[0] = 0x40;
    buf[1] = 0x04;
    buf[2] = 0x00;
    buf[3] = 0x00;
    ret = usb_send(buf, 4);
    if (ret) return ret;
    usleep(8000);

    /* Report 0x40 */
    buf[0] = 0x40;
    buf[1] = 0x03;
    buf[2] = 0x0d;
    buf[3] = 0x00;
    ret = usb_send(buf, 4);
    if (ret) return ret;

    LOG_INFO("Initialization complete");
    return 0;
}

/* Upload constant force effect */
static int upload_constant_effect(int id, struct ff_effect *effect)
{
    unsigned char buf[15];
    int ret;

    LOG_DEBUG("Uploading constant effect %d, force=%d",
              id, effect->u.constant.level);

    /* Report 0x02 - Envelope (attack/fade) - use defaults */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x02;
    buf[1] = 0x1c;
    buf[2] = 0x00;
    buf[3] = 0x00;
    buf[4] = 0x00;
    buf[5] = 0x00;
    buf[6] = 0x00;
    buf[7] = 0x00;
    buf[8] = 0x00;
    ret = usb_send(buf, 9);
    if (ret) return ret;
    usleep(5000);

    /* Report 0x03 - Force level (set to 0 initially, will be set on play) */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x03;
    buf[1] = 0x0e;
    buf[2] = 0x00;
    buf[3] = 0x00;  /* Will be set when effect starts */
    ret = usb_send(buf, 4);
    if (ret) return ret;
    usleep(5000);

    /* Report 0x01 - Main effect upload */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x01;
    buf[1] = id;
    buf[2] = 0x00;  /* Constant force type */
    buf[3] = 0x40;
    buf[4] = 0x69;
    buf[5] = 0x23;
    buf[6] = 0x00;
    buf[7] = 0xff;
    buf[8] = 0xff;
    buf[9] = 0x0e;
    buf[10] = 0x00;
    buf[11] = 0x1c;
    buf[12] = 0x00;
    buf[13] = 0x00;
    buf[14] = 0x00;
    ret = usb_send(buf, 15);
    if (ret) return ret;

    LOG_DEBUG("Constant effect uploaded");

    return 0;
}

/* Upload spring/damper/friction effect */
static int upload_condition_effect(int id, struct ff_effect *effect)
{
    unsigned char buf[15];
    int ret;
    unsigned char effect_type;
    const char *type_name;

    /* Determine effect type */
    switch (effect->type) {
    case FF_SPRING:
        effect_type = 0x40;
        type_name = "spring";
        break;
    case FF_DAMPER:
        effect_type = 0x41;
        type_name = "damper";
        break;
    case FF_FRICTION:
        effect_type = 0x41;
        type_name = "friction";
        break;
    case FF_INERTIA:
        effect_type = 0x41;
        type_name = "inertia";
        break;
    default:
        LOG_ERROR("Unknown condition effect type: %d", effect->type);
        return -1;
    }

    /* Coefficient - scale to 0-100 (0x64) as seen in captures */
    int coeff = effect->u.condition[0].right_coeff;
    unsigned char strength = (abs(coeff) * 100) / 32767;

    LOG_DEBUG("Uploading %s effect %d, coeff=%d, strength=0x%02x",
              type_name, id, coeff, strength);

    /* Report 0x05 - Condition parameters (two reports needed) */
    /* First report - 0x0e */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x05;
    buf[1] = 0x0e;
    buf[2] = 0x00;
    buf[3] = strength;  /* Right coefficient */
    buf[4] = strength;  /* Left coefficient */
    buf[5] = 0x00;
    buf[6] = 0x00;
    buf[7] = 0x00;
    buf[8] = 0x00;
    buf[9] = 0x64;   /* Right saturation */
    buf[10] = 0x64;  /* Left saturation */
    ret = usb_send(buf, 11);
    if (ret) return ret;
    usleep(5000);

    /* Second report - 0x1c */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x05;
    buf[1] = 0x1c;
    buf[2] = 0x00;
    buf[3] = 0x00;
    buf[4] = 0x00;
    buf[5] = 0x00;
    buf[6] = 0x00;
    buf[7] = 0x00;
    buf[8] = 0x00;
    buf[9] = 0x64;   /* Right saturation */
    buf[10] = 0x64;  /* Left saturation */
    ret = usb_send(buf, 11);
    if (ret) return ret;
    usleep(5000);

    /* Report 0x01 - Main effect upload */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x01;
    buf[1] = id;
    buf[2] = effect_type;  /* 0x40 for spring, 0x41 for damper/friction/inertia */
    buf[3] = 0x40;
    buf[4] = 0x17;
    buf[5] = 0x25;
    buf[6] = 0x00;
    buf[7] = 0xff;
    buf[8] = 0xff;
    buf[9] = 0x0e;
    buf[10] = 0x00;
    buf[11] = 0x1c;
    buf[12] = 0x00;
    buf[13] = 0x00;
    buf[14] = 0x00;

    ret = usb_send(buf, 15);
    if (ret) return ret;

    LOG_DEBUG("%s effect uploaded: strength=0x%02x", type_name, strength);

    return 0;
}

/* Start effect */
static int start_effect(int id)
{
    unsigned char buf[4];
    int is_constant = 0;
    int force = 0;
    int ret;

    LOG_DEBUG("Starting effect %d", id);

    /* Check if this is a constant force (mutex already locked by caller) */
    if (id >= 0 && id < MAX_EFFECTS) {
        if (effects[id].effect.type == FF_CONSTANT) {
            is_constant = 1;
            force = effects[id].effect.u.constant.level;
        }
    }

    /* For constant force, set the level before starting */
    if (is_constant) {
        int abs_force = abs(force);
        unsigned char level = (abs_force * 127) / 32767;

        /* Report 0x03 - Set force level */
        buf[0] = 0x03;
        buf[1] = 0x0e;
        buf[2] = 0x00;
        buf[3] = level;
        ret = usb_send(buf, 4);
        if (ret) return ret;
        usleep(5000);

        LOG_DEBUG("Set constant force level to 0x%02x", level);
    }

    /* Send start command */
    buf[0] = 0x41;
    buf[1] = id;

    /* For negative constant force, use different direction code */
    if (is_constant && force < 0) {
        buf[2] = 0x00;  /* Left/negative direction */
        buf[3] = 0x01;
        LOG_DEBUG("Starting negative constant force (left)");
    } else {
        buf[2] = 0x41;  /* Right/positive direction */
        buf[3] = 0x01;
    }

    return usb_send(buf, 4);
}

/* Stop effect */
static int stop_effect(int id)
{
    unsigned char buf[4];

    LOG_DEBUG("Stopping effect %d", id);

    buf[0] = 0x41;
    buf[1] = id;
    buf[2] = 0x00;
    buf[3] = 0x01;

    return usb_send(buf, 4);
}




/* Handle force feedback upload */
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
        /* Periodic effects not yet implemented - would need Report 0x04 */
        LOG_ERROR("Periodic effects not yet supported");
        ret = -ENOSYS;
        break;
    case FF_RAMP:
        /* Ramp effects not yet implemented */
        LOG_ERROR("Ramp effects not yet supported");
        ret = -ENOSYS;
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

/* Handle force feedback erase */
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

/* Process uinput events */
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
            LOG_DEBUG("EV_FF event: code=%d, value=%d", ev.code, ev.value);
            pthread_mutex_lock(&effects_lock);
            if (ev.code < MAX_EFFECTS) {
                if (ev.value > 0) {
                    LOG_DEBUG("Playing effect %d", ev.code);
                    start_effect(ev.code);
                    effects[ev.code].active = 1;
                } else {
                    LOG_DEBUG("Stopping effect %d", ev.code);
                    stop_effect(ev.code);
                    effects[ev.code].active = 0;
                }
            }
            pthread_mutex_unlock(&effects_lock);
            break;
        }
    }
}

/* Setup uinput device */
static int setup_uinput(void)
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

    /* Enable force feedback effects */
    ioctl(uinput_fd, UI_SET_FFBIT, FF_CONSTANT);
    ioctl(uinput_fd, UI_SET_FFBIT, FF_SPRING);
    ioctl(uinput_fd, UI_SET_FFBIT, FF_DAMPER);
    ioctl(uinput_fd, UI_SET_FFBIT, FF_FRICTION);
    ioctl(uinput_fd, UI_SET_FFBIT, FF_INERTIA);
    ioctl(uinput_fd, UI_SET_FFBIT, FF_GAIN);
    ioctl(uinput_fd, UI_SET_FFBIT, FF_AUTOCENTER);

    /* Setup device info */
    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = VENDOR_ID;
    usetup.id.product = PRODUCT_ID;
    usetup.id.version = 1;
    strcpy(usetup.name, "Thrustmaster T500RS (FFB)");
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
    return 0;
}

/* Cleanup */
static void cleanup(void)
{
    LOG_INFO("Cleaning up...");

    /* Stop all active effects */
    pthread_mutex_lock(&effects_lock);
    for (int i = 0; i < MAX_EFFECTS; i++) {
        if (effects[i].active) {
            stop_effect(i);
        }
    }
    pthread_mutex_unlock(&effects_lock);

    /* Destroy uinput device */
    if (uinput_fd >= 0) {
        ioctl(uinput_fd, UI_DEV_DESTROY);
        close(uinput_fd);
    }

    /* Release USB interface */
    if (usb_handle) {
        libusb_release_interface(usb_handle, INTERFACE);
        libusb_attach_kernel_driver(usb_handle, INTERFACE);
        libusb_close(usb_handle);
    }

    if (usb_ctx) {
        libusb_exit(usb_ctx);
    }

    LOG_INFO("Cleanup complete");
}

/* Main function */
int main(int argc, char **argv)
{
    int ret;

    printf("========================================\n");
    printf("T500RS Force Feedback Driver\n");
    printf("========================================\n\n");

    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Initialize libusb */
    LOG_INFO("Initializing libusb...");
    ret = libusb_init(&usb_ctx);
    if (ret < 0) {
        LOG_ERROR("libusb_init failed: %s", libusb_error_name(ret));
        return 1;
    }

    /* Open device */
    LOG_INFO("Opening T500RS device...");
    usb_handle = libusb_open_device_with_vid_pid(usb_ctx, VENDOR_ID, PRODUCT_ID);
    if (!usb_handle) {
        LOG_ERROR("Cannot open device (VID=%04x, PID=%04x)", VENDOR_ID, PRODUCT_ID);
        LOG_ERROR("Make sure the device is connected and you have permissions");
        cleanup();
        return 1;
    }
    LOG_INFO("Device opened successfully");

    /* Detach kernel driver */
    if (libusb_kernel_driver_active(usb_handle, INTERFACE) == 1) {
        LOG_INFO("Detaching kernel driver...");
        ret = libusb_detach_kernel_driver(usb_handle, INTERFACE);
        if (ret < 0) {
            LOG_ERROR("Failed to detach kernel driver: %s", libusb_error_name(ret));
            LOG_ERROR("Try: sudo rmmod hid_tmff_new");
            cleanup();
            return 1;
        }
    }

    /* Claim interface */
    LOG_INFO("Claiming USB interface...");
    ret = libusb_claim_interface(usb_handle, INTERFACE);
    if (ret < 0) {
        LOG_ERROR("Cannot claim interface: %s", libusb_error_name(ret));
        cleanup();
        return 1;
    }

    /* Initialize device */
    ret = t500rs_initialize();
    if (ret < 0) {
        LOG_ERROR("Device initialization failed");
        cleanup();
        return 1;
    }

    /* Setup uinput */
    ret = setup_uinput();
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

    /* Main event loop */
    process_uinput_events();

    /* Cleanup */
    cleanup();

    LOG_INFO("Driver stopped");
    return 0;
}
