// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Force feedback support for Thrustmaster T500RS
 * 
 * USB INTERRUPT implementation based on working userspace driver
 * Uses endpoint 0x01 OUT for all communication
 */

#include <linux/hid.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include "../hid-tmff2.h"

/* T500RS Constants */
#define T500RS_MAX_EFFECTS 16
#define T500RS_BUFFER_LENGTH 32  /* USB endpoint max packet size */
#define T500RS_EP_OUT 0x01       /* INTERRUPT OUT endpoint */

/* USB timeout */
#define T500RS_USB_TIMEOUT 1000  /* 1 second */

/* Gain scaling */
#define GAIN_MAX 65535

/* Work item for USB transfers */
struct t500rs_work_item {
	struct work_struct work;
	struct t500rs_device_entry *t500rs;
	u8 data[32];  /* Max USB packet size */
	size_t len;
	int result;  /* Result of USB transfer */
	struct completion done;  /* Completion signal */
};

/* T500RS device data */
struct t500rs_device_entry {
	struct hid_device *hdev;
	struct input_dev *input_dev;
	struct usb_device *usbdev;
	struct usb_interface *usbif;

	int ep_out;  /* INTERRUPT OUT endpoint address */

	u8 *send_buffer;
	size_t buffer_length;

	struct workqueue_struct *wq;  /* Work queue for USB transfers */

	/* Force update timer (T500RS needs continuous force streaming!) */
	struct timer_list force_timer;
	struct work_struct force_work;
	spinlock_t force_lock;
	s8 current_force_level;  /* Current force level (-127 to 127) */
	bool force_timer_active;
	bool device_active;  /* Set to false during destruction to prevent new work */

	int (*open)(struct input_dev *dev);
	void (*close)(struct input_dev *dev);
};

/* Supported parameters */
static const unsigned long t500rs_params =
	PARAM_SPRING_LEVEL
	| PARAM_DAMPER_LEVEL
	| PARAM_FRICTION_LEVEL
	| PARAM_GAIN
	| PARAM_RANGE
	;

/* Supported effects */
static const signed short t500rs_effects[] = {
	FF_CONSTANT,
	FF_SPRING,
	FF_DAMPER,
	FF_FRICTION,
	FF_INERTIA,
	FF_PERIODIC,
	FF_RAMP,
	FF_GAIN,
	-1
};

/* Work queue handler - sends USB INTERRUPT transfer (can sleep) */
static void t500rs_work_handler(struct work_struct *work)
{
	struct t500rs_work_item *item = container_of(work, struct t500rs_work_item, work);
	struct t500rs_device_entry *t500rs = item->t500rs;
	int ret, transferred;

	/* CRITICAL: Check if device is still valid before accessing */
	if (!t500rs || !t500rs->usbdev || !t500rs->hdev) {
		pr_warn("t500rs_work_handler: Device no longer valid, aborting transfer\n");
		item->result = -ENODEV;
		complete(&item->done);
		kfree(item);
		return;
	}

	/* DEBUG: Log what we're sending */
	hid_info(t500rs->hdev, "USB TX [%zu]: %02x %02x %02x %02x %02x %02x %02x %02x\n",
		 item->len,
		 item->len > 0 ? item->data[0] : 0,
		 item->len > 1 ? item->data[1] : 0,
		 item->len > 2 ? item->data[2] : 0,
		 item->len > 3 ? item->data[3] : 0,
		 item->len > 4 ? item->data[4] : 0,
		 item->len > 5 ? item->data[5] : 0,
		 item->len > 6 ? item->data[6] : 0,
		 item->len > 7 ? item->data[7] : 0);

	/* Send via USB INTERRUPT (blocking, but we're in work queue so it's OK) */
	ret = usb_interrupt_msg(t500rs->usbdev,
				usb_sndintpipe(t500rs->usbdev, t500rs->ep_out),
				item->data, item->len,
				&transferred,
				T500RS_USB_TIMEOUT);

	if (ret < 0) {
		hid_err(t500rs->hdev, "USB transfer FAILED: ret=%d, transferred=%d/%zu\n",
			ret, transferred, item->len);
	} else if (transferred != item->len) {
		hid_warn(t500rs->hdev, "USB transfer incomplete: %d/%zu bytes\n",
			 transferred, item->len);
	} else {
		hid_info(t500rs->hdev, "USB transfer OK: %d bytes\n", transferred);
	}

	/* Store result and signal completion */
	item->result = ret;
	complete(&item->done);

	/* Free work item (async mode - caller doesn't wait) */
	kfree(item);
}

/* Forward declaration */
static int t500rs_send_usb(struct t500rs_device_entry *t500rs, const u8 *data, size_t len);

/* Force update work handler - sends Report 0x03 with current force level */
static void t500rs_force_update_work(struct work_struct *work)
{
	struct t500rs_device_entry *t500rs = container_of(work, struct t500rs_device_entry, force_work);
	u8 buf[4];
	unsigned long flags;
	s8 level;

	/* CRITICAL: Check if device is still valid */
	if (!t500rs || !t500rs->usbdev || !t500rs->hdev) {
		pr_warn("t500rs_force_update_work: Device no longer valid, stopping\n");
		return;
	}

	/* Get current force level */
	spin_lock_irqsave(&t500rs->force_lock, flags);
	level = t500rs->current_force_level;
	spin_unlock_irqrestore(&t500rs->force_lock, flags);

	/* Send Report 0x03 - Force level */
	buf[0] = 0x03;
	buf[1] = 0x0e;
	buf[2] = 0x00;
	buf[3] = (u8)level;

	hid_info(t500rs->hdev, "Force update: sending level=%d (0x%02x)\n", level, buf[3]);
	t500rs_send_usb(t500rs, buf, 4);
}

