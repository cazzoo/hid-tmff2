/**
 * @file t500rs_effects.c
 * @brief Force feedback effect management
 * 
 * This module handles uploading, starting, stopping, and managing
 * force feedback effects including:
 * - Constant force
 * - Periodic effects (sine, triangle, square, sawtooth)
 * - Condition effects (spring, damper, friction, inertia)
 * - Ramp effects (disabled due to firmware limitation)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include "../include/t500rs_effects.h"
#include "../include/t500rs_common.h"
#include "../include/t500rs_usb.h"

/* Per-effect-type gain multipliers (0.0 to 1.0) */
static float effect_type_gains[128] = {
    [FF_CONSTANT] = 1.0,
    [FF_PERIODIC] = 1.0,
    [FF_SPRING] = 1.0,
    [FF_DAMPER] = 1.0,
    [FF_FRICTION] = 1.0,
    [FF_INERTIA] = 1.0,
    [FF_RAMP] = 1.0,
};

/* ============================================================================
 * Gain Control
 * ============================================================================ */

int apply_effect_gain(int force, int effect_type)
{
    if (effect_type < 0 || effect_type >= 128) {
        return force;
    }
    
    /* Apply global gain */
    float global_gain = (float)current_gain / 65535.0f;
    
    /* Apply per-effect-type gain */
    float type_gain = effect_type_gains[effect_type];
    
    /* Combined gain */
    float combined_gain = global_gain * type_gain;
    
    /* Apply to force */
    int result = (int)(force * combined_gain);
    
    return result;
}

int set_gain(uint16_t gain)
{
    current_gain = gain;
    if (gain == 0) {
        LOG_INFO("WARNING: Global gain set to 0%% - NO FORCE WILL BE FELT!");
    } else {
        LOG_INFO("Global gain set to %u (%.1f%%)", gain, (gain * 100.0f) / 65535.0f);
    }
    return 0;
}

int set_autocenter(uint16_t strength)
{
    unsigned char buf[4];
    int ret;

    LOG_INFO("Setting autocenter to %u (%.1f%%)", strength, (strength * 100.0f) / 65535.0f);

    /* Report 0x43 - Autocenter
     * Format from captures:
     * buf[0] = 0x43 (report ID)
     * buf[1] = strength (0-255, scaled from 0-65535)
     * buf[2] = 0x00
     * buf[3] = 0x00
     */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x43;
    buf[1] = (strength * 255) / 65535;  /* Scale to 0-255 */
    buf[2] = 0x00;
    buf[3] = 0x00;

    ret = usb_send(buf, 4);
    if (ret) {
        LOG_ERROR("Failed to set autocenter");
        return ret;
    }

    return 0;
}

/* ============================================================================
 * Effect Upload
 * ============================================================================ */

int upload_constant_effect(int id, struct ff_effect *effect)
{
    unsigned char buf[15];
    int ret;

    /* Apply per-effect gain */
    int level = apply_effect_gain(effect->u.constant.level, FF_CONSTANT);

    LOG_DEBUG("Uploading constant effect %d, force=%d (after gain: %d)",
              id, effect->u.constant.level, level);

    /* Report 0x02 - Envelope (attack/fade) */
    unsigned short attack_len = effect->u.constant.envelope.attack_length;
    unsigned char attack_lvl = (effect->u.constant.envelope.attack_level * 127) / 65535;
    unsigned short fade_len = effect->u.constant.envelope.fade_length;
    unsigned char fade_lvl = (effect->u.constant.envelope.fade_level * 127) / 65535;

    memset(buf, 0, sizeof(buf));
    buf[0] = 0x02;
    buf[1] = 0x1c;
    buf[2] = 0x00;
    buf[3] = attack_len & 0xff;
    buf[4] = (attack_len >> 8) & 0xff;
    buf[5] = attack_lvl;
    buf[6] = fade_len & 0xff;
    buf[7] = (fade_len >> 8) & 0xff;
    buf[8] = fade_lvl;
    ret = usb_send(buf, 9);
    if (ret) return ret;
    usleep(5000);

    LOG_DEBUG("Envelope: attack=%ums@%u, fade=%ums@%u",
              attack_len, attack_lvl, fade_len, fade_lvl);

    /* Report 0x01 - Main effect upload */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x01;
    buf[1] = id;
    buf[2] = 0x00;  /* Constant force type */
    buf[3] = 0x40;
    buf[4] = 0x69;
    buf[5] = 0x23;
    buf[6] = 0x00;
    buf[7] = 0xff;
    buf[8] = 0xff;
    buf[9] = 0x0e;
    buf[10] = 0x00;
    buf[11] = 0x1c;
    buf[12] = 0x00;
    buf[13] = 0x00;
    buf[14] = 0x00;
    ret = usb_send(buf, 15);
    if (ret) return ret;

    LOG_DEBUG("Constant effect uploaded");

    return 0;
}

