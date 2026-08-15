// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for Thrustmaster T500RS wheel base that provides Force feedback
 *
 *  Protocol documentation: docs/T500RS_FFBEFFECTS.md
 *  Capture-derived protocol analysis: work/analysis/ (start at SUMMARY.md)
 *
 *  Reports observed in Windows captures that this driver deliberately does
 *  NOT produce or parse (see work/analysis/06_unknown_reports.md):
 *  - 0x0a (OUT, F1 rim only, 6x during init): attachment activation handshake
 *  - 42 01 00 (OUT, 9/15/32-byte variants): protocol re-sync / reset
 *  - 0x07 (IN, 230 Hz state report): handled by the stock HID parser
 *  - 0x14 (IN, 6x during init): device-identification report
 *  - vendor request 0x49 (host-polled during init): capability reply
 *
 *  Copyright (c) 2025 Casimir Bonnet <casimir.bonnet@gmail.com>
 */

#include "hid-tmt500rs.h"
#include "../hid-tmff2.h"
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/jiffies.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

/* Packet sequence templates for each effect type */
static const enum t500rs_seq_packet t500rs_seq_constant[] = {
	T500RS_SEQ_ENVELOPE,
	T500RS_SEQ_CONSTANT,
	T500RS_SEQ_MAIN,
};

static const enum t500rs_seq_packet t500rs_seq_periodic[] = {
	T500RS_SEQ_STOP,
	T500RS_SEQ_SYNC_42_05,
	T500RS_SEQ_SYNC_42_04,
	T500RS_SEQ_ENVELOPE,
	T500RS_SEQ_PERIODIC_RAMP,
	T500RS_SEQ_MAIN,
};

static const enum t500rs_seq_packet t500rs_seq_ramp[] = {
	T500RS_SEQ_STOP,
	T500RS_SEQ_ENVELOPE,
	T500RS_SEQ_PERIODIC_RAMP,
	T500RS_SEQ_MAIN,
};

static const enum t500rs_seq_packet t500rs_seq_condition[] = {
	T500RS_SEQ_CONDITION_X,
	T500RS_SEQ_CONDITION_Y,
	T500RS_SEQ_MAIN,
};

/* Scale constant level (-32767..32767) to signed 8-bit (-127..127) */
static inline s8 t500rs_scale_const_level_s8(int level)
{
	/* Input validation and clamping */
	if (level > 32767)
		level = 32767;
	if (level < -32767)
		level = -32767;

	/* Use 32-bit arithmetic to prevent overflow */
	return (s8)((level * 127LL) / 32767);
}

/* Apply effect direction to a constant level and convert to s8.
 * Mirrors t300rs_calculate_constant_level()'s projection semantics but
 * keeps the full T500RS range and uses t500rs_scale_const_level_s8() for
 * clamping and conversion.
 */
static inline s8 t500rs_scale_const_with_direction(int level, u16 direction)
{
	int projected;

	projected = (level * fixp_sin16(direction * 360 / 0x10000)) / 0x7fff;

	return t500rs_scale_const_level_s8(projected);
}

/*
 * T500RS encodes the effect "slot" in the parameter/envelope subtypes
 * (0x0e + 0x1c*n / 0x1c + 0x1c*n), AND in the 0x01/0x41 effect_id byte.
 * The effect_id byte mirrors the slot index (0=constant, 1+=non-constant),
 * matching the param_sub derivation in t500rs_index_to_subtypes() and the
 * captured Windows behaviour (see work/analysis/04_effect_id_bug.md).
 */

/* Map effect index to parameter/envelope subtypes as per protocol:
 *  param_sub = 0x000e + 0x001c * idx
 *  env_sub   = 0x001c + 0x001c * idx
 * idx is the per-effect slot index (callers pass effect->id + 1 so that
 * non-constant effects never collide with the constant force's fixed
 * index-0 subtypes). See docs/T500RS_FFBEFFECTS.md.
 */
static inline void t500rs_index_to_subtypes(unsigned int idx, u16 *param_sub,
					    u16 *env_sub)
{
	/* Validate inputs */
	if (idx >= T500RS_MAX_HW_EFFECTS) {
		idx = T500RS_MAX_HW_EFFECTS - 1; /* Clamp to valid range */
	}

	*param_sub = 0x000e + (0x001c * idx);
	*env_sub = 0x001c + (0x001c * idx);
}

/* Debug logging helper: pass struct t500rs_device_entry * explicitly */
#define T500RS_DBG(dev, fmt, ...) hid_dbg((dev)->hdev, fmt, ##__VA_ARGS__)

/* T500RS device data */
struct t500rs_device_entry {
	struct hid_device *hdev;
	struct input_dev *input_dev;

	u8 *send_buffer;
	size_t buffer_length;

	/*
	 * Software-expiry tracker. The T500RS hardware never auto-stops an
	 * effect: once STARTed it runs until an explicit 0x41 STOP. The tmff2
	 * core relies on hardware auto-stop (which T500RS lacks), so we enforce
	 * replay.length here. A single re-arming delayed_work scans active[]
	 * and sends a per-slot 0x41 STOP when each finite effect's time elapses.
	 *
	 * Per Windows USB captures (see work/analysis/04_effect_id_bug.md and
	 * 10_second_pass_findings.md), STOP is per-slot: the 0x41 effect_id
	 * byte addresses one slot at a time. There is no need for a "global
	 * STOP" or a playing-flag guard, because each STOP only halts its own
	 * effect.
	 *
	 * expiry_buffer is a dedicated DMA-safe buffer so the worker (which runs
	 * outside the core FFB worker) never races send_buffer (mirrors the
	 * set_range pattern).
	 */
	spinlock_t expiry_lock;
	struct delayed_work expiry_work;
	u8 *expiry_buffer;
	struct t500rs_active_effect {
		bool active; /* has a finite expiry deadline (drives expiry_work) */
		unsigned long start_ms; /* jiffies_to_msecs(jiffies) at play */
		unsigned long total_ms; /* (delay+length)*count; 0 == infinite */
	} active[T500RS_MAX_EFFECTS];
};

/*
 * Build a protocol-accurate 0x01 main upload packet.
 *
 * Per the T500RS USB protocol documentation:
 * - effect_id: 16-bit LE hardware effect slot (0..15 for now)
 * - duration_ms: duration in milliseconds
 * - delay_ms: delay before effect starts
 * - param_sub: parameter subtype (used by 0x03/0x04/0x05)
 * - envelope_sub: envelope subtype (used by 0x02), or second conditional
 * subtype
 *
 * Per Windows captures, effect_type values are:
 * - 0x00 = Constant
 * - 0x20 = Square, 0x21 = Triangle, 0x22 = Sine,
 *   0x23 = Sawtooth Up, 0x24 = Sawtooth Down
 *   (see docs/T500RS_FFBEFFECTS.md effect-type table)
 * - 0x40 = Spring
 * - 0x41 = Damper/Friction/Inertia
 *
 * NOTE: Direction is sent separately in a 0x03 packet for constant force,
 * not in this 0x01 packet.
 *
 * This is a pure constructor: callers must validate effect_id/effect_type
 * beforehand (the MAIN sequence step derives both from validated effect
 * fields; effect_id mirrors the hardware slot index via
 * t500rs_effect_to_hw_id()).
 */
static void t500rs_build_r01_main(struct t500rs_pkt_r01_main *p, u8 effect_id,
				  u8 effect_type, u16 duration_ms, u16 delay_ms,
				  u16 param_sub, u16 envelope_sub)
{
	memset(p, 0, sizeof(*p));
	p->id = T500RS_PKT_MAIN;
	p->effect_id = effect_id;
	p->effect_type = effect_type;
	p->control = T500RS_CONTROL_DEFAULT;
	p->duration_ms = cpu_to_le16(duration_ms);
	p->delay_ms = cpu_to_le16(delay_ms);
	p->reserved1 = 0;
	p->packet_code_1 = cpu_to_le16(param_sub);
	p->packet_code_2 = cpu_to_le16(envelope_sub);
	p->reserved2 = 0;
}

/*
 * Build a protocol-accurate 0x04 periodic/ramp packet.
 *
 * Per the T500RS USB protocol documentation:
 * - code: low byte of param_subtype from 0x01 (e.g., 0x2a for periodic, not
 * 0x0e!)
 * - magnitude: 0..127 (scaled from 0..32767)
 * - offset: signed DC offset (scaled from -32768..32767 to device range)
 * - phase: 0..255 (256 steps for 360 degrees, scaled from 0..35999)
 * - period_ms: period in MILLISECONDS (no Hz*100 conversion!)
 * - reserved: always 0
 *
 * Scaling formulas (from protocol doc):
 *   device_mag   = os_ffb_mag * 127 / 32767
 *   device_phase = (os_ffb_phase * 256 / 36000) & 0xFF
 *   device_offset = os_ffb_offset / 256  (TODO(hw-verify): unconfirmed)
 *   period_ms    = direct copy (no frequency conversion)
 *
 * CAPTURE-VERIFY: no community capture contains a real periodic packet in
 * this layout. Both captures' 32222 '0x04' packets all use code=0x0e with
 * the constant-force DC layout (b2/b3/b5=0, b4=signed level, b6-b7=magic
 * 0x2710) - see work/analysis/05_periodic_0x04_anomaly.md. The only
 * reference for THIS layout is the unsourced example '04 2a 06 00 3f 0a
 * 00 00' below. Verify by playing a sine via fftest (magnitude 16384,
 * period 100ms) and capturing with usbmon: expected packet is
 * '04 <code> 40 00 00 64 00 00'.
 */
