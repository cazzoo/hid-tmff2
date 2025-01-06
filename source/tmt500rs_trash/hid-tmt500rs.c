// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <linux/hid.h>
#include <linux/fixp-arith.h>
#include "../hid-tmff2.h"
#include "hid-tmt500rs.h"

#define T500RS_MAX_EFFECTS 4
#define T500RS_NORM_BUFFER_LENGTH 32

#define T500RS_CMD_HEADER1 0x1b
#define T500RS_CMD_HEADER2 0x00

#define T500RS_EFFECT_SPRING 0x01
#define T500RS_EFFECT_CONSTANT 0x02
#define T500RS_EFFECT_DAMPER 0x03
#define T500RS_EFFECT_FRICTION 0x04

#define T500RS_RANGE_MIN 40
#define T500RS_RANGE_MAX 1080
#define T500RS_GAIN_MAX 0xffff
#define T500RS_AUTOCENTER_MAX 0xffff

static const unsigned long t500rs_params =
	PARAM_SPRING_LEVEL
	| PARAM_DAMPER_LEVEL
	| PARAM_FRICTION_LEVEL
	| PARAM_GAIN
	| PARAM_RANGE
	| PARAM_ALT_MODE;

static const signed short t500rs_effects[] = {
	FF_CONSTANT,
	FF_SPRING,
	FF_DAMPER,
	FF_FRICTION,
	-1
};

static int t500rs_send_buf(struct t500rs_device_entry *t500rs, u8 *send_buffer, size_t len)
{
	int ret, retries = 5;  // Increased retries
	u8 *cmd_buffer;
	size_t cmd_len = 15;  // Fixed size from captured HID report
	struct usb_interface *usbif;
	struct usb_device *usbdev;
	int pipe;

	if (len > cmd_len - 1)  // Reserve one byte for report ID
		return -EINVAL;

	cmd_buffer = kzalloc(cmd_len, GFP_KERNEL);
	if (!cmd_buffer)
		return -ENOMEM;

	cmd_buffer[0] = 0x07;  // Report ID 7
	memcpy(cmd_buffer + 1, send_buffer, len);

	usbif = to_usb_interface(t500rs->hdev->dev.parent);
	usbdev = interface_to_usbdev(usbif);
	pipe = usb_sndintpipe(usbdev, 0x01);  // Use interrupt out endpoint 1

	// Add a longer delay before sending the command
	msleep(100);  // Increased initial delay

	while (retries--) {
		ret = usb_interrupt_msg(usbdev, pipe, cmd_buffer, cmd_len,
				       NULL, 1000);  // 1 second timeout

		if (ret >= 0)
			break;

		hid_err(t500rs->hdev, "usb_interrupt_msg failed with %d, retrying...\n", ret);
		msleep(200);  // Much longer delay between retries
	}

	kfree(cmd_buffer);

	if (ret < 0) {
		hid_err(t500rs->hdev, "USB transfer failed after retries with %d\n", ret);
		return ret;
	}

	// Add a longer delay after sending the command
	msleep(100);  // Increased final delay

	return 0;
}

static int t500rs_send_int(struct t500rs_device_entry *t500rs)
{
	memset(t500rs->send_buffer + 2, 0, t500rs->buffer_length - 2);
	return t500rs_send_buf(t500rs, t500rs->send_buffer + 2, t500rs->buffer_length);
}

static int t500rs_send_open(struct t500rs_device_entry *t500rs)
{
	u8 cmd[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Zeroed header
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Zeroed init
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Zeroed settings
		0x00, 0x00, 0x00, 0x00, 0x00                      // Simple start command
	};
	return t500rs_send_buf(t500rs, cmd, sizeof(cmd));
}

static int t500rs_send_close(struct t500rs_device_entry *t500rs)
{
	u8 cmd[] = {
		0xe0, 0x65, 0x11, 0x88, 0x03, 0xce, 0xff, 0xff,  // Command header
		0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x01,  // Device init
		0x00, 0x02, 0x00, 0x01, 0x01, 0x04, 0x00, 0x00,  // Mode settings
		0x00, 0x41, 0x00, 0x00, 0x01                      // Stop command
	};
	return t500rs_send_buf(t500rs, cmd, sizeof(cmd));
}

static int t500rs_wheel_destroy(void *data)
{
	struct t500rs_device_entry *t500rs = data;

	if (!t500rs)
		return -ENODEV;

	kfree(t500rs->send_buffer);
	kfree(t500rs);
	return 0;
}

static int t500rs_open(void *data, int open_mode)
{
	struct t500rs_device_entry *t500rs = data;
	int ret;

	if (!t500rs)
		return -ENODEV;

	if (open_mode) {
		ret = t500rs_send_open(t500rs);
	if (ret)
	return ret;
}

	return t500rs->open(t500rs->input_dev);
}

