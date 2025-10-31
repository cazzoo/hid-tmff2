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


/* Logging verbosity (0=minimal, 1=verbose) */
static int t500rs_log_level;
module_param(t500rs_log_level, int, 0644);
MODULE_PARM_DESC(t500rs_log_level, "Log level: 0=minimal (default), 1=verbose");

/* Debug logging helper (requires local variable named 't500rs') */
#define T500RS_DBG(fmt, ...) do { \
	if (t500rs_log_level > 0) \
		hid_info(t500rs->hdev, fmt, ##__VA_ARGS__); \
} while (0)

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

	/* Timer debugging */
	unsigned long timer_start_jiffies;  /* When the timer was started */
	unsigned long force_update_count;   /* Number of force updates sent */

	/* Gain settings (0-65535 range, where 65535 = 100%) */
	u16 device_gain;  /* Device-level master gain (set via sysfs) */
	u16 game_gain;    /* In-game gain (set dynamically by game via set_gain callback) */

	/* Per-effect gain multipliers (0-100 range, where 100 = 100%) */
	/* These are applied in software when uploading/playing effects */
	u8 constant_gain;  /* Constant force effects gain */
	u8 periodic_gain;  /* Periodic effects (sine, square, triangle) gain */
	u8 spring_gain;    /* Spring effects gain */
	u8 damper_gain;    /* Damper effects gain */
	u8 friction_gain;  /* Friction effects gain */
	u8 inertia_gain;   /* Inertia effects gain */

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
	FF_AUTOCENTER,
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
	T500RS_DBG("USB TX [%zu]: %02x %02x %02x %02x %02x %02x %02x %02x\n",
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
		T500RS_DBG("USB transfer OK: %d bytes\n", transferred);
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
	u8 buf[15];
	unsigned long flags;
	s8 level;
	unsigned long elapsed_ms;

	/* CRITICAL: Check if device is still valid */
	if (!t500rs || !t500rs->usbdev || !t500rs->hdev) {
		pr_warn("t500rs_force_update_work: Device no longer valid, stopping\n");
		return;
	}

	/* Get current force level and update counter */
	spin_lock_irqsave(&t500rs->force_lock, flags);
	level = t500rs->current_force_level;
	t500rs->force_update_count++;
	spin_unlock_irqrestore(&t500rs->force_lock, flags);

	/* Calculate elapsed time since timer started */
	elapsed_ms = jiffies_to_msecs(jiffies - t500rs->timer_start_jiffies);

	/* WORKAROUND: Re-upload effect every 5 seconds to keep T500RS alive
	 * The T500RS seems to stop responding to force updates after ~9-10 seconds.
	 * Re-uploading the effect (Reports 0x02 and 0x01) seems to reset this timeout.
	 */
	if (t500rs->force_update_count % 250 == 0) {  /* Every 5 seconds (250 * 20ms) */
		T500RS_DBG("Re-uploading effect to keep T500RS alive (elapsed: %lu ms)\n", elapsed_ms);

		/* Report 0x02 - Envelope */
		memset(buf, 0, 15);
		buf[0] = 0x02;
		buf[1] = 0x1c;
		buf[2] = 0x00;
		t500rs_send_usb(t500rs, buf, 9);

		/* Report 0x01 - Main effect upload - MATCH WINDOWS! */
		memset(buf, 0, 15);
		buf[0] = 0x01;
		buf[1] = 0x00;  /* Effect ID 0 (Windows uses 0) */
		buf[2] = 0x00;  /* Constant force type */
		buf[3] = 0x40;
		buf[4] = 0xff;  /* Windows uses 0xff */
		buf[5] = 0xff;  /* Windows uses 0xff */
		buf[6] = 0x00;
		buf[7] = 0xff;
		buf[8] = 0xff;
		buf[9] = 0x0e;
		buf[10] = 0x00;
		buf[11] = 0x1c;
		buf[12] = 0x00;
		buf[13] = 0x00;
		buf[14] = 0x00;
		t500rs_send_usb(t500rs, buf, 15);
	}

	/* Send Report 0x03 - Force level */
	buf[0] = 0x03;
	buf[1] = 0x0e;
	buf[2] = 0x00;
	buf[3] = (u8)level;

	T500RS_DBG("Force update #%lu (elapsed: %lu ms): level=%d (0x%02x)\n",
		 t500rs->force_update_count, elapsed_ms, level, buf[3]);
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

	T500RS_DBG("Starting continuous force updates at 50Hz, level=%d\n", force_level);

	spin_lock_irqsave(&t500rs->force_lock, flags);
	t500rs->current_force_level = force_level;
	t500rs->force_timer_active = true;
	t500rs->timer_start_jiffies = jiffies;
	t500rs->force_update_count = 0;
	spin_unlock_irqrestore(&t500rs->force_lock, flags);

	T500RS_DBG("Starting force timer with level=%d\n", force_level);

	/* Start timer (20ms = 50Hz) */
	mod_timer(&t500rs->force_timer, jiffies + msecs_to_jiffies(20));
}