static void t500rs_build_r04_periodic(struct t500rs_pkt_r04_periodic_ramp *p,
				      u8 code, u8 magnitude, s8 offset,
				      u8 phase, u16 period_ms)
{
	/* Byte order per Windows USB captures (example: 04 2a 06 00 3f 0a 00 00):
	* b0=T500RS_PKT_PERIODIC, b1=code, b2=mag, b3=offset,
	* b4=phase, b5-b6=period, b7=reserved
	*/
	memset(p, 0, sizeof(*p));
	p->id = T500RS_PKT_PERIODIC; /* b0 */
	p->code = code; /* b1 */
	p->magnitude = magnitude; /* b2 */
	p->offset = offset; /* b3 */
	p->phase = phase; /* b4 */
	p->period_ms = cpu_to_le16(period_ms); /* b5-b6 */
	/* p->reserved = 0 (cleared by memset) */
}

/*
 * Scale periodic magnitude with direction projection.
 *
 * For periodic effects, the direction determines the axis of oscillation.
 * We project the magnitude onto the wheel axis using sin(direction).
 *
 * When the projected magnitude is negative, we:
 * 1. Take the absolute value (wheel only supports positive magnitudes)
 * 2. Add 180 degrees to the phase to maintain correct force direction
 *
 * Linux FFB magnitude: 0..32767 (unsigned)
 * Linux FFB direction: 0..65535 (0=forward, 16384=right, 32768=back,
 * 49152=left) Linux FFB phase: 0..35999 (0..360 degrees in 1/100ths of a
 * degree; capped by the validator before this function is reached)
 * Device magnitude: 0..127
 *
 * @param os_ffb_mag: Original magnitude from Linux FFB (0..32767)
 * @param direction: Effect direction from Linux FFB (0..65535)
 * @param phase_ptr: Pointer to phase value; will be adjusted if projection is
 * negative
 * @return: Scaled magnitude (0..127)
 */
static inline u8 t500rs_scale_periodic_with_direction(int os_ffb_mag,
						      u16 direction,
						      u16 *phase_ptr)
{
	int projected;

	/* Project magnitude based on direction (same formula as T300RS) */
	projected =
		(os_ffb_mag * fixp_sin16(direction * 360 / 0x10000)) / 0x7fff;

	if (projected < 0) {
		/* Wheel handles positive magnitudes only */
		projected = -projected;

		/* Add 180 degrees to phase to maintain correct force direction.
		 * Phase is in 0..35999 (hundredths-of-a-degree) units here, so
		 * 180 deg = 18000 and a full circle = 36000. This must match
		 * t500rs_scale_periodic_phase(); do NOT use the 0x8000/0x10000
		 * values from the T300RS driver, which uses a different unit
		 * system (0..65535).
		 */
		if (phase_ptr)
			*phase_ptr = (*phase_ptr + 18000) % 36000;
	}

	/* Clamp to valid range */
	if (projected > 32767)
		projected = 32767;

	/* Scale to device range: 0..32767 -> 0..127 */
	return (u8)((projected * 127LL) / 32767);
}

/*
 * Scale periodic phase from Linux FFB subsystem format to device format.
 * Linux FFB: 0..35999 (0.01 degree units, 0-359.99 degrees)
 * Device: 0..255 (256 steps for 360 degrees)
 */
static inline u8 t500rs_scale_periodic_phase(u16 os_ffb_phase)
{
	/* Clamp to valid range just in case */
	if (os_ffb_phase > 35999)
		os_ffb_phase = 35999;
	return (u8)((os_ffb_phase * 256) / 36000);
}

/*
 * Scale periodic offset from Linux FFB subsystem format to device format.
 * Linux FFB: -32768..32767
 * Device: signed, stored as s8 (-128..127)
 *
 * TODO(hw-verify): exact mapping is unconfirmed; using simple /256 for
 * now. Capture a periodic effect with a known non-zero offset and verify
 * the device reproduces it correctly, then adjust the divisor if needed.
 */
static inline s8 t500rs_scale_periodic_offset(s16 os_ffb_offset)
{
	return (s8)(os_ffb_offset / 256);
}

/*
 * Build a 0x04 packet for ramp effects.
 *
 * Per the T500RS USB protocol documentation, ramp effects use the same
 * 0x04 packet structure as periodic effects. The encoding is:
 * - magnitude: scaled from start/end levels (midpoint or average)
 * - offset: difference between start and end (direction of ramp)
 * - phase: encodes ramp direction (0x7f = up, 0x00 = down)
 * - period_ms: ramp duration in milliseconds
 *
 * Note: TODO(hw-verify) the exact mapping of start/end to magnitude/offset
 * is unconfirmed; Windows captures show identical packets for different
 * ramp parameters. The current implementation uses a simple average for
 * magnitude. Capture ramps with varied start/end levels and confirm the
 * device reproduces the intended slope before trusting this encoding.
 * Test procedure: play ramps via fftest with (start,end) of (0,32767),
 * (32767,0), (-16384,16384), each 500ms, and capture with usbmon.
 * See work/analysis/03_packet_inventory.md and 09_action_items.md (P3-6).
 */
static void t500rs_build_r04_ramp(struct t500rs_pkt_r04_periodic_ramp *p,
				  u8 code, s16 start_level, s16 end_level,
				  u16 duration_ms)
{
	int avg_level;
	u8 magnitude;
	s8 offset;
	u8 phase;

	memset(p, 0, sizeof(*p));

	/* Compute average magnitude from start/end levels */
	avg_level = (abs(start_level) + abs(end_level)) / 2;
	magnitude = (u8)((avg_level * 127) / 32767);

	/* Offset encodes direction: positive = ramping up, negative = ramping down */
	/* TODO(hw-verify): (end - start) / 512 to fit in s8 range; divisor
	 * unconfirmed against captures. */
	offset = (s8)((end_level - start_level) / 512);

	/*
	* Phase encodes ramp direction per FFEdit captures:
	* - Positive ramp (start < end): phase = 0x7f (127)
	* - Negative ramp (start > end): phase = 0x00
	* - Equal levels: treat as positive (neutral case)
	*
	* Example captures:
	* - 049a0000007f0000 - phase 0x7f = positive/up direction
	* - 049a000c00000000 - phase 0x00 = negative/down direction
	*/
	phase = (start_level < end_level) ? 0x7f : 0x00;

	/* Field layout matches wire format (see struct t500rs_pkt_r04_periodic_ramp):
	 * b2=magnitude, b3=offset, b4=phase(direction), b5-b6=period_ms */
	memset(p, 0, sizeof(*p));
	p->id = 0x04; /* b0 */
	p->code = code; /* b1 */
	p->magnitude = magnitude; /* b2 */
	p->offset = offset; /* b3 */
	p->phase = phase; /* b4: direction (0x7f=up, 0x00=down) */
	p->period_ms = cpu_to_le16(duration_ms); /* b5-b6 */
	/* p->reserved = 0 (cleared by memset) */
}

/* Forward declarations for functions used by helper functions */
static int t500rs_send_hid(struct t500rs_device_entry *t500rs, u8 *data,
			   size_t len);
static int t500rs_send_stop(struct t500rs_device_entry *t500rs, u8 effect_id);
static int t500rs_send_start(struct t500rs_device_entry *t500rs, u8 effect_id);
static int t500rs_send_stop_now(struct t500rs_device_entry *t500rs, u8 *buf,
				u8 effect_id);
static void t500rs_expiry_work(struct work_struct *work);
static void t500rs_build_r03_constant(struct t500rs_r03_const *p, u8 code,
				      s8 level);
static void t500rs_build_r02_envelope(struct t500rs_pkt_r02_envelope *p,
				      u8 subtype, const struct ff_envelope *env,
				      bool allow_nonzero);

/* Saturation scaling constants */
#define T500RS_SATURATION_DEVICE_MAX 100
#define T500RS_SATURATION_LINUX_MAX 65535

/**
 * t500rs_scale_saturation - Scale saturation from Linux FFB to device range
 * @saturation: Linux FFB saturation value (0-65535)
 *
 * Returns: Scaled saturation value (0-100)
 *
 * Uses 32-bit arithmetic to prevent overflow and ensures accurate scaling.
 * The result is clamped to 0-100 range.
 */
static inline u8 t500rs_scale_saturation(u16 saturation)
{
	return (u8)min_t(u32,
		((u32)saturation * T500RS_SATURATION_DEVICE_MAX) /
		T500RS_SATURATION_LINUX_MAX,
		T500RS_SATURATION_DEVICE_MAX);
}

/*
 * Build a 0x05 conditional effect packet.
 *
 * Per captures (T500RS_FFBEFFECTS.md):
 * - packet structure with u8 coefficients and proper field layout
 * - Coefficients are sent as 0-10 scale (not zero)
 * - Center and deadband are scaled from Linux FFB ranges
 *
 * Parameters:
 * - code: From 0x01 packet bytes 9-10 (first packet) or 11-12 (second packet)
 * - right_coeff: Right/positive coefficient from ff_condition_effect (0-32767)
 * - left_coeff: Left/negative coefficient from ff_condition_effect (0-32767)
 * - saturation: Saturation value (0-100) for both right/left channels
 * - deadband: Deadband from ff_condition_effect (0-65535)
 * - center: Center offset from ff_condition_effect (-32767 to +32767)
 */
/* Resolve the per-effect-type strength level (0-100) for conditional effects.
 * Mirrors T300RS t300rs_calculate_coefficient()'s input_level selection:
 * spring/damper/friction honor their module params; inertia defaults to 100.
 *
 * The module params are 'int' and are not range-checked at module_param load
 * time; the sysfs store clamps >100 but not negatives. Clamp to [0,100] here
 * so an out-of-range/negative value cannot wrap through the u8 return and
 * skew coefficient scaling.
 */
