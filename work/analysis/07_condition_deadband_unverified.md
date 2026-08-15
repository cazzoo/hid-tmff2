# 07 — Condition effect scaling: the still-unverified divisors

The driver has two TODO markers for unverified scaling in the conditional (`0x05`)
packet builder. The captures let us partially constrain these.

## Source code reference

`src/tmt500rs/hid-tmt500rs.c:442-486` (`t500rs_build_r05_condition`):

```c
p->right_coeff = clamp_t(int, ((right_coeff * level) / 100) * 10 / 32767, 0, 10);
p->left_coeff  = clamp_t(int, ((left_coeff  * level) / 100) * 10 / 32767, 0, 10);
p->center      = cpu_to_le16((s16)(center / 20));          // VERIFIED by capture C
p->deadband    = cpu_to_le16((u16)(deadband / 65));        // UNVERIFIED
```

Comments at lines 470-482:
- `center / 20` — confirmed (capture C: center=-372 = -7439/20; capture B: 250 = 5000/20)
- `deadband / 65` — UNVERIFIED (doc self-contradictory: "/10" in byte-layout vs "/65" in scaling)

## What the captures tell us

### We have NO 0x05 captures with non-zero deadband

Capture 1's 2 packets and capture 2's 4 packets all start with `05 38 00 ...` and
`05 2a 00 ...` (the `00` is `b2 = reserved`). I did not extract the full 11-byte
bodies, but the visible prefix is consistent with zero deadband.

The condition effects uploaded by both games had `deadband = 0`, so the device-side
deadband byte is also zero — which is consistent with both `/10` and `/65` and any
other divisor. **The captures do not disambiguate the divisor.**

### Coefficient scaling is also only weakly constrained

We see `b3` (right_coeff) and `b4` (left_coeff) bytes in the 0x05 packets but don't
know the input values the games used. Without a known input/output pair, we can't
verify `coeff * level / 100 * 10 / 32767` either.

### Center scaling

Capture C (per the doc): `center = -7439` input → device byte = `-372` = `-7439/20`.
Capture B: `center = 5000` → `250` = `5000/20`. Both confirm `/20`.

## What we'd need to verify `deadband`

To verify the deadband divisor, we need to upload a spring/damper with a known
non-zero `deadband` input and observe the corresponding device byte.

### Test plan

Use `fftest` (from linuxconsole tools) or a small custom program to upload a SPRING
effect with:

```c
struct ff_effect e = {
    .type = FF_SPRING,
    .id = -1,
    .u.condition[0] = {
        .right_coeff = 0x4000,        /* well within 0..32767 */
        .left_coeff  = 0x4000,
        .right_saturation = 0xffff,
        .left_saturation  = 0xffff,
        .deadband = TEST_DEADBAND,    /* sweep this: 100, 1000, 10000, 30000 */
        .center = 0,
    },
    .replay = { .length = 1000 },
};
```

For each TEST_DEADBAND value, capture the USB OUT traffic with `usbmon` + wireshark,
extract the `0x05 2a 00 ...` packet's bytes 7-8 (deadband LE u16), and tabulate.

### Expected outcomes

If `/65` is correct:
- input 100 → device ≈ 1 (100/65 = 1.5, rounds to 1 or 2)
- input 1000 → device ≈ 15
- input 10000 → device ≈ 153
- input 30000 → device ≈ 461
- input 65535 → device ≈ 1008 (max)

If `/10` is correct:
- input 100 → device ≈ 10
- input 1000 → device ≈ 100
- input 10000 → device ≈ 1000 (already maxing out a u16 nicely)
- input 30000 → device ≈ 3000
- input 65535 → device ≈ 6553 (which exceeds the apparent device max of ~1008)

**Sanity check:** if the device-side deadband is a u16, the max value the firmware
accepts is `0xffff = 65535`. If it's actually a smaller field (e.g. u10 = 1023), then
`/65` gives `65535/65 = 1008` which fits in u10 exactly — **strongly supporting `/65`**.

`/10` would overflow any reasonable smaller-than-u16 device field, so `/65` is the
likely correct divisor.

## What we'd need to verify `coeff`

Upload springs with `right_coeff` swept across `0x0000, 0x2000, 0x4000, 0x6000, 0x7fff`
and module params `spring_level=100`. Capture and tabulate `b3` of the `0x05 2a ...`
packet. Expected with our formula:
- 0x0000 → 0
- 0x2000 (8192) → 8192 * 100 / 100 * 10 / 32767 = 2.5 → 2 or 3
- 0x4000 (16384) → 5
- 0x6000 (24576) → 7.5 → 7 or 8
- 0x7fff (32767) → 10

If the device bytes match, the formula is verified.

## Open captures to make

If anyone with a T500RS + Linux can capture these tests:

1. **Deadband sweep** (any wheel mode, standard or F1):
   ```
   for db in 100 1000 10000 30000 65535; do
       fftest --spring --deadband=$db --duration=200
   done
   # Capture with: sudo cat /sys/kernel/debug/usb/usbmon/3u | tee sweep.log
   ```
   Send the resulting `0x05 ...` packets our way.

2. **Coefficient sweep**:
   ```
   for coeff in 0 8192 16384 24576 32767; do
       fftest --spring --right-coeff=$coeff --duration=200
   done
   ```

3. **Combined non-zero-envelope ramp**: to verify the ramp `0x04` phase/offset mapping
   (see `05_periodic_0x04_anomaly.md`).

These tests would close the last 3 TODOs in the driver.

## Recommendation

Until verified:
- Leave `deadband / 65` (most likely correct per u10 hypothesis)
- Leave `coeff * 10 / 32767` (consistent with doc captures)
- Leave `center / 20` (verified)

Add a `// CAPTURE-VERIFY:` comment block referencing this doc so future maintainers
know exactly what test to run.
