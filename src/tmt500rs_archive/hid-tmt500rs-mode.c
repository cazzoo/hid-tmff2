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
int t500rs_init_mode_switch(struct t500rs_device_entry *t500rs)
{
    struct t500rs_mode_switch *mode_switch;

    if (!t500rs || !t500rs->data)
        return -EINVAL;

    mode_switch = kzalloc(sizeof(*mode_switch), GFP_KERNEL);
    if (!mode_switch)
        return -ENOMEM;

    mode_switch->state = T500RS_MODE_STATE_INIT;
    mode_switch->current_mode = 0;
    mode_switch->target_mode = 0;
    mode_switch->retries = 0;
    mode_switch->last_attempt = jiffies;
    mode_switch->switch_start_time = 0;
    mode_switch->force_retry = false;
    mode_switch->usb_initialized = false;

    t500rs->data->mode_switch = mode_switch;
    return 0;
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
    struct usb_interface *intf;

    if (!t500rs || !t500rs->data || !t500rs->data->mode_switch)
        return -EINVAL;

    mode_switch = t500rs->data->mode_switch;
    data = t500rs->data;
    current_time = jiffies;

    /* Global timeout check */
    if (mode_switch->switch_start_time && 
        time_after(current_time, mode_switch->switch_start_time + msecs_to_jiffies(120000))) {
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
        mode_switch->usb_initialized = false;

        /* Clean up any existing USB state */
        t500rs_cleanup_usb(t500rs);
        msleep(10000);  // Wait longer for cleanup to complete
        break;

    case T500RS_MODE_STATE_DETECT:
        /* Initialize USB if needed */
        if (!mode_switch->usb_initialized) {
            /* Wait for device to settle */
            msleep(10000);  // Wait longer for device to settle

            ret = t500rs_init_usb(t500rs);
            if (ret) {
                if (++mode_switch->retries < 10) {  // More retries
                    dev_info(&t500rs->data->hdev->dev, "USB init retry %d/10\n", mode_switch->retries);
                    msleep(10000 * (mode_switch->retries + 1));  // Longer exponential backoff
                    break;
                }
                dev_err(&t500rs->data->hdev->dev, "USB init failed after retries\n");
                mode_switch->state = T500RS_MODE_STATE_ERROR;
                break;
            }
            mode_switch->usb_initialized = true;
            msleep(15000);  // Wait longer for USB to stabilize
        }

        /* Detect current mode */
        dev_info(&t500rs->data->hdev->dev, "Detecting current mode\n");
        ret = t500rs_detect_mode(t500rs, &mode_switch->current_mode);
        if (ret) {
            if (++mode_switch->retries < 10) {  // More retries
                dev_info(&t500rs->data->hdev->dev, "Mode detection retry %d/10\n", mode_switch->retries);
                msleep(8000 * (mode_switch->retries + 1));  // Longer exponential backoff
                break;
            }
            dev_err(&t500rs->data->hdev->dev, "Mode detection failed after retries\n");
            mode_switch->state = T500RS_MODE_STATE_ERROR;
            break;
        }
        dev_info(&t500rs->data->hdev->dev, "Current mode: 0x%02x\n", mode_switch->current_mode);

        /* If already in wheel mode, skip to verification */
        if (mode_switch->current_mode == TMT500RS_MODE_WHEEL) {
            dev_info(&t500rs->data->hdev->dev, "Already in wheel mode, skipping switch\n");
            mode_switch->state = T500RS_MODE_STATE_VERIFY;
            mode_switch->target_mode = TMT500RS_MODE_WHEEL;
            msleep(10000);  // Wait longer before verification
            break;
        }

        mode_switch->state = T500RS_MODE_STATE_SWITCHING;
        mode_switch->retries = 0;
        mode_switch->last_attempt = current_time;
        msleep(10000);  // Wait longer before switching
        break;

    case T500RS_MODE_STATE_SWITCHING:
        /* Switch to target mode */
        if (mode_switch->current_mode == mode_switch->target_mode && !mode_switch->force_retry) {
            dev_info(&t500rs->data->hdev->dev, "Already in target mode 0x%02x\n", mode_switch->target_mode);
            mode_switch->state = T500RS_MODE_STATE_VERIFY;
            msleep(10000);  // Wait longer before verification
            break;
        }

        dev_info(&t500rs->data->hdev->dev, "Switching from mode 0x%02x to 0x%02x\n",
                mode_switch->current_mode, mode_switch->target_mode);

        /* Stop all URBs and USB activity before mode switch */
        t500rs_stop_urbs(t500rs);
        msleep(15000);  // Wait longer for URBs to complete

        /* Disable USB autosuspend */
        intf = to_usb_interface(data->hdev->dev.parent);
        if (intf) {
            usb_autopm_get_interface(intf);
            msleep(8000);  // Wait longer for power management
        }

        /* Set state before sending command */
        data->state = T500RS_STATE_RECONNECTING;
        
        /* Send mode switch command without waiting for response */
        ret = t500rs_send_command(t500rs, 0x0f, 0x03, mode_switch->target_mode);
        if (ret) {
            if (++mode_switch->retries < 10) {  // More retries
                dev_info(&t500rs->data->hdev->dev, "Mode switch command retry %d/10\n", mode_switch->retries);
                msleep(15000 * (mode_switch->retries + 1));  // Longer exponential backoff
                break;
            }
            dev_err(&t500rs->data->hdev->dev, "Mode switch command failed after retries\n");
            mode_switch->state = T500RS_MODE_STATE_ERROR;
            break;
        }

        /* Re-enable USB autosuspend */
        if (intf) {
            msleep(8000);  // Wait longer before re-enabling
            usb_autopm_put_interface(intf);
        }

        mode_switch->state = T500RS_MODE_STATE_WAIT_DISCONNECT;
        mode_switch->retries = 0;
        mode_switch->last_attempt = current_time;
        msleep(20000);  // Give device more time to process command
        break;

    case T500RS_MODE_STATE_WAIT_DISCONNECT:
        /* Check if already disconnected */
        if (!data->usbdev || data->state == T500RS_STATE_DISCONNECTED) {
            dev_info(&t500rs->data->hdev->dev, "Device disconnected, waiting for reconnect\n");
            mode_switch->state = T500RS_MODE_STATE_WAIT_RECONNECT;
            mode_switch->retries = 0;
            mode_switch->last_attempt = current_time;
            msleep(20000);  // Give device more time to settle
            break;
        }

        /* Wait for device to disconnect with timeout */
        timeout = mode_switch->last_attempt + msecs_to_jiffies(60000);  // Longer timeout
        if (time_after(current_time, timeout)) {
            if (++mode_switch->retries < 8) {  // More retries
                dev_info(&t500rs->data->hdev->dev, "Disconnect timeout, retry %d/8\n", mode_switch->retries);
                mode_switch->state = T500RS_MODE_STATE_SWITCHING;
                mode_switch->force_retry = true;
                msleep(15000);  // Longer delay before retry
                break;
            }
            dev_err(&t500rs->data->hdev->dev, "Device disconnect timeout after retries\n");
            mode_switch->state = T500RS_MODE_STATE_ERROR;
            break;
        }
        msleep(8000);  // Check less frequently
        break;

    case T500RS_MODE_STATE_WAIT_RECONNECT:
        /* Wait for device to reconnect with timeout */
        timeout = mode_switch->last_attempt + msecs_to_jiffies(60000);  // Longer timeout
        if (data->usbdev && data->state == T500RS_STATE_READY) {
            dev_info(&t500rs->data->hdev->dev, "Device reconnected, waiting for stability\n");
            msleep(20000);  // Wait longer for device to stabilize
            mode_switch->state = T500RS_MODE_STATE_VERIFY;
            mode_switch->retries = 0;
            break;
        }

        /* Check for transitional states */
        if (data->usbdev && data->state == T500RS_STATE_INITIALIZING) {
            dev_info(&t500rs->data->hdev->dev, "Device initializing, waiting...\n");
            msleep(8000);  // Wait longer between checks
            break;
        }

        if (time_after(current_time, timeout)) {
            if (++mode_switch->retries < 8) {  // More retries
                dev_info(&t500rs->data->hdev->dev, "Reconnect timeout, retry %d/8\n", mode_switch->retries);
                mode_switch->state = T500RS_MODE_STATE_SWITCHING;
                mode_switch->force_retry = true;
                msleep(15000);  // Longer delay before retry
                break;
            }
            dev_err(&t500rs->data->hdev->dev, "Device reconnect timeout after retries\n");
            mode_switch->state = T500RS_MODE_STATE_ERROR;
            break;
        }
        msleep(8000);  // Check less frequently
        break;

    case T500RS_MODE_STATE_VERIFY:
        /* Verify current mode */
        ret = t500rs_detect_mode(t500rs, &mode_switch->current_mode);
        if (ret) {
            if (++mode_switch->retries < 8) {  // More retries
                dev_info(&t500rs->data->hdev->dev, "Mode verification retry %d/8\n", mode_switch->retries);
                msleep(8000 * (mode_switch->retries + 1));  // Longer exponential backoff
                break;
            }
            dev_err(&t500rs->data->hdev->dev, "Mode verification failed after retries\n");
            mode_switch->state = T500RS_MODE_STATE_ERROR;
            break;
        }

        if (mode_switch->current_mode != mode_switch->target_mode) {
            if (++mode_switch->retries < 8) {  // More retries
                dev_info(&t500rs->data->hdev->dev, "Mode mismatch (current: 0x%02x, target: 0x%02x), retry %d/8\n",
                        mode_switch->current_mode, mode_switch->target_mode, mode_switch->retries);
                mode_switch->state = T500RS_MODE_STATE_SWITCHING;
                mode_switch->force_retry = true;
                msleep(15000);  // Longer delay before retry
                break;
            }
            dev_err(&t500rs->data->hdev->dev, "Mode verification failed: wrong mode\n");
            mode_switch->state = T500RS_MODE_STATE_ERROR;
            break;
        }

        dev_info(&t500rs->data->hdev->dev, "Mode switch completed successfully\n");
        mode_switch->state = T500RS_MODE_STATE_DONE;
        break;

    case T500RS_MODE_STATE_ERROR:
        dev_err(&t500rs->data->hdev->dev, "Mode switch failed\n");
        return -EIO;

    case T500RS_MODE_STATE_DONE:
        return 0;

    default:
        dev_err(&t500rs->data->hdev->dev, "Invalid mode switch state %d\n", mode_switch->state);
        return -EINVAL;
    }

    return 0;
}
EXPORT_SYMBOL_GPL(t500rs_handle_mode_switch);

