// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Force feedback support for Thrustmaster T500RS
 *
 * USB INTERRUPT implementation using endpoint 0x01 OUT for all communication.
 *
 * Protocol documentation: captures/T500RS_USB_Protocol_Analysis.md
 *
 * Key protocol details:
 * - 0x01 packet: Main upload (15 bytes) - effect_id, direction, duration, delay, code1/2
 * - 0x02 packet: Envelope (9 bytes) - attack/fade levels and times
 * - 0x03 packet: Constant force level (4 bytes)
 * - 0x04 packet: Periodic/Ramp parameters (8 bytes) - code 0x2a, period in ms
 * - 0x05 packet: Conditional parameters (11 bytes) - two packets per effect (X/Y)
 * - 0x41 packet: START/STOP command (4 bytes) - per-effect hw_id
 *
 * Hardware supports 16 concurrent effects with internal mixing.
 */

#include <linux/hid.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include "../hid-tmff2.h"

/* T500RS Constants */
#define T500RS_MAX_EFFECTS 16
#define T500RS_MAX_HW_EFFECTS T500RS_MAX_EFFECTS
#define T500RS_BUFFER_LENGTH 32 /* USB endpoint max packet size */
#define T500RS_EP_OUT 0x01      /* INTERRUPT OUT endpoint */

/* USB timeout */
#define T500RS_USB_TIMEOUT 1000 /* 1 second */

/* Gain scaling */
#define GAIN_MAX 65535

/*
 * Protocol-accurate packet structs (to be wired in by later refactor phases).
 * These mirror the Windows T500RS USB protocol documentation and are kept
 * separate from the legacy structs currently used by the upload paths.
 */

/* 0x01 - Main upload (15 bytes)
 *
 * Per Windows captures (analysis.json):
 * - Constant: 01 00 00 40 d0 07 00 00 00 0e 00 1c 00 00 00
 * - Sine:     01 00 22 40 d0 07 00 00 00 2a 00 38 00 00 00
 * - Spring:   01 00 40 40 d0 07 00 00 00 2a 00 38 00 00 00
 *
 * Structure:
 * - b0: packet_type = 0x01
 * - b1: effect_id (always 0x00 on T500RS)
 * - b2: effect_type (0x00=const, 0x22=sine, 0x40=all conditionals)
 * - b3: control (always 0x40)
 * - b4-b5: duration_ms (LE)
 * - b6-b7: delay_ms (LE)
 * - b8: reserved (0x00)
 * - b9-b10: code1/param_subtype (LE)
 * - b11-b12: code2/env_subtype (LE)
 * - b13-b14: reserved (0x00)
 *
 * NOTE: Direction is NOT in this packet!
 */
struct t500rs_pkt_r01_main {
	u8 id;             /* b0: 0x01 */
	u8 effect_id;      /* b1: always 0x00 on T500RS */
	u8 effect_type;    /* b2: 0x00=const, 0x22=sine, 0x40=all conditionals */
	u8 control;        /* b3: always 0x40 */
	__le16 duration_ms;/* b4-b5: duration in ms */
	__le16 delay_ms;   /* b6-b7: delay in ms */
	u8 reserved1;      /* b8: 0x00 */
	__le16 code1;      /* b9-b10: param subtype (0x000e, 0x002a, etc.) */
	__le16 code2;      /* b11-b12: env subtype (0x001c, 0x0038, etc.) */
	__le16 reserved2;  /* b13-b14: 0x0000 */
} __packed;

/* 0x04 - Periodic / Ramp parameters (8 bytes) */
/* 0x04 packet byte order per Windows USB captures:
 * Example: 04 2a 00 06 00 3f 0a 00 (sine, mag=6, phase=63, period=10ms)
 *   b0=0x04 id
 *   b1=0x2a code
 *   b2=0x00 reserved1 (always zero in captures)
 *   b3=0x06 magnitude
 *   b4=0x00 offset
 *   b5=0x3f phase
 *   b6-b7=0x000a period (little-endian)
 *
 * NOTE: The protocol doc had the wrong byte order. This struct matches
 * actual Windows USB captures where period is at bytes 6-7, not 5-6.
 */
struct t500rs_pkt_r04_periodic_ramp {
	u8 id;            /* b0: 0x04 */
	u8 code;          /* b1: subtype code (0x2a for periodic/ramp) */
	u8 reserved1;     /* b2: always 0x00 */
	u8 magnitude;     /* b3: 0..127 magnitude */
	u8 offset;        /* b4: signed -127..+127 offset */
	u8 phase;         /* b5: 0..255 phase (0-360 degrees) */
	__le16 period_ms; /* b6-b7: period in milliseconds */
} __packed;

/* 0x05 - Conditional parameters (11 bytes) */
struct t500rs_pkt_r05_condition {
	u8 id;     /* 0x05 */
	u8 code;   /* from 0x01 code1/code2 (subtype low byte) */
	__le16 right_coeff;
	__le16 left_coeff;
	__le16 deadband;
	u8 center;
	u8 right_sat;
	u8 left_sat;
} __packed;

/* Packet structs still in use */
struct t500rs_r03_const {
  u8 id;    /* 0x03 */
  u8 code;  /* 0x0e */
  u8 zero;  /* 0x00 */
  s8 level; /* -127..127 */
} __packed;

struct t500rs_r41_cmd {
  u8 id;        /* 0x41 */
  u8 effect_id; /* usually 0 on T500RS */
  u8 command;   /* 0x41 START, 0x00 STOP, 0x00 clear in init */
  u8 arg;       /* 0x01 */
} __packed;

/* Scale constant level (-32767..32767) to signed 8-bit (-127..127) */
static inline s8 t500rs_scale_const_level_s8(int level) {
  if (level > 32767)
    level = 32767;
  if (level < -32767)
    level = -32767;
  return (s8)((level * 127) / 32767);
}

/* Apply effect direction to a constant level and convert to s8.
 * Mirrors t300rs_calculate_constant_level()'s projection semantics but
 * keeps the full T500RS range and uses t500rs_scale_const_level_s8() for
 * clamping and conversion.
 */
static inline s8 t500rs_scale_const_with_direction(int level, u16 direction) {
  int projected;

  projected = (level * fixp_sin16(direction * 360 / 0x10000)) / 0x7fff;

  return t500rs_scale_const_level_s8(projected);
}

/* Scale magnitude (0..32767 or signed) to 7-bit (0..127) */
static inline u8 t500rs_scale_mag_u7(int magnitude) {
  if (magnitude < 0)
    magnitude = -magnitude;
  if (magnitude > 32767)
    magnitude = 32767;
  return (u8)((magnitude * 127) / 32767);
}

/* Map logical effect index to parameter/envelope subtypes as per protocol:
 *  param_sub = 0x000e + 0x001c * idx
 *  env_sub   = 0x001c + 0x001c * idx
 * idx is wrapped to the hardware limit of 16 effect slots.
 */
static inline void
t500rs_index_to_subtypes(unsigned int idx, u16 *param_sub, u16 *env_sub) {
  idx %= T500RS_MAX_HW_EFFECTS;
  *param_sub = 0x000e + 0x001c * idx;
  *env_sub = 0x001c + 0x001c * idx;
}

/* Debug logging helper: pass struct t500rs_device_entry * explicitly */
#define T500RS_DBG(dev, fmt, ...) hid_dbg((dev)->hdev, fmt, ##__VA_ARGS__)

/* T500RS device data */
struct t500rs_device_entry {
  struct hid_device *hdev;
  struct input_dev *input_dev;
  struct usb_device *usbdev;
  struct usb_interface *usbif;

  int ep_out; /* INTERRUPT OUT endpoint address */

  u8 *send_buffer;
  size_t buffer_length;

  /* Current wheel range for smooth transitions */
  u16 current_range; /* Current rotation range in degrees */

  /*
   * Hardware effect ID management (Phase 3 of USB refactor).
   *
   * T500RS hardware supports up to 16 simultaneous effects with internal
   * mixing. The Windows driver assigns unique hardware effect IDs (0..15)
   * for concurrent effects and tracks them per logical effect slot.
   *
   * hw_id[logical_id] = hardware effect ID assigned to that logical slot
   * hw_id_in_use[hw_slot] = true if that hardware slot is currently in use
   * hw_id_owner[hw_slot] = logical_id that owns this hw slot (valid only if in_use)
   *
   * These are wired in by later refactor phases; for now they are populated
   * but legacy paths continue to use effect_id=0 for all effects.
   */
  u16 hw_id[T500RS_MAX_EFFECTS];
  bool hw_id_in_use[T500RS_MAX_HW_EFFECTS];
  u16 hw_id_owner[T500RS_MAX_HW_EFFECTS]; /* Which logical_id owns each hw slot */
};

