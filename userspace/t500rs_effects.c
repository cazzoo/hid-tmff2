/*
 * T500RS Windows-Compatible Effect Translation Layer
 *
 * Translates Linux FF effects to Windows driver-compatible HID commands
 * based on Ghidra reverse engineering analysis
 *
 * Copyright (C) 2025
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <linux/input.h>

#include "t500rs_protocol.h"

/* Logging macros */
#define LOG_INFO(fmt, ...) fprintf(stdout, "[EFFECTS] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) fprintf(stderr, "[EFFECTS] " fmt "\n", ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) fprintf(stdout, "[EFFECTS DEBUG] " fmt "\n", ##__VA_ARGS__)

/* Translate constant force effect to Windows HID command */
int t500rs_translate_constant_effect(struct ff_effect *effect, 
                                     struct t500rs_hid_output *cmd,
                                     int apply_gain)
{
    if (!effect || !cmd) {
        return -EINVAL;
    }
    
    /* Apply per-effect-type gain if requested */
    int level = effect->u.constant.level;
    if (apply_gain) {
        level = apply_effect_gain(level, FF_CONSTANT);
    }
    
    LOG_DEBUG("Constant effect: level=%d (after gain: %d)", 
              effect->u.constant.level, level);
    
    /* Construct Windows-style constant force command */
    memset(cmd, 0, sizeof(*cmd));
    cmd->report_id = T500RS_REPORT_ID;
    cmd->command_type = T500RS_CMD_FF_PRIMARY;  /* 0x03 for constant forces */
    
    /* Parameter is the force magnitude */
    uint16_t magnitude = abs(level);
    cmd->parameter = htole16(magnitude);
    
    /* Flags indicate direction: bit 0 = negative force */
    cmd->flags = (level < 0) ? 0x01 : 0x00;
    
    LOG_DEBUG("Constant command: magnitude=%u, flags=0x%02x", magnitude, cmd->flags);
    
    return 0;
}

/* Translate periodic effect (sine, square, triangle, saw) to Windows HID command */
int t500rs_translate_periodic_effect(struct ff_effect *effect,
                                     struct t500rs_hid_output *cmd,
                                     int apply_gain)
{
    if (!effect || !cmd) {
        return -EINVAL;
    }
    
    /* Apply per-effect-type gain if requested */
    int magnitude = effect->u.periodic.magnitude;
    if (apply_gain) {
        magnitude = apply_effect_gain(magnitude, FF_PERIODIC);
    }
    
    LOG_DEBUG("Periodic effect: type=%d, magnitude=%d (after gain: %d), period=%u",
              effect->u.periodic.waveform, effect->u.periodic.magnitude, 
              magnitude, effect->u.periodic.period);
    
    /* Construct Windows-style periodic force command */
    memset(cmd, 0, sizeof(*cmd));
    cmd->report_id = T500RS_REPORT_ID;
    cmd->command_type = T500RS_CMD_FF_EXTENDED;  /* 0x11 for periodic effects */
    
    /* Parameter encodes magnitude */
    uint16_t mag = abs(magnitude);
    cmd->parameter = htole16(mag);
    
    /* Flags encode waveform type and direction */
    uint8_t waveform_type = 0;
    switch (effect->u.periodic.waveform) {
    case FF_SINE:
        waveform_type = 0x00;
        break;
    case FF_SQUARE:
        waveform_type = 0x01;
        break;
    case FF_TRIANGLE:
        waveform_type = 0x02;
        break;
    case FF_SAW_UP:
        waveform_type = 0x03;
        break;
    case FF_SAW_DOWN:
        waveform_type = 0x04;
        break;
    default:
        waveform_type = 0x00;  /* Default to sine */
    }
    
    cmd->flags = waveform_type;
    if (magnitude < 0) {
        cmd->flags |= 0x80;  /* High bit indicates negative */
    }
    
    /* Encode period in payload (milliseconds) */
    uint16_t period_ms = effect->u.periodic.period;
    cmd->payload[0] = period_ms & 0xFF;
    cmd->payload[1] = (period_ms >> 8) & 0xFF;
    
    /* Encode phase offset if non-zero */
    if (effect->u.periodic.phase > 0) {
        uint16_t phase = effect->u.periodic.phase;
        cmd->payload[2] = phase & 0xFF;
        cmd->payload[3] = (phase >> 8) & 0xFF;
    }
    
    LOG_DEBUG("Periodic command: waveform=%u, magnitude=%u, period=%u ms, flags=0x%02x",
              waveform_type, mag, period_ms, cmd->flags);
    
    return 0;
}

