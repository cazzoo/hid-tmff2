// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Force feedback support for Thrustmaster T500RS - Device Initialization
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

/* Device initialization functions */

int t500rs_wheel_init(struct tmff2_device_entry *tmff2, int open_mode)
{
    struct t500rs_device_entry *t500rs = tmff2->data;
    struct list_head *report_list;
    int ret;

    if (!t500rs)
        return -EINVAL;

    /* Initialize USB communication */
    spin_lock_init(&t500rs->lock);
    init_completion(&t500rs->response_completion);
    t500rs->waiting_for_response = false;
    t500rs->retry_count = 0;

    ret = t500rs_interrupts(t500rs);
    if (ret < 0)
        return ret;

    report_list = &t500rs->hdev->report_enum[HID_OUTPUT_REPORT].report_list;
    t500rs->report = list_entry(report_list->next, struct hid_report, list);
    t500rs->ff_field = t500rs->report->field[0];

    /* Initialize force feedback */
    memcpy(tmff2->supported_effects, t500rs_supported_effects, FF_CNT * sizeof(signed short));

    /* Send initialization commands */
    ret = t500rs_send_command(t500rs, 0x0f, 0x01, 0x00);
    if (ret < 0)
        goto err_free_usb;

    if (open_mode) {
        ret = t500rs_send_command(t500rs, 0x0f, 0x02, 0x00);
        if (ret < 0)
            goto err_free_usb;
    }

    return 0;

err_free_usb:
    if (t500rs->urb) {
        usb_kill_urb(t500rs->urb);
        if (t500rs->buffer)
            usb_free_coherent(t500rs->usbdev, T500RS_REPORT_LENGTH,
                            t500rs->buffer, t500rs->buffer_dma);
        usb_free_urb(t500rs->urb);
    }
    return ret;
}
EXPORT_SYMBOL_GPL(t500rs_wheel_init);

int t500rs_wheel_destroy(void *data)
{
    struct t500rs_device_entry *t500rs = data;
    int ret = 0;

    if (t500rs) {
        /* Send cleanup commands */
        ret = t500rs_send_command(t500rs, 0x0f, 0x02, 0x01);

        /* Free USB resources */
        if (t500rs->urb) {
            usb_kill_urb(t500rs->urb);
            if (t500rs->buffer)
                usb_free_coherent(t500rs->usbdev, T500RS_REPORT_LENGTH,
                                t500rs->buffer, t500rs->buffer_dma);
            usb_free_urb(t500rs->urb);
        }

        kfree(t500rs);
    }

    return ret;
}
EXPORT_SYMBOL_GPL(t500rs_wheel_destroy);

int t500rs_open(void *data, int open_mode)
{
    struct t500rs_device_entry *t500rs = data;
    return t500rs_send_command(t500rs, 0x0f, 0x02, 0x00);
}

int t500rs_close(void *data, int open_mode)
{
    struct t500rs_device_entry *t500rs = data;
    return t500rs_send_command(t500rs, 0x0f, 0x02, 0x01);
}

__u8 *t500rs_wheel_fixup(struct hid_device *hdev, __u8 *rdesc, unsigned int *rsize)
{
    return rdesc;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Force feedback support for Thrustmaster T500RS - Device Initialization"); 