/*
 * Allocate a hardware effect ID for the given logical effect id.
 *
 * Per Windows USB captures, the T500RS device has specific expectations:
 * - Index 0 (subtypes 0x0e/0x1c) is ONLY valid for constant effects (0x03 packets)
 * - Indices 1+ (subtypes 0x2a/0x38, etc.) are valid for all effect types
 * - Periodic/ramp effects (0x04 packets) sent with index 0 subtypes cause EPROTO
 *
 * The skip_index_zero parameter should be true for periodic, ramp, and conditional
 * effects to avoid the firmware rejecting the upload.
 *
 * Returns the hardware ID (0..15) on success, or -ENOSPC if all slots are used.
 */
static int t500rs_alloc_hw_id(struct t500rs_device_entry *t500rs,
                              unsigned int logical_id,
                              bool skip_index_zero) {
  unsigned int i;
  unsigned int start_idx = skip_index_zero ? 1 : 0;
  unsigned int current_hw_id;

  if (logical_id >= T500RS_MAX_EFFECTS)
    return -EINVAL;

  /* Check if this logical_id already owns a hw slot */
  current_hw_id = t500rs->hw_id[logical_id];
  if (current_hw_id < T500RS_MAX_HW_EFFECTS &&
      t500rs->hw_id_in_use[current_hw_id] &&
      t500rs->hw_id_owner[current_hw_id] == logical_id) {
    /* This logical_id already owns this slot */
    return (int)current_hw_id;
  }

  /* Find a free hardware slot, starting from start_idx */
  for (i = start_idx; i < T500RS_MAX_HW_EFFECTS; i++) {
    if (!t500rs->hw_id_in_use[i]) {
      t500rs->hw_id[logical_id] = (u16)i;
      t500rs->hw_id_in_use[i] = true;
      t500rs->hw_id_owner[i] = (u16)logical_id;
      return (int)i;
    }
  }
  return -ENOSPC;
}

/*
 * Get the hardware effect ID for the given logical effect id.
 * Allocates a new slot if one is not yet assigned.
 * Returns the hardware ID (0..15) on success, or negative error.
 */
static int t500rs_get_hw_id(struct t500rs_device_entry *t500rs,
                            unsigned int logical_id) {
  if (logical_id >= T500RS_MAX_EFFECTS)
    return -EINVAL;

  /* If not yet allocated, allocate now.
   * Note: This wrapper doesn't know the effect type, so it defaults to
   * skip_index_zero=true for safety. Direct callers should use
   * t500rs_alloc_hw_id() with the appropriate skip_index_zero value.
   */
  if (!t500rs->hw_id_in_use[t500rs->hw_id[logical_id]])
    return t500rs_alloc_hw_id(t500rs, logical_id, true);

  return (int)t500rs->hw_id[logical_id];
}

/*
 * Free the hardware effect ID for the given logical effect id.
 * Called from stop_effect path if we want to recycle slots.
 */
static void t500rs_free_hw_id(struct t500rs_device_entry *t500rs,
                              unsigned int logical_id) {
  u16 hw_slot;
  if (logical_id >= T500RS_MAX_EFFECTS)
    return;

  hw_slot = t500rs->hw_id[logical_id];
  if (hw_slot < T500RS_MAX_HW_EFFECTS &&
      t500rs->hw_id_owner[hw_slot] == logical_id) {
    t500rs->hw_id_in_use[hw_slot] = false;
    t500rs->hw_id_owner[hw_slot] = 0xFFFF; /* Mark as unowned */
  }
}

/*
 * Scale direction from Linux ff_effect format to T500RS protocol format.
 *
 * Linux ff_effect.direction: 0-65535 (0 = forward, 16384 = right, 32768 = back, 49152 = left)
 * T500RS protocol: 0-35999 in 0.01 degree units (0 = 0°, 9000 = 90°, 18000 = 180°, etc.)
 *
 * Conversion: device_dir = (linux_dir * 36000) / 65536
 * This maps 0-65535 → 0-35999 (approximately, since 65535 → 35999.45)
 */
static inline u16 t500rs_scale_direction(u16 linux_dir) {
  /* Use 32-bit arithmetic to avoid overflow */
  return (u16)(((u32)linux_dir * 36000) / 65536);
}

/*
 * Build a protocol-accurate 0x01 main upload packet.
 *
 * Per the T500RS USB protocol documentation:
 * - effect_id: 16-bit LE hardware effect slot (0..15 for now)
 * - direction: 0..35999 in 0.01 degree units (already scaled, use t500rs_scale_direction)
 * - duration_ms: duration in milliseconds
 * - delay_ms: delay before effect starts
 * - code1: parameter subtype (used by 0x03/0x04/0x05)
 * - code2: envelope subtype (used by 0x02), or second conditional subtype
 *
 * Per Windows captures, effect_type values are:
 * - 0x00 = Constant
 * - 0x22 = Sine
 * - 0x21 = Triangle (inferred)
 * - 0x23 = Sawtooth Up (inferred)
 * - 0x24 = Sawtooth Down / Ramp (inferred)
 * - 0x40 = Spring
 * - 0x41 = Damper/Friction/Inertia
 *
 * NOTE: Direction is NOT in the 0x01 packet on T500RS!
 */
static void t500rs_build_r01_main(struct t500rs_pkt_r01_main *p,
                                  u8 effect_type,
                                  u16 duration_ms,
                                  u16 delay_ms,
                                  u16 code1,
                                  u16 code2) {
  memset(p, 0, sizeof(*p));
  p->id = 0x01;
  p->effect_id = 0x00;           /* Always 0x00 on T500RS */
  p->effect_type = effect_type;  /* 0x00=const, 0x22=sine, 0x40=spring, etc. */
  p->control = 0x40;             /* Always 0x40 per Windows captures */
  p->duration_ms = cpu_to_le16(duration_ms);
  p->delay_ms = cpu_to_le16(delay_ms);
  p->reserved1 = 0;
  p->code1 = cpu_to_le16(code1);
  p->code2 = cpu_to_le16(code2);
  p->reserved2 = 0;
}

/*
 * Build a protocol-accurate 0x04 periodic/ramp packet.
 *
 * Per the T500RS USB protocol documentation:
 * - code: low byte of param_subtype from 0x01 (e.g., 0x2a for periodic, not 0x0e!)
 * - magnitude: 0..127 (scaled from SDL's 0..32767)
 * - offset: signed DC offset (scaled from SDL's -32768..32767 to device range)
 * - phase: 0..255 (256 steps for 360°, scaled from SDL's 0..35999)
 * - period_ms: period in MILLISECONDS (no Hz×100 conversion!)
 * - reserved: always 0
 *
 * Scaling formulas (from protocol doc):
 *   device_mag   = sdl_mag * 127 / 32767
 *   device_phase = (sdl_phase * 256 / 36000) & 0xFF
 *   device_offset = sdl_offset / 256  (approximate, TBD based on testing)
 *   period_ms    = direct copy (no frequency conversion)
 */
static void t500rs_build_r04_periodic(struct t500rs_pkt_r04_periodic_ramp *p,
                                      u8 code,
                                      u8 magnitude,
                                      s8 offset,
                                      u8 phase,
                                      u16 period_ms) {
  /* Byte order per Windows USB captures (example: 04 2a 00 06 00 3f 0a 00):
   *   b0=0x04, b1=code, b2=reserved1, b3=mag, b4=offset, b5=phase, b6-b7=period
   */
  memset(p, 0, sizeof(*p));
  p->id = 0x04;              /* b0 */
  p->code = code;            /* b1 */
  p->reserved1 = 0;          /* b2: always 0x00 */
  p->magnitude = magnitude;  /* b3 */
  p->offset = (u8)offset;    /* b4 */
  p->phase = phase;          /* b5 */
  p->period_ms = cpu_to_le16(period_ms); /* b6-b7 */
}

/*
 * Scale periodic magnitude from SDL format to device format.
 * SDL: 0..32767 (unsigned)
 * Device: 0..127
 */
static inline u8 t500rs_scale_periodic_magnitude(int sdl_mag) {
  if (sdl_mag < 0)
    sdl_mag = -sdl_mag;
  if (sdl_mag > 32767)
    sdl_mag = 32767;
  return (u8)((sdl_mag * 127) / 32767);
}

/*
 * Scale periodic phase from SDL format to device format.
 * SDL: 0..35999 (0.01 degree units, 0-359.99°)
 * Device: 0..255 (256 steps for 360°)
 */
