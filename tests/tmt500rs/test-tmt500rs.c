#define USB_VENDOR_ID_THRUSTMASTER 0x044f
#define USB_DEVICE_ID_THRUSTMASTER_T500RS 0xb65d

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Test framework for Thrustmaster T500RS driver
 *
 * Copyright (c) 2024 Your Name
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/workqueue.h>
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
    struct input_dev *input_dev;
    /* Test-specific fields */
    struct delayed_work work;
    atomic_t pending_transfers;
    bool initialized;
    struct mutex lock;  /* Protect device state */
};

/* Global test device */
static struct mock_t500rs_device *test_mock_dev;
static DEFINE_MUTEX(test_lock);  /* Protect global state */

/* Mock HID device release function */
static void mock_hid_release(struct device *dev)
{
    pr_debug("Mock HID device released\n");
}

/* Mock USB device release function */
static void mock_usb_release(struct device *dev)
{
    pr_debug("Mock USB device released\n");
}

static int test_device_init(void)
{
    int ret;
    struct tmff2_device_entry *tmff2;
    struct t500rs_device_entry *t500rs;

    pr_info("=== Starting T500RS Driver Test Suite ===\n");
    pr_info("T500RS Test [1/3]: Device initialization\n");

    mutex_lock(&test_lock);
    if (!test_mock_dev || !test_mock_dev->initialized) {
        mutex_unlock(&test_lock);
        pr_err("Mock device not properly initialized\n");
        return -EINVAL;
    }

    mutex_lock(&test_mock_dev->lock);
    tmff2 = &test_mock_dev->base;

    /* Setup HID device */
    test_mock_dev->hdev = kzalloc(sizeof(struct hid_device), GFP_KERNEL);
    if (!test_mock_dev->hdev) {
        mutex_unlock(&test_mock_dev->lock);
        mutex_unlock(&test_lock);
        return -ENOMEM;
    }

    /* Setup USB device */
    test_mock_dev->usbdev = kzalloc(sizeof(struct usb_device), GFP_KERNEL);
    if (!test_mock_dev->usbdev) {
        kfree(test_mock_dev->hdev);
        test_mock_dev->hdev = NULL;
        mutex_unlock(&test_mock_dev->lock);
        mutex_unlock(&test_lock);
        return -ENOMEM;
    }

    /* Setup input device */
    test_mock_dev->input_dev = input_allocate_device();
    if (!test_mock_dev->input_dev) {
        kfree(test_mock_dev->usbdev);
        test_mock_dev->usbdev = NULL;
        kfree(test_mock_dev->hdev);
        test_mock_dev->hdev = NULL;
        mutex_unlock(&test_mock_dev->lock);
        mutex_unlock(&test_lock);
        return -ENOMEM;
    }

    /* Initialize device fields */
    test_mock_dev->hdev->vendor = USB_VENDOR_ID_THRUSTMASTER;
    test_mock_dev->hdev->product = USB_DEVICE_ID_THRUSTMASTER_T500RS;
    test_mock_dev->hdev->bus = BUS_USB;
    test_mock_dev->hdev->type = HID_TYPE_USBMOUSE;
    test_mock_dev->hdev->dev.parent = &test_mock_dev->usbdev->dev;
    test_mock_dev->hdev->dev.release = mock_hid_release;
    device_initialize(&test_mock_dev->hdev->dev);

    test_mock_dev->usbdev->descriptor.idVendor = USB_VENDOR_ID_THRUSTMASTER;
    test_mock_dev->usbdev->descriptor.idProduct = USB_DEVICE_ID_THRUSTMASTER_T500RS;
    test_mock_dev->usbdev->dev.release = mock_usb_release;
    device_initialize(&test_mock_dev->usbdev->dev);

    /* Setup base device */
    tmff2->hdev = test_mock_dev->hdev;
    tmff2->input_dev = test_mock_dev->input_dev;

    /* Initialize input device */
    test_mock_dev->input_dev->name = "Thrustmaster T500RS Racing Wheel";
    test_mock_dev->input_dev->id.bustype = BUS_USB;
    test_mock_dev->input_dev->id.vendor = USB_VENDOR_ID_THRUSTMASTER;
    test_mock_dev->input_dev->id.product = USB_DEVICE_ID_THRUSTMASTER_T500RS;
    test_mock_dev->input_dev->dev.parent = &test_mock_dev->hdev->dev;

    /* Initialize T500RS device entry */
    t500rs = kzalloc(sizeof(*t500rs), GFP_KERNEL);
    if (!t500rs) {
        ret = -ENOMEM;
        goto cleanup_devices;
    }

    t500rs->hdev = test_mock_dev->hdev;
    t500rs->usbdev = test_mock_dev->usbdev;
    t500rs->input_dev = test_mock_dev->input_dev;
    tmff2->data = t500rs;

    /* Register devices in correct order */
    ret = device_add(&test_mock_dev->usbdev->dev);
    if (ret) {
        pr_err("Failed to add USB device: %d\n", ret);
        goto cleanup_t500rs;
    }

    ret = device_add(&test_mock_dev->hdev->dev);
    if (ret) {
        pr_err("Failed to add HID device: %d\n", ret);
        goto cleanup_usb;
    }

    /* Initialize input device */
    input_set_capability(test_mock_dev->input_dev, EV_FF, FF_CONSTANT);
    input_set_capability(test_mock_dev->input_dev, EV_FF, FF_SPRING);
    input_set_capability(test_mock_dev->input_dev, EV_FF, FF_DAMPER);
    input_set_capability(test_mock_dev->input_dev, EV_FF, FF_FRICTION);

    ret = input_register_device(test_mock_dev->input_dev);
    if (ret) {
        pr_err("Failed to register input device: %d\n", ret);
        goto cleanup_hid;
    }

    /* Initialize wheel */
    ret = t500rs_wheel_init(tmff2, 1);
    if (ret) {
        pr_err("Failed to initialize wheel: %d\n", ret);
        goto cleanup_input;
    }

    mutex_unlock(&test_mock_dev->lock);
    mutex_unlock(&test_lock);
    pr_info("Device initialization test passed\n");
    return 0;

cleanup_input:
    input_unregister_device(test_mock_dev->input_dev);
    test_mock_dev->input_dev = NULL;  /* input_unregister_device frees the device */
cleanup_hid:
    device_del(&test_mock_dev->hdev->dev);
cleanup_usb:
    device_del(&test_mock_dev->usbdev->dev);
cleanup_t500rs:
    kfree(t500rs);
    tmff2->data = NULL;
cleanup_devices:
    if (test_mock_dev->input_dev)
        input_free_device(test_mock_dev->input_dev);
    kfree(test_mock_dev->hdev);
    test_mock_dev->hdev = NULL;
    kfree(test_mock_dev->usbdev);
    test_mock_dev->usbdev = NULL;
    mutex_unlock(&test_mock_dev->lock);
    mutex_unlock(&test_lock);
    return ret;
}

