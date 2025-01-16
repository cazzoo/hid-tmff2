// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Force feedback support for Thrustmaster T500RS - Mode Switch
 *
 * Copyright (c) 2024 Your Name
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/hid.h>
#include <linux/delay.h>
#include "hid-tmt500rs.h"
#include "hid-tmt500rs-utils.h"

/* Initialize mode switch context */
void t500rs_init_mode_switch(struct t500rs_device_entry *t500rs)
{
    struct t500rs_mode_switch *mode_switch;

    if (!t500rs || !t500rs->data)
        return;

    mode_switch = kzalloc(sizeof(*mode_switch), GFP_KERNEL);
    if (!mode_switch)
        return;

    mode_switch->state = T500RS_MODE_STATE_INIT;
    mode_switch->current_mode = 0;
    mode_switch->target_mode = 0;
    mode_switch->retries = 0;
    mode_switch->last_attempt = jiffies;
    mode_switch->switch_start_time = 0;
    mode_switch->force_retry = false;

    t500rs->data->mode_switch = mode_switch;
}
EXPORT_SYMBOL_GPL(t500rs_init_mode_switch);

/* Clean up mode switch context */
void t500rs_cleanup_mode_switch(struct t500rs_device_entry *t500rs)
{
    if (!t500rs || !t500rs->data || !t500rs->data->mode_switch)
        return;

    kfree(t500rs->data->mode_switch);
    t500rs->data->mode_switch = NULL;
}
EXPORT_SYMBOL_GPL(t500rs_cleanup_mode_switch);