static inline u8 t500rs_scale_periodic_phase(u16 sdl_phase) {
  /* Clamp to valid range just in case */
  if (sdl_phase > 35999)
    sdl_phase = 35999;
  return (u8)((sdl_phase * 256) / 36000);
}

/*
 * Scale periodic offset from SDL format to device format.
 * SDL: -32768..32767
 * Device: signed, stored as s8 (-128..127)
 * Note: exact mapping TBD based on testing; using simple /256 for now.
 */
static inline s8 t500rs_scale_periodic_offset(s16 sdl_offset) {
  return (s8)(sdl_offset / 256);
}

/*
 * Build a 0x04 packet for ramp effects.
 *
 * Per the T500RS USB protocol documentation, ramp effects use the same
 * 0x04 packet structure as periodic effects. The encoding is:
 * - magnitude: scaled from start/end levels (midpoint or average)
 * - offset: difference between start and end (direction of ramp)
 * - phase: typically 0 for ramp
 * - period_ms: ramp duration in milliseconds
 *
 * Note: exact mapping of start/end to magnitude/offset is uncertain;
 * Windows captures show identical packets for different ramp parameters.
 * Current implementation uses a simple average for magnitude.
 */
static void t500rs_build_r04_ramp(struct t500rs_pkt_r04_periodic_ramp *p,
                                  u8 code,
                                  s16 start_level,
                                  s16 end_level,
                                  u16 duration_ms) {
  int avg_level;
  u8 magnitude;
  s8 offset;

  memset(p, 0, sizeof(*p));

  /* Compute average magnitude from start/end levels */
  avg_level = (abs(start_level) + abs(end_level)) / 2;
  magnitude = (u8)((avg_level * 127) / 32767);

  /* Offset encodes direction: positive = ramping up, negative = ramping down */
  /* Simple approximation: (end - start) / 512 to fit in s8 range */
  offset = (s8)((end_level - start_level) / 512);

  /* Byte order per Windows USB captures: b0=id, b1=code, b2=reserved1, b3=mag, b4=offset, b5=phase, b6-b7=period */
  p->id = 0x04;              /* b0 */
  p->code = code;            /* b1 */
  p->reserved1 = 0;          /* b2: always 0x00 */
  p->magnitude = magnitude;  /* b3 */
  p->offset = (u8)offset;    /* b4 */
  p->phase = 0;              /* b5: Ramp doesn't use phase */
  p->period_ms = cpu_to_le16(duration_ms);  /* b6-b7 */
}

/*
 * Build a 0x05 conditional effect packet.
 *
 * Per the T500RS USB protocol documentation:
 * - Conditional effects (spring, damper, inertia, friction) require TWO 0x05 packets
 * - First packet uses code from 0x01 bytes 9-10 (param_sub)
 * - Second packet uses code from 0x01 bytes 11-12 (env_sub)
 * - T500RS is single-axis, so second packet typically contains zeros
 *
 * Parameter scaling (from protocol doc, needs verification):
 * - Coefficients: SDL2 0-32767 → device value (scaling TBD, using /256 for now)
 * - Deadband: SDL2 0-65535 → device value (scaling TBD, using /256 for now)
 * - Center: SDL2 -32767..+32767 → device 0-255 (using (val + 32767) / 256)
 * - Saturation: SDL2 0-32767 → device 0-255 (observed: 0x54, 0x64)
 *
 * The `is_first_packet` flag determines which code to use and whether to
 * populate parameters (first packet) or zeros (second packet for Y-axis).
 */
static void t500rs_build_r05_condition(struct t500rs_pkt_r05_condition *p,
                                       u8 code,
                                       const struct ff_condition_effect *c,
                                       bool is_first_packet) {
  u8 right_sat, left_sat;

  memset(p, 0, sizeof(*p));
  p->id = 0x05;
  p->code = code;

  if (!c)
    return;

  /*
   * Scale saturation: SDL 0-65535 → device 0-100 (0x64)
   * Per Windows captures, observed values are 0x54 (84) and 0x64 (100).
   * The device appears to reject values > 100, so we clamp.
   */
  right_sat = (u8)((c->right_saturation * 100) / 65535);
  if (right_sat > 100)
    right_sat = 100;
  left_sat = (u8)((c->left_saturation * 100) / 65535);
  if (left_sat > 100)
    left_sat = 100;

  /*
   * WORKAROUND: Per Windows captures, BOTH X-axis and Y-axis packets have
   * zeroed coefficients/deadband/center. Only saturation is set.
   * Examples from Windows:
   *   X-axis: 05 2a 00 00 00 00 00 00 00 54 54
   *   Y-axis: 05 38 00 00 00 00 00 00 00 54 54
   *
   * Sending non-zero coefficients may confuse the device firmware.
   * For now, send zeros for all parameters except saturation.
   */
  p->right_sat = right_sat;
  p->left_sat = left_sat;

  /* Coefficients, deadband, center are intentionally left as zeros */
  (void)is_first_packet; /* Unused for now - both packets get zeros */
}

/*
 * Scale constant force level from SDL format to device format.
 *
 * Per the T500RS USB protocol documentation:
 * - SDL2 level: 0-65535 (unsigned)
 * - Device level: -127 to +127 (signed 8-bit)
 * - Formula: device_level = (sdl_level * 255 / 65535) - 127
 *
 * This maps:
 *   SDL 0     → Device -127 (max negative)
 *   SDL 32767 → Device 0 (neutral)
 *   SDL 65535 → Device +127 (max positive)
 */
static inline s8 t500rs_scale_constant_level(u16 sdl_level) {
  s32 tmp = ((s32)sdl_level * 255) / 65535;
  return (s8)(tmp - 127);
}

/*
 * Build a 0x03 constant force packet.
 *
 * Per the T500RS USB protocol documentation:
 * - code: low byte of param_subtype from 0x01 (e.g., 0x0e)
 * - reserved: always 0x00
 * - level: signed -127 to +127
 */
static void t500rs_build_r03_constant(struct t500rs_r03_const *p,
                                      u8 code,
                                      s8 level) {
  p->id = 0x03;
  p->code = code;
  p->zero = 0x00;
  p->level = level;
}

/*
 * Protocol-accurate 0x02 envelope packet (9 bytes).
 *
 * Note: The existing t500rs_r02_envelope struct has an incorrect layout
 * (extra zero byte at offset 2). This struct matches the protocol doc.
 */
struct t500rs_pkt_r02_envelope {
  u8 id;              /* 0x02 */
  u8 subtype;         /* from 0x01 code2 (env_sub low byte) */
  __le16 attack_len;  /* attack duration in ms */
  u8 attack_level;    /* 0-255 */
  __le16 fade_len;    /* fade duration in ms */
  u8 fade_level;      /* 0-255 */
  u8 reserved;        /* 0x00 */
} __packed;

/*
 * Scale envelope level from SDL format to device format.
 * SDL: 0-32767
 * Device: 0-255
 * Formula: device_level = sdl_level * 255 / 32767
 */
static inline u8 t500rs_scale_envelope_level(u16 sdl_level) {
  if (sdl_level > 32767)
    sdl_level = 32767;
  return (u8)((sdl_level * 255) / 32767);
}

/*
 * Build a protocol-accurate 0x02 envelope packet.
 *
 * Per the T500RS USB protocol documentation:
 * - subtype: low byte of env_sub from 0x01 (e.g., 0x1c)
 * - attack_len: attack duration in milliseconds
 * - attack_level: 0-255 (scaled from SDL 0-32767)
 * - fade_len: fade duration in milliseconds
 * - fade_level: 0-255 (scaled from SDL 0-32767)
 * - reserved: always 0x00
 */
static void t500rs_build_r02_envelope(struct t500rs_pkt_r02_envelope *p,
                                      u8 subtype,
                                      const struct ff_envelope *env) {
  memset(p, 0, sizeof(*p));
  p->id = 0x02;
  p->subtype = subtype;

  if (env) {
    p->attack_len = cpu_to_le16(env->attack_length);
    p->attack_level = t500rs_scale_envelope_level(env->attack_level);
    p->fade_len = cpu_to_le16(env->fade_length);
    p->fade_level = t500rs_scale_envelope_level(env->fade_level);
  }
  p->reserved = 0x00;
}

/* Supported parameters */
static const unsigned long t500rs_params =
    PARAM_SPRING_LEVEL | PARAM_DAMPER_LEVEL | PARAM_FRICTION_LEVEL |
    PARAM_GAIN | PARAM_RANGE;

/*
 * Supported effects.
 *
 * NOTE: FF_SQUARE is intentionally OMITTED. Per Windows USB captures, the
 * T500RS protocol does not encode waveform type in USB packets. Windows/SDL2
 * may emulate square waves in software, but the device hardware appears to
 * only support the base waveforms. Rather than silently map to sine (which
 * would feel wrong to users), we reject FF_SQUARE and let applications fall
 * back to alternative effects.
 */