/* Start mode switch */
int t500rs_start_mode_switch(struct t500rs_device_entry *t500rs, u8 target_mode)
{
    struct t500rs_mode_switch *mode_switch;
    struct t500rs_device_data *data;
    struct usb_interface *intf;
    enum usb_device_state dev_state;

    if (!t500rs || !t500rs->data || !t500rs->data->mode_switch)
        return -EINVAL;

    mode_switch = t500rs->data->mode_switch;
    data = t500rs->data;

    /* Log initial USB state */
    if (debug) {
        dev_state = data->usbdev->state;
        dev_info(&data->hdev->dev, 
                "Mode switch start - USB state: %d (%s)\n",
                dev_state,
                dev_state == USB_STATE_CONFIGURED ? "CONFIGURED" :
                dev_state == USB_STATE_SUSPENDED ? "SUSPENDED" :
                dev_state == USB_STATE_DEFAULT ? "DEFAULT" :
                dev_state == USB_STATE_ADDRESS ? "ADDRESS" : "UNKNOWN");
    }

    /* Don't start if already in progress */
    if (mode_switch->state != T500RS_MODE_STATE_INIT &&
        mode_switch->state != T500RS_MODE_STATE_DONE &&
        mode_switch->state != T500RS_MODE_STATE_ERROR)
        return -EBUSY;

    /* Disable USB autosuspend before mode switch */
    intf = to_usb_interface(data->hdev->dev.parent);
    if (intf) {
        if (debug)
            dev_info(&data->hdev->dev, "Disabling USB autosuspend for mode switch\n");
        usb_autopm_get_interface(intf);
    }

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
    enum usb_device_state dev_state;

    if (!t500rs || !t500rs->data || !mode)
        return -EINVAL;

    if (debug) {
        dev_state = t500rs->data->usbdev->state;
        dev_info(&t500rs->data->hdev->dev, 
                "Mode detection - USB state: %d (%s)\n",
                dev_state,
                dev_state == USB_STATE_CONFIGURED ? "CONFIGURED" :
                dev_state == USB_STATE_SUSPENDED ? "SUSPENDED" :
                dev_state == USB_STATE_DEFAULT ? "DEFAULT" :
                dev_state == USB_STATE_ADDRESS ? "ADDRESS" : "UNKNOWN");
    }

    ret = t500rs_send_cmd_with_retry(t500rs, cmd, sizeof(cmd), 3);
    if (ret < 0) {
        if (debug)
            dev_info(&t500rs->data->hdev->dev, "Mode detection command failed: %d\n", ret);
        return ret;
    }

    ret = t500rs_read_response(t500rs, resp, sizeof(resp));
    if (ret < 0) {
        if (debug)
            dev_info(&t500rs->data->hdev->dev, "Mode detection response failed: %d\n", ret);
        return ret;
    }

    *mode = resp[2];  /* Mode is in third byte */
    
    if (debug)
        dev_info(&t500rs->data->hdev->dev, "Detected mode: 0x%02x\n", *mode);
    
    return 0;
}
EXPORT_SYMBOL_GPL(t500rs_detect_mode);

