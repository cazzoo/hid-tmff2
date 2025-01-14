// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Force feedback support for Thrustmaster T500RS - Force Feedback
 *
 * Copyright (c) 2024 Your Name
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/mutex.h>
#include <linux/bitops.h>
#include <linux/ktime.h>
#include <linux/hid-debug.h>
#include <linux/device.h>
#include <asm/unaligned.h>
#include "../hid-tmff2.h"
#include "hid-tmt500rs.h"
#include "hid-tmt500rs-utils.h"

/* Supported force feedback effects */
const signed short t500rs_supported_effects[] = {
    FF_CONSTANT,
    FF_SPRING,
    FF_DAMPER,
    FF_FRICTION,
    FF_INERTIA,
    FF_PERIODIC,
    FF_SINE,
    FF_TRIANGLE,
    FF_SQUARE,
    FF_SAW_UP,
    FF_SAW_DOWN,
    FF_AUTOCENTER,
    FF_GAIN,
    -1  /* Terminator */
};

/* Effect state flags */
#define EFFECT_STARTED FF_EFFECT_PLAYING

/* Scale 16-bit value to 7-bit range for T500RS */
static inline u8 scale_param(s16 value)
{
    /* Convert to unsigned and scale to 0-127 range */
    return (u8)((abs(value) >> 9) & 0x7F);
}

/* Force feedback implementation */
int t500rs_upload_effect(void *data, struct tmff2_effect_state *state)
{
    struct t500rs_device_entry *t500rs = data;
    int ret;
    u8 scaled_param;
    
    if (!t500rs || !state) {
        tmff2_dbg("T500RS: Invalid data or state\n");
        return -EINVAL;
    }
        
    switch (state->effect.type) {
    case FF_CONSTANT:
        scaled_param = scale_param(state->effect.u.constant.level);
        tmff2_dbg("T500RS: Uploading constant force effect id=%d level=%d scaled=%d\n",
                state->effect.id, state->effect.u.constant.level, scaled_param);
        ret = t500rs_send_command(t500rs, 0x0e, state->effect.id, scaled_param);
        break;
    case FF_SPRING:
        scaled_param = scale_param(state->effect.u.condition[0].right_coeff);
        tmff2_dbg("T500RS: Uploading spring effect id=%d coeff=%d scaled=%d\n",
                state->effect.id, state->effect.u.condition[0].right_coeff, scaled_param);
        ret = t500rs_send_command(t500rs, 0x0e, state->effect.id | 0x20, scaled_param);
        break;
    case FF_DAMPER:
        scaled_param = scale_param(state->effect.u.condition[0].right_coeff);
        tmff2_dbg("T500RS: Uploading damper effect id=%d coeff=%d scaled=%d\n",
                state->effect.id, state->effect.u.condition[0].right_coeff, scaled_param);
        ret = t500rs_send_command(t500rs, 0x0e, state->effect.id | 0x40, scaled_param);
        break;
    case FF_FRICTION:
        scaled_param = scale_param(state->effect.u.condition[0].right_coeff);
        tmff2_dbg("T500RS: Uploading friction effect id=%d coeff=%d scaled=%d\n",
                state->effect.id, state->effect.u.condition[0].right_coeff, scaled_param);
        ret = t500rs_send_command(t500rs, 0x0e, state->effect.id | 0x60, scaled_param);
        break;
    case FF_PERIODIC:
        switch (state->effect.u.periodic.waveform) {
        case FF_SINE:
            scaled_param = scale_param(state->effect.u.periodic.magnitude);
            tmff2_dbg("T500RS: Uploading sine effect id=%d magnitude=%d scaled=%d\n",
                    state->effect.id, state->effect.u.periodic.magnitude, scaled_param);
            ret = t500rs_send_command(t500rs, 0x0e, state->effect.id | 0x80, scaled_param);
            break;
        case FF_TRIANGLE:
            scaled_param = scale_param(state->effect.u.periodic.magnitude);
            tmff2_dbg("T500RS: Uploading triangle effect id=%d magnitude=%d scaled=%d\n",
                    state->effect.id, state->effect.u.periodic.magnitude, scaled_param);
            ret = t500rs_send_command(t500rs, 0x0e, state->effect.id | 0x90, scaled_param);
            break;
        case FF_SQUARE:
            scaled_param = scale_param(state->effect.u.periodic.magnitude);
            tmff2_dbg("T500RS: Uploading square effect id=%d magnitude=%d scaled=%d\n",
                    state->effect.id, state->effect.u.periodic.magnitude, scaled_param);
            ret = t500rs_send_command(t500rs, 0x0e, state->effect.id | 0xa0, scaled_param);
            break;
        default:
            tmff2_dbg("T500RS: Unsupported waveform %d\n", state->effect.u.periodic.waveform);
            return -EINVAL;
        }
        break;
    default:
        tmff2_dbg("T500RS: Unsupported effect type %d\n", state->effect.type);
        return -EINVAL;
    }

    return ret;
}

int t500rs_play_effect(void *data, struct tmff2_effect_state *state)
{
    struct t500rs_device_entry *t500rs = data;
    u8 cmd = state->flags & EFFECT_STARTED ? 0x01 : 0x00;
    
    if (!t500rs || !state)
        return -EINVAL;
    
    return t500rs_send_command(t500rs, 0x0e, state->effect.id | 0x10, cmd);
}
EXPORT_SYMBOL_GPL(t500rs_play_effect);

int t500rs_set_gain(void *data, uint16_t gain)
{
    struct t500rs_device_entry *t500rs = data;
    return t500rs_send_command(t500rs, 0x0e, 0xf0, gain >> 8);
}

int t500rs_set_autocenter(void *data, uint16_t autocenter)
{
    struct t500rs_device_entry *t500rs = data;
    return t500rs_send_command(t500rs, 0x0e, 0xf1, autocenter >> 8);
}

int t500rs_set_range(void *data, uint16_t range)
{
    struct t500rs_device_entry *t500rs = data;
    return t500rs_send_command(t500rs, 0x0e, 0xf2, range >> 8);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Force feedback support for Thrustmaster T500RS - Force Feedback"); 