static int t500rs_close(void *data, int open_mode)
{
	struct t500rs_device_entry *t500rs = data;
	int ret;

	if (!t500rs)
		return -ENODEV;

	if (open_mode) {
		ret = t500rs_send_close(t500rs);
	if (ret)
	return ret;
}

	t500rs->close(t500rs->input_dev);
	return 0;
}

static int t500rs_wheel_init(struct tmff2_device_entry *tmff2, int open_mode)
{
	struct t500rs_device_entry *t500rs;
	struct list_head *report_list;
	int ret;

	hid_info(tmff2->hdev, "Initializing T500RS wheel\n");

	t500rs = kzalloc(sizeof(struct t500rs_device_entry), GFP_KERNEL);
	if (!t500rs) {
		ret = -ENOMEM;
		goto t500rs_err;
	}

	t500rs->hdev = tmff2->hdev;
	t500rs->input_dev = tmff2->input_dev;
	t500rs->usbdev = interface_to_usbdev(to_usb_interface(t500rs->hdev->dev.parent));
	t500rs->buffer_length = 15;  // Fixed size from captured HID report

	hid_info(t500rs->hdev, "Setting up input device name\n");
	t500rs->input_dev->name = "Thrustmaster T500RS Racing Wheel";

	t500rs->send_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
	if (!t500rs->send_buffer) {
		hid_err(t500rs->hdev, "failed allocating send_buffer\n");
		ret = -ENOMEM;
		goto err_free_t500rs;
	}

	report_list = &t500rs->hdev->report_enum[HID_OUTPUT_REPORT].report_list;
	if (list_empty(report_list)) {
			hid_err(t500rs->hdev, "no output report found\n");
			ret = -ENODEV;
			goto err_free_buffer;
	}

	t500rs->report = list_entry(report_list->next, struct hid_report, list);
	if (!t500rs->report) {
			hid_err(t500rs->hdev, "no report found\n");
			ret = -ENODEV;
			goto err_free_buffer;
	}

	hid_info(t500rs->hdev, "Found output report with id: %d\n", t500rs->report->id);

	t500rs->open = t500rs->input_dev->open;
	t500rs->close = t500rs->input_dev->close;

	hid_info(t500rs->hdev, "Setting up input device capabilities\n");

	// Set up input device capabilities
	__set_bit(EV_KEY, t500rs->input_dev->evbit);
	__set_bit(EV_ABS, t500rs->input_dev->evbit);

	// Set up axes
	__set_bit(ABS_X, t500rs->input_dev->absbit);  // Steering
	__set_bit(ABS_Y, t500rs->input_dev->absbit);  // Accelerator
	__set_bit(ABS_Z, t500rs->input_dev->absbit);  // Brake
	__set_bit(ABS_RZ, t500rs->input_dev->absbit); // Clutch

	// Set up axis ranges
	input_set_abs_params(t500rs->input_dev, ABS_X, -32768, 32767, 0, 0);
	input_set_abs_params(t500rs->input_dev, ABS_Y, -32768, 32767, 0, 0);
	input_set_abs_params(t500rs->input_dev, ABS_Z, -32768, 32767, 0, 0);
	input_set_abs_params(t500rs->input_dev, ABS_RZ, -32768, 32767, 0, 0);

	// Set up buttons
	for (int i = 0; i < 13; i++)  // T500RS has 13 buttons
		__set_bit(BTN_TRIGGER + i, t500rs->input_dev->keybit);

	hid_info(t500rs->hdev, "Initializing device with proper delays\n");

	// Initialize the device with proper delays
	msleep(1000);  // Initial delay for USB stability

	// Send a minimal initialization sequence
	if (!open_mode) {
		u8 init_cmd[] = {
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Zeroed header
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00               // Zeroed init
		};

		hid_info(t500rs->hdev, "Sending initialization command\n");
		ret = t500rs_send_buf(t500rs, init_cmd, sizeof(init_cmd));
		if (ret < 0) {
			hid_err(tmff2->hdev, "failed to initialize device: %d\n", ret);
			goto err_free_buffer;
		}

		msleep(500);  // Wait for device to stabilize
	}

	tmff2->data = t500rs;
	tmff2->params = t500rs_params;
	tmff2->max_effects = T500RS_MAX_EFFECTS;
	memcpy(tmff2->supported_effects, t500rs_effects, sizeof(t500rs_effects));

	hid_info(t500rs->hdev, "force feedback for T500RS initialized\n");
	return 0;

err_free_buffer:
	kfree(t500rs->send_buffer);
err_free_t500rs:
	kfree(t500rs);
t500rs_err:
	hid_err(tmff2->hdev, "failed initializing T500RS\n");
	return ret;
}