static int test_force_feedback(void)
{
    int ret;
    struct tmff2_device_entry *tmff2;
    struct tmff2_effect_state effect = {
        .effect = {
            .type = FF_CONSTANT,
            .id = -1,
            .u.constant = {
                .level = 0x4000,  /* 50% force */
            },
        },
    };

    pr_info("T500RS Test [2/3]: Force feedback\n");

    if (!test_mock_dev || !test_mock_dev->initialized) {
        pr_err("Mock device not properly initialized\n");
        return -EINVAL;
    }

    tmff2 = &test_mock_dev->base;

    /* Upload effect */
    ret = t500rs_upload_effect(tmff2, &effect);
    if (ret) {
        pr_err("Failed to upload effect: %d\n", ret);
        return ret;
    }

    /* Play effect */
    effect.flags = FF_EFFECT_PLAYING;
    ret = t500rs_play_effect(tmff2, &effect);
    if (ret) {
        pr_err("Failed to play effect: %d\n", ret);
        return ret;
    }

    /* Stop effect */
    effect.flags = 0;
    ret = t500rs_play_effect(tmff2, &effect);
    if (ret) {
        pr_err("Failed to stop effect: %d\n", ret);
        return ret;
    }

    pr_info("Force feedback test passed\n");
    return 0;
}

