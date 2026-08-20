# 14 — Direction sign on the wire (M0 verdict)

> **M0 CONFIRMED (2026-08-20, ffpanel on hardware):** the expected-force
> monitor shows the **opposite** side of what the wheel actually does —
> user report from the ffpanel TUI: bar/`R` while the wheel pulls left
> (and vice versa). Per the M0 decision tree
> (`.sisyphus/plans/ffpanel.md` §1) this outcome means: **the device
> wire sign is inverted relative to the UAPI projection convention.**

**Date:** 2026-08-20 · **Tool:** `tools/ffpanel` (parity monitor,
golden-vector backed) · **Driver:** projection math unchanged since
`768afca` (`t500rs_synth_dir_project()` /
`t500rs_scale_const_with_direction()`)

## What was tested

The monitor implements the UAPI expectation exactly: direction 16384
(90°) ⇒ positive projected force ⇒ displayed `R`; 49152 (270°) ⇒ `L`.
The driver streams the projected level on the wire as `03 0e` / `04 0e`
bytes. The user felt the wheel pull to the side opposite the monitor's
prediction — i.e. positive stream level pushes/pulls the other way.

## Conclusion

- The tool is **not** wrong: it mirrors the driver math 1:1 (427 golden
  vectors) and the UAPI direction convention used by the whole hid-tmff2
  family and upstream Logitech FF drivers.
- The device-side meaning of a positive level was never established from
  the C1/C2 pcaps (game intent at those instants is not recoverable);
  this test establishes it: **positive level = leftward on this wheel**.
- Interim tool behavior: press `i` in the TUI so the monitor display
  matches reality (finding persisted to `~/.config/ffpanel.json`).

## D1 (driver sign fix) — ready, gated on the in-game check

Negate the projection in `t500rs_scale_const_with_direction()` and
`t500rs_synth_dir_project()` (`src/tmt500rs/hid-tmt500rs.c`) so a
positive projected force always means rightward on the wire. Per the
plan, commit only after **both**:

1. ✅ M0 confirmation (this file).
2. ⬜ In-game sanity check: torque feels correct with **no** invert-FF
   option enabled in the game (rules out a game-side convention).

Coordinated-change checklist when D1 lands (one commit):

- negate the two projection functions in the driver;
- update `docs/T500RS_FFBEFFECTS.md` §7 direction row;
- re-copy the negated bodies into `tools/ffpanel/parity/harness.c`,
  regenerate `parity/vectors.txt`, and flip `dirProject()` in
  `tools/ffpanel/synth.go` until `go test ./...` passes — otherwise the
  tool becomes a liar, which is the one thing it must never be;
- update this file with the D1 commit hash.
