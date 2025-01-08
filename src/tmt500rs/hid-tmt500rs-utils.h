#ifndef __HID_TMT500RS_UTILS_H
#define __HID_TMT500RS_UTILS_H

#include <linux/kernel.h>
#include "hid-tmt500rs.h"

/* Command definitions */
static const struct t500rs_command_info t500rs_commands[] = {
    { 0x41, 0x03, "Initialize", 8, 8, true },
    { 0x42, 0x01, "Switch Mode", 8, 8, true },
    { 0x43, 0x01, "Set Force", 8, 8, false },
    { 0x44, 0x01, "Set Range", 8, 8, false },
    { 0x45, 0x01, "Set Gain", 8, 8, false },
    { 0x46, 0x01, "Set Autocenter", 8, 8, false },
    { 0x47, 0x01, "Stop Effect", 8, 8, false },
    { 0x48, 0x01, "Play Effect", 8, 8, false },
    { 0x49, 0x01, "Update Effect", 8, 8, false },
    { 0x4A, 0x01, "Upload Effect", 8, 8, false },
};

static inline void t500rs_info(struct t500rs_device_entry *t500rs, const char *fmt, ...)
{
    struct va_format vaf;
    va_list args;

    va_start(args, fmt);
    vaf.fmt = fmt;
    vaf.va = &args;
    hid_info(t500rs->hdev, "%pV", &vaf);
    va_end(args);
}

static inline const char *t500rs_state_to_string(enum t500rs_device_state state)
{
    switch (state) {
    case T500RS_STATE_DISCONNECTED:  return "DISCONNECTED";
    case T500RS_STATE_INITIALIZING:  return "INITIALIZING";
    case T500RS_STATE_SWITCHING_MODE: return "SWITCHING_MODE";
    case T500RS_STATE_READY:         return "READY";
    case T500RS_STATE_ERROR:         return "ERROR";
    default:                         return "UNKNOWN";
    }
}

static inline const char *t500rs_error_to_string(int error)
{
    switch (error) {
    case T500RS_SUCCESS:         return "Success";
    case T500RS_ERROR_TIMEOUT:   return "Command timeout";
    case T500RS_ERROR_PROTO:     return "Protocol error";
    case T500RS_ERROR_STALL:     return "Endpoint stalled";
    case T500RS_ERROR_DISCONNECT: return "Device disconnected";
    case T500RS_ERROR_INVALID:   return "Invalid command";
    default:                     return "Unknown error";
    }
}

static inline bool t500rs_validate_command(u8 cmd, u8 id, size_t len)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(t500rs_commands); i++) {
        if (t500rs_commands[i].cmd == cmd && t500rs_commands[i].id == id) {
            return len >= t500rs_commands[i].min_length && 
                   len <= t500rs_commands[i].max_length;
        }
    }
    return false;
}

static inline bool t500rs_state_allows_command(struct t500rs_device_entry *t500rs, u8 cmd, u8 id)
{
    switch (t500rs->state) {
    case T500RS_STATE_INITIALIZING:
        return true;
    case T500RS_STATE_READY:
        return !(cmd == 0x42 || (cmd == 0x41 && id == 0x03));
    case T500RS_STATE_SWITCHING_MODE:
        return false;
    case T500RS_STATE_ERROR:
        return false;
    default:
        return false;
    }
}

static inline void t500rs_set_state(struct t500rs_device_entry *t500rs, 
                           enum t500rs_device_state new_state)
{
    enum t500rs_device_state old_state = t500rs->state;
    t500rs->state = new_state;
    
    t500rs_info(t500rs, "State changed: %s -> %s\n",
                t500rs_state_to_string(old_state),
                t500rs_state_to_string(new_state));
}

#endif /* __HID_TMT500RS_UTILS_H */ 