static inline u8 t500rs_condition_level(u16 effect_type)
{
	int level;

	switch (effect_type) {
	case FF_SPRING:
		level = spring_level;
		break;
	case FF_DAMPER:
		level = damper_level;
		break;
	case FF_FRICTION:
		level = friction_level;
		break;
	default:
		level = 100;
		break;
	}

	return (u8)clamp_t(int, level, 0, 100);
}

static void t500rs_build_r05_condition(struct t500rs_pkt_r05_condition *p,
				       u8 code, s16 right_coeff, s16 left_coeff,
				       u8 level, u8 right_sat, u8 left_sat,
				       u16 deadband, s16 center)
{
	memset(p, 0, sizeof(*p));
	p->id = T500RS_PKT_CONDITIONAL;
	p->code = code;
	p->reserved = 0x00;

	/* Scale coefficients from Linux 0-32767 range to device 0-10 u8 scale,
	 * applying the per-effect-type strength level (spring/damper/friction
	 * module params), matching the T300RS t300rs_calculate_coefficient().
	 *
	 * right_coeff/left_coeff are __s16 and may be negative (the FF UAPI
	 * allows signed condition coefficients). The T500RS device field is an
	 * unsigned 0..10 strength byte (unlike T300RS's signed 16-bit field),
	 * so compute in int and clamp the result to [0,10]: a negative
	 * coefficient maps to 0 (no force) rather than wrapping to ~246, and
	 * any overflow saturates at 10.
	 *
	 * CAPTURE-VERIFY: unvalidated against a known input/output pair. The
	 * community captures only show the device-side bytes (C2 f2653:
	 * right_coeff=left_coeff=10 with unknown game input), which is
	 * consistent with this formula but does not prove it. Sweep
	 * right_coeff over {0, 8192, 16384, 24576, 32767} with spring_level=100
	 * via fftest and capture with usbmon; expected device bytes are
	 * {0, 2-3, 5, 7-8, 10}. See work/analysis/07_condition_deadband_unverified.md.
	 */
	p->right_coeff = (u8)clamp_t(int,
				((right_coeff * (int)level) / 100) * 10 / 32767,
				0, 10);
	p->left_coeff = (u8)clamp_t(int,
				((left_coeff * (int)level) / 100) * 10 / 32767,
				0, 10);

	/* Center: /20 confirmed by captures (e.g. center=-372 = -7439/20 in
	 * docs/T500RS_FFBEFFECTS.md capture C, and center=250 = 5000/20 in
	 * capture B). The doc's contradictory "/65" row is incorrect.
	 */
	p->center = cpu_to_le16((s16)(center / 20));

	/* Deadband: /65 is a guess between the doc's self-contradictory "/10"
	 * and "/65" rows. CAPTURE-VERIFY: every 0x05 packet in both community
	 * captures has deadband=0, so the divisor is completely unconstrained
	 * by evidence. "/65" was chosen because 65535/65 = 1008 fits a u10
	 * device field, whereas "/10" would give 6553 (overflows any field
	 * smaller than u16). Verify by uploading springs with deadband
	 * {100, 1000, 10000, 30000, 65535} via fftest and capturing with
	 * usbmon; expected device words are ~{1, 15, 153, 461, 1008}. See
	 * work/analysis/07_condition_deadband_unverified.md.
	 */
	p->deadband = cpu_to_le16((u16)(deadband / 65));

	p->right_sat = right_sat;
	p->left_sat = left_sat;
}

/*
 * Build and send a 0x05 conditional effect packet.
 *
 * This helper function encapsulates the common pattern of building and
 * sending a condition (spring/damper/friction/inertia) packet, reducing
 * code duplication and improving maintainability.
 *
 * Parameters:
 * - t500rs: Device context
 * - buf: Buffer to use for packet construction
 * - code: Packet code (from param_sub or env_sub)
 * - cond: Condition effect parameters
 *
 * Returns: 0 on success, negative errno on failure
 */
static int t500rs_send_condition_packet(struct t500rs_device_entry *t500rs,
					u8 *buf, u8 code,
					const struct ff_condition_effect *cond,
					u8 level)
{
	struct t500rs_pkt_r05_condition *p;

	if (!t500rs || !buf || !cond)
		return -EINVAL;

	/* Scale saturation from Linux FFB range to device range */
	u8 right_sat = t500rs_scale_saturation(cond->right_saturation);
	u8 left_sat = t500rs_scale_saturation(cond->left_saturation);

	/* Build and send the condition packet */
	p = (struct t500rs_pkt_r05_condition *)buf;
	t500rs_build_r05_condition(p, code, cond->right_coeff, cond->left_coeff,
				   level, right_sat, left_sat, cond->deadband,
				   cond->center);

	return t500rs_send_hid(t500rs, buf,
			       sizeof(struct t500rs_pkt_r05_condition));
}

/*
 * Build and send a 0x03 constant force packet.
 *
 * This helper function encapsulates the common pattern of building and
 * sending a constant force packet, reducing code duplication and improving
 * maintainability. Handles level scaling with direction projection.
 *
 * Parameters:
 * - t500rs: Device context
 * - buf: Buffer to use for packet construction
 * - code: Packet code (from param_sub)
 * - level: Constant force level (-32767 to 32767)
 * - direction: Effect direction (0-65535)
 *
 * Returns: 0 on success, negative errno on failure
 */
static int t500rs_send_constant_packet(struct t500rs_device_entry *t500rs,
				       u8 *buf, u8 code,
				       s16 level, u16 direction)
{
	struct t500rs_r03_const *r3;
	s8 scaled_level;

	if (!t500rs || !buf)
		return -EINVAL;

	/* Scale level with direction projection */
	scaled_level = t500rs_scale_const_with_direction(level, direction);

	/* Build and send packet */
	r3 = (struct t500rs_r03_const *)buf;
	t500rs_build_r03_constant(r3, code, scaled_level);

	return t500rs_send_hid(t500rs, buf, sizeof(*r3));
}

/*
 * Build and send a 0x04 periodic effect packet.
 *
 * This helper function encapsulates the common pattern of building and
 * sending a periodic effect packet, reducing code duplication and improving
 * maintainability. Handles magnitude scaling with direction projection,
 * phase adjustment, and period validation.
 *
 * Parameters:
 * - t500rs: Device context
 * - buf: Buffer to use for packet construction
 * - code: Packet code (from param_sub)
 * - periodic: Periodic effect parameters
 * - direction: Effect direction (0-65535)
 *
 * Returns: 0 on success, negative errno on failure
 */
static int t500rs_send_periodic_packet(struct t500rs_device_entry *t500rs,
				       u8 *buf, u8 code,
				       const struct ff_periodic_effect *periodic,
				       u16 direction)
{
	struct t500rs_pkt_r04_periodic_ramp *p;
	u16 phase_raw;
	u8 mag, phase;
	s8 offset;
	u16 period_ms;

	if (!t500rs || !buf || !periodic)
		return -EINVAL;

	/* Validate period */
	period_ms = periodic->period;
	if (period_ms == 0) {
		hid_err(t500rs->hdev,
			"Periodic effect period cannot be zero\n");
		return -EINVAL;
	}

	/* Apply direction projection to magnitude and adjust phase */
	phase_raw = periodic->phase;
	mag = t500rs_scale_periodic_with_direction(
		periodic->magnitude, direction, &phase_raw);
	phase = t500rs_scale_periodic_phase(phase_raw);
	offset = t500rs_scale_periodic_offset(periodic->offset);

	/* Build and send packet */
	p = (struct t500rs_pkt_r04_periodic_ramp *)buf;
	t500rs_build_r04_periodic(p, code, mag, offset, phase, period_ms);

	return t500rs_send_hid(t500rs, buf, sizeof(*p));
}

/*
 * Build and send a 0x04 ramp effect packet.
 *
 * This helper function encapsulates the common pattern of building and
 * sending a ramp effect packet, reducing code duplication and improving
 * maintainability. Ramp effects use the same 0x04 packet structure as
 * periodic effects.
 *
 * Parameters:
 * - t500rs: Device context
 * - buf: Buffer to use for packet construction
 * - code: Packet code (from param_sub)
 * - ramp: Ramp effect parameters
 * - duration_ms: Ramp duration in milliseconds
 *
 * Returns: 0 on success, negative errno on failure
 */
static int t500rs_send_ramp_packet(struct t500rs_device_entry *t500rs,
				   u8 *buf, u8 code,
				   const struct ff_ramp_effect *ramp,
				   u16 duration_ms)
{
	struct t500rs_pkt_r04_periodic_ramp *p;

	if (!t500rs || !buf || !ramp)
		return -EINVAL;

	/* Validate duration */
	if (duration_ms == 0) {
		hid_err(t500rs->hdev,
			"Ramp effect duration cannot be zero\n");
		return -EINVAL;
	}

	/* Build and send ramp packet */
	p = (struct t500rs_pkt_r04_periodic_ramp *)buf;
	t500rs_build_r04_ramp(p, code, ramp->start_level,
			     ramp->end_level, duration_ms);

	return t500rs_send_hid(t500rs, buf, sizeof(*p));
}

/*
 * Build and send a 0x02 envelope packet.
 *
 * This helper function encapsulates the common pattern of building and
 * sending an envelope packet, reducing code duplication and improving
 * maintainability. Determines envelope availability based on effect type.
 *
 * Per firmware behavior, only ramp effects support non-zero envelope values.
 * Periodic and constant effects must send zero envelope values due to
 * firmware limitations.
 *
 * Parameters:
 * - t500rs: Device context
 * - buf: Buffer to use for packet construction
 * - subtype: Envelope subtype (from env_sub)
 * - effect: Effect containing envelope parameters
 *
 * Returns: 0 on success, negative errno on failure
 */
