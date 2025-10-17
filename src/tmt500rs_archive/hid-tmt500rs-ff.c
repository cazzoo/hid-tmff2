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
#include <linux/device.h>
#include <asm/unaligned.h>
#include "../hid-tmff2.h"
#include "hid-tmt500rs.h"
#include "hid-tmt500rs-utils.h"

/* Supported effects */
const signed short t500rs_supported_effects[] = {
    FF_CONSTANT,
    FF_SPRING,
    FF_DAMPER,
    FF_FRICTION,
    FF_SINE,
    FF_TRIANGLE,
    FF_SQUARE,
    -1
};

int t500rs_init_ff(struct t500rs_device_entry *t500rs)
{
    struct input_dev *input_dev = t500rs->data->input_dev;
    int error;

    if (!input_dev)
        return -EINVAL;

    /* Set input device capabilities */
    input_set_capability(input_dev, EV_FF, FF_CONSTANT);
    input_set_capability(input_dev, EV_FF, FF_SPRING);
    input_set_capability(input_dev, EV_FF, FF_DAMPER);
    input_set_capability(input_dev, EV_FF, FF_FRICTION);
    input_set_capability(input_dev, EV_FF, FF_SINE);
    input_set_capability(input_dev, EV_FF, FF_TRIANGLE);
    input_set_capability(input_dev, EV_FF, FF_SQUARE);

    /* Create force feedback device */
    error = input_ff_create(input_dev, T500RS_MAX_EFFECTS);
    if (error) {
        dev_err(&t500rs->data->hdev->dev, "Failed to create FF device: %d\n", error);
        return error;
    }

    return 0;
}

void t500rs_cleanup_ff(struct t500rs_device_entry *t500rs)
{
    struct input_dev *input_dev;

    if (!t500rs || !t500rs->data)
        return;

    input_dev = t500rs->data->input_dev;
    if (input_dev)
        input_ff_destroy(input_dev);
}

int t500rs_set_range(void *data, u16 range)
{
    struct t500rs_device_entry *t500rs = data;
    
    if (!t500rs || !t500rs->data || !t500rs->data->initialized)
        return -EINVAL;

    // Range is 270-1080 degrees
    if (range < 270)
        range = 270;
    else if (range > 1080)
        range = 1080;

    dev_dbg(&t500rs->data->hdev->dev, "Setting wheel range to %u degrees\n", range);
    return t500rs_send_command(t500rs, T500RS_CMD_SET_RANGE, range & 0xFF, (range >> 8) & 0xFF);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Force feedback support for Thrustmaster T500RS - Force Feedback"); 