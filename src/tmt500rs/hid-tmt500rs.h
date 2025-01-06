#ifndef __HID_TMT500RS_H
#define __HID_TMT500RS_H

#include "../hid-tmff2.h"

/* Buffer and effect limits */
#define TMT500RS_BUFFER_LENGTH 64
#define TMT500RS_MAX_EFFECTS 16
#define T500RS_MAX_RETRIES 3

/* Logging macros */
#define t500rs_err(t500rs, fmt, args...) \
    hid_err((t500rs)->hdev, "T500RS Error: " fmt, ##args)

#define t500rs_info(t500rs, fmt, args...) \
    hid_info((t500rs)->hdev, "T500RS: " fmt, ##args)

#define t500rs_dbg(t500rs, fmt, args...) \
    hid_dbg((t500rs)->hdev, "T500RS Debug: " fmt, ##args)

#define t500rs_warn(t500rs, fmt, args...) \
    hid_warn((t500rs)->hdev, "T500RS Warning: " fmt, ##args)

/* Command validation and tracking */
struct t500rs_command_info {
    u8 cmd;
    u8 id;
    const char *description;
    size_t min_length;
    size_t max_length;
    bool requires_response;
};

/* Error codes */
#define T500RS_SUCCESS          0
#define T500RS_ERROR_TIMEOUT   -1
#define T500RS_ERROR_PROTO     -2
#define T500RS_ERROR_STALL     -3
#define T500RS_ERROR_DISCONNECT -4
#define T500RS_ERROR_INVALID   -5

static const struct t500rs_command_info t500rs_commands[] = {
    { 0x41, 0x03, "Mode command",         8, 8, true  },
    { 0x42, 0x01, "Init command",         8, 8, true  },
    { 0x08, 0x00, "Open command",         8, 8, false },
    { 0x08, 0x01, "Enable interrupts",    8, 8, false },
    { 0x08, 0x03, "Effect control",       8, 8, false },
    { 0x08, 0x04, "Upload effect",        8, 8, false },
    { 0x08, 0x11, "Set range",           8, 8, false },
    { 0x41, 0x04, "Set gain",            8, 8, false },
    { 0x41, 0x05, "Set autocenter",      8, 8, false }
};

struct t500rs_packet_header {
    u8 cmd;
    u8 id;
    union {
        u16 gain;
        u16 autocenter;
        u16 range;
        u8 data[62];  // Rest of the buffer for other commands
    };
} __packed;

enum t500rs_device_state {
    T500RS_STATE_DISCONNECTED,
    T500RS_STATE_INITIALIZING,
    T500RS_STATE_SWITCHING_MODE,
    T500RS_STATE_READY,
    T500RS_STATE_ERROR
};

struct t500rs_device_entry {
    struct hid_device *hdev;
    struct input_dev *input_dev;
    struct hid_report *report;
    struct hid_field *ff_field;
    struct usb_device *usbdev;

    int (*open)(struct input_dev *dev);
    void (*close)(struct input_dev *dev);

    u8 endpoint_in;
    u8 endpoint_out;
    u8 buffer_length;
    u8 *send_buffer;
    
    /* Device state tracking */
    enum t500rs_device_state state;
    unsigned long last_command_time;
    int command_retries;
    bool force_feedback_enabled;
    u16 current_gain;
    u16 current_range;
};

// Function declarations
int t500rs_populate_api(struct tmff2_device_entry *tmff2);
int t500rs_send_buf(struct t500rs_device_entry *t500rs, u8 *send_buffer, size_t len);
int t500rs_set_gain(void *data, uint16_t gain);
int t500rs_set_range(void *data, uint16_t range);
int t500rs_set_autocenter(void *data, uint16_t value);
int t500rs_open(struct input_dev *dev);
void t500rs_close(struct input_dev *dev);
void t500rs_set_state(struct t500rs_device_entry *t500rs, enum t500rs_device_state state);
const char *t500rs_get_command_description(u8 cmd, u8 id);
bool t500rs_state_allows_command(struct t500rs_device_entry *t500rs, u8 cmd, u8 id);
bool t500rs_validate_command(u8 cmd, u8 id, size_t len);
const char *t500rs_error_to_string(int error);
const char *t500rs_state_to_string(enum t500rs_device_state state);

#endif