/* Force update timer callback - schedules work to send force update */
static void t500rs_force_timer_callback(struct timer_list *t)
{
	struct t500rs_device_entry *t500rs = from_timer(t500rs, t, force_timer);
	unsigned long flags;
	bool active;

	if (!t500rs)
		return;

	/* Check if timer should continue */
	spin_lock_irqsave(&t500rs->force_lock, flags);
	active = t500rs->force_timer_active;
	spin_unlock_irqrestore(&t500rs->force_lock, flags);

	if (active) {
		/* Schedule work to send force update */
		queue_work(t500rs->wq, &t500rs->force_work);

		/* Re-arm timer for next update (20ms = 50Hz) */
		mod_timer(&t500rs->force_timer, jiffies + msecs_to_jiffies(20));
	}
}

/* Start continuous force updates */
static void t500rs_start_force_timer(struct t500rs_device_entry *t500rs, s8 force_level)
{
	unsigned long flags;

	if (!t500rs)
		return;

	hid_info(t500rs->hdev, "Starting continuous force updates at 50Hz, level=%d\n", force_level);

	spin_lock_irqsave(&t500rs->force_lock, flags);
	t500rs->current_force_level = force_level;
	t500rs->force_timer_active = true;
	spin_unlock_irqrestore(&t500rs->force_lock, flags);

	/* Start timer (20ms = 50Hz) */
	mod_timer(&t500rs->force_timer, jiffies + msecs_to_jiffies(20));
}

/* Stop continuous force updates */
static void t500rs_stop_force_timer(struct t500rs_device_entry *t500rs)
{
	unsigned long flags;

	if (!t500rs)
		return;

	hid_info(t500rs->hdev, "Stopping continuous force updates\n");

	spin_lock_irqsave(&t500rs->force_lock, flags);
	t500rs->force_timer_active = false;
	t500rs->current_force_level = 0;
	spin_unlock_irqrestore(&t500rs->force_lock, flags);

	del_timer_sync(&t500rs->force_timer);
	cancel_work_sync(&t500rs->force_work);
}

/* Update force level (called from play_effect) */
static void t500rs_update_force_level(struct t500rs_device_entry *t500rs, s8 force_level)
{
	unsigned long flags;

	if (!t500rs)
		return;

	spin_lock_irqsave(&t500rs->force_lock, flags);
	t500rs->current_force_level = force_level;
	spin_unlock_irqrestore(&t500rs->force_lock, flags);
}

/* Send data via USB INTERRUPT transfer (queues work, safe from atomic context) */
static int t500rs_send_usb(struct t500rs_device_entry *t500rs, const u8 *data, size_t len)
{
	struct t500rs_work_item *item;

	if (!t500rs || !data || len == 0 || len > 32) {
		return -EINVAL;
	}

	/* CRITICAL: Don't queue new work if device is being destroyed */
	if (!t500rs->device_active) {
		pr_warn("t500rs_send_usb: Device not active, rejecting transfer\n");
		return -ENODEV;
	}

	/* Allocate work item */
	item = kzalloc(sizeof(*item), GFP_ATOMIC);
	if (!item) {
		hid_err(t500rs->hdev, "Failed to allocate work item\n");
		return -ENOMEM;
	}

	/* Initialize work item */
	INIT_WORK(&item->work, t500rs_work_handler);
	init_completion(&item->done);
	item->t500rs = t500rs;
	memcpy(item->data, data, len);
	item->len = len;
	item->result = 0;

	/* Queue work - don't wait, return immediately (async) */
	queue_work(t500rs->wq, &item->work);

	/* Return success immediately - actual USB transfer happens asynchronously */
	return 0;
}

/* Send data via USB INTERRUPT transfer (blocking, for initialization only) */
static int t500rs_send_usb_blocking(struct t500rs_device_entry *t500rs, const u8 *data, size_t len)
{
	int ret, transferred;

	if (!t500rs || !data || len == 0 || len > T500RS_BUFFER_LENGTH) {
		return -EINVAL;
	}

	/* DEBUG: Log what we're sending */
	hid_info(t500rs->hdev, "INIT USB TX [%zu]: %02x %02x %02x %02x %02x %02x %02x %02x\n",
		 len,
		 len > 0 ? data[0] : 0,
		 len > 1 ? data[1] : 0,
		 len > 2 ? data[2] : 0,
		 len > 3 ? data[3] : 0,
		 len > 4 ? data[4] : 0,
		 len > 5 ? data[5] : 0,
		 len > 6 ? data[6] : 0,
		 len > 7 ? data[7] : 0);

	ret = usb_interrupt_msg(t500rs->usbdev,
				usb_sndintpipe(t500rs->usbdev, t500rs->ep_out),
				(void *)data, len,
				&transferred,
				T500RS_USB_TIMEOUT);

	if (ret < 0) {
		hid_err(t500rs->hdev, "USB INTERRUPT transfer failed: %d\n", ret);
		return ret;
	}

	if (transferred != len) {
		hid_err(t500rs->hdev, "USB transfer incomplete: %d/%zu bytes\n", transferred, len);
		return -EIO;
	}

	hid_info(t500rs->hdev, "INIT USB transfer OK: %d bytes\n", transferred);
	return 0;
}

