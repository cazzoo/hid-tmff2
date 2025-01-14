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

static bool debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Enable debug output");

#define tmff2_dbg(fmt, ...) \
    do { if (debug) pr_info("tmff2: " fmt, ##__VA_ARGS__); } while (0)

int t500rs_populate_api(struct tmff2_device_entry *tmff2)
{
    struct t500rs_device_entry *t500rs;
    
    t500rs = kzalloc(sizeof(*t500rs), GFP_KERNEL);
    if (!t500rs)
        return -ENOMEM;

    t500rs->data = kzalloc(sizeof(struct t500rs_device_data), GFP_KERNEL);
    if (!t500rs->data) {
        kfree(t500rs);
        return -ENOMEM;
    }

    t500rs->data->hdev = tmff2->hdev;
    t500rs->data->usbdev = to_usb_device(tmff2->hdev->dev.parent);
    t500rs->data->state = T500RS_STATE_INIT;
    t500rs->data->initialized = false;

    tmff2->data = t500rs;
    tmff2->set_range = t500rs_set_range;
    tmff2->wheel_fixup = t500rs_wheel_fixup;
    tmff2->open = t500rs_open;
    tmff2->close = t500rs_close;
    tmff2->wheel_init = t500rs_wheel_init;
    tmff2->wheel_destroy = t500rs_wheel_destroy;

    return 0;
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

    tmff2_dbg("Setting wheel range to %u degrees\n", range);
    return t500rs_send_command(t500rs, T500RS_CMD_SET_RANGE, range & 0xFF, (range >> 8) & 0xFF);
}

__u8 *t500rs_wheel_fixup(struct hid_device *hdev, __u8 *rdesc, unsigned int *rsize)
{
    tmff2_dbg("Applying wheel fixups\n");
    // No descriptor modifications needed for now
    return rdesc;
}

int t500rs_open(void *data, int open_mode)
{
    struct t500rs_device_entry *t500rs = data;
    
    if (!t500rs || !t500rs->data)
        return -EINVAL;

    tmff2_dbg("Opening T500RS device\n");
    return t500rs_init_wheel(t500rs->data->hdev);
}

int t500rs_close(void *data, int open_mode)
{
    struct t500rs_device_entry *t500rs = data;
    
    if (!t500rs || !t500rs->data)
        return -EINVAL;

    tmff2_dbg("Closing T500RS device\n");
    t500rs_cleanup_usb(t500rs->data->hdev);
    return 0;
}

int t500rs_wheel_init(struct tmff2_device_entry *tmff2, int open_mode)
{
    struct t500rs_device_entry *t500rs = tmff2->data;
    int ret = 0;
    int retries = 0;

    if (!t500rs || !t500rs->data)
        return -EINVAL;

    if (t500rs->data->initialized)
        return 0;

    // Add delay for device stabilization
    msleep(300);

    // Initialize USB with retries
    while (retries < 3) {
        ret = t500rs_init_wheel(t500rs->data->hdev);
        if (ret == 0)
            break;
        if (ret != -19 && ret != -ENODEV)
            break;
        msleep(100 * (retries + 1));
        retries++;
    }

    if (ret < 0) {
        dev_err(&t500rs->data->hdev->dev, "Failed to initialize wheel after %d retries: %d\n", 
                retries, ret);
        return ret;
    }

    t500rs->data->initialized = true;
    return 0;
}

int t500rs_wheel_destroy(void *data)
{
    struct t500rs_device_entry *t500rs = data;
    
    if (!t500rs || !t500rs->data)
        return -EINVAL;

    tmff2_dbg("Destroying T500RS wheel\n");
    
    if (t500rs->data->initialized) {
        t500rs_cleanup_ff(t500rs->data->hdev);
        t500rs_cleanup_usb(t500rs->data->hdev);
        t500rs->data->initialized = false;
    }

    return 0;
}

static int t500rs_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
    struct tmff2_device_entry *tmff2;
    int ret;

    tmff2 = kzalloc(sizeof(*tmff2), GFP_KERNEL);
    if (!tmff2)
        return -ENOMEM;

    tmff2->hdev = hdev;
    hid_set_drvdata(hdev, tmff2);

    ret = t500rs_populate_api(tmff2);
    if (ret < 0) {
        kfree(tmff2);
        return ret;
    }

    return 0;
}

static void t500rs_remove(struct hid_device *hdev)
{
    struct tmff2_device_entry *tmff2 = hid_get_drvdata(hdev);
    struct t500rs_device_entry *t500rs;

    if (tmff2) {
        t500rs = tmff2->data;
        if (t500rs) {
            if (t500rs->data) {
                t500rs_cleanup_usb(t500rs->data->hdev);
                kfree(t500rs->data);
            }
            kfree(t500rs);
        }
    }
}

static const struct hid_device_id t500rs_devices[] = {
    { HID_USB_DEVICE(0x044f, 0xb65d) },
    { }
};
MODULE_DEVICE_TABLE(hid, t500rs_devices);

static struct hid_driver t500rs_driver = {
    .name = "t500rs",
    .id_table = t500rs_devices,
    .probe = t500rs_probe,
    .remove = t500rs_remove,
};
module_hid_driver(t500rs_driver);
