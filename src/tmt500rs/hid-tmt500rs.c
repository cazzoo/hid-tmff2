// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/hid.h>
#include <linux/fixp-arith.h>
#include "../hid-tmff2.h"
#include "hid-tmt500rs.h"

/* HID descriptor based on USB captures */
static u8 t500rs_rdesc_fixed[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x04,        // Usage (Joystick)
    0xa1, 0x01,        // Collection (Application)
    0x09, 0x01,        // Usage (Pointer)
    0xa1, 0x00,        // Collection (Physical)
    0x85, 0x07,        // Report ID (7)
    0x09, 0x30,        // Usage (X)
    0x15, 0x00,        // Logical Minimum (0)
    0x27, 0xff, 0xff, 0x00, 0x00,  // Logical Maximum (65535)
    0x35, 0x00,        // Physical Minimum (0)
    0x47, 0xff, 0xff, 0x00, 0x00,  // Physical Maximum (65535)
    0x75, 0x10,        // Report Size (16)
    0x95, 0x01,        // Report Count (1)
    0x81, 0x02,        // Input (Data,Var,Abs)
    0x09, 0x31,        // Usage (Y)
    0x26, 0xff, 0x03,  // Logical Maximum (1023)
    0x46, 0xff, 0x03,  // Physical Maximum (1023)
    0x81, 0x02,        // Input (Data,Var,Abs)
    0x09, 0x35,        // Usage (Rz)
    0x81, 0x02,        // Input (Data,Var,Abs)
    0x09, 0x36,        // Usage (Slider)
    0x81, 0x02,        // Input (Data,Var,Abs)
    0x81, 0x03,        // Input (Cnst,Var,Abs)
    0x05, 0x09,        // Usage Page (Button)
    0x19, 0x01,        // Usage Minimum (1)
    0x29, 0x0d,        // Usage Maximum (13)
    0x25, 0x01,        // Logical Maximum (1)
    0x45, 0x01,        // Physical Maximum (1)
    0x75, 0x01,        // Report Size (1)
    0x95, 0x0d,        // Report Count (13)
    0x81, 0x02,        // Input (Data,Var,Abs)
    0x75, 0x0b,        // Report Size (11)
    0x95, 0x01,        // Report Count (1)
    0x81, 0x03,        // Input (Cnst,Var,Abs)
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x39,        // Usage (Hat switch)
    0x25, 0x07,        // Logical Maximum (7)
    0x46, 0x3b, 0x01,  // Physical Maximum (315)
    0x55, 0x00,        // Unit Exponent (0)
    0x65, 0x14,        // Unit (Eng Rot:Angular Pos)
    0x75, 0x04,        // Report Size (4)
    0x81, 0x42,        // Input (Data,Var,Abs,Null)
    0x65, 0x00,        // Unit (None)
    0x81, 0x03,        // Input (Cnst,Var,Abs)
    0x85, 0x0a,        // Report ID (10)
    0x06, 0x00, 0xff,  // Usage Page (Vendor Defined)
    0x09, 0x0a,        // Usage (10)
    0x75, 0x08,        // Report Size (8)
    0x95, 0x0e,        // Report Count (14)
    0x26, 0xff, 0x00,  // Logical Maximum (255)
    0x46, 0xff, 0x00,  // Physical Maximum (255)
    0x91, 0x02,        // Output (Data,Var,Abs)
    0x85, 0x02,        // Report ID (2)
    0x09, 0x02,        // Usage (2)
    0x81, 0x02,        // Input (Data,Var,Abs)
    0x09, 0x14,        // Usage (20)
    0x85, 0x14,        // Report ID (20)
    0x81, 0x02,        // Input (Data,Var,Abs)
    0xc0,              // End Collection
    0xc0               // End Collection
};

