#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/hid.h>
#include "../hid-tmff2.h"
#include "hid-tmt500rs.h"

static void t500rs_urb_complete(struct urb *urb)
{
    struct t500rs_device_data *data = urb->context;
    struct hid_device *hdev = data->hdev;
    int retries = 0;
    unsigned long flags;

    /* Protect against concurrent mode switch */
    spin_lock_irqsave(&data->lock, flags);

    if (!data->initialized && data->state != T500RS_STATE_INITIALIZING) {
        dev_dbg(&hdev->dev, "T500RS: Device not ready in URB completion (state=%d)\n", data->state);
        spin_unlock_irqrestore(&data->lock, flags);
        return;
    }

    switch (urb->status) {
    case 0:
        // Success
        break;

    case -ECONNRESET:
    case -ENOENT:
    case -ESHUTDOWN:
        // These errors are expected when unplugging
        spin_unlock_irqrestore(&data->lock, flags);
        return;

    case -EPIPE:
        dev_dbg(&hdev->dev, "T500RS: URB pipe error, attempting to clear halt\n");
        usb_clear_halt(data->usbdev, urb->pipe);
        break;

    case -EPROTO:
        dev_dbg(&hdev->dev, "T500RS: URB protocol error\n");
        break;

    case -EILSEQ:
        dev_dbg(&hdev->dev, "T500RS: URB CRC error\n");
        break;

    case -ETIMEDOUT:
        dev_dbg(&hdev->dev, "T500RS: URB timeout\n");
        data->state = T500RS_STATE_ERROR;
        spin_unlock_irqrestore(&data->lock, flags);
        return;

    case -EOVERFLOW:
        dev_dbg(&hdev->dev, "T500RS: URB overflow error\n");
        break;

    default:
        dev_err(&hdev->dev, "T500RS: Unexpected URB status %d\n", urb->status);
        data->state = T500RS_STATE_ERROR;
        spin_unlock_irqrestore(&data->lock, flags);
        return;
    }

    /* Only resubmit if we're still in ready or initializing state */
    if (data->state == T500RS_STATE_READY || data->state == T500RS_STATE_INITIALIZING) {
        while (retries < 3) {
            int ret = usb_submit_urb(urb, GFP_ATOMIC);
            if (ret == 0)
                break;
            if (ret != -ENODEV && ret != -EPIPE)
                break;
            retries++;
            msleep(50);
        }
    }

    spin_unlock_irqrestore(&data->lock, flags);
}

int t500rs_init_usb(struct t500rs_device_entry *t500rs)
{
    struct t500rs_device_data *data;
    struct usb_device *usbdev;
    struct usb_interface *intf;
    struct usb_host_interface *interface;
    struct usb_endpoint_descriptor *endpoint;
    int ret, retries = 0;

    if (!t500rs || !t500rs->data || !t500rs->data->hdev)
        return -EINVAL;

    data = t500rs->data;
    intf = to_usb_interface(data->hdev->dev.parent);
    if (!intf)
        return -ENODEV;

    usbdev = interface_to_usbdev(intf);
    if (!usbdev)
        return -ENODEV;

    interface = intf->cur_altsetting;
    if (!interface)
        return -ENODEV;

    /* Find the interrupt in endpoint */
    endpoint = &interface->endpoint[0].desc;
    if (!endpoint || !usb_endpoint_is_int_in(endpoint)) {
        dev_err(&data->hdev->dev, "T500RS: Could not find interrupt IN endpoint\n");
        return -ENODEV;
    }

    /* Store the polling interval */
    data->interval = endpoint->bInterval;
    if (data->interval < 1)
        data->interval = 1;

    // Make sure we're not already initialized
    if (data->initialized) {
        t500rs_cleanup_usb(t500rs);
        msleep(100); // Wait for cleanup to complete
    }

    data->state = T500RS_STATE_INITIALIZING;
    data->usbdev = usbdev;
    data->initialized = false;

retry_init:
    // Allocate URB and buffer
    data->urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!data->urb) {
        ret = -ENOMEM;
        goto err_free_data;
    }

    data->buffer = usb_alloc_coherent(data->usbdev, T500RS_REPORT_LENGTH,
                                     GFP_KERNEL, &data->buffer_dma);
    if (!data->buffer) {
        ret = -ENOMEM;
        goto err_free_urb;
    }

    // Initialize URB with proper interval
    usb_fill_int_urb(data->urb, data->usbdev,
                     usb_rcvintpipe(data->usbdev, endpoint->bEndpointAddress),
                     data->buffer, T500RS_REPORT_LENGTH,
                     t500rs_urb_complete, data, data->interval);
    data->urb->transfer_dma = data->buffer_dma;
    data->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

    // Wait for device to settle
    msleep(200);

    // Submit URB
    ret = usb_submit_urb(data->urb, GFP_KERNEL);
    if (ret) {
        if ((ret == -ENODEV || ret == -EPIPE || ret == -19) && retries < 5) {
            dev_info(&data->hdev->dev, "T500RS: Device not ready, retrying... (attempt %d)\n", retries + 1);
            usb_free_coherent(data->usbdev, T500RS_REPORT_LENGTH,
                            data->buffer, data->buffer_dma);
            usb_free_urb(data->urb);
            msleep(200 * (retries + 1));
            retries++;
            goto retry_init;
        }
        dev_err(&data->hdev->dev, "T500RS: Failed to submit URB: %d\n", ret);
        goto err_free_buffer;
    }

    // Wait for URB to start processing
    msleep(200);

    // Send initial device state command
    ret = t500rs_send_command(t500rs, 0x0f, 0x01, 0x00);
    if (ret < 0) {
        dev_err(&data->hdev->dev, "T500RS: Failed to set initial state: %d\n", ret);
        goto err_kill_urb;
    }

    // Wait for device to process command
    msleep(500);

    // Verify device is still connected
    if (!data->usbdev || !data->urb) {
        dev_err(&data->hdev->dev, "T500RS: Device disconnected during initialization\n");
        ret = -ENODEV;
        goto err_kill_urb;
    }

    data->state = T500RS_STATE_READY;
    data->initialized = true;
    return 0;

