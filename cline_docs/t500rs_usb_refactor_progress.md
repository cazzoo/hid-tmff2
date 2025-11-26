# T500RS USB Refactor Progress

## 2025-11-25

- Initialized implementation based on `src/tmt500rs/T500RS_USB_Refactor_Checklist.md`.
- **Phase 1  Packet struct alignment**:
  - Added protocol-accurate structs `t500rs_pkt_r01_main`, `t500rs_pkt_r04_periodic_ramp`, and `t500rs_pkt_r05_condition` in `src/tmt500rs/hid-tmt500rs-usb.c`.
  - Confirmed `sizeof(struct t500rs_pkt_r01_main) == 15` using `BUILD_BUG_ON` inside `t500rs_wheel_init()`.
  - Decided to reuse existing `t500rs_r02_envelope` and `t500rs_r03_const` as protocol 0x02 and 0x03 packets.
- **Phase 2  Subtype / index helpers**:
  - Added `T500RS_MAX_HW_EFFECTS` alias and implemented `t500rs_index_to_subtypes()` using the documented `0x000e/0x001c` arithmetic, wrapping the index modulo 16.
- **Phase 3 – Hardware effect ID management**:
  - Extended `struct t500rs_device_entry` with `hw_id[]` (logical→hardware mapping) and `hw_id_in_use[]` (slot occupancy).
  - Implemented `t500rs_alloc_hw_id()`, `t500rs_get_hw_id()`, `t500rs_free_hw_id()` helpers.
  - Helpers are not yet wired into upload/play/stop paths (that comes in Phase 10).
- **Phase 4 – 0x01 main upload builder**:
  - Implemented `t500rs_build_r01_main()` helper using the protocol-accurate `t500rs_pkt_r01_main` struct.
  - Key insight from protocol doc: waveform type (sine/triangle/saw) is NOT encoded in 0x01 packet; it's determined at a higher level.
  - Helper not yet wired into upload paths (legacy `t500rs_r01_main` still in use).
- **Phase 5 – Periodic 0x04 (period in ms)**:
  - Implemented `t500rs_build_r04_periodic()` helper using `t500rs_pkt_r04_periodic_ramp` struct.
  - Added scaling helpers: `t500rs_scale_periodic_magnitude()`, `t500rs_scale_periodic_phase()`, `t500rs_scale_periodic_offset()`.
  - Key: period is in MILLISECONDS (no Hz×100 conversion per protocol doc and memory note).
  - Helper not yet wired into upload paths (legacy `t500rs_r04_periodic` still in use).
- **Phase 6 – Ramp via 0x04**:
  - Implemented `t500rs_build_r04_ramp()` helper reusing `t500rs_pkt_r04_periodic_ramp` struct.
  - Ramp uses same 0x04 packet as periodic; magnitude/offset encode start/end levels.
  - Note: exact encoding uncertain per protocol doc; Windows captures show identical packets.
  - Helper not yet wired into upload paths.
- **Phase 7 – Conditional 0x05**:
  - Implemented `t500rs_build_r05_condition()` helper using `t500rs_pkt_r05_condition` struct.
  - Conditional effects require TWO 0x05 packets (first for X-axis, second for Y-axis).
  - T500RS is single-axis, so second packet contains zeros.
  - Scaling formulas documented but need hardware verification.
  - Helper not yet wired into upload paths.
- **Phase 8 – Constant force 0x03**:
  - Implemented `t500rs_scale_constant_level()` using protocol formula: `(sdl_level * 255 / 65535) - 127`.
  - Implemented `t500rs_build_r03_constant()` helper.
  - Helper not yet wired into upload paths.
- No functional behavior changes yet; the new structs and helpers are scaffolding for later phases. Next steps: implement Phase 9 (envelope 0x02), then start wiring.
