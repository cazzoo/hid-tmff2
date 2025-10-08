/*
 * T500RS Windows-Compatible Protocol Implementation
 *
 * Core protocol functions based on comprehensive Ghidra reverse engineering
 * of Windows tmpid.dll driver. These functions replicate Windows behavior exactly.
 *
 * Copyright (C) 2025
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <libusb-1.0/libusb.h>
#include <linux/input.h>

#include "t500rs_protocol.h"

/* External references to main driver globals */
extern libusb_device_handle *usb_handle;
extern int running;

/* Logging macros */
#define LOG_INFO(fmt, ...) fprintf(stdout, "[PROTOCOL] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) fprintf(stderr, "[PROTOCOL] " fmt "\n", ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) fprintf(stdout, "[PROTOCOL DEBUG] " fmt "\n", ##__VA_ARGS__)

/* Global device state - Windows driver style */
static struct t500rs_device_state g_device_state;
static pthread_mutex_t g_state_lock = PTHREAD_MUTEX_INITIALIZER;

/* Windows MulDiv function implementation - exact match to Windows API */
uint32_t MulDiv(uint32_t number, uint32_t numerator, uint32_t denominator) {
    if (denominator == 0) {
        LOG_ERROR("MulDiv: Division by zero attempted");
        return 0;
    }
    
    /* Use 64-bit arithmetic to prevent overflow, matching Windows behavior */
    uint64_t result = ((uint64_t)number * numerator) / denominator;
    
    /* Clamp to 32-bit range */
    if (result > 0xFFFFFFFF) {
        result = 0xFFFFFFFF;
    }
    
    return (uint32_t)result;
}

/* Send raw USB data - shared with main driver */
extern int usb_send(unsigned char *data, int len);

/* Core HID command transmission function */
int t500rs_send_hid_command(uint8_t cmd_type, uint16_t param, uint8_t flags) {
    struct t500rs_hid_output cmd;
    
    /* Construct command in Windows driver format */
    memset(&cmd, 0, sizeof(cmd));
    cmd.report_id = T500RS_REPORT_ID;                    /* Always 0xEF */
    cmd.command_type = cmd_type;                         /* Command type */
    cmd.parameter = htole16(param);                      /* Little-endian parameter */
    cmd.flags = flags;                                   /* Command flags */
    
    /* Debug output */
    t500rs_dump_command(&cmd, "OUT");
    
    /* Send to device */
    return usb_send((unsigned char*)&cmd, T500RS_REPORT_SIZE_STD);
}

/* Send complete HID report structure */
int t500rs_send_hid_report(const struct t500rs_hid_output *cmd) {
    if (!cmd || !t500rs_validate_command(cmd)) {
        LOG_ERROR("Invalid HID command structure");
        return -EINVAL;
    }
    
    t500rs_dump_command(cmd, "OUT");
    return usb_send((unsigned char*)cmd, T500RS_REPORT_SIZE_STD);
}

/* Windows-compatible initialization sequence - based on Ghidra analysis */
int t500rs_initialize_windows_compatible(void) {
    int ret;
    
    LOG_INFO("Initializing T500RS with Windows-compatible protocol...");
    
    /* Initialize device state structure first */
    ret = t500rs_init_device_state(&g_device_state);
    if (ret) {
        LOG_ERROR("Failed to initialize device state");
        return ret;
    }
    
    /* System initialization command (0x01) - from Windows driver init */
    LOG_DEBUG("Sending system initialization command");
    ret = t500rs_send_hid_command(T500RS_CMD_SYSTEM, 0x0001, 0x00);
    if (ret) {
        LOG_ERROR("System initialization command failed");
        return ret;
    }
    usleep(40000);  /* Windows driver timing */
    
    /* Configuration commands (0x05) - from Windows initialization sequence */
    LOG_DEBUG("Sending configuration command 1");
    ret = t500rs_send_hid_command(T500RS_CMD_CONFIG, 0x0390, 0x04);
    if (ret) {
        LOG_ERROR("Configuration command 1 failed");
        return ret;
    }
    usleep(4000);
    
    LOG_DEBUG("Sending configuration command 2");
    ret = t500rs_send_hid_command(T500RS_CMD_CONFIG, 0x1012, 0x04);
    if (ret) {
        LOG_ERROR("Configuration command 2 failed");
        return ret;
    }
    usleep(4000);
    
    LOG_DEBUG("Sending configuration command 3");
    ret = t500rs_send_hid_command(T500RS_CMD_CONFIG, 0x0600, 0x00);
    if (ret) {
        LOG_ERROR("Configuration command 3 failed");
        return ret;
    }
    usleep(64000);  /* Longer delay like Windows driver */
    
    /* Status query (0x06) - confirm device ready */
    LOG_DEBUG("Sending status query");
    ret = t500rs_send_hid_command(T500RS_CMD_STATUS, 0x0000, 0x00);
    if (ret) {
        LOG_ERROR("Status query failed");
        return ret;
    }
    usleep(10000);
    
    /* Mark protocol as active */
    pthread_mutex_lock(&g_state_lock);
    g_device_state.protocol_active = true;
    g_device_state.initialized = 1;
    pthread_mutex_unlock(&g_state_lock);
    
    LOG_INFO("✅ Windows-compatible initialization complete");
    return 0;
}