static int t500rs_send_envelope_packet(struct t500rs_device_entry *t500rs,
				       u8 *buf, u8 subtype,
				       const struct ff_effect *effect)
{
	struct t500rs_pkt_r02_envelope *env;
	const struct ff_envelope *envelope = NULL;
	bool allow_envelope = false;

	if (!t500rs || !buf || !effect)
		return -EINVAL;

	/* Determine envelope availability based on effect type */
	switch (effect->type) {
	case FF_RAMP:
		envelope = &effect->u.ramp.envelope;
		allow_envelope = true;
		break;
	case FF_CONSTANT:
	case FF_PERIODIC:
		envelope = &effect->u.periodic.envelope;
		allow_envelope = false; /* Firmware bug: must send zeros */
		break;
	default:
		/* No envelope for this effect type */
		envelope = NULL;
		allow_envelope = false;
		break;
	}

	/* Build and send envelope packet */
	env = (struct t500rs_pkt_r02_envelope *)buf;
	t500rs_build_r02_envelope(env, subtype, envelope, allow_envelope);

	return t500rs_send_hid(t500rs, buf, sizeof(*env));
}

/*
 * Build a 0x03 constant force packet.
 *
 * Per the T500RS USB protocol documentation:
 * - code: low byte of param_subtype from 0x01 (e.g., 0x0e)
 * - reserved: always 0x00
 * - level: signed -127 to +127
 */
static void t500rs_build_r03_constant(struct t500rs_r03_const *p, u8 code,
				      s8 level)
{
	p->id = T500RS_PKT_CONSTANT;
	p->code = code;
	p->zero = 0x00;
	p->level = level;
}

/*
 * Scale envelope level from Linux FFB subsystem format to device format.
 * Linux FFB : 0-32767
 * Device: 0-255
 * Formula: device_level = os_ffb_level * 255 / 32767
 */
static inline u8 t500rs_scale_envelope_level(u16 os_ffb_level)
{
	/* Input validation and clamping */
	if (os_ffb_level > 32767)
		os_ffb_level = 32767;

	/* Use long long arithmetic to prevent overflow */
	return (u8)((os_ffb_level * 255LL) / 32767);
}

/*
 * Build a protocol-accurate 0x02 envelope packet.
 *
 * Per the T500RS USB protocol documentation:
 * - subtype: low byte of env_sub from 0x01 (e.g., 0x1c)
 * - attack_len: attack duration in milliseconds
 * - attack_level: 0-255 (scaled from Linux FFB 0-32767)
 * - fade_len: fade duration in milliseconds
 * - fade_level: 0-255 (scaled from Linux FFB 0-32767)
 * - reserved: always 0x00
 */
static void t500rs_build_r02_envelope(struct t500rs_pkt_r02_envelope *p,
				      u8 subtype, const struct ff_envelope *env,
				      bool allow_nonzero)
{
	memset(p, 0, sizeof(*p));
	p->id = 0x02;
	p->subtype = subtype;

	/*
	* Per T500RS_EFFECTS.md, the device firmware rejects
	* non-zero envelope values for periodic and constant effects with
	* EPROTO (-71). Only ramp effects can safely use envelopes.
	*
	* Windows driver always sends zeros for periodic/constant:
	* 02 38 00 00 00 00 00 00 00
	*
	* CAPTURE-VERIFY: every 0x02 packet in both community captures is
	* all-zero (5 packets across the two games), so the non-zero ramp
	* path below has never been observed on the wire. The EPROTO claim
	* and the ramp exception both need hardware confirmation: play a
	* ramp with attack/fade (e.g. attack 100ms/50%, fade 100ms/50%) via
	* fftest and capture with usbmon. See work/analysis/03_packet_inventory.md
	* (0x02 section) and 09_action_items.md (P3-6).
	*/
	if (env && allow_nonzero) {
		p->attack_len = cpu_to_le16(env->attack_length);
		p->attack_level =
			t500rs_scale_envelope_level(env->attack_level);
		p->fade_len = cpu_to_le16(env->fade_length);
		p->fade_level = t500rs_scale_envelope_level(env->fade_level);
	} else if (env && (env->attack_length || env->attack_level ||
			   env->fade_length || env->fade_level)) {
		/* The user supplied a non-zero envelope that the device cannot
		 * apply to this effect type (firmware rejects it with EPROTO).
		 * Warn once and silently drop it by sending zeros.
		 */
		pr_warn_once(
			"t500rs: non-zero envelope ignored for this effect type\n");
	}
	/* else: zero/no envelope -> sending zeros is normal protocol behavior */
}

/* Supported parameters */
static unsigned long t500rs_params = PARAM_SPRING_LEVEL | PARAM_DAMPER_LEVEL |
				     PARAM_FRICTION_LEVEL | PARAM_GAIN |
				     PARAM_RANGE;

/* Supported effects. */
const signed short t500rs_effects[] = { FF_CONSTANT, FF_SPRING,	    FF_DAMPER,
					FF_FRICTION, FF_INERTIA,    FF_PERIODIC,
					FF_SQUARE,   FF_SINE,	    FF_TRIANGLE,
					FF_SAW_UP,   FF_SAW_DOWN,   FF_RAMP,
					FF_GAIN,     FF_AUTOCENTER, -1 };

/*
 * Resolve the hardware effect slot index for a given effect.
 *
 * Per Windows USB captures (work/analysis/04_effect_id_bug.md, validated
 * across all 8 0x01 and 10 0x41 packets in both community captures), the
 * protocol mirrors the param_sub derivation:
 *
 *   slot 0   -> param_sub=0x000e, env_sub=0x001c  (constant force)
 *   slot n>0 -> param_sub=0x000e+0x001c*n, env_sub=0x001c+0x001c*n
 *
 * The driver keeps constant force pinned to slot 0 (its subtypes are fixed
 * in the firmware) and assigns every other effect slot n = effect->id + 1,
 * so the hardware slot mirrors the per-effect subtype channel.
 */
static u8 t500rs_effect_to_hw_id(const struct ff_effect *effect)
{
	if (effect->type == FF_CONSTANT)
		return 0;
	return (u8)(effect->id + 1);
}

/*
 * Send a sequence of packets for effect upload.
 * Abstracts the hardcoded packet orders in upload functions.
 *
 * The 0x01 effect_id byte and the 0x41 START/STOP effect_id byte both mirror
 * the hardware slot derived above. Constant force uses fixed subtypes
 * (T500RS_CONSTANT_PARAM_SUB/ENV_SUB); every other effect derives subtypes
 * from its logical id (effect->id + 1).
 */
static int t500rs_send_packet_sequence(struct t500rs_device_entry *t500rs,
				       const struct tmff2_effect_state *state,
				       const enum t500rs_seq_packet *sequence,
				       size_t seq_len)
{
	const struct ff_effect *effect = &state->effect;
	u8 *buf = t500rs->send_buffer;
	u8 hw_id = t500rs_effect_to_hw_id(effect);
	int ret;
	u16 param_sub, env_sub;

	if (effect->type == FF_CONSTANT) {
		param_sub = T500RS_CONSTANT_PARAM_SUB;
		env_sub = T500RS_CONSTANT_ENV_SUB;
	} else {
		t500rs_index_to_subtypes(effect->id + 1, &param_sub, &env_sub);
	}

	for (size_t i = 0; i < seq_len; i++) {
		/* Log sequence progress for debugging */
		T500RS_DBG(t500rs,
			   "Sequence step %zu/%zu: packet type 0x%02x\n", i + 1,
			   seq_len, sequence[i]);

		switch (sequence[i]) {
		case T500RS_SEQ_STOP:
			ret = t500rs_send_stop(t500rs, hw_id);
			break;

		case T500RS_SEQ_SYNC_42_05:
			buf[0] = 0x42;
			buf[1] = 0x05;
			ret = t500rs_send_hid(t500rs, buf, 2);
			break;

		case T500RS_SEQ_SYNC_42_04:
			buf[0] = 0x42;
			buf[1] = 0x04;
			ret = t500rs_send_hid(t500rs, buf, 2);
			break;

		case T500RS_SEQ_ENVELOPE: {
			ret = t500rs_send_envelope_packet(t500rs, buf,
							  (u8)env_sub, effect);
			break;
		}

		case T500RS_SEQ_CONSTANT: {
			ret = t500rs_send_constant_packet(t500rs, buf,
							  (u8)param_sub,
							  effect->u.constant.level,
							  effect->direction);
			break;
		}

		case T500RS_SEQ_PERIODIC_RAMP: {
			if (effect->type == FF_RAMP) {
				ret = t500rs_send_ramp_packet(t500rs, buf,
							     (u8)param_sub,
							     &effect->u.ramp,
							     effect->replay.length);
			} else {
				ret = t500rs_send_periodic_packet(t500rs, buf,
								  (u8)param_sub,
								  &effect->u.periodic,
								  effect->direction);
			}
			break;
		}

		case T500RS_SEQ_CONDITION_X: {
			const struct ff_condition_effect *cond =
				&effect->u.condition[0];
			ret = t500rs_send_condition_packet(t500rs, buf,
							   (u8)param_sub, cond,
							   t500rs_condition_level(effect->type));
			break;
		}

		case T500RS_SEQ_CONDITION_Y: {
			/* Y-axis: use condition[1] if available, else zeros */
			const struct ff_condition_effect *cond =
				&effect->u.condition[1];
			ret = t500rs_send_condition_packet(t500rs, buf,
							   (u8)env_sub, cond,
							   t500rs_condition_level(effect->type));
			break;
		}

		case T500RS_SEQ_MAIN: {
			u8 effect_type = 0;
			switch (effect->type) {
			case FF_CONSTANT:
				effect_type = T500RS_EFFECT_CONSTANT;
				break;
			case FF_SPRING:
				effect_type = T500RS_EFFECT_SPRING;
				break;
			case FF_DAMPER:
				effect_type = T500RS_EFFECT_DAMPER;
				break;
			case FF_FRICTION:
				effect_type = T500RS_EFFECT_FRICTION;
				break;
			case FF_INERTIA:
				effect_type = T500RS_EFFECT_INERTIA;
				break;
			case FF_PERIODIC:
				switch (effect->u.periodic.waveform) {
				case FF_SQUARE:
					effect_type = T500RS_EFFECT_SQUARE;
					break;
				case FF_SINE:
					effect_type = T500RS_EFFECT_SINE;
					break;
				case FF_TRIANGLE:
					effect_type = T500RS_EFFECT_TRIANGLE;
					break;
				case FF_SAW_UP:
					effect_type = T500RS_EFFECT_SAW_UP;
					break;
				case FF_SAW_DOWN:
					effect_type = T500RS_EFFECT_SAW_DOWN;
					break;
				default:
					return -EINVAL;
				}
				break;
			case FF_RAMP:
				/* Intentional: ramps reuse the sawtooth-down
				 * effect type code per captures
				 * (docs/T500RS_FFBEFFECTS.md "RAMP EFFECTS":
				 * effect_type = 0x24). Do not "fix" this to a
				 * ramp-specific code. */
				effect_type = T500RS_EFFECT_SAW_DOWN;
				break;
			default:
				return -EINVAL;
			}

			u16 duration_ms = effect->replay.length ?
						  effect->replay.length :
						  0xffff;
			u16 delay_ms = effect->replay.delay;

			struct t500rs_pkt_r01_main *m =
				(struct t500rs_pkt_r01_main *)buf;
			t500rs_build_r01_main(m, hw_id, effect_type,
					      duration_ms, delay_ms, param_sub,
					      env_sub);

			ret = t500rs_send_hid(
				t500rs, buf,
				sizeof(struct t500rs_pkt_r01_main));
			break;
		}

		default:
			ret = -EINVAL;
		}

		if (ret) {
			hid_err(t500rs->hdev,
				"Sequence failed at step %zu/%zu (packet type 0x%02x): %d\n",
				i + 1, seq_len, sequence[i], ret);
			return ret;
		}
	}

	T500RS_DBG(t500rs, "Sequence completed successfully (%zu packets)\n",
		   seq_len);
	return 0;
}

