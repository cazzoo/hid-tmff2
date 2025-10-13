/**
 * @file t500rs_error.c
 * @brief Error handling implementation
 */

#include "t500rs_error.h"
#include "t500rs_logging.h"
#include <stdio.h>

/**
 * @brief Error message lookup table
 */
static const struct {
    t500rs_error_t code;
    const char *message;
} error_messages[] = {
    /* Success */
    {T500RS_OK, "Success"},
    
    /* USB errors */
    {T500RS_ERR_USB_INIT, "USB initialization failed"},
    {T500RS_ERR_USB_DEVICE_NOT_FOUND, "T500RS device not found"},
    {T500RS_ERR_USB_OPEN, "Failed to open USB device"},
    {T500RS_ERR_USB_CLAIM, "Failed to claim USB interface"},
    {T500RS_ERR_USB_TRANSFER, "USB transfer failed"},
    {T500RS_ERR_USB_TIMEOUT, "USB transfer timed out"},
    {T500RS_ERR_USB_DETACH, "Failed to detach kernel driver"},
    
    /* Device errors */
    {T500RS_ERR_DEVICE_MODE, "Failed to switch device mode"},
    {T500RS_ERR_DEVICE_INIT, "Device initialization failed"},
    {T500RS_ERR_DEVICE_NOT_READY, "Device not ready"},
    {T500RS_ERR_DEVICE_DISCONNECTED, "Device disconnected"},
    
    /* Input/uinput errors */
    {T500RS_ERR_UINPUT_OPEN, "Failed to open uinput"},
    {T500RS_ERR_UINPUT_CREATE, "Failed to create uinput device"},
    {T500RS_ERR_UINPUT_IOCTL, "uinput ioctl failed"},
    {T500RS_ERR_INPUT_READ, "Failed to read input"},
    
    /* Effect errors */
    {T500RS_ERR_EFFECT_INVALID_ID, "Invalid effect ID"},
    {T500RS_ERR_EFFECT_INVALID_TYPE, "Invalid effect type"},
    {T500RS_ERR_EFFECT_UPLOAD, "Effect upload failed"},
    {T500RS_ERR_EFFECT_NOT_UPLOADED, "Effect not uploaded"},
    {T500RS_ERR_EFFECT_START, "Failed to start effect"},
    {T500RS_ERR_EFFECT_STOP, "Failed to stop effect"},
    
    /* Thread errors */
    {T500RS_ERR_THREAD_CREATE, "Failed to create thread"},
    {T500RS_ERR_THREAD_JOIN, "Failed to join thread"},
    {T500RS_ERR_MUTEX_INIT, "Failed to initialize mutex"},
    {T500RS_ERR_MUTEX_LOCK, "Failed to lock mutex"},
    
    /* Configuration errors */
    {T500RS_ERR_CONFIG_LOAD, "Failed to load configuration"},
    {T500RS_ERR_CONFIG_SAVE, "Failed to save configuration"},
    {T500RS_ERR_CONFIG_INVALID, "Invalid configuration value"},
    
    /* Memory errors */
    {T500RS_ERR_OUT_OF_MEMORY, "Out of memory"},
    {T500RS_ERR_BUFFER_TOO_SMALL, "Buffer too small"},
    
    /* General errors */
    {T500RS_ERR_INVALID_PARAM, "Invalid parameter"},
    {T500RS_ERR_NOT_INITIALIZED, "Driver not initialized"},
    {T500RS_ERR_ALREADY_RUNNING, "Driver already running"},
    {T500RS_ERR_PERMISSION, "Permission denied (try running as root)"},
    {T500RS_ERR_UNKNOWN, "Unknown error"},
};

#define NUM_ERROR_MESSAGES (sizeof(error_messages) / sizeof(error_messages[0]))

/**
 * @brief Get human-readable error message
 */
const char *t500rs_error_string(t500rs_error_t error)
{
    for (size_t i = 0; i < NUM_ERROR_MESSAGES; i++) {
        if (error_messages[i].code == error) {
            return error_messages[i].message;
        }
    }
    
    return "Unknown error code";
}

/**
 * @brief Log an error with context
 */
void t500rs_log_error(t500rs_error_t error, const char *context)
{
    const char *msg = t500rs_error_string(error);
    
    if (context) {
        LOG_ERROR("%s: %s (error code: %d)", context, msg, error);
    } else {
        LOG_ERROR("%s (error code: %d)", msg, error);
    }
}

