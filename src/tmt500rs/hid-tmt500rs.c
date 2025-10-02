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
#include "hid-tmt500rs-simple.h"

/* Debug parameter */
static bool debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Enable debug output for T500RS module");

/* Debug macro */
#define t500rs_dbg(fmt, ...) \
    do { if (debug) dev_dbg(&hdev->dev, fmt, ##__VA_ARGS__); } while (0)

/* Module dependencies */
MODULE_SOFTDEP("pre: hid_tmff_new");

/*
 * T500RS API population - now using simplified implementation
 *
 * This function delegates to the simplified implementation which follows
 * T300RS patterns for reliable device detection and basic force feedback.
 */
int t500rs_populate_api(struct tmff2_device_entry *tmff2)
{
    /* Use simplified implementation */
    return t500rs_simple_populate_api(tmff2);
}

/*
 * Simplified T500RS implementation - all complex initialization,
 * mode switching, and USB handling has been removed.
 *
 * The populate_api function now delegates to the simplified
 * implementation in hid-tmt500rs-simple.c which follows T300RS patterns.
 *
 * This file now only contains the populate_api function which is called
 * by the main tmff2 driver during device initialization.
 */
