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
#include <sys/time.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <libusb-1.0/libusb.h>
#include <pthread.h>

/* Enable Windows-compatible protocol by default */
#define USE_WINDOWS_PROTOCOL 0  /* Temporarily disabled for testing */

#if USE_WINDOWS_PROTOCOL
#include "t500rs_protocol.h"
#endif

#define VENDOR_ID       0x044f
#define PRODUCT_ID_BOOT 0xb65d  /* Boot mode - before initialization */
#define PRODUCT_ID      0xb65e  /* Normal mode - after initialization */
#define EP_OUT          0x01
#define EP_IN           0x82
#define INTERFACE       0

#define MAX_EFFECTS 16

/* Configuration parameters */
static int invert_throttle = 1;  /* 1 = invert, 0 = normal */
static int invert_brake = 1;
static int invert_clutch = 1;

/* Global state */
static libusb_context *usb_ctx = NULL;
static libusb_device_handle *usb_handle = NULL;
static int uinput_fd = -1;
static int running = 1;
static pthread_t input_thread;

/* Effect state */
struct effect_state {
    int active;
    struct ff_effect effect;
    /* Ramp-specific state */
    int is_ramp;
    int ramp_start_level;
    int ramp_end_level;
    unsigned long ramp_duration_ms;
    struct timespec ramp_start_time;
    /* Constant force state for continuous updates */
    int is_constant;
    int current_force_level;
    struct timespec start_time;
};

static struct effect_state effects[MAX_EFFECTS];
static pthread_mutex_t effects_lock = PTHREAD_MUTEX_INITIALIZER;

/* Ramp update thread */
static pthread_t ramp_thread;
static int ramp_thread_running = 0;

/* Force update thread for continuous updates */
static pthread_t force_update_thread;
static int force_update_thread_running = 0;

/* TEMPORARY: Disable ramp effects due to kernel crash bug */
#define ENABLE_RAMP_EFFECTS 0

/* USB hex debug logging - enable to see all USB packets */
#define USB_HEX_DEBUG 0

/* Gain control state */
static int current_gain = 0xffff;  /* Default: maximum (0-65535) */

/* Per-effect-type gains (custom event codes 0x70-0x75) */
#define FF_GAIN_CONSTANT 0x70
#define FF_GAIN_PERIODIC 0x71
#define FF_GAIN_SPRING   0x72
#define FF_GAIN_DAMPER   0x73
#define FF_GAIN_FRICTION 0x74
#define FF_GAIN_INERTIA  0x75

static int constant_gain = 0xffff;  /* Constant force gain */
static int periodic_gain = 0xffff;  /* Periodic force gain */
static int spring_gain = 0xffff;    /* Spring force gain */
static int damper_gain = 0xffff;    /* Damper force gain */
static int friction_gain = 0xffff;  /* Friction force gain */
static int inertia_gain = 0xffff;   /* Inertia force gain */

/* Autocenter state */
static int current_autocenter = 0;  /* Default: off (0-65535) */
#define AUTOCENTER_EFFECT_ID 15  /* Reserve slot 15 for autocenter spring */

/* Rotation angle state */
static int current_rotation_angle = 1080;  /* Default: 1080 degrees */
#define FF_ROTATION_ANGLE 0x76  /* Custom event code for rotation angle */

/* Forward declarations */
int apply_effect_gain(int value, int effect_type);
static int set_effect_type_gain(int effect_type, int gain);
static int set_rotation_angle(int angle);

#if USE_WINDOWS_PROTOCOL
/* Windows protocol effect upload - uses new translation layer */
static int upload_effect_windows_protocol(int id, struct ff_effect *effect);
#endif

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

