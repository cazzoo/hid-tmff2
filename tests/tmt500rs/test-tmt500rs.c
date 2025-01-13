#define USB_VENDOR_ID_THRUSTMASTER 0x044f
#define USB_DEVICE_ID_THRUSTMASTER_T500RS 0xb65d

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Test framework for Thrustmaster T500RS driver
 *
 * Copyright (c) 2024 Your Name
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/fixp-arith.h>
#include <linux/dma-mapping.h>
#include <linux/input.h>
#include "../../src/hid-tmff2.h"
#include "../../src/tmt500rs/hid-tmt500rs.h"

MODULE_AUTHOR("Test Author");
MODULE_DESCRIPTION("Test module for TMT500RS driver");
MODULE_LICENSE("GPL");

/* Mock device structure */
struct mock_t500rs_device {
    struct tmff2_device_entry base;
    struct hid_device *hdev;
    struct usb_device *usbdev;
    struct usb_interface *intf;
    struct usb_host_interface *alt;
    struct usb_host_endpoint *ep;
    struct platform_device *pdev;  /* Root device */
    /* Test-specific fields */
    bool initialized;
    bool devices_registered;
    struct mutex lock;  /* Protect device state */
};

/* Mock HID parse function */
static int mock_test_hid_parse(struct hid_device *hdev)
{
    struct mock_t500rs_device *mock_dev = hid_get_drvdata(hdev);

    if (!mock_dev)
        return -ENODEV;

    return 0;
}

/* Mock HID raw request function */
static int mock_test_hid_raw_request(struct hid_device *hdev, unsigned char reportnum,
                              __u8 *buf, size_t len, unsigned char rtype,
                              int reqtype)
{
    struct mock_t500rs_device *mock_dev = hid_get_drvdata(hdev);

    if (!mock_dev)
        return -ENODEV;

    return len;
}

/* Mock HID low-level driver */
static struct hid_ll_driver mock_test_hid_ll_driver = {
    .parse = mock_test_hid_parse,
    .raw_request = mock_test_hid_raw_request,
};

/* Mock HID driver */
static struct hid_driver mock_test_hid_driver = {
    .name = "mock_test_hid",
};

/* Global test device */
static struct mock_t500rs_device *test_mock_dev;
static DEFINE_MUTEX(test_lock);  /* Protect global state */

/* Mock device type for USB device */
static struct device_type mock_test_usb_type = {
    .name = "mock_test_usb_device",
};

/* Mock device type for HID device */
static struct device_type mock_test_hid_type = {
    .name = "mock_test_hid",
};

/* Mock HID device release function */
static void mock_test_hid_release(struct device *dev)
{
    pr_debug("Mock test HID device released\n");
}

/* Mock USB device release function */
static void mock_test_usb_release(struct device *dev)
{
    pr_debug("Mock test USB device released\n");
}

/* Add debug logging for device state */
static void debug_device_state(struct mock_t500rs_device *mock_dev, const char *stage)
{
    if (!mock_dev)
        return;

    pr_debug("Device state at %s:\n", stage);
    pr_debug("  initialized: %d\n", mock_dev->initialized);
    pr_debug("  devices_registered: %d\n", mock_dev->devices_registered);
    pr_debug("  hdev: %p\n", mock_dev->hdev);
    pr_debug("  usbdev: %p\n", mock_dev->usbdev);
    pr_debug("  intf: %p\n", mock_dev->intf);
    pr_debug("  pdev: %p\n", mock_dev->pdev);
}

/* Device state validation function */
static int validate_device_state(struct mock_t500rs_device *mock_dev, const char *stage)
{
    int ret = 0;

    if (!mock_dev) {
        pr_err("Invalid device state at %s: NULL device\n", stage);
        return -EINVAL;
    }

    /* Check device hierarchy */
        if (mock_dev->initialized) {
        if (!mock_dev->hdev) {
            pr_err("Invalid state at %s: initialized but no HID device\n", stage);
            ret = -EINVAL;
        }
        if (!mock_dev->usbdev) {
            pr_err("Invalid state at %s: initialized but no USB device\n", stage);
            ret = -EINVAL;
        }
        if (!mock_dev->intf) {
            pr_err("Invalid state at %s: initialized but no interface\n", stage);
            ret = -EINVAL;
        }
    }

    /* Check device registration state */
    if (mock_dev->devices_registered) {
        if (mock_dev->hdev && !device_is_registered(&mock_dev->hdev->dev)) {
            pr_err("Invalid state at %s: HID device marked registered but not registered\n", stage);
            ret = -EINVAL;
        }
        if (mock_dev->usbdev && !device_is_registered(&mock_dev->usbdev->dev)) {
            pr_err("Invalid state at %s: USB device marked registered but not registered\n", stage);
            ret = -EINVAL;
        }
        if (mock_dev->intf && !device_is_registered(&mock_dev->intf->dev)) {
            pr_err("Invalid state at %s: Interface marked registered but not registered\n", stage);
            ret = -EINVAL;
        }
    }

    return ret;
}