static int t500rs_set_gain(void *data, u16 gain)
{
	struct t500rs_device_entry *t500rs = data;
	u8 *buf;
	u8 device_gain_byte;
	int ret;

	if (!t500rs->send_buffer) {
		hid_err(t500rs->hdev, "t500rs_set_gain: NULL send buffer\n");
		return -ENOMEM;
	}

	buf = t500rs->send_buffer;

	/* Scale 0..65535 to device 0..255 */
	device_gain_byte = (u8)((gain * 255ULL) / T500RS_GAIN_MAX);

	/* Per-frame gain changes are common; keep this at dbg to avoid
	 * flooding dmesg. */
	hid_dbg(t500rs->hdev, "FFB: set_gain %u -> device %u\n", gain,
		device_gain_byte);

	buf[0] = T500RS_PKT_GAIN;
	buf[1] = device_gain_byte;

	ret = t500rs_send_hid(t500rs, buf, 2);
	if (ret)
		hid_err(t500rs->hdev, "FFB: Failed to set gain: %d\n", ret);
	return ret;
}

/* Send data via HID output report (blocking) */
static int t500rs_send_hid(struct t500rs_device_entry *t500rs, u8 *data,
			   size_t len)
{
	int ret;

	/* Input validation */
	if (len == 0 || len > T500RS_BUFFER_LENGTH) {
		hid_err(t500rs->hdev,
			"t500rs_send_hid: Invalid length %zu (max %d)\n", len,
			T500RS_BUFFER_LENGTH);
		return -EINVAL;
	}

	ret = hid_hw_output_report(t500rs->hdev, data, len);
	if (ret < 0) {
		hid_err(t500rs->hdev, "HID output report failed: %d\n", ret);
		return ret;
	}

	if (ret != len) {
		hid_err(t500rs->hdev,
			"HID output report truncated: sent %d, expected %zu\n",
			ret, len);
		return -EIO;
	}

	return 0;
}

/*
 * Send STOP command (0x41) for the given hardware slot into the supplied
 * buffer. Callers in the core FFB worker use t500rs_send_stop() (shared
 * send_buffer); the expiry worker uses this with its own DMA-safe buffer so
 * it never races the core worker.
 */
static int t500rs_send_stop_now(struct t500rs_device_entry *t500rs, u8 *buf,
				u8 effect_id)
{
	struct t500rs_r41_cmd *r41;

	if (!t500rs)
		return -ENODEV;
	if (!buf)
		return -ENOMEM;

	r41 = (struct t500rs_r41_cmd *)buf;
	r41->id = 0x41;
	r41->effect_id = effect_id;
	r41->command = 0x00; /* STOP */
	r41->arg = 0x01;
	return t500rs_send_hid(t500rs, (u8 *)r41, sizeof(*r41));
}

/*
 * Send STOP command for the given hardware slot. Per protocol the 0x41
 * effect_id addresses one slot at a time; this halts only that slot's
 * playback, leaving all other slots intact.
 */
static int t500rs_send_stop(struct t500rs_device_entry *t500rs, u8 effect_id)
{
	if (!t500rs)
		return -ENODEV;
	if (!t500rs->send_buffer)
		return -ENOMEM;
	return t500rs_send_stop_now(t500rs, t500rs->send_buffer, effect_id);
}

/*
 * Send START command (0x41) for the given hardware slot. No duration/count
 * field: the T500RS runs the effect until an explicit 0x41 STOP, which the
 * driver enforces in software via the expiry tracker.
 */
static int t500rs_send_start(struct t500rs_device_entry *t500rs, u8 effect_id)
{
	struct t500rs_r41_cmd *r41;

	if (!t500rs)
		return -ENODEV;
	if (!t500rs->send_buffer)
		return -ENOMEM;

	r41 = (struct t500rs_r41_cmd *)t500rs->send_buffer;
	r41->id = 0x41;
	r41->effect_id = effect_id;
	r41->command = 0x41; /* START */
	/* arg=0xff matches the dominant Windows pattern (rFactor2 C2 frames 2651,
	 * 2659, 373753, 373823 all use 41 0X 41 ff). C1's '41 0X 41 01' is the
	 * only known counter-example; 0xff is the safer default for START. */
	r41->arg = 0xff;
	return t500rs_send_hid(t500rs, (u8 *)r41, sizeof(*r41));
}

/* Upload constant force effect */
static int t500rs_upload_constant(struct t500rs_device_entry *t500rs,
				  const struct tmff2_effect_state *state)
{
	const struct ff_effect *effect = &state->effect;
	int ret;
	int level = effect->u.constant.level;

	/* Note: Gain is applied in play_effect, not here */
	T500RS_DBG(t500rs, "Upload constant: id=%d, level=%d, dir=%u\n",
		   effect->id, level, effect->direction);

	/* Send packet sequence for constant effect. Constant force uses
	 * fixed subtypes (T500RS_CONSTANT_PARAM_SUB/ENV_SUB) and hardware
	 * slot 0. */
	ret = t500rs_send_packet_sequence(
		t500rs, state, t500rs_seq_constant,
		sizeof(t500rs_seq_constant) / sizeof(t500rs_seq_constant[0]));
	if (ret) {
		hid_err(t500rs->hdev,
			"Failed to send constant effect sequence: %d\n", ret);
		return ret;
	}

	T500RS_DBG(t500rs, "Constant effect %d uploaded\n", effect->id);
	return 0;
}

/*
 * Upload spring/damper/friction/inertia effect.
 *
 * Per Windows captures (T500RS_FFBEFFECTS.md):
 * - 0x01 packet: direction=0x4000, param_sub=0x002a, envelope_sub=0x0038
 * - Two 0x05 packets: X-axis (code 0x2a) and Y-axis (code 0x38)
 * - Saturation values 0x54 (84) for spring, 0x64 (100) for damper/friction
 */
static int t500rs_upload_condition(struct t500rs_device_entry *t500rs,
				   const struct tmff2_effect_state *state)
{
	const struct ff_effect *effect = &state->effect;
	int ret;
	const char *type_name;

	/* Resolve the effect name for diagnostics. The hardware effect_type
	 * code and the per-type strength level are derived inside the packet
	 * sequence (MAIN step) and t500rs_condition_level() respectively.
	 */
	switch (effect->type) {
	case FF_SPRING:
		type_name = "spring";
		break;
	case FF_DAMPER:
		type_name = "damper";
		break;
	case FF_FRICTION:
		type_name = "friction";
		break;
	case FF_INERTIA:
		type_name = "inertia";
		break;
	default:
		return -EINVAL;
	}

	/* Send packet sequence for conditional effect */
	ret = t500rs_send_packet_sequence(
		t500rs, state, t500rs_seq_condition,
		sizeof(t500rs_seq_condition) / sizeof(t500rs_seq_condition[0]));
	if (ret) {
		hid_err(t500rs->hdev, "Failed to send %s effect sequence: %d\n",
			type_name, ret);
		return ret;
	}

	return 0;
}

/*
 * Upload periodic effect (sine, square, triangle, saw).
 *
 * Per Windows captures (T500RS_FFBEFFECTS.md):
 * - Waveform type is NOT encoded in USB packets; determined by Linux FFB
 * subsystem
 * - 0x01 packet: direction, duration, delay, param_sub=0x000e,
 * envelope_sub=0x001c
 * - 0x02 packet: envelope with subtype 0x1c
 * - 0x04 packet: code=0x2a (NOT 0x0e!), magnitude, offset, phase, period_ms
 * - Period is in MILLISECONDS (no Hz*100 conversion)
 *
 * NOTE: The current implementation only sends the simplified packet sequence
 * observed in Windows captures. The dual-0x01/0x02 sequence in the old code
 * may have been incorrect and is removed.
 */