static const signed short t500rs_effects[] = {
    FF_CONSTANT, FF_SPRING, FF_DAMPER,   FF_FRICTION,   FF_INERTIA,
    FF_PERIODIC, FF_SINE,   FF_TRIANGLE, FF_SAW_UP,     FF_SAW_DOWN,
    FF_RAMP,     FF_GAIN,   FF_AUTOCENTER, -1};

/* Forward declarations to avoid implicit declarations before worker uses them
 */
static int t500rs_send_usb(struct t500rs_device_entry *t500rs, const u8 *data,
                           size_t len);
static int t500rs_set_autocenter(void *data, u16 autocenter);
static int t500rs_set_range(void *data, u16 range);
static int t500rs_upload_effect(void *data,
                                const struct tmff2_effect_state *state);
static int t500rs_update_effect(void *data,
                                const struct tmff2_effect_state *state);
static int t500rs_play_effect(void *data,
                              const struct tmff2_effect_state *state);
static int t500rs_stop_effect(void *data,
                              const struct tmff2_effect_state *state);

static int t500rs_set_gain(void *data, u16 gain) {
  struct t500rs_device_entry *t500rs = data;
  u8 *buf;
  u8 device_gain_byte;
  if (!t500rs)
    return -ENODEV;
  buf = t500rs->send_buffer;
  if (!buf)
    return -ENOMEM;
  /* Scale 0..65535 to device 0..255 */
  device_gain_byte = (u8)((gain * 255) / GAIN_MAX);
  buf[0] = 0x43;
  buf[1] = device_gain_byte;
  return t500rs_send_usb(t500rs, buf, 2);
}

/* Send data via USB INTERRUPT transfer (blocking) */
static int t500rs_send_usb(struct t500rs_device_entry *t500rs, const u8 *data,
                           size_t len) {
  int ret, transferred;
  if (!t500rs || !data || len == 0 || len > T500RS_BUFFER_LENGTH)
    return -EINVAL;

  ret = usb_interrupt_msg(t500rs->usbdev,
                          usb_sndintpipe(t500rs->usbdev, t500rs->ep_out),
                          (void *)data, len, &transferred, T500RS_USB_TIMEOUT);
  if (ret < 0)
    return ret;
  return (transferred == len) ? 0 : -EIO;
}

/*
 * Send STOP command for a specific hardware effect ID.
 * Used both for pre-upload clearing and explicit stop.
 * Per protocol: 0x41 effect_id command arg
 *   command = 0x00 for STOP, 0x41 for START
 */
static inline int t500rs_send_stop(struct t500rs_device_entry *t500rs,
                                   u8 hw_effect_id) {
  u8 *buf;
  struct t500rs_r41_cmd *r41;
  if (!t500rs)
    return -ENODEV;
  buf = t500rs->send_buffer;
  if (!buf)
    return -ENOMEM;
  r41 = (struct t500rs_r41_cmd *)buf;
  r41->id = 0x41;
  r41->effect_id = hw_effect_id;
  r41->command = 0x00; /* STOP */
  r41->arg = 0x01;
  return t500rs_send_usb(t500rs, buf, sizeof(*r41));
}

/*
 * Send START command for a specific hardware effect ID.
 */
static inline int t500rs_send_start(struct t500rs_device_entry *t500rs,
                                    u8 hw_effect_id) {
  u8 *buf;
  struct t500rs_r41_cmd *r41;
  if (!t500rs)
    return -ENODEV;
  buf = t500rs->send_buffer;
  if (!buf)
    return -ENOMEM;
  r41 = (struct t500rs_r41_cmd *)buf;
  r41->id = 0x41;
  r41->effect_id = hw_effect_id;
  r41->command = 0x41; /* START */
  r41->arg = 0x01;
  return t500rs_send_usb(t500rs, buf, sizeof(*r41));
}

/* Upload constant force effect */
static int t500rs_upload_constant(struct t500rs_device_entry *t500rs,
                                  const struct tmff2_effect_state *state) {
  const struct ff_effect *effect = &state->effect;
  u8 *buf = t500rs->send_buffer; /* Use DMA-safe buffer */
  int ret;
  int hw_id;
  int level = effect->u.constant.level;
  u16 direction_dev;
  u16 duration_ms;
  u16 delay_ms;
  u16 param_sub, env_sub;
  s8 signed_level;

  /* Note: Gain is applied in play_effect, not here */
  T500RS_DBG(t500rs, "Upload constant: id=%d, level=%d, dir=%u\n",
             effect->id, level, effect->direction);

  /* Allocate a hardware effect ID for this logical effect.
   * Constant effects CAN use index 0 (subtypes 0x0e/0x1c) per Windows captures.
   */
  hw_id = t500rs_alloc_hw_id(t500rs, effect->id, false);
  if (hw_id < 0) {
    hid_err(t500rs->hdev, "Failed to allocate hw_id for effect %d\n",
            effect->id);
    return hw_id;
  }

  /* Pre-upload STOP to clear the slot (Windows parity) */
  ret = t500rs_send_stop(t500rs, (u8)hw_id);
  if (ret) {
    hid_err(t500rs->hdev, "Pre-upload STOP failed: %d\n", ret);
    return ret;
  }

  /* Compute protocol parameters using hw_id for subtype calculation */
  direction_dev = t500rs_scale_direction(effect->direction);
  duration_ms = effect->replay.length ? effect->replay.length : 0xffff; /* 0 = infinite */
  delay_ms = effect->replay.delay;
  t500rs_index_to_subtypes(hw_id, &param_sub, &env_sub);

  /*
   * CRITICAL: Windows sends packets in this order for constant effects:
   *   1. 0x02 Envelope parameters
   *   2. 0x03 Constant force level
   *   3. 0x01 Main effect descriptor
   *   4. 0x41 START
   */

  /* Report 0x02 - Envelope (FIRST per Windows captures)
   *
   * NOTE: Windows driver always sends zeros for envelope on constant effects.
   * This appears to be a firmware limitation - envelope is not supported
   * for constant effects on T500RS hardware.
   */
  {
    struct t500rs_pkt_r02_envelope *env = (struct t500rs_pkt_r02_envelope *)buf;
    t500rs_build_r02_envelope(env, (u8)(env_sub & 0xff), NULL);
  }
  ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_pkt_r02_envelope));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x02 (const): %d\n", ret);
    return ret;
  }

  /* Report 0x03 - Constant force level (SECOND per Windows captures) */
  signed_level = t500rs_scale_const_with_direction(level, effect->direction);
  {
    struct t500rs_r03_const *r3 = (struct t500rs_r03_const *)buf;
    t500rs_build_r03_constant(r3, (u8)(param_sub & 0xff), signed_level);
  }
  ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_r03_const));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x03 (const level): %d\n",
            ret);
    return ret;
  }

  /* Report 0x01 - Main effect descriptor (THIRD per Windows captures)
   * Per Windows captures: effect_type=0x00 for constant
   */
  {
    struct t500rs_pkt_r01_main *m = (struct t500rs_pkt_r01_main *)buf;
    t500rs_build_r01_main(m, 0x00, duration_ms, delay_ms, param_sub, env_sub);
  }
  ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_pkt_r01_main));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x01: %d\n", ret);
    return ret;
  }

  T500RS_DBG(t500rs,
             "Constant effect %d uploaded (hw_id=%d, 0x02 + 0x03 + 0x01)\n",
             effect->id, hw_id);
  return 0;
}

/*
 * Upload spring/damper/friction/inertia effect.
 *
 * Per Windows captures (T500RS_USB_Protocol_Analysis.md):
 * - 0x01 packet: direction=0x4000, code1=0x002a, code2=0x0038
 * - Two 0x05 packets: X-axis (code 0x2a) and Y-axis (code 0x38)
 * - Saturation values 0x54 (84) for spring, 0x64 (100) for damper/friction
 */