/* Stop continuous force updates */
static void t500rs_stop_force_timer(struct t500rs_device_entry *t500rs)
{
	unsigned long flags;
	unsigned long elapsed_ms;
	unsigned long update_count;
	bool was_active;

	if (!t500rs)
		return;

	spin_lock_irqsave(&t500rs->force_lock, flags);
	was_active = t500rs->force_timer_active;
	t500rs->force_timer_active = false;
	t500rs->current_force_level = 0;
	spin_unlock_irqrestore(&t500rs->force_lock, flags);

	/* Only log stats if timer was actually running */
	if (was_active && t500rs->timer_start_jiffies != 0) {
		elapsed_ms = jiffies_to_msecs(jiffies - t500rs->timer_start_jiffies);
		update_count = t500rs->force_update_count;
		T500RS_DBG("Stopping force timer after %lu updates in %lu ms\n",
			 update_count, elapsed_ms);
	}

	/* Stop the timer - this is safe from any context */
	del_timer(&t500rs->force_timer);

	/* Only cancel work if we're NOT being called from the work handler itself
	 * to avoid "scheduling while atomic" bug */
	if (!in_interrupt() && !in_atomic()) {
		cancel_work_sync(&t500rs->force_work);
	}
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
	T500RS_DBG("INIT USB TX [%zu]: %02x %02x %02x %02x %02x %02x %02x %02x\n",
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

	T500RS_DBG("INIT USB transfer OK: %d bytes\n", transferred);
	return 0;
}

/* Helper function to apply per-effect gain */
static inline int apply_effect_gain(int value, u8 effect_gain)
{
	/* effect_gain is 0-100, value is effect-specific range */
	/* Return value scaled by effect_gain percentage */
	return (value * effect_gain) / 100;
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

	T500RS_DBG("Upload constant: id=%d, level=%d\n",
		 effect->id, level);

	/* NO DEADZONE - Send all forces exactly as requested, matching Windows behavior */

	/* SIMPLE SEQUENCE - Match working userspace driver! */
	/* Report 0x02 - Envelope (attack/fade) */
	memset(buf, 0, 15);
	buf[0] = 0x02;
	buf[1] = 0x1c;  /* Subtype 0x1c */
	buf[2] = 0x00;
	T500RS_DBG("Sending Report 0x02 (envelope)...\n");
	ret = t500rs_send_usb(t500rs, buf, 9);
	if (ret) {
		hid_err(t500rs->hdev, "Failed to send Report 0x02: %d\n", ret);
		return ret;
	}

	/* Report 0x01 - Main effect upload - MATCH WINDOWS DRIVER EXACTLY! */
	memset(buf, 0, 15);
	buf[0] = 0x01;
	buf[1] = 0x00;  /* Effect ID 0 (Windows uses 0, not effect->id) */
	buf[2] = 0x00;  /* Constant force type */
	buf[3] = 0x40;
	buf[4] = 0xff;  /* Windows uses 0xff (was 0x69) */
	buf[5] = 0xff;  /* Windows uses 0xff (was 0x23) */
	buf[6] = 0x00;
	buf[7] = 0xff;
	buf[8] = 0xff;
	buf[9] = 0x0e;   /* Parameter subtype reference */
	buf[10] = 0x00;
	buf[11] = 0x1c;  /* Envelope subtype reference */
	buf[12] = 0x00;
	buf[13] = 0x00;
	buf[14] = 0x00;
	T500RS_DBG("Sending Report 0x01 (duration/control)...\n");
	ret = t500rs_send_usb(t500rs, buf, 15);
	if (ret) {
		hid_err(t500rs->hdev, "Failed to send Report 0x01: %d\n", ret);
		return ret;
	}

	T500RS_DBG("Constant effect %d uploaded (simple sequence)\n", effect->id);

	/* CRITICAL FIX FOR AMS2: Always update the force level when uploading.
	 * AMS2 calls stop/upload/play in rapid succession, so the timer might be
	 * stopped when upload is called. We update the force level here so that
	 * when play_effect starts the timer, it will use the correct force value.
	 *
	 * MATCH WINDOWS: Send forces exactly as requested - no amplification!
	 * Windows sends weak forces (4-27 out of 127) and they work fine.
	 */
	{
		s8 signed_level;
		s32 scaled;

		/* Simple linear scaling from -32767..32767 to -127..127 */
		scaled = (level * 127) / 32767;
		signed_level = (s8)scaled;

		T500RS_DBG("Upload constant: id=%d, level=%d -> %d (0x%02x)\n",
			 effect->id, level, signed_level, (u8)signed_level);

		/* Update the force level (will be used when timer starts/restarts) */
		t500rs_update_force_level(t500rs, signed_level);
	}

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
	u8 effect_gain;
	int right_strength, left_strength;

	/* Determine effect type and select appropriate gain */
	switch (effect->type) {
	case FF_SPRING:
		effect_type = 0x40;
		effect_gain = t500rs->spring_gain;
		break;
	case FF_DAMPER:
		effect_type = 0x41;
		effect_gain = t500rs->damper_gain;
		break;
	case FF_FRICTION:
		effect_type = 0x41;
		effect_gain = t500rs->friction_gain;
		break;
	case FF_INERTIA:
		effect_type = 0x41;
		effect_gain = t500rs->inertia_gain;
		break;
	default:
		return -EINVAL;
	}

	/* Get effect parameters and apply per-effect gain */
	/* Condition effects use right_saturation and left_saturation (0-65535) */
	right_strength = effect->u.condition[0].right_saturation;
	left_strength = effect->u.condition[0].left_saturation;

	/* Apply per-effect gain */
	right_strength = apply_effect_gain(right_strength, effect_gain);
	left_strength = apply_effect_gain(left_strength, effect_gain);

	/* Scale to device range (0-127) */
	right_strength = (right_strength * 127) / 65535;
	left_strength = (left_strength * 127) / 65535;

	T500RS_DBG("Upload condition: id=%d, type=0x%02x, gain=%u%%, R=%d, L=%d\n",
		 effect->id, effect_type, effect_gain, right_strength, left_strength);

	/* Report 0x05 - Condition parameters (coefficients) */
	memset(buf, 0, 15);
	buf[0] = 0x05;
	buf[1] = 0x0e;
	buf[2] = 0x00;
	buf[3] = (u8)right_strength;
	buf[4] = (u8)left_strength;
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

	/* Apply per-effect gain (periodic_gain is 0-100) */
	magnitude = apply_effect_gain(magnitude, t500rs->periodic_gain);

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

	T500RS_DBG("Upload %s: id=%d, magnitude=%d (0x%02x), period=%dms\n",
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

	T500RS_DBG("%s effect %d uploaded\n", type_name, effect->id);
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

	T500RS_DBG("Upload ramp: id=%d, start=%d, end=%d, duration=%dms\n",
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

	T500RS_DBG("Ramp effect %d uploaded (simple mode)\n", effect->id);
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

	T500RS_DBG("Play effect: id=%d, type=0x%02x (FF_CONSTANT=0x%02x)\n",
		 effect->id, effect->type, FF_CONSTANT);

	/* NOTE: Condition effect disable code removed - testing Hypothesis 6 */
	/* Keeping it simple to match userspace driver more closely */

	/* For constant force, start continuous force updates */


	if (effect->type == FF_CONSTANT) {
		s8 signed_level;
		unsigned long flags;

		/* NOTE: Gain is now sent to device via Report 0x43 in set_gain() */
		/* No need to apply gain here - the device handles it */

		/* CRITICAL FIX FOR AMS2: Use the force level that was set by upload_effect.
		 * AMS2 calls stop/upload/play in rapid succession, so upload_effect has
		 * already calculated and stored the correct force level in current_force_level.
		 */
		spin_lock_irqsave(&t500rs->force_lock, flags);
		signed_level = t500rs->current_force_level;
		spin_unlock_irqrestore(&t500rs->force_lock, flags);

			/* If already armed (timer running), skip re-sending 0x41 START.
			 * Subsequent updates will adjust force level.
			 */
			{
				unsigned long __fl;
				bool __active;
				spin_lock_irqsave(&t500rs->force_lock, __fl);
				__active = t500rs->force_timer_active;
				spin_unlock_irqrestore(&t500rs->force_lock, __fl);
				if (__active) {
					T500RS_DBG("Constant already armed; skipping 0x41 START\n");
					return 0;
				}
			}

		T500RS_DBG("Constant force: using level=%d (0x%02x) from upload_effect\n",
			 signed_level, (u8)signed_level);

		/* CRITICAL: Send Report 0x03 BEFORE Report 0x41! */
		/* This matches the working userspace driver sequence */
		buf[0] = 0x03;
		buf[1] = 0x0e;
		buf[2] = 0x00;
		buf[3] = (u8)signed_level;

		T500RS_DBG("Sending Report 0x03 (force level): level=%d (0x%02x)\n",
			 signed_level, (u8)signed_level);
		ret = t500rs_send_usb(t500rs, buf, 4);
		if (ret) {
			hid_err(t500rs->hdev, "Failed to send Report 0x03: %d\n", ret);
			return ret;
		}

		/* Now send Report 0x41 START command */
		/* This activates the effect with the force level already set */
		buf[0] = 0x41;
		buf[1] = 0x00;  /* Effect ID 0 to match Windows */
		buf[2] = 0x41;  /* START command */
		buf[3] = 0x01;

		T500RS_DBG("Sending Report 0x41 (START): %02x %02x %02x %02x\n",
			buf[0], buf[1], buf[2], buf[3]);
		ret = t500rs_send_usb(t500rs, buf, 4);
		if (ret) {
			hid_err(t500rs->hdev, "Failed to send START command: %d\n", ret);
			return ret;
		}

		/* T500RS requires CONTINUOUS force updates at 50Hz! */
		/* Start the force update timer which will send Report 0x03 every 20ms */
		/* The timer will use current_force_level which was set by upload_effect */
		t500rs_start_force_timer(t500rs, signed_level);

		return 0;
	}

	/* For other effect types, send start command - Report 0x41
	 * T500RS expects EffectID=0 for 0x41 commands as well.
	 */
	buf[0] = 0x41;
	buf[1] = 0x00;  /* Effect ID 0 to match device expectations */
	buf[2] = 0x41;  /* START command */
	buf[3] = 0x01;

	T500RS_DBG("Sending START command (EffectID=0) for effect %d\n", effect->id);
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

	T500RS_DBG("Stop effect: id=%d, type=%d\n", state->effect.id, state->effect.type);

	/* For constant force: either SOFT-STOP (param) or Windows-style STOP */
	if (state->effect.type == FF_CONSTANT) {
		/* Windows-style: stop timer and send STOP (0x41 00 00 01) */
				t500rs_stop_force_timer(t500rs);
			buf[0] = 0x41;
			buf[1] = 0x00;
			buf[2] = 0x00;  /* STOP command */
			buf[3] = 0x01;
			T500RS_DBG("Sending Report 0x41 (STOP): %02x %02x %02x %02x\n",
				buf[0], buf[1], buf[2], buf[3]);
			ret = t500rs_send_usb(t500rs, buf, 4);
			T500RS_DBG("Stop effect (constant) returned: %d\n", ret);
			return ret;
	}

	/* For other effect types, send stop command - Report 0x41
	 * Use EffectID=0 to match device expectations for 0x41.
	 */
	buf[0] = 0x41;
	buf[1] = 0x00;  /* Effect ID 0 */
	buf[2] = 0x00;  /* STOP command */
	buf[3] = 0x01;

	ret = t500rs_send_usb(t500rs, buf, 4);
	T500RS_DBG("Stop effect (non-constant) returned: %d\n", ret);
	return ret;
}

/* Update effect - re-upload and update force level if constant force */
int t500rs_update_effect(void *data, struct tmff2_effect_state *state)
{
	struct t500rs_device_entry *t500rs = data;
	struct ff_effect *effect = &state->effect;

	if (!t500rs)
		return -ENODEV;

	/* Do NOT re-upload here; Windows keeps the effect and only updates force level */
	/* This avoids redundant USB traffic and state churn */

	/* CRITICAL FIX: If this is a constant force effect and the timer is active,
	 * update the force level being sent to the wheel.
	 * AMS2 calls update_effect() repeatedly to change force strength during gameplay.
	 *
	 * MATCH WINDOWS: Send forces exactly as requested - no deadzone, no amplification!
	 */
	if (effect->type == FF_CONSTANT) {
		int level = effect->u.constant.level;
		s8 signed_level;
		s32 scaled;

		/* Simple linear scaling from -32767..32767 to -127..127 */
		scaled = (level * 127) / 32767;
		signed_level = (s8)scaled;

		T500RS_DBG("Update constant force: level=%d -> %d (0x%02x)\n",
			 level, signed_level, (u8)signed_level);

		/* Update the force level state */
		t500rs_update_force_level(t500rs, signed_level);

		if (t500rs->force_timer_active) {
			/* Timer running: next tick will send updated level */
	}
		}


	return 0;
}

/* Set gain - called dynamically by games during gameplay */
int t500rs_set_gain(void *data, u16 gain)
{
	struct t500rs_device_entry *t500rs = data;
	u8 buf[4];
	u8 device_gain_byte;
	u32 combined_gain;
	int ret;

	if (!t500rs)
		return -ENODEV;

	/* Store the game's gain setting */
	t500rs->game_gain = gain;

	/* Calculate combined gain: (device_gain * game_gain) / 65535 */
	/* Both are 0-65535, result is also 0-65535 */
	combined_gain = ((u32)t500rs->device_gain * (u32)gain) / 65535;

	/* Convert to device range (0-127, where 127 = 100%) */
	device_gain_byte = (u8)((combined_gain * 127) / 65535);

	T500RS_DBG("Set gain: game=%u (%u%%), device=%u (%u%%), combined=%u (%u%%), sending=0x%02x\n",
		 gain, (gain * 100) / 65535,
		 t500rs->device_gain, (t500rs->device_gain * 100) / 65535,
		 combined_gain, (combined_gain * 100) / 65535,
		 device_gain_byte);

	/* Send Report 0x43 - Set global gain */
	buf[0] = 0x43;
	buf[1] = device_gain_byte;

	ret = t500rs_send_usb(t500rs, buf, 2);
	if (ret) {
		hid_err(t500rs->hdev, "Failed to set gain: %d\n", ret);
		return ret;
	}

	return 0;
}

/* Set autocenter */
int t500rs_set_autocenter(void *data, u16 autocenter)
{
	struct t500rs_device_entry *t500rs = data;
	u8 *buf;
	int ret;
	u8 autocenter_percent;

	if (!t500rs)
		return -ENODEV;

	/* Convert from 0-65535 range to 0-100 percentage */
	autocenter_percent = (u8)((autocenter * 100) / 65535);

	T500RS_DBG("Set autocenter: %u%% (value=%u)\n",
		 autocenter_percent, autocenter);

	/* Allocate separate buffer to avoid conflicts with FFB operations
	 * Use GFP_ATOMIC because this can be called from atomic context
	 * (input_ff_event holds spinlocks) */
	buf = kzalloc(t500rs->buffer_length, GFP_ATOMIC);
	if (!buf) {
		hid_err(t500rs->hdev, "Failed to allocate buffer for autocenter\n");
		return -ENOMEM;
	}

	if (autocenter == 0) {
		/* Disable autocenter: Report 0x40 0x04 0x00 */
		buf[0] = 0x40;
		buf[1] = 0x04;
		buf[2] = 0x00;  /* Disable */
		buf[3] = 0x00;
		ret = t500rs_send_usb(t500rs, buf, 4);
		if (ret) {
			hid_err(t500rs->hdev, "Failed to disable autocenter: %d\n", ret);
			kfree(buf);
			return ret;
		}
		T500RS_DBG("Autocenter disabled\n");
	} else {
		/* Enable autocenter: Report 0x40 0x04 0x01 */
		buf[0] = 0x40;
		buf[1] = 0x04;
		buf[2] = 0x01;  /* Enable */
		buf[3] = 0x00;
		ret = t500rs_send_usb(t500rs, buf, 4);
		if (ret) {
			hid_err(t500rs->hdev, "Failed to enable autocenter: %d\n", ret);
			kfree(buf);
			return ret;
		}

		/* NOTE: Removed usleep_range() here - cannot sleep in atomic context!
		 * This function is called from input_ff_event() which holds spinlocks.
		 * The USB subsystem handles queuing properly without explicit delays. */

		/* Set autocenter strength: Report 0x40 0x03 [value] */
		buf[0] = 0x40;
		buf[1] = 0x03;
		buf[2] = autocenter_percent;  /* 0-100 percentage */
		buf[3] = 0x00;
		ret = t500rs_send_usb(t500rs, buf, 4);
		if (ret) {
			hid_err(t500rs->hdev, "Failed to set autocenter strength: %d\n", ret);
			kfree(buf);
			return ret;
		}

		T500RS_DBG("Autocenter enabled at %u%%\n", autocenter_percent);
	}

	/* Apply settings: Report 0x42 0x05 */
	buf[0] = 0x42;
	buf[1] = 0x05;
	ret = t500rs_send_usb(t500rs, buf, 2);
	if (ret) {
		hid_err(t500rs->hdev, "Failed to apply autocenter settings: %d\n", ret);
		kfree(buf);
		return ret;
	}

	kfree(buf);
	return 0;
}

/* Set wheel rotation range (270-1080 degrees) */
int t500rs_set_range(void *data, u16 range)
{
	struct t500rs_device_entry *t500rs = data;
	u8 *buf;
	int ret;
	u16 range_value;

	if (!t500rs)
		return -ENODEV;

	/* Clamp range to valid values */
	if (range < 270) {
		hid_warn(t500rs->hdev, "Range %u too small, clamping to 270\n", range);
		range = 270;
	}
	if (range > 1080) {
		hid_warn(t500rs->hdev, "Range %u too large, clamping to 1080\n", range);
		range = 1080;
	}

	/* Allocate separate buffer to avoid conflicts with FFB operations
	 * Use GFP_ATOMIC because this can be called from atomic context
	 * (though currently only called from probe/sysfs, being safe) */
	buf = kzalloc(t500rs->buffer_length, GFP_ATOMIC);
	if (!buf) {
		hid_err(t500rs->hdev, "Failed to allocate buffer for range setting\n");
		return -ENOMEM;
	}

	T500RS_DBG("Setting wheel range to %u degrees\n", range);

	/* Based on USB capture analysis of "changed_rotation_angle_from_900_to_200_degrees.pcapng":
	 * The T500RS uses Report 0x40 0x11 [value_hi] [value_lo] to set rotation range
	 *
	 * Observed values from capture (BIG-ENDIAN byte order):
	 * - 900° → 0xf6d2 (63186 decimal) → bytes: 40 11 f6 d2
	 * - 200° → 0xa13d (41277 decimal) → bytes: 40 11 a1 3d
	 *
	 * Linear regression formula:
	 * slope = (63186 - 41277) / (900 - 200) = 21909 / 700 = 31.298571
	 * intercept = 41277 - (200 * 31.298571) = 35017.285714
	 *
	 * Formula: value = 35017 + (range * 31.3)
	 * Integer approximation: value = 35017 + ((range * 313) / 10)
	 */
	range_value = 35017 + ((range * 313) / 10);

	/* Send Report 0x40 0x11 [value_hi] [value_lo] to set range
	 * NOTE: This uses BIG-ENDIAN byte order (high byte first)! */
	buf[0] = 0x40;
	buf[1] = 0x11;
	buf[2] = (range_value >> 8) & 0xFF; /* High byte first (big-endian) */
	buf[3] = range_value & 0xFF;        /* Low byte second */

	ret = t500rs_send_usb(t500rs, buf, 4);
	if (ret) {
		hid_err(t500rs->hdev, "Failed to send range command: %d\n", ret);
		kfree(buf);
		return ret;
	}

	T500RS_DBG("Range set to %u degrees (value=0x%04x)\n", range, range_value);

	/* Apply settings with Report 0x42 0x05 */
	buf[0] = 0x42;
	buf[1] = 0x05;
	ret = t500rs_send_usb(t500rs, buf, 2);
	if (ret) {
		hid_err(t500rs->hdev, "Failed to apply range settings: %d\n", ret);
		kfree(buf);
		return ret;
	}

	kfree(buf);
	return 0;
}

/* Set spring level (0-100) - called from sysfs */
int t500rs_set_spring_level(void *data, u8 level)
{
	struct t500rs_device_entry *t500rs = data;

	if (!t500rs)
		return -ENODEV;

	/* Clamp to valid range */
	if (level > 100)
		level = 100;

	/* Update the spring gain setting */
	t500rs->spring_gain = level;

	T500RS_DBG("Spring level set to %u%%\n", level);

	/* Note: The new level will be applied when the next spring effect is uploaded/updated */
	return 0;
}

/* Set damper level (0-100) - called from sysfs */
int t500rs_set_damper_level(void *data, u8 level)
{
	struct t500rs_device_entry *t500rs = data;

	if (!t500rs)
		return -ENODEV;

	/* Clamp to valid range */
	if (level > 100)
		level = 100;

	/* Update the damper gain setting */
	t500rs->damper_gain = level;

	T500RS_DBG("Damper level set to %u%%\n", level);

	/* Note: The new level will be applied when the next damper effect is uploaded/updated */
	return 0;
}

/* Set friction level (0-100) - called from sysfs */
int t500rs_set_friction_level(void *data, u8 level)
{
	struct t500rs_device_entry *t500rs = data;

	if (!t500rs)
		return -ENODEV;

	/* Clamp to valid range */
	if (level > 100)
		level = 100;

	/* Update the friction gain setting */
	t500rs->friction_gain = level;

	T500RS_DBG("Friction level set to %u%%\n", level);

	/* Note: The new level will be applied when the next friction effect is uploaded/updated */
	return 0;
}

/* Sysfs attributes for per-effect gains */

static ssize_t constant_gain_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct tmff2_device_entry *tmff2 = hid_get_drvdata(hdev);
	struct t500rs_device_entry *t500rs;

	if (!tmff2 || !tmff2->data)
		return -ENODEV;

	t500rs = tmff2->data;
	return scnprintf(buf, PAGE_SIZE, "%u\n", t500rs->constant_gain);
}

static ssize_t constant_gain_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct tmff2_device_entry *tmff2 = hid_get_drvdata(hdev);
	struct t500rs_device_entry *t500rs;
	unsigned int value;
	int ret;

	if (!tmff2 || !tmff2->data)
		return -ENODEV;

	t500rs = tmff2->data;

	ret = kstrtouint(buf, 0, &value);
	if (ret) {
		hid_err(hdev, "kstrtouint failed at constant_gain_store: %d\n", ret);
		return ret;
	}

	if (value > 100) {
		hid_err(hdev, "constant_gain must be 0-100, got %u\n", value);
		return -EINVAL;
	}

	t500rs->constant_gain = (u8)value;
	hid_dbg(hdev, "Constant gain set to %u%%\n", value);

	return count;
}
static DEVICE_ATTR_RW(constant_gain);

static ssize_t periodic_gain_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct tmff2_device_entry *tmff2 = hid_get_drvdata(hdev);
	struct t500rs_device_entry *t500rs;

	if (!tmff2 || !tmff2->data)
		return -ENODEV;

	t500rs = tmff2->data;
	return scnprintf(buf, PAGE_SIZE, "%u\n", t500rs->periodic_gain);
}

