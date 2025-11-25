# T500RS USB Refactor Progress

## 2025-11-25

- Initialized implementation based on `src/tmt500rs/T500RS_USB_Refactor_Checklist.md`.
- **Phase 1  Packet struct alignment**:
  - Added protocol-accurate structs `t500rs_pkt_r01_main`, `t500rs_pkt_r04_periodic_ramp`, and `t500rs_pkt_r05_condition` in `src/tmt500rs/hid-tmt500rs-usb.c`.
  - Confirmed `sizeof(struct t500rs_pkt_r01_main) == 15` using `BUILD_BUG_ON` inside `t500rs_wheel_init()`.
  - Decided to reuse existing `t500rs_r02_envelope` and `t500rs_r03_const` as protocol 0x02 and 0x03 packets.
- **Phase 2  Subtype / index helpers**:
  - Added `T500RS_MAX_HW_EFFECTS` alias and implemented `t500rs_index_to_subtypes()` using the documented `0x000e/0x001c` arithmetic, wrapping the index modulo 16.
- No functional behavior changes yet; the new structs and helpers are not wired into upload/update paths. Next steps: implement Phase 3 hardware effect ID management and then start replacing legacy 0x01/0x04/0x05 builders with the new packet types.

