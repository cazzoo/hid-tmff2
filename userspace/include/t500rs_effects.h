/**
 * @file t500rs_effects.h
 * @brief Force feedback effect management
 * 
 * This module handles uploading, starting, stopping, and managing
 * force feedback effects including:
 * - Constant force
 * - Periodic effects (sine, triangle, square, sawtooth)
 * - Condition effects (spring, damper, friction, inertia)
 * - Ramp effects (disabled due to firmware limitation)
 */

#ifndef T500RS_EFFECTS_H
#define T500RS_EFFECTS_H

#include "t500rs_common.h"

/* ============================================================================
 * Effect Upload
 * ============================================================================ */

/**
 * @brief Upload constant force effect to device
 * 
 * @param id Effect ID (0-63)
 * @param effect Linux FF effect structure
 * @return 0 on success, negative on error
 */
int upload_constant_effect(int id, struct ff_effect *effect);

/**
 * @brief Upload periodic effect to device
 * 
 * @param id Effect ID (0-63)
 * @param effect Linux FF effect structure
 * @return 0 on success, negative on error
 */
int upload_periodic_effect(int id, struct ff_effect *effect);

/**
 * @brief Upload condition effect to device
 * 
 * @param id Effect ID (0-63)
 * @param effect Linux FF effect structure
 * @return 0 on success, negative on error
 */
int upload_condition_effect(int id, struct ff_effect *effect);

/**
 * @brief Upload ramp effect to device
 * 
 * Note: Ramp effects are disabled (ENABLE_RAMP_EFFECTS=0) due to
 * firmware limitation that causes device to enter safe mode.
 * 
 * @param id Effect ID (0-63)
 * @param effect Linux FF effect structure
 * @return 0 on success, negative on error
 */
int upload_ramp_effect(int id, struct ff_effect *effect);

/* ============================================================================
 * Effect Control
 * ============================================================================ */

/**
 * @brief Start playing an effect
 * 
 * @param id Effect ID (0-63)
 * @return 0 on success, negative on error
 */
int start_effect(int id);

/**
 * @brief Stop playing an effect
 * 
 * @param id Effect ID (0-63)
 * @return 0 on success, negative on error
 */
int stop_effect(int id);

/* ============================================================================
 * Gain Control
 * ============================================================================ */

/**
 * @brief Set global gain (master volume for all effects)
 * 
 * @param gain Gain value (0-65535, where 65535 = 100%)
 * @return 0 on success, negative on error
 */
int set_gain(uint16_t gain);

/**
 * @brief Set autocenter strength
 * 
 * @param strength Autocenter strength (0-65535)
 * @return 0 on success, negative on error
 */
int set_autocenter(uint16_t strength);

/**
 * @brief Apply per-effect-type gain
 * 
 * Applies gain scaling specific to effect type. This allows different
 * effect types to have different strength multipliers.
 * 
 * @param force Input force level
 * @param effect_type Effect type (FF_CONSTANT, FF_PERIODIC, etc.)
 * @return Scaled force level
 */
int apply_effect_gain(int force, int effect_type);

#endif /* T500RS_EFFECTS_H */

