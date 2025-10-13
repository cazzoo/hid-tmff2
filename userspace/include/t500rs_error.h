/**
 * @file t500rs_error.h
 * @brief Error codes and error handling for T500RS driver
 * 
 * This file defines all error codes used throughout the driver
 * and provides utilities for error handling and reporting.
 */

#ifndef T500RS_ERROR_H
#define T500RS_ERROR_H

/**
 * @brief Error codes for T500RS driver
 * 
 * All functions that can fail should return one of these codes.
 * Success is indicated by T500RS_OK (0).
 * All error codes are negative.
 */
typedef enum {
    /* Success */
    T500RS_OK = 0,                          /**< Operation successful */
    
    /* USB errors (-1 to -99) */
    T500RS_ERR_USB_INIT = -1,               /**< USB initialization failed */
    T500RS_ERR_USB_DEVICE_NOT_FOUND = -2,   /**< T500RS device not found */
    T500RS_ERR_USB_OPEN = -3,               /**< Failed to open USB device */
    T500RS_ERR_USB_CLAIM = -4,              /**< Failed to claim USB interface */
    T500RS_ERR_USB_TRANSFER = -5,           /**< USB transfer failed */
    T500RS_ERR_USB_TIMEOUT = -6,            /**< USB transfer timed out */
    T500RS_ERR_USB_DETACH = -7,             /**< Failed to detach kernel driver */
    
    /* Device errors (-100 to -199) */
    T500RS_ERR_DEVICE_MODE = -100,          /**< Failed to switch device mode */
    T500RS_ERR_DEVICE_INIT = -101,          /**< Device initialization failed */
    T500RS_ERR_DEVICE_NOT_READY = -102,     /**< Device not ready */
    T500RS_ERR_DEVICE_DISCONNECTED = -103,  /**< Device disconnected */
    
    /* Input/uinput errors (-200 to -299) */
    T500RS_ERR_UINPUT_OPEN = -200,          /**< Failed to open uinput */
    T500RS_ERR_UINPUT_CREATE = -201,        /**< Failed to create uinput device */
    T500RS_ERR_UINPUT_IOCTL = -202,         /**< uinput ioctl failed */
    T500RS_ERR_INPUT_READ = -203,           /**< Failed to read input */
    
    /* Effect errors (-300 to -399) */
    T500RS_ERR_EFFECT_INVALID_ID = -300,    /**< Invalid effect ID */
    T500RS_ERR_EFFECT_INVALID_TYPE = -301,  /**< Invalid effect type */
    T500RS_ERR_EFFECT_UPLOAD = -302,        /**< Effect upload failed */
    T500RS_ERR_EFFECT_NOT_UPLOADED = -303,  /**< Effect not uploaded */
    T500RS_ERR_EFFECT_START = -304,         /**< Failed to start effect */
    T500RS_ERR_EFFECT_STOP = -305,          /**< Failed to stop effect */
    
    /* Thread errors (-400 to -499) */
    T500RS_ERR_THREAD_CREATE = -400,        /**< Failed to create thread */
    T500RS_ERR_THREAD_JOIN = -401,          /**< Failed to join thread */
    T500RS_ERR_MUTEX_INIT = -402,           /**< Failed to initialize mutex */
    T500RS_ERR_MUTEX_LOCK = -403,           /**< Failed to lock mutex */
    
    /* Configuration errors (-500 to -599) */
    T500RS_ERR_CONFIG_LOAD = -500,          /**< Failed to load config */
    T500RS_ERR_CONFIG_SAVE = -501,          /**< Failed to save config */
    T500RS_ERR_CONFIG_INVALID = -502,       /**< Invalid configuration value */
    
    /* Memory errors (-600 to -699) */
    T500RS_ERR_OUT_OF_MEMORY = -600,        /**< Out of memory */
    T500RS_ERR_BUFFER_TOO_SMALL = -601,     /**< Buffer too small */
    
    /* General errors (-700 to -799) */
    T500RS_ERR_INVALID_PARAM = -700,        /**< Invalid parameter */
    T500RS_ERR_NOT_INITIALIZED = -701,      /**< Driver not initialized */
    T500RS_ERR_ALREADY_RUNNING = -702,      /**< Driver already running */
    T500RS_ERR_PERMISSION = -703,           /**< Permission denied */
    T500RS_ERR_UNKNOWN = -999,              /**< Unknown error */
} t500rs_error_t;

/**
 * @brief Get human-readable error message
 * @param error Error code
 * @return Error message string (never NULL)
 */
const char *t500rs_error_string(t500rs_error_t error);

/**
 * @brief Log an error with context
 * @param error Error code
 * @param context Additional context string (can be NULL)
 */
void t500rs_log_error(t500rs_error_t error, const char *context);

/**
 * @brief Check if error code indicates success
 * @param error Error code to check
 * @return true if success, false if error
 */
static inline int t500rs_is_success(t500rs_error_t error) {
    return error == T500RS_OK;
}

/**
 * @brief Check if error code indicates failure
 * @param error Error code to check
 * @return true if error, false if success
 */
static inline int t500rs_is_error(t500rs_error_t error) {
    return error != T500RS_OK;
}

#endif /* T500RS_ERROR_H */

