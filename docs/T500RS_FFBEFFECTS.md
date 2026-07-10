# T500RS Force Feedback Protocol

This document explains, how the driver talks to the Thrustmaster
T500RS racing wheel to make it push, shake, and resist through its force-feedback
(FFB) motor.

The facts here come from USB captures of the official Windows driver.

---

## 1. What force feedback actually is

The T500RS is a steering wheel that can physically push back against your hands.
Instead of *telling* the wheel "push with 40% force", the computer describes a
behaviour - an **effect** - and then asks the wheel to start or stop it.

Different effects feel like different real-world sensations:

| Effect            | What it feels like                                            |
|-------------------|---------------------------------------------------------------|
| Constant force    | A steady push or pull in one direction                       |
| Periodic (sine)   | A smooth, repeating vibration (engine rumble, road texture)   |
| Square / triangle | Sharper, more mechanical vibrations                          |
| Sawtooth / ramp   | A force that rises then drops, or slides one way             |
| Spring            | The wheel is pulled back toward the centre                    |
| Damper            | The wheel gets "thick" and resists being moved                |
| Friction          | A constant drag as you turn                                   |
| Inertia           | Resistance to *changing* direction, as if the wheel were heavy|

In a game, these combine to let you feel the road, a collision, or the weight of
the car.

---

## 2. How the computer talks to the wheel

Communication happens over USB as a stream of tiny **packets**. Each packet is
just a list of bytes (numbers from 0 to 255, written in hexadecimal like `0x01`).
The very first byte of every packet says what *kind* of packet it is.

A few simple conventions used throughout:

- **Hexadecimal** (`0x` prefix) is just another way to write numbers. `0x0e` is 14.
- **Little-endian**: when a value is bigger than 255 and needs two bytes, the
  *small* byte is sent first. So a duration of 500 ms is written `f4 01`
  (because 500 = `0x01f4`, and we send `f4` then `01`).
- **Reserved bytes** are always `0x00`; the wheel ignores them, but they must be
  present to keep the packet the right length.

---

## 3. The life of an effect (the big picture)

Playing any effect follows the same three steps:

1. **Declare it** - send a *main upload* packet (`0x01`) describing what kind of
   effect it is and how long it should last.
2. **Configure it** - send one or more *parameter* packets (`0x02`/`0x03`/`0x04`/`0x05`)
   that set the strength, speed, or shape.
3. **Start it** - send a *command* packet (`0x41`) with START.

To make it stop, send the same `0x41` command with STOP.

### The one rule that trips everyone up

On the T500RS, the `effect_id` byte is **always `0x00`** - in every `0x01` upload
and every `0x41` command. It is *not* a slot number you get to choose. If you
send anything other than `0x00`, the wheel does not crash, but constant force
produces **no torque at all** and other effects become unreliable. This was
confirmed on real hardware.

### Why the wheel never stops on its own

Unlike some other wheels, the T500RS has no built-in timer: once an effect is
started, it plays **forever** until an explicit STOP arrives. So the Linux driver
keeps its own software timer and sends the STOP at the right moment. When you read
"the driver enforces the duration", that is what is happening - the wheel itself
won't do it.

---

## 4. How the wheel tells effects apart (slots and subtypes)

Because `effect_id` is always `0x00`, the wheel cannot use it to distinguish one
effect from another. Instead it uses a different pair of numbers called the
**subtypes** (a "parameter subtype" and an "envelope subtype").

Think of a subtype as a **channel number**. Each effect is assigned its own
channel so its parameter packets are routed to the right effect.

- **Constant force** always uses the fixed channels:
  - parameter subtype = `0x0e`
  - envelope subtype  = `0x1c`
- **Every other effect** gets a slot number `n` (the first non-constant effect is
  `n = 1`, the next `n = 2`, and so on). Its channels are computed by a simple
  formula:

  ```
  parameter subtype = 0x0e + 0x1c x n
  envelope subtype  = 0x1c + 0x1c x n
  ```

  For example, the first non-constant effect (`n = 1`) gets `0x2a` and `0x38`.
  The wheel only cares about these numbers matching between the `0x01` packet and
  the later parameter packets.

These two subtype values are written into bytes 9–12 of the `0x01` packet, and
the parameter packets echo back the same numbers so the wheel knows which effect
they belong to.

---

## 5. The packets

Each section below describes one packet type: its length, what every byte means,
and any gotchas. You do not need to memorise the hex - this is a reference to come
back to.

### 5.1 Main upload - `0x01` (15 bytes)

This declares an effect. Sent first.

