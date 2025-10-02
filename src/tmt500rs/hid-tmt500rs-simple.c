// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Force feedback support for Thrustmaster T500RS - Simplified Implementation
 *
 * This implementation follows T300RS patterns for reliable device detection
 * and basic force feedback support using a 4-byte USB command protocol.
 */

#include <linux/usb.h>
#include <linux/hid.h>
#include "../hid-tmff2.h"
#include "hid-tmt500rs-simple.h"

/* Supported parameters */
static const unsigned long t500rs_params =
	PARAM_SPRING_LEVEL
	| PARAM_DAMPER_LEVEL
	| PARAM_FRICTION_LEVEL
	| PARAM_GAIN
	| PARAM_RANGE
	;

/* Supported force feedback effects */
static const signed short t500rs_effects[] = {
	FF_CONSTANT,
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

/* Send buffer using RAW USB INTERRUPT transfer (bypass HID layer) */
static int t500rs_send_buf(struct t500rs_simple_entry *t500rs, u8 *send_buffer, size_t len)
{
	char hex_str[64];
	int pos = 0;
	int i;
	int allowed = 0;
	int ret, actual;

	/* Build hex string for debug */
	for (i = 0; i < len && pos < sizeof(hex_str) - 3; ++i) {
		pos += snprintf(hex_str + pos, sizeof(hex_str) - pos, "%02x ", send_buffer[i]);
	}

#if T500RS_SAFE_MODE
	/* SAFE MODE: Check if this specific command is allowed */

	/* Check for stop command (safest) */
	if (len == 4 && send_buffer[0] == 0x41 && send_buffer[2] == 0x00) {
#if T500RS_ALLOW_STOP_ONLY
		allowed = 1;
		hid_info(t500rs->hdev, "[STOP ALLOWED] len=%zu, data=[%s]\n", len, hex_str);
#else
		hid_info(t500rs->hdev, "[SAFE MODE - STOP NOT SENT] len=%zu, data=[%s]\n", len, hex_str);
		return 0;
#endif
	}
	/* Check for parameter upload - Reports 0x01, 0x02, 0x04 */
	else if ((len == 15 && send_buffer[0] == 0x01) ||  /* Report 0x01 - 15 bytes */
		 (len == 9 && send_buffer[0] == 0x02) ||   /* Report 0x02 - 9 bytes */
		 (len == 8 && send_buffer[0] == 0x04)) {   /* Report 0x04 - 8 bytes */
#if T500RS_ALLOW_PARAMS
		allowed = 1;
		hid_info(t500rs->hdev, "[PARAMS ALLOWED] len=%zu, data=[%s]\n", len, hex_str);
#else
		hid_info(t500rs->hdev, "[SAFE MODE - PARAMS NOT SENT] len=%zu, data=[%s]\n", len, hex_str);
		return 0;
#endif
	}
	/* Check for start command */
	else if (len == 4 && send_buffer[0] == 0x41 && send_buffer[2] == 0x41) {
#if T500RS_ALLOW_START
		allowed = 1;
		hid_info(t500rs->hdev, "[START ALLOWED] len=%zu, data=[%s]\n", len, hex_str);
#else
		hid_info(t500rs->hdev, "[SAFE MODE - START NOT SENT] len=%zu, data=[%s]\n", len, hex_str);
		return 0;
#endif
	}
	/* Unknown command - block it */
	else {
		hid_info(t500rs->hdev, "[SAFE MODE - UNKNOWN NOT SENT] len=%zu, data=[%s]\n", len, hex_str);
		return 0;
	}

	/* If we get here and not allowed, don't send */
	if (!allowed) {
		return 0;
	}
#else
	/* Safe mode disabled - log what we're sending */
	hid_info(t500rs->hdev, "Sending via RAW USB: len=%zu, data=[%s]\n", len, hex_str);
#endif

	/* Safety check */
	if (!t500rs || !t500rs->usbdev) {
		pr_err("t500rs: Invalid state in send_buf\n");
		return -EINVAL;
	}

	/* Use hid_hw_output_report which should work for raw data */
	hid_dbg(t500rs->hdev, "Sending via hid_hw_output_report: len=%zu, data=[%s]\n",
		len, hex_str);

	ret = hid_hw_output_report(t500rs->hdev, send_buffer, len);

	if (ret < 0) {
		hid_err(t500rs->hdev, "hid_hw_output_report failed: %d\n", ret);
		return ret;
	}

	hid_dbg(t500rs->hdev, "hid_hw_output_report success: %d bytes\n", ret);

	/* Small delay */
	msleep(5);

	return 0;
}

/* Send command using internal buffer */
static int t500rs_send_int(struct t500rs_simple_entry *t500rs, u8 *buf, size_t len)
{
	int ret = t500rs_send_buf(t500rs, buf, len);
	return ret;
}

/* Initialize device with Windows sequence */
static int t500rs_initialize(struct t500rs_simple_entry *t500rs)
{
	int ret;
	u8 buf[15];

	hid_info(t500rs->hdev, "Initializing T500RS with Windows sequence...\n");

	/* Report 0x42 - Init (15 bytes) */
	memset(buf, 0, sizeof(buf));
	buf[0] = 0x42;
	buf[1] = 0x01;
	ret = t500rs_send_int(t500rs, buf, 15);
	if (ret) return ret;
	msleep(40);

	/* Report 0x0a - Config 1 (15 bytes) */
	memset(buf, 0, sizeof(buf));
	buf[0] = 0x0a;
	buf[1] = 0x04;
	buf[2] = 0x90;
	buf[3] = 0x03;
	ret = t500rs_send_int(t500rs, buf, 15);
	if (ret) return ret;
	msleep(4);

	/* Report 0x0a - Config 2 (15 bytes) */
	memset(buf, 0, sizeof(buf));
	buf[0] = 0x0a;
	buf[1] = 0x04;
	buf[2] = 0x12;
	buf[3] = 0x10;
	ret = t500rs_send_int(t500rs, buf, 15);
	if (ret) return ret;
	msleep(4);

	/* Report 0x0a - Config 3 (15 bytes) */
	memset(buf, 0, sizeof(buf));
	buf[0] = 0x0a;
	buf[1] = 0x04;
	buf[2] = 0x00;
	buf[3] = 0x06;
	ret = t500rs_send_int(t500rs, buf, 15);
	if (ret) return ret;
	msleep(64);

	/* Report 0x40 (4 bytes) */
	buf[0] = 0x40;
	buf[1] = 0x11;
	buf[2] = 0x55;
	buf[3] = 0xd5;
	ret = t500rs_send_int(t500rs, buf, 4);
	if (ret) return ret;
	msleep(10);

	/* Report 0x42 short (2 bytes) */
	buf[0] = 0x42;
	buf[1] = 0x04;
	ret = t500rs_send_int(t500rs, buf, 2);
	if (ret) return ret;
	msleep(8);

	/* Report 0x40 (4 bytes) */
	buf[0] = 0x40;
	buf[1] = 0x04;
	buf[2] = 0x00;
	buf[3] = 0x00;
	ret = t500rs_send_int(t500rs, buf, 4);
	if (ret) return ret;
	msleep(8);

	/* Report 0x40 (4 bytes) */
	buf[0] = 0x40;
	buf[1] = 0x03;
	buf[2] = 0x0d;
	buf[3] = 0x00;
	ret = t500rs_send_int(t500rs, buf, 4);
	if (ret) return ret;

	hid_info(t500rs->hdev, "Initialization complete!\n");
	return 0;
}

/* Send effect control command (Report 0x41 - 4 bytes) */
static int t500rs_send_control(struct t500rs_simple_entry *t500rs, u8 effect_id, u8 command)
{
	u8 buf[T500RS_BUFFER_CONTROL];

	/* Build Report 0x41 command */
	buf[0] = T500RS_REPORT_CONTROL;  /* 0x41 */
	buf[1] = effect_id;              /* Effect ID (0-15) */
	buf[2] = command;                /* 0x41=start, 0x00=stop */
	buf[3] = 0x01;                   /* Constant */

	hid_info(t500rs->hdev, "Control: effect=%d, cmd=%02x, buffer=[%02x %02x %02x %02x]\n",
		 effect_id, command, buf[0], buf[1], buf[2], buf[3]);

	return t500rs_send_int(t500rs, buf, T500RS_BUFFER_CONTROL);
}

/* Play effect */
static int t500rs_play_effect(void *data, struct tmff2_effect_state *state)
{
	struct t500rs_simple_entry *t500rs = data;

	hid_info(t500rs->hdev, "Playing effect: id=%d, type=%d\n",
		 state->effect.id, state->effect.type);

	/* Send start command (Report 0x41) */
	return t500rs_send_control(t500rs, state->effect.id, T500RS_CMD_START);
}

/* Stop effect */
static int t500rs_stop_effect(void *data, struct tmff2_effect_state *state)
{
	struct t500rs_simple_entry *t500rs = data;

	hid_info(t500rs->hdev, "Stopping effect: id=%d\n", state->effect.id);

	/* Send stop command (Report 0x41) */
	return t500rs_send_control(t500rs, state->effect.id, T500RS_CMD_STOP);
}

/* Upload constant force effect */
static int t500rs_upload_constant(struct t500rs_simple_entry *t500rs,
				   struct tmff2_effect_state *state)
{
	struct ff_effect effect = state->effect;
	u8 buf[15];  /* Now 15 bytes for raw USB */
	int ret;

	hid_info(t500rs->hdev, "Uploading constant effect: id=%d, level=%d\n",
		 effect.id, effect.u.constant.level);

	/* Windows sends THREE reports for each effect: 0x02, 0x04, 0x01 */
	/* Let's send them in the exact order Windows does */

	/* First: Send Report 0x02 (9 bytes) - from Windows frame 252 */
	memset(buf, 0, sizeof(buf));
	buf[0] = 0x02;           /* Report ID */
	buf[1] = 0x38;           /* Parameter (from Windows) */
	buf[2] = 0x00;
	buf[3] = 0x90;
	buf[4] = 0x01;
	buf[5] = 0x00;
	buf[6] = 0x52;
	buf[7] = 0x03;
	buf[8] = 0x00;

	ret = t500rs_send_int(t500rs, buf, 9);
	if (ret) return ret;
	msleep(5);  /* Small delay like Windows */

	/* Second: Send Report 0x04 (8 bytes) - from Windows frame 254 */
	memset(buf, 0, sizeof(buf));
	buf[0] = 0x04;           /* Report ID */
	buf[1] = 0x2a;           /* Parameter (from Windows) */
	buf[2] = 0x00;
	buf[3] = 0x2c;
	buf[4] = 0x00;
	buf[5] = 0x00;
	buf[6] = 0x14;
	buf[7] = 0x00;

	ret = t500rs_send_int(t500rs, buf, 8);
	if (ret) return ret;
	msleep(5);

	/* Third: Send Report 0x01 (15 bytes) - from Windows frame 256 */
	memset(buf, 0, sizeof(buf));
	buf[0] = 0x01;           /* Report ID */
	buf[1] = effect.id;      /* Effect ID */
	buf[2] = 0x22;           /* Effect type */
	buf[3] = 0x40;           /* Parameters from Windows */
	buf[4] = 0xe2;
	buf[5] = 0x04;
	buf[6] = 0x00;
	buf[7] = 0xe8;
	buf[8] = 0x03;
	buf[9] = 0x2a;
	buf[10] = 0x00;
	buf[11] = 0x38;
	buf[12] = 0x00;
	buf[13] = 0x00;
	buf[14] = 0x00;

	return t500rs_send_int(t500rs, buf, 15);  /* Now 15 bytes */
}

/* Upload spring effect (basic implementation) */
static int t500rs_upload_spring(struct t500rs_simple_entry *t500rs,
				 struct tmff2_effect_state *state)
{
	struct ff_effect effect = state->effect;
	u8 buf[15];  /* Now 15 bytes for raw USB */
	s16 coeff;

	/* Use right coefficient as spring strength */
	coeff = effect.u.condition[0].right_coeff / 2;

	hid_info(t500rs->hdev, "Uploading spring effect: id=%d, coeff=%d\n",
		 effect.id, coeff);

	/* Build Report 0x01 - Spring Parameters (15 bytes) */
	memset(buf, 0, sizeof(buf));
	buf[0] = 0x01;           /* Report ID */
	buf[1] = effect.id;      /* Effect ID */
	buf[2] = 0x20;           /* Spring type (from capture) */
	buf[3] = coeff & 0xff;   /* Coefficient low byte */
	buf[4] = (coeff >> 8) & 0xff;  /* Coefficient high byte */
	/* Rest are parameters - using values from Windows capture */
	buf[5] = 0x00;
	buf[6] = 0x00;
	buf[7] = 0x0e;
	buf[8] = 0x06;
	buf[9] = 0x46;
	buf[10] = 0x00;
	buf[11] = 0x54;
	buf[12] = 0x00;
	buf[13] = 0x00;
	buf[14] = 0x00;

	return t500rs_send_int(t500rs, buf, 15);  /* Now 15 bytes */
}

/* Upload effect dispatcher */
static int t500rs_upload_effect(void *data, struct tmff2_effect_state *state)
{
	struct t500rs_simple_entry *t500rs = data;
	
	switch (state->effect.type) {
	case FF_CONSTANT:
		return t500rs_upload_constant(t500rs, state);
	case FF_SPRING:
	case FF_DAMPER:
	case FF_FRICTION:
	case FF_INERTIA:
		return t500rs_upload_spring(t500rs, state);
	default:
		hid_info(t500rs->hdev, "effect type %d not yet implemented\n",
			 state->effect.type);
		return 0; /* Don't fail, just not implemented yet */
	}
}

/* Update effect (reuse upload for now) */
static int t500rs_update_effect(void *data, struct tmff2_effect_state *state)
{
	return t500rs_upload_effect(data, state);
}

/* Set gain */
static int t500rs_set_gain(void *data, uint16_t gain)
{
	struct t500rs_simple_entry *t500rs = data;

	if (!t500rs)
		return -ENODEV;

	hid_info(t500rs->hdev, "Setting gain: %d\n", gain);

	/* TODO: Implement gain control with appropriate report */
	/* For now, just accept it */

	return 0;
}

/* Set autocenter */
static int t500rs_set_autocenter(void *data, uint16_t value)
{
	struct t500rs_simple_entry *t500rs = data;

	if (!t500rs)
		return -ENODEV;

	hid_info(t500rs->hdev, "Setting autocenter: %d\n", value);

	/* TODO: Implement autocenter control with appropriate report */
	/* For now, just accept it */

	return 0;
}

/* Set range (basic implementation) */
static int t500rs_set_range(void *data, uint16_t range)
{
	struct t500rs_simple_entry *t500rs = data;
	
	if (!t500rs)
		return -ENODEV;
	
	/* Range setting not implemented yet */
	hid_info(t500rs->hdev, "range setting not yet implemented\n");
	return 0;
}

/* Send initialization sequence */
static int t500rs_init_device(struct t500rs_simple_entry *t500rs)
{
	/* Call the full Windows initialization sequence (proven to work via libusb) */
	return t500rs_initialize(t500rs);
}

/* Open device */
static int t500rs_open(void *data, int open_mode)
{
	struct t500rs_simple_entry *t500rs = data;

	if (!t500rs)
		return -ENODEV;

	/* Send initialization sequence */
	t500rs_init_device(t500rs);

	return t500rs->open(t500rs->input_dev);
}

/* Close device */
static int t500rs_close(void *data, int open_mode)
{
	struct t500rs_simple_entry *t500rs = data;

	if (!t500rs)
		return -ENODEV;

	t500rs->close(t500rs->input_dev);
	return 0;
}

/* Find INTERRUPT OUT endpoint */
static int t500rs_find_endpoint(struct t500rs_simple_entry *t500rs)
{
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *ep;
	int i;

	interface = t500rs->usbdev->actconfig->interface[0]->cur_altsetting;

	for (i = 0; i < interface->desc.bNumEndpoints; i++) {
		ep = &interface->endpoint[i].desc;
		if (usb_endpoint_is_int_out(ep)) {
			t500rs->ep_out = ep->bEndpointAddress;
			hid_info(t500rs->hdev, "Found INTERRUPT OUT endpoint: 0x%02x\n",
				 t500rs->ep_out);
			return 0;
		}
	}

	hid_err(t500rs->hdev, "No INTERRUPT OUT endpoint found\n");
	return -ENODEV;
}

/* Initialize wheel */
static int t500rs_wheel_init(struct tmff2_device_entry *tmff2, int open_mode)
{
	struct t500rs_simple_entry *t500rs;
	int ret;

	t500rs = kzalloc(sizeof(struct t500rs_simple_entry), GFP_KERNEL);
	if (!t500rs) {
		ret = -ENOMEM;
		goto t500rs_err;
	}

	t500rs->hdev = tmff2->hdev;
	t500rs->input_dev = tmff2->input_dev;
	t500rs->usbdev = to_usb_device(tmff2->hdev->dev.parent->parent);

	/* Find INTERRUPT OUT endpoint for raw USB transfers */
	ret = t500rs_find_endpoint(t500rs);
	if (ret)
		goto t500rs_err;

	/* Allocate send buffer (max 32 bytes per endpoint spec) */
	t500rs->buffer_length = 32;
	t500rs->send_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
	if (!t500rs->send_buffer) {
		ret = -ENOMEM;
		goto t500rs_err;
	}

	/* Save original open/close functions */
	t500rs->open = t500rs->input_dev->open;
	t500rs->close = t500rs->input_dev->close;

	/* Everything went OK */
	tmff2->data = t500rs;
	tmff2->params = t500rs_params;
	tmff2->max_effects = T500RS_MAX_EFFECTS;
	memcpy(tmff2->supported_effects, t500rs_effects, sizeof(t500rs_effects));

	hid_info(t500rs->hdev, "force feedback for T500RS (RAW USB mode)\n");
	return 0;

t500rs_err:
	if (t500rs) {
		kfree(t500rs->send_buffer);
		kfree(t500rs);
	}
	hid_err(tmff2->hdev, "failed initializing T500RS\n");
	return ret;
}

/* Destroy wheel */
static int t500rs_wheel_destroy(void *data)
{
	struct t500rs_simple_entry *t500rs = data;
	
	if (!t500rs)
		return -ENODEV;
	
	kfree(t500rs->send_buffer);
	kfree(t500rs);
	return 0;
}

/* Populate API callbacks */
int t500rs_simple_populate_api(struct tmff2_device_entry *tmff2)
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