static ssize_t periodic_gain_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct tmff2_device_entry *tmff2 = hid_get_drvdata(hdev);
	struct t500rs_device_entry *t500rs;
	unsigned int value;
	int ret;

	if (!tmff2 || !tmff2->data)
		return -ENODEV;

	t500rs = tmff2->data;

	ret = kstrtouint(buf, 0, &value);
	if (ret) {
		hid_err(hdev, "kstrtouint failed at periodic_gain_store: %d\n", ret);
		return ret;
	}

	if (value > 100) {
		hid_err(hdev, "periodic_gain must be 0-100, got %u\n", value);
		return -EINVAL;
	}

	t500rs->periodic_gain = (u8)value;
	hid_dbg(hdev, "Periodic gain set to %u%%\n", value);

	return count;
}
static DEVICE_ATTR_RW(periodic_gain);

static ssize_t t500rs_spring_gain_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct tmff2_device_entry *tmff2 = hid_get_drvdata(hdev);
	struct t500rs_device_entry *t500rs;

	if (!tmff2 || !tmff2->data)
		return -ENODEV;

	t500rs = tmff2->data;
	return scnprintf(buf, PAGE_SIZE, "%u\n", t500rs->spring_gain);
}

static ssize_t t500rs_spring_gain_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct tmff2_device_entry *tmff2 = hid_get_drvdata(hdev);
	struct t500rs_device_entry *t500rs;
	unsigned int value;
	int ret;

	if (!tmff2 || !tmff2->data)
		return -ENODEV;

	t500rs = tmff2->data;

	ret = kstrtouint(buf, 0, &value);
	if (ret) {
		hid_err(hdev, "kstrtouint failed at t500rs_spring_gain_store: %d\n", ret);
		return ret;
	}

	if (value > 100) {
		hid_err(hdev, "spring_gain must be 0-100, got %u\n", value);
		return -EINVAL;
	}

	t500rs->spring_gain = (u8)value;
	hid_dbg(hdev, "Spring gain set to %u%%\n", value);

	return count;
}
static DEVICE_ATTR(t500rs_spring_gain, 0660, t500rs_spring_gain_show, t500rs_spring_gain_store);