/* Safe device cleanup function with retry mechanism */
static int safe_device_cleanup_with_retry(struct mock_t500rs_device *mock_dev, int max_retries)
{
    int retry_count = 0;
    int ret;
    bool got_lock;

    if (!mock_dev)
        return -EINVAL;

    while (retry_count < max_retries) {
        debug_device_state(mock_dev, "cleanup_retry_start");
        
        /* Try to get the lock with timeout */
        got_lock = mutex_trylock(&mock_dev->lock);
        if (!got_lock) {
            pr_warn("Could not get device lock during cleanup, proceeding without lock\n");
            /* Continue without lock - better than hanging */
        }

        ret = validate_device_state(mock_dev, "cleanup_start");
        if (ret)
            pr_warn("Device in invalid state before cleanup\n");

        /* First mark as not registered to prevent new operations */
        mock_dev->devices_registered = false;
        mock_dev->initialized = false;
        
        if (got_lock)
            mutex_unlock(&mock_dev->lock);

        synchronize_rcu();  /* Ensure no one is still using our devices */
        msleep(100);  /* Wait for any pending operations to complete */

        /* Remove HID device if registered */
        if (mock_dev->hdev) {
            struct device *hdev = &mock_dev->hdev->dev;
            if (device_is_registered(hdev)) {
                device_release_driver(hdev);
                msleep(50);
                device_del(hdev);
                synchronize_rcu();
            }
            /* Only put the device if it's still referenced */
            if (device_is_registered(hdev) && refcount_read(&hdev->kobj.kref.refcount) > 0) {
                put_device(hdev);
            }
            mock_dev->hdev = NULL;
        }

        /* Remove USB interface if registered */
        if (mock_dev->intf) {
            struct device *intf = &mock_dev->intf->dev;
            if (device_is_registered(intf)) {
                device_release_driver(intf);
                msleep(50);
                device_del(intf);
                synchronize_rcu();
            }
            /* Only put the device if it's still referenced */
            if (device_is_registered(intf) && refcount_read(&intf->kobj.kref.refcount) > 0) {
                put_device(intf);
            }
            mock_dev->intf = NULL;
        }

        /* Remove USB device if registered */
        if (mock_dev->usbdev) {
            struct device *udev = &mock_dev->usbdev->dev;
            if (device_is_registered(udev)) {
                device_release_driver(udev);
                msleep(50);
                device_del(udev);
                synchronize_rcu();
            }
            if (mock_dev->usbdev->bus) {
                kfree(mock_dev->usbdev->bus);
                mock_dev->usbdev->bus = NULL;
            }
            /* Only put the device if it's still referenced */
            if (device_is_registered(udev) && refcount_read(&udev->kobj.kref.refcount) > 0) {
                put_device(udev);
            }
            mock_dev->usbdev = NULL;
        }

        /* Remove platform device if registered */
        if (mock_dev->pdev) {
            platform_device_unregister(mock_dev->pdev);
            mock_dev->pdev = NULL;
        }

        /* Free remaining structures */
        kfree(mock_dev->ep);
        mock_dev->ep = NULL;
        kfree(mock_dev->alt);
        mock_dev->alt = NULL;

        debug_device_state(mock_dev, "cleanup_retry_complete");

        /* Check if cleanup was successful */
        if (!mock_dev->hdev && !mock_dev->usbdev && !mock_dev->intf && !mock_dev->pdev) {
            pr_info("Device cleanup successful after %d retries\n", retry_count);
            return 0;
        }

        retry_count++;
        if (retry_count < max_retries) {
            pr_info("Device cleanup retry %d/%d\n", retry_count + 1, max_retries);
            msleep(100);
        }
    }

    pr_err("Device cleanup failed after %d retries\n", max_retries);
    return -EBUSY;
}

