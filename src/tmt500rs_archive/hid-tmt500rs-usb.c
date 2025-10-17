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
    int ret;

    if (debug) {
        dev_info(&hdev->dev, "URB completion status: %d, actual_length: %d\n",
                 urb->status, urb->actual_length);
    }

    /* Protect against concurrent mode switch */
    spin_lock_irqsave(&data->lock, flags);

    /* Check device state */
    if (!data->usbdev || !data->urb) {
        dev_dbg(&hdev->dev, "T500RS: Device disconnected in URB completion\n");
        data->state = T500RS_STATE_DISCONNECTED;
        spin_unlock_irqrestore(&data->lock, flags);
        return;
    }

    /* Only process URB completion if we're in a valid state */
    if (data->state != T500RS_STATE_READY && 
        data->state != T500RS_STATE_INITIALIZING) {
        dev_dbg(&hdev->dev, "T500RS: Ignoring URB completion in state %d\n", data->state);
        spin_unlock_irqrestore(&data->lock, flags);
        return;
    }

    switch (urb->status) {
    case 0:
        /* Success - Reset error count */
        data->urb_error_count = 0;
        
        /* Handle state transition */
        if (data->state == T500RS_STATE_INITIALIZING) {
            dev_info(&hdev->dev, "T500RS: First URB completed successfully, device ready\n");
            data->state = T500RS_STATE_READY;
            data->initialized = true;
        }
        break;

    case -ECONNRESET:
    case -ENOENT:
    case -ESHUTDOWN:
        /* Expected errors during shutdown/unplug */
        dev_dbg(&hdev->dev, "T500RS: Expected URB error during unplug: %d\n", urb->status);
        data->state = T500RS_STATE_DISCONNECTED;
        spin_unlock_irqrestore(&data->lock, flags);
        return;

    case -ENODEV:
        /* Handle ENODEV more gracefully during initialization */
        if (data->state == T500RS_STATE_INITIALIZING) {
            dev_dbg(&hdev->dev, "T500RS: ENODEV during initialization, retrying\n");
            data->urb_error_count++;
            if (data->urb_error_count > 10) {  // More retries during init
                dev_err(&hdev->dev, "T500RS: Too many ENODEV errors during init\n");
                data->state = T500RS_STATE_ERROR;
                spin_unlock_irqrestore(&data->lock, flags);
                return;
            }
        } else {
            /* In ready state, be more lenient */
            data->urb_error_count++;
            if (data->urb_error_count > 5) {  // More retries in ready state
                dev_dbg(&hdev->dev, "T500RS: Multiple ENODEV errors, marking disconnected\n");
                data->state = T500RS_STATE_DISCONNECTED;
                spin_unlock_irqrestore(&data->lock, flags);
                return;
            }
        }
        break;

    case -EPIPE:
        dev_dbg(&hdev->dev, "T500RS: URB pipe error, attempting to clear halt\n");
        spin_unlock_irqrestore(&data->lock, flags);
        usb_clear_halt(data->usbdev, urb->pipe);
        spin_lock_irqsave(&data->lock, flags);
        break;

    default:
        dev_err(&hdev->dev, "T500RS: Unexpected URB status %d\n", urb->status);
        data->urb_error_count++;
        
        /* Be more lenient with errors during initialization */
        if (data->state == T500RS_STATE_INITIALIZING) {
            if (data->urb_error_count > 20) {  // More retries during init
                dev_err(&hdev->dev, "T500RS: Too many URB errors during init\n");
                data->state = T500RS_STATE_ERROR;
                spin_unlock_irqrestore(&data->lock, flags);
                return;
            }
        } else {
            if (data->urb_error_count > 15) {  // More retries in ready state
                dev_err(&hdev->dev, "T500RS: Too many URB errors, resetting device\n");
                data->state = T500RS_STATE_ERROR;
                spin_unlock_irqrestore(&data->lock, flags);
                usb_reset_device(data->usbdev);
                return;
            }
        }
        break;
    }

    /* Only resubmit if we're still in a valid state */
    if (data->state == T500RS_STATE_READY || 
        data->state == T500RS_STATE_INITIALIZING) {
        
        while (retries < 8) {  // More retries for resubmission
            ret = usb_submit_urb(urb, GFP_ATOMIC);
            if (ret == 0) {
                dev_dbg(&hdev->dev, "T500RS: URB resubmitted successfully\n");
                break;
            }
            
            if (ret == -ENODEV) {
                /* Handle ENODEV differently during initialization */
                if (data->state == T500RS_STATE_INITIALIZING) {
                    dev_dbg(&hdev->dev, "T500RS: ENODEV during init resubmit, retrying\n");
                    data->urb_error_count++;
                    if (data->urb_error_count > 10) {  // More retries during init
                        dev_err(&hdev->dev, "T500RS: Too many ENODEV errors during init resubmit\n");
                        data->state = T500RS_STATE_ERROR;
                        break;
                    }
                } else {
                    data->urb_error_count++;
                    if (data->urb_error_count > 5) {  // More retries in ready state
                        dev_dbg(&hdev->dev, "T500RS: Device disconnected during URB resubmit\n");
                        data->state = T500RS_STATE_DISCONNECTED;
                        break;
                    }
                }
            }
            
            if (ret == -EPIPE) {
                dev_dbg(&hdev->dev, "T500RS: Pipe error during resubmit, clearing halt\n");
                spin_unlock_irqrestore(&data->lock, flags);
                usb_clear_halt(data->usbdev, urb->pipe);
                spin_lock_irqsave(&data->lock, flags);
            }
            
            retries++;
            if (retries < 8) {
                dev_dbg(&hdev->dev, "T500RS: URB resubmit retry %d/8\n", retries);
                if (data->state == T500RS_STATE_INITIALIZING)
                    udelay(2000);  // Longer delay during init
                else
                    udelay(1000);  // Normal delay during operation
            }
        }
        
        if (retries == 8) {
            dev_err(&hdev->dev, "T500RS: Failed to resubmit URB after %d retries\n", retries);
            data->state = T500RS_STATE_ERROR;
        }
    } else {
        dev_dbg(&hdev->dev, "T500RS: Not resubmitting URB in state %d\n", data->state);
    }

    spin_unlock_irqrestore(&data->lock, flags);
}

