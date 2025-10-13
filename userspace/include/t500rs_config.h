/**
 * @file t500rs_config.h
 * @brief Configuration management for T500RS driver
 * 
 * This file provides configuration structures and default values
 * for the T500RS force feedback driver.
 */

#ifndef T500RS_CONFIG_H
#define T500RS_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Logging verbosity levels
 */
typedef enum {
    LOG_LEVEL_ERROR = 0,   /**< Only errors */
    LOG_LEVEL_WARN = 1,    /**< Warnings and errors */
    LOG_LEVEL_INFO = 2,    /**< Info, warnings, and errors */
    LOG_LEVEL_DEBUG = 3    /**< All messages including debug */
} log_level_t;

/**
 * @brief Force feedback configuration
 */
typedef struct {
    /* Force update settings */
    int update_rate_hz;           /**< Force update frequency (default: 50Hz) */
    bool skip_identical_updates;  /**< Skip sending identical force values (default: true) */
    
    /* Gain settings */
    uint16_t default_gain;        /**< Default master gain 0-65535 (default: 65535) */
    bool ignore_zero_gain;        /**< Ignore gain=0 commands (default: true) */
    
    /* Effect settings */
    int autocenter_effect_id;     /**< Effect slot for autocenter (default: 15) */
    int min_autocenter_strength;  /**< Minimum autocenter strength (default: 10) */
    
    /* Performance settings */
    int force_thread_priority;    /**< Force update thread priority (default: 0) */
    int input_thread_priority;    /**< Input reading thread priority (default: 0) */
} ffb_config_t;

/**
 * @brief USB communication configuration
 */
typedef struct {
    int timeout_ms;               /**< USB transfer timeout (default: 1000ms) */
    int max_retries;              /**< Max retry attempts for failed transfers (default: 3) */
    bool hex_debug;               /**< Enable USB hex dumps (default: false) */
} usb_config_t;

/**
 * @brief Logging configuration
 */
typedef struct {
    log_level_t level;            /**< Current log level (default: INFO) */
    bool use_colors;              /**< Use ANSI colors in output (default: true) */
    bool show_timestamps;         /**< Show timestamps in logs (default: false) */
    const char *log_file;         /**< Log file path (NULL = stdout only) */
} log_config_t;

/**
 * @brief Complete driver configuration
 */
typedef struct {
    ffb_config_t ffb;             /**< Force feedback configuration */
    usb_config_t usb;             /**< USB configuration */
    log_config_t log;             /**< Logging configuration */
} driver_config_t;

/**
 * @brief Get default driver configuration
 * @return Default configuration structure
 */
driver_config_t get_default_config(void);

/**
 * @brief Load configuration from file
 * @param filename Path to configuration file
 * @param config Pointer to configuration structure to fill
 * @return 0 on success, -1 on error
 */
int load_config_from_file(const char *filename, driver_config_t *config);

/**
 * @brief Save configuration to file
 * @param filename Path to configuration file
 * @param config Pointer to configuration structure to save
 * @return 0 on success, -1 on error
 */
int save_config_to_file(const char *filename, const driver_config_t *config);

/**
 * @brief Print current configuration
 * @param config Pointer to configuration structure
 */
void print_config(const driver_config_t *config);

/* Global configuration instance */
extern driver_config_t g_config;

#endif /* T500RS_CONFIG_H */