| Offset | Size | Field          | Meaning                                                        |
|--------|------|----------------|----------------------------------------------------------------|
| 0      | 1    | packet type    | `0x01`                                                         |
| 1      | 1    | effect_id      | **Always `0x00`** (see paragraph 3)                                     |
| 2      | 1    | effect type    | What kind of effect (see table below)                          |
| 3      | 1    | control        | Always `0x40`                                                  |
| 4–5    | 2    | duration       | How long it should run, in milliseconds                        |
| 6–7    | 2    | delay          | Pause before it starts, in milliseconds                        |
| 8      | 1    | reserved       | `0x00`                                                         |
| 9–10   | 2    | parameter sub  | The channel for this effect (see paragraph 4)                           |
| 11–12  | 2    | envelope sub   | The second channel for this effect (see paragraph 4)                    |
| 13–14  | 2    | reserved       | `0x0000`                                                       |

**Effect type codes (byte 2):**

| Code | Effect                        |
|------|-------------------------------|
| 0x00 | Constant force                |
| 0x20 | Square wave                   |
| 0x21 | Triangle wave                 |
| 0x22 | Sine wave                     |
| 0x23 | Sawtooth up                   |
| 0x24 | Sawtooth down *(also used for ramps)* |
| 0x40 | Spring                        |
| 0x41 | Damper / friction / inertia   |

**Duration note:** for constant and periodic effects the wheel ignores the
duration and runs until stopped, so the driver sends `0xffff` ("infinite") and
relies on its own timer (paragraph 3). Ramp effects use the real duration.

### 5.2 Envelope - `0x02` (9 bytes)

An envelope shapes the *edges* of a force: how quickly it fades in (attack) and
out (fade).

| Offset | Size | Field          | Meaning                                  |
|--------|------|----------------|------------------------------------------|
| 0      | 1    | packet type    | `0x02`                                   |
| 1      | 1    | subtype        | The envelope channel from the `0x01` packet |
| 2–3    | 2    | attack length  | Fade-in time, milliseconds               |
| 4      | 1    | attack level   | Fade-in strength, 0–255                  |
| 5–6    | 2    | fade length    | Fade-out time, milliseconds              |
| 7      | 1    | fade level     | Fade-out strength, 0–255                 |
| 8      | 1    | reserved       | `0x00`                                   |

**Important limitation:** for constant and periodic effects the envelope values
**must be zero**. Sending anything else makes the wheel reject later packets (a
firmware bug). Only ramp effects use real envelope values.

### 5.3 Constant force - `0x03` (4 bytes)

Sets the actual push/pull of a constant effect.

| Offset | Size | Field       | Meaning                                            |
|--------|------|-------------|----------------------------------------------------|
| 0      | 1    | packet type | `0x03`                                             |
| 1      | 1    | code        | Low byte of the parameter subtype (`0x0e` for constant) |
| 2      | 1    | reserved    | `0x00`                                             |
| 3      | 1    | level       | Force, signed −127 to +127 (negative = other way)  |

### 5.4 Periodic / ramp - `0x04` (8 bytes)

Sets the shape of a vibrating (periodic) or sliding (ramp) effect.

| Offset | Size | Field      | Meaning                                                  |
|--------|------|------------|----------------------------------------------------------|
| 0      | 1    | packet type| `0x04`                                                   |
| 1      | 1    | code       | Low byte of the parameter subtype                        |
| 2      | 1    | magnitude  | Strength, 0–127                                          |
| 3      | 1    | offset     | A steady push added on top, signed −127 to +127          |
| 4      | 1    | phase      | Starting point in the cycle, 0–255 (= 0° to 360°)        |
| 5–6    | 2    | period     | Time for one full cycle, in milliseconds                 |
| 7      | 1    | reserved   | `0x00`                                                   |

**Period is a time, not a frequency.** 100 means "one cycle every 100 ms". Do not
convert to Hz.

### 5.5 Condition - `0x05` (11 bytes, sent twice)

Used for spring, damper, friction, and inertia. It is sent **twice**: once for the
x axis and once for the Y axis. The T500RS is a single-axis wheel, so the Y packet
is normally all zeros.

| Offset | Size | Field          | Meaning                                     |
|--------|------|----------------|---------------------------------------------|
| 0      | 1    | packet type    | `0x05`                                      |
| 1      | 1    | code           | Subtype (first packet uses parameter sub, second uses envelope sub) |
| 2      | 1    | reserved       | `0x00`                                      |
| 3      | 1    | right coeff    | Stiffness to the right, 0–10                 |
| 4      | 1    | left coeff     | Stiffness to the left, 0–10                  |
| 5–6    | 2    | center         | Where "centre" sits (offset)                 |
| 7–8    | 2    | deadband       | A zone around centre with no force           |
| 9      | 1    | right sat      | Max force to the right, 0–100                |
| 10     | 1    | left sat       | Max force to the left, 0–100                 |