err_kill_urb:
    if (data->urb) {
        usb_kill_urb(data->urb);
        msleep(100);
    }
err_free_buffer:
    if (data->buffer)
        usb_free_coherent(data->usbdev, T500RS_REPORT_LENGTH,
                         data->buffer, data->buffer_dma);
err_free_urb:
    if (data->urb)
        usb_free_urb(data->urb);
err_free_data:
    data->state = T500RS_STATE_ERROR;
    return ret;
}

void t500rs_cleanup_usb(struct t500rs_device_entry *t500rs)
{
    struct t500rs_device_data *data;
    unsigned long flags;

    if (!t500rs || !t500rs->data)
        return;

    data = t500rs->data;

    /* Protect against concurrent access */
    spin_lock_irqsave(&data->lock, flags);

    /* Mark as disconnecting before cleanup */
    data->state = T500RS_STATE_DISCONNECTED;
    data->initialized = false;

    spin_unlock_irqrestore(&data->lock, flags);

    /* Kill URB and wait for completion */
    if (data->urb) {
        usb_kill_urb(data->urb);
        msleep(100);

        if (data->buffer) {
            usb_free_coherent(data->usbdev, T500RS_REPORT_LENGTH,
                             data->buffer, data->buffer_dma);
            data->buffer = NULL;
        }
        usb_free_urb(data->urb);
        data->urb = NULL;
    }
}

void t500rs_stop_urbs(struct t500rs_device_entry *t500rs)
{
    struct t500rs_device_data *data;
    unsigned long flags;

    if (!t500rs || !t500rs->data)
        return;

    data = t500rs->data;

    /* Protect against concurrent access */
    spin_lock_irqsave(&data->lock, flags);
    
    if (data->urb) {
        /* Mark as disconnecting before killing URB */
        data->state = T500RS_STATE_DISCONNECTED;
        spin_unlock_irqrestore(&data->lock, flags);
        
        /* Kill URB and wait for completion */
        usb_kill_urb(data->urb);
        msleep(100);
    } else {
        spin_unlock_irqrestore(&data->lock, flags);
    }
}

int t500rs_send_cmd_with_retry(struct t500rs_device_entry *t500rs, u8 *buf, size_t len, int max_retries)
{
    int ret, retries = 0;
    struct hid_device *hdev;

    if (!t500rs || !t500rs->data || !buf || len == 0)
        return -EINVAL;

    hdev = t500rs->data->hdev;
    if (!hdev)
        return -ENODEV;

    do {
        ret = hid_hw_raw_request(hdev, buf[0], buf, len, HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);
        if (ret < 0) {
            if (retries < max_retries) {
                retries++;
                msleep(50);
                continue;
            }
            hid_err(hdev, "raw request failed: %d\n", ret);
            return ret;
        }
        break;
    } while (retries < max_retries);

    return 0;
}

int t500rs_read_response(struct t500rs_device_entry *t500rs, u8 *buf, size_t len)
{
    int ret;
    struct hid_device *hdev;

    if (!t500rs || !t500rs->data || !buf || len == 0)
        return -EINVAL;

    hdev = t500rs->data->hdev;
    if (!hdev)
        return -ENODEV;

    /* Wait for any pending URBs to complete */
    hid_hw_wait(hdev);

    ret = hid_hw_raw_request(hdev, 0, buf, len, HID_INPUT_REPORT, HID_REQ_GET_REPORT);
    if (ret < 0) {
        hid_err(hdev, "read response failed: %d\n", ret);
        return ret;
    }

    return 0;
}

int t500rs_send_command(struct t500rs_device_entry *t500rs, u8 cmd_type, u8 cmd_id, u8 param)
{
    u8 cmd[T500RS_REPORT_LENGTH] = {0};

    if (!t500rs || !t500rs->data)
        return -EINVAL;

    cmd[0] = cmd_type;
    cmd[1] = cmd_id;
    cmd[2] = param;

    return t500rs_send_cmd_with_retry(t500rs, cmd, sizeof(cmd), 3);
}

int t500rs_interrupts(struct t500rs_device_data *data)
{
    if (!data)
        return -EINVAL;

    return 0;
}

EXPORT_SYMBOL_GPL(t500rs_init_usb);
EXPORT_SYMBOL_GPL(t500rs_cleanup_usb);
EXPORT_SYMBOL_GPL(t500rs_stop_urbs);
EXPORT_SYMBOL_GPL(t500rs_send_cmd_with_retry);
EXPORT_SYMBOL_GPL(t500rs_read_response);
EXPORT_SYMBOL_GPL(t500rs_send_command);
EXPORT_SYMBOL_GPL(t500rs_interrupts);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Force feedback support for Thrustmaster T500RS - USB");