/* Handle mode switch state machine */
int t500rs_handle_mode_switch(struct t500rs_device_entry *t500rs)
{
    struct t500rs_mode_switch *mode_switch;
    struct t500rs_device_data *data;
    int ret = 0;
    unsigned long timeout;
    unsigned long current_time;

    if (!t500rs || !t500rs->data || !t500rs->data->mode_switch)
        return -EINVAL;

    mode_switch = t500rs->data->mode_switch;
    data = t500rs->data;
    current_time = jiffies;

    /* Global timeout check */
    if (mode_switch->switch_start_time && 
        time_after(current_time, mode_switch->switch_start_time + msecs_to_jiffies(30000))) {
        dev_err(&t500rs->data->hdev->dev, "Mode switch global timeout\n");
        mode_switch->state = T500RS_MODE_STATE_ERROR;
        return -ETIMEDOUT;
    }

    switch (mode_switch->state) {
    case T500RS_MODE_STATE_INIT:
        /* Initialize mode switch */
        dev_info(&t500rs->data->hdev->dev, "Starting mode switch initialization\n");
        mode_switch->state = T500RS_MODE_STATE_DETECT;
        mode_switch->retries = 0;
        mode_switch->last_attempt = current_time;
        mode_switch->switch_start_time = current_time;
        mode_switch->force_retry = false;
        break;

    case T500RS_MODE_STATE_DETECT:
        /* Detect current mode */
        dev_info(&t500rs->data->hdev->dev, "Detecting current mode\n");
        ret = t500rs_detect_mode(t500rs, &mode_switch->current_mode);
        if (ret) {
            if (++mode_switch->retries < 5) {
                dev_info(&t500rs->data->hdev->dev, "Mode detection retry %d/5\n", mode_switch->retries);
                msleep(200 * (mode_switch->retries + 1));  // Exponential backoff
                break;
            }
            dev_err(&t500rs->data->hdev->dev, "Mode detection failed after retries\n");
            mode_switch->state = T500RS_MODE_STATE_ERROR;
            break;
        }
        dev_info(&t500rs->data->hdev->dev, "Current mode: 0x%02x\n", mode_switch->current_mode);
        mode_switch->state = T500RS_MODE_STATE_SWITCHING;
        mode_switch->retries = 0;
        mode_switch->last_attempt = current_time;
        break;

    case T500RS_MODE_STATE_SWITCHING:
        /* Switch to target mode */
        if (mode_switch->current_mode == mode_switch->target_mode && !mode_switch->force_retry) {
            dev_info(&t500rs->data->hdev->dev, "Already in target mode 0x%02x\n", mode_switch->target_mode);
            mode_switch->state = T500RS_MODE_STATE_VERIFY;
            break;
        }

        dev_info(&t500rs->data->hdev->dev, "Switching from mode 0x%02x to 0x%02x\n",
                mode_switch->current_mode, mode_switch->target_mode);

        /* Stop all URBs and USB activity before mode switch */
        t500rs_stop_urbs(t500rs);
        msleep(1000);  // Wait longer for URBs to complete

        /* Set state before sending command */
        data->state = T500RS_STATE_RECONNECTING;
        
        /* Send mode switch command without waiting for response */
        ret = t500rs_send_command(t500rs, 0x0f, 0x03, mode_switch->target_mode);
        if (ret) {
            if (++mode_switch->retries < 5) {
                dev_info(&t500rs->data->hdev->dev, "Mode switch command retry %d/5\n", mode_switch->retries);
                msleep(1000 * (mode_switch->retries + 1));  // Longer exponential backoff
                break;
            }
            dev_err(&t500rs->data->hdev->dev, "Mode switch command failed after retries\n");
            mode_switch->state = T500RS_MODE_STATE_ERROR;
            break;
        }

        mode_switch->state = T500RS_MODE_STATE_WAIT_DISCONNECT;
        mode_switch->retries = 0;
        mode_switch->last_attempt = current_time;
        msleep(3000);  // Give device more time to process command
        break;

    case T500RS_MODE_STATE_WAIT_DISCONNECT:
        /* Check if already disconnected */
        if (!data->usbdev || data->state == T500RS_STATE_DISCONNECTED) {
            dev_info(&t500rs->data->hdev->dev, "Device disconnected, waiting for reconnect\n");
            mode_switch->state = T500RS_MODE_STATE_WAIT_RECONNECT;
            mode_switch->retries = 0;
            mode_switch->last_attempt = current_time;
            msleep(5000);  // Give device more time to settle
            break;
        }

        /* Wait for device to disconnect with timeout */
        timeout = mode_switch->last_attempt + msecs_to_jiffies(15000);  // Longer timeout
        if (time_after(current_time, timeout)) {
            if (++mode_switch->retries < 3) {
                dev_info(&t500rs->data->hdev->dev, "Disconnect timeout, retry %d/3\n", mode_switch->retries);
                mode_switch->state = T500RS_MODE_STATE_SWITCHING;
                mode_switch->force_retry = true;
                msleep(2000);  // Longer delay before retry
                break;
            }
            dev_err(&t500rs->data->hdev->dev, "Device disconnect timeout after retries\n");
            mode_switch->state = T500RS_MODE_STATE_ERROR;
            break;
        }
        msleep(500);  // Check less frequently
        break;

    case T500RS_MODE_STATE_WAIT_RECONNECT:
        /* Wait for device to reconnect with timeout */
        timeout = mode_switch->last_attempt + msecs_to_jiffies(20000);  // Longer timeout
        if (data->usbdev && data->state == T500RS_STATE_READY) {
            dev_info(&t500rs->data->hdev->dev, "Device reconnected, waiting for stability\n");
            
            /* Disable USB autosuspend during mode switch */
            struct usb_interface *intf = to_usb_interface(data->hdev->dev.parent);
            if (intf)
                usb_autopm_get_interface(intf);
            
            /* Wait for device to stabilize */
            msleep(5000);
            
            /* Verify USB device is still present */
            if (!data->usbdev) {
                dev_info(&t500rs->data->hdev->dev, "Device disconnected during stabilization\n");
                if (++mode_switch->retries < 3) {
                    dev_info(&t500rs->data->hdev->dev, "Retrying reconnection %d/3\n", mode_switch->retries);
                    msleep(2000);
                    break;
                }
                mode_switch->state = T500RS_MODE_STATE_ERROR;
                break;
            }
            
            /* Re-initialize USB communication */
            ret = t500rs_init_usb(t500rs);
            if (ret) {
                dev_err(&t500rs->data->hdev->dev, "Failed to reinitialize USB: %d\n", ret);
                if (++mode_switch->retries < 3) {
                    dev_info(&t500rs->data->hdev->dev, "Retrying USB initialization %d/3\n", mode_switch->retries);
                    msleep(2000);
                    break;
                }
                mode_switch->state = T500RS_MODE_STATE_ERROR;
                break;
            }
            
            /* Wait for USB initialization to complete */
            msleep(3000);
            
            /* Verify USB communication is stable */
            if (!data->usbdev || !data->urb || data->state != T500RS_STATE_READY) {
                dev_err(&t500rs->data->hdev->dev, "USB communication not stable after initialization\n");
                if (++mode_switch->retries < 3) {
                    dev_info(&t500rs->data->hdev->dev, "Retrying stabilization %d/3\n", mode_switch->retries);
                    msleep(2000);
                    break;
                }
                mode_switch->state = T500RS_MODE_STATE_ERROR;
                break;
            }
            
            /* Re-enable USB autosuspend */
            if (intf)
                usb_autopm_put_interface(intf);
            
            /* Proceed to verification after successful USB initialization */
            mode_switch->state = T500RS_MODE_STATE_VERIFY;
            mode_switch->retries = 0;
            msleep(2000);  // Additional settling time
            break;
        }

        /* Check for transitional states */
        if (data->usbdev && data->state == T500RS_STATE_INITIALIZING) {
            dev_info(&t500rs->data->hdev->dev, "Device initializing, waiting...\n");
            msleep(1000);
            break;
        }

        if (time_after(current_time, timeout)) {
            if (++mode_switch->retries < 3) {
                dev_info(&t500rs->data->hdev->dev, "Reconnect timeout, retry %d/3\n", mode_switch->retries);
                mode_switch->state = T500RS_MODE_STATE_SWITCHING;
                mode_switch->force_retry = true;
                msleep(2000);  // Longer delay before retry
                break;
            }
            dev_err(&t500rs->data->hdev->dev, "Device reconnect timeout after retries\n");
            mode_switch->state = T500RS_MODE_STATE_ERROR;
            break;
        }
        msleep(500);  // Check less frequently
        break;

    case T500RS_MODE_STATE_VERIFY:
        /* Verify mode switch */
        dev_info(&t500rs->data->hdev->dev, "Verifying mode switch\n");
        
        /* Ensure USB communication is stable */
        if (!data->usbdev || !data->urb || data->state != T500RS_STATE_READY) {
            dev_err(&t500rs->data->hdev->dev, "USB communication not stable\n");
            if (++mode_switch->retries < 3) {
                dev_info(&t500rs->data->hdev->dev, "Retrying USB stabilization %d/3\n", mode_switch->retries);
                msleep(2000);
                break;
            }
            mode_switch->state = T500RS_MODE_STATE_ERROR;
            break;
        }
        
        ret = t500rs_verify_mode(t500rs, mode_switch->target_mode);
        if (ret) {
            if (++mode_switch->retries < 3) {
                dev_info(&t500rs->data->hdev->dev, "Mode verification retry %d/3\n", mode_switch->retries);
                msleep(2000);  // Longer delay between retries
                break;
            }
            dev_err(&t500rs->data->hdev->dev, "Mode verification failed after retries\n");
            mode_switch->state = T500RS_MODE_STATE_ERROR;
            break;
        }

        /* Initialize device in new mode */
        dev_info(&t500rs->data->hdev->dev, "Initializing device in new mode\n");
        ret = t500rs_send_command(t500rs, 0x0f, 0x01, 0x00);
        if (ret) {
            if (++mode_switch->retries < 3) {
                dev_info(&t500rs->data->hdev->dev, "Init command retry %d/3\n", mode_switch->retries);
                msleep(2000);  // Longer delay between retries
                break;
            }
            dev_err(&t500rs->data->hdev->dev, "Init command failed after retries\n");
            mode_switch->state = T500RS_MODE_STATE_ERROR;
            break;
        }

        /* Wait for initialization to complete */
        msleep(5000);  // Give device more time to initialize
        
        /* Final USB stability check */
        if (!data->usbdev || !data->urb || data->state != T500RS_STATE_READY) {
            dev_err(&t500rs->data->hdev->dev, "Lost USB communication after initialization\n");
            if (++mode_switch->retries < 3) {
                dev_info(&t500rs->data->hdev->dev, "Retrying initialization %d/3\n", mode_switch->retries);
                msleep(2000);
                break;
            }
            mode_switch->state = T500RS_MODE_STATE_ERROR;
            break;
        }

        dev_info(&t500rs->data->hdev->dev, "Mode switch completed successfully\n");
        mode_switch->state = T500RS_MODE_STATE_COMPLETE;
        break;

    case T500RS_MODE_STATE_COMPLETE:
        /* Mode switch complete */
        ret = 0;
        break;

    case T500RS_MODE_STATE_ERROR:
        /* Mode switch failed */
        ret = -EIO;
        break;
    }

    return ret;
}
EXPORT_SYMBOL_GPL(t500rs_handle_mode_switch);

