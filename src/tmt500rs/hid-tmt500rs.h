#ifndef HID_TMT500RS_H
#define HID_TMT500RS_H

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

/* Command definitions */
#define T500RS_CMD_SET_RANGE 0x44

/* Device states */
enum t500rs_state {
    T500RS_STATE_INIT,
    T500RS_STATE_INITIALIZING,
    T500RS_STATE_READY,
    T500RS_STATE_ERROR,
    T500RS_STATE_DISCONNECTED,
    T500RS_STATE_RECONNECTING
};

/* Driver data states */
enum t500rs_driver_state {
    T500RS_DRIVER_STATE_INIT = 0,
    T500RS_DRIVER_STATE_READY = 1
};

/* Mode switch states */
enum t500rs_mode_state {
    T500RS_MODE_STATE_INIT,
    T500RS_MODE_STATE_DETECT,
    T500RS_MODE_STATE_SWITCHING,
    T500RS_MODE_STATE_WAIT_DISCONNECT,
    T500RS_MODE_STATE_WAIT_RECONNECT,
    T500RS_MODE_STATE_VERIFY,
    T500RS_MODE_STATE_COMPLETE,
    T500RS_MODE_STATE_ERROR
};

/* Mode switch context */
struct t500rs_mode_switch {
    enum t500rs_mode_state state;
    u8 current_mode;
    u8 target_mode;
    int retries;
    unsigned long last_attempt;
    unsigned long switch_start_time;
    bool force_retry;
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
    struct t500rs_mode_switch *mode_switch;
};

/* Device entry */
struct t500rs_device_entry {
    struct tmff2_device_entry *tmff2;
    struct t500rs_device_data *data;
};

/* Driver initialization/cleanup */
int t500rs_driver_init(void);
void t500rs_driver_exit(void);

/* Function declarations */
int t500rs_init_wheel(struct t500rs_device_entry *t500rs, int open_mode);
int t500rs_wheel_destroy(void *data);
int t500rs_init_usb(struct t500rs_device_entry *t500rs);
void t500rs_cleanup_usb(struct t500rs_device_entry *t500rs);
int t500rs_init_ff(struct t500rs_device_entry *t500rs);
void t500rs_cleanup_ff(struct t500rs_device_entry *t500rs);
int t500rs_set_mode(struct t500rs_device_entry *t500rs, u8 mode);
int t500rs_reset_device(struct t500rs_device_entry *t500rs);
int t500rs_interrupts(struct t500rs_device_data *data);
int t500rs_start_urbs(struct t500rs_device_entry *t500rs);
void t500rs_stop_urbs(struct t500rs_device_entry *t500rs);
int t500rs_send_cmd_with_retry(struct t500rs_device_entry *t500rs, u8 *buf, size_t len, int max_retries);
int t500rs_send_reset_cmd(struct t500rs_device_entry *t500rs);
int t500rs_send_command(struct t500rs_device_entry *t500rs, u8 cmd_type, u8 cmd_id, u8 param);
int t500rs_set_range(void *data, u16 range);
int t500rs_open(void *data, int open_mode);
int t500rs_close(void *data, int open_mode);
int t500rs_wheel_init(struct tmff2_device_entry *tmff2, int open_mode);
__u8 *t500rs_wheel_fixup(struct hid_device *hdev, __u8 *rdesc, unsigned int *rsize);

/* Mode switch functions */
void t500rs_init_mode_switch(struct t500rs_device_entry *t500rs);
void t500rs_cleanup_mode_switch(struct t500rs_device_entry *t500rs);
int t500rs_handle_mode_switch(struct t500rs_device_entry *t500rs);
int t500rs_start_mode_switch(struct t500rs_device_entry *t500rs, u8 target_mode);
int t500rs_detect_mode(struct t500rs_device_entry *t500rs, u8 *mode);
int t500rs_verify_mode(struct t500rs_device_entry *t500rs, u8 expected_mode);
int t500rs_read_response(struct t500rs_device_entry *t500rs, u8 *buf, size_t len);

/* Supported effects */
extern const signed short t500rs_supported_effects[];

#endif /* HID_TMT500RS_H */
