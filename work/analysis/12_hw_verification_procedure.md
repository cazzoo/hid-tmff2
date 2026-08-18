# 12 — Hardware verification procedures (P3-5, P3-6)

Step-by-step procedures to close the two remaining hardware-verification items with a
single capture session on a real wheel. No code changes required — the driver as of
`796eade` is hardware-stable; these tests only produce evidence.

| Item | Claim under test | Driver code path |
|------|------------------|------------------|
| **P3-5** | Our `0x04` periodic layout (`04 <code> mag off phase period_lo period_hi 00`) is valid firmware input | `t500rs_send_periodic_packet()` → struct `t500rs_pkt_r04_periodic_ramp` (`hid-tmt500rs.h:159`) |
| **P3-6** | Non-zero `0x02` envelope is accepted for FF_RAMP (and the doc claim that periodic/constant non-zero envelopes trigger EPROTO) | `t500rs_send_envelope_packet()` → struct `t500rs_pkt_r02_envelope` (`hid-tmt500rs.h:219`) |

Both stem from the same evidence gap: **zero reference packets exist in the community
captures** — C1/C2 contain no `0x04` with code ≠ `0x0e` and no non-zero `0x02`
(`10_second_pass_findings.md` §4, §9).

---

## 0. Prerequisites

```bash
# tools
sudo pacman -S wireshark-cli linuxconsole  # or: sudo apt install tshark joystick

# driver debug (T500RS_DBG is hid_dbg -> dynamic debug; module name has underscores)
echo 'module hid_tmff_new +p' | sudo tee /sys/kernel/debug/dynamic_debug/control

# find the wheel's USB bus/device
lsusb | grep 044f:b65e   # e.g. "Bus 001 Device 004: ID 044f:b65e Thrustmaster"
# note BUS=1, DEV=4 for below

# load usbmon and start the capture (as root or wireshark group)
sudo modprobe usbmon
sudo tshark -i usbmon1 -w /tmp/t500_verify.pcapng &
TSHARK_PID=$!
```

> Isolate the wheel early: `tshark -r /tmp/t500_verify.pcapng -Y 'usb.addr == "1.4"'`
> (adjust `Bus.Dev`). Keep other FFB-capable apps closed so the only traffic is ours.

Find the event device:

```bash
grep -l 044f /sys/class/input/event*/device/id/vendor | while read f; do
  d=$(dirname $(dirname $f)); echo "$d: $(cat $d/name)"; done
```

---

## 1. P3-5 — periodic layout verification

**Rationale:** the periodic path has never been observed on the wire. The driver's
builder is based on an unsourced internal example (`04 2a 06 00 3f 0a 00 00`,
`09_action_items.md` P3-5). Windows rFactor2 only ever sends `04 0e …` which is a
*constant-force update* in disguise (§9 Hypothesis B), not a periodic effect.

### 1.1 fftest recipe

Run `fftest /dev/input/eventXX` and upload **one periodic effect** with distinctive,
unambiguous values (chosen so every scaled byte is unique and recognizable):

```
Effect type:      3  (periodic)
Waveform:         1  (SINE)
Axis:             0  (X / direction 0)
Level/Magnitude:  12000   -> magnitude byte should be ~46 (0x2e)*
Offset:           0
Phase:            0       (0 deg -> byte 0x00 — recognizable baseline)
Envelope:         all 0   (attack 0/0, fade 0/0)
Duration:         5000 ms -> period? NO — see below
```

Then the periodic parameters:

```
Period:           200 ms  -> period bytes should read c8 00
Custom waveform:  n
```

Play it when prompted. **Record what you feel**: a sine oscillation you can count
(~5 cycles over the 1 s play — a 200 ms period is slow enough to feel each crest).

*\* exact byte: magnitude is scaled by `t500rs_scale_periodic_with_direction()`
(magnitude × 127 / 32767 ≈ 46 for 12000); offset via `/256`.*

### 1.2 Extract and decode our own packets