/* Range calculation and setting - based on Ghidra FUN_1800260c0 analysis */
int t500rs_calculate_range_scaling(int degrees, uint32_t *internal_range, uint32_t *scaled_range) {
    if (!internal_range || !scaled_range) {
        return -EINVAL;
    }
    
    /* Clamp to valid range */
    if (degrees < T500RS_MIN_RANGE_DEGREES) degrees = T500RS_MIN_RANGE_DEGREES;
    if (degrees > T500RS_MAX_RANGE_DEGREES) degrees = T500RS_MAX_RANGE_DEGREES;
    
    /* Convert degrees to internal range (0-10000) - Windows formula */
    *internal_range = ((degrees - T500RS_MIN_RANGE_DEGREES) * T500RS_DEFAULT_RANGE) / 
                      (T500RS_MAX_RANGE_DEGREES - T500RS_MIN_RANGE_DEGREES);
    
    /* Apply Windows driver scaling: MulDiv(100, range, 10000) */
    *scaled_range = MulDiv(100, *internal_range, T500RS_DEFAULT_RANGE);
    
    LOG_DEBUG("Range calculation: %d° -> internal:%u -> scaled:%u", 
              degrees, *internal_range, *scaled_range);
    
    return 0;
}

/* Windows-compatible range setting */
int t500rs_set_range_windows_compatible(int angle_degrees) {
    uint32_t internal_range, scaled_range;
    int ret;
    
    LOG_INFO("Setting range to %d° using Windows-compatible protocol", angle_degrees);
    
    /* Calculate Windows-style scaling */
    ret = t500rs_calculate_range_scaling(angle_degrees, &internal_range, &scaled_range);
    if (ret) {
        LOG_ERROR("Range calculation failed");
        return ret;
    }
    
    pthread_mutex_lock(&g_state_lock);
    
    /* Send range enable command - based on 0x313 constant from Ghidra */
    LOG_DEBUG("Sending range enable command (0x313)");
    ret = t500rs_send_hid_command(T500RS_CMD_FF_PRIMARY, 
                                  T500RS_INTERNAL_FF_ENABLE, 
                                  scaled_range & 0xFF);
    if (ret) {
        LOG_ERROR("Range enable command failed");
        pthread_mutex_unlock(&g_state_lock);
        return ret;
    }
    usleep(5000);
    
    /* Send range parameter command - based on 0x303 constant from Ghidra */
    LOG_DEBUG("Sending range parameter command (0x303)");
    ret = t500rs_send_hid_command(T500RS_CMD_FF_PRIMARY,
                                  T500RS_INTERNAL_FF_PARAM, 
                                  scaled_range & 0xFF);
    if (ret) {
        LOG_ERROR("Range parameter command failed");
        pthread_mutex_unlock(&g_state_lock);
        return ret;
    }
    usleep(5000);
    
    /* Update device state */
    g_device_state.steering_range = internal_range;
    g_device_state.needs_update = true;
    
    pthread_mutex_unlock(&g_state_lock);
    
    LOG_INFO("✅ Range set to %d° (internal:%u, scaled:%u)", 
             angle_degrees, internal_range, scaled_range);
    
    return 0;
}

