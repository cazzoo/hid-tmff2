// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Force feedback support for Thrustmaster T500RS racing wheel
 *
 * THIS DRIVER IS NON-FUNCTIONAL. DO NOT USE.
 */

#include <linux/usb.h>
#include <linux/hid.h>
#include "../hid-tmff2.h"

/* T500RS Constants */
#define T500RS_MAX_EFFECTS 16
#define T500RS_BUFFER_LENGTH 11560  /* HID Feature Report size */

/* HID Report ID for Force Feedback (use low byte for hid_hw_raw_request) */
#define T500RS_FFB_REPORT_ID 0xEF

/* Gain scaling constant (matches hid-tmff2.c) */
#define GAIN_MAX 65535

/* Effect Type IDs (from decompiled Windows driver) */
#define TM_EFFECT_CONSTANT      0x01
#define TM_EFFECT_SPRING        0x02
#define TM_EFFECT_DAMPER        0x03
#define TM_EFFECT_FRICTION      0x04
#define TM_EFFECT_INERTIA       0x05
#define TM_EFFECT_PERIODIC      0x06
#define TM_EFFECT_RAMP          0x07

/* Effect Operations */
#define TM_EFFECT_OP_START      0x01
#define TM_EFFECT_OP_STOP       0x02
#define TM_EFFECT_OP_SOLO       0x03
#define TM_EFFECT_OP_UPDATE     0x04

/* Maximum force magnitude */
#define TM_MAX_MAGNITUDE        10000

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
	FF_RAMP,
	FF_SPRING,
	FF_DAMPER,
	FF_FRICTION,
	FF_INERTIA,
	FF_PERIODIC,
	FF_SINE,
	FF_TRIANGLE,
	FF_SQUARE,
	FF_SAW_UP,
	FF_SAW_DOWN,
	FF_AUTOCENTER,
	FF_GAIN,
	-1
};

/* T500RS device-specific data */
struct t500rs_device_entry {
	struct hid_device *hdev;
	struct input_dev *input_dev;
	struct usb_device *usbdev;
	
	u8 *send_buffer;  /* 11560-byte FFB buffer */
	
	int (*open)(struct input_dev *dev);
	void (*close)(struct input_dev *dev);
};

/* Map Linux FF effect type to T500RS effect type */
static u8 t500rs_map_effect_type(u16 linux_type)
{
	switch (linux_type) {
	case FF_CONSTANT:
		return TM_EFFECT_CONSTANT;
	case FF_SPRING:
		return TM_EFFECT_SPRING;
	case FF_DAMPER:
		return TM_EFFECT_DAMPER;
	case FF_FRICTION:
		return TM_EFFECT_FRICTION;
	case FF_INERTIA:
		return TM_EFFECT_INERTIA;
	case FF_PERIODIC:
		return TM_EFFECT_PERIODIC;
	case FF_RAMP:
		return TM_EFFECT_RAMP;
	default:
		return TM_EFFECT_CONSTANT;
	}
}

/* Encode constant force effect into buffer */
static void t500rs_encode_constant(u8 *buf, struct ff_effect *effect, u8 gain)
{
	s16 magnitude;
	u16 direction;
	
	/* Apply gain scaling */
	magnitude = (effect->u.constant.level * gain) / 100;
	
	/* Clamp to valid range */
	if (magnitude > TM_MAX_MAGNITUDE)
		magnitude = TM_MAX_MAGNITUDE;
	else if (magnitude < -TM_MAX_MAGNITUDE)
		magnitude = -TM_MAX_MAGNITUDE;
	
	/* Direction (0-35999, representing 0-359.99 degrees) */
	direction = (effect->direction * 100) / 360;
	if (direction >= 36000)
		direction = 0;
	
	/* Encode parameters at offset 0x08 */
	*(s16 *)(buf + 0x08) = cpu_to_le16(magnitude);
	*(u16 *)(buf + 0x0A) = cpu_to_le16(direction);
	buf[0x0C] = 0x01;  /* Enable X axis */
}