static int t500rs_upload_condition(struct t500rs_device_entry *t500rs,
                                   const struct tmff2_effect_state *state) {
  const struct ff_effect *effect = &state->effect;
  u8 *buf = t500rs->send_buffer;
  int ret;
  int hw_id;
  u8 effect_gain;
  const char *type_name;
  u16 direction_dev, duration_ms, delay_ms;
  u16 param_sub, env_sub;
  const struct ff_condition_effect *cond = &effect->u.condition[0];

  /* Determine effect type and select appropriate gain */
  switch (effect->type) {
  case FF_SPRING:
    type_name = "spring";
    effect_gain = spring_level;
    break;
  case FF_DAMPER:
    type_name = "damper";
    effect_gain = damper_level;
    break;
  case FF_FRICTION:
    type_name = "friction";
    effect_gain = friction_level;
    break;
  case FF_INERTIA:
    type_name = "inertia";
    effect_gain = 100;
    break;
  default:
    return -EINVAL;
  }

  /* Allocate a hardware effect ID for this logical effect.
   * Conditional effects MUST skip index 0 - the device rejects 0x05 packets
   * with index 0 subtypes (EPROTO). Per Windows captures, all conditional
   * effects use index 1+ (subtypes 0x2a/0x38 or higher).
   */
  hw_id = t500rs_alloc_hw_id(t500rs, effect->id, true);
  if (hw_id < 0) {
    hid_err(t500rs->hdev, "Failed to allocate hw_id for %s effect %d\n",
            type_name, effect->id);
    return hw_id;
  }

  /* Per Windows captures, conditional effects use index-based subtypes */
  direction_dev = t500rs_scale_direction(effect->direction);
  duration_ms = effect->replay.length ? effect->replay.length : 0xffff;
  delay_ms = effect->replay.delay;
  t500rs_index_to_subtypes(hw_id, &param_sub, &env_sub);

  T500RS_DBG(t500rs,
             "Upload %s: id=%d, hw_id=%d, gain=%u%%, dir=%u, param_sub=0x%04x, "
             "env_sub=0x%04x, rcoef=%d, lcoef=%d\n",
             type_name, effect->id, hw_id, effect_gain, direction_dev,
             param_sub, env_sub, cond->right_coeff, cond->left_coeff);

  /* Pre-upload STOP to clear the slot (Windows parity) */
  ret = t500rs_send_stop(t500rs, (u8)hw_id);
  if (ret) {
    hid_err(t500rs->hdev, "Pre-upload STOP failed: %d\n", ret);
    return ret;
  }

  /*
   * CRITICAL: Windows sends packets in this order for conditionals:
   *   1. 0x05 X-axis parameters
   *   2. 0x05 Y-axis parameters
   *   3. 0x01 Main effect descriptor
   *   4. 0x41 START
   *
   * This is DIFFERENT from constant/periodic which send 0x01 first!
   */

  /* Report 0x05 - X-axis condition parameters (code = param_sub low byte)
   * is_first_packet=true fills in the condition data
   */
  {
    struct t500rs_pkt_r05_condition *p = (struct t500rs_pkt_r05_condition *)buf;
    t500rs_build_r05_condition(p, (u8)(param_sub & 0xff), cond, true);
  }
  ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_pkt_r05_condition));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x05 (X-axis): %d\n", ret);
    return ret;
  }

  /* Report 0x05 - Y-axis condition parameters (code = env_sub low byte)
   * For single-axis T500RS, Y-axis coefficients/deadband/center are zeroed,
   * but saturation values must still be set per Windows captures.
   * is_first_packet=false zeros coefficients but keeps saturation.
   */
  {
    struct t500rs_pkt_r05_condition *p = (struct t500rs_pkt_r05_condition *)buf;
    t500rs_build_r05_condition(p, (u8)(env_sub & 0xff), cond, false);
  }
  ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_pkt_r05_condition));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x05 (Y-axis): %d\n", ret);
    return ret;
  }

  /* Report 0x01 - Main effect upload (AFTER 0x05 packets for conditionals!)
   * Per Windows captures: ALL conditional effects use effect_type=0x40
   * (Spring: 01 00 40 40..., Damper: 01 00 40 40...)
   */
  {
    u8 effect_type = 0x40;  /* All conditionals use 0x40 per Windows captures */
    struct t500rs_pkt_r01_main *m = (struct t500rs_pkt_r01_main *)buf;
    t500rs_build_r01_main(m, effect_type, duration_ms, delay_ms, param_sub, env_sub);
  }
  ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_pkt_r01_main));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x01 (condition): %d\n", ret);
    return ret;
  }

  T500RS_DBG(t500rs, "%s effect %d uploaded (2x 0x05 + 0x01)\n",
             type_name, effect->id);
  return 0;
}

/*
 * Upload periodic effect (sine, square, triangle, saw).
 *
 * Per Windows captures (T500RS_USB_Protocol_Analysis.md):
 * - Waveform type is NOT encoded in USB packets; determined by SDL2/DirectInput
 * - 0x01 packet: direction, duration, delay, code1=0x000e, code2=0x001c
 * - 0x02 packet: envelope with subtype 0x1c
 * - 0x04 packet: code=0x2a (NOT 0x0e!), magnitude, offset, phase, period_ms
 * - Period is in MILLISECONDS (no Hz×100 conversion)
 *
 * NOTE: The current implementation only sends the simplified packet sequence
 * observed in Windows captures. The dual-0x01/0x02 sequence in the old code
 * may have been incorrect and is removed.
 */
static int t500rs_upload_periodic(struct t500rs_device_entry *t500rs,
                                  const struct tmff2_effect_state *state) {
  const struct ff_effect *effect = &state->effect;
  u8 *buf = t500rs->send_buffer;
  int ret;
  int hw_id;
  const char *type_name;
  u8 effect_type;
  u16 param_sub, env_sub;
  u16 direction_dev, duration_ms, delay_ms;
  u16 period_ms;
  u8 mag, phase, offset;

  /*
   * Determine waveform name and effect_type for 0x01 packet.
   *
   * Per Windows captures, waveform type IS encoded in the 0x01 packet's
   * effect_type field (byte 2). We only support the waveforms observed in captures.
   *
   * FF_SQUARE is rejected because it's not in our supported effects list.
   *
   * Per Windows captures, effect_type values for periodic:
   * - 0x21 = Triangle (inferred from protocol pattern)
   * - 0x22 = Sine (confirmed from analysis.json)
   * - 0x23 = Sawtooth Up (inferred)
   * - 0x24 = Sawtooth Down (inferred)
   */
  switch (effect->u.periodic.waveform) {
  case FF_TRIANGLE:
    type_name = "triangle";
    effect_type = 0x21;
    break;
  case FF_SINE:
    type_name = "sine";
    effect_type = 0x22;
    break;
  case FF_SAW_UP:
    type_name = "sawtooth_up";
    effect_type = 0x23;
    break;
  case FF_SAW_DOWN:
    type_name = "sawtooth_down";
    effect_type = 0x24;
    break;
  default:
    /* FF_SQUARE and other unsupported waveforms */
    hid_err(t500rs->hdev, "Unsupported periodic waveform: %d\n",
            effect->u.periodic.waveform);
    return -EINVAL;
  }

  /* Allocate a hardware effect ID for this logical effect.
   * Periodic effects MUST skip index 0 - the device rejects 0x04 packets
   * with index 0 subtypes (EPROTO). Per Windows captures, all periodic
   * effects use index 1+ (subtypes 0x2a/0x38 or higher).
   */
  hw_id = t500rs_alloc_hw_id(t500rs, effect->id, true);
  if (hw_id < 0) {
    hid_err(t500rs->hdev, "Failed to allocate hw_id for %s effect %d\n",
            type_name, effect->id);
    return hw_id;
  }

  /* Scale parameters using new protocol-accurate helpers */
  mag = t500rs_scale_periodic_magnitude(effect->u.periodic.magnitude);
  phase = t500rs_scale_periodic_phase(effect->u.periodic.phase);
  offset = t500rs_scale_periodic_offset(effect->u.periodic.offset);
  direction_dev = t500rs_scale_direction(effect->direction);
  duration_ms = effect->replay.length ? effect->replay.length : 0xffff;
  delay_ms = effect->replay.delay;
  period_ms = effect->u.periodic.period;
  if (period_ms == 0)
    period_ms = 100; /* Default 100ms if not specified */

  /* Use index-based subtypes like all other effect types.
   * The device uses these subtypes to associate parameter packets with effect slots.
   */
  t500rs_index_to_subtypes(hw_id, &param_sub, &env_sub);

  T500RS_DBG(t500rs,
             "Upload %s: id=%d, hw_id=%d, mag=0x%02x, phase=0x%02x, offset=0x%02x, "
             "period=%ums, dir=%u\n",
             type_name, effect->id, hw_id, mag, phase, offset, period_ms, direction_dev);

  /* Pre-upload STOP to clear the slot (Windows parity) */
  ret = t500rs_send_stop(t500rs, (u8)hw_id);
  if (ret) {
    hid_warn(t500rs->hdev, "Pre-upload STOP for periodic failed: %d (continuing)\n", ret);
    /* Don't fail - the slot might already be empty */
  }

  /* Pre-upload: Send 0x42 status/sync commands like Windows does */
  buf[0] = 0x42;
  buf[1] = 0x05;
  t500rs_send_usb(t500rs, buf, 2); /* Ignore errors - these are optional sync */

  buf[0] = 0x42;
  buf[1] = 0x04;
  t500rs_send_usb(t500rs, buf, 2);

  /*
   * CRITICAL: Windows sends packets in this order for periodic effects:
   *   1. 0x02 Envelope parameters
   *   2. 0x04 Periodic parameters
   *   3. 0x01 Main effect descriptor
   *   4. 0x41 START
   *
   * This is DIFFERENT from sending 0x01 first!
   */

  /* Report 0x02 - Envelope (FIRST per Windows captures)
   *
   * NOTE: Windows driver always sends zeros for envelope on periodic effects.
   * Non-zero envelope values cause EPROTO on subsequent 0x04 packet.
   * This appears to be a firmware limitation - envelope is not supported
   * for periodic effects on T500RS hardware.
   */
  {
    struct t500rs_pkt_r02_envelope *env = (struct t500rs_pkt_r02_envelope *)buf;
    t500rs_build_r02_envelope(env, (u8)(env_sub & 0xff), NULL);
  }
  ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_pkt_r02_envelope));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x02 (periodic): %d\n", ret);
    return ret;
  }

  /* Report 0x04 - Periodic parameters (SECOND per Windows captures) */
  {
    struct t500rs_pkt_r04_periodic_ramp *p =
        (struct t500rs_pkt_r04_periodic_ramp *)buf;
    t500rs_build_r04_periodic(p, (u8)(param_sub & 0xff), mag, offset, phase, period_ms);
  }
  ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_pkt_r04_periodic_ramp));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x04 (periodic): %d\n", ret);
    return ret;
  }

  /* Report 0x01 - Main effect descriptor (THIRD per Windows captures) */
  {
    struct t500rs_pkt_r01_main *m = (struct t500rs_pkt_r01_main *)buf;
    t500rs_build_r01_main(m, effect_type, duration_ms, delay_ms, param_sub, env_sub);
  }
  ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_pkt_r01_main));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x01 (periodic): %d\n", ret);
    return ret;
  }

  T500RS_DBG(t500rs, "%s effect %d uploaded (0x02 + 0x04 + 0x01)\n",
             type_name, effect->id);
  return 0;
}

