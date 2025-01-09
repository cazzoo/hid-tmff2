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

/* Force feedback implementation */
int t500rs_upload_effect(void *data, struct tmff2_effect_state *state)
{
    struct t500rs_device_entry *t500rs = data;
    int ret;
    
    if (!t500rs || !state)
        return -EINVAL;
        
    switch (state->effect.type) {
    case FF_CONSTANT:
        ret = t500rs_send_command(t500rs, 0x0e, state->effect.id,
                                state->effect.u.constant.level >> 8);
        break;
    case FF_SPRING:
        ret = t500rs_send_command(t500rs, 0x0e, state->effect.id | 0x20,
                                state->effect.u.condition[0].right_coeff >> 8);
        break;
    case FF_DAMPER:
        ret = t500rs_send_command(t500rs, 0x0e, state->effect.id | 0x40,
                                state->effect.u.condition[0].right_coeff >> 8);
        break;
    case FF_FRICTION:
        ret = t500rs_send_command(t500rs, 0x0e, state->effect.id | 0x60,
                                state->effect.u.condition[0].right_coeff >> 8);
        break;
    case FF_PERIODIC:
        switch (state->effect.u.periodic.waveform) {
        case FF_SINE:
            ret = t500rs_send_command(t500rs, 0x0e, state->effect.id | 0x80,
                                    state->effect.u.periodic.magnitude >> 8);
            break;
        case FF_TRIANGLE:
            ret = t500rs_send_command(t500rs, 0x0e, state->effect.id | 0x90,
                                    state->effect.u.periodic.magnitude >> 8);
            break;
        case FF_SQUARE:
            ret = t500rs_send_command(t500rs, 0x0e, state->effect.id | 0xa0,
                                    state->effect.u.periodic.magnitude >> 8);
            break;
        case FF_SAW_UP:
            ret = t500rs_send_command(t500rs, 0x0e, state->effect.id | 0xb0,
                                    state->effect.u.periodic.magnitude >> 8);
            break;
        case FF_SAW_DOWN:
            ret = t500rs_send_command(t500rs, 0x0e, state->effect.id | 0xc0,
                                    state->effect.u.periodic.magnitude >> 8);
            break;
        default:
            return -EINVAL;
        }
        break;
    default:
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