/* USB communication - made global for protocol module */
int usb_send(unsigned char *data, int len)
{
    int ret, transferred;

#if USB_HEX_DEBUG
    /* Debug: print hex dump of outgoing packet */
    fprintf(stderr, "[USB OUT %2d] ", len);
    for (int i = 0; i < len; i++) {
        fprintf(stderr, "%02x", data[i]);
        if (i < len - 1 && (i + 1) % 4 == 0) {
            fprintf(stderr, " ");  /* Space every 4 bytes */
        }
    }
    fprintf(stderr, "\n");
#endif

    ret = libusb_interrupt_transfer(usb_handle, EP_OUT, data, len, &transferred, 1000);
    if (ret < 0) {
        /* Don't log NO_DEVICE errors during shutdown - these are expected */
        if (ret != LIBUSB_ERROR_NO_DEVICE && running) {
            LOG_ERROR("USB transfer failed: %s", libusb_error_name(ret));
        }
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
    unsigned char buf[16];
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
    usleep(10000);

    /* Additional commands from pcap to trigger mode switch */
    /* Report 0x42 - Query */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x42;
    buf[1] = 0x00;
    buf[2] = 0x00;
    buf[3] = 0x00;
    buf[4] = 0x00;
    buf[5] = 0x08;
    ret = usb_send(buf, 6);
    if (ret) return ret;
    usleep(10000);

    /* Report 0x42 - Set value */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x42;
    buf[1] = 0xe8;
    buf[2] = 0x03;
    ret = usb_send(buf, 3);
    if (ret) return ret;
    usleep(10000);

    /* Report 0x4e - Query */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x4e;
    buf[1] = 0x00;
    buf[2] = 0x00;
    buf[3] = 0x00;
    buf[4] = 0x00;
    buf[5] = 0x08;
    ret = usb_send(buf, 6);
    if (ret) return ret;
    usleep(10000);

    /* Report 0x4e - Set value */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x4e;
    buf[1] = 0x14;
    ret = usb_send(buf, 2);
    if (ret) return ret;
    usleep(10000);

    /* Report 0x56 - Query */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x56;
    buf[1] = 0x00;
    buf[2] = 0x00;
    buf[3] = 0x00;
    buf[4] = 0x00;
    buf[5] = 0x08;
    ret = usb_send(buf, 6);
    if (ret) return ret;
    usleep(10000);

    /* Report 0x56 - Set value */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x56;
    buf[1] = 0x00;
    buf[2] = 0x2f;
    ret = usb_send(buf, 3);
    if (ret) return ret;
    usleep(100000);

    /* NOTE: USB control transfers (request 73, 83) are NOT needed for mode switch
     * The Windows driver does NOT send these control transfers
     * Mode switch is triggered purely by the HID interrupt transfers above
     * The device will disconnect and reconnect automatically after receiving the init sequence */

    LOG_INFO("Initialization complete (mode switch commands sent)");
    return 0;
}

/* Send Report 0x02 continuous force update
 * Based on USB capture analysis:
 * - Bytes 3-4: SIGNED force value (little-endian, -1500 to +1500)
 * - Byte 5: Direction flag (0x5e or 0x3f) - testing shows this may not control direction
 * - Byte 8: Constant 0x21
 *
 * TESTING RESULT: Direction flag alone doesn't work - both pull same direction
 * HYPOTHESIS: Bytes 3-4 should be SIGNED value, not unsigned magnitude
 */
static int send_force_update(int force_level)
{
    unsigned char buf[9];
    int ret;

    /* Scale force to -1500 to +1500 range (from captures)
     * Keep the sign! */
    int scaled_force = (force_level * 1500) / 32767;

    /* Convert to little-endian signed 16-bit
     * Negative values will have high bit set */
    unsigned short force_word = (unsigned short)(short)scaled_force;

    /* Direction flag - try both values to see if it matters
     * Testing shows 0x5e vs 0x3f alone doesn't control direction */
    unsigned char direction = (force_level >= 0) ? 0x5e : 0x3f;

    memset(buf, 0, sizeof(buf));
    buf[0] = 0x02;
    buf[1] = 0x1c;
    buf[2] = 0x00;
    buf[3] = force_word & 0xff;        /* Force low byte (SIGNED) */
    buf[4] = (force_word >> 8) & 0xff; /* Force high byte (SIGNED) */
    buf[5] = direction;                 /* Direction flag */
    buf[6] = 0x00;
    buf[7] = 0x00;
    buf[8] = 0x21;                      /* Constant from captures */

    ret = usb_send(buf, 9);

    LOG_DEBUG("Force update: level=%d, scaled=%d (0x%04x), direction=0x%02x",
              force_level, scaled_force, force_word, direction);

    return ret;
}

/* Upload constant force effect */
static int upload_constant_effect(int id, struct ff_effect *effect)
{
    unsigned char buf[15];
    int ret;

    /* Apply per-effect gain */
    int level = apply_effect_gain(effect->u.constant.level, FF_CONSTANT);

    LOG_DEBUG("Uploading constant effect %d, force=%d (after gain: %d)",
              id, effect->u.constant.level, level);

    /* Report 0x02 - Envelope (attack/fade) - use defaults for now
     * NOTE: This is also used for continuous updates during playback
     * For upload, we send with zeros (envelope parameters) */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x02;
    buf[1] = 0x1c;
    buf[2] = 0x00;
    buf[3] = 0x00;  /* Attack length low */
    buf[4] = 0x00;  /* Attack length high */
    buf[5] = 0x00;  /* Attack level */
    buf[6] = 0x00;  /* Fade length low */
    buf[7] = 0x00;  /* Fade length high */
    buf[8] = 0x00;  /* Fade level */
    ret = usb_send(buf, 9);
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

    /* Get coefficients for both directions and apply per-effect gain */
    int right_coeff = apply_effect_gain(effect->u.condition[0].right_coeff, effect->type);
    int left_coeff = apply_effect_gain(effect->u.condition[0].left_coeff, effect->type);

    /* Scale to 0-100 (0x64) as seen in captures */
    unsigned char right_strength = (abs(right_coeff) * 100) / 32767;
    unsigned char left_strength = (abs(left_coeff) * 100) / 32767;

    /* If coefficients are 0 or very low, use default values */
    if (right_strength < 10) {
        right_strength = 50;  /* Default to medium strength */
        LOG_DEBUG("Right coefficient too low, using default strength");
    }
    if (left_strength < 10) {
        left_strength = 50;  /* Default to medium strength */
        LOG_DEBUG("Left coefficient too low, using default strength");
    }

    LOG_DEBUG("Uploading %s effect %d, right_coeff=%d (0x%02x), left_coeff=%d (0x%02x)",
              type_name, id, right_coeff, right_strength, left_coeff, left_strength);

    /* Report 0x05 - Condition parameters (two reports needed) */
    /* First report - 0x0e - Coefficients */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x05;
    buf[1] = 0x0e;
    buf[2] = 0x00;
    buf[3] = right_strength;  /* Right coefficient */
    buf[4] = left_strength;   /* Left coefficient */
    buf[5] = 0x00;
    buf[6] = 0x00;
    buf[7] = 0x00;
    buf[8] = 0x00;

    /* Saturation values - different for spring vs damper */
    if (effect->type == FF_SPRING) {
        buf[9] = 0x54;   /* Right saturation (from capture) */
        buf[10] = 0x54;  /* Left saturation */
    } else {
        buf[9] = 0x64;   /* Right saturation (damper/friction) */
        buf[10] = 0x64;  /* Left saturation */
    }
    ret = usb_send(buf, 11);
    if (ret) return ret;
    usleep(5000);

    /* Second report - 0x1c - Deadband and center */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x05;
    buf[1] = 0x1c;
    buf[2] = 0x00;
    buf[3] = 0x00;  /* Deadband */
    buf[4] = 0x00;  /* Center position */
    buf[5] = 0x00;
    buf[6] = 0x00;
    buf[7] = 0x00;
    buf[8] = 0x00;

    /* Saturation values for second report */
    if (effect->type == FF_SPRING) {
        buf[9] = 0x46;   /* Right saturation (from capture) */
        buf[10] = 0x54;  /* Left saturation */
    } else {
        buf[9] = 0x64;   /* Right saturation (damper/friction) */
        buf[10] = 0x64;  /* Left saturation */
    }
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

    LOG_DEBUG("%s effect uploaded: right=0x%02x, left=0x%02x",
              type_name, right_strength, left_strength);

    return 0;
}

/* Upload periodic effect (sine, square, triangle, sawtooth) */
static int upload_periodic_effect(int id, struct ff_effect *effect)
{
    unsigned char buf[15];
    int ret;
    unsigned char effect_type;
    const char *type_name;

    /* Apply per-effect gain to magnitude */
    int magnitude = apply_effect_gain(effect->u.periodic.magnitude, FF_PERIODIC);

    /* Determine waveform type */
    switch (effect->u.periodic.waveform) {
    case FF_SQUARE:
        effect_type = 0x20;
        type_name = "square";
        break;
    case FF_TRIANGLE:
        effect_type = 0x21;
        type_name = "triangle";
        break;
    case FF_SINE:
        effect_type = 0x22;
        type_name = "sine";
        break;
    case FF_SAW_UP:
        effect_type = 0x23;
        type_name = "sawtooth_up";
        break;
    case FF_SAW_DOWN:
        effect_type = 0x24;
        type_name = "sawtooth_down";
        break;
    default:
        LOG_ERROR("Unknown periodic waveform: %d", effect->u.periodic.waveform);
        return -1;
    }

    /* Magnitude - scale to 0-127 (already has gain applied) */
    unsigned char mag = (abs(magnitude) * 127) / 32767;

    /* Ensure minimum magnitude */
    if (mag < 20) {
        mag = 50;  /* Default to medium if too low */
    }

    /* Period (frequency) - Linux uses milliseconds, convert appropriately */
    /* From capture: 0x03e8 = 1000ms = 1 Hz */
    unsigned short period = effect->u.periodic.period;
    if (period == 0) {
        period = 100;  /* Default to 100ms = 10 Hz */
    }

    LOG_DEBUG("Uploading %s effect %d, magnitude=%d (0x%02x), period=%dms",
              type_name, id, magnitude, mag, period);

    /* Report 0x02 - Envelope */
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

    /* Report 0x04 - Periodic parameters */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x04;
    buf[1] = 0x0e;
    buf[2] = 0x00;
    buf[3] = mag;  /* Magnitude */
    buf[4] = 0x00;  /* Offset */
    buf[5] = 0x00;  /* Phase */
    buf[6] = period & 0xff;  /* Period low byte */
    buf[7] = (period >> 8) & 0xff;  /* Period high byte */
    ret = usb_send(buf, 8);
    if (ret) return ret;
    usleep(5000);

    /* Report 0x01 - Main effect upload */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x01;
    buf[1] = id;
    buf[2] = effect_type;  /* Waveform type */
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

    LOG_DEBUG("%s effect uploaded: magnitude=0x%02x, period=%d", type_name, mag, period);

    return 0;
}

/* Upload ramp effect */
static int upload_ramp_effect(int id, struct ff_effect *effect)
{
    unsigned char buf[15];
    int ret;

    /* Start and end levels */
    int start_level = effect->u.ramp.start_level;
    int end_level = effect->u.ramp.end_level;

    LOG_DEBUG("Uploading ramp effect %d, start=%d, end=%d",
              id, start_level, end_level);

    /* Report 0x02 - Envelope */
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

    /* Report 0x04 - Ramp parameters */
    /* NOTE: T500RS doesn't support native ramp - needs continuous updates */
    /* For now, just set the start level (Option 1 - simple implementation) */
    unsigned short duration_ms = effect->replay.length;
    unsigned short start_scaled = (abs(start_level) * 0x00ff) / 32767;  /* Scale to 0-255 */

    memset(buf, 0, sizeof(buf));
    buf[0] = 0x04;
    buf[1] = 0x0e;
    buf[2] = start_scaled & 0xff;        /* Start level low byte */
    buf[3] = (start_scaled >> 8) & 0xff; /* Start level high byte */
    buf[4] = start_scaled & 0xff;        /* Current level (same as start) */
    buf[5] = (start_scaled >> 8) & 0xff; /* Current level high byte */
    buf[6] = duration_ms & 0xff;         /* Duration low byte */
    buf[7] = (duration_ms >> 8) & 0xff;  /* Duration high byte */
    buf[8] = 0x00;
    ret = usb_send(buf, 9);
    if (ret) return ret;
    usleep(5000);

    LOG_DEBUG("Ramp Report 0x04: start=0x%04x, duration=%dms (simple mode - no gradual ramp)",
              start_scaled, duration_ms);

    /* Report 0x01 - Main effect upload */
    /* Simple implementation - just upload as ramp type */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x01;
    buf[1] = id;
    buf[2] = 0x24;  /* Ramp type (0x24 = sawtooth down / ramp) */
    buf[3] = 0x40;
    buf[4] = duration_ms & 0xff;         /* Duration low byte */
    buf[5] = (duration_ms >> 8) & 0xff;  /* Duration high byte */
    buf[6] = 0x00;  /* Will be set by Report 0x04 */
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

    LOG_DEBUG("Ramp effect uploaded (simple mode - holds start level)");

    return 0;
}

/* Start effect */
static int start_effect(int id)
{
    unsigned char buf[4];
    int is_constant = 0;
    int is_periodic = 0;
    int is_ramp = 0;
    int force = 0;
    int magnitude = 0;
    int ramp_start = 0;
    int ret;

    LOG_DEBUG("Starting effect %d", id);

    /* Check effect type (mutex already locked by caller) */
    if (id >= 0 && id < MAX_EFFECTS) {
        if (effects[id].effect.type == FF_CONSTANT) {
            is_constant = 1;
            force = effects[id].effect.u.constant.level;

            /* Initialize constant force state for continuous updates */
            effects[id].is_constant = 1;
            effects[id].current_force_level = force;
            clock_gettime(CLOCK_MONOTONIC, &effects[id].start_time);

            LOG_DEBUG("Constant force initialized: level=%d", force);
        } else if (effects[id].effect.type == FF_PERIODIC) {
            is_periodic = 1;
            magnitude = effects[id].effect.u.periodic.magnitude;
        } else if (effects[id].effect.type == FF_RAMP) {
            is_ramp = 1;
            ramp_start = effects[id].effect.u.ramp.start_level;
        }
    }

    /* For constant force, set the level using Report 0x03
     * CORRECTED: Manual analysis shows Report 0x03 IS used!
     * Format: 03 0e 00 [level]
     * - Positive values (0x01-0x7F) = left pull
     * - Negative values (0x80-0xFF) = right pull
     * - 0xFF = neutral/middle
     *
     * Examples from manual analysis:
     *   03 0e 00 01 = left small
     *   03 0e 00 29 = left bigger
     *   03 0e 00 cc = right bigger (-52 signed)
     *   03 0e 00 ff = middle (-1 signed)
     */
    if (is_constant) {
        /* Map force (-32767 to +32767) to signed byte (-128 to +127)
         * Then cast to unsigned to preserve bit pattern */
        signed char signed_level = (signed char)((force * 127) / 32767);
        unsigned char level = (unsigned char)signed_level;

        /* Report 0x03 - Set force level (SIGNED!) */
        buf[0] = 0x03;
        buf[1] = 0x0e;
        buf[2] = 0x00;
        buf[3] = level;  /* Signed value */
        ret = usb_send(buf, 4);
        if (ret) return ret;
        usleep(5000);

        LOG_DEBUG("Set constant force level to 0x%02x (force=%d, signed_level=%d)",
                  level, force, signed_level);
    }

    /* For periodic effects, magnitude is set via Report 0x04 during upload
     * No need to send anything here */

    /* Ramp effects don't need Report 0x03 - the ramp is in Report 0x04 */

    /* Send start command
     * 
     * NEW UNDERSTANDING from Windows capture analysis:
     *   - buf[2]=0x41 is used for START (regardless of direction)
     *   - buf[2]=0x00 is used for STOP (in stop_effect())
     *   - Direction is encoded in Report 0x03 buf[3] as SIGNED value!
     */
    buf[0] = 0x41;
    buf[1] = id;
    buf[2] = 0x41;  /* Always 0x41 for START */
    buf[3] = 0x01;  /* Action: 01 = start */

    LOG_DEBUG("Starting effect (buf[2]=0x41, force=%d)", force);

    return usb_send(buf, 4);
}

/* Stop effect */
static int stop_effect(int id)
{
    unsigned char buf[4];

    LOG_DEBUG("Stopping effect %d", id);

    /* Clear constant force state (mutex already locked by caller) */
    if (id >= 0 && id < MAX_EFFECTS) {
        effects[id].is_constant = 0;
        effects[id].current_force_level = 0;
    }

    buf[0] = 0x41;
    buf[1] = id;
    buf[2] = 0x00;
    buf[3] = 0x01;

    return usb_send(buf, 4);
}

/* Apply per-effect gain to a value */
int apply_effect_gain(int value, int effect_type)
{
    int gain = 0xffff;

    switch (effect_type) {
        case FF_CONSTANT:
            gain = constant_gain;
            break;
        case FF_PERIODIC:
        case FF_SINE:
        case FF_SQUARE:
        case FF_TRIANGLE:
        case FF_SAW_UP:
        case FF_SAW_DOWN:
            gain = periodic_gain;
            break;
        case FF_SPRING:
            gain = spring_gain;
            break;
        case FF_DAMPER:
            gain = damper_gain;
            break;
        case FF_FRICTION:
            gain = friction_gain;
            break;
        case FF_INERTIA:
            gain = inertia_gain;
            break;
        default:
            gain = 0xffff;
    }

    /* Apply gain: value * gain / 65535 */
    if (gain != 0xffff) {
        long long scaled = ((long long)value * gain) / 65535;
        return (int)scaled;
    }

    return value;
}

/* Set gain (overall force feedback strength) */
static int set_gain(int gain)
{
    unsigned char buf[2];
    int ret;

    /* Gain is 0-65535, scale to 0-255 */
    unsigned char scaled_gain = (gain * 255) / 65535;

    LOG_INFO("Setting gain: %d%% (raw=%d, scaled=0x%02x)",
             (gain * 100) / 65535, gain, scaled_gain);

    /* Report 0x43 - Set gain (only 2 bytes based on Windows capture) */
    buf[0] = 0x43;
    buf[1] = scaled_gain;

    ret = usb_send(buf, 2);
    if (ret == 0) {
        current_gain = gain;
        LOG_INFO("✅ Gain set successfully");
    } else {
        LOG_ERROR("❌ Failed to set gain");
    }

    return ret;
}

/* Set per-effect-type gain */
static int set_effect_type_gain(int effect_type, int gain)
{
    const char *type_name = "Unknown";

    switch (effect_type) {
        case FF_CONSTANT:
            constant_gain = gain;
            type_name = "Constant";
            break;
        case FF_PERIODIC:
            periodic_gain = gain;
            type_name = "Periodic";
            break;
        case FF_SPRING:
            spring_gain = gain;
            type_name = "Spring";
            break;
        case FF_DAMPER:
            damper_gain = gain;
            type_name = "Damper";
            break;
        case FF_FRICTION:
            friction_gain = gain;
            type_name = "Friction";
            break;
        case FF_INERTIA:
            inertia_gain = gain;
            type_name = "Inertia";
            break;
        default:
            LOG_ERROR("Unknown effect type for gain: %d", effect_type);
            return -1;
    }

    LOG_INFO("Set %s gain: %d%% (raw=%d)", type_name, (gain * 100) / 65535, gain);
    return 0;
}

#if USE_WINDOWS_PROTOCOL
/* Windows-compatible range setting - uses Ghidra reverse engineering findings */
static int set_rotation_angle_windows_protocol(int angle)
{
    LOG_INFO("Setting range to %d° using Windows-compatible protocol", angle);
    return t500rs_set_range_windows_compatible(angle);
}
#endif

/* Set rotation angle (steering range) - Enhanced with Windows protocol support */
static int set_rotation_angle(int angle)
{
#if USE_WINDOWS_PROTOCOL
    /* Try Windows protocol first if available */
    struct t500rs_device_state *state = t500rs_get_device_state();
    if (state && state->protocol_active) {
        LOG_INFO("Using Windows-compatible range setting");
        return set_rotation_angle_windows_protocol(angle);
    }
    LOG_INFO("Windows protocol not active, falling back to legacy method");
#endif

    unsigned char buf[15];
    int ret;
    unsigned char angle_code;
    int actual_angle;

    /* T500RS only supports these discrete angles - round to nearest */
    /* Mapping from Windows capture: 90°=0x01, 180°=0x02, 360°=0x03, 500°=0x04, 900°=0x05, 1080°=0x06 */

    if (angle <= 135) {
        angle_code = 0x01;
        actual_angle = 90;
    } else if (angle <= 270) {
        angle_code = 0x02;
        actual_angle = 180;
    } else if (angle <= 430) {
        angle_code = 0x03;
        actual_angle = 360;
    } else if (angle <= 700) {
        angle_code = 0x04;
        actual_angle = 500;
    } else if (angle <= 990) {
        angle_code = 0x05;
        actual_angle = 900;
    } else {
        angle_code = 0x06;
        actual_angle = 1080;
    }

    LOG_INFO("🔄 Setting rotation angle: requested=%d°, actual=%d° (code=0x%02x)",
             angle, actual_angle, angle_code);

    /* Report 0x42 - Set rotation angle */
    buf[0] = 0x42;
    buf[1] = angle_code;

    ret = usb_send(buf, 2);
    if (ret != 0) {
        LOG_ERROR("❌ Failed to send Report 0x42");
        return ret;
    }

    usleep(10000);  /* Wait 10ms */

    /* Report 0x40 - Follow-up command (from Windows capture) */
    buf[0] = 0x40;
    buf[1] = angle_code;
    buf[2] = 0x00;
    buf[3] = 0x00;

    ret = usb_send(buf, 4);
    if (ret != 0) {
        LOG_ERROR("❌ Failed to send Report 0x40 (first)");
        return ret;
    }

    usleep(8000);  /* Wait 8ms */

    /* Report 0x40 - Second follow-up (from Windows capture frame 206) */
    buf[0] = 0x40;
    buf[1] = 0x03;
    buf[2] = 0x0d;
    buf[3] = 0x00;

    ret = usb_send(buf, 4);
    if (ret != 0) {
        LOG_ERROR("❌ Failed to send Report 0x40 (second)");
        return ret;
    }

    current_rotation_angle = actual_angle;
    LOG_INFO("✅ Rotation angle set successfully to %d°", actual_angle);

    return 0;
}

/* Set autocenter (self-centering force when no effects playing) */
static int set_autocenter(int autocenter)
{
    unsigned char buf[64];
    int ret;

    LOG_INFO("Autocenter requested: %d%% (raw=%d)",
             (autocenter * 100) / 65535, autocenter);

    /* Autocenter is implemented as a spring effect on T500RS */
    /* Scale autocenter (0-65535) to spring coefficient (0-32767) */
    int spring_coefficient = (autocenter * 32767) / 65535;

    if (autocenter == 0) {
        /* Stop and remove autocenter spring effect */
        LOG_INFO("Disabling autocenter (stopping spring effect)");

        /* Stop effect */
        buf[0] = 0x41;
        buf[1] = AUTOCENTER_EFFECT_ID;
        buf[2] = 0x00;
        buf[3] = 0x01;
        usb_send(buf, 4);

        /* Mark as inactive */
        pthread_mutex_lock(&effects_lock);
        effects[AUTOCENTER_EFFECT_ID].active = 0;
        pthread_mutex_unlock(&effects_lock);

        current_autocenter = 0;
        LOG_INFO("✅ Autocenter disabled");
        return 0;
    }

    /* Upload spring effect for autocenter */
    /* Use EXACT same format as working spring effects */

    /* Scale to 0-100 like working spring effects */
    unsigned char strength = (abs(spring_coefficient) * 100) / 32767;
    if (strength < 10) strength = 10;  /* Minimum strength */

    LOG_INFO("Enabling autocenter with strength=%d (0x%02x)", strength, strength);

    /* Report 0x05 - First report (0x0e) - Coefficients */
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
    buf[9] = 0x54;   /* Right saturation (from working spring) */
    buf[10] = 0x54;  /* Left saturation */
    ret = usb_send(buf, 11);  /* Only 11 bytes! */
    if (ret) return ret;
    usleep(5000);

    /* Report 0x05 - Second report (0x1c) - Deadband and center */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x05;
    buf[1] = 0x1c;
    buf[2] = 0x00;
    buf[3] = 0x00;  /* Deadband */
    buf[4] = 0x00;  /* Center position */
    buf[5] = 0x00;
    buf[6] = 0x00;
    buf[7] = 0x00;
    buf[8] = 0x00;
    buf[9] = 0x46;   /* Right saturation (from working spring) */
    buf[10] = 0x54;  /* Left saturation */
    ret = usb_send(buf, 11);  /* Only 11 bytes! */
    if (ret) return ret;
    usleep(5000);

    /* Report 0x01 - Effect upload (EXACT format from working spring) */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x01;
    buf[1] = AUTOCENTER_EFFECT_ID;
    buf[2] = 0x40;  /* Spring type (0x40, not 0x26!) */
    buf[3] = 0x40;
    buf[4] = 0x17;  /* From working spring */
    buf[5] = 0x25;  /* From working spring */
    buf[6] = 0x00;
    buf[7] = 0xff;  /* Infinite duration */
    buf[8] = 0xff;
    buf[9] = 0x0e;
    buf[10] = 0x00;
    buf[11] = 0x1c;
    buf[12] = 0x00;
    buf[13] = 0x00;
    buf[14] = 0x00;
    ret = usb_send(buf, 15);
    if (ret) return ret;
    usleep(5000);

    /* Start effect */
    buf[0] = 0x41;
    buf[1] = AUTOCENTER_EFFECT_ID;
    buf[2] = 0x41;
    buf[3] = 0x01;
    ret = usb_send(buf, 4);
    if (ret) return ret;

    /* Mark as active */
    pthread_mutex_lock(&effects_lock);
    effects[AUTOCENTER_EFFECT_ID].active = 1;
    effects[AUTOCENTER_EFFECT_ID].effect.type = FF_SPRING;
    pthread_mutex_unlock(&effects_lock);

    current_autocenter = autocenter;
    LOG_INFO("✅ Autocenter enabled as spring effect");

    return 0;
}

/* Send ramp level update (Report 0x04) */
static int send_ramp_update(int id, unsigned short level, unsigned short duration_ms)
{
    unsigned char buf[9];

    buf[0] = 0x04;
    buf[1] = 0x0e;
    buf[2] = level & 0xff;
    buf[3] = (level >> 8) & 0xff;
    buf[4] = level & 0xff;
    buf[5] = (level >> 8) & 0xff;
    buf[6] = duration_ms & 0xff;
    buf[7] = (duration_ms >> 8) & 0xff;
    buf[8] = 0x00;

    return usb_send(buf, 9);
}

/* Force update thread - continuously updates constant force effects
 * Sends Report 0x03 periodically to maintain force level
 * This ensures smooth force feedback even if the application doesn't send updates
 */
static void *force_update_thread_func(void *arg)
{
    unsigned char buf[4];

    LOG_DEBUG("Force update thread started");

    while (force_update_thread_running) {
        /* Check if we should continue */
        if (!force_update_thread_running) break;

        /* Try to lock with timeout to avoid deadlock */
        if (pthread_mutex_trylock(&effects_lock) != 0) {
            usleep(10000);  /* Wait 10ms and try again */
            continue;
        }

        /* Check USB handle is valid */
        if (!usb_handle) {
            pthread_mutex_unlock(&effects_lock);
            break;
        }

        /* Update all active constant force effects */
        for (int i = 0; i < MAX_EFFECTS; i++) {
            if (!effects[i].active || !effects[i].is_constant) {
                continue;
            }

            /* Get current force level */
            int force = effects[i].current_force_level;

            /* Apply global gain */
            force = (force * current_gain) / 65535;

            /* Apply per-effect-type gain */
            force = apply_effect_gain(force, FF_CONSTANT);

            /* Convert to signed byte */
            signed char signed_level = (signed char)((force * 127) / 32767);
            unsigned char level = (unsigned char)signed_level;

            /* Send Report 0x03 - Force level update */
            buf[0] = 0x03;
            buf[1] = 0x0e;
            buf[2] = 0x00;
            buf[3] = level;

            /* Send without holding lock for too long */
            pthread_mutex_unlock(&effects_lock);
            usb_send(buf, 4);
            pthread_mutex_lock(&effects_lock);

            /* Check if we should still continue after sending */
            if (!force_update_thread_running || !usb_handle) {
                pthread_mutex_unlock(&effects_lock);
                goto thread_exit;
            }
        }

        pthread_mutex_unlock(&effects_lock);

        /* Update every 20ms (50Hz) for smooth force feedback
         * This matches typical game update rates */
        usleep(20000);
    }

thread_exit:
    LOG_DEBUG("Force update thread stopped");
    return NULL;
}

/* Ramp update thread - continuously updates ramp effects */
static void *ramp_update_thread_func(void *arg)
{
    LOG_DEBUG("Ramp update thread started");

    while (ramp_thread_running) {
        /* Check if we should continue */
        if (!ramp_thread_running) break;

        /* Try to lock with timeout to avoid deadlock */
        if (pthread_mutex_trylock(&effects_lock) != 0) {
            usleep(10000);  /* Wait 10ms and try again */
            continue;
        }

        /* Check USB handle is valid */
        if (!usb_handle) {
            pthread_mutex_unlock(&effects_lock);
            break;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        for (int i = 0; i < MAX_EFFECTS; i++) {
            /* Safety check */
            if (!ramp_thread_running) break;

            if (effects[i].active && effects[i].is_ramp) {
                /* Validate duration to avoid division by zero */
                if (effects[i].ramp_duration_ms == 0) {
                    LOG_ERROR("Ramp effect %d has zero duration, stopping", i);
                    effects[i].active = 0;
                    effects[i].is_ramp = 0;
                    continue;
                }

                /* Calculate elapsed time in milliseconds */
                unsigned long elapsed_ms =
                    (now.tv_sec - effects[i].ramp_start_time.tv_sec) * 1000 +
                    (now.tv_nsec - effects[i].ramp_start_time.tv_nsec) / 1000000;

                /* Calculate progress (0.0 to 1.0) */
                float progress = (float)elapsed_ms / effects[i].ramp_duration_ms;
                if (progress >= 1.0f) {
                    progress = 1.0f;
                    /* Ramp complete - deactivate */
                    effects[i].active = 0;
                    effects[i].is_ramp = 0;
                    stop_effect(i);
                    LOG_DEBUG("Ramp effect %d complete", i);
                    continue;
                }

                /* Calculate current level */
                int start = effects[i].ramp_start_level;
                int end = effects[i].ramp_end_level;
                int current_level = start + (int)((end - start) * progress);

                /* Scale to 0-255 range */
                unsigned short scaled_level = (abs(current_level) * 0x00ff) / 32767;

                /* Clamp to valid range */
                if (scaled_level > 0xff) scaled_level = 0xff;

                /* Send update - check return value */
                int ret = send_ramp_update(i, scaled_level, effects[i].ramp_duration_ms);
                if (ret != 0) {
                    LOG_ERROR("Failed to send ramp update for effect %d, stopping", i);
                    effects[i].active = 0;
                    effects[i].is_ramp = 0;
                    continue;
                }

                LOG_DEBUG("Ramp effect %d: progress=%.2f%%, level=%d (0x%04x)",
                          i, progress * 100, current_level, scaled_level);
            }
        }

        pthread_mutex_unlock(&effects_lock);

        /* Update every 100ms for smooth ramp */
        usleep(100000);
    }

    LOG_DEBUG("Ramp update thread stopped");
    return NULL;
}




#if USE_WINDOWS_PROTOCOL
/* Upload effect using Windows-compatible protocol */
static int upload_effect_windows_protocol(int id, struct ff_effect *effect)
{
    struct t500rs_hid_output cmd;
    int ret;

    /* Use the new translation layer to convert Linux FF effect to Windows HID command */
    ret = t500rs_translate_effect(effect, &cmd, 1);  /* 1 = apply per-effect gains */
    if (ret != 0) {
        LOG_ERROR("Failed to translate effect %d: %d", id, ret);
        return ret;
    }

    /* Send the Windows-compatible HID command */
    LOG_DEBUG("Sending Windows protocol command: type=0x%02x, param=0x%04x, flags=0x%02x",
              cmd.command_type, cmd.parameter, cmd.flags);
    
    ret = usb_send((unsigned char *)&cmd, sizeof(cmd));
    if (ret != 0) {
        LOG_ERROR("Failed to send Windows protocol command for effect %d: %d", id, ret);
        return ret;
    }

    LOG_INFO("Effect %d uploaded using Windows protocol", id);
    return 0;
}

/* Start effect using Windows protocol - resend the effect command */
static int start_effect_windows_protocol(int id)
{
    struct t500rs_hid_output cmd;
    int ret;

    if (id < 0 || id >= MAX_EFFECTS) {
        LOG_ERROR("Invalid effect ID for start: %d", id);
        return -EINVAL;
    }

    if (!effects[id].effect.type) {
        LOG_ERROR("Effect %d not uploaded", id);
        return -EINVAL;
    }

    /* Translate effect again to get the command with actual parameters */
    ret = t500rs_translate_effect(&effects[id].effect, &cmd, 1);
    if (ret != 0) {
        LOG_ERROR("Failed to translate effect %d for start: %d", id, ret);
        return ret;
    }

    /* Send the command - this activates the effect */
    LOG_DEBUG("Starting effect %d: type=0x%02x, param=0x%04x, flags=0x%02x",
              id, cmd.command_type, cmd.parameter, cmd.flags);
    
    ret = usb_send((unsigned char *)&cmd, sizeof(cmd));
    if (ret != 0) {
        LOG_ERROR("Failed to start effect %d: %d", id, ret);
        return ret;
    }

    LOG_INFO("Effect %d started using Windows protocol", id);
    return 0;
}

/* Stop effect using Windows protocol - send command with zero magnitude */
static int stop_effect_windows_protocol(int id)
{
    struct t500rs_hid_output cmd;
    int ret;

    if (id < 0 || id >= MAX_EFFECTS) {
        LOG_ERROR("Invalid effect ID for stop: %d", id);
        return -EINVAL;
    }

    /* Construct a zero-magnitude stop command based on effect type */
    memset(&cmd, 0, sizeof(cmd));
    cmd.report_id = T500RS_REPORT_ID;
    
    /* Use command type from the stored effect */
    switch (effects[id].effect.type) {
    case FF_CONSTANT:
        cmd.command_type = T500RS_CMD_FF_PRIMARY;
        cmd.flags = 0x00;
        cmd.parameter = 0;  /* Zero magnitude = stop */
        break;
        
    case FF_PERIODIC:
        cmd.command_type = T500RS_CMD_FF_EXTENDED;
        cmd.flags = 0x00;
        cmd.parameter = 0;  /* Zero magnitude = stop */
        break;
        
    case FF_SPRING:
    case FF_DAMPER:
    case FF_FRICTION:
    case FF_INERTIA:
        cmd.command_type = T500RS_CMD_FF_SECONDARY;
        /* For conditional effects, use the appropriate flag but zero parameter */
        if (effects[id].effect.type == FF_SPRING)
            cmd.flags = 0x01;
        else if (effects[id].effect.type == FF_DAMPER)
            cmd.flags = 0x02;
        else if (effects[id].effect.type == FF_FRICTION)
            cmd.flags = 0x03;
        else if (effects[id].effect.type == FF_INERTIA)
            cmd.flags = 0x04;
        cmd.parameter = 0;  /* Zero coefficient = stop */
        break;
        
    default:
        LOG_ERROR("Unknown effect type %d for stop", effects[id].effect.type);
        return -EINVAL;
    }

    /* Send the zero-magnitude command */
    LOG_DEBUG("Stopping effect %d: type=0x%02x, param=0x%04x, flags=0x%02x",
              id, cmd.command_type, cmd.parameter, cmd.flags);
    
    ret = usb_send((unsigned char *)&cmd, sizeof(cmd));
    if (ret != 0) {
        LOG_ERROR("Failed to stop effect %d: %d", id, ret);
        return ret;
    }

    LOG_INFO("Effect %d stopped using Windows protocol", id);
    return 0;
}
#endif

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

#if USE_WINDOWS_PROTOCOL
    /* Use Windows-compatible protocol */
    ret = upload_effect_windows_protocol(id, &upload->effect);
#else
    /* Use legacy protocol - Upload to device */
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
        LOG_ERROR("Ramp effects temporarily disabled due to kernel crash bug");
        ret = -ENOSYS;
#endif
        break;
    case FF_RUMBLE:
        /* Rumble is like a periodic effect - convert to constant force */
        LOG_DEBUG("Rumble effect - converting to constant force");
        /* Use strong motor value as force level */
        upload->effect.type = FF_CONSTANT;
        upload->effect.u.constant.level = upload->effect.u.rumble.strong_magnitude / 2;
        ret = upload_constant_effect(id, &upload->effect);
        break;
    default:
        LOG_ERROR("Unsupported effect type: %d", upload->effect.type);
        ret = -EINVAL;
        break;
    }
#endif

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
#if USE_WINDOWS_PROTOCOL
        stop_effect_windows_protocol(id);
#else
        stop_effect(id);
#endif
        effects[id].active = 0;
    }

    erase->retval = 0;
    pthread_mutex_unlock(&effects_lock);

    return 0;
}

