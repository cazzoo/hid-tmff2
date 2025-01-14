#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include "hid-tmt500rs.h"

#define T500RS_REPORT_LENGTH 64

static void t500rs_urb_complete(struct urb *urb)
{
    struct t500rs_device_data *data = urb->context;
    struct hid_device *hdev = data->hdev;
    int retries = 0;

    if (!data->initialized) {
        tmff2_dbg("T500RS: Device not initialized in URB completion\n");
        return;
    }

    if (data->state != T500RS_STATE_READY) {
        tmff2_dbg("T500RS: Device not ready in URB completion (state=%d)\n", data->state);
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
        return;

    case -EPIPE:
        tmff2_dbg("T500RS: URB pipe error, attempting to clear halt\n");
        usb_clear_halt(data->usbdev, urb->pipe);
        break;

    case -EPROTO:
        tmff2_dbg("T500RS: URB protocol error\n");
        break;

    case -EILSEQ:
        tmff2_dbg("T500RS: URB CRC error\n");
        break;

    case -ETIMEDOUT:
        tmff2_dbg("T500RS: URB timeout\n");
        data->state = T500RS_STATE_ERROR;
        return;

    case -EOVERFLOW:
        tmff2_dbg("T500RS: URB overflow error\n");
        break;

    default:
        dev_err(&hdev->dev, "T500RS: Unexpected URB status %d\n", urb->status);
        data->state = T500RS_STATE_ERROR;
        return;
    }

    // Resubmit the URB with retries
    while (retries < 3) {
        int ret = usb_submit_urb(urb, GFP_ATOMIC);
        if (ret == 0)
            break;
        if (ret != -19 && ret != -ENODEV)
            break;
        msleep(100 * (retries + 1));
        retries++;
    }
}

int t500rs_init_usb(struct hid_device *hdev)
{
    struct t500rs_device_data *data;
    struct usb_device *usbdev = interface_to_usbdev(to_usb_interface(hdev->dev.parent));
    int ret;

    data = kzalloc(sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->state = T500RS_STATE_INITIALIZING;
    data->usbdev = usbdev;
    data->hdev = hdev;

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

    // Initialize URB
    usb_fill_int_urb(data->urb, data->usbdev,
                     usb_rcvintpipe(data->usbdev, 0x81),
                     data->buffer, T500RS_REPORT_LENGTH,
                     t500rs_urb_complete, data, data->interval);
    data->urb->transfer_dma = data->buffer_dma;
    data->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

    // Submit URB
    ret = usb_submit_urb(data->urb, GFP_KERNEL);
    if (ret) {
        dev_err(&hdev->dev, "T500RS: Failed to submit URB: %d\n", ret);
        goto err_free_buffer;
    }

    data->state = T500RS_STATE_READY;
    data->initialized = true;
    return 0;

err_free_buffer:
    usb_free_coherent(data->usbdev, T500RS_REPORT_LENGTH,
                      data->buffer, data->buffer_dma);
err_free_urb:
    usb_free_urb(data->urb);
err_free_data:
    data->state = T500RS_STATE_ERROR;
    kfree(data);
    return ret;
}

void t500rs_cleanup_usb(struct hid_device *hdev)
{
    struct t500rs_device_data *data = hid_get_drvdata(hdev);

    if (!data)
        return;

    data->state = T500RS_STATE_DISCONNECTED;
    data->initialized = false;

    if (data->urb) {
        usb_kill_urb(data->urb);
        if (data->buffer) {
            usb_free_coherent(data->usbdev, T500RS_REPORT_LENGTH,
                             data->buffer, data->buffer_dma);
            data->buffer = NULL;
        }
        usb_free_urb(data->urb);
        data->urb = NULL;
    }
}

int t500rs_interrupts(struct t500rs_device_data *data)
{
    struct hid_device *hdev = data->hdev;
    int ret;

    if (!data->initialized || data->state != T500RS_STATE_READY)
        return -EINVAL;

    if (data->urb) {
        ret = usb_submit_urb(data->urb, GFP_ATOMIC);
        if (ret) {
            if (ret == -EPIPE) {
                tmff2_dbg("T500RS: URB pipe error, attempting to clear halt\n");
                usb_clear_halt(data->usbdev, data->urb->pipe);
                ret = usb_submit_urb(data->urb, GFP_ATOMIC);
            }

            if (ret) {
                dev_err(&hdev->dev, "T500RS: Failed to submit URB: %d\n", ret);
                data->state = T500RS_STATE_ERROR;
                return ret;
            }
        }
    }

    return 0;
}

int t500rs_send_command(struct t500rs_device_entry *t500rs, u8 cmd_type, u8 cmd_id, u8 param)
{
    struct t500rs_device_data *data = t500rs->data;
    struct hid_device *hdev = data->hdev;
    u8 *buf;
    int ret;

    if (!data->initialized) {
        dev_err(&hdev->dev, "T500RS: Device not initialized\n");
        return -EINVAL;
    }

    buf = kzalloc(T500RS_REPORT_LENGTH, GFP_KERNEL);
    if (!buf)
        return -ENOMEM;

    buf[0] = cmd_type;
    buf[1] = cmd_id;
    buf[2] = param;

    ret = hid_hw_raw_request(hdev, 0, buf, T500RS_REPORT_LENGTH,
                            HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);

    kfree(buf);
    return ret;
}