In plain terms: *coefficients* control how strongly the effect responds, *center*
and *deadband* define where the neutral point is, and *saturation* caps the
maximum force so it never gets violent.

### 5.6 Command - `0x41` (4 bytes)

Starts or stops an effect.

| Offset | Size | Field       | Meaning                              |
|--------|------|-------------|--------------------------------------|
| 0      | 1    | packet type | `0x41`                               |
| 1      | 1    | effect_id   | **Always `0x00`**                    |
| 2      | 1    | command     | `0x41` = START, `0x00` = STOP        |
| 3      | 1    | argument    | `0x01`                              |

### 5.7 Control and sync commands (`0x40`, `0x42`)

Besides effects, the driver sends short control packets: `0x40` configures
behaviour such as the steering range and autocentering, and `0x42` packets are
brief handshake/sync messages the driver sends before periodic uploads (for
example `42 05` and `42 04`). You do not need them to understand the effect
protocol above.

---

## 6. A complete example: constant force

Putting the steps together, here is a real constant-force effect that plays for
about half a second at a low positive force:

```
01 00 00 40 f4 01 00 00 0e 00 1c 00 00 00    # 0x01: declare constant, 500 ms
02 1c 00 00 00 00 00 00 00                   # 0x02: envelope (zeros - required)
03 0e 00 03                                  # 0x03: level +3 (weak push)
41 00 41 01                                  # 0x41: START
... later ...
41 00 00 01                                  # 0x41: STOP
```

Reading it back:
- The `0x01` packet says "constant effect, 500 ms, channels `0x0e`/`0x1c`".
- The `0x02` envelope is all zeros (mandatory for constant force).
- The `0x03` packet sets a small positive level on channel `0x0e`.
- The `0x41` START begins playback; a later `0x41` STOP ends it.

Periodic and condition effects follow the exact same shape - only the effect type
and the parameter packets differ.

---

## 7. Converting values (a plain guide)

Programs on the computer work with large numbers (for example a force from
−32767 to +32767). The wheel expects small numbers (roughly −127 to +127), so the
driver scales everything down. You rarely need the exact math, but here it is for
reference:

| Quantity            | Computer range      | Wheel range     | Conversion (device = ...)        |
|---------------------|---------------------|-----------------|-------------------------------|
| Direction           | 0–65535 (0=forward) | 0–35999 (0.01°) | `dir x 36000 / 65536`          |
| Duration            | milliseconds        | milliseconds    | direct; `0xffff` = infinite    |
| Constant level      | −32767...+32767     | −127...+127     | `level x 127 / 32767`          |
| Magnitude           | 0–32767             | 0–127           | `mag x 127 / 32767`            |
| Phase               | 0–35999 (0.01°)     | 0–255           | `phase x 256 / 36000`          |
| Period              | milliseconds        | milliseconds    | direct                         |
| Envelope level      | 0–32767             | 0–255           | `env x 255 / 32767`            |
| Condition coeff.    | 0–32767             | 0–10            | `coeff x 10 / 32767`           |
| Condition center/deadband | −32767...+32767 / 0–65535 | device units | / 65 *(still being verified)* |
| Condition saturation| 0–65535             | 0–100           | `sat x 100 / 65535`            |

A few of the condition-effect scalings are marked *still being verified* in the
driver code - they work, but the exact divisors were not all confirmed against
hardware captures.

---

## 8. Things to watch out for

- **`effect_id` is always `0x00`.** This is the single most common mistake and the
  cause of "constant force does nothing".
- **Constant force uses fixed subtypes** (`0x0e` / `0x1c`). Giving it a per-effect
  channel breaks level updates.
- **Envelope must be zero** for constant and periodic effects, or the wheel errors
  out. Only ramps use real envelope values.
- **Duration:** send `0xffff` for constant/periodic (the driver stops them with its
  own timer); use the real value for ramps.
- **The wheel never auto-stops.** Ending an effect is the driver's job, via the
  software-expiry timer.
- **Direction** is folded into the constant *level* and into the periodic *phase*;
  it is not a separate field in the `0x01` packet.
- **Live updates:** only the parameter packets (`0x03`/`0x04`/`0x05`) can be
  changed while an effect plays. Changing duration or delay requires re-uploading
  the whole effect.