static int t500rs_upload_periodic(struct t500rs_device_entry *t500rs,
				  const struct tmff2_effect_state *state)
{
	const struct ff_effect *effect = &state->effect;
	int ret;
	const char *type_name;
	u8 effect_type;

	/*
	* Determine waveform name and effect_type for 0x01 packet.
	*
	* Per Windows captures, waveform type IS encoded in the 0x01 packet's
	* effect_type field (byte 2).
	*
	* Effect type values for periodic waveforms:
	* - 0x20 = Square
	* - 0x21 = Triangle
	* - 0x22 = Sine
	* - 0x23 = Sawtooth Up
	* - 0x24 = Sawtooth Down
	*/
	switch (effect->u.periodic.waveform) {
	case FF_SQUARE:
		type_name = "square";
		effect_type = T500RS_EFFECT_SQUARE;
		break;
	case FF_TRIANGLE:
		type_name = "triangle";
		effect_type = T500RS_EFFECT_TRIANGLE;
		break;
	case FF_SINE:
		type_name = "sine";
		effect_type = T500RS_EFFECT_SINE;
		break;
	case FF_SAW_UP:
		type_name = "sawtooth_up";
		effect_type = T500RS_EFFECT_SAW_UP;
		break;
	case FF_SAW_DOWN:
		type_name = "sawtooth_down";
		effect_type = T500RS_EFFECT_SAW_DOWN;
		break;
	default:
		hid_err(t500rs->hdev, "Unsupported periodic waveform: %d\n",
			effect->u.periodic.waveform);
		return -EINVAL;
	}

	/* Send packet sequence for periodic effect */
	ret = t500rs_send_packet_sequence(
		t500rs, state, t500rs_seq_periodic,
		sizeof(t500rs_seq_periodic) / sizeof(t500rs_seq_periodic[0]));
	if (ret) {
		hid_err(t500rs->hdev, "Failed to send %s effect sequence: %d\n",
			type_name, ret);
		return ret;
	}

	T500RS_DBG(t500rs, "%s effect %d uploaded\n", type_name, effect->id);
	return 0;
}

/*
 * Upload ramp effect.
 *
 * Per Windows captures (T500RS_FFBEFFECTS.md):
 * - Ramp uses same 0x04 packet structure as periodic (code 0x2a)
 * - Packet sequence: 0x01 + 0x02 + 0x04 + 0x41
 * - Start/end levels encoded in magnitude/offset fields
 * - Period field encodes ramp duration
 */
static int t500rs_upload_ramp(struct t500rs_device_entry *t500rs,
			      const struct tmff2_effect_state *state)
{
	const struct ff_effect *effect = &state->effect;
	int ret;

	/* Send packet sequence for ramp effect */
	ret = t500rs_send_packet_sequence(t500rs, state, t500rs_seq_ramp,
					  sizeof(t500rs_seq_ramp) /
						  sizeof(t500rs_seq_ramp[0]));
	if (ret) {
		hid_err(t500rs->hdev,
			"Failed to send ramp effect sequence: %d\n", ret);
		return ret;
	}

	T500RS_DBG(t500rs, "Ramp effect %d uploaded\n", effect->id);
	return 0;
}

/* Upload effect */
static int t500rs_upload_effect(void *data,
				const struct tmff2_effect_state *state)
{
	struct t500rs_device_entry *t500rs = data;
	const struct ff_effect *effect;
	int ret;

	effect = &state->effect;

	/* Validate effect ID range */
	if (effect->id >= T500RS_MAX_EFFECTS) {
		hid_err(t500rs->hdev, "Effect ID %d exceeds maximum %d\n",
			effect->id, T500RS_MAX_EFFECTS);
		return -EINVAL;
	}

	/* Validate effect parameters based on type */
	/* Per-type range checks. We only reject values that are genuinely
	 * malformed against the Linux input UAPI contract; values within the
	 * field's representable range are handled by the scaling helpers'
	 * clamping, so they are not validated here.
	 *
	 * Specifically NOT checked (all either impossible for the field's
	 * type or already covered by helper clamping):
	 *  - constant.level  (__s16; helper clamps to [-32767,32767])
	 *  - periodic.magnitude (__u16; helper clamps projected value)
	 *  - periodic.offset (__s16; full range handled by /256 scaling)
	 *  - ramp start/end_level (__s16; builder uses abs()/division)
	 *  - replay.delay (__u16; cannot exceed 65535)
	 */
	switch (effect->type) {
	case FF_CONSTANT:
	case FF_RAMP:
	case FF_SPRING:
	case FF_DAMPER:
	case FF_FRICTION:
	case FF_INERTIA:
		break;

	case FF_PERIODIC:
		/* phase is documented in the UAPI as 0..35999 (1/100 deg);
		 * reject a malformed value rather than silently clamping it
		 * (clamping would subtly shift the phase).
		 */
		if (effect->u.periodic.phase > 35999) {
			hid_err(t500rs->hdev,
				"Periodic phase %u exceeds maximum 35999\n",
				effect->u.periodic.phase);
			return -EINVAL;
		}
		break;

	default:
		hid_err(t500rs->hdev, "Unsupported effect type: %d\n",
			effect->type);
		return -EINVAL;
	}

	/* Direction is provided by the Linux FF subsystem as 0..65535 (u16);
	 * direction projection is applied in the per-effect scaling helpers
	 * (t500rs_scale_const_with_direction / t500rs_scale_periodic_with_
	 * direction), so accept the full u16 range here. */

	switch (effect->type) {
	case FF_CONSTANT:
		ret = t500rs_upload_constant(t500rs, state);
		break;
	case FF_SPRING:
	case FF_DAMPER:
	case FF_FRICTION:
	case FF_INERTIA:
		ret = t500rs_upload_condition(t500rs, state);
		break;
	case FF_PERIODIC:
		ret = t500rs_upload_periodic(t500rs, state);
		break;
	case FF_RAMP:
		ret = t500rs_upload_ramp(t500rs, state);
		break;
	default:
		hid_err(t500rs->hdev, "Unsupported effect type: %d\n",
			effect->type);
		return -EINVAL;
	}

	if (ret < 0) {
		hid_err(t500rs->hdev,
			"Failed to upload effect type %d, id %d: %d\n",
			effect->type, effect->id, ret);
	}
	return ret;
}

/*
 * (Re)arm the expiry worker for the soonest still-active finite effect.
 * Caller must hold t500rs->expiry_lock. Finite effects (total_ms != 0) get a
 * delayed_work at their deadline; infinite effects (total_ms == 0) never
 * auto-stop (Linux FFB semantics) and are skipped. If nothing finite is
 * pending the worker is cancelled.
 */
static void t500rs_expiry_arm_locked(struct t500rs_device_entry *t500rs)
{
	unsigned long soonest = 0;
	bool found = false;

	for (int i = 0; i < T500RS_MAX_EFFECTS; i++) {
		struct t500rs_active_effect *a = &t500rs->active[i];
		unsigned long deadline;

		if (!a->active || a->total_ms == 0)
			continue;
		deadline = a->start_ms + a->total_ms;
		if (!found || time_before(deadline, soonest)) {
			soonest = deadline;
			found = true;
		}
	}

	if (found) {
		long delay = (long)soonest - (long)jiffies;

		if (delay < 0)
			delay = 0;
		mod_delayed_work(system_wq, &t500rs->expiry_work,
				 (unsigned long)delay);
	} else {
		cancel_delayed_work(&t500rs->expiry_work);
	}
}

/*
 * Expiry worker. Scans active[]; any finite effect whose time has elapsed is
 * marked inactive and a per-slot 0x41 STOP is sent for it. Each STOP halts
 * only its own slot, so concurrent effects are not disturbed.
 */
static void t500rs_expiry_work(struct work_struct *work)
{
	struct t500rs_device_entry *t500rs =
		container_of(to_delayed_work(work), struct t500rs_device_entry,
			     expiry_work);
	unsigned long flags;
	unsigned long now = jiffies_to_msecs(jiffies);

	spin_lock_irqsave(&t500rs->expiry_lock, flags);
	for (int i = 0; i < T500RS_MAX_EFFECTS; i++) {
		struct t500rs_active_effect *a = &t500rs->active[i];

		if (!a->active || a->total_ms == 0)
			continue;
		if (now < a->start_ms + a->total_ms)
			continue;

		a->active = false;
		/* Slot index derivation mirrors t500rs_effect_to_hw_id(): slot 0
		 * is the constant-force slot; every other slot uses index+1. */
		t500rs_send_stop_now(t500rs, t500rs->expiry_buffer,
				     (u8)(i + 1));
	}
	t500rs_expiry_arm_locked(t500rs);
	spin_unlock_irqrestore(&t500rs->expiry_lock, flags);
}

/*
 * Play effect - send START command (0x41) for the effect and arm the
 * software expiry tracker so finite effects actually terminate.
 */