/* Upload constant force effect */
static int t500rs_upload_constant(struct t500rs_device_entry *t500rs,
				   struct tmff2_effect_state *state)
{
	struct ff_effect *effect = &state->effect;
	u8 *buf = t500rs->send_buffer;  /* Use DMA-safe buffer */
	int ret;
	int level = effect->u.constant.level;

	/* Note: Gain is applied in play_effect, not here */

	hid_info(t500rs->hdev, "Upload constant: id=%d, level=%d\n",
		 effect->id, level);

	/* SIMPLE SEQUENCE - Match working userspace driver! */
	/* Report 0x02 - Envelope (attack/fade) */
	memset(buf, 0, 15);
	buf[0] = 0x02;
	buf[1] = 0x1c;  /* Subtype 0x1c */
	buf[2] = 0x00;
	hid_info(t500rs->hdev, "Sending Report 0x02 (envelope)...\n");
	ret = t500rs_send_usb(t500rs, buf, 9);
	if (ret) {
		hid_err(t500rs->hdev, "Failed to send Report 0x02: %d\n", ret);
		return ret;
	}

	/* Report 0x01 - Main effect upload */
	memset(buf, 0, 15);
	buf[0] = 0x01;
	buf[1] = effect->id;
	buf[2] = 0x00;  /* Constant force type */
	buf[3] = 0x40;
	buf[4] = 0x69;
	buf[5] = 0x23;
	buf[6] = 0x00;
	buf[7] = 0xff;
	buf[8] = 0xff;
	buf[9] = 0x0e;   /* Parameter subtype reference */
	buf[10] = 0x00;
	buf[11] = 0x1c;  /* Envelope subtype reference */
	buf[12] = 0x00;
	buf[13] = 0x00;
	buf[14] = 0x00;
	hid_info(t500rs->hdev, "Sending Report 0x01 (duration/control)...\n");
	ret = t500rs_send_usb(t500rs, buf, 15);
	if (ret) {
		hid_err(t500rs->hdev, "Failed to send Report 0x01: %d\n", ret);
		return ret;
	}

	hid_info(t500rs->hdev, "✅ Constant effect %d uploaded (simple sequence)\n", effect->id);
	return 0;
}

/* Upload spring/damper/friction effect */
static int t500rs_upload_condition(struct t500rs_device_entry *t500rs,
				    struct tmff2_effect_state *state)
{
	struct ff_effect *effect = &state->effect;
	u8 *buf = t500rs->send_buffer;  /* Use DMA-safe buffer */
	u8 effect_type;
	int ret;

	/* Determine effect type */
	switch (effect->type) {
	case FF_SPRING:
		effect_type = 0x40;
		break;
	case FF_DAMPER:
	case FF_FRICTION:
	case FF_INERTIA:
		effect_type = 0x41;
		break;
	default:
		return -EINVAL;
	}

	hid_info(t500rs->hdev, "Upload condition: id=%d, type=0x%02x\n",
		 effect->id, effect_type);

	/* Report 0x05 - Condition parameters (coefficients) */
	memset(buf, 0, 15);
	buf[0] = 0x05;
	buf[1] = 0x0e;
	buf[2] = 0x00;
	buf[3] = 50;  /* Right strength - using default */
	buf[4] = 50;  /* Left strength - using default */
	buf[5] = 0x00;
	buf[6] = 0x00;
	buf[7] = 0x00;
	buf[8] = 0x00;
	buf[9] = (effect->type == FF_SPRING) ? 0x54 : 0x64;
	buf[10] = (effect->type == FF_SPRING) ? 0x54 : 0x64;
	ret = t500rs_send_usb(t500rs, buf, 11);
	if (ret) return ret;

	/* Report 0x05 - Condition parameters (deadband/center) */
	memset(buf, 0, 15);
	buf[0] = 0x05;
	buf[1] = 0x1c;
	buf[2] = 0x00;
	buf[3] = 0x00;  /* Deadband */
	buf[4] = 0x00;  /* Center */
	buf[5] = 0x00;
	buf[6] = 0x00;
	buf[7] = 0x00;
	buf[8] = 0x00;
	buf[9] = (effect->type == FF_SPRING) ? 0x46 : 0x64;
	buf[10] = (effect->type == FF_SPRING) ? 0x54 : 0x64;
	ret = t500rs_send_usb(t500rs, buf, 11);
	if (ret) return ret;

	/* Report 0x01 - Main effect upload */
	memset(buf, 0, 15);
	buf[0] = 0x01;
	buf[1] = effect->id;
	buf[2] = effect_type;
	buf[3] = 0x40;
	buf[4] = 0x17;
	buf[5] = 0x25;
	buf[6] = 0x00;
	buf[7] = 0xff;
	buf[8] = 0xff;
	buf[9] = 0x0e;
	buf[10] = 0x00;
	buf[11] = 0x1c;
	buf[12] = 0x00;
	buf[13] = 0x00;
	buf[14] = 0x00;
	ret = t500rs_send_usb(t500rs, buf, 15);
	if (ret) return ret;
	
	return 0;
}

