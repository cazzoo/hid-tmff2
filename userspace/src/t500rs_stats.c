/**
 * @file t500rs_stats.c
 * @brief Performance statistics implementation
 */

#include "t500rs_stats.h"
#include "t500rs_logging.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

/* Global statistics */
static t500rs_stats_t g_stats;

/**
 * @brief Initialize statistics
 */
void stats_init(void)
{
    memset(&g_stats, 0, sizeof(g_stats));
    clock_gettime(CLOCK_MONOTONIC, &g_stats.start_time);
}

/**
 * @brief Reset statistics
 */
void stats_reset(void)
{
    stats_init();
}

/**
 * @brief Get current statistics
 */
const t500rs_stats_t *stats_get(void)
{
    return &g_stats;
}

/**
 * @brief Update uptime
 */
void stats_update_uptime(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    g_stats.uptime_seconds = now.tv_sec - g_stats.start_time.tv_sec;
}

/**
 * @brief Print statistics
 */
void stats_print(void)
{
    stats_update_uptime();
    
    printf("\n");
    printf("========================================\n");
    printf("T500RS Driver Statistics\n");
    printf("========================================\n\n");
    
    printf("Uptime: %lu seconds (%.1f minutes)\n\n", 
           g_stats.uptime_seconds, g_stats.uptime_seconds / 60.0);
    
    printf("USB Statistics:\n");
    printf("  Total sends: %lu\n", g_stats.usb_sends_total);
    printf("  Failed sends: %lu\n", g_stats.usb_sends_failed);
    printf("  Skipped sends: %lu (%.1f%%)\n", 
           g_stats.usb_sends_skipped,
           g_stats.usb_sends_total > 0 ? 
               (g_stats.usb_sends_skipped * 100.0 / g_stats.usb_sends_total) : 0);
    printf("  Bytes sent: %lu (%.2f KB)\n", 
           g_stats.usb_bytes_sent, g_stats.usb_bytes_sent / 1024.0);
    printf("\n");
    
    printf("Effect Statistics:\n");
    printf("  Uploaded: %lu\n", g_stats.effects_uploaded);
    printf("  Started: %lu\n", g_stats.effects_started);
    printf("  Stopped: %lu\n", g_stats.effects_stopped);
    printf("  Currently active: %lu\n", g_stats.effects_active);
    printf("\n");
    
    printf("Force Update Statistics:\n");
    printf("  Total updates: %lu\n", g_stats.force_updates);
    printf("  Skipped updates: %lu (%.1f%%)\n",
           g_stats.force_updates_skipped,
           g_stats.force_updates > 0 ?
               (g_stats.force_updates_skipped * 100.0 / g_stats.force_updates) : 0);
    if (g_stats.force_updates > 0) {
        printf("  Avg update time: %.2f µs\n", g_stats.avg_force_update_time_us);
        printf("  Max update time: %.2f µs\n", g_stats.max_force_update_time_us);
    }
    printf("\n");
    
    printf("Performance:\n");
    if (g_stats.uptime_seconds > 0) {
        printf("  USB sends/sec: %.1f\n", 
               g_stats.usb_sends_total / (double)g_stats.uptime_seconds);
        printf("  Force updates/sec: %.1f\n",
               g_stats.force_updates / (double)g_stats.uptime_seconds);
    }
    
    printf("\n========================================\n\n");
}

/* Statistics update functions */

void stats_usb_send(int success, size_t bytes)
{
    g_stats.usb_sends_total++;
    if (success) {
        g_stats.usb_bytes_sent += bytes;
    } else {
        g_stats.usb_sends_failed++;
    }
}

void stats_usb_skip(void)
{
    g_stats.usb_sends_skipped++;
}

void stats_effect_upload(void)
{
    g_stats.effects_uploaded++;
}

void stats_effect_start(void)
{
    g_stats.effects_started++;
    g_stats.effects_active++;
}

void stats_effect_stop(void)
{
    g_stats.effects_stopped++;
    if (g_stats.effects_active > 0) {
        g_stats.effects_active--;
    }
}

void stats_force_update(double time_us)
{
    g_stats.force_updates++;
    
    /* Update average (running average) */
    if (g_stats.force_updates == 1) {
        g_stats.avg_force_update_time_us = time_us;
    } else {
        g_stats.avg_force_update_time_us = 
            (g_stats.avg_force_update_time_us * (g_stats.force_updates - 1) + time_us) / 
            g_stats.force_updates;
    }
    
    /* Update max */
    if (time_us > g_stats.max_force_update_time_us) {
        g_stats.max_force_update_time_us = time_us;
    }
}

void stats_force_skip(void)
{
    g_stats.force_updates_skipped++;
}