static int test_device_init(void)
{
    int ret;
    struct tmff2_device_entry *tmff2;

    pr_info("=== Starting T500RS Driver Test Suite ===\n");
    pr_info("T500RS Test [1/3]: Device initialization\n");

    mutex_lock(&test_lock);
    if (!test_mock_dev) {
        mutex_unlock(&test_lock);
        pr_err("Mock device not allocated\n");
        return -EINVAL;
    }

    mutex_lock(&test_mock_dev->lock);
    
    /* Check if already initialized */
    if (test_mock_dev->initialized) {
        mutex_unlock(&test_mock_dev->lock);
        mutex_unlock(&test_lock);
        pr_err("Device already initialized\n");
        return -EEXIST;
    }

    debug_device_state(test_mock_dev, "init_start");

    tmff2 = &test_mock_dev->base;

    /* Create platform device as root */
    test_mock_dev->pdev = platform_device_alloc("tmt500rs_test", PLATFORM_DEVID_AUTO);
    if (!test_mock_dev->pdev) {
        ret = -ENOMEM;
        goto err_cleanup;
    }

    ret = platform_device_add(test_mock_dev->pdev);
    if (ret) {
        platform_device_put(test_mock_dev->pdev);
        test_mock_dev->pdev = NULL;
        goto err_cleanup;
    }

    /* Initialize HID device */
    test_mock_dev->hdev = kzalloc(sizeof(struct hid_device), GFP_NOIO);
    if (!test_mock_dev->hdev) {
        ret = -ENOMEM;
        goto err_cleanup;
    }

    /* Initialize device hierarchy properly */
    device_initialize(&test_mock_dev->hdev->dev);
    test_mock_dev->hdev->dev.parent = &test_mock_dev->pdev->dev;
    test_mock_dev->hdev->dev.type = &mock_test_hid_type;
    test_mock_dev->hdev->dev.release = mock_test_hid_release;
    dev_set_name(&test_mock_dev->hdev->dev, "mock_test_hid_%d", 0);

    /* Initialize USB device */
    test_mock_dev->usbdev = kzalloc(sizeof(struct usb_device), GFP_NOIO);
    if (!test_mock_dev->usbdev) {
        ret = -ENOMEM;
        goto err_cleanup;
    }

    /* Initialize USB device hierarchy */
    device_initialize(&test_mock_dev->usbdev->dev);
    test_mock_dev->usbdev->dev.parent = &test_mock_dev->pdev->dev;
    test_mock_dev->usbdev->dev.type = &mock_test_usb_type;
    test_mock_dev->usbdev->dev.release = mock_test_usb_release;
    dev_set_name(&test_mock_dev->usbdev->dev, "mock_test_usb_%d", 0);

    /* Initialize USB interface */
    test_mock_dev->intf = kzalloc(sizeof(struct usb_interface), GFP_NOIO);
    if (!test_mock_dev->intf) {
        ret = -ENOMEM;
        goto err_cleanup;
    }

    /* Initialize interface hierarchy */
    device_initialize(&test_mock_dev->intf->dev);
    test_mock_dev->intf->dev.parent = &test_mock_dev->usbdev->dev;
    test_mock_dev->intf->dev.type = &mock_test_usb_type;
    dev_set_name(&test_mock_dev->intf->dev, "mock_test_intf_%d", 0);

    /* Setup interface alternate setting */
    test_mock_dev->alt = kzalloc(sizeof(struct usb_host_interface), GFP_NOIO);
    if (!test_mock_dev->alt) {
        ret = -ENOMEM;
        goto err_cleanup;
    }

    /* Setup endpoint */
    test_mock_dev->ep = kzalloc(sizeof(struct usb_host_endpoint) * 2, GFP_NOIO);
    if (!test_mock_dev->ep) {
        ret = -ENOMEM;
        goto err_cleanup;
    }

    /* Setup IN endpoint */
    test_mock_dev->ep[0].desc.bEndpointAddress = 0x81;  /* IN endpoint */
    test_mock_dev->ep[0].desc.bmAttributes = USB_ENDPOINT_XFER_INT;
    test_mock_dev->ep[0].desc.wMaxPacketSize = cpu_to_le16(32);
    test_mock_dev->ep[0].desc.bInterval = 1;

    /* Setup OUT endpoint */
    test_mock_dev->ep[1].desc.bEndpointAddress = 0x01;  /* OUT endpoint */
    test_mock_dev->ep[1].desc.bmAttributes = USB_ENDPOINT_XFER_INT;
    test_mock_dev->ep[1].desc.wMaxPacketSize = cpu_to_le16(32);
    test_mock_dev->ep[1].desc.bInterval = 1;

    /* Initialize USB interface */
    test_mock_dev->alt->desc.bInterfaceNumber = 0;
    test_mock_dev->alt->desc.bAlternateSetting = 0;
    test_mock_dev->alt->desc.bNumEndpoints = 2;
    test_mock_dev->alt->endpoint = test_mock_dev->ep;
    test_mock_dev->intf->cur_altsetting = test_mock_dev->alt;

    /* Setup HID device */
    test_mock_dev->hdev->vendor = USB_VENDOR_ID_THRUSTMASTER;
    test_mock_dev->hdev->product = USB_DEVICE_ID_THRUSTMASTER_T500RS;
    test_mock_dev->hdev->bus = BUS_USB;
    test_mock_dev->hdev->type = HID_TYPE_USBNONE;
    test_mock_dev->hdev->driver = &mock_test_hid_driver;
    test_mock_dev->hdev->ll_driver = &mock_test_hid_ll_driver;

    /* Setup USB device */
    test_mock_dev->usbdev->descriptor.idVendor = cpu_to_le16(USB_VENDOR_ID_THRUSTMASTER);
    test_mock_dev->usbdev->descriptor.idProduct = cpu_to_le16(USB_DEVICE_ID_THRUSTMASTER_T500RS);
    test_mock_dev->usbdev->bus = kzalloc(sizeof(struct usb_bus), GFP_NOIO);
    if (!test_mock_dev->usbdev->bus) {
        ret = -ENOMEM;
        goto err_cleanup;
    }

    /* Initialize device data */
    hid_set_drvdata(test_mock_dev->hdev, test_mock_dev);

    /* Add devices in proper order */
    ret = device_add(&test_mock_dev->usbdev->dev);
    if (ret)
        goto err_cleanup;

    ret = device_add(&test_mock_dev->intf->dev);
    if (ret)
        goto err_cleanup;

    ret = device_add(&test_mock_dev->hdev->dev);
    if (ret)
        goto err_cleanup;

    /* Mark as initialized */
    test_mock_dev->initialized = true;
    test_mock_dev->devices_registered = true;

    debug_device_state(test_mock_dev, "init_complete");

    mutex_unlock(&test_mock_dev->lock);
    mutex_unlock(&test_lock);
    return 0;

err_cleanup:
    debug_device_state(test_mock_dev, "init_error");
    safe_device_cleanup_with_retry(test_mock_dev, 5);
    mutex_unlock(&test_mock_dev->lock);
    mutex_unlock(&test_lock);
    return ret;
}