/* Upload periodic effect (sine, square, triangle, saw) */
static int t500rs_upload_periodic(struct t500rs_device_entry *t500rs,
				   struct tmff2_effect_state *state)
{
	struct ff_effect *effect = &state->effect;
	u8 *buf = t500rs->send_buffer;  /* Use DMA-safe buffer */
	int ret;
	u8 effect_type;
	const char *type_name;
	int magnitude = effect->u.periodic.magnitude;
	u16 period = effect->u.periodic.period;
	u8 mag;

	/* Apply global gain */
	extern int gain;
	magnitude = (magnitude * gain) / 65535;

	/* Determine waveform type */
	switch (effect->u.periodic.waveform) {
	case FF_SQUARE:
		effect_type = 0x20;
		type_name = "square";
		break;
	case FF_TRIANGLE:
		effect_type = 0x21;
		type_name = "triangle";
		break;
	case FF_SINE:
		effect_type = 0x22;
		type_name = "sine";
		break;
	case FF_SAW_UP:
		effect_type = 0x23;
		type_name = "sawtooth_up";
		break;
	case FF_SAW_DOWN:
		effect_type = 0x24;
		type_name = "sawtooth_down";
		break;
	default:
		hid_err(t500rs->hdev, "Unknown periodic waveform: %d\n",
			effect->u.periodic.waveform);
		return -EINVAL;
	}

	/* Magnitude - scale to 0-127 */
	mag = (abs(magnitude) * 127) / 32767;

	/* Ensure minimum magnitude */
	if (mag < 20) {
		mag = 50;  /* Default to medium if too low */
	}

	/* Period (frequency) - default to 100ms = 10 Hz if not set */
	if (period == 0) {
		period = 100;
	}

	hid_info(t500rs->hdev, "Upload %s: id=%d, magnitude=%d (0x%02x), period=%dms\n",
		 type_name, effect->id, magnitude, mag, period);

	/* Report 0x02 - Envelope */
	memset(buf, 0, 15);
	buf[0] = 0x02;
	buf[1] = 0x1c;
	buf[2] = 0x00;
	ret = t500rs_send_usb(t500rs, buf, 9);
	if (ret) {
		hid_err(t500rs->hdev, "Failed to send Report 0x02: %d\n", ret);
		return ret;
	}

	/* Report 0x04 - Periodic parameters */
	memset(buf, 0, 15);
	buf[0] = 0x04;
	buf[1] = 0x0e;
	buf[2] = 0x00;
	buf[3] = mag;  /* Magnitude */
	buf[4] = 0x00;  /* Offset */
	buf[5] = 0x00;  /* Phase */
	buf[6] = period & 0xff;  /* Period low byte */
	buf[7] = (period >> 8) & 0xff;  /* Period high byte */
	ret = t500rs_send_usb(t500rs, buf, 8);
	if (ret) {
		hid_err(t500rs->hdev, "Failed to send Report 0x04: %d\n", ret);
		return ret;
	}

	/* Report 0x01 - Main effect upload */
	memset(buf, 0, 15);
	buf[0] = 0x01;
	buf[1] = effect->id;
	buf[2] = effect_type;  /* Waveform type */
	buf[3] = 0x40;
	buf[4] = 0x17;
	buf[5] = 0x25;
	buf[6] = 0x00;
	buf[7] = 0xff;
	buf[8] = 0xff;
	buf[9] = 0x0e;
	buf[10] = 0x00;
	buf[11] = 0x1c;
	buf[12] = 0x00;
	buf[13] = 0x00;
	buf[14] = 0x00;
	ret = t500rs_send_usb(t500rs, buf, 15);
	if (ret) {
		hid_err(t500rs->hdev, "Failed to send Report 0x01: %d\n", ret);
		return ret;
	}

	hid_info(t500rs->hdev, "✅ %s effect %d uploaded\n", type_name, effect->id);
	return 0;
}

/* Upload ramp effect */
static int t500rs_upload_ramp(struct t500rs_device_entry *t500rs,
			       struct tmff2_effect_state *state)
{
	struct ff_effect *effect = &state->effect;
	u8 *buf = t500rs->send_buffer;  /* Use DMA-safe buffer */
	int ret;
	int start_level = effect->u.ramp.start_level;
	int end_level = effect->u.ramp.end_level;
	u16 duration_ms = effect->replay.length;
	u16 start_scaled;

	/* Apply global gain */
	extern int gain;
	start_level = (start_level * gain) / 65535;

	/* Scale to 0-255 */
	start_scaled = (abs(start_level) * 0xff) / 32767;

	hid_info(t500rs->hdev, "Upload ramp: id=%d, start=%d, end=%d, duration=%dms\n",
		 effect->id, start_level, end_level, duration_ms);

	/* Report 0x02 - Envelope */
	memset(buf, 0, 15);
	buf[0] = 0x02;
	buf[1] = 0x1c;
	buf[2] = 0x00;
	ret = t500rs_send_usb(t500rs, buf, 9);
	if (ret) {
		hid_err(t500rs->hdev, "Failed to send Report 0x02: %d\n", ret);
		return ret;
	}

