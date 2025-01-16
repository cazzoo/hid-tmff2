// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Force feedback support for Thrustmaster T500RS - Core
 *
 * Copyright (c) 2024 Your Name
 */

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
#include <linux/device.h>
#include <asm/unaligned.h>
#include "../hid-tmff2.h"
#include "hid-tmt500rs.h"

/* Use the main driver's debug parameter */
extern bool debug;

#define tmff2_dbg(fmt, ...) \
    do { if (debug) dev_dbg(&hdev->dev, fmt, ##__VA_ARGS__); } while (0)

int t500rs_populate_api(struct tmff2_device_entry *tmff2)
{
    struct t500rs_device_entry *t500rs;
    
    t500rs = kzalloc(sizeof(*t500rs), GFP_KERNEL);
    if (!t500rs)
        return -ENOMEM;

    t500rs->data = kzalloc(sizeof(struct t500rs_device_data), GFP_KERNEL);
    if (!t500rs->data) {
        kfree(t500rs);
        return -ENOMEM;
    }

    t500rs->data->hdev = tmff2->hdev;
    t500rs->data->usbdev = to_usb_device(tmff2->hdev->dev.parent);
    t500rs->data->state = T500RS_STATE_INIT;
    t500rs->data->initialized = false;

    tmff2->data = t500rs;
    tmff2->set_range = t500rs_set_range;
    tmff2->wheel_fixup = t500rs_wheel_fixup;
    tmff2->open = t500rs_open;
    tmff2->close = t500rs_close;
    tmff2->wheel_init = t500rs_wheel_init;
    tmff2->wheel_destroy = t500rs_wheel_destroy;

    return 0;
}

/* Mode attribute */
static ssize_t mode_show(struct device *dev, struct device_attribute *attr,
                        char *buf)
{
    struct hid_device *hdev = to_hid_device(dev);
    struct tmff2_device_entry *tmff2 = hid_get_drvdata(hdev);
    struct t500rs_device_entry *t500rs;
    u8 mode;
    int ret;

    if (!tmff2 || !tmff2->data)
        return -EINVAL;

    t500rs = tmff2->data;
    ret = t500rs_detect_mode(t500rs, &mode);
    if (ret)
        return ret;

    return sprintf(buf, "%u\n", mode);
}

static ssize_t mode_store(struct device *dev, struct device_attribute *attr,
                         const char *buf, size_t count)
{
    struct hid_device *hdev = to_hid_device(dev);
    struct tmff2_device_entry *tmff2 = hid_get_drvdata(hdev);
    struct t500rs_device_entry *t500rs;
    unsigned int mode;
    int ret;

    if (!tmff2 || !tmff2->data)
        return -EINVAL;

    t500rs = tmff2->data;
    ret = kstrtouint(buf, 10, &mode);
    if (ret)
        return ret;

    if (mode != TMT500RS_MODE_WHEEL && mode != TMT500RS_MODE_JOYSTICK)
        return -EINVAL;

    ret = t500rs_start_mode_switch(t500rs, mode);
    if (ret)
        return ret;

    return count;
}

static DEVICE_ATTR_RW(mode);

static struct attribute *t500rs_attrs[] = {
    &dev_attr_mode.attr,
    NULL
};

static const struct attribute_group t500rs_attr_group = {
    .attrs = t500rs_attrs,
};

static const struct hid_device_id t500rs_devices[] = {
    /* T500RS in initialization mode */
    { HID_USB_DEVICE(0x044f, 0xb65d),
      .driver_data = T500RS_STATE_INIT },
    /* T500RS in wheel mode */
    { HID_USB_DEVICE(0x044f, 0xb65e),
      .driver_data = T500RS_STATE_READY },
    { }
};
MODULE_DEVICE_TABLE(hid, t500rs_devices);

/* Forward declarations */
static int t500rs_probe(struct hid_device *hdev, const struct hid_device_id *id);
static void t500rs_remove(struct hid_device *hdev);

/* Driver structure */
static struct hid_driver t500rs_driver = {
    .name = "hid-tmt500rs",
    .id_table = t500rs_devices,
    .probe = t500rs_probe,
    .remove = t500rs_remove,
};

/* Function implementations */
static int t500rs_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
    struct t500rs_device_entry *t500rs;
    struct tmff2_device_entry *tmff2;
    int ret;

    /* Check if we can handle this device */
    if (!hid_is_usb(hdev))
        return -EINVAL;

    /* Unbind from hid-generic if it's bound */
    if (hdev->driver && hdev->driver->name && 
        strcmp(hdev->driver->name, "hid-generic") == 0) {
        tmff2_dbg("Unbinding from hid-generic\n");
        hid_hw_stop(hdev);
    }

    tmff2 = kzalloc(sizeof(*tmff2), GFP_KERNEL);
    if (!tmff2)
        return -ENOMEM;

    tmff2->hdev = hdev;
    ret = t500rs_populate_api(tmff2);
    if (ret) {
        kfree(tmff2);
        return ret;
    }

    hid_set_drvdata(hdev, tmff2);
    t500rs = tmff2->data;

    /* Set initial state based on device ID */
    t500rs->data->state = id->driver_data;
    t500rs->data->initialized = false;

    /* Parse HID report descriptors */
    ret = hid_parse(hdev);
    if (ret) {
        hid_err(hdev, "parse failed\n");
        goto err_free_tmff2;
    }

    /* Start with minimal features */
    ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
    if (ret) {
        hid_err(hdev, "hw start failed\n");
        goto err_free_tmff2;
    }

    /* Create sysfs attributes */
    ret = sysfs_create_group(&hdev->dev.kobj, &t500rs_attr_group);
    if (ret) {
        hid_err(hdev, "sysfs group creation failed\n");
        goto err_stop_hw;
    }

    /* Initialize based on current state */
    switch (t500rs->data->state) {
    case T500RS_STATE_INIT:
        /* Wait for device to settle */
        msleep(100);
        ret = t500rs_init_usb(t500rs);
        if (ret) {
            hid_err(hdev, "USB initialization failed\n");
            goto err_remove_sysfs;
        }
        t500rs->data->state = T500RS_STATE_INITIALIZING;
        /* Wait for device to reconnect */
        msleep(500);
        /* fallthrough */
    case T500RS_STATE_INITIALIZING:
        /* Wait for device to settle after USB init */
        msleep(100);
        ret = t500rs_wheel_init(tmff2, 0);
        if (ret) {
            hid_err(hdev, "wheel initialization failed\n");
            goto err_cleanup_usb;
        }
        /* Wait for wheel init to complete */
        msleep(100);
        ret = t500rs_init_ff(t500rs);
        if (ret) {
            hid_err(hdev, "force feedback initialization failed\n");
            goto err_cleanup_wheel;
        }
        t500rs->data->state = T500RS_STATE_READY;
        t500rs->data->initialized = true;
        break;
    case T500RS_STATE_READY:
        /* Device already initialized */
        t500rs->data->initialized = true;
        break;
    default:
        hid_err(hdev, "invalid state %d\n", t500rs->data->state);
        ret = -EINVAL;
        goto err_stop_hw;
    }

    /* Enable full features now that initialization is complete */
    if (t500rs->data->initialized) {
        ret = hid_hw_open(hdev);
        if (ret) {
            hid_err(hdev, "hw open failed\n");
            goto err_cleanup_wheel;
        }
        /* Enable input and force feedback */
        ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
        if (ret) {
            hid_err(hdev, "hw start with full features failed\n");
            hid_hw_close(hdev);
            goto err_cleanup_wheel;
        }
    }

    return 0;

err_cleanup_wheel:
    t500rs_wheel_destroy(t500rs);
err_cleanup_usb:
    t500rs_cleanup_usb(t500rs);
err_remove_sysfs:
    sysfs_remove_group(&hdev->dev.kobj, &t500rs_attr_group);
err_stop_hw:
    hid_hw_stop(hdev);
err_free_tmff2:
    if (tmff2) {
        if (tmff2->data) {
            if (t500rs->data)
                kfree(t500rs->data);
            kfree(tmff2->data);
        }
        kfree(tmff2);
    }
    return ret;
}

static void t500rs_remove(struct hid_device *hdev)
{
    struct tmff2_device_entry *tmff2 = hid_get_drvdata(hdev);
    struct t500rs_device_entry *t500rs;

    if (!tmff2)
        return;

    /* Remove sysfs attributes */
    sysfs_remove_group(&hdev->dev.kobj, &t500rs_attr_group);

    t500rs = tmff2->data;
    if (t500rs) {
        if (t500rs->data) {
            /* Cleanup mode switch */
            t500rs_cleanup_mode_switch(t500rs);
            
            /* Cleanup USB */
            t500rs_cleanup_usb(t500rs);
            
            /* Free data */
            kfree(t500rs->data);
        }
        kfree(t500rs);
    }
    kfree(tmff2);

    hid_hw_stop(hdev);
}

/* Initialize the T500RS driver */
int t500rs_driver_init(void)
{
    int ret;

    ret = hid_register_driver(&t500rs_driver);
    if (ret) {
        pr_err("t500rs: Failed to register driver\n");
        return ret;
    }

    return 0;
}

/* Cleanup the T500RS driver */
void t500rs_driver_exit(void)
{
    hid_unregister_driver(&t500rs_driver);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Force feedback support for Thrustmaster T500RS - Core");
