#ifndef __HID_TMT500RS_H
#define __HID_TMT500RS_H

#include <linux/hid.h>
#include <linux/usb.h>
#include <linux/spinlock.h>
#include <linux/completion.h>

/* Device modes */
#define TMT500RS_MODE_WHEEL 0x01
#define TMT500RS_MODE_JOYSTICK 0x02

/* Report length */
#define T500RS_REPORT_LENGTH 8

/* Maximum effects */
#define T500RS_MAX_EFFECTS 16

/* Device states */
enum t500rs_state {
    T500RS_STATE_INIT,
    T500RS_STATE_INITIALIZING,
    T500RS_STATE_READY,
    T500RS_STATE_ERROR,
    T500RS_STATE_DISCONNECTED
};

/* Device data */
struct t500rs_device_data {
    struct hid_device *hdev;
    struct usb_device *usbdev;
    struct input_dev *input_dev;
    struct urb *urb;
    u8 *buffer;
    dma_addr_t buffer_dma;
    int interval;
    enum t500rs_state state;
    bool initialized;
    spinlock_t lock;
    struct completion response_completion;
    bool waiting_for_response;
    int retry_count;
    struct hid_report *report;
    struct hid_field *ff_field;
};

/* Device entry */
struct t500rs_device_entry {
    struct tmff2_device_entry *tmff2;
    struct t500rs_device_data *data;
};

/* Function declarations */
int t500rs_init_wheel(struct t500rs_device_entry *t500rs, int open_mode);
void t500rs_wheel_destroy(struct t500rs_device_entry *t500rs);
int t500rs_init_usb(struct t500rs_device_entry *t500rs);
void t500rs_cleanup_usb(struct t500rs_device_entry *t500rs);
int t500rs_init_ff(struct t500rs_device_entry *t500rs);
void t500rs_cleanup_ff(struct t500rs_device_entry *t500rs);
int t500rs_set_mode(struct t500rs_device_entry *t500rs, u8 mode);
int t500rs_reset_device(struct t500rs_device_entry *t500rs);
int t500rs_interrupts(struct t500rs_device_entry *t500rs);
int t500rs_start_urbs(struct t500rs_device_entry *t500rs);
void t500rs_stop_urbs(struct t500rs_device_entry *t500rs);
int t500rs_send_cmd_with_retry(struct t500rs_device_entry *t500rs, u8 *buf, size_t len, int max_retries);
int t500rs_send_reset_cmd(struct t500rs_device_entry *t500rs);

/* Supported effects */
extern const signed short t500rs_supported_effects[];

#endif /* __HID_TMT500RS_H */