	/* Report 0x04 - Ramp parameters */
	/* NOTE: T500RS doesn't support native ramp - just holds start level */
	memset(buf, 0, 15);
	buf[0] = 0x04;
	buf[1] = 0x0e;
	buf[2] = start_scaled & 0xff;        /* Start level low byte */
	buf[3] = (start_scaled >> 8) & 0xff; /* Start level high byte */
	buf[4] = start_scaled & 0xff;        /* Current level (same as start) */
	buf[5] = (start_scaled >> 8) & 0xff; /* Current level high byte */
	buf[6] = duration_ms & 0xff;         /* Duration low byte */
	buf[7] = (duration_ms >> 8) & 0xff;  /* Duration high byte */
	buf[8] = 0x00;
	ret = t500rs_send_usb(t500rs, buf, 9);
	if (ret) {
		hid_err(t500rs->hdev, "Failed to send Report 0x04: %d\n", ret);
		return ret;
	}

	/* Report 0x01 - Main effect upload */
	memset(buf, 0, 15);
	buf[0] = 0x01;
	buf[1] = effect->id;
	buf[2] = 0x24;  /* Ramp type (0x24 = sawtooth down / ramp) */
	buf[3] = 0x40;
	buf[4] = duration_ms & 0xff;         /* Duration low byte */
	buf[5] = (duration_ms >> 8) & 0xff;  /* Duration high byte */
	buf[6] = 0x00;
	buf[7] = 0xff;
	buf[8] = 0xff;
	buf[9] = 0x0e;
	buf[10] = 0x00;
	buf[11] = 0x1c;
	buf[12] = 0x00;
	buf[13] = 0x00;
	buf[14] = 0x00;
	ret = t500rs_send_usb(t500rs, buf, 15);
	if (ret) {
		hid_err(t500rs->hdev, "Failed to send Report 0x01: %d\n", ret);
		return ret;
	}

	hid_info(t500rs->hdev, "✅ Ramp effect %d uploaded (simple mode)\n", effect->id);
	return 0;
}

/* Upload effect */
int t500rs_upload_effect(void *data, struct tmff2_effect_state *state)
{
	struct t500rs_device_entry *t500rs = data;
	struct ff_effect *effect = &state->effect;
	
	if (!t500rs)
		return -ENODEV;
	
	switch (effect->type) {
	case FF_CONSTANT:
		return t500rs_upload_constant(t500rs, state);
	case FF_SPRING:
	case FF_DAMPER:
	case FF_FRICTION:
	case FF_INERTIA:
		return t500rs_upload_condition(t500rs, state);
	case FF_PERIODIC:
		return t500rs_upload_periodic(t500rs, state);
	case FF_RAMP:
		return t500rs_upload_ramp(t500rs, state);
	default:
		return -EINVAL;
	}
}

/* Play effect */
int t500rs_play_effect(void *data, struct tmff2_effect_state *state)
{
	struct t500rs_device_entry *t500rs = data;
	struct ff_effect *effect = &state->effect;
	u8 *buf = t500rs->send_buffer;  /* Use DMA-safe buffer */
	int ret;

	if (!t500rs)
		return -ENODEV;

	hid_info(t500rs->hdev, "Play effect: id=%d, type=0x%02x (FF_CONSTANT=0x%02x)\n",
		 effect->id, effect->type, FF_CONSTANT);

	/* NOTE: Condition effect disable code removed - testing Hypothesis 6 */
	/* Keeping it simple to match userspace driver more closely */

	/* For constant force, start continuous force updates */
	if (effect->type == FF_CONSTANT) {
		int level = effect->u.constant.level;
		s8 signed_level;

		/* Apply global gain (from hid-tmff2.c) */
		extern int gain;
		level = (level * gain) / 65535;

		hid_info(t500rs->hdev, "Constant force: level=%d (with gain=%d/%d)\n", level, gain, 65535);

		/* Scale to -127..127 (signed 8-bit range) */
		/* Note: level is signed 16-bit (-32767 to 32767) */
		signed_level = (s8)((level * 127) / 32767);

		hid_info(t500rs->hdev, "Scaled force level: %d -> %d (0x%02x)\n",
			 level, signed_level, (u8)signed_level);

		/* CRITICAL: Send Report 0x03 BEFORE Report 0x41! */
		/* This matches the working userspace driver sequence */
		buf[0] = 0x03;
		buf[1] = 0x0e;
		buf[2] = 0x00;
		buf[3] = (u8)signed_level;

		hid_info(t500rs->hdev, "Sending Report 0x03 (force level): level=%d (0x%02x)\n",
			 signed_level, (u8)signed_level);
		ret = t500rs_send_usb(t500rs, buf, 4);
		if (ret) {
			hid_err(t500rs->hdev, "Failed to send Report 0x03: %d\n", ret);
			return ret;
		}

		/* Now send Report 0x41 START command */
		/* This activates the effect with the force level already set */
		buf[0] = 0x41;
		buf[1] = effect->id;  /* Use actual effect ID */
		buf[2] = 0x41;  /* START command */
		buf[3] = 0x01;

		hid_info(t500rs->hdev, "Sending Report 0x41 (START) for effect ID %d\n", effect->id);
		ret = t500rs_send_usb(t500rs, buf, 4);
		if (ret) {
			hid_err(t500rs->hdev, "Failed to send START command: %d\n", ret);
			return ret;
		}

		/* T500RS requires CONTINUOUS force updates at 50Hz! */
		/* Start the force update timer which will send Report 0x03 every 20ms */
		t500rs_start_force_timer(t500rs, signed_level);

		return 0;
	}

	/* For other effect types, send start command - Report 0x41 */
	buf[0] = 0x41;
	buf[1] = effect->id;
	buf[2] = 0x41;  /* START command */
	buf[3] = 0x01;

	hid_info(t500rs->hdev, "Sending START command for effect %d\n", effect->id);
	return t500rs_send_usb(t500rs, buf, 4);
}

