# T500RS USB Driver Refactor Checklist

Purpose: step-by-step checklist to reimplement `hid-tmt500rs-usb.c` to follow the documented Windows T500RS USB protocol.

Legend: `[ ]` = TODO, `[x]` = done.

---

## Phase 1 – Packet struct alignment

- [x] Define `struct t500rs_pkt_r01_main` (15 bytes) in `hid-tmt500rs-usb.c`:
  - [x] Fields: `u8 id; __le16 effect_id; __le16 direction; __le16 duration_ms; __le16 delay_ms; __le16 code1; __le16 code2;`.
  - [x] Confirm `sizeof(struct t500rs_pkt_r01_main) == 15` with `BUILD_BUG_ON`.
- [x] Reuse existing `struct t500rs_r02_envelope` as the protocol 0x02 envelope (9 bytes).
- [x] Reuse existing `struct t500rs_r03_const` as the protocol 0x03 constant packet (4 bytes).
- [x] Define `struct t500rs_pkt_r04_periodic_ramp` (8 bytes) with `id`, `code`, `magnitude`, `offset`, `phase`, `__le16 period_ms`, `u8 reserved`.
- [x] Define `struct t500rs_pkt_r05_condition` (11 bytes) with `id`, `code`, `__le16 right_coeff`, `__le16 left_coeff`, `__le16 deadband`, `u8 center`, `u8 right_sat`, `u8 left_sat`.
- [x] Keep/adjust `struct t500rs_r41_cmd` to match doc (4 bytes).

---

## Phase 2 – Subtype / index helpers

- [x] Add `#define T500RS_MAX_HW_EFFECTS 16` (or reuse existing max) in T500RS file.
- [x] Implement helper `t500rs_index_to_subtypes(unsigned int idx, u16 *param_sub, u16 *env_sub)`:
  - [x] `*param_sub = 0x000e + 0x001c * idx;`
  - [x] `*env_sub  = 0x001c + 0x001c * idx;`.
- [x] Decide mapping `idx = effect->id % T500RS_MAX_HW_EFFECTS` and document rationale (documented in helper comment, wrapping idx modulo 16).

---

## Phase 3 – Hardware effect ID management

- [x] In T500RS device state, add arrays `u16 hw_id[T500RS_MAX_EFFECTS]` and `bool hw_id_in_use[T500RS_MAX_HW_EFFECTS]`.
- [x] Implement `t500rs_alloc_hw_id(struct t500rs_device_entry *t500rs, unsigned int logical_id)`:
  - [x] If slot already assigned, return existing hw_id.
  - [x] Otherwise scan for free ID 0..15, assign, mark used.
- [x] Implement `t500rs_get_hw_id(struct t500rs_device_entry *t500rs, unsigned int logical_id)` to return assigned ID (allocating if needed).
- [x] Implement `t500rs_free_hw_id(struct t500rs_device_entry *t500rs, unsigned int logical_id)` called from STOP path to recycle slots.
- [ ] Replace all hard-coded use of EffectID=0 in T500RS code paths with mapped `hw_id` (deferred to Phase 10 START/STOP refactor).

---

## Phase 4 – 0x01 main upload builder

- [x] Implement `t500rs_build_r01_main(struct t500rs_pkt_r01_main *p, u16 hw_effect_id, u16 direction, u16 duration_ms, u16 delay_ms, u16 code1, u16 code2)`:
  - [x] Set `p->id = 0x01`.
  - [x] `p->effect_id = cpu_to_le16(hw_effect_id);`.
  - [x] `p->direction = cpu_to_le16(direction);` (0..35999 in 0.01°).
  - [x] `p->duration_ms = cpu_to_le16(duration_ms);`.
  - [x] `p->delay_ms = cpu_to_le16(delay_ms);`.
  - [x] `p->code1 = cpu_to_le16(code1);`.
  - [x] `p->code2 = cpu_to_le16(code2);` (0 if no envelope).
  - [x] `p->reserved = 0;`.
- [ ] Replace existing `t500rs_r01_main` uses with this builder in all upload functions (deferred to wiring phase).

**Note:** The waveform type (sine, triangle, saw, etc.) is NOT encoded in the 0x01 packet per Windows captures; it's determined at a higher level by SDL2/DirectInput.

---

## Phase 5 – Periodic 0x04 (period in ms)

- [x] Implement `t500rs_build_r04_periodic()` helper using `t500rs_pkt_r04_periodic_ramp` struct.
- [x] Implement scaling helpers:
  - [x] `t500rs_scale_periodic_magnitude()`: SDL 0..32767 → Device 0..127
  - [x] `t500rs_scale_periodic_phase()`: SDL 0..35999 → Device 0..255
  - [x] `t500rs_scale_periodic_offset()`: SDL -32768..32767 → Device -128..127
- [x] Document that period is in MILLISECONDS (no Hz×100 conversion).
- [ ] Remove all `Hz*100` period conversions from T500RS periodic upload/update (deferred to wiring phase).
- [ ] Wire the new helper into periodic upload/update paths (deferred to wiring phase).

---

## Phase 6 – Ramp via 0x04