static ssize_t t500rs_damper_gain_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct tmff2_device_entry *tmff2 = hid_get_drvdata(hdev);
	struct t500rs_device_entry *t500rs;

	if (!tmff2 || !tmff2->data)
		return -ENODEV;

	t500rs = tmff2->data;
	return scnprintf(buf, PAGE_SIZE, "%u\n", t500rs->damper_gain);
}

static ssize_t t500rs_damper_gain_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct tmff2_device_entry *tmff2 = hid_get_drvdata(hdev);
	struct t500rs_device_entry *t500rs;
	unsigned int value;
	int ret;

	if (!tmff2 || !tmff2->data)
		return -ENODEV;

	t500rs = tmff2->data;

	ret = kstrtouint(buf, 0, &value);
	if (ret) {
		hid_err(hdev, "kstrtouint failed at t500rs_damper_gain_store: %d\n", ret);
		return ret;
	}

	if (value > 100) {
		hid_err(hdev, "damper_gain must be 0-100, got %u\n", value);
		return -EINVAL;
	}

	t500rs->damper_gain = (u8)value;
	hid_dbg(hdev, "Damper gain set to %u%%\n", value);

	return count;
}
static DEVICE_ATTR(t500rs_damper_gain, 0660, t500rs_damper_gain_show, t500rs_damper_gain_store);