/* Encode condition effect (Spring/Damper/Friction/Inertia) */
static void t500rs_encode_condition(u8 *buf, struct ff_effect *effect, u8 gain)
{
	struct ff_condition_effect *cond = &effect->u.condition[0];
	s16 cp_offset, pos_coeff, neg_coeff;
	u16 pos_sat, neg_sat, deadband;
	
	/* Scale coefficients by gain */
	cp_offset = (cond->center * gain) / 100;
	pos_coeff = (cond->right_coeff * gain) / 100;
	neg_coeff = (cond->left_coeff * gain) / 100;
	pos_sat = (cond->right_saturation * gain) / 100;
	neg_sat = (cond->left_saturation * gain) / 100;
	deadband = cond->deadband;
	
	/* Encode parameters at offset 0x08 */
	*(s16 *)(buf + 0x08) = cpu_to_le16(cp_offset);
	*(s16 *)(buf + 0x0A) = cpu_to_le16(pos_coeff);
	*(s16 *)(buf + 0x0C) = cpu_to_le16(neg_coeff);
	*(u16 *)(buf + 0x0E) = cpu_to_le16(pos_sat);
	*(u16 *)(buf + 0x10) = cpu_to_le16(neg_sat);
	*(s16 *)(buf + 0x12) = cpu_to_le16(deadband);
}

/* Encode periodic effect */
static void t500rs_encode_periodic(u8 *buf, struct ff_effect *effect, u8 gain)
{
	struct ff_periodic_effect *periodic = &effect->u.periodic;
	u16 magnitude, phase, period;
	s16 offset;
	u8 waveform;
	
	/* Scale magnitude by gain */
	magnitude = (periodic->magnitude * gain) / 100;
	if (magnitude > TM_MAX_MAGNITUDE)
		magnitude = TM_MAX_MAGNITUDE;
	
	offset = periodic->offset;
	phase = (periodic->phase * 100) / 360;  /* Convert to degrees * 100 */
	period = periodic->period;
	
	/* Map waveform type */
	switch (periodic->waveform) {
	case FF_SINE:
		waveform = 1;
		break;
	case FF_SQUARE:
		waveform = 2;
		break;
	case FF_TRIANGLE:
		waveform = 3;
		break;
	case FF_SAW_UP:
		waveform = 4;
		break;
	case FF_SAW_DOWN:
		waveform = 5;
		break;
	default:
		waveform = 1;  /* Default to sine */
	}
	
	/* Encode parameters at offset 0x08 */
	*(u16 *)(buf + 0x08) = cpu_to_le16(magnitude);
	*(s16 *)(buf + 0x0A) = cpu_to_le16(offset);
	*(u16 *)(buf + 0x0C) = cpu_to_le16(phase);
	*(u16 *)(buf + 0x0E) = cpu_to_le16(period);
	buf[0x10] = waveform;
}

/* Encode envelope */
static void t500rs_encode_envelope(u8 *buf, struct ff_envelope *envelope)
{
	if (!envelope)
		return;
	
	/* Envelope at offset 0x108 */
	*(u16 *)(buf + 0x108) = cpu_to_le16(envelope->attack_level);
	*(u16 *)(buf + 0x10A) = cpu_to_le16(envelope->attack_length);
	*(u16 *)(buf + 0x10C) = cpu_to_le16(envelope->fade_level);
	*(u16 *)(buf + 0x10E) = cpu_to_le16(envelope->fade_length);
}