/*
 * Upload ramp effect.
 *
 * Per Windows captures (T500RS_USB_Protocol_Analysis.md):
 * - Ramp uses same 0x04 packet structure as periodic (code 0x2a)
 * - Packet sequence: 0x01 + 0x02 + 0x04 + 0x41
 * - Start/end levels encoded in magnitude/offset fields
 * - Period field encodes ramp duration
 */
static int t500rs_upload_ramp(struct t500rs_device_entry *t500rs,
                              const struct tmff2_effect_state *state) {
  const struct ff_effect *effect = &state->effect;
  u8 *buf = t500rs->send_buffer;
  int ret;
  int hw_id;
  u16 param_sub, env_sub;
  u16 direction_dev, duration_ms, delay_ms;
  u8 magnitude, offset;

  /* Allocate a hardware effect ID for this logical effect.
   * Ramp effects MUST skip index 0 - the device rejects 0x04 packets
   * with index 0 subtypes (EPROTO). Per Windows captures, all ramp
   * effects use index 1+ (subtypes 0x2a/0x38 or higher).
   */
  hw_id = t500rs_alloc_hw_id(t500rs, effect->id, true);
  if (hw_id < 0) {
    hid_err(t500rs->hdev, "Failed to allocate hw_id for ramp effect %d\n",
            effect->id);
    return hw_id;
  }

  /* Use index-based subtypes for ramp effects */
  direction_dev = t500rs_scale_direction(effect->direction);
  duration_ms = effect->replay.length ? effect->replay.length : 1000;
  delay_ms = effect->replay.delay;
  t500rs_index_to_subtypes(hw_id, &param_sub, &env_sub);

  T500RS_DBG(t500rs,
             "Upload ramp: id=%d, hw_id=%d, start=%d, end=%d, duration=%ums, dir=%u\n",
             effect->id, hw_id, effect->u.ramp.start_level, effect->u.ramp.end_level,
             duration_ms, direction_dev);

  /* Pre-upload STOP to clear the slot (Windows parity) */
  ret = t500rs_send_stop(t500rs, (u8)hw_id);
  if (ret) {
    hid_err(t500rs->hdev, "Pre-upload STOP failed: %d\n", ret);
    return ret;
  }

  /*
   * CRITICAL: Windows sends packets in this order for ramp effects:
   *   1. 0x02 Envelope parameters
   *   2. 0x04 Ramp parameters
   *   3. 0x01 Main effect descriptor
   *   4. 0x41 START
   */

  /* Report 0x02 - Envelope (FIRST per Windows captures) */
  {
    struct t500rs_pkt_r02_envelope *env = (struct t500rs_pkt_r02_envelope *)buf;
    t500rs_build_r02_envelope(env, (u8)(env_sub & 0xff),
                              &effect->u.ramp.envelope);
  }
  ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_pkt_r02_envelope));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x02 (ramp): %d\n", ret);
    return ret;
  }

  /* Report 0x04 - Ramp parameters (SECOND per Windows captures) */
  {
    struct t500rs_pkt_r04_periodic_ramp *p =
        (struct t500rs_pkt_r04_periodic_ramp *)buf;
    t500rs_build_r04_ramp(p, (u8)(param_sub & 0xff), effect->u.ramp.start_level,
                          effect->u.ramp.end_level, duration_ms);
    /* Compute magnitude and offset for debug */
    magnitude = p->magnitude;
    offset = p->offset;
  }
  ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_pkt_r04_periodic_ramp));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x04 (ramp): %d\n", ret);
    return ret;
  }

  /* Report 0x01 - Main effect descriptor (THIRD per Windows captures)
   * Per Windows captures: effect_type=0x24 for ramp (sawtooth down)
   */
  {
    struct t500rs_pkt_r01_main *m = (struct t500rs_pkt_r01_main *)buf;
    t500rs_build_r01_main(m, 0x24, duration_ms, delay_ms, param_sub, env_sub);
  }
  ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_pkt_r01_main));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x04 (ramp): %d\n", ret);
    return ret;
  }

  T500RS_DBG(t500rs,
             "Ramp effect %d uploaded (mag=0x%02x, offset=0x%02x)\n",
             effect->id, magnitude, offset);
  return 0;
}

/* Upload effect */
static int t500rs_upload_effect(void *data,
                                const struct tmff2_effect_state *state) {
  struct t500rs_device_entry *t500rs = data;
  const struct ff_effect *effect = &state->effect;

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

/*
 * Play effect - send START command (0x41) for the effect.
 *
 * For constant force, also send a level update (0x03) before START.
 * Uses per-effect hw_id for proper multi-effect support.
 */
static int t500rs_play_effect(void *data,
                              const struct tmff2_effect_state *state) {
  struct t500rs_device_entry *t500rs = data;
  const struct ff_effect *effect = &state->effect;
  u8 *buf;
  int ret;
  int hw_id;

  if (!t500rs)
    return -ENODEV;

  buf = t500rs->send_buffer;
  if (!buf)
    return -ENOMEM;

  /* Get the allocated hw_id for this logical effect */
  hw_id = t500rs_get_hw_id(t500rs, effect->id);
  if (hw_id < 0) {
    hid_err(t500rs->hdev, "No hw_id allocated for effect %d\n", effect->id);
    return hw_id;
  }

  T500RS_DBG(t500rs,
             "Play effect: id=%d, hw_id=%d, type=0x%02x\n",
             effect->id, hw_id, effect->type);

  /* For constant force: send level update (0x03) then START (0x41) */
  if (effect->type == FF_CONSTANT) {
    int level = effect->u.constant.level;
    u16 direction = effect->direction;
    s8 signed_level;
    u16 param_sub, env_sub;

    signed_level = t500rs_scale_const_with_direction(level, direction);
    t500rs_index_to_subtypes(hw_id, &param_sub, &env_sub);

    T500RS_DBG(t500rs,
               "Constant force: level=%d dir=%u -> %d (0x%02x)\n", level,
               direction, signed_level, (u8)signed_level);

    /* Send Report 0x03 (force level) */
    {
      struct t500rs_r03_const *r3 = (struct t500rs_r03_const *)buf;
      t500rs_build_r03_constant(r3, (u8)(param_sub & 0xff), signed_level);
    }
    ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_r03_const));
    if (ret) {
      hid_err(t500rs->hdev, "Failed to send Report 0x03: %d\n", ret);
      return ret;
    }
  }

  /* Send START command with proper hw_id */
  T500RS_DBG(t500rs, "Sending START command (hw_id=%d) for effect %d\n",
             hw_id, effect->id);
  return t500rs_send_start(t500rs, (u8)hw_id);
}