int t500rs_init_usb(struct t500rs_device_entry *t500rs)
{
    struct t500rs_device_data *data;
    struct usb_device *usbdev;
    struct usb_interface *intf;
    struct usb_host_interface *interface;
    struct usb_endpoint_descriptor *endpoint = NULL;
    int pipe, i;
    bool found_interrupt_in = false;

    if (!t500rs || !t500rs->data)
        return -EINVAL;

    data = t500rs->data;
    usbdev = data->usbdev;
    intf = to_usb_interface(data->hdev->dev.parent);

    if (debug) {
        enum usb_device_state dev_state = usbdev->state;
        dev_info(&data->hdev->dev, 
                "USB device state: %d (%s)\n", 
                dev_state,
                dev_state == USB_STATE_CONFIGURED ? "CONFIGURED" :
                dev_state == USB_STATE_SUSPENDED ? "SUSPENDED" :
                dev_state == USB_STATE_DEFAULT ? "DEFAULT" :
                dev_state == USB_STATE_ADDRESS ? "ADDRESS" : "UNKNOWN");
        
        dev_info(&data->hdev->dev, "USB device speed: %d\n", usbdev->speed);
        dev_info(&data->hdev->dev, "USB interface number: %d\n", intf->cur_altsetting->desc.bInterfaceNumber);
    }

    interface = intf->cur_altsetting;
    
    /* Log endpoint information */
    if (debug) {
        dev_info(&data->hdev->dev, "Interface has %d endpoints:\n", interface->desc.bNumEndpoints);
        for (i = 0; i < interface->desc.bNumEndpoints; i++) {
            struct usb_endpoint_descriptor *ep = &interface->endpoint[i].desc;
            dev_info(&data->hdev->dev, 
                    "Endpoint %d: addr=0x%02x, type=%s, dir=%s, maxpacket=%d\n",
                    i,
                    ep->bEndpointAddress,
                    (ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT ? "INT" :
                    (ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK ? "BULK" :
                    (ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_CONTROL ? "CTRL" : "OTHER",
                    ep->bEndpointAddress & USB_DIR_IN ? "IN" : "OUT",
                    le16_to_cpu(ep->wMaxPacketSize));
        }
    }

    /* Find interrupt IN endpoint */
    for (i = 0; i < interface->desc.bNumEndpoints; i++) {
        struct usb_endpoint_descriptor *ep = &interface->endpoint[i].desc;
        
        if ((ep->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN &&
            (ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT) {
            endpoint = ep;
            found_interrupt_in = true;
            if (debug) {
                dev_info(&data->hdev->dev, 
                        "Found interrupt IN endpoint: 0x%02x, maxpacket=%d, interval=%d\n",
                        endpoint->bEndpointAddress,
                        le16_to_cpu(endpoint->wMaxPacketSize),
                        endpoint->bInterval);
            }
            break;
        }
    }

    if (!found_interrupt_in) {
        dev_err(&data->hdev->dev, "Could not find interrupt IN endpoint\n");
        return -ENODEV;
    }

    /* Store USB device reference */
    spin_lock_irqsave(&data->lock, flags);
    data->usbdev = usbdev;
    data->state = T500RS_STATE_INITIALIZING;
    spin_unlock_irqrestore(&data->lock, flags);

    /* Wait for device to settle after enumeration */
    msleep(5000);  // Wait longer for device to settle

    /* Store the polling interval */
    data->interval = 1;  // Force 1ms polling

    /* Clear any halt condition on the endpoint */
    ret = usb_clear_halt(usbdev, usb_rcvintpipe(usbdev, endpoint->bEndpointAddress));
    if (ret) {
        dev_warn(&data->hdev->dev, "T500RS: Failed to clear endpoint halt: %d\n", ret);
    }
    msleep(2000);  // Wait for endpoint to stabilize

    /* Allocate URB */
    data->urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!data->urb) {
        dev_err(&data->hdev->dev, "T500RS: Failed to allocate URB\n");
        ret = -ENOMEM;
        goto err_put_interface;
    }

    /* Allocate coherent buffer */
    data->buffer = usb_alloc_coherent(usbdev, 32, GFP_KERNEL, &data->urb->transfer_dma);
    if (!data->buffer) {
        dev_err(&data->hdev->dev, "T500RS: Failed to allocate URB buffer\n");
        ret = -ENOMEM;
        goto err_free_urb;
    }

    /* Fill the URB */
    usb_fill_int_urb(data->urb, usbdev,
                     usb_rcvintpipe(usbdev, endpoint->bEndpointAddress),
                     data->buffer, 32, t500rs_urb_complete,
                     data, data->interval);

    data->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

    /* Submit URB with retries */
    while (retries < 8) {
        ret = usb_submit_urb(data->urb, GFP_KERNEL);
        if (ret == 0) {
            dev_info(&data->hdev->dev, "T500RS: URB submitted successfully\n");
            break;
        }

        if (ret == -ENODEV) {
            dev_warn(&data->hdev->dev, "T500RS: Device not ready (retry %d/8)\n", retries + 1);
        } else if (ret == -EPIPE) {
            dev_warn(&data->hdev->dev, "T500RS: Pipe error (retry %d/8)\n", retries + 1);
            usb_clear_halt(usbdev, data->urb->pipe);
            msleep(2000);  // Wait after clearing halt
        } else {
            dev_warn(&data->hdev->dev, "T500RS: URB submit error %d (retry %d/8)\n", ret, retries + 1);
        }

        retries++;
        if (retries < 8) {
            msleep(2000 * retries);  // Exponential backoff
        }
    }

    if (ret) {
        dev_err(&data->hdev->dev, "T500RS: Failed to submit URB after %d retries: %d\n", retries, ret);
        goto err_free_buffer;
    }

    /* Re-enable USB autosuspend */
    msleep(2000);  // Wait before re-enabling
    usb_autopm_put_interface(intf);

    dev_info(&data->hdev->dev, "T500RS: USB initialized successfully\n");
    return 0;

err_free_buffer:
    usb_free_coherent(usbdev, 32, data->buffer, data->urb->transfer_dma);
err_free_urb:
    usb_free_urb(data->urb);
    data->urb = NULL;
err_put_interface:
    usb_autopm_put_interface(intf);
    return ret;
}

void t500rs_cleanup_usb(struct t500rs_device_entry *t500rs)
{
    struct t500rs_device_data *data;
    struct usb_interface *intf;
    struct usb_device *usbdev;
    unsigned long flags;
    int retries = 0;

    if (!t500rs || !t500rs->data)
        return;

    data = t500rs->data;

    /* Protect against concurrent access */
    spin_lock_irqsave(&data->lock, flags);

    /* Store local copy of USB device pointer and clear it to prevent new URB submissions */
    usbdev = data->usbdev;
    data->usbdev = NULL;

    /* Mark device as disconnected */
    data->state = T500RS_STATE_DISCONNECTED;
    data->initialized = false;

    spin_unlock_irqrestore(&data->lock, flags);

    /* Get interface for power management */
    intf = to_usb_interface(data->hdev->dev.parent);
    if (intf) {
        /* Disable autosuspend during cleanup */
        usb_autopm_get_interface(intf);
        msleep(2000);  // Wait for power management to stabilize
    }

    /* Kill URB with retries */
    if (data->urb) {
        while (retries < 8) {  // More retries for URB killing
            usb_kill_urb(data->urb);
            if (atomic_read(&data->urb->use_count) == 0) {
                dev_dbg(&data->hdev->dev, "T500RS: URB killed successfully\n");
                break;
            }
            dev_dbg(&data->hdev->dev, "T500RS: URB still in use, retry %d/8\n", retries + 1);
            msleep(2000 * (retries + 1));  // Increasing delays with longer base time
            retries++;
        }

        if (retries == 8) {
            dev_warn(&data->hdev->dev, "T500RS: URB may still be in use after cleanup\n");
        }
    }

    /* Clear endpoint halt conditions */
    if (usbdev) {
        usb_clear_halt(usbdev, usb_rcvintpipe(usbdev, 0x82));
        msleep(2000);  // Wait longer for halt clear
    }

    /* Free USB resources */
    if (data->buffer) {
        usb_free_coherent(usbdev, 32, data->buffer, data->urb->transfer_dma);
        data->buffer = NULL;
    }

    if (data->urb) {
        usb_free_urb(data->urb);
        data->urb = NULL;
    }

    /* Re-enable autosuspend */
    if (intf) {
        msleep(2000);  // Wait before re-enabling autosuspend
        usb_autopm_put_interface(intf);
    }

    dev_info(&data->hdev->dev, "T500RS: USB cleanup complete\n");
}

int t500rs_start_urbs(struct t500rs_device_entry *t500rs)
{
    struct t500rs_device_data *data;
    int ret;
    enum usb_device_state dev_state;

    if (!t500rs || !t500rs->data)
        return -EINVAL;

    data = t500rs->data;
    
    if (!data->urb) {
        dev_err(&data->hdev->dev, "URB not initialized\n");
        return -EINVAL;
    }

    dev_state = data->usbdev->state;
    if (debug) {
        dev_info(&data->hdev->dev, 
                "Pre-submit USB state: %d (%s)\n",
                dev_state,
                dev_state == USB_STATE_CONFIGURED ? "CONFIGURED" :
                dev_state == USB_STATE_SUSPENDED ? "SUSPENDED" :
                dev_state == USB_STATE_DEFAULT ? "DEFAULT" :
                dev_state == USB_STATE_ADDRESS ? "ADDRESS" : "UNKNOWN");
        
        if (data->urb->ep) {
            dev_info(&data->hdev->dev, 
                    "URB endpoint: 0x%02x, maxpacket: %d, int_interval: %d\n",
                    data->urb->ep->desc.bEndpointAddress,
                    usb_endpoint_maxp(&data->urb->ep->desc),
                    data->urb->ep->desc.bInterval);
        }
    }

    if (dev_state != USB_STATE_CONFIGURED) {
        dev_err(&data->hdev->dev, "USB device not in configured state\n");
        return -ENODEV;
    }

    ret = usb_submit_urb(data->urb, GFP_KERNEL);
    if (ret) {
        dev_err(&data->hdev->dev, 
                "Failed to submit URB: %d (endpoint: 0x%02x, state: %d)\n",
                ret,
                data->urb->ep ? data->urb->ep->desc.bEndpointAddress : 0,
                dev_state);
        return ret;
    }

    if (debug) {
        dev_info(&data->hdev->dev, "URB submitted successfully\n");
    }

    return 0;
}

void t500rs_stop_urbs(struct t500rs_device_entry *t500rs)
{
    struct t500rs_device_data *data;
    unsigned long flags;
    int retries = 0;

    if (!t500rs || !t500rs->data)
        return;

    data = t500rs->data;

    spin_lock_irqsave(&data->lock, flags);

    if (!data->urb) {
        spin_unlock_irqrestore(&data->lock, flags);
        return;
    }

    /* Mark device as disconnected to prevent new URB submissions */
    data->state = T500RS_STATE_DISCONNECTED;

    spin_unlock_irqrestore(&data->lock, flags);

    /* Kill URB with retries */
    while (retries < 8) {  // More retries for URB killing
        usb_kill_urb(data->urb);
        if (atomic_read(&data->urb->use_count) == 0) {
            dev_dbg(&data->hdev->dev, "T500RS: URB stopped successfully\n");
            break;
        }
        msleep(2000 * (retries + 1));  // Increasing delays
        retries++;
    }

    if (retries == 8) {
        dev_warn(&data->hdev->dev, "T500RS: URB may still be in use after stop\n");
    }

    dev_info(&data->hdev->dev, "T500RS: URBs stopped\n");
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
