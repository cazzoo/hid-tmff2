// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Force feedback support for Thrustmaster T500RS - Core
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

int t500rs_populate_api(struct tmff2_device_entry *tmff2)
{
    struct t500rs_device_entry *t500rs = kzalloc(sizeof(struct t500rs_device_entry),
                                                GFP_KERNEL);
    if (!t500rs)
        return -ENOMEM;

    t500rs->hdev = tmff2->hdev;
    t500rs->input_dev = tmff2->input_dev;
    t500rs->usbdev = to_usb_device(tmff2->hdev->dev.parent->parent);
    t500rs->tmff2 = tmff2;

    tmff2->data = t500rs;
    tmff2->max_effects = T500RS_MAX_EFFECTS;
    tmff2->params = PARAM_SPRING_LEVEL | PARAM_DAMPER_LEVEL | PARAM_FRICTION_LEVEL |
                    PARAM_GAIN | PARAM_RANGE | PARAM_ALT_MODE;

    /* Set up supported effects */
    memcpy(tmff2->supported_effects, t500rs_supported_effects,
           sizeof(signed short) * FF_CNT);

    /* Set up force feedback interface */
    tmff2->upload_effect = t500rs_upload_effect;
    tmff2->play_effect = t500rs_play_effect;
    tmff2->set_gain = t500rs_set_gain;
    tmff2->set_autocenter = t500rs_set_autocenter;
    tmff2->set_range = t500rs_set_range;
    tmff2->wheel_fixup = t500rs_wheel_fixup;

    tmff2->open = t500rs_open;
    tmff2->close = t500rs_close;

    tmff2->wheel_init = t500rs_wheel_init;
    tmff2->wheel_destroy = t500rs_wheel_destroy;

    return 0;
}
EXPORT_SYMBOL_GPL(t500rs_populate_api);
