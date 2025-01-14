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
#include <linux/hid-debug.h>
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

int t500rs_init_wheel(struct t500rs_device_entry *t500rs, int open_mode)
{
    int error;

    if (!t500rs || !t500rs->data)
        return -EINVAL;

    t500rs->data->initialized = false;

    /* Initialize USB */
    error = t500rs_init_usb(t500rs);
    if (error) {
        t500rs_info(t500rs, "Failed to initialize USB: %d\n", error);
        goto err_usb;
    }

    /* Initialize force feedback */
    error = t500rs_init_ff(t500rs);
    if (error) {
        t500rs_info(t500rs, "Failed to initialize force feedback: %d\n", error);
        goto err_ff;
    }

    /* Set wheel mode */
    error = t500rs_set_mode(t500rs, TMT500RS_MODE_WHEEL);
    if (error) {
        t500rs_info(t500rs, "Failed to set wheel mode: %d\n", error);
        goto err_mode;
    }

    t500rs->data->initialized = true;
    return 0;

err_mode:
    t500rs_cleanup_ff(t500rs);
err_ff:
    t500rs_cleanup_usb(t500rs);
err_usb:
    return error;
}

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
        t500rs_info(t500rs, "Failed to create FF device: %d\n", error);
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

int t500rs_init_usb(struct t500rs_device_entry *t500rs)
{
    struct usb_device *usbdev;
    int error;

    if (!t500rs || !t500rs->data || !t500rs->data->hdev)
        return -EINVAL;

    usbdev = interface_to_usbdev(to_usb_interface(t500rs->data->hdev->dev.parent));
    if (!usbdev)
        return -ENODEV;

    t500rs->data->state = T500RS_STATE_INITIALIZING;
    t500rs->data->usbdev = usbdev;

    /* Reset device */
    error = t500rs_reset_device(t500rs);
    if (error) {
        t500rs_info(t500rs, "Failed to reset device: %d\n", error);
        goto err_reset;
    }

    /* Initialize interrupts */
    error = t500rs_interrupts(t500rs);
    if (error) {
        t500rs_info(t500rs, "Failed to initialize interrupts: %d\n", error);
        goto err_int;
    }

    t500rs->data->state = T500RS_STATE_READY;
    t500rs->data->initialized = true;
    return 0;

err_int:
    t500rs_cleanup_usb(t500rs);
err_reset:
    t500rs->data->state = T500RS_STATE_ERROR;
    return error;
}

void t500rs_cleanup_usb(struct t500rs_device_entry *t500rs)
{
    if (!t500rs || !t500rs->data)
        return;

    t500rs->data->state = T500RS_STATE_DISCONNECTED;
    t500rs->data->initialized = false;

    t500rs_stop_urbs(t500rs);
}

int t500rs_reset_device(struct t500rs_device_entry *t500rs)
{
    struct hid_device *hdev;
    int error;

    if (!t500rs || !t500rs->data || !t500rs->data->hdev)
        return -EINVAL;

    hdev = t500rs->data->hdev;

    /* Stop any active URBs */
    t500rs_stop_urbs(t500rs);

    /* Send reset command */
    error = t500rs_send_reset_cmd(t500rs);
    if (error) {
        t500rs_info(t500rs, "Failed to send reset command: %d\n", error);
        return error;
    }

    /* Wait for device to stabilize */
    msleep(300);

    /* Start URBs */
    error = t500rs_start_urbs(t500rs);
    if (error) {
        t500rs_info(t500rs, "Failed to start URBs: %d\n", error);
        return error;
    }

    return 0;
}

int t500rs_set_mode(struct t500rs_device_entry *t500rs, u8 mode)
{
    u8 buf[T500RS_REPORT_LENGTH];
    int ret;

    if (!t500rs || !t500rs->data || !t500rs->data->hdev)
        return -EINVAL;

    /* Prepare mode switch command */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x42;  /* Mode switch command */
    buf[1] = 0x01;  /* Command ID */
    buf[2] = mode;  /* Target mode */

    /* Send command with retries */
    ret = t500rs_send_cmd_with_retry(t500rs, buf, sizeof(buf), 3);
    if (ret) {
        t500rs_info(t500rs, "Failed to set mode %02x: %d\n", mode, ret);
        return ret;
    }

    /* Wait for mode switch to complete */
    msleep(100);

    return 0;
}

int t500rs_send_cmd_with_retry(struct t500rs_device_entry *t500rs,
                              u8 *buf, int len, int retries)
{
    int ret, i;

    for (i = 0; i < retries; i++) {
        ret = hid_hw_raw_request(t500rs->data->hdev, buf[0], buf, len,
                                HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);
        if (ret >= 0)
            return ret;

        msleep(50 * (i + 1));  // Increasing delay between retries
    }

    return ret;
}

void t500rs_wheel_destroy(struct t500rs_device_entry *t500rs)
{
    if (!t500rs || !t500rs->data)
        return;

    /* Cleanup force feedback */
    t500rs_cleanup_ff(t500rs);

    /* Cleanup USB */
    if (t500rs->data->urb) {
        usb_kill_urb(t500rs->data->urb);
        if (t500rs->data->buffer)
            usb_free_coherent(t500rs->data->usbdev, T500RS_REPORT_LENGTH,
                            t500rs->data->buffer, t500rs->data->buffer_dma);
        usb_free_urb(t500rs->data->urb);
    }

    /* Free device data */
    kfree(t500rs->data);
    t500rs->data = NULL;
}

void t500rs_stop_urbs(struct t500rs_device_entry *t500rs)
{
    if (!t500rs || !t500rs->data)
        return;

    if (t500rs->data->urb) {
        usb_kill_urb(t500rs->data->urb);
    }
}

int t500rs_start_urbs(struct t500rs_device_entry *t500rs)
{
    int ret = 0;

    if (!t500rs || !t500rs->data)
        return -EINVAL;

    if (t500rs->data->urb) {
        ret = usb_submit_urb(t500rs->data->urb, GFP_KERNEL);
        if (ret) {
            t500rs_info(t500rs, "Failed to submit URB: %d\n", ret);
            return ret;
        }
    }

    return 0;
}

int t500rs_send_reset_cmd(struct t500rs_device_entry *t500rs)
{
    u8 buf[8] = {0};
    buf[0] = 0x40;  // Reset command
    return hid_hw_raw_request(t500rs->data->hdev, buf[0], buf, sizeof(buf),
                             HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);
}

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
MODULE_DESCRIPTION("Force feedback support for Thrustmaster T500RS - Initialization"); 