/* Stop effect */
int t500rs_stop_effect(void *data, struct tmff2_effect_state *state)
{
	struct t500rs_device_entry *t500rs = data;
	u8 *buf;
	int ret;

	if (!t500rs) {
		pr_err("t500rs_stop_effect: t500rs is NULL!\n");
		return -ENODEV;
	}

	buf = t500rs->send_buffer;  /* Use DMA-safe buffer */
	if (!buf) {
		hid_err(t500rs->hdev, "Stop effect: send_buffer is NULL!\n");
		return -ENOMEM;
	}

	hid_info(t500rs->hdev, "Stop effect: id=%d, type=%d\n", state->effect.id, state->effect.type);

	/* For constant force, stop the continuous force timer */
	if (state->effect.type == FF_CONSTANT) {
		t500rs_stop_force_timer(t500rs);
		return 0;
	}

	/* For other effect types, send stop command - Report 0x41 */
	buf[0] = 0x41;
	buf[1] = state->effect.id;
	buf[2] = 0x00;  /* STOP command */
	buf[3] = 0x01;

	ret = t500rs_send_usb(t500rs, buf, 4);
	hid_info(t500rs->hdev, "Stop effect returned: %d\n", ret);
	return ret;
}

/* Update effect - just re-upload */
int t500rs_update_effect(void *data, struct tmff2_effect_state *state)
{
	return t500rs_upload_effect(data, state);
}

/* Set gain */
int t500rs_set_gain(void *data, u16 gain)
{
	struct t500rs_device_entry *t500rs = data;

	if (!t500rs)
		return -ENODEV;

	hid_info(t500rs->hdev, "Set gain: %u (%u%%)\n",
		 gain, (gain * 100) / 65535);

	/* Gain is applied per-effect in upload/play functions */
	return 0;
}

/* Set autocenter */
int t500rs_set_autocenter(void *data, u16 autocenter)
{
	struct t500rs_device_entry *t500rs = data;
	u8 *buf = t500rs->send_buffer;  /* Use DMA-safe buffer */

	if (!t500rs)
		return -ENODEV;

	hid_info(t500rs->hdev, "Set autocenter: %u%%\n",
		 (autocenter * 100) / 65535);

	if (autocenter == 0) {
		/* Stop autocenter spring effect (ID 15) */
		buf[0] = 0x41;
		buf[1] = 15;  /* Reserved autocenter effect ID */
		buf[2] = 0x00;  /* STOP */
		buf[3] = 0x01;
		return t500rs_send_usb(t500rs, buf, 4);
	}

	/* TODO: Upload and start spring effect for autocenter */
	return 0;
}