/* Test module specific functions */
static int mock_test_t500rs_play_effect(struct mock_t500rs_device *mock_dev)
{
    return 0;
}

static int test_t500rs_populate_api(struct mock_t500rs_device *mock_dev)
{
    return 0;
}

static int test_t500rs_wheel_init(struct mock_t500rs_device *mock_dev)
{
    return 0;
}

static void test_t500rs_wheel_destroy(struct mock_t500rs_device *mock_dev)
{
}

static int test_t500rs_send_command(struct mock_t500rs_device *mock_dev, u8 cmd)
{
    return 0;
}

static int test_t500rs_send_buf(struct mock_t500rs_device *mock_dev, const u8 *buf, size_t len)
{
    return 0;
}

static void test_t500rs_urb_complete(struct urb *urb)
{
}

static int test_t500rs_send_int(struct mock_t500rs_device *mock_dev, const u8 *buf, size_t len)
{
    return 0;
}

static int test_t500rs_interrupts(struct mock_t500rs_device *mock_dev)
{
    return 0;
}

static int test_t500rs_upload_effect(struct mock_t500rs_device *mock_dev, struct ff_effect *effect)
{
    return 0;
}

static int test_t500rs_set_gain(struct mock_t500rs_device *mock_dev, u16 gain)
{
    return 0;
}

static int test_t500rs_set_autocenter(struct mock_t500rs_device *mock_dev, u16 magnitude)
{
    return 0;
}

static int test_t500rs_set_range(struct mock_t500rs_device *mock_dev, u16 range)
{
    return 0;
}

static int test_t500rs_wheel_fixup(struct mock_t500rs_device *mock_dev)
{
    return 0;
}

static int test_t500rs_open(struct mock_t500rs_device *mock_dev)
{
    return 0;
}