/* Input reading thread - reads from USB and forwards to uinput */
static void *input_reading_thread(void *arg)
{
    unsigned char buf[64];
    int transferred;
    int ret;
    struct input_event ev;
    int disconnect_count = 0;
    int error_count = 0;

    while (running) {
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
            /* Successful read - reset disconnect counter */
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

        /* Parse HID input report
         * T500RS actual format (from USB captures):
         * Byte 0: Report ID (0x07)
         * Bytes 1-2: Steering (16-bit little-endian)
         * Bytes 3-4: Throttle (16-bit little-endian, 0-1023)
         * Bytes 5-6: Brake (16-bit little-endian, 0-1023)
         * Bytes 7-8: Clutch (16-bit little-endian, 0-1023)
         * Bytes 9-10: Unknown
         * Byte 11: Buttons (bit-packed)
         * Bytes 12-14: More buttons/data
         */

        if (buf[0] == 0x07 && transferred >= 15) {
            /* Steering - bytes 1-2 (little-endian 16-bit) */
            uint16_t steering_raw = buf[1] | (buf[2] << 8);
            /* Convert to signed -32768 to 32767 */
            int16_t steering = (int16_t)steering_raw - 32768;

            memset(&ev, 0, sizeof(ev));
            gettimeofday((struct timeval *)&ev.time, NULL);
            ev.type = EV_ABS;
            ev.code = ABS_X;
            ev.value = steering;
            write(uinput_fd, &ev, sizeof(ev));

            /* Throttle - bytes 3-4 (little-endian 16-bit, 0-1023) */
            uint16_t throttle = buf[3] | (buf[4] << 8);
            int throttle_scaled = (throttle * 255) / 1023;
            if (invert_throttle) {
                throttle_scaled = 255 - throttle_scaled;
            }
            ev.code = ABS_Y;
            ev.value = throttle_scaled;
            write(uinput_fd, &ev, sizeof(ev));

            /* Brake - bytes 5-6 (little-endian 16-bit, 0-1023) */
            uint16_t brake = buf[5] | (buf[6] << 8);
            int brake_scaled = (brake * 255) / 1023;
            if (invert_brake) {
                brake_scaled = 255 - brake_scaled;
            }
            ev.code = ABS_Z;
            ev.value = brake_scaled;
            write(uinput_fd, &ev, sizeof(ev));

            /* Clutch - bytes 7-8 (little-endian 16-bit, 0-1023) */
            uint16_t clutch = buf[7] | (buf[8] << 8);
            int clutch_scaled = (clutch * 255) / 1023;
            if (invert_clutch) {
                clutch_scaled = 255 - clutch_scaled;
            }
            ev.code = ABS_RZ;
            ev.value = clutch_scaled;
            write(uinput_fd, &ev, sizeof(ev));

            /* D-pad is in byte 14 (last byte)
             * T500RS D-pad encoding (verified from real hardware):
             * 0x0F = centered (released)
             * 0x00 = up (swapped with 0x01)
             * 0x01 = up-right (diagonal) (swapped with 0x00)
             * 0x02 = right
             * 0x03 = down-right (diagonal)
             * 0x04 = down
             * 0x05 = down-left (diagonal)
             * 0x06 = left
             * 0x07 = up-left (diagonal)
             */
            uint8_t dpad = buf[14];
            int hat_x = 0, hat_y = 0;

            switch (dpad) {
                case 0x0F:
                    /* Centered */
                    hat_x = 0;
                    hat_y = 0;
                    break;
                case 0x00:
                    /* Up */
                    hat_y = -1;
                    break;
                case 0x01:
                    /* Up-Right */
                    hat_x = 1;
                    hat_y = -1;
                    break;
                case 0x02:
                    /* Right */
                    hat_x = 1;
                    break;
                case 0x03:
                    /* Down-Right */
                    hat_x = 1;
                    hat_y = 1;
                    break;
                case 0x04:
                    /* Down */
                    hat_y = 1;
                    break;
                case 0x05:
                    /* Down-Left */
                    hat_x = -1;
                    hat_y = 1;
                    break;
                case 0x06:
                    /* Left */
                    hat_x = -1;
                    break;
                case 0x07:
                    /* Up-Left */
                    hat_x = -1;
                    hat_y = -1;
                    break;
                default:
                    /* Unknown value, treat as centered */
                    hat_x = 0;
                    hat_y = 0;
                    break;
            }

            ev.code = ABS_HAT0X;
            ev.value = hat_x;
            write(uinput_fd, &ev, sizeof(ev));

            ev.code = ABS_HAT0Y;
            ev.value = hat_y;
            write(uinput_fd, &ev, sizeof(ev));

            /* Buttons - byte 11 and possibly 12-14 */
            static uint32_t last_buttons = 0;
            uint32_t buttons = buf[11] | (buf[12] << 8) | (buf[13] << 16);

            /* Only send events for changed buttons */
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
    }

    LOG_INFO("Input reading thread stopped");
    return NULL;
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

            /* Handle special codes */
            if (ev.code == FF_GAIN) {
                /* Gain control - scale is 0-65535 */
                LOG_INFO("Gain control: %d (%.1f%%)", ev.value, (ev.value * 100.0) / 65535);
                set_gain(ev.value);
                break;
            }

            if (ev.code == FF_AUTOCENTER) {
                /* Autocenter control */
                LOG_INFO("Autocenter control: %d (%.1f%%)", ev.value, (ev.value * 100.0) / 65535);
                set_autocenter(ev.value);
                break;
            }

            /* Per-effect-type gains */
            if (ev.code == FF_GAIN_CONSTANT) {
                set_effect_type_gain(FF_CONSTANT, ev.value);
                break;
            }
            if (ev.code == FF_GAIN_PERIODIC) {
                set_effect_type_gain(FF_PERIODIC, ev.value);
                break;
            }
            if (ev.code == FF_GAIN_SPRING) {
                set_effect_type_gain(FF_SPRING, ev.value);
                break;
            }
            if (ev.code == FF_GAIN_DAMPER) {
                set_effect_type_gain(FF_DAMPER, ev.value);
                break;
            }
            if (ev.code == FF_GAIN_FRICTION) {
                set_effect_type_gain(FF_FRICTION, ev.value);
                break;
            }
            if (ev.code == FF_GAIN_INERTIA) {
                set_effect_type_gain(FF_INERTIA, ev.value);
                break;
            }

            /* Rotation angle */
            if (ev.code == FF_ROTATION_ANGLE) {
                /* Value is in degrees (90-1080) */
                set_rotation_angle(ev.value);
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
                        clock_gettime(CLOCK_MONOTONIC, &effects[ev.code].ramp_start_time);
                        LOG_DEBUG("Ramp effect initialized: start=%d, end=%d, duration=%lums",
                                  effects[ev.code].ramp_start_level,
                                  effects[ev.code].ramp_end_level,
                                  effects[ev.code].ramp_duration_ms);
                    }

#if USE_WINDOWS_PROTOCOL
                    start_effect_windows_protocol(ev.code);
#else
                    start_effect(ev.code);
#endif
                    effects[ev.code].active = 1;
                } else {
                    LOG_DEBUG("Stopping effect %d", ev.code);
#if USE_WINDOWS_PROTOCOL
                    stop_effect_windows_protocol(ev.code);
#else
                    stop_effect(ev.code);
#endif
                    effects[ev.code].active = 0;
                    effects[ev.code].is_ramp = 0;
                    effects[ev.code].is_constant = 0;
                }
            } else {
                LOG_DEBUG("Invalid effect code: %d (max is %d)", ev.code, MAX_EFFECTS - 1);
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
    return 0;
}