static int t500rs_play_effect(void *data,
			      const struct tmff2_effect_state *state)
{
	struct t500rs_device_entry *t500rs = data;
	const struct ff_effect *effect = &state->effect;
	unsigned long flags;
	int ret;

	/* Validate effect ID range */
	if (effect->id >= T500RS_MAX_EFFECTS) {
		hid_err(t500rs->hdev, "Effect ID %d exceeds maximum %d\n",
			effect->id, T500RS_MAX_EFFECTS);
		return -EINVAL;
	}

	/* Validate effect type is supported */
	switch (effect->type) {
	case FF_CONSTANT:
	case FF_PERIODIC:
	case FF_RAMP:
	case FF_SPRING:
	case FF_DAMPER:
	case FF_FRICTION:
	case FF_INERTIA:
		break;
	default:
		hid_err(t500rs->hdev, "Unsupported effect type for play: %d\n",
			effect->type);
		return -EINVAL;
	}

	ret = t500rs_send_start(t500rs, t500rs_effect_to_hw_id(effect));
	if (ret == 0) {
		/* Match the core's auto-expiry math (hid-tmff2.c): total playing
		 * time is (delay + length) * count. Infinite (length == 0) never
		 * auto-stops. */
		unsigned long total = (unsigned long)(effect->replay.delay +
						     effect->replay.length) *
					state->count;

		spin_lock_irqsave(&t500rs->expiry_lock, flags);
		if (total == 0) {
			/* Infinite effect: never auto-stop. */
			t500rs->active[effect->id].active = false;
		} else {
			t500rs->active[effect->id].active = true;
			t500rs->active[effect->id].start_ms =
				jiffies_to_msecs(jiffies);
			t500rs->active[effect->id].total_ms = total;
		}
		t500rs_expiry_arm_locked(t500rs);
		spin_unlock_irqrestore(&t500rs->expiry_lock, flags);

		T500RS_DBG(t500rs, "Started effect %d (total=%lu ms)\n",
			   effect->id, total);
	}
	return ret;
}

/*
 * Stop effect - deactivate the software expiry slot and send a per-slot
 * 0x41 STOP. Each STOP addresses only its own slot, so concurrent effects
 * remain unaffected (see work/analysis/10_second_pass_findings.md).
 */
static int t500rs_stop_effect(void *data,
			      const struct tmff2_effect_state *state)
{
	struct t500rs_device_entry *t500rs = data;
	const struct ff_effect *effect = &state->effect;
	unsigned long flags;

	/* Validate effect ID range */
	if (effect->id >= T500RS_MAX_EFFECTS) {
		hid_err(t500rs->hdev, "Effect ID %d exceeds maximum %d\n",
			effect->id, T500RS_MAX_EFFECTS);
		return -EINVAL;
	}

	if (!t500rs->send_buffer) {
		hid_err(t500rs->hdev, "t500rs_stop_effect: NULL send buffer\n");
		return -ENOMEM;
	}

	spin_lock_irqsave(&t500rs->expiry_lock, flags);
	t500rs->active[effect->id].active = false;
	t500rs_expiry_arm_locked(t500rs);
	spin_unlock_irqrestore(&t500rs->expiry_lock, flags);

	return t500rs_send_stop(t500rs, t500rs_effect_to_hw_id(effect));
}

/*
 * Update effect - send parameter updates without re-uploading
 *
 * Note: Only parameter-specific packets (0x03, 0x04, 0x05) are updated.
 * Duration and delay changes (from 0x01 packet) require full re-upload.
 * This limitation is acceptable as duration/delay modifications are rare
 * in gaming applications and the hardware may not support runtime updates
 * of these fields.
 */
static int t500rs_update_effect(void *data,
				const struct tmff2_effect_state *state)
{
	struct t500rs_device_entry *t500rs = data;
	const struct ff_effect *effect = &state->effect;
	const struct ff_effect *old = &state->old;
	u8 *buf;

	if (!t500rs)
		return -ENODEV;

	buf = t500rs->send_buffer;
	if (!buf)
		return -ENOMEM;

	switch (effect->type) {
	case FF_CONSTANT: {
		if (effect->u.constant.level == old->u.constant.level &&
		    effect->direction == old->direction)
			return 0;

		/* Constant force uses fixed subtypes (see docs/T500RS_FFBEFFECTS.md). */
		return t500rs_send_constant_packet(
			t500rs, buf, (u8)T500RS_CONSTANT_PARAM_SUB,
			effect->u.constant.level, effect->direction);
	}

	case FF_PERIODIC: {
		/* Skip update if parameters unchanged */
		if (effect->u.periodic.magnitude == old->u.periodic.magnitude &&
		    effect->u.periodic.offset == old->u.periodic.offset &&
		    effect->u.periodic.phase == old->u.periodic.phase &&
		    effect->u.periodic.period == old->u.periodic.period &&
		    effect->direction == old->direction)
			return 0;

		u16 param_sub, env_sub;
		t500rs_index_to_subtypes(effect->id + 1, &param_sub, &env_sub);
		return t500rs_send_periodic_packet(t500rs, buf, (u8)param_sub,
						   &effect->u.periodic,
						   effect->direction);
	}

	case FF_RAMP: {
		/* Skip update if parameters unchanged */
		if (effect->u.ramp.start_level == old->u.ramp.start_level &&
		    effect->u.ramp.end_level == old->u.ramp.end_level &&
		    effect->replay.length == old->replay.length)
			return 0;

		u16 param_sub, env_sub;
		t500rs_index_to_subtypes(effect->id + 1, &param_sub, &env_sub);
		return t500rs_send_ramp_packet(t500rs, buf, (u8)param_sub,
					       &effect->u.ramp,
					       effect->replay.length);
	}

	case FF_SPRING:
	case FF_DAMPER:
	case FF_FRICTION:
	case FF_INERTIA: {
		/*
		* Skip update if parameters unchanged - prevents micro-pulse/rumble
		* when games spam identical condition updates.
		*/
		const struct ff_condition_effect *cond =
			&effect->u.condition[0];
		const struct ff_condition_effect *cond_old =
			&old->u.condition[0];
		u16 param_sub, env_sub;

		if (cond->right_coeff == cond_old->right_coeff &&
		    cond->left_coeff == cond_old->left_coeff &&
		    cond->right_saturation == cond_old->right_saturation &&
		    cond->left_saturation == cond_old->left_saturation &&
		    cond->deadband == cond_old->deadband &&
		    cond->center == cond_old->center &&
		    effect->type == old->type)
			return 0;

		t500rs_index_to_subtypes(effect->id + 1, &param_sub, &env_sub);
		return t500rs_send_condition_packet(t500rs, buf,
						    (u8)param_sub, cond,
						    t500rs_condition_level(effect->type));
	}

	default:
		return -EOPNOTSUPP;
	}
}

/* Set autocenter */
static int t500rs_set_autocenter(void *data, u16 autocenter)
{
	struct t500rs_device_entry *t500rs = data;
	u8 *buf;
	int ret;
	u8 autocenter_percent;

	if (!t500rs)
		return -ENODEV;

	autocenter_percent = (u8)((autocenter * 100) / 65535);

	/* 
	* Wine compatibility: Some games (e.g., LFS under Wine) set autocenter to
	* 100%% at startup. That leaves a permanent strong
	* centering force which masks/overpowers other forces. To avoid this, message
	* the requests for the user to revert the gain value to expected value.
	*/
	if (autocenter_percent >= 100) {
		hid_warn(
			t500rs->hdev,
			"Game might have set autocenter to 100%%, you might want to set "
			"it back to expected value using oversteer (or keep oversteer "
			"open) or system gain.");
	}

	buf = t500rs->send_buffer;
	if (!buf)
		return -ENOMEM;

	/* Enable autocenter: Report 0x40 0x04 0x01 */
	struct t500rs_pkt_r40_config *config =
		(struct t500rs_pkt_r40_config *)buf;
	config->id = 0x40;
	config->subcmd = 0x04;
	config->data1 = 0x01; /* Enable */
	config->data2 = 0x00;
	ret = t500rs_send_hid(t500rs, buf, 4);
	if (ret)
		return ret;

	/* Set autocenter strength: Report 0x40 0x03 [value] */
	struct t500rs_pkt_r40_config *strength =
		(struct t500rs_pkt_r40_config *)buf;
	strength->id = 0x40;
	strength->subcmd = 0x03;
	strength->data1 = autocenter_percent; /* 0-100 percentage */
	strength->data2 = 0x00;
	ret = t500rs_send_hid(t500rs, buf, 4);
	if (ret)
		return ret;

	/* Apply settings: Report 0x42 0x05 */
	buf[0] = 0x42;
	buf[1] = 0x05;
	ret = t500rs_send_hid(t500rs, buf, 2);
	if (ret)
		return ret;

	return 0;
}

/* Set wheel rotation range */
static int t500rs_set_range(void *data, u16 range)
{
	struct t500rs_device_entry *t500rs = data;
	/* Use a dedicated heap buffer, NOT t500rs->send_buffer. The parent
	 * calls set_range directly from sysfs process context (range_store),
	 * which races the FFB worker that reuses send_buffer for
	 * upload/update/play/stop. Mirrors the documented T300RS fix.
	 *
	 * NB: the buffer must be DMA-safe because hid_hw_output_report()
	 * maps it for USB DMA; a stack buffer is rejected by the USB HCD
	 * ("transfer buffer is on stack"). kmalloc memory is DMA-safe.
	 */
	u8 *buf = kzalloc(4, GFP_KERNEL);
	int ret;
	u16 range_value;

	if (!buf) {
		hid_err(t500rs->hdev, "could not allocate range buffer\n");
		return -ENOMEM;
	}

	/* Validate range - minimum 40 degrees, maximum 1080 degrees */
	if (range < T500RS_RANGE_MIN)
		range = T500RS_RANGE_MIN;

	if (range > T500RS_RANGE_MAX)
		range = T500RS_RANGE_MAX;

	T500RS_DBG(t500rs, "Setting wheel range to %u degrees\n", range);

	/* Device expects LITTLE-ENDIAN and value = range * 60. */
	range_value = range * 60;

	/* Send Report 0x40 0x11 [value_lo] [value_hi] to set range */
	{
		struct t500rs_pkt_r40_config *config =
			(struct t500rs_pkt_r40_config *)buf;
		config->id = 0x40;
		config->subcmd = 0x11;
		config->data1 = range_value &
				0xFF; /* Low byte first (little-endian) */
		config->data2 = (range_value >> 8) &
				0xFF; /* High byte second */
	}

	ret = t500rs_send_hid(t500rs, buf, 4);
	if (ret) {
		hid_err(t500rs->hdev, "Failed to send range command: %d\n",
			ret);
		goto out;
	}

	/* Apply settings with Report 0x42 0x05 */
	buf[0] = 0x42;
	buf[1] = 0x05;
	ret = t500rs_send_hid(t500rs, buf, 2);
	if (ret) {
		hid_err(t500rs->hdev, "Failed to apply range settings: %d\n",
			ret);
		goto out;
	}

	T500RS_DBG(t500rs, "Range set to %u degrees (final value=0x%04x)\n",
		   range, range_value);

out:
	kfree(buf);
	return ret;
}