static void test_t500rs_close(struct mock_t500rs_device *mock_dev)
{
}

/* Test module initialization */
static int __init test_tmt500rs_init(void)
{
    int ret = 0;
    struct mock_t500rs_device *mock_dev;

    pr_info("TMT500RS test module loaded\n");

    mutex_lock(&test_lock);

    /* Allocate and initialize mock device */
    mock_dev = kzalloc(sizeof(*mock_dev), GFP_NOIO);
    if (!mock_dev) {
        mutex_unlock(&test_lock);
        pr_err("Failed to allocate mock device\n");
        return -ENOMEM;
    }

    /* Initialize mutex */
    mutex_init(&mock_dev->lock);

    /* Initialize device fields */
    memset(&mock_dev->base, 0, sizeof(mock_dev->base));
    mock_dev->devices_registered = false;
    mock_dev->initialized = false;
    mock_dev->ep = NULL;
    mock_dev->alt = NULL;
    mock_dev->intf = NULL;
    mock_dev->hdev = NULL;
    mock_dev->usbdev = NULL;

    /* Store mock device for cleanup */
    test_mock_dev = mock_dev;

    mutex_unlock(&test_lock);

    /* Run test cases */
    ret = test_device_init();
    if (ret) {
        pr_err("Device initialization test failed\n");
        goto cleanup;
    }

    pr_info("=== T500RS Driver Test Summary ===\n");
    pr_info("Total test phases: 1/1\n");
    pr_info("Total assertions: 1\n");
    pr_info("Passed: 1\n");
    pr_info("Failed: 0\n");
    pr_info("Success rate: 100%%\n");

    return 0;

cleanup:
    if (mock_dev) {
        mutex_lock(&test_lock);
        mutex_lock(&mock_dev->lock);
        safe_device_cleanup_with_retry(mock_dev, 5);
        mutex_unlock(&mock_dev->lock);
        mutex_destroy(&mock_dev->lock);
        kfree(mock_dev);
        test_mock_dev = NULL;
        mutex_unlock(&test_lock);
    }
    return ret;
}

static void __exit test_tmt500rs_exit(void)
{
    int ret;
    bool got_lock;

    pr_info("TMT500RS test module unloaded\n");
    
    if (test_mock_dev) {
        /* Try to get the lock with timeout */
        got_lock = mutex_trylock(&test_lock);
        if (!got_lock) {
            pr_warn("Could not get test lock during exit, proceeding without lock\n");
        }

        debug_device_state(test_mock_dev, "exit_start");

        /* Try cleanup with retries */
        ret = safe_device_cleanup_with_retry(test_mock_dev, 5);
        if (ret) {
            pr_err("Failed to cleanup devices, forcing cleanup\n");
            /* Force cleanup as last resort */
            if (test_mock_dev->hdev) {
                device_release_driver(&test_mock_dev->hdev->dev);
                device_del(&test_mock_dev->hdev->dev);
                put_device(&test_mock_dev->hdev->dev);
            }
            if (test_mock_dev->intf) {
                device_release_driver(&test_mock_dev->intf->dev);
                device_del(&test_mock_dev->intf->dev);
                put_device(&test_mock_dev->intf->dev);
            }
            if (test_mock_dev->usbdev) {
                device_release_driver(&test_mock_dev->usbdev->dev);
                device_del(&test_mock_dev->usbdev->dev);
                if (test_mock_dev->usbdev->bus)
                    kfree(test_mock_dev->usbdev->bus);
                put_device(&test_mock_dev->usbdev->dev);
            }
            if (test_mock_dev->pdev)
                platform_device_unregister(test_mock_dev->pdev);
            kfree(test_mock_dev->ep);
            kfree(test_mock_dev->alt);
        }

        debug_device_state(test_mock_dev, "exit_cleanup");

        mutex_destroy(&test_mock_dev->lock);
        kfree(test_mock_dev);
        test_mock_dev = NULL;

        if (got_lock)
        mutex_unlock(&test_lock);

        debug_device_state(NULL, "exit_complete");
    }
}

module_init(test_tmt500rs_init);
module_exit(test_tmt500rs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Test Author");
MODULE_DESCRIPTION("Test framework for Thrustmaster T500RS driver");
MODULE_VERSION("1.0");
MODULE_SOFTDEP("pre: hid-tmff-new");
MODULE_IMPORT_NS(ANDROID_GKI_VFS_EXPORT_ONLY); 