- [x] Implement `t500rs_build_r04_ramp()` helper reusing `t500rs_pkt_r04_periodic_ramp` struct.
- [x] Map start/end levels to `magnitude`/`offset`:
  - [x] `magnitude = avg(abs(start), abs(end)) * 127 / 32767`
  - [x] `offset = (end - start) / 512` (direction of ramp)
  - [x] `period_ms = duration_ms` (ramp duration)
- [x] Document uncertainty: Windows captures show identical packets for different ramp params.
- [ ] Delete legacy `t500rs_r04_ramp` struct (deferred to cleanup phase).
- [ ] Wire helper into ramp upload path (deferred to wiring phase).

---

## Phase 7 – Conditional 0x05

- [ ] Implement `t500rs_build_r05_condition(const struct ff_condition_effect *c, u8 code, bool is_spring_like, struct t500rs_pkt_r05_condition *p)`:
  - [ ] `id = 0x05; p->code = code;`.
  - [ ] Map `c->right_saturation/left_saturation` + spring/damper/friction levels into `right_sat`/`left_sat` (0x54/0x64 as in captures).
  - [ ] Map coefficients/deadband/center from `ff_condition_effect` with linear scaling; document formulas and TODOs where data is incomplete.
- [ ] In `t500rs_upload_condition`:
  - [ ] Compute `param_sub/env_sub` from Phase 2.
  - [ ] Send first 0x05 with `code = (u8)(param_sub & 0xff)`.
  - [ ] Send second 0x05 with `code = (u8)(env_sub & 0xff)`.
- [ ] In `t500rs_update_effect` conditional branch, always update by sending both 0x05 packets when parameters change.

---

## Phase 8 – Constant force scaling and sequence

- [ ] Implement helper `t500rs_scale_constant_level(u16 sdl_level)` using protocol formula:
  - [ ] `s32 tmp = (sdl_level * 255) / 65535; return (s8)(tmp - 127);`.
- [ ] Use this helper in constant upload/update instead of older `-32767..32767` mapping.
- [ ] Upload sequence for constant:
  - [ ] 0x01 main (Phase 4) with `param_sub/env_sub`.
  - [ ] 0x02 envelope (Phase 9).
  - [ ] 0x03 constant packet (`level = scaled`), `code = (u8)(param_sub & 0xff)`.

---

## Phase 9 – Envelope 0x02

- [ ] Adjust envelope helper to use `struct t500rs_pkt_r02_envelope` and subtype from `env_sub` low byte.
- [ ] Map SDL/ff envelope fields to attack/fade length and level using doc scaling (0..32767 -> 0..255).
- [ ] For effects without envelope, set `code2` in 0x01 to 0 and skip 0x02.

---

## Phase 10 – START/STOP 0x41 per effect ID

- [ ] Replace `t500rs_send_pre_stop` global EffectID=0 behavior with per-effect STOP:
  - [ ] Before re-uploading logical effect N, send STOP for its mapped `hw_id[N]` only.
- [ ] In `t500rs_play_effect`, use mapped `hw_id` in 0x41 START packet.
- [ ] In `t500rs_stop_effect`, send 0x41 STOP with same `hw_id`.
- [ ] Keep special STOP for autocenter ID (e.g., 15) in init only if still required by protocol.

---

## Phase 11 – Square wave handling

- [ ] Remove `FF_SQUARE` from T500RS supported periodic waveforms, or explicitly map it to a supported waveform with a big comment.
- [ ] Ensure no 0x04 packets are generated uniquely for square; either reject or emulate.

---

## Phase 12 – Remove Hz*100 remnants

- [ ] Search `hid-tmt500rs-usb.c` for `Hz`, `freq`, `100000/` etc. and delete or convert to ms semantics.
- [ ] Confirm periodic upload/update only use ms period.

---

## Phase 13 – Reuse tmff2 base logic

- [ ] Verify that all scheduling, timing, and state handling still go through `tmff2_work_handler` and `tmff2->states`.
- [ ] Ensure T500RS code only handles packet building + USB interrupt send; no duplicate timing logic.
- [ ] Consider moving any generic scaling helpers into a shared header if other wheels can reuse them.

---

## Phase 14 – Cleanup and simplification

- [ ] Remove dead structs/functions (old ramp, legacy subtype hacks, unused fields).
- [ ] Factor repeated 0x01/0x02/0x03/0x04/0x05 send sequences into small helpers where it improves clarity.
- [ ] Update comments to reference `captures/T500RS_USB_Protocol_Analysis.md` and explain key protocol decisions.

---

## Phase 15 – Validation

- [ ] After Phases 1–4: run a single constant effect; log 0x01/0x02/0x03 and compare to Windows captures.
- [ ] After Phases 5–6: test sine/triangle/saw periodic with known parameters and verify period in ms.
- [ ] After Phase 7: test spring/damper; inspect 0x05 packets to ensure two-pack sequence with correct codes.
- [ ] After Phases 8–10: run overlapping periodic effects and confirm multi-effect mixing and per-ID STOP.
- [ ] After Phases 11–14: run a broad SDL2/fftest suite (constant, periodic, ramp, conditionals) and confirm behavior matches expectations and protocol doc.