/* Start mode switch */
int t500rs_start_mode_switch(struct t500rs_device_entry *t500rs, u8 target_mode)
{
    struct t500rs_mode_switch *mode_switch;

    if (!t500rs || !t500rs->data || !t500rs->data->mode_switch)
        return -EINVAL;

    mode_switch = t500rs->data->mode_switch;

    /* Don't start if already in progress */
    if (mode_switch->state != T500RS_MODE_STATE_INIT &&
        mode_switch->state != T500RS_MODE_STATE_COMPLETE &&
        mode_switch->state != T500RS_MODE_STATE_ERROR)
        return -EBUSY;

    mode_switch->state = T500RS_MODE_STATE_INIT;
    mode_switch->target_mode = target_mode;
    mode_switch->retries = 0;
    mode_switch->last_attempt = jiffies;
    mode_switch->switch_start_time = jiffies;
    mode_switch->force_retry = false;

    return t500rs_handle_mode_switch(t500rs);
}
EXPORT_SYMBOL_GPL(t500rs_start_mode_switch);

/* Detect current mode */
int t500rs_detect_mode(struct t500rs_device_entry *t500rs, u8 *mode)
{
    u8 cmd[4] = {0x03, 0x0f, 0x04, 0x00};  /* Get mode command */
    u8 resp[4];
    int ret;

    if (!t500rs || !t500rs->data || !mode)
        return -EINVAL;

    ret = t500rs_send_cmd_with_retry(t500rs, cmd, sizeof(cmd), 3);
    if (ret < 0)
        return ret;

    ret = t500rs_read_response(t500rs, resp, sizeof(resp));
    if (ret < 0)
        return ret;

    *mode = resp[2];  /* Mode is in third byte */
    return 0;
}
EXPORT_SYMBOL_GPL(t500rs_detect_mode);

/* Verify current mode */
int t500rs_verify_mode(struct t500rs_device_entry *t500rs, u8 expected_mode)
{
    u8 current_mode;
    int ret;

    ret = t500rs_detect_mode(t500rs, &current_mode);
    if (ret)
        return ret;

    return (current_mode == expected_mode) ? 0 : -EINVAL;
}
EXPORT_SYMBOL_GPL(t500rs_verify_mode);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Force feedback support for Thrustmaster T500RS - Mode Switch"); 