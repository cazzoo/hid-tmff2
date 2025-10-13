/**
 * @file t500rs_common.h
 * @brief Common definitions and structures for T500RS driver
 * 
 * This header contains shared constants, structures, and type definitions
 * used across all T500RS driver modules.
 */

#ifndef T500RS_COMMON_H
#define T500RS_COMMON_H

#include <stdint.h>
#include <time.h>
#include <linux/input.h>
#include <libusb-1.0/libusb.h>

/* Include new infrastructure */
#include "t500rs_config.h"
#include "t500rs_logging.h"
#include "t500rs_error.h"
#include "t500rs_stats.h"

/* ============================================================================
 * USB Device Identifiers
 * ============================================================================ */

#define VENDOR_ID           0x044f
#define PRODUCT_ID          0xb65e  /* Normal mode */
#define PRODUCT_ID_BOOT     0xb65d  /* Boot mode */
#define INTERFACE           0

/* ============================================================================
 * Effect Management
 * ============================================================================ */

#define MAX_EFFECTS         64
#define AUTOCENTER_EFFECT_ID 15  /* Reserved slot for autocenter spring effect (must match monolithic!) */

/**
 * @brief State information for a single force feedback effect
 */
struct effect_state {
    int active;                     /* Effect is currently active */
    struct ff_effect effect;        /* Linux FF effect structure */
    
    /* Constant force state */
    int is_constant;                /* This is a constant force effect */
    int current_force_level;        /* Current force level */
    
    /* Ramp effect state */
    int is_ramp;                    /* This is a ramp effect */
    int ramp_start_level;           /* Starting force level */
    int ramp_end_level;             /* Ending force level */
    unsigned int ramp_duration_ms;  /* Duration in milliseconds */
    
    /* Periodic effect state */
    int is_periodic;                /* This is a periodic effect */
    unsigned int periodic_magnitude; /* Waveform magnitude */
    int periodic_offset;            /* Waveform offset */
    unsigned int periodic_phase;    /* Initial phase */
    unsigned int periodic_period_ms;/* Period in milliseconds */
    int periodic_waveform;          /* Waveform type (FF_SINE, etc.) */
    
    /* Timing */
    struct timespec start_time;     /* When effect started */
    
    /* Envelope parameters */
    unsigned int attack_length_ms;  /* Attack duration */
    unsigned int attack_level;      /* Starting level (0-65535) */
    unsigned int fade_length_ms;    /* Fade duration */
    unsigned int fade_level;        /* Ending level (0-65535) */
    unsigned int duration_ms;       /* Total effect duration */
    
    /* Force smoothing */
    int last_sent_force;            /* Last force sent to device */
    int target_force;               /* Target force before smoothing */
};

/* ============================================================================
 * Configuration
 * ============================================================================ */

/**
 * @brief Global configuration settings
 */
struct t500rs_config {
    int invert_throttle;            /* Invert throttle pedal */
    int invert_brake;               /* Invert brake pedal */
    int invert_clutch;              /* Invert clutch pedal */

    /* Advanced force feedback settings (runtime configurable) */
    int enable_force_smoothing;     /* Enable exponential force smoothing (default: 1) */
    int enable_multi_effect_mixing; /* Enable multi-effect mixing (default: 1) */
    int enable_dynamic_update_rate; /* Enable dynamic update rate (default: 1) */
};

/* ============================================================================
 * Logging Macros
 * ============================================================================ */

/* Logging is now handled by t500rs_logging.h
 * The LOG_ERROR, LOG_WARN, LOG_INFO, LOG_DEBUG macros are defined there
 */

/* ============================================================================
 * Feature Flags
 * ============================================================================ */

/* Ramp effects disabled - firmware limitation causes device crash */
#define ENABLE_RAMP_EFFECTS 0

/* ============================================================================
 * Custom Event Codes (for runtime configuration via Python GUI)
 * ============================================================================ */

/* Advanced force feedback control codes (0xC0-0xCF range to avoid effect ID conflicts) */
#define FF_TOGGLE_SMOOTHING      0xC0  /* Toggle force smoothing (value: 0=off, 1=on) */
#define FF_TOGGLE_MIXING         0xC1  /* Toggle multi-effect mixing (value: 0=off, 1=on) */
#define FF_TOGGLE_DYNAMIC_RATE   0xC2  /* Toggle dynamic update rate (value: 0=off, 1=on) */
#define FF_GET_CONFIG            0xC3  /* Get current configuration (returns via log) */

/* ============================================================================
 * Global State (defined in t500rs_main.c)
 * ============================================================================ */

extern libusb_context *usb_ctx;
extern libusb_device_handle *usb_handle;
extern int uinput_fd;
extern struct effect_state effects[MAX_EFFECTS];
extern pthread_mutex_t effects_lock;
extern struct t500rs_config config;
extern uint16_t current_gain;
extern uint16_t current_autocenter;

#endif /* T500RS_COMMON_H */

