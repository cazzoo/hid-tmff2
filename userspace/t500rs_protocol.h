/*
 * T500RS Windows-Compatible Protocol Definitions
 *
 * Based on comprehensive Ghidra reverse engineering analysis of Windows tmpid.dll driver
 * These constants and structures match the Windows driver exactly for maximum compatibility
 *
 * Copyright (C) 2025
 */

#ifndef T500RS_PROTOCOL_H
#define T500RS_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <endian.h>

/* HID Protocol Constants - From Windows Driver Analysis */
#define T500RS_REPORT_ID                0xEF    /* All commands use this report ID */
#define T500RS_REPORT_SIZE_STD         24       /* Standard report size (0x18 bytes) */

/* Windows Driver Command Types - From Ghidra FUN_18000ebf4 */
#define T500RS_CMD_SYSTEM              0x01     /* System/initialization command */
#define T500RS_CMD_FF_PRIMARY          0x03     /* Primary force feedback command */
#define T500RS_CMD_FF_SECONDARY        0x04     /* Secondary force feedback command */
#define T500RS_CMD_CONFIG              0x05     /* Device configuration */
#define T500RS_CMD_STATUS              0x06     /* Status/query command */
#define T500RS_CMD_FF_EXTENDED         0x11     /* Extended force feedback (17 decimal) */

/* Internal Command Constants - From Ghidra FUN_18001d994 and FUN_1800260c0 */
#define T500RS_INTERNAL_FF_ENABLE      0x313    /* 787 decimal - Force feedback enable */
#define T500RS_INTERNAL_FF_PARAM       0x303    /* 771 decimal - Force feedback parameter */
#define T500RS_INTERNAL_RANGE_SET      0x97     /* Range enable command */
#define T500RS_INTERNAL_RANGE_DIS      0x98     /* Range disable command */

/* Windows Driver Default Values - From Ghidra FUN_18002581c */
#define T500RS_DEFAULT_RANGE           10000    /* Default steering range */
#define T500RS_DEFAULT_UPDATE_RATE     10000    /* Default update frequency */
#define T500RS_CENTER_X                0x500    /* 1280 decimal - Center position */
#define T500RS_CENTER_Y                900      /* Default Y position */
#define T500RS_ERROR_STATUS            0x8F     /* Default status flags on error */
#define T500RS_ERROR_POSITION          0xFFFFFFFF /* -1 on communication error */

/* Device Limits */
#define T500RS_MAX_DEVICES             16       /* Maximum simultaneous devices (0x10) */
#define T500RS_DEVICE_STATE_SIZE       0xB30    /* 2864 bytes per device (from Windows) */
#define T500RS_MIN_RANGE_DEGREES       270      /* Minimum steering range */
#define T500RS_MAX_RANGE_DEGREES       1080     /* Maximum steering range */

/* HID Output Report Structure - Based on Windows Protocol */
struct t500rs_hid_output {
    uint8_t  report_id;      /* Always T500RS_REPORT_ID (0xEF) */
    uint8_t  command_type;   /* Command identifier (T500RS_CMD_*) */
    uint8_t  flags;          /* Command flags */
    uint16_t parameter;      /* Command parameter (little-endian) */
    uint8_t  payload[59];    /* Variable payload data for effects (total 64 bytes) */
} __attribute__((packed));

/* Device State Structure - Based on Ghidra Analysis Offsets */
struct t500rs_device_state {
    /* Core identification */
    uint32_t device_id;              /* Device identifier */
    uint32_t enabled;                /* Device enabled flag */
    
    /* Report configuration (from Ghidra memory offsets) */
    uint16_t output_report_size;     /* +0x5B4 - Output report size (24 bytes) */
    uint16_t input_report_size;      /* +0x5B2 - Input report size */
    uint16_t feature_report_size;    /* +0x66C - Feature report size */
    uint16_t collection_size;        /* +0x676 - HID collection size */
    
    /* Force feedback state (from Windows driver offsets) */
    uint32_t ff_enabled;             /* +0x15E - Force feedback enabled/disabled */
    uint32_t ff_gain;                /* +0x15F - Global FF gain */
    uint32_t constant_level;         /* +0x15B - Current constant force level */
    
    /* Position and range (from Windows driver state) */
    uint32_t steering_range;         /* +0x157 - Steering range (default 10000) */
    uint32_t current_x;              /* +0xAF4 - Current X position */
    uint32_t current_y;              /* +0xAFC - Current Y position */
    
    /* Status and control flags */
    uint32_t update_rate;            /* Update frequency */
    uint32_t dirty_flag;             /* +0x164 - Needs update flag */
    uint32_t initialized;            /* +0x82C - Initialization complete */
    