int upload_condition_effect(int id, struct ff_effect *effect)
{
    unsigned char buf[15];
    int ret;
    unsigned char effect_type;
    const char *type_name;

    /* Determine effect type */
    switch (effect->type) {
    case FF_SPRING:
        effect_type = 0x40;
        type_name = "spring";
        break;
    case FF_DAMPER:
        effect_type = 0x41;
        type_name = "damper";
        break;
    case FF_FRICTION:
        effect_type = 0x41;
        type_name = "friction";
        break;
    case FF_INERTIA:
        effect_type = 0x41;
        type_name = "inertia";
        break;
    default:
        LOG_ERROR("Unknown condition effect type: %d", effect->type);
        return -1;
    }

    /* Get coefficients and apply per-effect gain */
    int right_coeff = apply_effect_gain(effect->u.condition[0].right_coeff, effect->type);
    int left_coeff = apply_effect_gain(effect->u.condition[0].left_coeff, effect->type);

    /* Scale to 0-100 */
    unsigned char right_strength = (abs(right_coeff) * 100) / 32767;
    unsigned char left_strength = (abs(left_coeff) * 100) / 32767;

    /* Use default values if too low */
    if (right_strength < 10) right_strength = 50;
    if (left_strength < 10) left_strength = 50;

    LOG_DEBUG("Uploading %s effect %d, right=0x%02x, left=0x%02x",
              type_name, id, right_strength, left_strength);

    /* Report 0x05 - Condition parameters (coefficients) */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x05;
    buf[1] = 0x0e;
    buf[2] = 0x00;
    buf[3] = right_strength;
    buf[4] = left_strength;
    buf[5] = 0x00;
    buf[6] = 0x00;
    buf[7] = 0x00;
    buf[8] = 0x00;
    buf[9] = (effect->type == FF_SPRING) ? 0x54 : 0x64;
    buf[10] = (effect->type == FF_SPRING) ? 0x54 : 0x64;
    ret = usb_send(buf, 11);
    if (ret) return ret;
    usleep(5000);

    /* Report 0x05 - Condition parameters (deadband/center) */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x05;
    buf[1] = 0x1c;
    buf[2] = 0x00;
    buf[3] = 0x00;  /* Deadband */
    buf[4] = 0x00;  /* Center */
    buf[5] = 0x00;
    buf[6] = 0x00;
    buf[7] = 0x00;
    buf[8] = 0x00;
    buf[9] = (effect->type == FF_SPRING) ? 0x46 : 0x64;
    buf[10] = (effect->type == FF_SPRING) ? 0x54 : 0x64;
    ret = usb_send(buf, 11);
    if (ret) return ret;
    usleep(5000);

    /* Report 0x01 - Main effect upload */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x01;
    buf[1] = id;
    buf[2] = effect_type;
    buf[3] = 0x40;
    buf[4] = 0x17;
    buf[5] = 0x25;
    buf[6] = 0x00;
    buf[7] = 0xff;
    buf[8] = 0xff;
    buf[9] = 0x0e;
    buf[10] = 0x00;
    buf[11] = 0x1c;
    buf[12] = 0x00;
    buf[13] = 0x00;
    buf[14] = 0x00;
    ret = usb_send(buf, 15);
    if (ret) return ret;

    LOG_DEBUG("%s effect uploaded", type_name);

    return 0;
}