/* Device open callback - called when a game/application opens the device */
int t500rs_open(void *data, int open_mode)
{
	struct t500rs_device_entry *t500rs = data;

	if (!t500rs)
		return -ENODEV;

	T500RS_DBG("T500RS: Device opened by application (mode=%d)\n", open_mode);

	/* CRITICAL: Flush work queue to prevent deadlock with usbhid_open() */
	/* The work queue may be trying to send USB data while usbhid is opening */
	if (t500rs->wq)
		flush_workqueue(t500rs->wq);

	/* Call stored open callback if it exists */
	if (t500rs->open)
		return t500rs->open(t500rs->input_dev);

	return 0;
}

/* Device close callback - called when a game/application closes the device */
int t500rs_close(void *data, int open_mode)
{
	struct t500rs_device_entry *t500rs = data;

	if (!t500rs)
		return -ENODEV;

	T500RS_DBG("T500RS: Device closed by application (mode=%d)\n", open_mode);

	/* Stop any ongoing force effects */
	t500rs_stop_force_timer(t500rs);

	/* Flush work queue */
	if (t500rs->wq)
		flush_workqueue(t500rs->wq);

	/* Call stored close callback if it exists */
	if (t500rs->close)
		t500rs->close(t500rs->input_dev);

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

	hid_dbg(tmff2->hdev, "T500RS: Initializing USB INTERRUPT mode\n");

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

	T500RS_DBG("Found INTERRUPT OUT endpoint: 0x%02x\n", t500rs->ep_out);

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

	/* Initialize gain settings to 100% (65535 = 100%) */
	t500rs->device_gain = 65535;  /* Device-level master gain */
	t500rs->game_gain = 65535;    /* In-game gain */

	/* Initialize per-effect gains to 100% (0-100 range) */
	t500rs->constant_gain = 100;
	t500rs->periodic_gain = 100;
	t500rs->spring_gain = 100;
	t500rs->damper_gain = 100;
	t500rs->friction_gain = 100;
	t500rs->inertia_gain = 100;

	/* Store original input_dev open/close callbacks */
	t500rs->open = tmff2->input_dev->open;
	t500rs->close = tmff2->input_dev->close;

	/* Store device data in tmff2 */
	tmff2->data = t500rs;

	/* Use send_buffer for all USB transfers (DMA-safe) */
	init_buf = t500rs->send_buffer;

	/* Send initialization sequence (from userspace driver) */
	T500RS_DBG("Sending initialization sequence...\n");

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
		T500RS_DBG("Autocenter fully disabled\n");
	}

	/* Create sysfs attributes for per-effect gains */
	/* Note: Ignore -EEXIST errors (attribute already exists from previous init) */

	ret = device_create_file(&t500rs->hdev->dev, &dev_attr_constant_gain);
	if (ret && ret != -EEXIST)
		hid_warn(t500rs->hdev, "Failed to create constant_gain sysfs: %d\n", ret);
	else if (ret == 0)
		T500RS_DBG("Created constant_gain sysfs attribute\n");

	ret = device_create_file(&t500rs->hdev->dev, &dev_attr_periodic_gain);
	if (ret && ret != -EEXIST)
		hid_warn(t500rs->hdev, "Failed to create periodic_gain sysfs: %d\n", ret);
	else if (ret == 0)
		T500RS_DBG("Created periodic_gain sysfs attribute\n");

	ret = device_create_file(&t500rs->hdev->dev, &dev_attr_t500rs_spring_gain);
	if (ret && ret != -EEXIST)
		hid_warn(t500rs->hdev, "Failed to create t500rs_spring_gain sysfs: %d\n", ret);
	else if (ret == 0)
		T500RS_DBG("Created t500rs_spring_gain sysfs attribute\n");

	ret = device_create_file(&t500rs->hdev->dev, &dev_attr_t500rs_damper_gain);
	if (ret && ret != -EEXIST)
		hid_warn(t500rs->hdev, "Failed to create t500rs_damper_gain sysfs: %d\n", ret);
	else if (ret == 0)
		T500RS_DBG("Created t500rs_damper_gain sysfs attribute\n");

	hid_info(t500rs->hdev, "T500RS initialized successfully (USB INTERRUPT mode)\n");
	T500RS_DBG("Endpoint: 0x%02x, Buffer: %zu bytes\n",
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

	T500RS_DBG("T500RS: Cleaning up\n");

	/* CRITICAL: Mark device as inactive to prevent new work from being queued */
	t500rs->device_active = false;

	/* Stop force update timer */
	t500rs_stop_force_timer(t500rs);

	/* Destroy work queue (flushes pending work) */
	if (t500rs->wq) {
		flush_workqueue(t500rs->wq);
		destroy_workqueue(t500rs->wq);
	}

	/* Remove sysfs attributes */
	T500RS_DBG("Removing per-effect gain sysfs attributes...\n");
	device_remove_file(&t500rs->hdev->dev, &dev_attr_constant_gain);
	device_remove_file(&t500rs->hdev->dev, &dev_attr_periodic_gain);
	device_remove_file(&t500rs->hdev->dev, &dev_attr_t500rs_spring_gain);
	device_remove_file(&t500rs->hdev->dev, &dev_attr_t500rs_damper_gain);
	T500RS_DBG("Sysfs attributes removed\n");

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
	tmff2->set_range = t500rs_set_range;
	tmff2->set_spring_level = t500rs_set_spring_level;
	tmff2->set_damper_level = t500rs_set_damper_level;
	tmff2->set_friction_level = t500rs_set_friction_level;

	tmff2->wheel_init = t500rs_wheel_init;
	tmff2->wheel_destroy = t500rs_wheel_destroy;

	/* CRITICAL FIX: Implement open/close to prevent deadlock when games open device */
	/* These callbacks flush the work queue to avoid USB lock contention */
	tmff2->open = t500rs_open;
	tmff2->close = t500rs_close;

	tmff2->params = t500rs_params;
	tmff2->max_effects = T500RS_MAX_EFFECTS;

	/* Copy supported effects array */
	for (i = 0; t500rs_effects[i] != -1 && i < FF_CNT; i++)
		tmff2->supported_effects[i] = t500rs_effects[i];
	tmff2->supported_effects[i] = -1;

	return 0;
}