static int test_cleanup(void)
{
    struct tmff2_device_entry *tmff2;

    pr_info("T500RS Test [3/3]: Cleanup\n");

    mutex_lock(&test_lock);
    if (!test_mock_dev || !test_mock_dev->initialized) {
        mutex_unlock(&test_lock);
        pr_err("Mock device not properly initialized\n");
        return -EINVAL;
    }

    mutex_lock(&test_mock_dev->lock);
    tmff2 = &test_mock_dev->base;

    /* Test cleanup functions */
    if (tmff2->data) {
        t500rs_wheel_init(tmff2, 0); /* 0 to cleanup */
        kfree(tmff2->data);
        tmff2->data = NULL;
    }

    if (test_mock_dev->input_dev) {
        input_unregister_device(test_mock_dev->input_dev);
        test_mock_dev->input_dev = NULL;  /* input_unregister_device frees the device */
    }

    if (test_mock_dev->hdev) {
        device_del(&test_mock_dev->hdev->dev);
        kfree(test_mock_dev->hdev);
        test_mock_dev->hdev = NULL;
    }

    if (test_mock_dev->usbdev) {
        device_del(&test_mock_dev->usbdev->dev);
        kfree(test_mock_dev->usbdev);
        test_mock_dev->usbdev = NULL;
    }

    mutex_unlock(&test_mock_dev->lock);
    mutex_unlock(&test_lock);
    pr_info("Cleanup test passed\n");
    return 0;
}

static int __init test_tmt500rs_init(void)
{
    int ret = 0;
    struct mock_t500rs_device *mock_dev;

    pr_info("TMT500RS test module loaded\n");

    mutex_lock(&test_lock);

    /* Allocate and initialize mock device */
    mock_dev = kzalloc(sizeof(*mock_dev), GFP_KERNEL);
    if (!mock_dev) {
        mutex_unlock(&test_lock);
        pr_err("Failed to allocate mock device\n");
        return -ENOMEM;
    }

    /* Initialize mutex */
    mutex_init(&mock_dev->lock);

    /* Initialize device fields */
    memset(&mock_dev->base, 0, sizeof(mock_dev->base));
    atomic_set(&mock_dev->pending_transfers, 0);

    /* Store mock device for cleanup */
    test_mock_dev = mock_dev;
    mock_dev->initialized = true;

    mutex_unlock(&test_lock);

    /* Run test cases */
    ret = test_device_init();
    if (ret) {
        pr_err("Device initialization test failed\n");
        goto cleanup;
    }

    ret = test_force_feedback();
    if (ret) {
        pr_err("Force feedback test failed\n");
        goto cleanup;
    }

    ret = test_cleanup();
    if (ret) {
        pr_err("Cleanup test failed\n");
        goto cleanup;
    }

    pr_info("=== T500RS Driver Test Summary ===\n");
    pr_info("Total test phases: 3/3\n");
    pr_info("Total assertions: 3\n");
    pr_info("Passed: 3\n");
    pr_info("Failed: 0\n");
    pr_info("Success rate: 100%%\n");

    return 0;

cleanup:
    if (mock_dev) {
        if (mock_dev->initialized) {
            test_cleanup();
        }
        mutex_destroy(&mock_dev->lock);
        kfree(mock_dev);
        test_mock_dev = NULL;
    }
    return ret;
}

static void __exit test_tmt500rs_exit(void)
{
    pr_info("TMT500RS test module unloaded\n");
    
    if (test_mock_dev) {
        if (test_mock_dev->initialized) {
            test_cleanup();
        }
        mutex_destroy(&test_mock_dev->lock);
        kfree(test_mock_dev);
        test_mock_dev = NULL;
    }
}

module_init(test_tmt500rs_init);
module_exit(test_tmt500rs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Test framework for Thrustmaster T500RS driver");
MODULE_VERSION("1.0");
MODULE_SOFTDEP("pre: hid-tmff-new");
MODULE_IMPORT_NS(ANDROID_GKI_VFS_EXPORT_ONLY); 