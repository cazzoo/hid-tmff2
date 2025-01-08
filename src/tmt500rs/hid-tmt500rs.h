#ifndef __HID_TMT500RS_H
#define __HID_TMT500RS_H

#include <linux/hid.h>
#include <linux/input.h>
#include <linux/usb.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include "../hid-tmff2.h"

#define T500RS_REPORT_LENGTH 64
#define T500RS_FF_LENGTH 4
#define T500RS_MAX_EFFECTS 16

/* Device state */
enum t500rs_device_state {
    T500RS_STATE_DISCONNECTED,
    T500RS_STATE_INITIALIZING,
    T500RS_STATE_SWITCHING_MODE,
    T500RS_STATE_READY,
    T500RS_STATE_ERROR
};

/* Command info structure */
struct t500rs_command_info {
    u8 cmd;
    u8 id;
    const char *name;
    size_t min_length;
    size_t max_length;
    bool requires_response;
};

/* T500RS specific device structure */
struct t500rs_device_entry {
    struct hid_device *hdev;
    struct input_dev *input_dev;
    struct usb_device *usbdev;
    struct tmff2_device_entry *tmff2;
    struct hid_report *report;
    struct hid_field *ff_field;
    
    /* Device state */
    enum t500rs_device_state state;
    
    /* USB communication */
    struct urb *urb;
    u8 *buffer;
    dma_addr_t buffer_dma;
    size_t buffer_length;
    spinlock_t lock;
    struct completion response_completion;
    bool waiting_for_response;
    int retry_count;
    u8 endpoint_out;
    u8 interval;
};

/* Function declarations */
int t500rs_upload_effect(void *data, struct tmff2_effect_state *effect);
int t500rs_play_effect(void *data, struct tmff2_effect_state *effect);
int t500rs_set_gain(void *data, uint16_t gain);
int t500rs_set_autocenter(void *data, uint16_t autocenter);
int t500rs_set_range(void *data, uint16_t range);
__u8 *t500rs_wheel_fixup(struct hid_device *hdev, __u8 *rdesc, unsigned int *rsize);
int t500rs_open(void *data, int open_mode);
int t500rs_close(void *data, int open_mode);
int t500rs_wheel_init(struct tmff2_device_entry *tmff2, int open_mode);
int t500rs_wheel_destroy(void *data);

/* USB protocol constants */
#define T500RS_INPUT_REPORT_ID 0x07
#define T500RS_FF_REPORT_ID 0x03

/* Error codes */
#define T500RS_SUCCESS 0
#define T500RS_ERROR_TIMEOUT -1
#define T500RS_ERROR_PROTO -2
#define T500RS_ERROR_STALL -3
#define T500RS_ERROR_DISCONNECT -4
#define T500RS_ERROR_INVALID -5

/* Effect types */
extern const signed short t500rs_supported_effects[];

/* USB communication functions */
int t500rs_send_command(struct t500rs_device_entry *t500rs, u8 cmd_id, u8 param1, u8 param2);
int t500rs_send_buf(struct t500rs_device_entry *t500rs, const u8 *buf, size_t len);
int t500rs_send_int(struct t500rs_device_entry *t500rs, u8 cmd, u8 id);
int t500rs_interrupts(struct t500rs_device_entry *t500rs);

#endif /* __HID_TMT500RS_H */
