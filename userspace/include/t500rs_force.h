/**
 * @file t500rs_force.h
 * @brief Force calculation and processing
 * 
 * This module handles advanced force feedback processing including:
 * - Envelope calculation (attack/fade)
 * - Force smoothing (exponential)
 * - Multi-effect mixing
 * - Dynamic update rate optimization
 */

#ifndef T500RS_FORCE_H
#define T500RS_FORCE_H

#include "t500rs_common.h"

/* ============================================================================
 * Envelope Processing
 * ============================================================================ */

/**
 * @brief Apply envelope (attack/fade) to force level
 * 
 * Calculates envelope modulation based on elapsed time:
 * - Attack phase: Ramps from attack_level to full force
 * - Sustain phase: Full force maintained
 * - Fade phase: Ramps from full force to fade_level
 * 
 * @param force_level Base force level (before envelope)
 * @param state Effect state containing envelope parameters
 * @return Force level after envelope applied
 */
int apply_envelope(int force_level, struct effect_state *state);

/* ============================================================================
 * Force Smoothing
 * ============================================================================ */

/**
 * @brief Apply exponential smoothing to prevent sudden force jumps
 * 
 * Uses exponential smoothing: new = old + (target - old) * factor
 * Smoothing factor is 0.3 for good balance between responsiveness
 * and smoothness.
 * 
 * @param target_force Target force level
 * @param last_force Last force level sent to device
 * @return Smoothed force level
 */
int apply_force_smoothing(int target_force, int last_force);

/* ============================================================================
 * Multi-Effect Mixing
 * ============================================================================ */

/**
 * @brief Mixing modes for combining multiple effects
 */
enum mix_mode {
    MIX_SIMPLE_ADD,     /* Simple addition (can overflow) */
    MIX_CLAMPED_ADD,    /* Add and clamp to valid range (DEFAULT) */
    MIX_WEIGHTED_AVG,   /* Weighted average */
    MIX_PRIORITY        /* Strongest effect wins */
};

/**
 * @brief Mix multiple force values together
 * 
 * Combines multiple simultaneous effects using specified mixing mode.
 * Default mode is CLAMPED_ADD which provides realistic physics with
 * overflow protection.
 * 
 * @param forces Array of force values to mix
 * @param count Number of forces in array
 * @param mode Mixing mode to use
 * @return Combined force value
 */
int mix_forces(int *forces, int count, enum mix_mode mode);

/* ============================================================================
 * Dynamic Update Rate
 * ============================================================================ */

/**
 * @brief Calculate optimal update interval based on force change rate
 * 
 * Dynamically adjusts update frequency:
 * - Large changes (>5000): 100Hz (10ms)
 * - Medium changes (>2000): 66Hz (15ms)
 * - Small changes (>500): 50Hz (20ms)
 * - Tiny changes (>100): 33Hz (30ms)
 * - No change: 25Hz (40ms)
 * 
 * This optimizes CPU usage while maintaining responsiveness.
 * 
 * @param force_delta Absolute change in force since last update
 * @return Update interval in microseconds
 */
unsigned int calculate_update_interval(int force_delta);

/* ============================================================================
 * Force Update Thread
 * ============================================================================ */

/**
 * @brief Start continuous force update thread
 * 
 * Starts background thread that continuously calculates and sends
 * force updates to the device. Handles envelope, smoothing, mixing,
 * and dynamic update rate.
 * 
 * @return 0 on success, negative on error
 */
int force_thread_start(void);

/**
 * @brief Stop continuous force update thread
 * 
 * Stops the background force update thread.
 */
void force_thread_stop(void);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Calculate elapsed time in milliseconds
 * 
 * @param start_time Starting timespec
 * @return Elapsed time in milliseconds
 */
unsigned long get_elapsed_ms(struct timespec *start_time);

#endif /* T500RS_FORCE_H */