/* Initialize T500RS device.
 *
 * open_mode is intentionally unused here: this variant installs no
 * open/close callback (see t500rs_populate_api), so there is nothing to
 * gate on open/close. FFB is armed once during this init (0x42 0x04/
 * 0x05/0x00 handshake + 0x40 FFB-enable + 0x43 gain) and stays armed;
 * the parent falls back to the default HID input open/close. The parent's
 * open_mode module param therefore has no effect on this wheel, which is
 * the correct behavior given the single-armed init strategy.
 */
static int t500rs_wheel_init(struct tmff2_device_entry *tmff2, int open_mode)
{
	struct t500rs_device_entry *t500rs = NULL;
	u8 *init_buf; /* Will use send_buffer for transfers */
	int ret;

	/* Sanity check protocol packet sizes against documentation */
	BUILD_BUG_ON(sizeof(struct t500rs_pkt_r01_main) != 15);
	BUILD_BUG_ON(sizeof(struct t500rs_pkt_r04_periodic_ramp) != 8);
	BUILD_BUG_ON(sizeof(struct t500rs_pkt_r05_condition) != 11);

	/* Validate input parameters */
	if (!tmff2) {
		pr_err("t500rs_wheel_init: NULL tmff2 structure\n");
		return -EINVAL;
	}

	if (!tmff2->hdev || !tmff2->input_dev) {
		pr_err("t500rs_wheel_init: Invalid tmff2 structure"
		       " (missing hdev or input_dev)\n");
		return -EINVAL;
	}

	hid_dbg(tmff2->hdev, "T500RS: Initializing HID mode\n");

	/* Allocate device data */
	t500rs = kzalloc(sizeof(*t500rs), GFP_KERNEL);
	if (!t500rs) {
		hid_err(tmff2->hdev,
			"Failed to allocate t500rs device structure\n");
		ret = -ENOMEM;
		goto err_alloc;
	}

	/* Initialize device structure */
	t500rs->hdev = tmff2->hdev;
	t500rs->input_dev = tmff2->input_dev;

	/* Allocate send buffer */
	t500rs->buffer_length = T500RS_BUFFER_LENGTH;

	t500rs->send_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
	if (!t500rs->send_buffer) {
		hid_err(tmff2->hdev,
			"Failed to allocate send buffer (%zu bytes)\n",
			t500rs->buffer_length);
		ret = -ENOMEM;
		goto err_buffer_alloc;
	}

	/* Allocate dedicated DMA-safe buffer for the expiry worker so it never
	 * races the shared send_buffer used by the core FFB worker. */
	t500rs->expiry_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
	if (!t500rs->expiry_buffer) {
		hid_err(tmff2->hdev,
			"Failed to allocate expiry buffer (%zu bytes)\n",
			t500rs->buffer_length);
		ret = -ENOMEM;
		goto err_expiry_alloc;
	}

	spin_lock_init(&t500rs->expiry_lock);
	INIT_DELAYED_WORK(&t500rs->expiry_work, t500rs_expiry_work);
	memset(t500rs->active, 0, sizeof(t500rs->active));

	/* Store device data in tmff2 BEFORE any operations that might fail */
	tmff2->data = t500rs;

	/* Use send_buffer for all HID transfers */
	init_buf = t500rs->send_buffer;

	T500RS_DBG(t500rs, "Sending initialization sequence...\n");

	/* Report 0x42 - Init/status commands (2 bytes each)
	* Windows sends these at startup: 0x42 0x04, 0x42 0x05, 0x42 0x00
	* These appear to initialize the FFB subsystem state.
	*
	* The opening sync (0x42 0x04) is mandatory: if the device cannot
	* even acknowledge the first handshake, FFB will be dead and binding
	* would advertise a non-functional FF device. Fail probe loudly so the
	* failure is visible (wheel_destroy, called by the parent, frees the
	* buffers allocated above).
	*/
	memset(init_buf, 0, 2);
	init_buf[0] = 0x42;
	init_buf[1] = 0x04;
	ret = t500rs_send_hid(t500rs, init_buf, 2);
	if (ret) {
		hid_err(t500rs->hdev,
			"Mandatory init sync 0x42 0x04 failed: %d\n", ret);
		return ret;
	}

	memset(init_buf, 0, 2);
	init_buf[0] = 0x42;
	init_buf[1] = 0x05;
	ret = t500rs_send_hid(t500rs, init_buf, 2);
	if (ret)
		hid_warn(t500rs->hdev, "Init command 0x42 0x05 failed: %d\n",
			 ret);

	memset(init_buf, 0, 2);
	init_buf[0] = 0x42;
	init_buf[1] = 0x00;
	ret = t500rs_send_hid(t500rs, init_buf, 2);
	if (ret)
		hid_warn(t500rs->hdev, "Init command 0x42 0x00 failed: %d\n",
			 ret);

	/* Report 0x40 - Disable built-in autocenter (4 bytes). Advisory:
	 * if this fails the base keeps its default autocenter, which the
	 * set_autocenter callback can still override later.
	 *
	 * Note: an earlier version of this driver sent '0x40 0x11 0x42 0x7b'
	 * here, described as an "FFB-enable magic". Per community USB captures
	 * (work/analysis/02_init_sequence_diffs.md and 10_second_pass_findings.md
	 * §5), subcommand 0x11 is the RANGE command and Windows never sends it
	 * at init. The bytes 0x42 0x7b = 0x7b42 LE = 31554 -> /60 = 526 degrees,
	 * i.e. a non-standard range, not an FFB-enable marker. It has been
	 * removed; if a real FFB-enable packet is needed it must be sourced
	 * from a new capture, not this misidentified range write.
	 */
	{
		struct t500rs_pkt_r40_config *config =
			(struct t500rs_pkt_r40_config *)init_buf;
		config->id = 0x40;
		config->subcmd = 0x04;
		// Keep explicit zeros even though memset() clears them.
		config->data1 = 0x00;
		config->data2 = 0x00;
	}
	ret = t500rs_send_hid(t500rs, init_buf, 4);
	if (ret)
		hid_warn(t500rs->hdev,
			 "Autocenter-disable (0x40 0x04) failed: %d\n", ret);

	/* Report 0x43 - Set global gain (2 bytes). Advisory: start at maximum
	 * device gain; the FFB gain callback will adjust later, and a failure
	 * here just leaves whatever gain the base already has.
	 */
	memset(init_buf, 0, 2);
	init_buf[0] = 0x43;
	init_buf[1] = 0xFF;
	ret = t500rs_send_hid(t500rs, init_buf, 2);
	if (ret)
		hid_warn(t500rs->hdev,
			 "Initial gain set (0x43) failed: %d\n", ret);

	hid_info(t500rs->hdev, "T500RS initialized successfully (HID mode)\n");
	T500RS_DBG(t500rs, "Buffer: %zu bytes\n", t500rs->buffer_length);

	/* Advertise capabilities now that init succeeded */
	tmff2->params = t500rs_params;
	tmff2->max_effects = T500RS_MAX_EFFECTS;
	memcpy(tmff2->supported_effects, t500rs_effects,
	       sizeof(t500rs_effects));

	return 0;

err_expiry_alloc:
	kfree(t500rs->send_buffer);
err_buffer_alloc:
	/* t500rs structure is allocated but not yet stored in tmff2->data */
	kfree(t500rs);
err_alloc:
	return ret;
}

/* Cleanup T500RS device */
static int t500rs_wheel_destroy(void *data)
{
	struct t500rs_device_entry *t500rs = data;

	if (!t500rs) {
		pr_warn("t500rs_wheel_destroy: NULL data pointer\n");
		return 0;
	}

	T500RS_DBG(t500rs, "T500RS: Cleaning up\n");

	/* Cancel any pending expiry work before freeing its buffer. */
	cancel_delayed_work_sync(&t500rs->expiry_work);

	/* Free resources in reverse order of allocation */
	if (t500rs->expiry_buffer) {
		kfree(t500rs->expiry_buffer);
		t500rs->expiry_buffer = NULL;
	}

	if (t500rs->send_buffer) {
		kfree(t500rs->send_buffer);
		t500rs->send_buffer = NULL;
	}

	kfree(t500rs);

	return 0;
}

/* Populate API callbacks.
 *
 * No wheel_fixup is registered: the stock T500RS report descriptor already
 * correctly declares the wheel X axis (0..65535), pedals (Y/Rz/Slider,
 * 0..1023), 13 buttons, and an 8-way hat - it is well-formed and needs no
 * patching. The FFB output path also does not depend on the descriptor (it
 * sends raw packets via hid_hw_output_report()).
 *
 * NOTE: Oversteer expects pedals on the Simulation-page usages
 * (ABS_GAS/ABS_BRAKE/ABS_THROTTLE), but the hardware reports them as
 * Y/Rz/Slider. Remapping those in the descriptor breaks games that bind to
 * the stock Y/Rz layout, so the fix must live in userspace (SDL / Wine /
 * game-specific mapping), not here.
 *
 * No open/close callback is installed: FFB is armed once in wheel_init and
 * the parent falls back to the default HID open/close (see comment on
 * t500rs_wheel_init re: open_mode).
 */
int t500rs_populate_api(struct tmff2_device_entry *tmff2)
{
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