/* Initial setup packets based on USB captures */
/* Setup packets based on USB captures with improved timing and reliability */
static const u8 setup_0[8] = { 0x42, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // Init command
static const u8 setup_1[8] = { 0x0a, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // Get status
static const u8 setup_2[8] = { 0x41, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // Mode command
static const u8 setup_3[8] = { 0x41, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 }; // Pre-switch command
static const u8 setup_4[8] = { 0x41, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 }; // Switch command
static const u8 setup_5[8] = { 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // Reset command
static const u8 setup_6[8] = { 0x41, 0x03, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00 }; // Final mode command with mode flag
static const u8 *const setup_arr[] = { setup_0, setup_1, setup_2, setup_3, setup_4, setup_5, setup_6 };
static const unsigned int setup_arr_sizes[] = { 8, 8, 8, 8, 8, 8, 8 };

/* Command descriptions for logging with timing info */
static const char *const setup_descriptions[] = {
    "Initialize device (1000ms)",           // setup_0
    "Check device status (800ms)",          // setup_1
    "Set initial mode (1500ms)",            // setup_2
    "Prepare for mode switch (2000ms)",     // setup_3
    "Execute mode switch (2500ms)",         // setup_4
    "Reset device state (3000ms)",          // setup_5
    "Set final mode with flag (2000ms)"     // setup_6
};

/* Command timing delays in milliseconds */
static const int setup_delays[] = {
    1000,  // Init command
    800,   // Get status
    1500,  // Mode command
    2000,  // Pre-switch command
    2500,  // Switch command
    3000,  // Reset command
    2000   // Final mode command
};

/* Command timing requirements in milliseconds */
static const struct {
    int pre_delay;    // Delay before command
    int post_delay;   // Delay after command
    int retry_delay;  // Additional delay between retries
    int verify_delay; // Delay before verification
} setup_timings[] = {
    { 1000, 500,  200, 100 },  // Init command
    { 800,  300,  200, 200 },  // Get status
    { 1500, 800,  500, 300 },  // Mode command
    { 2000, 1000, 800, 500 },  // Pre-switch command
    { 2500, 1500, 1000, 800 }, // Switch command
    { 3000, 2000, 1500, 1000 },// Reset command
    { 2000, 1500, 1000, 800 }  // Final mode command
};

/* Command retry limits */
static const struct {
    int command_retries;  // Max retries for command send
    int verify_retries;   // Max retries for verification
    int reset_retries;    // Max retries for device reset
} setup_retry_limits[] = {
    { 2, 2, 1 },  // Init command
    { 3, 3, 2 },  // Get status
    { 3, 3, 2 },  // Mode command
    { 4, 4, 3 },  // Pre-switch command
    { 4, 4, 3 },  // Switch command
    { 5, 5, 3 },  // Reset command
    { 4, 4, 3 }   // Final mode command
};

/* USB endpoint addresses from descriptor */
#define T500RS_ENDPOINT_IN   0x82
#define T500RS_ENDPOINT_OUT  0x01

static const unsigned long t500rs_params =
    PARAM_SPRING_LEVEL
    | PARAM_DAMPER_LEVEL
    | PARAM_FRICTION_LEVEL
    | PARAM_RANGE
    | PARAM_GAIN;

static const signed short t500rs_effects[] = {
    FF_CONSTANT,
    FF_SPRING,
    FF_DAMPER,
    FF_FRICTION,
    FF_PERIODIC,
    FF_GAIN,
    -1
};

/* Supported effects */
const signed short t500rs_supported_effects[FF_CNT] = {
    [FF_CONSTANT] = 0x4000,
    [FF_SPRING] = 0x4001,
    [FF_DAMPER] = 0x4002,
    [FF_FRICTION] = 0x4003,
    [FF_SINE] = 0x4004,
    [FF_SAW_UP] = 0x4005,
    [FF_SAW_DOWN] = 0x4006,
    [FF_CUSTOM] = 0x4007,
    [FF_GAIN] = 0x4008,
    [FF_AUTOCENTER] = 0x4009,
};

/* Supported parameters */
const unsigned long t500rs_supported_parameters[FF_CNT] = {
    [FF_CONSTANT] = PARAM_GAIN,
    [FF_SPRING] = PARAM_SPRING_LEVEL | PARAM_GAIN,
    [FF_DAMPER] = PARAM_DAMPER_LEVEL | PARAM_GAIN,
    [FF_FRICTION] = PARAM_FRICTION_LEVEL | PARAM_GAIN,
    [FF_SINE] = PARAM_GAIN,
    [FF_SAW_UP] = PARAM_GAIN,
    [FF_SAW_DOWN] = PARAM_GAIN,
    [FF_CUSTOM] = PARAM_GAIN,
    [FF_GAIN] = 0,
    [FF_AUTOCENTER] = 0,
};

#define TMT500RS_MAX_EFFECTS 16

/* Helper function to send data using the report */
static int t500rs_send_int(struct t500rs_device_entry *t500rs)
{
    int ret;

    if (!t500rs || !t500rs->send_buffer) {
        t500rs_err(t500rs, "Invalid data in send_int\n");
        return -EINVAL;
    }

    if (t500rs->state == T500RS_STATE_ERROR) {
        t500rs_err(t500rs, "Cannot send command in error state\n");
        return -ENODEV;
    }

    t500rs_dbg(t500rs, "Sending command: %s (0x%02x, 0x%02x)\n",
               t500rs_get_command_description(t500rs->send_buffer[0], t500rs->send_buffer[1]),
               t500rs->send_buffer[0], t500rs->send_buffer[1]);

    // Track command attempt
    t500rs->last_command_time = jiffies;

    ret = t500rs_send_buf(t500rs, t500rs->send_buffer, t500rs->buffer_length);
    if (ret < 0) {
        if (ret == -ETIMEDOUT) {
            t500rs->command_retries++;
            if (t500rs->command_retries >= T500RS_MAX_RETRIES * 2) {
                t500rs_err(t500rs, "Too many timeouts (%d), marking device as error\n",
                          t500rs->command_retries);
                t500rs_set_state(t500rs, T500RS_STATE_ERROR);
            } else {
                t500rs_dbg(t500rs, "Command timeout (attempt %d/%d)\n",
                          t500rs->command_retries, T500RS_MAX_RETRIES * 2);
            }
        } else {
            t500rs_err(t500rs, "Command failed with error: %d\n", ret);
            t500rs_set_state(t500rs, T500RS_STATE_ERROR);
        }
    } else {
        t500rs_dbg(t500rs, "Command sent successfully\n");
        t500rs->command_retries = 0;  // Reset retry counter on success
    }

    memset(t500rs->send_buffer, 0, t500rs->buffer_length);
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

int t500rs_populate_api(struct tmff2_device_entry *tmff2)
{
    if (!tmff2)
        return -EINVAL;

    // Set up the API functions
    tmff2->set_gain = t500rs_set_gain;
    tmff2->set_autocenter = t500rs_set_autocenter;
    tmff2->set_range = t500rs_set_range;
    tmff2->wheel_init = t500rs_wheel_init;
    tmff2->wheel_destroy = t500rs_wheel_destroy;

    return 0;
}