    /* Runtime state management */
    bool needs_update;               /* State synchronization needed */
    bool protocol_active;            /* Windows protocol mode active */
    uint64_t last_update_time;       /* Last state update timestamp */
};

/* Effect Translation Structure */
struct t500rs_effect_translator {
    int (*translate_constant)(struct ff_effect *linux_effect, 
                             struct t500rs_hid_output *device_cmd);
    int (*translate_periodic)(struct ff_effect *linux_effect,
                             struct t500rs_hid_output *device_cmd);
    int (*translate_spring)(struct ff_effect *linux_effect,
                           struct t500rs_hid_output *device_cmd);
    int (*translate_damper)(struct ff_effect *linux_effect,
                           struct t500rs_hid_output *device_cmd);
    int (*translate_friction)(struct ff_effect *linux_effect,
                             struct t500rs_hid_output *device_cmd);
    int (*translate_inertia)(struct ff_effect *linux_effect,
                            struct t500rs_hid_output *device_cmd);
};

/* Forward declaration for ff_effect and ff_envelope */
struct ff_effect;
struct ff_envelope;

/* Function Prototypes */

/* Windows MulDiv function implementation - exact match to Windows API */
uint32_t MulDiv(uint32_t number, uint32_t numerator, uint32_t denominator);

/* Protocol communication functions */
int t500rs_send_hid_command(uint8_t cmd_type, uint16_t param, uint8_t flags);
int t500rs_send_hid_report(const struct t500rs_hid_output *cmd);

/* Initialization and configuration */
int t500rs_initialize_windows_compatible(void);
int t500rs_apply_windows_defaults(struct t500rs_device_state *state);

/* Range and steering control */
int t500rs_set_range_windows_compatible(int angle_degrees);
int t500rs_calculate_range_scaling(int degrees, uint32_t *internal_range, uint32_t *scaled_range);

/* Force feedback control */
int t500rs_upload_effect_windows_style(int id, struct ff_effect *effect);
int t500rs_start_effect_windows_style(int id);
int t500rs_stop_effect_windows_style(int id);

/* State management */
int t500rs_init_device_state(struct t500rs_device_state *state);
int t500rs_update_device_state(struct t500rs_device_state *state);
void t500rs_apply_error_defaults(struct t500rs_device_state *state);

/* Protocol validation and debugging */
bool t500rs_validate_command(const struct t500rs_hid_output *cmd);
void t500rs_dump_command(const struct t500rs_hid_output *cmd, const char *direction);
void t500rs_dump_device_state(const struct t500rs_device_state *state);

/* Command construction helpers */
void t500rs_construct_system_command(struct t500rs_hid_output *cmd, uint16_t param);
void t500rs_construct_ff_command(struct t500rs_hid_output *cmd, uint8_t ff_type, uint16_t param, uint8_t flags);
void t500rs_construct_config_command(struct t500rs_hid_output *cmd, uint16_t param, uint8_t flags);
void t500rs_construct_status_command(struct t500rs_hid_output *cmd);

/* USB communication function (from main driver) */
int usb_send(unsigned char *data, int len);

/* Global device state access */
struct t500rs_device_state *t500rs_get_device_state(void);

/* Effect translation functions - from t500rs_effects.c */
int t500rs_translate_effect(struct ff_effect *effect,
                            struct t500rs_hid_output *cmd,
                            int apply_gain);

int t500rs_translate_constant_effect(struct ff_effect *effect,
                                     struct t500rs_hid_output *cmd,
                                     int apply_gain);

int t500rs_translate_periodic_effect(struct ff_effect *effect,
                                     struct t500rs_hid_output *cmd,
                                     int apply_gain);

int t500rs_translate_spring_effect(struct ff_effect *effect,
                                   struct t500rs_hid_output *cmd,
                                   int apply_gain);

int t500rs_translate_damper_effect(struct ff_effect *effect,
                                   struct t500rs_hid_output *cmd,
                                   int apply_gain);

int t500rs_translate_friction_effect(struct ff_effect *effect,
                                     struct t500rs_hid_output *cmd,
                                     int apply_gain);

int t500rs_translate_inertia_effect(struct ff_effect *effect,
                                    struct t500rs_hid_output *cmd,
                                    int apply_gain);

/* Envelope support */
int t500rs_apply_envelope(struct ff_envelope *envelope,
                          unsigned long elapsed_ms,
                          int base_level);

/* Per-effect-type gain application (from main driver) */
int apply_effect_gain(int value, int effect_type);

#endif /* T500RS_PROTOCOL_H */