```bash
tshark -r /tmp/t500_verify.pcapng \
  -Y 'usb.addr == "1.4" && usb.endpoint_address == 0x01 && usb.data_len > 0' \
  -T fields -e frame.number -e frame.time_relative -e usbhid.data > /tmp/out.txt

# the 0x04 packet(s) belonging to this effect:
awk '$3 ~ /^04/ && $3 !~ /^040e/ {print}' /tmp/out.txt
```

### 1.3 Pass/fail matrix

| Observation | Meaning | P3-5 verdict |
|-------------|---------|--------------|
| `0x04` packet on wire, no `-71` in `dmesg`, wheel oscillates ~5 Hz, period bytes `c8 00` | Layout accepted & semantically right | ✅ **closed — layout confirmed** |
| Packet on wire, no error, but frequency wrong or no force | Firmware parses differently (compare byte positions: which change when you change period/magnitude/phase — re-run fftest varying ONE parameter at a time) | 🟡 layout wrong → record bytes, file new action item with evidence |
| `dmesg` shows `hid-tmff-new ... -71` on upload | Firmware rejects the packet outright | 🔴 periodic path broken on this firmware → action item P1 |

Record in `/tmp/out.txt` order: upload sequence (`42 05`, `42 04`, `02 …` envelope,
`04 …` periodic, `01 …` main, `41 …` START) — the `04` is the target; the rest is
context confirming the sequence ran.

---

## 2. P3-6 — ramp envelope verification

**Rationale:** driver sends real attack/fade values for FF_RAMP only; the "firmware
rejects non-zero envelopes for periodic/constant with EPROTO" claim in the source
comment is unproven folklore.

### 2.1 fftest recipe

```
Effect type:      6  (ramp)
Axis:             0
Start level:      -10000
End level:         10000
Duration:          3000 ms
Delay:                0 ms
Button:               0
Attack length:     1000 ms
Attack level:      8000   (≈50%*)
Fade length:       1000 ms
Fade level:        8000
```

*\* envelope levels are scaled 0-65535 → 0-255 (`t500rs_scale_envelope_level`):
8000 → byte `1f`.*

Play it. **Record what you feel**: force should ramp -→+ over 3 s with a visibly
softer first second (attack) and softer final second (fade).

### 2.2 Extract the envelope packet

```bash
awk '$3 ~ /^02/ {print}' /tmp/out.txt
```

Expected: `02 <env_sub> e8 03 1f e8 03 1f 00`
(`e8 03` = 1000 ms LE, `1f` = level 8000 scaled).

### 2.3 Pass/fail matrix

| Observation | P3-6 verdict |
|-------------|--------------|
| No `-71` in dmesg; envelope bytes non-zero as computed; force visibly shaped (soft start/end) | ✅ **closed — ramp envelopes work** |
| No error but force NOT shaped (straight ramp) | 🟡 firmware ignores envelope for ramps → simplify driver (drop `allow_nonzero`) |
| `-71` on the `02` packet | 🔴 ramp envelopes rejected → drop `allow_nonzero`, send zeros always |

**Optional bonus (same session):** one constant effect with attack/fade (fftest type
0, envelope non-zero) to test the periodic/constant EPROTO folklore claim the source
comment asserts. Driver currently *warns once and sends zeros* — watch `dmesg` for
`non-zero envelope ignored` (expected, harmless) and note whether the wheel still
plays the effect.

---

## 3. Wrap-up

1. Stop the capture: `kill $TSHARK_PID`
2. Archive: copy pcapng + `/tmp/out.txt` + a short findings note into
   `work/raw_data/` (see `work/README.md` conventions)
3. Update `09_action_items.md` P3-5 / P3-6 status lines with the verdict + evidence
   frame numbers
4. If any 🔴: open a new P1 action item with the captured bytes — **do not hot-fix
   the struct layout without a second confirming capture** (one wheel is one
   firmware; the F1-rim b662 may differ, cf. START-arg history)

> ⚠️ Lesson from the init-gain regression (P3-3 history): treat observed-on-one-wheel
> behaviour as *evidence*, not proof. Record wheel model + firmware (Windows control
> panel shows it; `lsusb -v` bcdDevice approximates it) alongside every verdict.