/*
 * Stop effect - send STOP command (0x41) for the effect.
 *
 * Uses per-effect hw_id for proper multi-effect support.
 * Also frees the hardware effect ID slot for reuse.
 */
static int t500rs_stop_effect(void *data,
                              const struct tmff2_effect_state *state) {
  struct t500rs_device_entry *t500rs = data;
  int ret;
  int hw_id;

  if (!t500rs) {
    pr_err("t500rs_stop_effect: t500rs is NULL!\n");
    return -ENODEV;
  }

  if (!t500rs->send_buffer) {
    hid_err(t500rs->hdev, "Stop effect: send_buffer is NULL!\n");
    return -ENOMEM;
  }

  /* Get the allocated hw_id for this logical effect */
  hw_id = t500rs_get_hw_id(t500rs, state->effect.id);
  if (hw_id < 0) {
    /* No hw_id means effect was never uploaded, nothing to stop */
    T500RS_DBG(t500rs, "Stop effect: no hw_id for effect %d (ok)\n",
               state->effect.id);
    return 0;
  }

  T500RS_DBG(t500rs, "Stop effect: id=%d, hw_id=%d, type=%d\n",
             state->effect.id, hw_id, state->effect.type);

  /* Send STOP command with proper hw_id */
  ret = t500rs_send_stop(t500rs, (u8)hw_id);

  /* Free the hardware slot for reuse */
  t500rs_free_hw_id(t500rs, state->effect.id);

  T500RS_DBG(t500rs, "Stop effect returned: %d\n", ret);
  return ret;
}

/* Update effect - re-upload and update force level if constant force */
static int t500rs_update_effect(void *data,
                                const struct tmff2_effect_state *state) {
  struct t500rs_device_entry *t500rs = data;
  const struct ff_effect *effect = &state->effect;
  const struct ff_effect *old = &state->old;
  u8 *buf;
  int hw_id;

  if (!t500rs)
    return -ENODEV;

  buf = t500rs->send_buffer;
  if (!buf)
    return -ENOMEM;

  /* Get the hw_id for this effect (must have been allocated during upload) */
  hw_id = t500rs_get_hw_id(t500rs, effect->id);
  if (hw_id < 0) {
    /* Effect not uploaded yet - skip update */
    return 0;
  }

  /* Do NOT re-upload here; Windows keeps the effect and we only update parameters */
  switch (effect->type) {
  case FF_CONSTANT: {
      int level = effect->u.constant.level;
      s8 signed_level = t500rs_scale_const_with_direction(level, effect->direction);
      u16 param_sub, env_sub;
      t500rs_index_to_subtypes(hw_id, &param_sub, &env_sub);
      struct t500rs_r03_const *r3 = (struct t500rs_r03_const *)buf;
      t500rs_build_r03_constant(r3, (u8)(param_sub & 0xff), signed_level);
      return t500rs_send_usb(t500rs, (u8 *)r3, sizeof(*r3));
    }
  case FF_PERIODIC: {
      /* Update periodic using protocol-accurate helpers and ms period */
      u8 mag = t500rs_scale_periodic_magnitude(effect->u.periodic.magnitude);
      u8 phase = t500rs_scale_periodic_phase(effect->u.periodic.phase);
      u8 offset = t500rs_scale_periodic_offset(effect->u.periodic.offset);
      u16 period_ms = effect->u.periodic.period;
      u16 param_sub, env_sub;
      if (period_ms == 0)
        period_ms = 100;

      /* Use index-based subtype for the 0x04 packet code */
      t500rs_index_to_subtypes(hw_id, &param_sub, &env_sub);
      struct t500rs_pkt_r04_periodic_ramp *p =
          (struct t500rs_pkt_r04_periodic_ramp *)buf;
      t500rs_build_r04_periodic(p, (u8)(param_sub & 0xff), mag, offset, phase, period_ms);
      return t500rs_send_usb(t500rs, buf, sizeof(*p));
    }
  case FF_RAMP: {
      /* Update ramp using protocol-accurate helper */
      u16 duration_ms = effect->replay.length ? effect->replay.length : 1000;
      u16 param_sub, env_sub;
      /* Use index-based subtype for the 0x04 packet code */
      t500rs_index_to_subtypes(hw_id, &param_sub, &env_sub);
      struct t500rs_pkt_r04_periodic_ramp *p =
          (struct t500rs_pkt_r04_periodic_ramp *)buf;
      t500rs_build_r04_ramp(p, (u8)(param_sub & 0xff), effect->u.ramp.start_level,
                            effect->u.ramp.end_level, duration_ms);
      return t500rs_send_usb(t500rs, buf, sizeof(*p));
    }
  case FF_SPRING:
  case FF_DAMPER:
  case FF_FRICTION:
  case FF_INERTIA: {
      /*
       * Update conditional effect using protocol-accurate helper.
       *
       * Rationale: ACC (and similar) may spam condition updates at low speed with
       * the exact same parameters. Re-sending 0x05 at high cadence makes T500RS
       * micro-pulse/rumble. Therefore we compare old vs new and only send when
       * they differ. We only update X-axis (code 0x2a); Y-axis is always zero
       * for single-axis T500RS.                                                                                                                                                                                                                              
       */
      const struct ff_condition_effect *cond = &effect->u.condition[0];
      const struct ff_condition_effect *cond_old = &old->u.condition[0];

      u16 param_sub, env_sub;

      /* Simple change detection: compare raw condition parameters */
      if (cond->right_coeff == cond_old->right_coeff &&
          cond->left_coeff == cond_old->left_coeff &&
          cond->right_saturation == cond_old->right_saturation &&
          cond->left_saturation == cond_old->left_saturation &&
          cond->deadband == cond_old->deadband &&
          cond->center == cond_old->center &&
          effect->type == old->type)
        return 0;

      /* Send updated X-axis 0x05 packet with proper per-effect code */
      t500rs_index_to_subtypes(hw_id, &param_sub, &env_sub);
      struct t500rs_pkt_r05_condition *p =
          (struct t500rs_pkt_r05_condition *)buf;
      t500rs_build_r05_condition(p, (u8)(param_sub & 0xff), cond, true);
      return t500rs_send_usb(t500rs, buf, sizeof(*p));
    }
  default:
      return 0;
  }
}

/* Set autocenter */
static int t500rs_set_autocenter(void *data, u16 autocenter) {
  struct t500rs_device_entry *t500rs = data;
  u8 *buf;
  int ret;
  u8 autocenter_percent;

  if (!t500rs)
    return -ENODEV;

  autocenter_percent = (u8)((autocenter * 100) / 65535);

  /* Wine compatibility: Some games (e.g., LFS under Wine) set autocenter to 100%%
   * at startup and never release it. That leaves a permanent strong centering force
   * which masks/overpowers other forces. To avoid this, ignore requests that try to
   * set maximum autocenter (100%%). Disabling (0) is still honored; lower values are
   * allowed. */
  if (autocenter_percent >= 100) {
    hid_warn(t500rs->hdev,
             "Ignoring 100%% autocenter request (Wine/LFS compatibility)");
    return 0;
  }

  buf = t500rs->send_buffer;
  if (!buf)
    return -ENOMEM;

  if (autocenter == 0) {
    /* Disable autocenter: Report 0x40 0x04 0x00 */
    buf[0] = 0x40;
    buf[1] = 0x04;
    buf[2] = 0x00; /* Disable */
    buf[3] = 0x00;
    ret = t500rs_send_usb(t500rs, buf, 4);
    if (ret)
      return ret;
  } else {
    /* Enable autocenter: Report 0x40 0x04 0x01 */
    buf[0] = 0x40;
    buf[1] = 0x04;
    buf[2] = 0x01; /* Enable */
    buf[3] = 0x00;
    ret = t500rs_send_usb(t500rs, buf, 4);
    if (ret)
      return ret;

    /* Set autocenter strength: Report 0x40 0x03 [value] */
    buf[0] = 0x40;
    buf[1] = 0x03;
    buf[2] = autocenter_percent; /* 0-100 percentage */
    buf[3] = 0x00;
    ret = t500rs_send_usb(t500rs, buf, 4);
    if (ret)
      return ret;
  }

  /* Apply settings: Report 0x42 0x05 */
  buf[0] = 0x42;
  buf[1] = 0x05;
  ret = t500rs_send_usb(t500rs, buf, 2);
  if (ret)
    return ret;

  return 0;
}

