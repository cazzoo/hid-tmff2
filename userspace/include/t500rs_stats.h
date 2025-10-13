/**
 * @file t500rs_stats.h
 * @brief Performance statistics and monitoring
 * 
 * This file provides performance monitoring and statistics
 * for the T500RS driver.
 */

#ifndef T500RS_STATS_H
#define T500RS_STATS_H

#include <stdint.h>
#include <time.h>

/**
 * @brief Performance statistics
 */
typedef struct {
    /* USB statistics */
    uint64_t usb_sends_total;           /**< Total USB sends */
    uint64_t usb_sends_failed;          /**< Failed USB sends */
    uint64_t usb_sends_skipped;         /**< Skipped (identical) sends */
    uint64_t usb_bytes_sent;            /**< Total bytes sent */
    
    /* Effect statistics */
    uint64_t effects_uploaded;          /**< Effects uploaded */
    uint64_t effects_started;           /**< Effects started */
    uint64_t effects_stopped;           /**< Effects stopped */
    uint64_t effects_active;            /**< Currently active effects */
    
    /* Force update statistics */
    uint64_t force_updates;             /**< Force updates processed */
    uint64_t force_updates_skipped;     /**< Force updates skipped */
    
    /* Timing statistics */
    struct timespec start_time;         /**< Driver start time */
    uint64_t uptime_seconds;            /**< Uptime in seconds */
    
    /* Performance metrics */
    double avg_force_update_time_us;    /**< Average force update time (microseconds) */
    double max_force_update_time_us;    /**< Max force update time (microseconds) */
} t500rs_stats_t;

/**
 * @brief Initialize statistics
 */
void stats_init(void);

/**
 * @brief Reset statistics
 */
void stats_reset(void);

/**
 * @brief Get current statistics
 * @return Pointer to statistics structure
 */
const t500rs_stats_t *stats_get(void);

/**
 * @brief Print statistics to stdout
 */
void stats_print(void);

/**
 * @brief Update uptime
 */
void stats_update_uptime(void);

/* Statistics update functions */
void stats_usb_send(int success, size_t bytes);
void stats_usb_skip(void);
void stats_effect_upload(void);
void stats_effect_start(void);
void stats_effect_stop(void);
void stats_force_update(double time_us);
void stats_force_skip(void);

#endif /* T500RS_STATS_H */