/* Verify current mode */
int t500rs_verify_mode(struct t500rs_device_entry *t500rs, u8 expected_mode)
{
    u8 current_mode;
    int ret;
    enum usb_device_state dev_state;

    if (debug) {
        dev_state = t500rs->data->usbdev->state;
        dev_info(&t500rs->data->hdev->dev, 
                "Mode verification - USB state: %d (%s), expected mode: 0x%02x\n",
                dev_state,
                dev_state == USB_STATE_CONFIGURED ? "CONFIGURED" :
                dev_state == USB_STATE_SUSPENDED ? "SUSPENDED" :
                dev_state == USB_STATE_DEFAULT ? "DEFAULT" :
                dev_state == USB_STATE_ADDRESS ? "ADDRESS" : "UNKNOWN",
                expected_mode);
    }

    ret = t500rs_detect_mode(t500rs, &current_mode);
    if (ret) {
        if (debug)
            dev_info(&t500rs->data->hdev->dev, "Mode verification failed: %d\n", ret);
        return ret;
    }

    if (debug)
        dev_info(&t500rs->data->hdev->dev, "Mode verification - current: 0x%02x, expected: 0x%02x\n",
                current_mode, expected_mode);

    return (current_mode == expected_mode) ? 0 : -EINVAL;
}
EXPORT_SYMBOL_GPL(t500rs_verify_mode);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Force feedback support for Thrustmaster T500RS - Mode Switch"); 