/* Translate spring effect to Windows HID command */
int t500rs_translate_spring_effect(struct ff_effect *effect,
                                   struct t500rs_hid_output *cmd,
                                   int apply_gain)
{
    if (!effect || !cmd) {
        return -EINVAL;
    }
    
    /* Spring effects use condition parameters */
    struct ff_condition_effect *cond = &effect->u.condition[0];
    
    /* Apply per-effect-type gain if requested */
    int right_coeff = cond->right_coeff;
    int left_coeff = cond->left_coeff;
    
    if (apply_gain) {
        right_coeff = apply_effect_gain(right_coeff, FF_SPRING);
        left_coeff = apply_effect_gain(left_coeff, FF_SPRING);
    }
    
    LOG_DEBUG("Spring effect: right_coeff=%d, left_coeff=%d, center=%d, deadband=%u",
              right_coeff, left_coeff, cond->center, cond->deadband);
    
    /* Construct Windows-style spring command */
    memset(cmd, 0, sizeof(*cmd));
    cmd->report_id = T500RS_REPORT_ID;
    cmd->command_type = T500RS_CMD_FF_SECONDARY;  /* 0x04 for conditional effects */
    
    /* Parameter encodes spring coefficient (average of left/right) */
    int avg_coeff = (abs(right_coeff) + abs(left_coeff)) / 2;
    cmd->parameter = htole16(avg_coeff);
    
    /* Flags indicate spring type */
    cmd->flags = 0x01;  /* Spring effect type */
    
    /* Encode coefficients in payload */
    cmd->payload[0] = abs(right_coeff) & 0xFF;
    cmd->payload[1] = abs(left_coeff) & 0xFF;
    
    /* Encode saturation levels */
    cmd->payload[2] = (cond->right_saturation >> 8) & 0xFF;
    cmd->payload[3] = (cond->left_saturation >> 8) & 0xFF;
    
    /* Encode center position and deadband */
    int16_t center = cond->center;
    cmd->payload[4] = center & 0xFF;
    cmd->payload[5] = (center >> 8) & 0xFF;
    
    uint16_t deadband = cond->deadband;
    cmd->payload[6] = deadband & 0xFF;
    cmd->payload[7] = (deadband >> 8) & 0xFF;
    
    LOG_DEBUG("Spring command: avg_coeff=%d, flags=0x%02x", avg_coeff, cmd->flags);
    
    return 0;
}

/* Translate damper effect to Windows HID command */
int t500rs_translate_damper_effect(struct ff_effect *effect,
                                   struct t500rs_hid_output *cmd,
                                   int apply_gain)
{
    if (!effect || !cmd) {
        return -EINVAL;
    }
    
    struct ff_condition_effect *cond = &effect->u.condition[0];
    
    /* Apply per-effect-type gain if requested */
    int right_coeff = cond->right_coeff;
    int left_coeff = cond->left_coeff;
    
    if (apply_gain) {
        right_coeff = apply_effect_gain(right_coeff, FF_DAMPER);
        left_coeff = apply_effect_gain(left_coeff, FF_DAMPER);
    }
    
    LOG_DEBUG("Damper effect: right_coeff=%d, left_coeff=%d",
              right_coeff, left_coeff);
    
    /* Damper uses same structure as spring but different type flag */
    memset(cmd, 0, sizeof(*cmd));
    cmd->report_id = T500RS_REPORT_ID;
    cmd->command_type = T500RS_CMD_FF_SECONDARY;
    
    int avg_coeff = (abs(right_coeff) + abs(left_coeff)) / 2;
    cmd->parameter = htole16(avg_coeff);
    
    /* Flags indicate damper type */
    cmd->flags = 0x02;  /* Damper effect type */
    
    /* Encode coefficients */
    cmd->payload[0] = abs(right_coeff) & 0xFF;
    cmd->payload[1] = abs(left_coeff) & 0xFF;
    cmd->payload[2] = (cond->right_saturation >> 8) & 0xFF;
    cmd->payload[3] = (cond->left_saturation >> 8) & 0xFF;
    
    LOG_DEBUG("Damper command: avg_coeff=%d, flags=0x%02x", avg_coeff, cmd->flags);
    
    return 0;
}

/* Translate friction effect to Windows HID command */
int t500rs_translate_friction_effect(struct ff_effect *effect,
                                     struct t500rs_hid_output *cmd,
                                     int apply_gain)
{
    if (!effect || !cmd) {
        return -EINVAL;
    }
    
    struct ff_condition_effect *cond = &effect->u.condition[0];
    
    int right_coeff = cond->right_coeff;
    int left_coeff = cond->left_coeff;
    
    if (apply_gain) {
        right_coeff = apply_effect_gain(right_coeff, FF_FRICTION);
        left_coeff = apply_effect_gain(left_coeff, FF_FRICTION);
    }
    
    LOG_DEBUG("Friction effect: right_coeff=%d, left_coeff=%d",
              right_coeff, left_coeff);
    
    memset(cmd, 0, sizeof(*cmd));
    cmd->report_id = T500RS_REPORT_ID;
    cmd->command_type = T500RS_CMD_FF_SECONDARY;
    
    int avg_coeff = (abs(right_coeff) + abs(left_coeff)) / 2;
    cmd->parameter = htole16(avg_coeff);
    
    /* Flags indicate friction type */
    cmd->flags = 0x03;  /* Friction effect type */
    
    cmd->payload[0] = abs(right_coeff) & 0xFF;
    cmd->payload[1] = abs(left_coeff) & 0xFF;
    cmd->payload[2] = (cond->right_saturation >> 8) & 0xFF;
    cmd->payload[3] = (cond->left_saturation >> 8) & 0xFF;
    
    LOG_DEBUG("Friction command: avg_coeff=%d, flags=0x%02x", avg_coeff, cmd->flags);
    
    return 0;
}