/* Set wheel rotation range */
static int t500rs_set_range(void *data, u16 range) {
  struct t500rs_device_entry *t500rs = data;
  u8 *buf;
  int ret;
  u16 range_value;

  if (!t500rs)
    return -ENODEV;

  /* Clamp range to maximum value only
   * Allow testing values below 270° to find hardware minimum */
  if (range > 1080) {
    hid_warn(t500rs->hdev, "Range %u too large, clamping to 1080\n", range);
    range = 1080;
  }

  /* Use DMA-safe preallocated buffer */
  buf = t500rs->send_buffer;
  if (!buf)
    return -ENOMEM;

  T500RS_DBG(t500rs, "Setting wheel range to %u degrees\n", range);

  /* Device expects LITTLE-ENDIAN and value = range * 60. */
  range_value = range * 60;

  /* Send Report 0x40 0x11 [value_lo] [value_hi] to set range */
  buf[0] = 0x40;
  buf[1] = 0x11;
  buf[2] = range_value & 0xFF;        /* Low byte first (little-endian) */
  buf[3] = (range_value >> 8) & 0xFF; /* High byte second */

  ret = t500rs_send_usb(t500rs, buf, 4);
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send range command: %d\n", ret);
    return ret;
  }

  /* Store current range */
  t500rs->current_range = range;

  /* Apply settings with Report 0x42 0x05 */
  buf[0] = 0x42;
  buf[1] = 0x05;
  ret = t500rs_send_usb(t500rs, buf, 2);
  if (ret) {
    hid_err(t500rs->hdev, "Failed to apply range settings: %d\n", ret);
    return ret;
  }

  T500RS_DBG(t500rs,
             "Range set to %u degrees (final value=0x%04x)\n", range,
             range_value);

  return 0;
}

/* Initialize T500RS device */
static int t500rs_wheel_init(struct tmff2_device_entry *tmff2, int open_mode) {
  struct t500rs_device_entry *t500rs;
  struct usb_host_endpoint *ep;
  u8 *init_buf; /* Will use send_buffer for DMA-safe transfers */
  int ret;

	/* Sanity check protocol main-upload packet size against documentation */
	BUILD_BUG_ON(sizeof(struct t500rs_pkt_r01_main) != 15);

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

  T500RS_DBG(t500rs, "Found INTERRUPT OUT endpoint: 0x%02x\n", t500rs->ep_out);

  /* Allocate send buffer */
  t500rs->buffer_length = T500RS_BUFFER_LENGTH;
  t500rs->send_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
  if (!t500rs->send_buffer) {
    ret = -ENOMEM;
    goto err_buffer;
  }

  /* Initialize current range to default (900°) */
  t500rs->current_range = 900;

  /* Initialize hw_id_owner to "unowned" state */
  {
    int i;
    for (i = 0; i < T500RS_MAX_HW_EFFECTS; i++)
      t500rs->hw_id_owner[i] = 0xFFFF;
  }

  /* Store device data in tmff2 */
  tmff2->data = t500rs;

  /* Use send_buffer for all USB transfers (DMA-safe) */
  init_buf = t500rs->send_buffer;

  T500RS_DBG(t500rs, "Sending initialization sequence...\n");

  /* Report 0x42 - Init/status commands (2 bytes each)
   * Windows sends these at startup: 0x42 0x04, 0x42 0x05, 0x42 0x00
   * These appear to initialize the FFB subsystem state.
   */
  memset(init_buf, 0, 2);
  init_buf[0] = 0x42;
  init_buf[1] = 0x04;
  ret = t500rs_send_usb(t500rs, init_buf, 2);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 0x42 0x04 failed: %d\n", ret);
  }

  memset(init_buf, 0, 2);
  init_buf[0] = 0x42;
  init_buf[1] = 0x05;
  ret = t500rs_send_usb(t500rs, init_buf, 2);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 0x42 0x05 failed: %d\n", ret);
  }

  memset(init_buf, 0, 2);
  init_buf[0] = 0x42;
  init_buf[1] = 0x00;
  ret = t500rs_send_usb(t500rs, init_buf, 2);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 0x42 0x00 failed: %d\n", ret);
  }

  /* Report 0x40 - Enable FFB (4 bytes)
   * Magic value seen in captures that enables FFB on the base.
   */
  memset(init_buf, 0, 4);
  init_buf[0] = 0x40;
  init_buf[1] = 0x11;
  init_buf[2] = 0x42;
  init_buf[3] = 0x7b;
  ret = t500rs_send_usb(t500rs, init_buf, 4);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 2 (0x40 enable) failed: %d\n", ret);
  }

  /* Report 0x40 - Disable built-in autocenter (4 bytes) */
  memset(init_buf, 0, 4);
  init_buf[0] = 0x40;
  init_buf[1] = 0x04;
  /* b2..b3 = 0x0000 -> disable autocenter.
   * Keep explicit zeros even though memset() clears them, to document the
   * wire image.
   */
  init_buf[2] = 0x00;
  init_buf[3] = 0x00;
  ret = t500rs_send_usb(t500rs, init_buf, 4);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 3 (0x40 config) failed: %d\n", ret);
  }

  /* Report 0x43 - Set global gain (2 bytes)
   * Start at maximum device gain; the FFB gain callback will adjust later.
   */
  memset(init_buf, 0, 2);
  init_buf[0] = 0x43;
  init_buf[1] = 0xFF;
  ret = t500rs_send_usb(t500rs, init_buf, 2);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 4 (0x43) failed: %d\n", ret);
  }

  /* The remaining initialization (0x05 spring zeroing and 0x41 STOP for
   * autocenter ID 15) is handled below.
   */

  /* Report 0x05 - Set deadband and center */
  memset(init_buf, 0, 11);
  init_buf[0] = 0x05;
  init_buf[1] = 0x1c;
  init_buf[2] = 0x00;
  init_buf[3] = 0x00;  /* Deadband = 0 */
  init_buf[4] = 0x00;  /* Center = 0 */
  init_buf[9] = 0x00;  /* Right saturation = 0 */
  init_buf[10] = 0x00; /* Left saturation = 0 */
  ret = t500rs_send_usb(t500rs, init_buf, 11);
  if (ret) {
    hid_warn(t500rs->hdev, "Disable autocenter (0x05 0x1c) failed: %d\n", ret);
  }

  /* Stop autocenter effect (effect ID 15) */
  {
    struct t500rs_r41_cmd *r41 = (struct t500rs_r41_cmd *)init_buf;
    r41->id = 0x41;
    r41->effect_id = 15; /* Autocenter effect ID */
    r41->command = 0x00; /* STOP */
    r41->arg = 0x01;
  }
  ret = t500rs_send_usb(t500rs, init_buf, sizeof(struct t500rs_r41_cmd));
  if (ret) {
    hid_warn(t500rs->hdev, "Stop autocenter effect failed: %d\n", ret);
  } else {
    T500RS_DBG(t500rs, "Autocenter fully disabled\n");
  }

  hid_info(t500rs->hdev,
           "T500RS initialized successfully (USB INTERRUPT mode)\n");
  T500RS_DBG(t500rs, "Endpoint: 0x%02x, Buffer: %zu bytes\n", t500rs->ep_out,
             t500rs->buffer_length);

  /* Advertise capabilities now that init succeeded */
  tmff2->params = t500rs_params;
  tmff2->max_effects = T500RS_MAX_EFFECTS;
  memcpy(tmff2->supported_effects, t500rs_effects, sizeof(t500rs_effects));

  return 0;

err_buffer:
  kfree(t500rs->send_buffer);
err_endpoint:
  kfree(t500rs);
err_alloc:
  return ret;
}

/* Cleanup T500RS device */
static int t500rs_wheel_destroy(void *data) {
  struct t500rs_device_entry *t500rs = data;

  if (!t500rs)
    return 0;

  T500RS_DBG(t500rs, "T500RS: Cleaning up\n");

  /* Free resources */
  kfree(t500rs->send_buffer);
  kfree(t500rs);

  return 0;
}

/* Populate API callbacks */
int t500rs_populate_api(struct tmff2_device_entry *tmff2) {

  tmff2->play_effect = t500rs_play_effect;
  tmff2->upload_effect = t500rs_upload_effect;
  tmff2->update_effect = t500rs_update_effect;
  tmff2->stop_effect = t500rs_stop_effect;

  tmff2->set_gain = t500rs_set_gain;
  tmff2->set_autocenter = t500rs_set_autocenter;
  tmff2->set_range = t500rs_set_range;

  tmff2->wheel_init = t500rs_wheel_init;
  tmff2->wheel_destroy = t500rs_wheel_destroy;

  return 0;
}
