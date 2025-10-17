/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Force feedback support for Thrustmaster T500RS - Simplified Implementation
 *
 * This is a simplified implementation following T300RS patterns
 * for reliable device detection and basic force feedback support.
 */

#ifndef __HID_TMT500RS_SIMPLE_H
#define __HID_TMT500RS_SIMPLE_H

#include "../hid-tmff2.h"

/* Maximum effects supported */
#define T500RS_MAX_EFFECTS 16

/* USB Protocol - T500RS uses INTERRUPT transfers, not SET_REPORT! */

/* SAFETY MODE - DISABLED (libusb approach works!) */
#define T500RS_SAFE_MODE 0              /* Safe mode OFF - using raw USB */

/* INCREMENTAL TESTING - All enabled since libusb works */
#define T500RS_ALLOW_STOP_ONLY 1        /* Allow stop commands */
#define T500RS_ALLOW_PARAMS 1           /* Allow parameter upload */
#define T500RS_ALLOW_START 1            /* Allow start commands */

/* Report IDs */
#define T500RS_REPORT_CONTROL 0x41      /* Effect control (start/stop) - 4 bytes */
#define T500RS_REPORT_EFFECT_PARAMS 0x01 /* Effect parameters - 15 bytes */
#define T500RS_REPORT_PARAMS2 0x02      /* Additional parameters - 9 bytes */
#define T500RS_REPORT_PARAMS3 0x04      /* More parameters - 8 bytes */
#define T500RS_REPORT_INIT 0x42         /* Initialization */
#define T500RS_REPORT_CONFIG 0x0a       /* Configuration */

/* Effect control commands (Report 0x41) */
#define T500RS_CMD_START 0x41           /* Start/play effect */
#define T500RS_CMD_STOP 0x00            /* Stop effect */

/* Buffer sizes */
#define T500RS_BUFFER_CONTROL 4         /* Control command size */
#define T500RS_BUFFER_PARAMS 15         /* Effect parameters size */

/* Simplified device entry structure */
struct t500rs_simple_entry {
	struct hid_device *hdev;
	struct input_dev *input_dev;
	struct usb_device *usbdev;

	/* USB endpoint for raw transfers */
	u8 ep_out;                      /* INTERRUPT OUT endpoint (0x01) */

	/* Send buffer for commands */
	u8 *send_buffer;
	size_t buffer_length;

	/* Input device open/close tracking */
	int (*open)(struct input_dev *dev);
	void (*close)(struct input_dev *dev);
};

/* Function declarations */
int t500rs_simple_populate_api(struct tmff2_device_entry *tmff2);

#endif /* __HID_TMT500RS_SIMPLE_H */

