// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Force feedback support for Thrustmaster T500RS - USB Communication
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

#define USB_TIMEOUT 1000

static void t500rs_urb_complete(struct urb *urb)
{
    struct t500rs_device_entry *t500rs = urb->context;

    if (t500rs->waiting_for_response)
        complete(&t500rs->response_completion);
}

int t500rs_send_buf(struct t500rs_device_entry *t500rs, const u8 *send_buffer, size_t len)
{
    unsigned long flags;
    int ret;

    if (!t500rs || !send_buffer || len > T500RS_REPORT_LENGTH)
        return -EINVAL;

    spin_lock_irqsave(&t500rs->lock, flags);

    if (!t500rs->buffer) {
        t500rs->buffer = usb_alloc_coherent(t500rs->usbdev, T500RS_REPORT_LENGTH,
                                          GFP_ATOMIC, &t500rs->buffer_dma);
        if (!t500rs->buffer) {
            spin_unlock_irqrestore(&t500rs->lock, flags);
            return -ENOMEM;
        }
    }

    memcpy(t500rs->buffer, send_buffer, len);
    t500rs->buffer_length = len;
    t500rs->retry_count = 0;
    t500rs->waiting_for_response = true;
    reinit_completion(&t500rs->response_completion);

    ret = usb_submit_urb(t500rs->urb, GFP_ATOMIC);
    if (ret) {
        t500rs->waiting_for_response = false;
        hid_err(t500rs->hdev, "Failed to submit URB: %d\n", ret);
        spin_unlock_irqrestore(&t500rs->lock, flags);
        return ret;
    }

    spin_unlock_irqrestore(&t500rs->lock, flags);

    ret = wait_for_completion_timeout(&t500rs->response_completion,
                                    msecs_to_jiffies(USB_TIMEOUT));
    if (!ret) {
        usb_kill_urb(t500rs->urb);
        return T500RS_ERROR_TIMEOUT;
    }

    return T500RS_SUCCESS;
}

int t500rs_send_int(struct t500rs_device_entry *t500rs, u8 cmd, u8 id)
{
    if (!t500rs || !t500rs->buffer)
        return -EINVAL;

    t500rs->buffer[0] = cmd;
    t500rs->buffer[1] = id;
    memset(t500rs->buffer + 2, 0, t500rs->buffer_length - 2);

    return t500rs_send_buf(t500rs, t500rs->buffer, t500rs->buffer_length);
}

int t500rs_interrupts(struct t500rs_device_entry *t500rs)
{
    struct usb_device *udev = t500rs->usbdev;
    struct usb_interface *intf = to_usb_interface(t500rs->hdev->dev.parent);
    struct usb_host_interface *interface = intf->cur_altsetting;
    struct usb_endpoint_descriptor *endpoint;
    int i;

    for (i = 0; i < interface->desc.bNumEndpoints; i++) {
        endpoint = &interface->endpoint[i].desc;
        if (usb_endpoint_is_int_out(endpoint)) {
            t500rs->endpoint_out = endpoint->bEndpointAddress;
            t500rs->interval = endpoint->bInterval;
            break;
        }
    }

    if (!t500rs->endpoint_out)
        return -ENODEV;

    t500rs->urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!t500rs->urb)
        return -ENOMEM;

    t500rs->buffer = usb_alloc_coherent(udev, T500RS_REPORT_LENGTH,
                                      GFP_KERNEL, &t500rs->buffer_dma);
    if (!t500rs->buffer) {
        usb_free_urb(t500rs->urb);
        return -ENOMEM;
    }

    t500rs->buffer_length = T500RS_REPORT_LENGTH;

    usb_fill_int_urb(t500rs->urb, udev,
                     usb_sndintpipe(udev, t500rs->endpoint_out),
                     t500rs->buffer, t500rs->buffer_length,
                     t500rs_urb_complete, t500rs, t500rs->interval);
    t500rs->urb->transfer_dma = t500rs->buffer_dma;
    t500rs->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

    return 0;
}

int t500rs_send_command(struct t500rs_device_entry *t500rs, u8 cmd_id, u8 param1, u8 param2)
{
    u8 buf[T500RS_FF_LENGTH];

    buf[0] = T500RS_FF_REPORT_ID;
    buf[1] = cmd_id;
    buf[2] = param1;
    buf[3] = param2;

    return t500rs_send_buf(t500rs, buf, sizeof(buf));
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Force feedback support for Thrustmaster T500RS - USB Communication"); 