/* Cleanup */
static void cleanup(void)
{
    LOG_INFO("Cleaning up...");

    /* Stop input reading thread (only if it was created) */
    running = 0;
    if (input_thread) {
        pthread_join(input_thread, NULL);
        input_thread = 0;
    }

    /* Stop force update thread */
    if (force_update_thread_running) {
        force_update_thread_running = 0;
        pthread_join(force_update_thread, NULL);
        LOG_DEBUG("Force update thread stopped");
    }

    /* Stop ramp update thread */
    if (ramp_thread_running) {
        ramp_thread_running = 0;
        pthread_join(ramp_thread, NULL);
    }

    /* Stop all active effects */
    pthread_mutex_lock(&effects_lock);
    for (int i = 0; i < MAX_EFFECTS; i++) {
        if (effects[i].active) {
            stop_effect(i);
            effects[i].active = 0;
            effects[i].is_ramp = 0;
            effects[i].is_constant = 0;
        }
    }
    pthread_mutex_unlock(&effects_lock);

    /* Send stop commands for ALL effect slots to be sure (only if USB is connected) */
    if (usb_handle) {
        unsigned char buf[4];
        for (int i = 0; i < MAX_EFFECTS; i++) {
            buf[0] = 0x41;
            buf[1] = i;
            buf[2] = 0x00;
            buf[3] = 0x01;
            usb_send(buf, 4);
        }

        /* Clear any ramp state with zero Report 0x04 */
        memset(buf, 0, sizeof(buf));
        buf[0] = 0x04;
        buf[1] = 0x0e;
        usb_send(buf, 9);
    }

    /* Destroy uinput device */
    if (uinput_fd >= 0) {
        ioctl(uinput_fd, UI_DEV_DESTROY);
        close(uinput_fd);
    }

    /* Release USB interface */
    if (usb_handle) {
        libusb_release_interface(usb_handle, INTERFACE);
        /* Don't reattach kernel driver - we want userspace driver to handle everything */
        /* libusb_attach_kernel_driver(usb_handle, INTERFACE); */
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

    /* Open device - try normal mode first, then boot mode */
    LOG_INFO("Opening T500RS device...");
    usb_handle = libusb_open_device_with_vid_pid(usb_ctx, VENDOR_ID, PRODUCT_ID);
    if (!usb_handle) {
        LOG_INFO("Normal mode device not found, trying boot mode...");
        usb_handle = libusb_open_device_with_vid_pid(usb_ctx, VENDOR_ID, PRODUCT_ID_BOOT);
        if (!usb_handle) {
            LOG_ERROR("Cannot open device (tried VID=%04x, PID=%04x and %04x)",
                     VENDOR_ID, PRODUCT_ID, PRODUCT_ID_BOOT);
            LOG_ERROR("Make sure the device is connected and you have permissions");
            cleanup();
            return 1;
        }
        LOG_INFO("Opened in boot mode (will initialize to normal mode)");
    }
    LOG_INFO("Device opened successfully");

    /* Detach kernel driver - try even if libusb doesn't think it's active */
    LOG_INFO("Detaching kernel driver...");
    ret = libusb_detach_kernel_driver(usb_handle, INTERFACE);
    if (ret < 0 && ret != LIBUSB_ERROR_NOT_FOUND) {
        LOG_ERROR("Failed to detach kernel driver: %s", libusb_error_name(ret));
        LOG_ERROR("Try manually: echo '2-1.3:1.0' | sudo tee /sys/bus/usb/drivers/usbhid/unbind");
        cleanup();
        return 1;
    }
    if (ret == LIBUSB_ERROR_NOT_FOUND) {
        LOG_INFO("No kernel driver was attached");
    } else {
        LOG_INFO("Kernel driver detached successfully");
    }

    /* Claim interface */
    LOG_INFO("Claiming USB interface...");
    ret = libusb_claim_interface(usb_handle, INTERFACE);
    if (ret < 0) {
        LOG_ERROR("Cannot claim interface: %s", libusb_error_name(ret));
        cleanup();
        return 1;
    }

    /* Check if we opened in boot mode */
    struct libusb_device_descriptor desc;
    libusb_device *dev = libusb_get_device(usb_handle);
    if (!dev) {
        LOG_ERROR("Failed to get device");
        cleanup();
        return 1;
    }

    ret = libusb_get_device_descriptor(dev, &desc);
    if (ret < 0) {
        LOG_ERROR("Failed to get device descriptor: %s", libusb_error_name(ret));
        cleanup();
        return 1;
    }

    int was_boot_mode = (desc.idProduct == PRODUCT_ID_BOOT);

    /* If in boot mode, send initialization sequence to trigger mode switch */
    if (was_boot_mode) {
        LOG_INFO("Device in boot mode, will send initialization sequence...");
        /* Don't do USB reset - it causes the handle to become invalid
         * Instead, we'll send the init sequence directly which includes
         * the USB control requests that trigger the mode switch */
        
        /* Initialize device (includes mode switch) */
        ret = t500rs_initialize();
        if (ret < 0) {
            LOG_ERROR("Device initialization failed");
            cleanup();
            return 1;
        }
    } else {
        LOG_INFO("Device already in normal mode, skipping mode switch");
        /* Device is already in normal mode, just send basic init commands */
        unsigned char init_buf[15];
        
        /* Report 0x40 - Initialize FFB system */
        memset(init_buf, 0, sizeof(init_buf));
        init_buf[0] = 0x40;
        init_buf[1] = 0x11;
        init_buf[2] = 0x55;
        init_buf[3] = 0xd5;
        ret = usb_send(init_buf, 4);
        if (ret) {
            LOG_ERROR("Failed to send init command 1");
        }
        usleep(10000);
        
        /* Report 0x42 - Configuration */
        memset(init_buf, 0, sizeof(init_buf));
        init_buf[0] = 0x42;
        init_buf[1] = 0x04;
        ret = usb_send(init_buf, 2);
        if (ret) {
            LOG_ERROR("Failed to send init command 2");
        }
        usleep(8000);
        
        /* Report 0x40 - Enable FFB */
        memset(init_buf, 0, sizeof(init_buf));
        init_buf[0] = 0x40;
        init_buf[1] = 0x04;
        init_buf[2] = 0x00;
        init_buf[3] = 0x00;
        ret = usb_send(init_buf, 4);
        if (ret) {
            LOG_ERROR("Failed to send init command 3");
        }
        usleep(8000);
        
        /* Report 0x40 - Finalize init */
        memset(init_buf, 0, sizeof(init_buf));
        init_buf[0] = 0x40;
        init_buf[1] = 0x03;
        init_buf[2] = 0x0d;
        init_buf[3] = 0x00;
        ret = usb_send(init_buf, 4);
        if (ret) {
            LOG_ERROR("Failed to send init command 4");
        }
        usleep(10000);
        
        LOG_INFO("Normal mode initialization complete");
    }

    /* If we were in boot mode, the device should have switched to normal mode */
    /* The device will disconnect and reconnect with new USB ID */
    if (was_boot_mode) {
        LOG_INFO("Device was in boot mode - it should now re-enumerate as normal mode");
        LOG_INFO("Waiting for device to disconnect and reconnect...");

        /* The device will disconnect after init sequence */
        /* We need to wait and detect the reconnection */

        /* Try to detect disconnection by attempting a simple USB operation */
        int disconnected = 0;
        for (int i = 0; i < 10; i++) {
            unsigned char test_buf[1];
            int test_ret = libusb_control_transfer(usb_handle,
                LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_DEVICE,
                LIBUSB_REQUEST_GET_STATUS,
                0, 0, test_buf, 1, 100);

            if (test_ret < 0) {
                LOG_INFO("Device disconnected (attempt %d/10)", i+1);
                disconnected = 1;
                break;
            }
            usleep(100000);  /* Wait 100ms between checks */
        }

        if (!disconnected) {
            LOG_INFO("Device did not disconnect as expected");
            LOG_INFO("Trying to reopen anyway...");
        }

        /* Close current handle */
        if (usb_handle) {
            libusb_close(usb_handle);
            usb_handle = NULL;
        }

        /* Wait for device to re-enumerate */
        LOG_INFO("Waiting for device to re-enumerate in normal mode...");
        sleep(1);

        /* Try to open in normal mode with retries */
        int max_retries = 10;
        for (int retry = 0; retry < max_retries; retry++) {
            usb_handle = libusb_open_device_with_vid_pid(usb_ctx, VENDOR_ID, PRODUCT_ID);
            if (usb_handle) {
                LOG_INFO("Device switched to normal mode successfully! (attempt %d/%d)", retry+1, max_retries);
                break;
            }

            if (retry < max_retries - 1) {
                LOG_INFO("Normal mode device not found yet, waiting... (attempt %d/%d)", retry+1, max_retries);
                sleep(1);
            }
        }

        if (!usb_handle) {
            LOG_ERROR("Device did not switch to normal mode after %d attempts!", max_retries);
            LOG_ERROR("Current device status:");
            system("lsusb | grep -i thrust");
            LOG_ERROR("");
            LOG_ERROR("If device shows as 044f:b65d, the mode switch failed.");
            LOG_ERROR("Please try:");
            LOG_ERROR("  1. Unplug the wheel USB cable");
            LOG_ERROR("  2. Wait 10 seconds");
            LOG_ERROR("  3. Plug it back in");
            LOG_ERROR("  4. Run: lsusb | grep -i thrust");
            LOG_ERROR("  5. If it shows 044f:b65e, start the driver again");
            cleanup();
            return 1;
        }

        /* Detach kernel driver from normal mode device */
        LOG_INFO("Detaching kernel driver from normal mode device...");
        ret = libusb_detach_kernel_driver(usb_handle, INTERFACE);
        if (ret < 0 && ret != LIBUSB_ERROR_NOT_FOUND) {
            LOG_ERROR("Failed to detach kernel driver: %s", libusb_error_name(ret));
            cleanup();
            return 1;
        }

        /* Claim interface */
        LOG_INFO("Claiming USB interface...");
        ret = libusb_claim_interface(usb_handle, INTERFACE);
        if (ret < 0) {
            LOG_ERROR("Failed to claim interface: %s", libusb_error_name(ret));
            cleanup();
            return 1;
        }

        LOG_INFO("Normal mode device ready!");
        
        /* Initialize the normal mode device - this is critical for FFB */
        LOG_INFO("Initializing normal mode device for force feedback...");
        
        /* Send essential initialization commands for normal mode */
        unsigned char init_buf[15];
        
        /* Report 0x40 - Initialize FFB system */
        memset(init_buf, 0, sizeof(init_buf));
        init_buf[0] = 0x40;
        init_buf[1] = 0x11;
        init_buf[2] = 0x55;
        init_buf[3] = 0xd5;
        ret = usb_send(init_buf, 4);
        if (ret) {
            LOG_ERROR("Failed to send normal mode init command 1");
        }
        usleep(10000);
        
        /* Report 0x42 - Configuration */
        memset(init_buf, 0, sizeof(init_buf));
        init_buf[0] = 0x42;
        init_buf[1] = 0x04;
        ret = usb_send(init_buf, 2);
        if (ret) {
            LOG_ERROR("Failed to send normal mode init command 2");
        }
        usleep(8000);
        
        /* Report 0x40 - Enable FFB */
        memset(init_buf, 0, sizeof(init_buf));
        init_buf[0] = 0x40;
        init_buf[1] = 0x04;
        init_buf[2] = 0x00;
        init_buf[3] = 0x00;
        ret = usb_send(init_buf, 4);
        if (ret) {
            LOG_ERROR("Failed to send normal mode init command 3");
        }
        usleep(8000);
        
        /* Report 0x40 - Finalize init */
        memset(init_buf, 0, sizeof(init_buf));
        init_buf[0] = 0x40;
        init_buf[1] = 0x03;
        init_buf[2] = 0x0d;
        init_buf[3] = 0x00;
        ret = usb_send(init_buf, 4);
        if (ret) {
            LOG_ERROR("Failed to send normal mode init command 4");
        }
        usleep(10000);
        
        LOG_INFO("Normal mode initialization complete");
    }

#if USE_WINDOWS_PROTOCOL
    /* Initialize Windows-compatible protocol layer */
    LOG_INFO("Initializing Windows-compatible protocol layer...");
    ret = t500rs_initialize_windows_compatible();
    if (ret) {
        LOG_ERROR("Windows protocol initialization failed, continuing with legacy mode");
        /* Not fatal - continue with legacy protocol */
    } else {
        LOG_INFO("✅ Windows-compatible protocol layer active");
        
        /* Set initial range using Windows protocol */
        ret = set_rotation_angle_windows_protocol(current_rotation_angle);
        if (ret) {
            LOG_ERROR("Failed to set initial range using Windows protocol");
        }
    }
#endif

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

    /* Start force update thread for continuous force feedback */
    force_update_thread_running = 1;
    if (pthread_create(&force_update_thread, NULL, force_update_thread_func, NULL) != 0) {
        LOG_ERROR("Failed to create force update thread");
        cleanup();
        return 1;
    }
    LOG_INFO("Force update thread created (20ms updates)");

    /* Start ramp update thread */
#if ENABLE_RAMP_EFFECTS
    ramp_thread_running = 1;
    if (pthread_create(&ramp_thread, NULL, ramp_update_thread_func, NULL) != 0) {
        LOG_ERROR("Failed to create ramp update thread");
        cleanup();
        return 1;
    }
    LOG_DEBUG("Ramp update thread created");
#else
    LOG_INFO("Ramp effects disabled (ENABLE_RAMP_EFFECTS=0)");
#endif

    /* Start input reading thread */
    if (pthread_create(&input_thread, NULL, input_reading_thread, NULL) != 0) {
        LOG_ERROR("Failed to create input reading thread");
        cleanup();
        return 1;
    }
    LOG_INFO("Input reading thread created");

    /* Main event loop */
    process_uinput_events();

    /* Cleanup */
    cleanup();

    LOG_INFO("Driver stopped");
    return 0;
}
