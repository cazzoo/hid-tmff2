// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Force feedback support for Thrustmaster T500RS - Initialization
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

int t500rs_init_wheel(struct t500rs_device_entry *t500rs, int open_mode)
{
    int error;
    u8 current_mode;
    int init_retries = 0;
    struct hid_device *hdev;

    if (!t500rs || !t500rs->data)
        return -EINVAL;

    hdev = t500rs->data->hdev;

    /* Ensure clean state */
    t500rs_cleanup_usb(t500rs);
    msleep(500);  // Increased delay for cleanup

    t500rs->data->initialized = false;
    t500rs->data->state = T500RS_STATE_INIT;
    t500rs->data->urb_error_count = 0;

retry_init:
    /* Initialize USB */
    error = t500rs_init_usb(t500rs);
    if (error) {
        if (init_retries++ < 3) {
            dev_warn(&hdev->dev, "USB init failed, retrying: %d\n", error);
            msleep(1000);
            goto retry_init;
        }
        dev_err(&hdev->dev, "Failed to initialize USB after retries: %d\n", error);
        goto err_usb;
    }

    /* Wait for USB to stabilize */
    msleep(2000);  // Increased delay for USB stability

    /* Initialize mode switch */
    error = t500rs_init_mode_switch(t500rs);
    if (error) {
        dev_err(&hdev->dev, "Failed to initialize mode switch: %d\n", error);
        goto err_mode;
    }

    msleep(500);  // Wait after mode switch init

    /* Detect current mode */
    error = t500rs_detect_mode(t500rs, &current_mode);
    if (error) {
        dev_err(&hdev->dev, "Failed to detect mode: %d\n", error);
        goto err_mode;
    }

    dev_info(&hdev->dev, "Current wheel mode: 0x%02x\n", current_mode);

    /* If not in wheel mode, switch to it */
    if (current_mode != TMT500RS_MODE_WHEEL) {
        dev_info(&hdev->dev, "Switching to wheel mode\n");
        error = t500rs_start_mode_switch(t500rs, TMT500RS_MODE_WHEEL);
        if (error) {
            dev_err(&hdev->dev, "Failed to switch to wheel mode: %d\n", error);
            goto err_mode;
        }

        /* Wait for mode switch to complete */
        msleep(3000);  // Increased delay for mode switch

        /* Verify mode switch with retries */
        for (init_retries = 0; init_retries < 3; init_retries++) {
            error = t500rs_verify_mode(t500rs, TMT500RS_MODE_WHEEL);
            if (!error)
                break;
            msleep(1000);
        }

        if (error) {
            dev_err(&hdev->dev, "Failed to verify wheel mode: %d\n", error);
            goto err_mode;
        }
    }

    /* Initialize basic device state */
    for (init_retries = 0; init_retries < 3; init_retries++) {
        error = t500rs_send_command(t500rs, 0x0f, 0x01, 0x00);
        if (!error)
            break;
        msleep(500);
    }

    if (error) {
        dev_err(&hdev->dev, "Failed to initialize device state: %d\n", error);
        goto err_init;
    }

    /* Wait for device to settle after initial setup */
    msleep(2000);

    t500rs->data->state = T500RS_STATE_INITIALIZING;

    /* Initialize force feedback */
    error = t500rs_init_ff(t500rs);
    if (error) {
        dev_err(&hdev->dev, "Failed to initialize force feedback: %d\n", error);
        goto err_ff;
    }

    /* Final initialization steps */
    t500rs->data->initialized = true;
    t500rs->data->state = T500RS_STATE_READY;

    dev_info(&hdev->dev, "T500RS wheel initialized successfully\n");
    return 0;

err_ff:
    t500rs->data->state = T500RS_STATE_ERROR;
    return error;

err_init:
err_mode:
    t500rs_cleanup_usb(t500rs);
err_usb:
    t500rs->data->state = T500RS_STATE_ERROR;
    return error;
}

int t500rs_wheel_init(struct tmff2_device_entry *tmff2, int open_mode)
{
    struct t500rs_device_entry *t500rs = tmff2->data;
    struct list_head *report_list;
    int ret;
    int i;

    if (!t500rs || !t500rs->data)
        return -EINVAL;

    /* Initialize USB communication */
    spin_lock_init(&t500rs->data->lock);
    init_completion(&t500rs->data->response_completion);
    t500rs->data->waiting_for_response = false;
    t500rs->data->retry_count = 0;
    t500rs->data->state = T500RS_STATE_INIT;

    /* Initialize mode switch */
    t500rs_init_mode_switch(t500rs);

    /* Send initialization commands */
    ret = t500rs_send_command(t500rs, 0x0f, 0x01, 0x00);
    if (ret < 0)
        goto err_free_usb;

    /* Wait for initialization to complete */
    msleep(1000);

    /* Initialize interrupts */
    ret = t500rs_interrupts(t500rs->data);
    if (ret < 0)
        goto err_free_usb;

    report_list = &t500rs->data->hdev->report_enum[HID_OUTPUT_REPORT].report_list;
    t500rs->data->report = list_entry(report_list->next, struct hid_report, list);
    t500rs->data->ff_field = t500rs->data->report->field[0];

    /* Initialize force feedback */
    for (i = 0; t500rs_supported_effects[i] >= 0; i++)
        tmff2->supported_effects[i] = t500rs_supported_effects[i];
    tmff2->supported_effects[i] = -1;

    if (open_mode) {
        ret = t500rs_send_command(t500rs, 0x0f, 0x02, 0x00);
        if (ret < 0)
            goto err_free_usb;
    }

    t500rs->data->state = T500RS_STATE_READY;
    return 0;

err_free_usb:
    t500rs_cleanup_usb(t500rs);
    t500rs->data->state = T500RS_STATE_ERROR;
    return ret;
}

int t500rs_wheel_destroy(void *data)
{
    struct t500rs_device_entry *t500rs = data;
    int ret = 0;

    if (!t500rs || !t500rs->data)
        return -EINVAL;

    /* Send cleanup commands */
    ret = t500rs_send_command(t500rs, 0x0f, 0x02, 0x01);

    /* Cleanup mode switch */
    t500rs_cleanup_mode_switch(t500rs);

    /* Free USB resources */
    if (t500rs->data->urb) {
        usb_kill_urb(t500rs->data->urb);
        if (t500rs->data->buffer)
            usb_free_coherent(t500rs->data->usbdev, T500RS_REPORT_LENGTH,
                            t500rs->data->buffer, t500rs->data->buffer_dma);
        usb_free_urb(t500rs->data->urb);
    }

    return ret;
}

int t500rs_open(void *data, int open_mode)
{
    struct t500rs_device_entry *t500rs = data;
    int ret;

    if (!t500rs || !t500rs->data)
        return -EINVAL;

    /* Verify we're in wheel mode */
    ret = t500rs_verify_mode(t500rs, TMT500RS_MODE_WHEEL);
    if (ret) {
        /* Try to switch to wheel mode */
        ret = t500rs_start_mode_switch(t500rs, TMT500RS_MODE_WHEEL);
        if (ret)
            return ret;

        /* Wait for mode switch to complete */
        msleep(2000);
    }

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
MODULE_DESCRIPTION("Force feedback support for Thrustmaster T500RS - Initialization"); 