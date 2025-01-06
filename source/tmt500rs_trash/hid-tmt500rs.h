/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __HID_TMT500RS_H
#define __HID_TMT500RS_H

#include <linux/hid.h>
#include "../hid-tmff2.h"

struct t500rs_packet_header {
    u8 cmd;
    u8 id;
    u8 flags;
    u8 type;
    u16 level;
    u16 range;
    u16 gain;
    u16 autocenter;
} __packed;

struct t500rs_device_entry {
    struct hid_device *hdev;
    struct input_dev *input_dev;
    struct usb_device *usbdev;
    struct hid_report *report;
    struct hid_field *ff_field;
    int (*open)(struct input_dev *dev);
    void (*close)(struct input_dev *dev);
    u8 *send_buffer;
    size_t buffer_length;
};

/* Function declarations */
int t500rs_populate_api(struct tmff2_device_entry *tmff2);

#endif /* __HID_TMT500RS_H */
