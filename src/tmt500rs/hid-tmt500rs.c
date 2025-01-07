// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Force feedback support for Thrustmaster T500RS
 *
 * Copyright (c) 2024 Your Name
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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

/* USB endpoints */
#define T500RS_ENDPOINT_IN   0x82
#define T500RS_ENDPOINT_OUT  0x01

/* Supported effects */
static const signed short t500rs_supported_effects[] = {
    FF_CONSTANT,
    -1  /* Terminator */
};

/* Device initialization data */
struct setup_command {
    u8 cmd;
    u8 id;
    u8 data[6];
};

struct setup_timing {
    u16 pre_delay;
    u16 post_delay;
    u16 retry_delay;
    u16 verify_delay;
};

struct setup_retry_limit {
    u8 command_retries;
    u8 verify_retries;
};

static const u8 setup_arr[][8] = {
    {0x42, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // Init command
    {0x41, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // Mode command
    {0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // Open command
    {0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // Enable interrupts
    {0x08, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // Effect control
    {0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // Upload effect
    {0x41, 0x04, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00},  // Set gain
};

static const size_t setup_arr_sizes[] = {8, 8, 8, 8, 8, 8, 8};

static const char *setup_descriptions[] = {
    "Init command",
    "Mode command",
    "Open command",
    "Enable interrupts",
    "Effect control",
    "Upload effect",
    "Set gain"
};

static const u16 setup_delays[] = {
    1000,  // Init command
    500,   // Mode command
    200,   // Open command
    200,   // Enable interrupts
    200,   // Effect control
    200,   // Upload effect
    200    // Set gain
};

static const struct setup_timing setup_timings[] = {
    {1000, 500, 200, 300},  // Init command
    {500,  300, 200, 300},  // Mode command
    {200,  200, 200, 300},  // Open command
    {200,  200, 200, 300},  // Enable interrupts
    {200,  200, 200, 300},  // Effect control
    {200,  200, 200, 300},  // Upload effect
    {200,  200, 200, 300}   // Set gain
};

static const struct setup_retry_limit setup_retry_limits[] = {
    {3, 2},  // Init command
    {3, 2},  // Mode command
    {3, 2},  // Open command
    {3, 2},  // Enable interrupts
    {3, 2},  // Effect control
    {3, 2},  // Upload effect
    {3, 2}   // Set gain
};

static int t500rs_send_int(struct t500rs_device_entry *t500rs)
{
	memset(t500rs->send_buffer + 2, 0, t500rs->buffer_length - 2);
	return t500rs_send_buf(t500rs, t500rs->send_buffer + 2, t500rs->buffer_length);
}

/* Helper function to send data using the report */
static int t500rs_send_effect(struct t500rs_device_entry *t500rs, u8 *data, size_t len)
{
    int ret;

    if (!t500rs || !data)
        return -EINVAL;

    if (t500rs->state != T500RS_STATE_READY) {
        t500rs_warn(t500rs, "device not ready, ignoring effect\n");
        return -EAGAIN;
    }

    ret = t500rs_send_buf(t500rs, data, len);
    if (ret < 0) {
        t500rs_err(t500rs, "failed to send effect: %d\n", ret);
        return ret;
    }

    return 0;
}

static int t500rs_upload_constant_force(struct t500rs_device_entry *t500rs,
                                      struct tmff2_effect_state *effect)
{
    u8 *send_buffer;
    int ret;
    s16 level;
    u8 scaled_level;
    u16 attack_length, fade_length;
    u16 attack_level, fade_level;
    bool has_envelope = false;

    if (!t500rs || !effect) {
        t500rs_err(t500rs, "Invalid parameters in upload_constant_force\n");
        return -EINVAL;
    }

    if (t500rs->state != T500RS_STATE_READY) {
        t500rs_warn(t500rs, "Device not ready for constant force upload\n");
        return -EAGAIN;
    }

    send_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
    if (!send_buffer) {
        t500rs_err(t500rs, "Failed to allocate constant force buffer\n");
        return -ENOMEM;
    }

    // Check for envelope
    if (effect->effect.u.constant.envelope.attack_length || 
        effect->effect.u.constant.envelope.fade_length) {
        has_envelope = true;
        attack_length = effect->effect.u.constant.envelope.attack_length;
        fade_length = effect->effect.u.constant.envelope.fade_length;
        attack_level = effect->effect.u.constant.envelope.attack_level;
        fade_level = effect->effect.u.constant.envelope.fade_level;
        
        t500rs_dbg(t500rs, "Constant force envelope: attack=%u/%u fade=%u/%u\n",
                  attack_length, attack_level, fade_length, fade_level);
    }

    // Map level from -32767...32767 to 0...255 with improved precision
    level = effect->effect.u.constant.level;
    scaled_level = (u8)(((s32)(level + 32768) * 255) / 65536);
    
    t500rs_dbg(t500rs, "Constant force level: %d -> %u\n", level, scaled_level);

    // Upload effect parameters with retries
    int retries = 0;
    bool upload_success = false;
    while (retries < T500RS_MAX_RETRIES && !upload_success) {
        // Clear any previous data
        memset(send_buffer, 0, t500rs->buffer_length);

        // Upload command
        send_buffer[0] = 0x02;  // Upload command
        send_buffer[1] = 0x1c;  // Constant force subtype
        send_buffer[2] = effect->effect.id & 0xFF;  // Effect ID
        send_buffer[3] = has_envelope ? 0x01 : 0x00;  // Envelope flag
        
        ret = t500rs_send_effect(t500rs, send_buffer, t500rs->buffer_length);
        if (ret >= 0) {
            upload_success = true;
            break;
        }

        if (ret == -EPIPE || ret == -EPROTO) {
            t500rs_warn(t500rs, "Upload retry %d after error: %d\n", 
                       retries + 1, ret);
            msleep(50 * (retries + 1));  // Progressive delay
            retries++;
        } else {
            t500rs_err(t500rs, "Critical error in upload: %d\n", ret);
            goto err_free;
        }
    }

    if (!upload_success) {
        t500rs_err(t500rs, "Failed to upload effect after retries\n");
        ret = -ETIMEDOUT;
        goto err_free;
    }

    // Wait before sending force level
    msleep(50);

    // Set force level with envelope if present
    memset(send_buffer, 0, t500rs->buffer_length);
    send_buffer[0] = 0x03;  // Modify command
    send_buffer[1] = 0x0e;  // Force level subtype
    send_buffer[2] = effect->effect.id & 0xFF;  // Effect ID
    send_buffer[3] = scaled_level;  // Force level

    if (has_envelope) {
        // Pack envelope parameters
        send_buffer[4] = attack_length & 0xFF;
        send_buffer[5] = (attack_length >> 8) & 0xFF;
        send_buffer[6] = attack_level & 0xFF;
        send_buffer[7] = (attack_level >> 8) & 0xFF;
        send_buffer[8] = fade_length & 0xFF;
        send_buffer[9] = (fade_length >> 8) & 0xFF;
        send_buffer[10] = fade_level & 0xFF;
        send_buffer[11] = (fade_level >> 8) & 0xFF;
    }

    ret = t500rs_send_effect(t500rs, send_buffer, t500rs->buffer_length);
    if (ret < 0) {
        t500rs_err(t500rs, "Failed to set force level: %d\n", ret);
        goto err_free;
    }

    // Wait before setting duration
    msleep(50);

    // Set duration with improved timing handling
    memset(send_buffer, 0, t500rs->buffer_length);
    send_buffer[0] = 0x01;  // Duration command
    send_buffer[1] = 0x00;  // Reserved
    send_buffer[2] = effect->effect.id & 0xFF;  // Effect ID
    send_buffer[3] = 0x40;  // Duration flag

    // Handle infinite duration
    if (effect->effect.replay.length == 0) {
        send_buffer[4] = 0xFF;
        send_buffer[5] = 0xFF;
        t500rs_dbg(t500rs, "Setting infinite duration\n");
    } else {
        // Duration in milliseconds (little endian)
        send_buffer[4] = effect->effect.replay.length & 0xFF;
        send_buffer[5] = (effect->effect.replay.length >> 8) & 0xFF;
        t500rs_dbg(t500rs, "Setting duration: %u ms\n", effect->effect.replay.length);
    }

    send_buffer[9] = 0x0e;   // Effect type
    send_buffer[11] = 0x1c;  // Effect subtype

    ret = t500rs_send_effect(t500rs, send_buffer, t500rs->buffer_length);
    if (ret < 0) {
        t500rs_err(t500rs, "Failed to set duration: %d\n", ret);
    } else {
        t500rs_dbg(t500rs, "Successfully uploaded constant force effect\n");
    }

err_free:
    kfree(send_buffer);
    return ret;
}

static int t500rs_play_constant_force(struct t500rs_device_entry *t500rs,
                                    struct tmff2_effect_state *effect,
                                    int value)
{
    u8 *send_buffer;
    int ret;
    bool play_success = false;
    int retries = 0;

    if (!t500rs || !effect) {
        t500rs_err(t500rs, "Invalid parameters in play_constant_force\n");
        return -EINVAL;
    }

    if (t500rs->state != T500RS_STATE_READY) {
        t500rs_warn(t500rs, "Device not ready for constant force playback\n");
        return -EAGAIN;
    }

    if (effect->effect.id >= TMT500RS_MAX_EFFECTS) {
        t500rs_err(t500rs, "Invalid effect ID: %d\n", effect->effect.id);
        return -EINVAL;
    }

    send_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
    if (!send_buffer) {
        t500rs_err(t500rs, "Failed to allocate play buffer\n");
        return -ENOMEM;
    }

    t500rs_dbg(t500rs, "%s constant force effect %d\n",
               value ? "Starting" : "Stopping", effect->effect.id);

    // Try to play/stop effect with retries
    while (retries < T500RS_MAX_RETRIES && !play_success) {
        memset(send_buffer, 0, t500rs->buffer_length);

        if (value) {
            // Start effect with proper ID
            send_buffer[0] = 0x41;  // Play command
            send_buffer[1] = 0x00;  // Reserved
            send_buffer[2] = 0x41;  // Play flag
            send_buffer[3] = 0x01;  // Effect type
            send_buffer[4] = effect->effect.id & 0xFF;  // Effect ID
            send_buffer[5] = 0x01;  // Start flag
        } else {
            // Stop effect with proper ID
            send_buffer[0] = 0x41;  // Play command
            send_buffer[1] = 0x00;  // Reserved
            send_buffer[2] = 0x00;  // Stop flag
            send_buffer[3] = 0x01;  // Effect type
            send_buffer[4] = effect->effect.id & 0xFF;  // Effect ID
            send_buffer[5] = 0x00;  // Stop flag
        }

        // Ensure minimum delay between commands
        if (time_before(jiffies, t500rs->last_command_time + msecs_to_jiffies(50))) {
            msleep(jiffies_to_msecs(t500rs->last_command_time + 
                   msecs_to_jiffies(50) - jiffies));
        }

        ret = t500rs_send_effect(t500rs, send_buffer, t500rs->buffer_length);
        if (ret >= 0) {
            play_success = true;
            t500rs_dbg(t500rs, "Successfully %s effect %d\n",
                      value ? "started" : "stopped", effect->effect.id);

            // Update last command time
            t500rs->last_command_time = jiffies;
            break;
        }

        if (ret == -EPIPE || ret == -EPROTO) {
            t500rs_warn(t500rs, "Play retry %d after error: %d\n",
                       retries + 1, ret);
            msleep(50 * (retries + 1));  // Progressive delay
            retries++;
        } else {
            t500rs_err(t500rs, "Critical error in play: %d\n", ret);
            goto err_free;
        }
    }

    if (!play_success) {
        t500rs_err(t500rs, "Failed to play effect after retries\n");
        ret = -ETIMEDOUT;
    }

    kfree(send_buffer);
    return ret;

err_free:
    kfree(send_buffer);
    return ret;
}

/* USB communication functions */
int t500rs_send_buf(struct t500rs_device_entry *t500rs, u8 *send_buffer, size_t len)
{
    int ret = 0, retries = 0;
    u8 *cmd_buffer;
    size_t cmd_len = 8;  // Fixed size for T500RS commands
    bool success = false;
    unsigned long time_since_last;

    if (!t500rs || !t500rs->hdev || !send_buffer)
        return -EINVAL;

    if (len > cmd_len)
        return -EINVAL;

    if (t500rs->state == T500RS_STATE_ERROR) {
        hid_err(t500rs->hdev, "device in error state, cannot send command\n");
        return -ENODEV;
    }

    if (t500rs->state != T500RS_STATE_READY && 
        t500rs->state != T500RS_STATE_INITIALIZING) {
        hid_err(t500rs->hdev, "device not ready (state: %d)\n", t500rs->state);
        return -EAGAIN;
    }

    // Check time since last command
    time_since_last = jiffies - t500rs->last_command_time;
    if (time_since_last < msecs_to_jiffies(50)) {  // Minimum 50ms between commands
        msleep(50 - jiffies_to_msecs(time_since_last));
    }

    cmd_buffer = kzalloc(cmd_len, GFP_KERNEL);
    if (!cmd_buffer)
        return -ENOMEM;

    memcpy(cmd_buffer, send_buffer, len);

    // Try multiple times with increasing delays
    while (retries < T500RS_MAX_RETRIES && !success) {
        ret = hid_hw_raw_request(t500rs->hdev, t500rs->endpoint_out, 
                                cmd_buffer, cmd_len,
                                HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);
        
        if (ret >= 0) {
            success = true;
            t500rs->command_retries = 0;  // Reset retry counter on success
            t500rs->last_command_time = jiffies;
            break;
        }

        // Don't retry on critical errors
        if (ret != -ETIMEDOUT && ret != -EPROTO && ret != -EPIPE) {
            if (ret != -ENODEV)
                hid_err(t500rs->hdev, "critical error in raw request: %d\n", ret);
            t500rs->state = T500RS_STATE_ERROR;
            break;
        }

        retries++;
        t500rs->command_retries++;
        
        if (t500rs->command_retries >= T500RS_MAX_RETRIES * 2) {
            hid_err(t500rs->hdev, "too many failed commands, marking device as error\n");
            t500rs->state = T500RS_STATE_ERROR;
            break;
        }

        if (retries < T500RS_MAX_RETRIES) {
            hid_warn(t500rs->hdev, "retrying command after error: %d (attempt %d/%d)\n",
                    ret, retries, T500RS_MAX_RETRIES);
            msleep(50 * retries);  // Increasing delay between retries
        }
    }

    if (!success && retries == T500RS_MAX_RETRIES) {
        hid_err(t500rs->hdev, "command failed after %d retries\n", T500RS_MAX_RETRIES);
        ret = -ETIMEDOUT;
    }

    kfree(cmd_buffer);
    return ret;
}

int t500rs_interrupts(struct t500rs_device_entry *t500rs)
{
    int ret;
    struct usb_interface *usbif;
    struct usb_host_interface *interface;
    struct usb_endpoint_descriptor *endpoint;
    int i;

    if (!t500rs || !t500rs->hdev) {
        t500rs_err(t500rs, "Invalid device data in interrupt setup\n");
        return -EINVAL;
    }

    usbif = to_usb_interface(t500rs->hdev->dev.parent);
    if (!usbif) {
        t500rs_err(t500rs, "Failed to get USB interface\n");
        return -ENODEV;
    }

    interface = usbif->cur_altsetting;
    if (!interface) {
        t500rs_err(t500rs, "Failed to get interface altsetting\n");
        return -ENODEV;
    }

    t500rs_info(t500rs, "Configuring endpoints, interface has %d endpoints\n", 
                interface->desc.bNumEndpoints);

    // Find and configure endpoints
    for (i = 0; i < interface->desc.bNumEndpoints; i++) {
        endpoint = &interface->endpoint[i].desc;
        if (!endpoint) {
            t500rs_err(t500rs, "Invalid endpoint at index %d\n", i);
            continue;
        }

        if (usb_endpoint_is_int_out(endpoint)) {
            t500rs->endpoint_out = endpoint->bEndpointAddress;
            t500rs_info(t500rs, "Found OUT endpoint: 0x%02x\n", t500rs->endpoint_out);
        } else if (usb_endpoint_is_int_in(endpoint)) {
            t500rs->endpoint_in = endpoint->bEndpointAddress;
            t500rs_info(t500rs, "Found IN endpoint: 0x%02x\n", t500rs->endpoint_in);
        }
    }

    if (!t500rs->endpoint_out || !t500rs->endpoint_in) {
        t500rs_err(t500rs, "Missing endpoints - OUT: 0x%02x, IN: 0x%02x\n",
                  t500rs->endpoint_out, t500rs->endpoint_in);
        return -ENODEV;
    }

    // First, clear any pending transfers
    usb_clear_halt(interface_to_usbdev(usbif), usb_sndintpipe(interface_to_usbdev(usbif), t500rs->endpoint_out));
    usb_clear_halt(interface_to_usbdev(usbif), usb_rcvintpipe(interface_to_usbdev(usbif), t500rs->endpoint_in));

    msleep(100);  // Wait for USB reset to settle

    // Enable interrupts using control transfer
    ret = usb_control_msg(interface_to_usbdev(usbif),
                         usb_sndctrlpipe(interface_to_usbdev(usbif), 0),
                         HID_REQ_SET_IDLE,
                         USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                         0, // Report type and ID
                         interface->desc.bInterfaceNumber, // Use actual interface number
                         NULL,
                         0,
                         USB_CTRL_SET_TIMEOUT);

    if (ret < 0) {
        t500rs_err(t500rs, "Failed to set idle state: %d\n", ret);
        return ret;
    }

    msleep(100);  // Wait for USB setup to stabilize

    // Now send the interrupt enable command
    u8 *send_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
    if (!send_buffer) {
        t500rs_err(t500rs, "Failed to allocate interrupt command buffer\n");
        return -ENOMEM;
    }

    send_buffer[0] = 0x08;
    send_buffer[1] = 0x01;
    ret = t500rs_send_buf(t500rs, send_buffer, t500rs->buffer_length);
    if (ret < 0) {
        t500rs_err(t500rs, "Failed to send interrupt enable command: %d\n", ret);
    } else {
        t500rs_info(t500rs, "Successfully enabled interrupts\n");
    }

    kfree(send_buffer);
    return ret;
}

int t500rs_send_open(struct t500rs_device_entry *t500rs)
{
    u8 *send_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
    int ret;

    if (!send_buffer) {
        ret = -ENOMEM;
        goto err;
    }

    send_buffer[0] = 0x08;
    send_buffer[1] = 0x00;
    ret = t500rs_send_buf(t500rs, send_buffer, t500rs->buffer_length);

err:
    kfree(send_buffer);
    return ret;
}

/* Device open/close functions */
int t500rs_open(struct input_dev *dev)
{
    struct t500rs_device_entry *t500rs = input_get_drvdata(dev);
    int ret;

    if (t500rs->open)
        ret = t500rs->open(dev);
    else
        ret = 0;

    if (ret)
        return ret;

    return t500rs_send_open(t500rs);
}

void t500rs_close(struct input_dev *dev)
{
    struct t500rs_device_entry *t500rs = input_get_drvdata(dev);

    if (t500rs->close)
        t500rs->close(dev);
}

static int t500rs_wheel_destroy(void *data)
{
    struct t500rs_device_entry *t500rs = data;

    if (!t500rs)
        return -ENODEV;

    // Cancel any pending USB transfers first
    if (t500rs->input_dev) {
        // Restore original callbacks
        if (t500rs->open && t500rs->close) {
            t500rs->input_dev->open = t500rs->open;
            t500rs->input_dev->close = t500rs->close;
        }
        input_set_drvdata(t500rs->input_dev, NULL);
    }

    // Clear any pending commands and free buffers
    if (t500rs->send_buffer) {
        memset(t500rs->send_buffer, 0, t500rs->buffer_length);
        kfree(t500rs->send_buffer);
        t500rs->send_buffer = NULL;
    }

    // Finally free the device structure
    kfree(t500rs);
    return 0;
}

int t500rs_set_range(void *data, uint16_t value)
{
    struct t500rs_device_entry *t500rs = data;
    u8 *send_buffer;
    uint16_t scaled_value;
    int ret;

    if (!t500rs || !t500rs->hdev) {
        printk(KERN_ERR "T500RS: Invalid data in set_range\n");
        return -EINVAL;
    }

    if (t500rs->state != T500RS_STATE_READY) {
        hid_warn(t500rs->hdev, "device not ready, ignoring set range\n");
        return -EAGAIN;
    }

    // Clamp range values
    if (value < 270) {
        hid_info(t500rs->hdev, "value %i too small, clamping to 270\n", value);
        value = 270;
    }

    if (value > 1080) {
        hid_info(t500rs->hdev, "value %i too large, clamping to 1080\n", value);
        value = 1080;
    }

    send_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
    if (!send_buffer) {
        hid_err(t500rs->hdev, "could not allocate send_buffer\n");
        return -ENOMEM;
    }

    // Scale range value for device protocol
    scaled_value = value * 0x3c;
    send_buffer[0] = 0x08;
    send_buffer[1] = 0x11;
    send_buffer[2] = scaled_value & 0xff;
    send_buffer[3] = scaled_value >> 8;

    ret = t500rs_send_buf(t500rs, send_buffer, t500rs->buffer_length);
    if (ret < 0) {
        hid_warn(t500rs->hdev, "failed setting range: %d\n", ret);
    } else {
        // Track successful range change
        t500rs->current_range = value;
        t500rs->last_command_time = jiffies;
        t500rs->command_retries = 0;
        hid_info(t500rs->hdev, "range set to %u degrees\n", value);
    }

    kfree(send_buffer);
    return ret;
}

static int t500rs_send_close(struct t500rs_device_entry *t500rs)
{
    int ret;
    t500rs->send_buffer[0] = 0x41;
    t500rs->send_buffer[1] = 0x00;
    t500rs->send_buffer[2] = 0x00;
    t500rs->send_buffer[3] = 0x01;
    if ((ret = t500rs_send_int(t500rs)))
        return ret;

    return 0;
}

#define T500RS_USB_TIMEOUT    1000  /* timeout in milliseconds */
#define T500RS_MAX_RETRIES    3     /* maximum number of retries for failed commands */

static int t500rs_send_init_command(struct usb_device *udev, u8 endpoint, const u8 *cmd, size_t size)
{
    int ret, retries = 0;
    int delay = 200;  // Start with longer initial delay
    bool success = false;

    // First clear any halted endpoints
    usb_clear_halt(udev, usb_sndintpipe(udev, endpoint));
    msleep(100);  // Wait longer for clear to take effect

    while (retries < T500RS_MAX_RETRIES && !success) {
        // Reset endpoint before each attempt
        usb_clear_halt(udev, usb_sndintpipe(udev, endpoint));
        msleep(100);  // Wait for clear to take effect

        // Try to send command
        ret = usb_interrupt_msg(udev,
                              usb_sndintpipe(udev, endpoint),
                              (u8 *)cmd, size,
                              NULL, T500RS_USB_TIMEOUT);
        
        if (ret >= 0) {
            success = true;
            msleep(200);  // Wait longer after successful command
            break;
        }
        
        // Handle specific errors
        if (ret == -EPROTO) {
            printk(KERN_INFO "T500RS: Protocol error during init, resetting device\n");
            usb_reset_device(udev);  // Reset device on protocol error
            msleep(1000);  // Extended wait after reset
            
            // Clear endpoints after reset
            usb_clear_halt(udev, usb_sndintpipe(udev, endpoint));
            msleep(200);
            
            // Verify device is still present
            struct usb_device_descriptor desc;
            ret = usb_get_descriptor(udev, USB_DT_DEVICE, 0, &desc, sizeof(desc));
            if (ret < 0) {
                if (ret == -ENODEV) {
                    printk(KERN_INFO "T500RS: Device disconnected during mode switch, expected\n");
                    return 0;  // Return success since this is expected during mode switch
                }
                printk(KERN_ERR "T500RS: Failed to verify device presence: %d\n", ret);
                return ret;
            }
            
            msleep(500);  // Additional wait before retry
        } else if (ret == -EPIPE) {
            printk(KERN_INFO "T500RS: Pipe error, clearing endpoint\n");
            usb_clear_halt(udev, usb_sndintpipe(udev, endpoint));
            msleep(300);  // Longer wait after pipe error
        } else if (ret != -ETIMEDOUT) {
            if (ret == -ENODEV) {
                printk(KERN_INFO "T500RS: Device disconnected during mode switch, expected\n");
                return 0;  // Return success since this is expected during mode switch
            }
            printk(KERN_ERR "T500RS: Critical error: %d\n", ret);
            return ret;  // Return immediately on other critical errors
        }
            
        retries++;
        
        // Exponential backoff with maximum delay of 2 seconds
        delay = min(delay * 2, 2000);
        msleep(delay);
        
        printk(KERN_INFO "T500RS: Retrying init command after %dms (attempt %d/%d)\n",
               delay, retries, T500RS_MAX_RETRIES);
    }
    
    if (!success) {
        printk(KERN_ERR "T500RS: Command failed after %d retries\n", T500RS_MAX_RETRIES);
        return -ETIMEDOUT;
    }

    return 0;
}

/* Command validation functions */
bool t500rs_validate_command(u8 cmd, u8 id, size_t len)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(t500rs_commands); i++) {
        if (t500rs_commands[i].cmd == cmd && t500rs_commands[i].id == id) {
            return len >= t500rs_commands[i].min_length && 
                   len <= t500rs_commands[i].max_length;
        }
    }
    return false;
}

bool t500rs_state_allows_command(struct t500rs_device_entry *t500rs, u8 cmd, u8 id)
{
    switch (t500rs->state) {
    case T500RS_STATE_INITIALIZING:
        // Allow all setup commands during initialization
        return true;
    case T500RS_STATE_READY:
        // Don't allow init/mode commands in ready state
        return !(cmd == 0x42 || (cmd == 0x41 && id == 0x03));
    case T500RS_STATE_SWITCHING_MODE:
        // Only allow status checks during mode switch
        return false;
    case T500RS_STATE_ERROR:
        // No commands allowed in error state
        return false;
    default:
        return false;
    }
}

const char *t500rs_error_to_string(int error)
{
    switch (error) {
    case T500RS_SUCCESS:         return "Success";
    case T500RS_ERROR_TIMEOUT:   return "Command timeout";
    case T500RS_ERROR_PROTO:     return "Protocol error";
    case T500RS_ERROR_STALL:     return "Endpoint stalled";
    case T500RS_ERROR_DISCONNECT: return "Device disconnected";
    case T500RS_ERROR_INVALID:   return "Invalid command";
    default:                     return "Unknown error";
    }
}

const char *t500rs_state_to_string(enum t500rs_device_state state)
{
    switch (state) {
    case T500RS_STATE_DISCONNECTED:  return "DISCONNECTED";
    case T500RS_STATE_INITIALIZING:  return "INITIALIZING";
    case T500RS_STATE_SWITCHING_MODE: return "SWITCHING_MODE";
    case T500RS_STATE_READY:         return "READY";
    case T500RS_STATE_ERROR:         return "ERROR";
    default:                         return "UNKNOWN";
    }
}

const char *t500rs_get_command_description(u8 cmd, u8 id)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(t500rs_commands); i++) {
        if (t500rs_commands[i].cmd == cmd && t500rs_commands[i].id == id)
            return t500rs_commands[i].description;
    }
    return "Unknown command";
}

void t500rs_set_state(struct t500rs_device_entry *t500rs, 
                     enum t500rs_device_state new_state)
{
    enum t500rs_device_state old_state = t500rs->state;
    t500rs->state = new_state;
    
    t500rs_info(t500rs, "State changed: %s -> %s\n",
                t500rs_state_to_string(old_state),
                t500rs_state_to_string(new_state));
}

static int t500rs_wheel_init(struct tmff2_device_entry *tmff2, int open_mode)
{
    struct t500rs_device_entry *t500rs;
    struct usb_interface *usbif;
    struct usb_host_interface *interface;
    struct usb_endpoint_descriptor *endpoint;
    struct list_head *report_list;
    struct usb_device *udev;
    int ret = 0, i;
    bool is_init_mode = (tmff2->hdev->product == TMT500RS_INIT_ID);

    hid_info(tmff2->hdev, "Starting wheel initialization (mode: %s)\n", 
             is_init_mode ? "INIT" : "PC");

    t500rs = kzalloc(sizeof(*t500rs), GFP_KERNEL);
    if (!t500rs) {
        hid_err(tmff2->hdev, "Failed to allocate memory for device structure\n");
        return -ENOMEM;
    }

    t500rs->hdev = tmff2->hdev;
    t500rs->input_dev = tmff2->input_dev;
    t500rs->buffer_length = TMT500RS_BUFFER_LENGTH;
    t500rs->state = T500RS_STATE_INITIALIZING;
    t500rs->last_command_time = jiffies;
    t500rs->command_retries = 0;
    t500rs->force_feedback_enabled = false;
    t500rs->current_gain = 0xFFFF;  // Full gain by default
    t500rs->current_range = 900;    // Default range

    // Get USB interface and endpoint information
    usbif = to_usb_interface(tmff2->hdev->dev.parent);
    if (!usbif) {
        t500rs_err(t500rs, "Failed to get USB interface\n");
        ret = -ENODEV;
        goto err_free_t500rs;
    }

    interface = usbif->cur_altsetting;
    if (!interface) {
        t500rs_err(t500rs, "Failed to get interface altsetting\n");
        ret = -ENODEV;
        goto err_free_t500rs;
    }

    udev = interface_to_usbdev(usbif);
    if (!udev) {
        t500rs_err(t500rs, "Failed to get USB device\n");
        ret = -ENODEV;
        goto err_free_t500rs;
    }

    t500rs_info(t500rs, "USB Device Info - Vendor:0x%04x Product:0x%04x Interface:%d\n",
                le16_to_cpu(udev->descriptor.idVendor),
                le16_to_cpu(udev->descriptor.idProduct),
                interface->desc.bInterfaceNumber);

    t500rs->send_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
    if (!t500rs->send_buffer) {
        t500rs_err(t500rs, "Failed to allocate send buffer (size: %d)\n",
                  t500rs->buffer_length);
        ret = -ENOMEM;
        goto err_free_t500rs;
    }

    // Get the output report
    report_list = &t500rs->hdev->report_enum[HID_OUTPUT_REPORT].report_list;
    if (list_empty(report_list)) {
        t500rs_err(t500rs, "No output report found in HID report list\n");
        ret = -ENODEV;
        goto err_free_send_buffer;
    }

    t500rs->report = list_entry(report_list->next, struct hid_report, list);
    if (!t500rs->report) {
        t500rs_err(t500rs, "Failed to get HID report from list\n");
        ret = -ENODEV;
        goto err_free_send_buffer;
    }

    // Get the force feedback field
    if (!t500rs->report->field[0]) {
        t500rs_err(t500rs, "No force feedback field found in HID report\n");
        ret = -ENODEV;
        goto err_free_send_buffer;
    }

    t500rs_dbg(t500rs, "Found HID report with %d fields\n", 
               t500rs->report->maxfield);
    t500rs->ff_field = t500rs->report->field[0];

    // Store original callbacks and set our own
    t500rs->open = t500rs->input_dev->open;
    t500rs->close = t500rs->input_dev->close;
    t500rs->input_dev->open = t500rs_open;
    t500rs->input_dev->close = t500rs_close;
    input_set_drvdata(t500rs->input_dev, t500rs);

    // Set device data
    tmff2->data = t500rs;

    // Copy supported effects
    memcpy(tmff2->supported_effects, t500rs_supported_effects, sizeof(t500rs_supported_effects));

    // Wait for device to stabilize before sending any commands
    msleep(1000);  // Initial delay

    // Only perform initialization sequence if we're in init mode
    if (is_init_mode) {
        t500rs_info(t500rs, "Starting USB configuration in INIT mode\n");
        t500rs_set_state(t500rs, T500RS_STATE_INITIALIZING);

        // Extended enumeration stabilization sequence
        t500rs_info(t500rs, "Starting USB enumeration stabilization\n");
        
        // First phase: Initial stabilization
        msleep(5000);  // Extended initial delay
        
        // Second phase: Port reset and power cycle
        struct usb_device *parent = udev->parent;
        if (parent) {
            t500rs_info(t500rs, "Resetting parent USB port\n");
            ret = usb_reset_device(parent);
            if (ret < 0 && ret != -ENODEV) {
                t500rs_warn(t500rs, "Parent port reset warning: %d\n", ret);
            }
            msleep(2000);  // Wait after port reset
        }
        
        // Third phase: Clear host controller state
        t500rs_info(t500rs, "Clearing host controller state\n");
        usb_reset_endpoint(udev, 0);  // Reset control endpoint
        msleep(1000);
        
        // Fourth phase: Clear all endpoints with retries
        int clear_retries = 0;
        bool endpoints_cleared = false;
        while (clear_retries < 5 && !endpoints_cleared) {
            bool out_clear = false, in_clear = false;
            
            // Clear OUT endpoint
            ret = usb_clear_halt(udev, usb_sndintpipe(udev, t500rs->endpoint_out));
            if (ret < 0) {
                if (ret != -ENODEV) {  // Ignore expected disconnects
                    t500rs_warn(t500rs, "Failed to clear OUT endpoint: %d\n", ret);
                }
            } else {
                out_clear = true;
            }
            msleep(500);
            
            // Clear IN endpoint
            ret = usb_clear_halt(udev, usb_rcvintpipe(udev, t500rs->endpoint_in));
            if (ret < 0) {
                if (ret != -ENODEV) {  // Ignore expected disconnects
                    t500rs_warn(t500rs, "Failed to clear IN endpoint: %d\n", ret);
                }
            } else {
                in_clear = true;
            }
            
            if (out_clear && in_clear) {
                endpoints_cleared = true;
                t500rs_info(t500rs, "Successfully cleared all endpoints\n");
                break;
            }
            
            clear_retries++;
            if (!endpoints_cleared) {
                t500rs_info(t500rs, "Retrying endpoint clear (attempt %d/5)\n", clear_retries);
                msleep(1000 * clear_retries);  // Progressive delay
            }
        }
        
        // Final phase: Extended stabilization
        msleep(3000);  // Final wait for complete stabilization

        // Reset USB device state with retries
        int reset_retries = 0;
        while (reset_retries < 3) {
            ret = usb_reset_device(udev);
            if (ret == -ENODEV || ret == -ENOENT) {
                // These errors are expected during mode switch
                t500rs_info(t500rs, "Device reset indicated mode switch, continuing...\n");
                break;
            } else if (ret < 0) {
                t500rs_warn(t500rs, "Device reset attempt %d warning: %d\n", 
                           reset_retries + 1, ret);
                msleep(1000);  // Wait between reset attempts
                reset_retries++;
            } else {
                break;
            }
        }
        msleep(2000);  // Extended wait after reset attempts

        // Reset and configure endpoints with improved error handling
        t500rs->endpoint_in = T500RS_ENDPOINT_IN;
        t500rs->endpoint_out = T500RS_ENDPOINT_OUT;

        // Function to clear endpoint with retries
        bool clear_endpoint_with_retries(struct usb_device *dev, unsigned int pipe, const char *name) {
            int retries = 0;
            while (retries < 3) {
                int ret = usb_clear_halt(dev, pipe);
                if (ret < 0) {
                    if (ret == -EPIPE) {
                        t500rs_warn(t500rs, "Pipe error clearing %s endpoint, resetting device\n", name);
                        usb_reset_device(dev);
                        msleep(1000);
                    } else if (ret == -EPROTO) {
                        t500rs_warn(t500rs, "Protocol error clearing %s endpoint, retrying\n", name);
                        msleep(500);
                    } else {
                        t500rs_warn(t500rs, "Failed to clear %s endpoint, attempt %d: %d\n",
                                   name, retries + 1, ret);
                        msleep(500);
                    }
                    retries++;
                    continue;
                }
                t500rs_info(t500rs, "Successfully cleared %s endpoint\n", name);
                return true;
            }
            t500rs_err(t500rs, "Failed to clear %s endpoint after retries\n", name);
            return false;
        }

        // Clear OUT endpoint
        if (!clear_endpoint_with_retries(udev, usb_sndintpipe(udev, t500rs->endpoint_out), "OUT")) {
            // Try alternate endpoint address
            t500rs_info(t500rs, "Trying alternate OUT endpoint\n");
            t500rs->endpoint_out = 0x02;  // Try alternate address
            if (!clear_endpoint_with_retries(udev, usb_sndintpipe(udev, t500rs->endpoint_out), "OUT")) {
                ret = -ENODEV;
                goto err_free_send_buffer;
            }
        }
        msleep(500);  // Wait between endpoint clears

        // Clear IN endpoint
        if (!clear_endpoint_with_retries(udev, usb_rcvintpipe(udev, t500rs->endpoint_in), "IN")) {
            // Try alternate endpoint address
            t500rs_info(t500rs, "Trying alternate IN endpoint\n");
            t500rs->endpoint_in = 0x81;  // Try alternate address
            if (!clear_endpoint_with_retries(udev, usb_rcvintpipe(udev, t500rs->endpoint_in), "IN")) {
                ret = -ENODEV;
                goto err_free_send_buffer;
            }
        }
        
        msleep(1000);  // Extended wait for clear to take effect

        // Set configuration using standard USB request with retries
        int config_retries = 0;
        bool config_success = false;
        while (config_retries < 3 && !config_success) {
            ret = usb_set_configuration(udev, 1);
            if (ret < 0) {
                if (ret == -EPROTO) {
                    t500rs_info(t500rs, "Protocol error setting configuration, attempt %d\n",
                               config_retries + 1);
                    msleep(1000);  // Longer delay between attempts
                    config_retries++;
                } else {
                    t500rs_err(t500rs, "Failed to set USB configuration: %d\n", ret);
                    goto err_free_send_buffer;
                }
            } else {
                config_success = true;
            }
        }

        if (!config_success) {
            t500rs_err(t500rs, "Failed to set configuration after retries\n");
            goto err_free_send_buffer;
        }

        msleep(1000);  // Extended wait after configuration

        // Find and configure endpoints
        for (i = 0; i < interface->desc.bNumEndpoints; i++) {
            endpoint = &interface->endpoint[i].desc;
            if (!endpoint) {
                t500rs_err(t500rs, "Invalid endpoint at index %d\n", i);
                continue;
            }

            if (usb_endpoint_is_int_out(endpoint)) {
                t500rs->endpoint_out = endpoint->bEndpointAddress;
                t500rs_info(t500rs, "Found OUT endpoint: 0x%02x\n", t500rs->endpoint_out);
            } else if (usb_endpoint_is_int_in(endpoint)) {
                t500rs->endpoint_in = endpoint->bEndpointAddress;
                t500rs_info(t500rs, "Found IN endpoint: 0x%02x\n", t500rs->endpoint_in);
            }
        }

        // Set interface using standard USB request
        ret = usb_set_interface(udev, interface->desc.bInterfaceNumber, 0);
        if (ret < 0) {
            if (ret == -EPROTO) {
                t500rs_info(t500rs, "Protocol error setting interface, retrying after delay\n");
                msleep(1000);
                ret = usb_set_interface(udev, interface->desc.bInterfaceNumber, 0);
            }
            if (ret < 0) {
                t500rs_err(t500rs, "Failed to set interface: %d\n", ret);
                goto err_free_send_buffer;
            }
        }
        msleep(500);  // Wait for interface to settle

        // Send initialization sequence with retries
        for (i = 0; i < ARRAY_SIZE(setup_arr); i++) {
            // Clear endpoint before each command
            usb_clear_halt(udev, usb_sndintpipe(udev, t500rs->endpoint_out));
            msleep(200);  // Wait for clear to take effect

            // Use timing array for consistent delays
            t500rs_info(t500rs, "Executing command: %s\n", setup_descriptions[i]);

            // Clear endpoints before critical commands
            if (i >= 2) {  // Mode-related commands need clean endpoints
                usb_clear_halt(udev, usb_sndintpipe(udev, t500rs->endpoint_out));
                usb_clear_halt(udev, usb_rcvintpipe(udev, t500rs->endpoint_in));
                msleep(500);  // Wait for clear to take effect
            }

            // Pre-command delay based on command type
            msleep(setup_delays[i]);

            // Additional preparation for mode switch commands
            if (i >= 3) {  // Pre-switch and later commands
                // Reset device state before critical commands with retries
                int cmd_reset_retries = 0;
                bool cmd_reset_success = false;
                while (cmd_reset_retries < 3 && !cmd_reset_success) {
                    // Clear endpoints before reset
                    usb_clear_halt(udev, usb_sndintpipe(udev, t500rs->endpoint_out));
                    usb_clear_halt(udev, usb_rcvintpipe(udev, t500rs->endpoint_in));
                    msleep(500);  // Wait for clear

                    t500rs_info(t500rs, "Resetting device before command %d (attempt %d/3)\n",
                               i, cmd_reset_retries + 1);
                    ret = usb_reset_device(udev);

                    if (ret == -ENODEV) {
                        t500rs_info(t500rs, "Device disconnected during reset (expected)\n");
                        cmd_reset_success = true;  // This is actually expected
                        break;
                    } else if (ret < 0) {
                        t500rs_warn(t500rs, "Device reset failed: %d, retrying after delay\n", ret);
                        msleep(1000 * (cmd_reset_retries + 1));  // Increasing delay
                        cmd_reset_retries++;
                    } else {
                        t500rs_info(t500rs, "Device reset successful\n");
                        cmd_reset_success = true;
                        msleep(2000);  // Extended wait after successful reset
                        break;
                    }
                }

                if (!cmd_reset_success && cmd_reset_retries == 3) {
                    t500rs_warn(t500rs, "Device reset failed after retries, continuing anyway\n");
                    msleep(2000);  // Extended wait before continuing
                }

                // Clear endpoints after reset attempt
                usb_clear_halt(udev, usb_sndintpipe(udev, t500rs->endpoint_out));
                usb_clear_halt(udev, usb_rcvintpipe(udev, t500rs->endpoint_in));
                msleep(500);  // Wait for clear
            }

            // Pre-command preparation
            t500rs_info(t500rs, "Preparing command: %s\n", setup_descriptions[i]);
            msleep(setup_timings[i].pre_delay);  // Pre-command delay

            // Execute command with retry limits from configuration
            int cmd_retries = 0;
            bool cmd_success = false;
            while (cmd_retries < setup_retry_limits[i].command_retries && !cmd_success) {
                if (cmd_retries > 0) {
                    t500rs_info(t500rs, "Retrying command %d (attempt %d/%d)\n",
                               i, cmd_retries + 1, setup_retry_limits[i].command_retries);
                    msleep(setup_timings[i].retry_delay);  // Delay between retries
                }

                ret = t500rs_send_init_command(udev, t500rs->endpoint_out,
                                             setup_arr[i], setup_arr_sizes[i]);
                t500rs_dbg(t500rs, "Command data: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                          setup_arr[i][0], setup_arr[i][1], setup_arr[i][2], setup_arr[i][3],
                          setup_arr[i][4], setup_arr[i][5], setup_arr[i][6], setup_arr[i][7]);

                if (ret >= 0) {
                    cmd_success = true;
                    t500rs_info(t500rs, "Command %d executed successfully\n", i);
                    msleep(setup_timings[i].post_delay);  // Post-command delay
                    break;
                } else if (ret == -ENODEV) {
                    // Device disconnect is expected for some commands
                    t500rs_info(t500rs, "Device disconnected during command %d (expected)\n", i);
                    cmd_success = true;
                    break;
                } else if (ret == -EPIPE || ret == -EPROTO) {
                    // Clear endpoint and retry on pipe/protocol errors
                    usb_clear_halt(udev, usb_sndintpipe(udev, t500rs->endpoint_out));
                    msleep(300);
                } else {
                    t500rs_warn(t500rs, "Command %d failed with error: %d\n", i, ret);
                }

                cmd_retries++;
            }

            if (!cmd_success) {
                t500rs_err(t500rs, "Command %d failed after %d retries\n",
                          i, setup_retry_limits[i].command_retries);
                goto err_free_send_buffer;
            }

            // Wait before verification
            msleep(setup_timings[i].verify_delay);
            if (ret < 0) {
                if (ret == -EPIPE) {
                    t500rs_info(t500rs, "Command %d stalled, clearing endpoint and retrying...\n", i);
                    usb_clear_halt(udev, usb_sndintpipe(udev, t500rs->endpoint_out));
                    msleep(300);  // Longer wait after stall
                    ret = t500rs_send_init_command(udev, t500rs->endpoint_out,
                                                 setup_arr[i], setup_arr_sizes[i]);
                }
                if (ret < 0) {
                    if (ret == -EPROTO) {
                        t500rs_info(t500rs, "Protocol error sending command %d, waiting and continuing...\n", i);
                        msleep(1000);  // Much longer wait after protocol error
                        continue;  // Skip to next command on protocol error
                    }
                    t500rs_err(t500rs, "Failed to send setup command %d after retries: %d\n",
                              i, ret);
                    goto err_free_send_buffer;
                }
            }
            
            // Handle status check for get_status command
            if (setup_arr[i][0] == 0x0a && setup_arr[i][1] == 0x04) {
                u8 status_buf[8] = {0};
                int actual_length;
                
                // Wait for status response
                msleep(200);
                
                ret = usb_interrupt_msg(udev,
                                      usb_rcvintpipe(udev, t500rs->endpoint_in),
                                      status_buf, sizeof(status_buf),
                                      &actual_length, T500RS_USB_TIMEOUT);
                
                if (ret < 0) {
                    if (ret == -EPIPE || ret == -EPROTO) {
                        t500rs_info(t500rs, "Status check failed, continuing anyway\n");
                    } else {
                        t500rs_warn(t500rs, "Could not read status: %d\n", ret);
                    }
                } else {
                    t500rs_info(t500rs, "Status response: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                               status_buf[0], status_buf[1], status_buf[2], status_buf[3],
                               status_buf[4], status_buf[5], status_buf[6], status_buf[7]);
                    
                    // Check status response
                    if (status_buf[0] != 0x0a || status_buf[1] != 0x04) {
                        t500rs_warn(t500rs, "Unexpected status response, retrying command\n");
                        msleep(1000);  // Extended delay before retry
                        
                        // Retry status check
                        ret = t500rs_send_init_command(udev, t500rs->endpoint_out,
                                                     setup_arr[i], setup_arr_sizes[i]);
                        if (ret < 0) {
                            t500rs_warn(t500rs, "Status check retry failed: %d\n", ret);
                            msleep(1000);  // Wait after failed retry
                        } else {
                            // Wait for retry response
                            msleep(500);
                            ret = usb_interrupt_msg(udev,
                                                  usb_rcvintpipe(udev, t500rs->endpoint_in),
                                                  status_buf, sizeof(status_buf),
                                                  &actual_length, T500RS_USB_TIMEOUT);
                            
                            if (ret < 0) {
                                t500rs_warn(t500rs, "Status check retry response failed: %d\n", ret);
                            } else {
                                t500rs_info(t500rs, "Retry status: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                                           status_buf[0], status_buf[1], status_buf[2], status_buf[3],
                                           status_buf[4], status_buf[5], status_buf[6], status_buf[7]);
                            }
                        }
                        
                        msleep(1000);  // Extended delay after retry attempt
                    } else {
                        t500rs_info(t500rs, "Status indicates ready for mode switch\n");
                        msleep(500);  // Normal delay after good status
                    }
                }
            } else {
                // Verify other commands were accepted with retries
                int verify_retries = 0;
                bool verify_success = false;
                while (verify_retries < 3 && !verify_success) {
                    u8 status = 0;
                    ret = usb_interrupt_msg(udev,
                                          usb_rcvintpipe(udev, t500rs->endpoint_in),
                                          &status, 1,
                                          NULL, T500RS_USB_TIMEOUT);
                    
                    if (ret < 0) {
                        if (ret == -EPIPE || ret == -EPROTO) {
                            t500rs_info(t500rs, "Command %d verification failed (attempt %d/3), retrying...\n",
                                      i, verify_retries + 1);
                            // Clear endpoint before retry
                            usb_clear_halt(udev, usb_rcvintpipe(udev, t500rs->endpoint_in));
                            msleep(500);  // Wait for clear
                            verify_retries++;
                            continue;
                        } else {
                            t500rs_warn(t500rs, "Could not verify command %d status: %d\n",
                                      i, ret);
                            break;
                        }
                    } else {
                        verify_success = true;
                        t500rs_info(t500rs, "Command %d verified successfully\n", i);
                    }
                }
                
                if (!verify_success) {
                    t500rs_warn(t500rs, "Command %d verification failed after retries\n", i);
                    msleep(1000);  // Extended delay after failed verification
                }
            }
            
            // Add longer delays after mode switch commands
            if (i >= 2) {  // setup_2 and later are mode-related commands
                msleep(1000);  // Extra delay after mode commands
            } else {
                msleep(500);  // Normal delay between other commands
            }
        }

        // Wait longer for device to stabilize before mode switch
        msleep(3000);  // Extended wait before mode switch

        // Prepare for mode switch
        t500rs_info(t500rs, "Preparing for mode switch sequence\n");
        t500rs_set_state(t500rs, T500RS_STATE_SWITCHING_MODE);

        // Reset device state before mode switch with retries
        bool reset_success = false;
        while (reset_retries < 3 && !reset_success) {
            // Clear endpoints before reset
            usb_clear_halt(udev, usb_sndintpipe(udev, t500rs->endpoint_out));
            usb_clear_halt(udev, usb_rcvintpipe(udev, t500rs->endpoint_in));
            msleep(500);  // Wait for clear to take effect

            t500rs_info(t500rs, "Resetting device before mode switch (attempt %d/3)\n", 
                       reset_retries + 1);
            ret = usb_reset_device(udev);
            
            if (ret == -ENODEV) {
                t500rs_info(t500rs, "Device disconnected during reset (expected)\n");
                reset_success = true;  // This is actually expected
                break;
            } else if (ret < 0) {
                t500rs_warn(t500rs, "Device reset failed: %d, retrying after delay\n", ret);
                msleep(1000 * (reset_retries + 1));  // Increasing delay between attempts
                reset_retries++;
            } else {
                t500rs_info(t500rs, "Device reset successful\n");
                reset_success = true;
                msleep(2000);  // Extended wait after successful reset
                break;
            }
        }

        if (!reset_success && reset_retries == 3) {
            t500rs_warn(t500rs, "Device reset failed after retries, continuing anyway\n");
            msleep(2000);  // Extended wait before continuing
        }

        // Clear endpoints one final time
        usb_clear_halt(udev, usb_sndintpipe(udev, t500rs->endpoint_out));
        usb_clear_halt(udev, usb_rcvintpipe(udev, t500rs->endpoint_in));
        msleep(500);

        t500rs_info(t500rs, "Device prepared for mode switch\n");

        // Send final mode command with retries
        int mode_retries = 0;
        bool mode_success = false;
        while (mode_retries < 3 && !mode_success) {
            // Clear endpoint before final command
            usb_clear_halt(udev, usb_sndintpipe(udev, t500rs->endpoint_out));
            msleep(500);  // Extended wait before final command

            t500rs_info(t500rs, "Sending final mode command (attempt %d/3)\n", mode_retries + 1);
            ret = t500rs_send_init_command(udev, t500rs->endpoint_out,
                                         setup_arr[6], setup_arr_sizes[6]);
            if (ret < 0) {
                if (ret == -ENODEV) {
                    t500rs_info(t500rs, "Device disconnected during mode switch (expected)\n");
                    mode_success = true;  // This is actually expected
                    break;
                } else if (ret == -EPROTO || ret == -EPIPE) {
                    if (mode_retries < 2) {  // Allow one more retry
                        t500rs_info(t500rs, "Final command error, retrying after delay\n");
                        msleep(1000);  // Longer delay between attempts
                        mode_retries++;
                        continue;
                    }
                    // On last retry, treat protocol errors as potential mode switch
                    t500rs_info(t500rs, "Protocol error on final retry, assuming mode switch\n");
                    mode_success = true;
                    break;
                } else {
                    t500rs_err(t500rs, "Final command failed: %d\n", ret);
                    goto err_free_send_buffer;
                }
            } else {
            // Command succeeded, implement advanced mode switch handling
            t500rs_info(t500rs, "Final command succeeded, initiating mode switch sequence\n");
            
            // First phase: Initial disconnect check with short intervals
            struct usb_device_descriptor desc;
            bool disconnect_detected = false;
            int quick_check_count = 0;
            while (quick_check_count < 3 && !disconnect_detected) {
                msleep(100);  // Quick checks initially
                ret = usb_get_descriptor(udev, USB_DT_DEVICE, 0, &desc, sizeof(desc));
                
                if (ret == -ENODEV) {
                    t500rs_info(t500rs, "Device disconnected quickly (expected)\n");
                    disconnect_detected = true;
                    mode_success = true;
                    break;
                }
                quick_check_count++;
            }
            
            // Second phase: Progressive checks with null commands
            if (!disconnect_detected) {
                int check_count = 0;
                while (check_count < 4 && !disconnect_detected) {
                    // Clear endpoints before each attempt
                    usb_clear_halt(udev, usb_sndintpipe(udev, t500rs->endpoint_out));
                    usb_clear_halt(udev, usb_rcvintpipe(udev, t500rs->endpoint_in));
                    msleep(300);  // Wait for clear
                    
                    // Send null command sequence
                    t500rs_info(t500rs, "Sending null command sequence %d/4\n", check_count + 1);
                    u8 null_cmd[8] = {0};
                    usb_interrupt_msg(udev, usb_sndintpipe(udev, t500rs->endpoint_out), 
                                    null_cmd, sizeof(null_cmd),
                                    NULL, T500RS_USB_TIMEOUT);
                    msleep(500);  // Wait after command
                    
                    // Check disconnect
                    ret = usb_get_descriptor(udev, USB_DT_DEVICE, 0, &desc, sizeof(desc));
                    if (ret == -ENODEV) {
                        t500rs_info(t500rs, "Device disconnected after null sequence %d\n",
                                   check_count + 1);
                        disconnect_detected = true;
                        mode_success = true;
                        break;
                    }
                    
                    check_count++;
                    msleep(500 * check_count);  // Progressive delay between attempts
                }
            }
            
            // Final phase: Reset sequence if still not disconnected
            if (!disconnect_detected) {
                t500rs_warn(t500rs, "Device persisting, attempting reset sequence\n");
                
                // Try up to 3 reset attempts
                int reset_count = 0;
                while (reset_count < 3 && !disconnect_detected) {
                    // Clear endpoints before reset
                    usb_clear_halt(udev, usb_sndintpipe(udev, t500rs->endpoint_out));
                    usb_clear_halt(udev, usb_rcvintpipe(udev, t500rs->endpoint_in));
                    msleep(500);
                    
                    t500rs_info(t500rs, "Executing reset sequence %d/3\n", reset_count + 1);
                    ret = usb_reset_device(udev);
                    msleep(1000);  // Wait after reset
                    
                    // Verify disconnect
                    ret = usb_get_descriptor(udev, USB_DT_DEVICE, 0, &desc, sizeof(desc));
                    if (ret == -ENODEV) {
                        t500rs_info(t500rs, "Device disconnected after reset sequence %d\n",
                                   reset_count + 1);
                        disconnect_detected = true;
                        mode_success = true;
                        break;
                    }
                    
                    reset_count++;
                    msleep(1000 * reset_count);  // Progressive delay between resets
                }
                
                if (!disconnect_detected) {
                    t500rs_warn(t500rs, "Device failed to disconnect after all attempts\n");
                    mode_success = false;
                }
            }
            }
        }

        // Give more time for mode switch to complete
        msleep(2000);  // Extended wait after mode switch

        t500rs_info(t500rs, "Mode switch sequence completed %s\n",
                   mode_success ? "successfully" : "with errors");
        return mode_success ? 0 : -ETIMEDOUT;
    }

    // For PC mode, configure endpoints and set initial parameters
    t500rs_info(t500rs, "Device in PC mode, configuring endpoints\n");
    t500rs_set_state(t500rs, T500RS_STATE_INITIALIZING);

    // Find and configure endpoints
    for (i = 0; i < interface->desc.bNumEndpoints; i++) {
        endpoint = &interface->endpoint[i].desc;
        if (!endpoint) {
            t500rs_err(t500rs, "Invalid endpoint at index %d\n", i);
            continue;
        }

        if (usb_endpoint_is_int_out(endpoint)) {
            t500rs->endpoint_out = endpoint->bEndpointAddress;
            t500rs_info(t500rs, "Found OUT endpoint: 0x%02x\n", t500rs->endpoint_out);
        } else if (usb_endpoint_is_int_in(endpoint)) {
            t500rs->endpoint_in = endpoint->bEndpointAddress;
            t500rs_info(t500rs, "Found IN endpoint: 0x%02x\n", t500rs->endpoint_in);
        }
    }

    // Set initial parameters with retries
    int retries = 0;
    while (retries < T500RS_MAX_RETRIES) {
        ret = t500rs_set_gain(tmff2->data, 0xFFFF);
        if (ret >= 0)
            break;
        retries++;
        msleep(50 * retries);
    }
    if (ret < 0) {
        printk(KERN_ERR "T500RS: Failed to set initial gain after retries: %d\n", ret);
        goto err_free_send_buffer;
    }

    msleep(200);

    retries = 0;
    while (retries < T500RS_MAX_RETRIES) {
        ret = t500rs_set_range(tmff2->data, 900);
        if (ret >= 0)
            break;
        retries++;
        msleep(50 * retries);
    }
    if (ret < 0) {
        printk(KERN_ERR "T500RS: Failed to set initial range after retries: %d\n", ret);
        goto err_free_send_buffer;
    }

    printk(KERN_INFO "T500RS: PC mode configuration completed successfully\n");
    t500rs->state = T500RS_STATE_READY;
    t500rs->force_feedback_enabled = true;
    return 0;

err_free_send_buffer:
    t500rs->state = T500RS_STATE_ERROR;
    kfree(t500rs->send_buffer);
err_free_t500rs:
    kfree(t500rs);
    return ret;
}

int t500rs_set_gain(void *data, uint16_t gain)
{
    struct t500rs_device_entry *t500rs = data;
    u8 *send_buffer;
    int ret;

    if (!t500rs || !t500rs->hdev) {
        printk(KERN_ERR "T500RS: Invalid data in set_gain\n");
        return -EINVAL;
    }

    if (t500rs->state != T500RS_STATE_READY) {
        hid_warn(t500rs->hdev, "device not ready, ignoring set gain\n");
        return -EAGAIN;
    }

    send_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
    if (!send_buffer) {
        hid_err(t500rs->hdev, "could not allocate send_buffer\n");
        return -ENOMEM;
    }

    send_buffer[0] = 0x41;
    send_buffer[1] = 0x04;
    send_buffer[2] = gain & 0xff;
    send_buffer[3] = (gain >> 8) & 0xff;

    ret = t500rs_send_buf(t500rs, send_buffer, t500rs->buffer_length);
    if (ret < 0) {
        hid_warn(t500rs->hdev, "failed setting gain: %d\n", ret);
    } else {
        t500rs->current_gain = gain;
        t500rs->last_command_time = jiffies;
        t500rs->command_retries = 0;
        hid_info(t500rs->hdev, "gain set to %u\n", gain);
    }

    kfree(send_buffer);
    return ret;
}

int t500rs_set_autocenter(void *data, uint16_t value)
{
    struct t500rs_device_entry *t500rs = data;
    u8 *send_buffer;
    int ret;

    if (!t500rs || !t500rs->hdev) {
        printk(KERN_ERR "T500RS: Invalid data in set_autocenter\n");
        return -EINVAL;
    }

    if (t500rs->state != T500RS_STATE_READY) {
        hid_warn(t500rs->hdev, "device not ready, ignoring set autocenter\n");
        return -EAGAIN;
    }

    send_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
    if (!send_buffer) {
        hid_err(t500rs->hdev, "could not allocate send_buffer\n");
        return -ENOMEM;
    }

    send_buffer[0] = 0x41;
    send_buffer[1] = 0x05;
    send_buffer[2] = value & 0xff;
    send_buffer[3] = (value >> 8) & 0xff;

    ret = t500rs_send_buf(t500rs, send_buffer, t500rs->buffer_length);
    if (ret < 0) {
        hid_warn(t500rs->hdev, "failed setting autocenter: %d\n", ret);
    } else {
        t500rs->last_command_time = jiffies;
        t500rs->command_retries = 0;
        hid_info(t500rs->hdev, "autocenter set to %u\n", value);
    }

    kfree(send_buffer);
    return ret;
}

/* Force feedback effect implementation */
static int t500rs_play_effect2(struct t500rs_device_entry *t500rs, u8 *data, size_t len)
{
    int ret;

    if (!t500rs || !data)
        return -EINVAL;

    if (t500rs->state != T500RS_STATE_READY) {
        t500rs_warn(t500rs, "device not ready, ignoring effect\n");
        return -EAGAIN;
    }

    ret = t500rs_send_buf(t500rs, data, len);
    if (ret < 0) {
        t500rs_err(t500rs, "failed to send effect: %d\n", ret);
        return ret;
    }

    return 0;
}

static int t500rs_upload_constant_force2(struct t500rs_device_entry *t500rs,
                                      struct tmff2_effect_state *effect)
{
    u8 *send_buffer;
    int ret;
    s16 level;
    u8 scaled_level;
    u16 attack_length, fade_length;
    u16 attack_level, fade_level;
    bool has_envelope = false;

    if (!t500rs || !effect) {
        t500rs_err(t500rs, "Invalid parameters in upload_constant_force\n");
        return -EINVAL;
    }

    if (t500rs->state != T500RS_STATE_READY) {
        t500rs_warn(t500rs, "Device not ready for constant force upload\n");
        return -EAGAIN;
    }

    send_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
    if (!send_buffer) {
        t500rs_err(t500rs, "Failed to allocate constant force buffer\n");
        return -ENOMEM;
    }

    // Check for envelope
    if (effect->effect.u.constant.envelope.attack_length || 
        effect->effect.u.constant.envelope.fade_length) {
        has_envelope = true;
        attack_length = effect->effect.u.constant.envelope.attack_length;
        fade_length = effect->effect.u.constant.envelope.fade_length;
        attack_level = effect->effect.u.constant.envelope.attack_level;
        fade_level = effect->effect.u.constant.envelope.fade_level;
        
        t500rs_dbg(t500rs, "Constant force envelope: attack=%u/%u fade=%u/%u\n",
                  attack_length, attack_level, fade_length, fade_level);
    }

    // Map level from -32767...32767 to 0...255 with improved precision
    level = effect->effect.u.constant.level;
    scaled_level = (u8)(((s32)(level + 32768) * 255) / 65536);
    
    t500rs_dbg(t500rs, "Constant force level: %d -> %u\n", level, scaled_level);

    // Upload effect parameters with retries
    int retries = 0;
    bool upload_success = false;
    while (retries < T500RS_MAX_RETRIES && !upload_success) {
        // Clear any previous data
        memset(send_buffer, 0, t500rs->buffer_length);

        // Upload command
        send_buffer[0] = 0x02;  // Upload command
        send_buffer[1] = 0x1c;  // Constant force subtype
        send_buffer[2] = effect->effect.id & 0xFF;  // Effect ID
        send_buffer[3] = has_envelope ? 0x01 : 0x00;  // Envelope flag
        
        ret = t500rs_play_effect2(t500rs, send_buffer, t500rs->buffer_length);
        if (ret >= 0) {
            upload_success = true;
            break;
        }

        if (ret == -EPIPE || ret == -EPROTO) {
            t500rs_warn(t500rs, "Upload retry %d after error: %d\n", 
                       retries + 1, ret);
            msleep(50 * (retries + 1));  // Progressive delay
            retries++;
        } else {
            t500rs_err(t500rs, "Critical error in upload: %d\n", ret);
            goto err_free;
        }
    }

    if (!upload_success) {
        t500rs_err(t500rs, "Failed to upload effect after retries\n");
        ret = -ETIMEDOUT;
        goto err_free;
    }

    // Wait before sending force level
    msleep(50);

    // Set force level with envelope if present
    memset(send_buffer, 0, t500rs->buffer_length);
    send_buffer[0] = 0x03;  // Modify command
    send_buffer[1] = 0x0e;  // Force level subtype
    send_buffer[2] = effect->effect.id & 0xFF;  // Effect ID
    send_buffer[3] = scaled_level;  // Force level

    if (has_envelope) {
        // Pack envelope parameters
        send_buffer[4] = attack_length & 0xFF;
        send_buffer[5] = (attack_length >> 8) & 0xFF;
        send_buffer[6] = attack_level & 0xFF;
        send_buffer[7] = (attack_level >> 8) & 0xFF;
        send_buffer[8] = fade_length & 0xFF;
        send_buffer[9] = (fade_length >> 8) & 0xFF;
        send_buffer[10] = fade_level & 0xFF;
        send_buffer[11] = (fade_level >> 8) & 0xFF;
    }

    ret = t500rs_play_effect2(t500rs, send_buffer, t500rs->buffer_length);
    if (ret < 0) {
        t500rs_err(t500rs, "Failed to set force level: %d\n", ret);
        goto err_free;
    }

    // Wait before setting duration
    msleep(50);

    // Set duration with improved timing handling
    memset(send_buffer, 0, t500rs->buffer_length);
    send_buffer[0] = 0x01;  // Duration command
    send_buffer[1] = 0x00;  // Reserved
    send_buffer[2] = effect->effect.id & 0xFF;  // Effect ID
    send_buffer[3] = 0x40;  // Duration flag

    // Handle infinite duration
    if (effect->effect.replay.length == 0) {
        send_buffer[4] = 0xFF;
        send_buffer[5] = 0xFF;
        t500rs_dbg(t500rs, "Setting infinite duration\n");
    } else {
        // Duration in milliseconds (little endian)
        send_buffer[4] = effect->effect.replay.length & 0xFF;
        send_buffer[5] = (effect->effect.replay.length >> 8) & 0xFF;
        t500rs_dbg(t500rs, "Setting duration: %u ms\n", effect->effect.replay.length);
    }

    send_buffer[9] = 0x0e;   // Effect type
    send_buffer[11] = 0x1c;  // Effect subtype

    ret = t500rs_play_effect2(t500rs, send_buffer, t500rs->buffer_length);
    if (ret < 0) {
        t500rs_err(t500rs, "Failed to set duration: %d\n", ret);
    } else {
        t500rs_dbg(t500rs, "Successfully uploaded constant force effect\n");
    }

err_free:
    kfree(send_buffer);
    return ret;
}

static int t500rs_play_constant_force2(struct t500rs_device_entry *t500rs,
                                    struct tmff2_effect_state *effect, int value)
{
    u8 *send_buffer;
    int ret;
    bool play_success = false;
    int retries = 0;

    if (!t500rs || !effect) {
        t500rs_err(t500rs, "Invalid parameters in play_constant_force\n");
        return -EINVAL;
    }

    if (t500rs->state != T500RS_STATE_READY) {
        t500rs_warn(t500rs, "Device not ready for constant force playback\n");
        return -EAGAIN;
    }

    if (effect->effect.id >= TMT500RS_MAX_EFFECTS) {
        t500rs_err(t500rs, "Invalid effect ID: %d\n", effect->effect.id);
        return -EINVAL;
    }

    send_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
    if (!send_buffer) {
        t500rs_err(t500rs, "Failed to allocate play buffer\n");
        return -ENOMEM;
    }

    t500rs_dbg(t500rs, "%s constant force effect %d\n",
               value ? "Starting" : "Stopping", effect->effect.id);

    // Try to play/stop effect with retries
    while (retries < T500RS_MAX_RETRIES && !play_success) {
        memset(send_buffer, 0, t500rs->buffer_length);

        if (value) {
            // Start effect with proper ID
            send_buffer[0] = 0x41;  // Play command
            send_buffer[1] = 0x00;  // Reserved
            send_buffer[2] = 0x41;  // Play flag
            send_buffer[3] = 0x01;  // Effect type
            send_buffer[4] = effect->effect.id & 0xFF;  // Effect ID
            send_buffer[5] = 0x01;  // Start flag
        } else {
            // Stop effect with proper ID
            send_buffer[0] = 0x41;  // Play command
            send_buffer[1] = 0x00;  // Reserved
            send_buffer[2] = 0x00;  // Stop flag
            send_buffer[3] = 0x01;  // Effect type
            send_buffer[4] = effect->effect.id & 0xFF;  // Effect ID
            send_buffer[5] = 0x00;  // Stop flag
        }

        // Ensure minimum delay between commands
        if (time_before(jiffies, t500rs->last_command_time + msecs_to_jiffies(50))) {
            msleep(jiffies_to_msecs(t500rs->last_command_time + 
                   msecs_to_jiffies(50) - jiffies));
        }

        ret = t500rs_play_effect2(t500rs, send_buffer, t500rs->buffer_length);
        if (ret >= 0) {
            play_success = true;
            t500rs_dbg(t500rs, "Successfully %s effect %d\n",
                      value ? "started" : "stopped", effect->effect.id);

            // Update last command time
            t500rs->last_command_time = jiffies;
            break;
        }

        if (ret == -EPIPE || ret == -EPROTO) {
            t500rs_warn(t500rs, "Play retry %d after error: %d\n",
                       retries + 1, ret);
            msleep(50 * (retries + 1));  // Progressive delay
            retries++;
        } else {
            t500rs_err(t500rs, "Critical error in play: %d\n", ret);
            goto err_free;
        }
    }

    if (!play_success) {
        t500rs_err(t500rs, "Failed to play effect after retries\n");
        ret = -ETIMEDOUT;
    }

err_free:
    kfree(send_buffer);
    return ret;
}

// Effect handlers
static int t500rs_upload_effect(void *data, struct tmff2_effect_state *effect)
{
    struct t500rs_device_entry *t500rs = data;

    if (!t500rs || !effect)
        return -EINVAL;

    switch (effect->effect.type) {
    case FF_CONSTANT:
        return t500rs_upload_constant_force(t500rs, effect);
    default:
        t500rs_warn(t500rs, "Unsupported effect type: %d\n", effect->effect.type);
        return -EINVAL;
    }
}

static int t500rs_play_effect(void *data, struct tmff2_effect_state *effect)
{
    struct t500rs_device_entry *t500rs = data;

    if (!t500rs || !effect)
        return -EINVAL;

    switch (effect->effect.type) {
    case FF_CONSTANT:
        return t500rs_play_constant_force(t500rs, effect, effect->count);
    default:
        t500rs_warn(t500rs, "Unsupported effect type: %d\n", effect->effect.type);
        return -EINVAL;
    }
}

int t500rs_populate_api(struct tmff2_device_entry *tmff2)
{
    if (!tmff2)
        return -EINVAL;

	tmff2->play_effect = t300rs_play_effect;
	tmff2->upload_effect = t300rs_upload_effect;
	tmff2->update_effect = t300rs_update_effect;
	tmff2->stop_effect = t300rs_stop_effect;

    // Set up the API functions
    tmff2->set_gain = t500rs_set_gain;
    tmff2->set_autocenter = t500rs_set_autocenter;
    tmff2->set_range = t500rs_set_range;
    tmff2->wheel_init = t500rs_wheel_init;
    tmff2->wheel_destroy = t500rs_wheel_destroy;
    tmff2->upload_effect = t500rs_upload_effect;

    return 0;
}