/* Initialize T500RS device */
int t500rs_wheel_init(struct tmff2_device_entry *tmff2, int open_mode)
{
	struct t500rs_device_entry *t500rs;
	struct usb_host_endpoint *ep;
	u8 *init_buf;  /* Will use send_buffer for DMA-safe transfers */
	int ret;

	/* Validate input parameters */
	if (!tmff2 || !tmff2->hdev || !tmff2->input_dev) {
		pr_err("t500rs: Invalid tmff2 structure\n");
		return -EINVAL;
	}

	hid_info(tmff2->hdev, "T500RS: Initializing USB INTERRUPT mode\n");

	/* Allocate device data */
	t500rs = kzalloc(sizeof(*t500rs), GFP_KERNEL);
	if (!t500rs) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	t500rs->hdev = tmff2->hdev;
	t500rs->input_dev = tmff2->input_dev;

	/* Get USB device */
	if (!t500rs->hdev->dev.parent) {
		hid_err(t500rs->hdev, "No parent device\n");
		ret = -ENODEV;
		goto err_endpoint;
	}

	t500rs->usbif = to_usb_interface(t500rs->hdev->dev.parent);
	if (!t500rs->usbif) {
		hid_err(t500rs->hdev, "Failed to get USB interface\n");
		ret = -ENODEV;
		goto err_endpoint;
	}

	t500rs->usbdev = interface_to_usbdev(t500rs->usbif);
	if (!t500rs->usbdev) {
		hid_err(t500rs->hdev, "Failed to get USB device\n");
		ret = -ENODEV;
		goto err_endpoint;
	}

	/* Find INTERRUPT OUT endpoint (should be endpoint 1) */
	if (t500rs->usbif->cur_altsetting->desc.bNumEndpoints < 2) {
		hid_err(t500rs->hdev, "Not enough USB endpoints\n");
		ret = -ENODEV;
		goto err_endpoint;
	}

	ep = &t500rs->usbif->cur_altsetting->endpoint[1];
	t500rs->ep_out = ep->desc.bEndpointAddress;

	hid_info(t500rs->hdev, "Found INTERRUPT OUT endpoint: 0x%02x\n", t500rs->ep_out);

	/* Allocate send buffer */
	t500rs->buffer_length = T500RS_BUFFER_LENGTH;
	t500rs->send_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
	if (!t500rs->send_buffer) {
		ret = -ENOMEM;
		goto err_buffer;
	}

	/* Create work queue for USB transfers (avoids atomic context issues) */
	t500rs->wq = create_singlethread_workqueue("t500rs_wq");
	if (!t500rs->wq) {
		hid_err(t500rs->hdev, "Failed to create work queue\n");
		ret = -ENOMEM;
		goto err_wq;
	}

	/* Initialize force update timer and work (T500RS needs continuous force streaming!) */
	spin_lock_init(&t500rs->force_lock);
	t500rs->current_force_level = 0;
	t500rs->force_timer_active = false;
	t500rs->device_active = true;  /* Device is now active */
	timer_setup(&t500rs->force_timer, t500rs_force_timer_callback, 0);
	INIT_WORK(&t500rs->force_work, t500rs_force_update_work);

	/* Store device data in tmff2 */
	tmff2->data = t500rs;

	/* Use send_buffer for all USB transfers (DMA-safe) */
	init_buf = t500rs->send_buffer;

	/* Send initialization sequence (from userspace driver) */
	hid_info(t500rs->hdev, "Sending initialization sequence...\n");

	/* Report 0x42 - Init (15 bytes) */
	memset(init_buf, 0, 15);
	init_buf[0] = 0x42;
	init_buf[1] = 0x01;
	ret = t500rs_send_usb_blocking(t500rs, init_buf, 15);
	if (ret) {
		hid_warn(t500rs->hdev, "Init command 1 (0x42) failed: %d\n", ret);
	}
	usleep_range(40000, 41000);

	/* Report 0x0a - Config 1 (15 bytes) */
	memset(init_buf, 0, 15);
	init_buf[0] = 0x0a;
	init_buf[1] = 0x04;
	init_buf[2] = 0x90;
	init_buf[3] = 0x03;
	ret = t500rs_send_usb_blocking(t500rs, init_buf, 15);
	if (ret) {
		hid_warn(t500rs->hdev, "Init command 2 (0x0a config1) failed: %d\n", ret);
	}
	usleep_range(4000, 5000);

	/* Report 0x0a - Config 2 (15 bytes) */
	memset(init_buf, 0, 15);
	init_buf[0] = 0x0a;
	init_buf[1] = 0x04;
	init_buf[2] = 0x12;
	init_buf[3] = 0x10;
	ret = t500rs_send_usb_blocking(t500rs, init_buf, 15);
	if (ret) {
		hid_warn(t500rs->hdev, "Init command 3 (0x0a config2) failed: %d\n", ret);
	}
	usleep_range(4000, 5000);

	/* Report 0x0a - Config 3 (15 bytes) */
	memset(init_buf, 0, 15);
	init_buf[0] = 0x0a;
	init_buf[1] = 0x04;
	init_buf[2] = 0x00;
	init_buf[3] = 0x06;
	ret = t500rs_send_usb_blocking(t500rs, init_buf, 15);
	if (ret) {
		hid_warn(t500rs->hdev, "Init command 4 (0x0a config3) failed: %d\n", ret);
	}
	usleep_range(64000, 65000);

	/* Report 0x40 - Enable FFB (4 bytes) */
	/* CRITICAL FIX: Use Windows parameters 42 7b instead of 55 d5 */
	memset(init_buf, 0, 4);
	init_buf[0] = 0x40;
	init_buf[1] = 0x11;
	init_buf[2] = 0x42;  /* Changed from 0x55 to match Windows! */
	init_buf[3] = 0x7b;  /* Changed from 0xd5 to match Windows! */
	ret = t500rs_send_usb_blocking(t500rs, init_buf, 4);
	if (ret) {
		hid_warn(t500rs->hdev, "Init command 5 (0x40 enable) failed: %d\n", ret);
	}
	usleep_range(10000, 11000);

	/* Report 0x42 - Additional init (2 bytes) */
	memset(init_buf, 0, 4);
	init_buf[0] = 0x42;
	init_buf[1] = 0x04;
	ret = t500rs_send_usb_blocking(t500rs, init_buf, 2);
	if (ret) {
		hid_warn(t500rs->hdev, "Init command 6 (0x42) failed: %d\n", ret);
	}
	usleep_range(8000, 9000);

	/* Report 0x40 - Config (4 bytes) */
	memset(init_buf, 0, 4);
	init_buf[0] = 0x40;
	init_buf[1] = 0x04;
	init_buf[2] = 0x00;
	init_buf[3] = 0x00;
	ret = t500rs_send_usb_blocking(t500rs, init_buf, 4);
	if (ret) {
		hid_warn(t500rs->hdev, "Init command 7 (0x40 config) failed: %d\n", ret);
	}
	usleep_range(8000, 9000);

	/* Report 0x43 - Set global gain (2 bytes) */
	/* CRITICAL FIX: Set gain to maximum (0xFF = 100%), not 0x00! */
	memset(init_buf, 0, 4);
	init_buf[0] = 0x43;
	init_buf[1] = 0xFF;  /* Maximum gain - was 0x00 which DISABLED all forces! */
	ret = t500rs_send_usb_blocking(t500rs, init_buf, 2);
	if (ret) {
		hid_warn(t500rs->hdev, "Init command 8 (0x43) failed: %d\n", ret);
	}
	usleep_range(8000, 9000);

	/* Report 0x41 - Clear effects (4 bytes) */
	memset(init_buf, 0, 4);
	init_buf[0] = 0x41;
	init_buf[1] = 0x00;
	init_buf[2] = 0x00;
	init_buf[3] = 0x00;
	ret = t500rs_send_usb_blocking(t500rs, init_buf, 4);
	if (ret) {
		hid_warn(t500rs->hdev, "Init command 9 (0x41 clear) failed: %d\n", ret);
	}
	usleep_range(8000, 9000);

	/* Report 0x40 - Final setup (4 bytes) */
	memset(init_buf, 0, 4);
	init_buf[0] = 0x40;
	init_buf[1] = 0x08;
	init_buf[2] = 0x00;
	init_buf[3] = 0x00;
	ret = t500rs_send_usb_blocking(t500rs, init_buf, 4);
	if (ret) {
		hid_warn(t500rs->hdev, "Init command 10 (0x40 final) failed: %d\n", ret);
	}
	usleep_range(8000, 9000);

	/* Report 0x40 - Set mode (4 bytes) */
	memset(init_buf, 0, 4);
	init_buf[0] = 0x40;
	init_buf[1] = 0x03;
	init_buf[2] = 0x0d;
	init_buf[3] = 0x00;
	ret = t500rs_send_usb_blocking(t500rs, init_buf, 4);
	if (ret) {
		hid_warn(t500rs->hdev, "Init command 11 (0x40 mode) failed: %d\n", ret);
	}
	usleep_range(8000, 9000);

	/* Disable autocenter spring properly (like userspace driver) */
	/* Report 0x05 - Set spring coefficients to 0 */
	memset(init_buf, 0, 15);
	init_buf[0] = 0x05;
	init_buf[1] = 0x0e;
	init_buf[2] = 0x00;
	init_buf[3] = 0x00;  /* Right coefficient = 0 */
	init_buf[4] = 0x00;  /* Left coefficient = 0 */
	init_buf[9] = 0x00;  /* Right saturation = 0 */
	init_buf[10] = 0x00; /* Left saturation = 0 */
	ret = t500rs_send_usb_blocking(t500rs, init_buf, 11);
	if (ret) {
		hid_warn(t500rs->hdev, "Disable autocenter (0x05 0x0e) failed: %d\n", ret);
	}
	usleep_range(5000, 6000);

	/* Report 0x05 - Set deadband and center */
	memset(init_buf, 0, 15);
	init_buf[0] = 0x05;
	init_buf[1] = 0x1c;
	init_buf[2] = 0x00;
	init_buf[3] = 0x00;  /* Deadband = 0 */
	init_buf[4] = 0x00;  /* Center = 0 */
	init_buf[9] = 0x00;  /* Right saturation = 0 */
	init_buf[10] = 0x00; /* Left saturation = 0 */
	ret = t500rs_send_usb_blocking(t500rs, init_buf, 11);
	if (ret) {
		hid_warn(t500rs->hdev, "Disable autocenter (0x05 0x1c) failed: %d\n", ret);
	}
	usleep_range(5000, 6000);

	/* Stop autocenter effect (effect ID 15) */
	memset(init_buf, 0, 4);
	init_buf[0] = 0x41;
	init_buf[1] = 15;  /* Autocenter effect ID */
	init_buf[2] = 0x00;  /* STOP */
	init_buf[3] = 0x01;
	ret = t500rs_send_usb_blocking(t500rs, init_buf, 4);
	if (ret) {
		hid_warn(t500rs->hdev, "Stop autocenter effect failed: %d\n", ret);
	} else {
		hid_info(t500rs->hdev, "Autocenter fully disabled\n");
	}

	hid_info(t500rs->hdev, "T500RS initialized successfully (USB INTERRUPT mode)\n");
	hid_info(t500rs->hdev, "Endpoint: 0x%02x, Buffer: %zu bytes\n",
		 t500rs->ep_out, t500rs->buffer_length);

	return 0;

err_wq:
	kfree(t500rs->send_buffer);
err_buffer:
err_endpoint:
	kfree(t500rs);
err_alloc:
	return ret;
}