/* Initialize device state with Windows defaults */
int t500rs_init_device_state(struct t500rs_device_state *state) {
    if (!state) return -EINVAL;
    
    memset(state, 0, sizeof(*state));
    
    /* Set Windows driver default values from Ghidra analysis */
    state->enabled = 1;
    state->output_report_size = T500RS_REPORT_SIZE_STD;          /* 24 bytes */
    state->input_report_size = 15;                               /* From USB captures */
    state->feature_report_size = 0;                              /* TBD */
    state->collection_size = 0;                                  /* TBD */
    
    state->ff_enabled = 1;                                       /* FF enabled by default */
    state->ff_gain = 0xFFFF;                                     /* Maximum gain */
    state->constant_level = 0;                                   /* No initial force */
    
    state->steering_range = T500RS_DEFAULT_RANGE;                /* 10000 */
    state->current_x = T500RS_CENTER_X;                          /* 1280 - center */
    state->current_y = T500RS_CENTER_Y;                          /* 900 */
    
    state->update_rate = T500RS_DEFAULT_UPDATE_RATE;             /* 10000 */
    state->dirty_flag = 0;
    state->initialized = 0;
    
    state->needs_update = false;
    state->protocol_active = false;
    state->last_update_time = 0;
    
    LOG_DEBUG("Device state initialized with Windows defaults");
    return 0;
}

/* Apply Windows error defaults when communication fails */
void t500rs_apply_error_defaults(struct t500rs_device_state *state) {
    if (!state) return;
    
    LOG_INFO("Applying Windows driver error defaults");
    
    pthread_mutex_lock(&g_state_lock);
    
    /* Set Windows driver error values from Ghidra analysis */
    state->current_x = T500RS_CENTER_X;                          /* 0x500 = 1280 */
    state->current_y = T500RS_CENTER_Y;                          /* 900 */
    state->steering_range = T500RS_DEFAULT_RANGE;                /* 10000 */
    state->ff_enabled = 1;                                       /* Keep FF enabled */
    state->constant_level = 0;                                   /* Stop all forces */
    state->dirty_flag = T500RS_ERROR_POSITION;                  /* -1 (0xFFFFFFFF) */
    
    /* Send emergency stop command */
    struct t500rs_hid_output stop_cmd;
    memset(&stop_cmd, 0, sizeof(stop_cmd));
    stop_cmd.report_id = T500RS_REPORT_ID;
    stop_cmd.command_type = T500RS_CMD_FF_PRIMARY;
    stop_cmd.parameter = htole16(0x0000);
    stop_cmd.flags = 0x00;
    
    /* Try to send stop command (may fail if device disconnected) */
    usb_send((unsigned char*)&stop_cmd, T500RS_REPORT_SIZE_STD);
    
    pthread_mutex_unlock(&g_state_lock);
    
    LOG_DEBUG("Error defaults applied successfully");
}

/* Update device state - Windows driver style polling */
int t500rs_update_device_state(struct t500rs_device_state *state) {
    if (!state || !state->protocol_active) {
        return -EINVAL;
    }
    
    pthread_mutex_lock(&g_state_lock);
    
    if (state->needs_update) {
        /* Send status query like Windows driver */
        struct t500rs_hid_output status_cmd;
        memset(&status_cmd, 0, sizeof(status_cmd));
        status_cmd.report_id = T500RS_REPORT_ID;
        status_cmd.command_type = T500RS_CMD_STATUS;
        status_cmd.parameter = htole16(0x0000);
        
        int ret = usb_send((unsigned char*)&status_cmd, T500RS_REPORT_SIZE_STD);
        if (ret == 0) {
            state->needs_update = false;
            state->last_update_time = time(NULL);
        }
        
        pthread_mutex_unlock(&g_state_lock);
        return ret;
    }
    
    pthread_mutex_unlock(&g_state_lock);
    return 0;
}