/* Translate inertia effect to Windows HID command */
int t500rs_translate_inertia_effect(struct ff_effect *effect,
                                    struct t500rs_hid_output *cmd,
                                    int apply_gain)
{
    if (!effect || !cmd) {
        return -EINVAL;
    }
    
    struct ff_condition_effect *cond = &effect->u.condition[0];
    
    int right_coeff = cond->right_coeff;
    int left_coeff = cond->left_coeff;
    
    if (apply_gain) {
        right_coeff = apply_effect_gain(right_coeff, FF_INERTIA);
        left_coeff = apply_effect_gain(left_coeff, FF_INERTIA);
    }
    
    LOG_DEBUG("Inertia effect: right_coeff=%d, left_coeff=%d",
              right_coeff, left_coeff);
    
    memset(cmd, 0, sizeof(*cmd));
    cmd->report_id = T500RS_REPORT_ID;
    cmd->command_type = T500RS_CMD_FF_SECONDARY;
    
    int avg_coeff = (abs(right_coeff) + abs(left_coeff)) / 2;
    cmd->parameter = htole16(avg_coeff);
    
    /* Flags indicate inertia type */
    cmd->flags = 0x04;  /* Inertia effect type */
    
    cmd->payload[0] = abs(right_coeff) & 0xFF;
    cmd->payload[1] = abs(left_coeff) & 0xFF;
    cmd->payload[2] = (cond->right_saturation >> 8) & 0xFF;
    cmd->payload[3] = (cond->left_saturation >> 8) & 0xFF;
    
    LOG_DEBUG("Inertia command: avg_coeff=%d, flags=0x%02x", avg_coeff, cmd->flags);
    
    return 0;
}

/* Apply envelope to effect level - Windows driver style */
int t500rs_apply_envelope(struct ff_envelope *envelope, 
                          unsigned long elapsed_ms,
                          int base_level)
{
    if (!envelope) {
        return base_level;
    }
    
    /* No envelope if attack and fade are both zero */
    if (envelope->attack_length == 0 && envelope->fade_length == 0) {
        return base_level;
    }
    
    int level = base_level;
    
    /* Attack phase */
    if (envelope->attack_length > 0 && elapsed_ms < envelope->attack_length) {
        /* Ramp from attack_level to base_level */
        float progress = (float)elapsed_ms / envelope->attack_length;
        level = envelope->attack_level + 
                (int)((base_level - envelope->attack_level) * progress);
        
        LOG_DEBUG("Attack phase: elapsed=%lu ms, progress=%.2f, level=%d",
                  elapsed_ms, progress, level);
    }
    /* Fade phase (calculated from end of effect) */
    else if (envelope->fade_length > 0) {
        /* Note: fade calculation requires effect duration, 
         * which should be passed by caller if needed */
        LOG_DEBUG("Fade phase logic would apply here");
    }
    
    return level;
}

/* Master effect translation function - Windows driver compatible */
int t500rs_translate_effect(struct ff_effect *effect,
                            struct t500rs_hid_output *cmd,
                            int apply_gain)
{
    if (!effect || !cmd) {
        LOG_ERROR("Invalid parameters to t500rs_translate_effect");
        return -EINVAL;
    }
    
    int ret = 0;
    
    LOG_INFO("Translating effect: type=%d, id=%d", effect->type, effect->id);
    
    switch (effect->type) {
    case FF_CONSTANT:
        ret = t500rs_translate_constant_effect(effect, cmd, apply_gain);
        break;
        
    case FF_PERIODIC:
        ret = t500rs_translate_periodic_effect(effect, cmd, apply_gain);
        break;
        
    case FF_SPRING:
        ret = t500rs_translate_spring_effect(effect, cmd, apply_gain);
        break;
        
    case FF_DAMPER:
        ret = t500rs_translate_damper_effect(effect, cmd, apply_gain);
        break;
        
    case FF_FRICTION:
        ret = t500rs_translate_friction_effect(effect, cmd, apply_gain);
        break;
        
    case FF_INERTIA:
        ret = t500rs_translate_inertia_effect(effect, cmd, apply_gain);
        break;
        
    case FF_RAMP:
        LOG_ERROR("Ramp effects not yet supported in Windows protocol");
        ret = -ENOSYS;
        break;
        
    default:
        LOG_ERROR("Unsupported effect type: %d", effect->type);
        ret = -EINVAL;
        break;
    }
    
    if (ret == 0) {
        LOG_INFO("✅ Effect translation successful");
    } else {
        LOG_ERROR("❌ Effect translation failed: %d", ret);
    }
    
    return ret;
}