/* Cleanup T500RS device */
int t500rs_wheel_destroy(void *data)
{
	struct t500rs_device_entry *t500rs = data;

	if (!t500rs)
		return 0;

	hid_info(t500rs->hdev, "T500RS: Cleaning up\n");

	/* CRITICAL: Mark device as inactive to prevent new work from being queued */
	t500rs->device_active = false;

	/* Stop force update timer */
	t500rs_stop_force_timer(t500rs);

	/* Destroy work queue (flushes pending work) */
	if (t500rs->wq) {
		flush_workqueue(t500rs->wq);
		destroy_workqueue(t500rs->wq);
	}

	/* Free resources */
	kfree(t500rs->send_buffer);
	kfree(t500rs);

	return 0;
}

/* Populate API callbacks */
int t500rs_populate_api(struct tmff2_device_entry *tmff2)
{
	int i;

	tmff2->play_effect = t500rs_play_effect;
	tmff2->upload_effect = t500rs_upload_effect;
	tmff2->update_effect = t500rs_update_effect;
	tmff2->stop_effect = t500rs_stop_effect;

	tmff2->set_gain = t500rs_set_gain;
	tmff2->set_autocenter = t500rs_set_autocenter;

	tmff2->wheel_init = t500rs_wheel_init;
	tmff2->wheel_destroy = t500rs_wheel_destroy;

	tmff2->params = t500rs_params;
	tmff2->max_effects = T500RS_MAX_EFFECTS;

	/* Copy supported effects array */
	for (i = 0; t500rs_effects[i] != -1 && i < FF_CNT; i++)
		tmff2->supported_effects[i] = t500rs_effects[i];
	tmff2->supported_effects[i] = -1;

	return 0;
}

