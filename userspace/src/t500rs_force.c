/**
 * @file t500rs_force.c
 * @brief Force calculation and processing
 * 
 * This module handles advanced force feedback processing including:
 * - Envelope calculation (attack/fade)
 * - Force smoothing (exponential)
 * - Multi-effect mixing
 * - Dynamic update rate optimization
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <math.h>
#include "../include/t500rs_force.h"
#include "../include/t500rs_common.h"
#include "../include/t500rs_usb.h"
#include "../include/t500rs_effects.h"

/* Force update thread */
static pthread_t force_update_thread = 0;
static int force_update_thread_running = 0;

/* Current update interval (microseconds) */
static unsigned int current_update_interval_us = 20000;  /* Start at 20ms (50Hz) */

/* External running flag (from main) */
extern int running;

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

unsigned long get_elapsed_ms(struct timespec *start_time)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    unsigned long elapsed_sec = now.tv_sec - start_time->tv_sec;
    long elapsed_nsec = now.tv_nsec - start_time->tv_nsec;

    return (elapsed_sec * 1000) + (elapsed_nsec / 1000000);
}

/* ============================================================================
 * Force Smoothing
 * ============================================================================ */

int apply_force_smoothing(int target_force, int last_force)
{
    /* Check if smoothing is enabled */
    if (!config.enable_force_smoothing) {
        return target_force;  /* No smoothing - return target directly */
    }

    /* Smoothing factor (0.3 * 65535) */
    const int smoothing_factor = 19660;

    /* Calculate force delta */
    int delta = target_force - last_force;

    /* Apply smoothing: new = old + delta * factor */
    int smoothed_delta = (delta * smoothing_factor) / 65535;
    int smoothed_force = last_force + smoothed_delta;

    /* For very small deltas, just use target to avoid drift */
    if (abs(delta) < 100) {
        smoothed_force = target_force;
    }

    return smoothed_force;
}

/* ============================================================================
 * Dynamic Update Rate
 * ============================================================================ */

unsigned int calculate_update_interval(int force_delta)
{
    /* Check if dynamic update rate is enabled */
    if (!config.enable_dynamic_update_rate) {
        return 20000;  /* Fixed 20ms = 50Hz when disabled */
    }

    int abs_delta = abs(force_delta);

    /* Thresholds for different update rates */
    if (abs_delta > 5000) {
        return 10000;  /* 10ms = 100Hz */
    } else if (abs_delta > 2000) {
        return 15000;  /* 15ms = 66Hz */
    } else if (abs_delta > 500) {
        return 20000;  /* 20ms = 50Hz */
    } else if (abs_delta > 100) {
        return 30000;  /* 30ms = 33Hz */
    } else {
        return 40000;  /* 40ms = 25Hz */
    }
}

/* ============================================================================
 * Multi-Effect Mixing
 * ============================================================================ */

int mix_forces(int *forces, int count, enum mix_mode mode)
{
    if (count == 0) return 0;
    if (count == 1) return forces[0];

    int result = 0;

    switch (mode) {
        case MIX_SIMPLE_ADD:
            /* Simple addition - can overflow */
            for (int i = 0; i < count; i++) {
                result += forces[i];
            }
            break;

        case MIX_CLAMPED_ADD:
            /* Add all forces and clamp to valid range */
            for (int i = 0; i < count; i++) {
                result += forces[i];
            }
            if (result > 32767) result = 32767;
            if (result < -32767) result = -32767;
            break;

        case MIX_WEIGHTED_AVG:
            /* Weighted average - prevents overflow naturally */
            for (int i = 0; i < count; i++) {
                result += forces[i];
            }
            result = result / count;
            break;

        case MIX_PRIORITY:
            /* Use strongest force only */
            result = forces[0];
            for (int i = 1; i < count; i++) {
                if (abs(forces[i]) > abs(result)) {
                    result = forces[i];
                }
            }
            break;

        default:
            /* Default to clamped add */
            for (int i = 0; i < count; i++) {
                result += forces[i];
            }
            if (result > 32767) result = 32767;
            if (result < -32767) result = -32767;
            break;
    }

    return result;
}

/* ============================================================================
 * Envelope Processing
 * ============================================================================ */

int apply_envelope(int force_level, struct effect_state *state)
{
    unsigned long elapsed_ms = get_elapsed_ms(&state->start_time);
    int adjusted_force = force_level;

    /* Attack phase - ramp up from attack_level to full force */
    if (state->attack_length_ms > 0 && elapsed_ms < state->attack_length_ms) {
        /* Calculate attack progress (0-65535) */
        unsigned int progress = (elapsed_ms * 65535) / state->attack_length_ms;

        /* Interpolate from attack_level to full force */
        int attack_force = (force_level * state->attack_level) / 65535;
        adjusted_force = attack_force + ((force_level - attack_force) * progress) / 65535;

        LOG_DEBUG("Envelope attack: progress=%u%%, force=%d->%d",
                  (progress * 100) / 65535, force_level, adjusted_force);
    }
    /* Fade phase - ramp down from full force to fade_level */
    else if (state->fade_length_ms > 0 && state->duration_ms > 0) {
        unsigned long fade_start_ms = state->duration_ms - state->fade_length_ms;

        if (elapsed_ms >= fade_start_ms) {
            /* Calculate fade progress (0-65535) */
            unsigned long fade_elapsed = elapsed_ms - fade_start_ms;
            unsigned int progress = (fade_elapsed * 65535) / state->fade_length_ms;

            /* Interpolate from full force to fade_level */
            int fade_force = (force_level * state->fade_level) / 65535;
            adjusted_force = force_level - ((force_level - fade_force) * progress) / 65535;

            LOG_DEBUG("Envelope fade: progress=%u%%, force=%d->%d",
                      (progress * 100) / 65535, force_level, adjusted_force);
        }
    }

    return adjusted_force;
}

/* ============================================================================
 * Force Update Thread
 * ============================================================================ */

static void *force_update_thread_func(void *arg)
{
    (void)arg;  /* Unused */
    unsigned char buf[4];
    int loop_count = 0;

    LOG_INFO("Force update thread started");

    while (force_update_thread_running && running) {
        loop_count++;

        /* Log every 100 iterations to show thread is alive */
        if (loop_count % 100 == 0) {
            LOG_DEBUG("Force update thread alive (iteration %d)", loop_count);
        }
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

            /* Apply envelope (attack/fade) */
            force = apply_envelope(force, &effects[i]);

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
        }  /* End of for loop */

        pthread_mutex_unlock(&effects_lock);

        /* Sleep for update interval */
        usleep(20000);  /* 20ms = 50Hz */
    }

thread_exit:
    LOG_INFO("Force update thread stopped");
    return NULL;
}

int force_thread_start(void)
{
    force_update_thread_running = 1;

    if (pthread_create(&force_update_thread, NULL, force_update_thread_func, NULL) != 0) {
        LOG_ERROR("Failed to create force update thread");
        force_update_thread_running = 0;
        return -1;
    }

    LOG_INFO("Force update thread created");
    return 0;
}

void force_thread_stop(void)
{
    if (force_update_thread_running) {
        force_update_thread_running = 0;
        pthread_join(force_update_thread, NULL);
        LOG_INFO("Force update thread joined");
    }
}