int t500rs_play_effect(void *data, struct tmff2_effect_state *state)
{
	struct t500rs_device_entry *t500rs = data;
	struct t500rs_packet_header *header = (struct t500rs_packet_header *)t500rs->send_buffer;

	header->cmd = 0x01;
	header->id = state->effect.id;
	header->flags = state->flags & FF_EFFECT_PLAYING ? 0x01 : 0x00;

	return t500rs_send_int(t500rs);
}

int t500rs_stop_effect(void *data, struct tmff2_effect_state *state)
{
	struct t500rs_device_entry *t500rs = data;
	struct t500rs_packet_header *header = (struct t500rs_packet_header *)t500rs->send_buffer;

	header->cmd = 0x01;
	header->id = state->effect.id;
	header->flags = 0x00;

	return t500rs_send_int(t500rs);
}

int t500rs_upload_effect(void *data, struct tmff2_effect_state *state)
{
	struct t500rs_device_entry *t500rs = data;
	struct t500rs_packet_header *header = (struct t500rs_packet_header *)t500rs->send_buffer;

	header->cmd = 0x02;
	header->id = state->effect.id;
	header->flags = 0x00;

	switch (state->effect.type) {
		case FF_CONSTANT:
		header->type = T500RS_EFFECT_CONSTANT;
		header->level = state->effect.u.constant.level;
		break;
		case FF_SPRING:
		header->type = T500RS_EFFECT_SPRING;
		break;
		case FF_DAMPER:
		header->type = T500RS_EFFECT_DAMPER;
		break;
		case FF_FRICTION:
		header->type = T500RS_EFFECT_FRICTION;
		break;
		default:
		return -EINVAL;
	}

	return t500rs_send_int(t500rs);
}

int t500rs_update_effect(void *data, struct tmff2_effect_state *state)
{
	return t500rs_upload_effect(data, state);
}

int t500rs_set_range(void *data, uint16_t value)
{
	struct t500rs_device_entry *t500rs = data;
	struct t500rs_packet_header *header = (struct t500rs_packet_header *)t500rs->send_buffer;

	if (value < T500RS_RANGE_MIN) {
		hid_info(t500rs->hdev, "value %i too small, clamping to %i\n", value, T500RS_RANGE_MIN);
		value = T500RS_RANGE_MIN;
	}

	if (value > T500RS_RANGE_MAX) {
		hid_info(t500rs->hdev, "value %i too large, clamping to %i\n", value, T500RS_RANGE_MAX);
		value = T500RS_RANGE_MAX;
	}

	header->cmd = 0x03;
	header->id = 0x00;
	header->flags = 0x00;
	header->range = value;

	return t500rs_send_int(t500rs);
}

int t500rs_set_gain(void *data, uint16_t gain)
{
	struct t500rs_device_entry *t500rs = data;
	struct t500rs_packet_header *header = (struct t500rs_packet_header *)t500rs->send_buffer;

	if (gain > T500RS_GAIN_MAX)
		gain = T500RS_GAIN_MAX;

	header->cmd = 0x04;
	header->id = 0x00;
	header->flags = 0x00;
	header->gain = gain;

	return t500rs_send_int(t500rs);
}

int t500rs_set_autocenter(void *data, uint16_t value)
{
	struct t500rs_device_entry *t500rs = data;
	struct t500rs_packet_header *header = (struct t500rs_packet_header *)t500rs->send_buffer;

	if (value > T500RS_AUTOCENTER_MAX)
		value = T500RS_AUTOCENTER_MAX;

	header->cmd = 0x05;
	header->id = 0x00;
	header->flags = 0x00;
	header->autocenter = value;

	return t500rs_send_int(t500rs);
}

int t500rs_populate_api(struct tmff2_device_entry *tmff2)
{
	tmff2->data = NULL;
	tmff2->params = t500rs_params;
	tmff2->max_effects = T500RS_MAX_EFFECTS;
	memcpy(tmff2->supported_effects, t500rs_effects, sizeof(t500rs_effects));

	tmff2->wheel_init = t500rs_wheel_init;
	tmff2->wheel_destroy = t500rs_wheel_destroy;
	tmff2->play_effect = t500rs_play_effect;
	tmff2->stop_effect = t500rs_stop_effect;
	tmff2->upload_effect = t500rs_upload_effect;
	tmff2->update_effect = t500rs_update_effect;
	tmff2->set_gain = t500rs_set_gain;
	tmff2->set_autocenter = t500rs_set_autocenter;
	tmff2->set_range = t500rs_set_range;
	tmff2->open = t500rs_open;
	tmff2->close = t500rs_close;

	return 0;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("T500RS Driver Module");