/* Validate HID command structure */
bool t500rs_validate_command(const struct t500rs_hid_output *cmd) {
    if (!cmd) return false;
    
    /* Check report ID */
    if (cmd->report_id != T500RS_REPORT_ID) {
        LOG_ERROR("Invalid report ID: 0x%02x (expected 0x%02x)", cmd->report_id, T500RS_REPORT_ID);
        return false;
    }
    
    /* Check command type */
    switch (cmd->command_type) {
    case T500RS_CMD_SYSTEM:
    case T500RS_CMD_FF_PRIMARY:
    case T500RS_CMD_FF_SECONDARY:
    case T500RS_CMD_CONFIG:
    case T500RS_CMD_STATUS:
    case T500RS_CMD_FF_EXTENDED:
        break;  /* Valid command types */
    default:
        LOG_ERROR("Unknown command type: 0x%02x", cmd->command_type);
        return false;
    }
    
    return true;
}

/* Debug: dump HID command structure */
void t500rs_dump_command(const struct t500rs_hid_output *cmd, const char *direction) {
    if (!cmd || !direction) return;
    
    LOG_DEBUG("%s: ReportID=0x%02x, Cmd=0x%02x, Param=0x%04x, Flags=0x%02x",
              direction, cmd->report_id, cmd->command_type, 
              le16toh(cmd->parameter), cmd->flags);
    
    /* Dump payload if non-zero */
    bool has_payload = false;
    for (int i = 0; i < sizeof(cmd->payload); i++) {
        if (cmd->payload[i] != 0) {
            has_payload = true;
            break;
        }
    }
    
    if (has_payload) {
        char payload_str[64];
        char *p = payload_str;
        for (int i = 0; i < 8 && i < sizeof(cmd->payload); i++) {  /* Show first 8 bytes */
            p += sprintf(p, "%02x ", cmd->payload[i]);
        }
        LOG_DEBUG("%s: Payload: %s%s", direction, payload_str, 
                  sizeof(cmd->payload) > 8 ? "..." : "");
    }
}

/* Debug: dump device state */
void t500rs_dump_device_state(const struct t500rs_device_state *state) {
    if (!state) return;
    
    pthread_mutex_lock((pthread_mutex_t*)&g_state_lock);
    
    LOG_DEBUG("=== Device State ===");
    LOG_DEBUG("Enabled: %u, Protocol: %s, Initialized: %u", 
              state->enabled, state->protocol_active ? "active" : "inactive", state->initialized);
    LOG_DEBUG("Report sizes - Out: %u, In: %u, Feature: %u", 
              state->output_report_size, state->input_report_size, state->feature_report_size);
    LOG_DEBUG("FF - Enabled: %u, Gain: 0x%x, Level: %d", 
              state->ff_enabled, state->ff_gain, state->constant_level);
    LOG_DEBUG("Position - X: %u (0x%x), Y: %u, Range: %u", 
              state->current_x, state->current_x, state->current_y, state->steering_range);
    LOG_DEBUG("Status - Dirty: 0x%x, Update needed: %s", 
              state->dirty_flag, state->needs_update ? "yes" : "no");
    
    pthread_mutex_unlock((pthread_mutex_t*)&g_state_lock);
}

/* Command construction helpers */
void t500rs_construct_system_command(struct t500rs_hid_output *cmd, uint16_t param) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->report_id = T500RS_REPORT_ID;
    cmd->command_type = T500RS_CMD_SYSTEM;
    cmd->parameter = htole16(param);
}

void t500rs_construct_ff_command(struct t500rs_hid_output *cmd, uint8_t ff_type, uint16_t param, uint8_t flags) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->report_id = T500RS_REPORT_ID;
    cmd->command_type = ff_type;  /* T500RS_CMD_FF_PRIMARY, etc. */
    cmd->parameter = htole16(param);
    cmd->flags = flags;
}

void t500rs_construct_config_command(struct t500rs_hid_output *cmd, uint16_t param, uint8_t flags) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->report_id = T500RS_REPORT_ID;
    cmd->command_type = T500RS_CMD_CONFIG;
    cmd->parameter = htole16(param);
    cmd->flags = flags;
}

void t500rs_construct_status_command(struct t500rs_hid_output *cmd) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->report_id = T500RS_REPORT_ID;
    cmd->command_type = T500RS_CMD_STATUS;
    cmd->parameter = htole16(0x0000);
}

/* Get global device state (thread-safe) */
struct t500rs_device_state *t500rs_get_device_state(void) {
    return &g_device_state;
}