/* Send buffer to device via HID feature report */
static int t500rs_send_buf(struct t500rs_device_entry *t500rs, u8 *send_buffer, size_t len)
{
	int ret;
	
	ret = hid_hw_raw_request(t500rs->hdev, T500RS_FFB_REPORT_ID,
				 send_buffer, len,
				 HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	
	if (ret < 0) {
		hid_err(t500rs->hdev, "Failed to send HID report: %d\n", ret);
		return ret;
	}
	
	return 0;
}

/* Upload effect to device */
int t500rs_upload_effect(void *data, struct tmff2_effect_state *state)
{
	struct t500rs_device_entry *t500rs = data;
	struct ff_effect *effect = &state->effect;
	u8 *buf = t500rs->send_buffer;
	u8 effect_gain = (gain * 100) / GAIN_MAX;
	
	if (!t500rs || !buf)
		return -ENODEV;
	
	/* Clear buffer */
	memset(buf, 0, T500RS_BUFFER_LENGTH);
	
	/* Set report ID (little-endian: 0xEF 0xCF) */
	buf[0] = 0xEF;
	buf[1] = 0xCF;
	
	/* Set effect type and operation */
	buf[2] = t500rs_map_effect_type(effect->type);
	buf[3] = TM_EFFECT_OP_START;
	
	/* Set effect ID and gain */
	buf[4] = effect->id & 0x0F;
	buf[5] = effect_gain;
	
	/* Set duration (milliseconds, 0 = infinite) */
	*(u16 *)(buf + 6) = cpu_to_le16(effect->replay.length);
	
	/* Encode effect-specific parameters */
	switch (effect->type) {
	case FF_CONSTANT:
		t500rs_encode_constant(buf, effect, effect_gain);
		if (effect->u.constant.envelope.attack_length ||
		    effect->u.constant.envelope.fade_length)
			t500rs_encode_envelope(buf, &effect->u.constant.envelope);
		break;
		
	case FF_SPRING:
	case FF_DAMPER:
	case FF_FRICTION:
	case FF_INERTIA:
		t500rs_encode_condition(buf, effect, effect_gain);
		break;
		
	case FF_PERIODIC:
		t500rs_encode_periodic(buf, effect, effect_gain);
		if (effect->u.periodic.envelope.attack_length ||
		    effect->u.periodic.envelope.fade_length)
			t500rs_encode_envelope(buf, &effect->u.periodic.envelope);
		break;
		
	case FF_RAMP:
		/* Encode ramp (start/end levels) */
		*(s16 *)(buf + 0x08) = cpu_to_le16(
			(effect->u.ramp.start_level * effect_gain) / 100);
		*(s16 *)(buf + 0x0A) = cpu_to_le16(
			(effect->u.ramp.end_level * effect_gain) / 100);
		break;
		
	default:
		hid_warn(t500rs->hdev, "Unsupported effect type: %d\n", effect->type);
		return -EINVAL;
	}
	
	/* Set magic constants (from decompiled Windows driver) */
	buf[0x48] = 0x01;  /* Effect count */
	*(u16 *)(buf + 0x4C) = cpu_to_le16(0x2D28);  /* Magic (11560 decimal) */
	
	/* Send to device */
	return t500rs_send_buf(t500rs, buf, T500RS_BUFFER_LENGTH);
}

/* Play effect (start playback) */
int t500rs_play_effect(void *data, struct tmff2_effect_state *state)
{
	struct t500rs_device_entry *t500rs = data;
	u8 *buf = t500rs->send_buffer;
	u8 effect_gain = (gain * 100) / GAIN_MAX;

	if (!t500rs || !buf)
		return -ENODEV;

	/* Clear buffer */
	memset(buf, 0, T500RS_BUFFER_LENGTH);

	/* Set report ID */
	buf[0] = 0xEF;
	buf[1] = 0xCF;

	/* Set operation to START */
	buf[3] = TM_EFFECT_OP_START;
	buf[4] = state->effect.id & 0x0F;
	buf[5] = effect_gain;

	return t500rs_send_buf(t500rs, buf, T500RS_BUFFER_LENGTH);
}

/* Stop effect */
int t500rs_stop_effect(void *data, struct tmff2_effect_state *state)
{
	struct t500rs_device_entry *t500rs = data;
	u8 *buf = t500rs->send_buffer;

	if (!t500rs || !buf)
		return -ENODEV;

	/* Clear buffer */
	memset(buf, 0, T500RS_BUFFER_LENGTH);

	/* Set report ID */
	buf[0] = 0xEF;
	buf[1] = 0xCF;

	/* Set operation to STOP */
	buf[3] = TM_EFFECT_OP_STOP;
	buf[4] = state->effect.id & 0x0F;

	return t500rs_send_buf(t500rs, buf, T500RS_BUFFER_LENGTH);
}

/* Update effect (modify running effect) */
int t500rs_update_effect(void *data, struct tmff2_effect_state *state)
{
	struct t500rs_device_entry *t500rs = data;
	struct ff_effect *effect = &state->effect;
	u8 *buf = t500rs->send_buffer;
	u8 effect_gain = (gain * 100) / GAIN_MAX;

	if (!t500rs || !buf)
		return -ENODEV;

	/* Clear buffer */
	memset(buf, 0, T500RS_BUFFER_LENGTH);

	/* Set report ID */
	buf[0] = 0xEF;
	buf[1] = 0xCF;

	/* Set effect type and UPDATE operation */
	buf[2] = t500rs_map_effect_type(effect->type);
	buf[3] = TM_EFFECT_OP_UPDATE;

	/* Set effect ID and gain */
	buf[4] = effect->id & 0x0F;
	buf[5] = effect_gain;

	/* Set duration */
	*(u16 *)(buf + 6) = cpu_to_le16(effect->replay.length);

	/* Encode effect-specific parameters (same as upload) */
	switch (effect->type) {
	case FF_CONSTANT:
		t500rs_encode_constant(buf, effect, effect_gain);
		if (effect->u.constant.envelope.attack_length ||
		    effect->u.constant.envelope.fade_length)
			t500rs_encode_envelope(buf, &effect->u.constant.envelope);
		break;

	case FF_SPRING:
	case FF_DAMPER:
	case FF_FRICTION:
	case FF_INERTIA:
		t500rs_encode_condition(buf, effect, effect_gain);
		break;

	case FF_PERIODIC:
		t500rs_encode_periodic(buf, effect, effect_gain);
		if (effect->u.periodic.envelope.attack_length ||
		    effect->u.periodic.envelope.fade_length)
			t500rs_encode_envelope(buf, &effect->u.periodic.envelope);
		break;

	case FF_RAMP:
		*(s16 *)(buf + 0x08) = cpu_to_le16(
			(effect->u.ramp.start_level * effect_gain) / 100);
		*(s16 *)(buf + 0x0A) = cpu_to_le16(
			(effect->u.ramp.end_level * effect_gain) / 100);
		break;

	default:
		return -EINVAL;
	}

	/* Set magic constants */
	buf[0x48] = 0x01;
	*(u16 *)(buf + 0x4C) = cpu_to_le16(0x2D28);

	return t500rs_send_buf(t500rs, buf, T500RS_BUFFER_LENGTH);
}

/* Set gain (master volume) */
int t500rs_set_gain(void *data, uint16_t new_gain)
{
	/* Gain is handled globally by the tmff2 framework */
	/* Individual effects will use the updated gain value */
	gain = new_gain;
	return 0;
}

/* Set wheel rotation range */
int t500rs_set_range(void *data, uint16_t new_range)
{
	/* T500RS supports 270-1080 degree range */
	/* Range setting would require additional USB commands */
	/* For now, just update the global range variable */
	if (new_range < 270)
		new_range = 270;
	if (new_range > 1080)
		new_range = 1080;

	range = new_range;
	return 0;
}

/* Set autocenter strength */
int t500rs_set_autocenter(void *data, uint16_t autocenter)
{
	struct t500rs_device_entry *t500rs = data;
	u8 *buf = t500rs->send_buffer;
	u8 strength;

	if (!t500rs || !buf)
		return -ENODEV;

	/* Convert 0-65535 to 0-100 */
	strength = (autocenter * 100) / 65535;

	/* Clear buffer */
	memset(buf, 0, T500RS_BUFFER_LENGTH);

	/* Set report ID */
	buf[0] = 0xEF;
	buf[1] = 0xCF;

	/* Set spring effect for autocenter */
	buf[2] = TM_EFFECT_SPRING;
	buf[3] = TM_EFFECT_OP_START;
	buf[4] = 0x00;  /* Use effect slot 0 for autocenter */
	buf[5] = strength;

	/* Spring parameters for centering */
	*(s16 *)(buf + 0x08) = cpu_to_le16(0);  /* Center at 0 */
	*(s16 *)(buf + 0x0A) = cpu_to_le16(strength * 100);  /* Positive coeff */
	*(s16 *)(buf + 0x0C) = cpu_to_le16(strength * 100);  /* Negative coeff */

	return t500rs_send_buf(t500rs, buf, T500RS_BUFFER_LENGTH);
}

/* Open device */
int t500rs_open(void *data, int open_mode)
{
	struct t500rs_device_entry *t500rs = data;

	if (!t500rs)
		return -ENODEV;

	return t500rs->open(t500rs->input_dev);
}

/* Close device */
int t500rs_close(void *data, int close_mode)
{
	struct t500rs_device_entry *t500rs = data;

	if (!t500rs)
		return -ENODEV;

	t500rs->close(t500rs->input_dev);
	return 0;
}

/* Initialize T500RS wheel */
static int t500rs_wheel_init(struct tmff2_device_entry *tmff2, int open_mode)
{
	struct t500rs_device_entry *t500rs;
	int ret;

	/* Allocate device data */
	t500rs = kzalloc(sizeof(struct t500rs_device_entry), GFP_KERNEL);
	if (!t500rs) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	/* Initialize device pointers */
	t500rs->hdev = tmff2->hdev;
	t500rs->input_dev = tmff2->input_dev;
	t500rs->usbdev = to_usb_device(tmff2->hdev->dev.parent->parent);

	/* Allocate FFB buffer (11560 bytes) */
	t500rs->send_buffer = kzalloc(T500RS_BUFFER_LENGTH, GFP_KERNEL);
	if (!t500rs->send_buffer) {
		ret = -ENOMEM;
		goto err_buffer;
	}

	/* Store original open/close functions */
	t500rs->open = tmff2->input_dev->open;
	t500rs->close = tmff2->input_dev->close;

	/* Set device data in tmff2 structure */
	tmff2->data = t500rs;
	tmff2->params = t500rs_params;
	tmff2->max_effects = T500RS_MAX_EFFECTS;
	memcpy(tmff2->supported_effects, t500rs_effects, sizeof(t500rs_effects));

	hid_info(t500rs->hdev, "T500RS force feedback initialized\n");
	return 0;

err_buffer:
	kfree(t500rs);
err_alloc:
	hid_err(tmff2->hdev, "Failed to initialize T500RS: %d\n", ret);
	return ret;
}

/* Cleanup T500RS wheel */
static int t500rs_wheel_destroy(void *data)
{
	struct t500rs_device_entry *t500rs = data;

	if (!t500rs)
		return -ENODEV;

	/* Free FFB buffer */
	kfree(t500rs->send_buffer);

	/* Free device data */
	kfree(t500rs);

	return 0;
}

/* Populate API - register callbacks with tmff2 framework */
int t500rs_populate_api(struct tmff2_device_entry *tmff2)
{
	/* Set effect callbacks */
	tmff2->play_effect = t500rs_play_effect;
	tmff2->upload_effect = t500rs_upload_effect;
	tmff2->update_effect = t500rs_update_effect;
	tmff2->stop_effect = t500rs_stop_effect;

	/* Set initialization callbacks */
	tmff2->wheel_init = t500rs_wheel_init;
	tmff2->wheel_destroy = t500rs_wheel_destroy;

	/* Set control callbacks */
	tmff2->open = t500rs_open;
	tmff2->close = t500rs_close;
	tmff2->set_gain = t500rs_set_gain;
	tmff2->set_range = t500rs_set_range;
	tmff2->set_autocenter = t500rs_set_autocenter;

	return 0;
}