int upload_periodic_effect(int id, struct ff_effect *effect)
{
    unsigned char buf[15];
    int ret;
    unsigned char effect_type;
    const char *type_name;

    /* Apply per-effect gain to magnitude */
    int magnitude = apply_effect_gain(effect->u.periodic.magnitude, FF_PERIODIC);

    /* Determine waveform type */
    switch (effect->u.periodic.waveform) {
    case FF_SQUARE:
        effect_type = 0x20;
        type_name = "square";
        break;
    case FF_TRIANGLE:
        effect_type = 0x21;
        type_name = "triangle";
        break;
    case FF_SINE:
        effect_type = 0x22;
        type_name = "sine";
        break;
    case FF_SAW_UP:
        effect_type = 0x23;
        type_name = "sawtooth_up";
        break;
    case FF_SAW_DOWN:
        effect_type = 0x24;
        type_name = "sawtooth_down";
        break;
    default:
        LOG_ERROR("Unknown periodic waveform: %d", effect->u.periodic.waveform);
        return -1;
    }

    /* Magnitude - scale to 0-127 */
    unsigned char mag = (abs(magnitude) * 127) / 32767;
    if (mag < 20) mag = 50;  /* Minimum magnitude */

    /* Period */
    unsigned short period = effect->u.periodic.period;
    if (period == 0) period = 100;  /* Default 100ms = 10Hz */

    LOG_DEBUG("Uploading %s effect %d, magnitude=0x%02x, period=%dms",
              type_name, id, mag, period);

    /* Report 0x02 - Envelope */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x02;
    buf[1] = 0x1c;
    ret = usb_send(buf, 9);
    if (ret) return ret;
    usleep(5000);

    /* Report 0x04 - Periodic parameters */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x04;
    buf[1] = 0x0e;
    buf[2] = 0x00;
    buf[3] = mag;
    buf[4] = 0x00;  /* Offset */
    buf[5] = 0x00;  /* Phase */
    buf[6] = period & 0xff;
    buf[7] = (period >> 8) & 0xff;
    ret = usb_send(buf, 8);
    if (ret) return ret;
    usleep(5000);

    /* Report 0x01 - Main effect upload */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x01;
    buf[1] = id;
    buf[2] = effect_type;
    buf[3] = 0x40;
    buf[4] = 0x17;
    buf[5] = 0x25;
    buf[6] = 0x00;
    buf[7] = 0xff;
    buf[8] = 0xff;
    buf[9] = 0x0e;
    buf[10] = 0x00;
    buf[11] = 0x1c;
    buf[12] = 0x00;
    buf[13] = 0x00;
    buf[14] = 0x00;
    ret = usb_send(buf, 15);
    if (ret) return ret;

    LOG_DEBUG("%s effect uploaded", type_name);

    return 0;
}

int upload_ramp_effect(int id, struct ff_effect *effect)
{
#if !ENABLE_RAMP_EFFECTS
    LOG_ERROR("Ramp effects are disabled (firmware limitation)");
    return -1;
#else
    unsigned char buf[15];
    int ret;

    int start_level = effect->u.ramp.start_level;
    int end_level = effect->u.ramp.end_level;

    LOG_DEBUG("Uploading ramp effect %d, start=%d, end=%d",
              id, start_level, end_level);

    /* Report 0x02 - Envelope */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x02;
    buf[1] = 0x1c;
    ret = usb_send(buf, 9);
    if (ret) return ret;
    usleep(5000);

    /* Report 0x04 - Ramp parameters */
    unsigned short duration_ms = effect->replay.length;
    unsigned short start_scaled = (abs(start_level) * 0x00ff) / 32767;

    memset(buf, 0, sizeof(buf));
    buf[0] = 0x04;
    buf[1] = 0x0e;
    buf[2] = start_scaled & 0xff;
    buf[3] = (start_scaled >> 8) & 0xff;
    buf[4] = start_scaled & 0xff;
    buf[5] = (start_scaled >> 8) & 0xff;
    buf[6] = duration_ms & 0xff;
    buf[7] = (duration_ms >> 8) & 0xff;
    ret = usb_send(buf, 9);
    if (ret) return ret;
    usleep(5000);

    /* Report 0x01 - Main effect upload */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x01;
    buf[1] = id;
    buf[2] = 0x24;  /* Ramp type */
    buf[3] = 0x40;
    buf[4] = duration_ms & 0xff;
    buf[5] = (duration_ms >> 8) & 0xff;
    buf[6] = 0x00;
    buf[7] = 0xff;
    buf[8] = 0xff;
    buf[9] = 0x0e;
    buf[10] = 0x00;
    buf[11] = 0x1c;
    buf[12] = 0x00;
    buf[13] = 0x00;
    buf[14] = 0x00;
    ret = usb_send(buf, 15);
    if (ret) return ret;

    LOG_DEBUG("Ramp effect uploaded");

    return 0;
#endif
}

