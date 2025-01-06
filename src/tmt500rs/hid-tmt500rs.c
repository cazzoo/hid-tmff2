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
static const u8 setup_0[8] = { 0x09, 0x02, 0x29, 0x00, 0x01, 0x01, 0x00, 0x00 }; // Set config
static const u8 setup_1[8] = { 0x09, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // Set interface
static const u8 setup_2[8] = { 0x42, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // Init command
static const u8 setup_3[8] = { 0x41, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // Mode command
static const u8 *const setup_arr[] = { setup_0, setup_1, setup_2, setup_3 };
static const unsigned int setup_arr_sizes[] = { 8, 8, 8, 8 };

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

    while (retries < T500RS_MAX_RETRIES) {
        ret = usb_interrupt_msg(udev,
                              usb_sndintpipe(udev, endpoint),
                              (u8 *)cmd, size,
                              NULL, T500RS_USB_TIMEOUT);
        if (ret >= 0)
            return 0;
        
        if (ret != -ETIMEDOUT && ret != -EPROTO)
            return ret;  // Return on critical errors
            
        retries++;
        msleep(50 * retries);  // Increasing delay between retries
    }
    
    return -ETIMEDOUT;
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
    msleep(500);

    // Only perform initialization sequence if we're in init mode
    if (is_init_mode) {
        t500rs_info(t500rs, "Starting USB configuration in INIT mode\n");
        t500rs_set_state(t500rs, T500RS_STATE_INITIALIZING);

        // Reset USB device first
        ret = usb_reset_device(udev);
        if (ret < 0) {
            t500rs_warn(t500rs, "Device reset failed: %d (continuing anyway)\n", ret);
            // Continue anyway as some devices don't support reset
        }

        msleep(200);

        // First, ensure device is in config 1
        ret = usb_control_msg(udev,
                             usb_sndctrlpipe(udev, 0),
                             USB_REQ_SET_CONFIGURATION,
                             USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                             1, // configuration value
                             0, // index
                             NULL,
                             0,
                             USB_CTRL_SET_TIMEOUT);
        if (ret < 0) {
            t500rs_err(t500rs, "Failed to set USB configuration: %d\n", ret);
            goto err_free_send_buffer;
        }

        msleep(300);

        t500rs_info(t500rs, "Configuring USB endpoints (interface %d has %d endpoints)\n", 
                   interface->desc.bInterfaceNumber,
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
                t500rs_info(t500rs, "Found OUT endpoint: 0x%02x (wMaxPacketSize: %d)\n",
                           t500rs->endpoint_out, endpoint->wMaxPacketSize);
            } else if (usb_endpoint_is_int_in(endpoint)) {
                t500rs->endpoint_in = endpoint->bEndpointAddress;
                t500rs_info(t500rs, "Found IN endpoint: 0x%02x (wMaxPacketSize: %d)\n",
                           t500rs->endpoint_in, endpoint->wMaxPacketSize);
            }
        }

        // Clear any halted endpoints before starting initialization
        usb_clear_halt(udev, usb_sndintpipe(udev, t500rs->endpoint_out));
        usb_clear_halt(udev, usb_rcvintpipe(udev, t500rs->endpoint_in));
        msleep(100);  // Wait for clear to take effect

        // Send initialization sequence with retries
        for (i = 0; i < ARRAY_SIZE(setup_arr); i++) {
            // Special handling for SET_INTERFACE command
            if (i == 1) {  // setup_1 is SET_INTERFACE
                t500rs_info(t500rs, "Setting interface %d\n", interface->desc.bInterfaceNumber);
                ret = usb_set_interface(udev, interface->desc.bInterfaceNumber, 0);
                if (ret < 0) {
                    t500rs_err(t500rs, "Failed to set interface: %d\n", ret);
                    if (ret == -EPIPE) {
                        t500rs_info(t500rs, "Endpoint stalled, clearing and retrying...\n");
                        usb_clear_halt(udev, usb_sndintpipe(udev, t500rs->endpoint_out));
                        msleep(100);
                        ret = usb_set_interface(udev, interface->desc.bInterfaceNumber, 0);
                        if (ret < 0) {
                            t500rs_err(t500rs, "Second attempt to set interface failed: %d\n", ret);
                            goto err_free_send_buffer;
                        }
                    } else {
                        goto err_free_send_buffer;
                    }
                }
                msleep(200);  // Extra delay after interface setup
                continue;
            }

            ret = t500rs_send_init_command(udev, t500rs->endpoint_out,
                                         setup_arr[i], setup_arr_sizes[i]);
            if (ret < 0) {
                if (ret == -EPIPE) {
                    t500rs_info(t500rs, "Command %d stalled, clearing endpoint and retrying...\n", i);
                    usb_clear_halt(udev, usb_sndintpipe(udev, t500rs->endpoint_out));
                    msleep(100);
                    ret = t500rs_send_init_command(udev, t500rs->endpoint_out,
                                                 setup_arr[i], setup_arr_sizes[i]);
                }
                if (ret < 0) {
                    t500rs_err(t500rs, "Failed to send setup command %d (%s) after retries: %d\n",
                              i, t500rs_get_command_description(setup_arr[i][0], setup_arr[i][1]), ret);
                    goto err_free_send_buffer;
                }
            }
            
            // Verify command was accepted
            u8 status = 0;
            ret = usb_interrupt_msg(udev,
                                  usb_rcvintpipe(udev, t500rs->endpoint_in),
                                  &status, 1,
                                  NULL, T500RS_USB_TIMEOUT);
            if (ret < 0) {
                if (ret == -EPIPE) {
                    usb_clear_halt(udev, usb_rcvintpipe(udev, t500rs->endpoint_in));
                    msleep(100);
                    ret = usb_interrupt_msg(udev,
                                          usb_rcvintpipe(udev, t500rs->endpoint_in),
                                          &status, 1,
                                          NULL, T500RS_USB_TIMEOUT);
                }
                if (ret < 0) {
                    t500rs_warn(t500rs, "Could not verify command %d (%s) status: %d\n",
                               i, t500rs_get_command_description(setup_arr[i][0], setup_arr[i][1]), ret);
                    // Continue anyway as some commands don't respond
                }
            }
            
            msleep(200);  // Longer wait between commands
        }

        // Wait for device to stabilize and switch modes
        msleep(500);

        // The device will disconnect and reconnect in PC mode
        t500rs_info(t500rs, "Initialization sequence completed, waiting for mode switch\n");
        t500rs_set_state(t500rs, T500RS_STATE_SWITCHING_MODE);
        return 0;
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
            t500rs_info(t500rs, "Found OUT endpoint: 0x%02x (wMaxPacketSize: %d)\n",
                       t500rs->endpoint_out, endpoint->wMaxPacketSize);
        } else if (usb_endpoint_is_int_in(endpoint)) {
            t500rs->endpoint_in = endpoint->bEndpointAddress;
            t500rs_info(t500rs, "Found IN endpoint: 0x%02x (wMaxPacketSize: %d)\n",
                       t500rs->endpoint_in, endpoint->wMaxPacketSize);
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

static __u8 *t500rs_wheel_fixup(struct hid_device *hdev, __u8 *rdesc,
                unsigned int *rsize)
{
    *rsize = sizeof(t500rs_rdesc_fixed);
    return t500rs_rdesc_fixed;
}

/* Force feedback effect functions */
static int t500rs_play_effect(void *data, struct tmff2_effect_state *state)
{
    struct t500rs_device_entry *t500rs = data;
    u8 *send_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
    int ret;

    if (!send_buffer) {
        ret = -ENOMEM;
        goto err;
    }

    if (!t500rs->force_feedback_enabled) {
        hid_warn(t500rs->hdev, "force feedback not enabled, ignoring play effect\n");
        ret = -ENODEV;
        goto err;
    }

    if (t500rs->state != T500RS_STATE_READY) {
        hid_warn(t500rs->hdev, "device not ready, ignoring play effect\n");
        ret = -EAGAIN;
        goto err;
    }

    send_buffer[0] = 0x08;
    send_buffer[1] = 0x03;
    send_buffer[2] = state->effect.id;
    send_buffer[3] = 0x01; // Start effect

    ret = t500rs_send_buf(t500rs, send_buffer, t500rs->buffer_length);
    if (ret == 0) {
        // Track successful effect play
        t500rs->last_command_time = jiffies;
        t500rs->command_retries = 0;
    }

err:
    kfree(send_buffer);
    return ret;
}

static int t500rs_stop_effect(void *data, struct tmff2_effect_state *state)
{
    struct t500rs_device_entry *t500rs = data;
    u8 *send_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
    int ret;

    if (!send_buffer) {
        ret = -ENOMEM;
        goto err;
    }

    if (!t500rs->force_feedback_enabled) {
        hid_warn(t500rs->hdev, "force feedback not enabled, ignoring stop effect\n");
        ret = -ENODEV;
        goto err;
    }

    if (t500rs->state != T500RS_STATE_READY) {
        hid_warn(t500rs->hdev, "device not ready, ignoring stop effect\n");
        ret = -EAGAIN;
        goto err;
    }

    send_buffer[0] = 0x08;
    send_buffer[1] = 0x03;
    send_buffer[2] = state->effect.id;
    send_buffer[3] = 0x00; // Stop effect

    ret = t500rs_send_buf(t500rs, send_buffer, t500rs->buffer_length);
    if (ret == 0) {
        // Track successful effect stop
        t500rs->last_command_time = jiffies;
        t500rs->command_retries = 0;
    }

err:
    kfree(send_buffer);
    return ret;
}

static int t500rs_upload_effect(void *data, struct tmff2_effect_state *state)
{
    struct t500rs_device_entry *t500rs = data;
    u8 *send_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
    int ret;
    uint16_t magnitude;

    if (!send_buffer) {
        ret = -ENOMEM;
        goto err;
    }

    if (!t500rs->force_feedback_enabled) {
        hid_warn(t500rs->hdev, "force feedback not enabled, ignoring upload effect\n");
        ret = -ENODEV;
        goto err;
    }

    if (t500rs->state != T500RS_STATE_READY) {
        hid_warn(t500rs->hdev, "device not ready, ignoring upload effect\n");
        ret = -EAGAIN;
        goto err;
    }

    send_buffer[0] = 0x08;
    send_buffer[1] = 0x04;
    send_buffer[2] = state->effect.id;

    switch (state->effect.type) {
    case FF_CONSTANT:
        send_buffer[3] = 0x01;  // Constant force
        magnitude = abs(state->effect.u.constant.level);
        // Apply envelope if specified
        if (state->effect.u.constant.envelope.attack_length || 
            state->effect.u.constant.envelope.fade_length) {
            magnitude = magnitude * 
                (0xFFFF - state->effect.u.constant.envelope.attack_level) / 0xFFFF;
        }
        send_buffer[4] = magnitude & 0xff;
        send_buffer[5] = (magnitude >> 8) & 0xff;
        // Direction: clockwise for positive, counter-clockwise for negative
        send_buffer[6] = state->effect.u.constant.level < 0 ? 0x01 : 0x00;
        break;

    case FF_SPRING:
        send_buffer[3] = 0x02;  // Spring effect
        // Use both left and right coefficients for asymmetric spring
        send_buffer[4] = state->effect.u.condition[0].right_coeff & 0xff;
        send_buffer[5] = (state->effect.u.condition[0].right_coeff >> 8) & 0xff;
        send_buffer[6] = state->effect.u.condition[0].left_coeff & 0xff;
        send_buffer[7] = (state->effect.u.condition[0].left_coeff >> 8) & 0xff;
        // Center point (deadband) and saturation
        send_buffer[8] = state->effect.u.condition[0].center & 0xff;
        send_buffer[9] = state->effect.u.condition[0].deadband & 0xff;
        break;

    case FF_DAMPER:
        send_buffer[3] = 0x03;  // Damper effect
        send_buffer[4] = state->effect.u.condition[0].right_coeff & 0xff;
        send_buffer[5] = (state->effect.u.condition[0].right_coeff >> 8) & 0xff;
        send_buffer[6] = state->effect.u.condition[0].left_coeff & 0xff;
        send_buffer[7] = (state->effect.u.condition[0].left_coeff >> 8) & 0xff;
        break;

    case FF_FRICTION:
        send_buffer[3] = 0x04;  // Friction effect
        send_buffer[4] = state->effect.u.condition[0].right_coeff & 0xff;
        send_buffer[5] = (state->effect.u.condition[0].right_coeff >> 8) & 0xff;
        break;

    case FF_SINE:
        send_buffer[3] = 0x05;  // Sine wave
        magnitude = abs(state->effect.u.periodic.magnitude);
        send_buffer[4] = magnitude & 0xff;
        send_buffer[5] = (magnitude >> 8) & 0xff;
        // Period in milliseconds
        send_buffer[6] = state->effect.u.periodic.period & 0xff;
        send_buffer[7] = (state->effect.u.periodic.period >> 8) & 0xff;
        // Phase (0-360 degrees)
        send_buffer[8] = state->effect.u.periodic.phase / 360 * 0xff;
        break;

    case FF_SAW_UP:
        send_buffer[3] = 0x06;  // Sawtooth up
        magnitude = abs(state->effect.u.periodic.magnitude);
        send_buffer[4] = magnitude & 0xff;
        send_buffer[5] = (magnitude >> 8) & 0xff;
        send_buffer[6] = state->effect.u.periodic.period & 0xff;
        send_buffer[7] = (state->effect.u.periodic.period >> 8) & 0xff;
        break;

    case FF_SAW_DOWN:
        send_buffer[3] = 0x07;  // Sawtooth down
        magnitude = abs(state->effect.u.periodic.magnitude);
        send_buffer[4] = magnitude & 0xff;
        send_buffer[5] = (magnitude >> 8) & 0xff;
        send_buffer[6] = state->effect.u.periodic.period & 0xff;
        send_buffer[7] = (state->effect.u.periodic.period >> 8) & 0xff;
        break;

    default:
        ret = -EINVAL;
        goto err;
    }

    // Common parameters for all effects
    if (state->effect.replay.delay) {
        send_buffer[8] = state->effect.replay.delay & 0xff;
        send_buffer[9] = (state->effect.replay.delay >> 8) & 0xff;
    }
    
    if (state->effect.replay.length) {
        send_buffer[10] = state->effect.replay.length & 0xff;
        send_buffer[11] = (state->effect.replay.length >> 8) & 0xff;
    }

    ret = t500rs_send_buf(t500rs, send_buffer, t500rs->buffer_length);

err:
    kfree(send_buffer);
    return ret;
}

static int t500rs_update_effect(void *data, struct tmff2_effect_state *state)
{
    return t500rs_upload_effect(data, state);
}

int t500rs_set_gain(void *data, uint16_t gain)
{
    struct t500rs_device_entry *t500rs = data;
    struct t500rs_packet_header *header;
    int ret;

    if (!t500rs || !t500rs->send_buffer) {
        printk(KERN_ERR "T500RS: Invalid data in set_gain\n");
        return -EINVAL;
    }

    if (t500rs->state != T500RS_STATE_READY) {
        hid_warn(t500rs->hdev, "device not ready, ignoring set gain\n");
        return -EAGAIN;
    }

    if (gain > 0xffff)
        gain = 0xffff;

    header = (struct t500rs_packet_header *)t500rs->send_buffer;
    header->cmd = 0x41;
    header->id = 0x04;
    header->gain = gain;

    ret = t500rs_send_int(t500rs);
    if (ret == 0) {
        t500rs->current_gain = gain;
        t500rs->last_command_time = jiffies;
        t500rs->command_retries = 0;
    }

    return ret;
}

int t500rs_set_autocenter(void *data, uint16_t value)
{
    struct t500rs_device_entry *t500rs = data;
    struct t500rs_packet_header *header;
    int ret;

    if (!t500rs || !t500rs->send_buffer) {
        printk(KERN_ERR "T500RS: Invalid data in set_autocenter\n");
        return -EINVAL;
    }

    if (t500rs->state != T500RS_STATE_READY) {
        hid_warn(t500rs->hdev, "device not ready, ignoring set autocenter\n");
        return -EAGAIN;
    }

    if (!t500rs->force_feedback_enabled) {
        hid_warn(t500rs->hdev, "force feedback not enabled, ignoring set autocenter\n");
        return -ENODEV;
    }

    if (value > 0xffff)
        value = 0xffff;

    header = (struct t500rs_packet_header *)t500rs->send_buffer;
    header->cmd = 0x41;
    header->id = 0x05;
    header->autocenter = value;

    ret = t500rs_send_int(t500rs);
    if (ret == 0) {
        t500rs->last_command_time = jiffies;
        t500rs->command_retries = 0;
    }

    return ret;
}

int t500rs_populate_api(struct tmff2_device_entry *tmff2)
{
    tmff2->max_effects = TMT500RS_MAX_EFFECTS;
    memcpy(tmff2->supported_effects, t500rs_effects, sizeof(t500rs_effects));
    tmff2->params = t500rs_params;
    tmff2->wheel_init = t500rs_wheel_init;
    tmff2->wheel_destroy = t500rs_wheel_destroy;
    tmff2->wheel_fixup = t500rs_wheel_fixup;
    tmff2->set_range = t500rs_set_range;
    tmff2->play_effect = t500rs_play_effect;
    tmff2->upload_effect = t500rs_upload_effect;
    tmff2->update_effect = t500rs_update_effect;
    tmff2->stop_effect = t500rs_stop_effect;
    tmff2->set_gain = t500rs_set_gain;
    tmff2->set_autocenter = t500rs_set_autocenter;
    return 0;
}
EXPORT_SYMBOL_GPL(t500rs_populate_api);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Caz");
MODULE_DESCRIPTION("T500RS Driver Module");