/* ============================================================================
 * Effect Control
 * ============================================================================ */

int start_effect(int id)
{
    unsigned char buf[4];
    int is_constant = 0;
    int force = 0;
    int ret;

    LOG_DEBUG("Starting effect %d (type=%d)", id, effects[id].effect.type);

    /* Check effect type (mutex already locked by caller) */
    if (id >= 0 && id < MAX_EFFECTS) {
        if (effects[id].effect.type == FF_CONSTANT) {
            is_constant = 1;
            force = effects[id].effect.u.constant.level;

            /* Initialize constant force state */
            effects[id].is_constant = 1;
            effects[id].current_force_level = force;
            clock_gettime(CLOCK_MONOTONIC, &effects[id].start_time);

            /* Initialize envelope parameters */
            effects[id].attack_length_ms = effects[id].effect.u.constant.envelope.attack_length;
            effects[id].attack_level = effects[id].effect.u.constant.envelope.attack_level;
            effects[id].fade_length_ms = effects[id].effect.u.constant.envelope.fade_length;
            effects[id].fade_level = effects[id].effect.u.constant.envelope.fade_level;
            effects[id].duration_ms = effects[id].effect.replay.length;

            LOG_DEBUG("Constant force initialized: id=%d, level=%d", id, force);
        } else if (effects[id].effect.type == FF_PERIODIC) {
            /* Initialize periodic effect state */
            effects[id].is_periodic = 1;
            effects[id].periodic_magnitude = effects[id].effect.u.periodic.magnitude;
            effects[id].periodic_offset = effects[id].effect.u.periodic.offset;
            effects[id].periodic_phase = effects[id].effect.u.periodic.phase;
            effects[id].periodic_period_ms = effects[id].effect.u.periodic.period;
            effects[id].periodic_waveform = effects[id].effect.u.periodic.waveform;
            clock_gettime(CLOCK_MONOTONIC, &effects[id].start_time);

            /* Initialize envelope parameters */
            effects[id].attack_length_ms = effects[id].effect.u.periodic.envelope.attack_length;
            effects[id].attack_level = effects[id].effect.u.periodic.envelope.attack_level;
            effects[id].fade_length_ms = effects[id].effect.u.periodic.envelope.fade_length;
            effects[id].fade_level = effects[id].effect.u.periodic.envelope.fade_level;
            effects[id].duration_ms = effects[id].effect.replay.length;

            LOG_DEBUG("Periodic effect initialized: waveform=%d", effects[id].periodic_waveform);
        }
    }

    /* For constant force, set the level using Report 0x03 */
    if (is_constant) {
        signed char signed_level = (signed char)((force * 127) / 32767);
        unsigned char level = (unsigned char)signed_level;

        buf[0] = 0x03;
        buf[1] = 0x0e;
        buf[2] = 0x00;
        buf[3] = level;
        ret = usb_send(buf, 4);
        if (ret) return ret;
        usleep(5000);

        LOG_DEBUG("Set constant force level to 0x%02x", level);
    }

    /* Send start command */
    buf[0] = 0x41;
    buf[1] = id;
    buf[2] = 0x41;  /* Always 0x41 for START */
    buf[3] = 0x01;  /* Action: start */

    LOG_INFO("Starting effect %d: type=%d (spring=%d, periodic=%d, constant=%d)",
             id, effects[id].effect.type,
             effects[id].effect.type == FF_SPRING,
             effects[id].effect.type == FF_PERIODIC,
             effects[id].effect.type == FF_CONSTANT);

    return usb_send(buf, 4);
}

int stop_effect(int id)
{
    unsigned char buf[4];

    LOG_DEBUG("Stopping effect %d", id);

    /* Clear effect state (mutex already locked by caller) */
    if (id >= 0 && id < MAX_EFFECTS) {
        effects[id].is_constant = 0;
        effects[id].is_periodic = 0;
        effects[id].current_force_level = 0;
        effects[id].last_sent_force = 0;
        effects[id].target_force = 0;
    }

    buf[0] = 0x41;
    buf[1] = id;
    buf[2] = 0x00;  /* 0x00 for STOP */
    buf[3] = 0x01;

    return usb_send(buf, 4);
}

