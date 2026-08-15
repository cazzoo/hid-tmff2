# 02 — Init sequence: real Windows vs. our `t500rs_wheel_init()`

Side-by-side comparison of the FFB-arm init sequence sent by the **Windows driver**
(captures 1 and 2) vs. the sequence sent by our driver in
`src/tmt500rs/hid-tmt500rs.c:t500rs_wheel_init()` (lines 1828-2005).

## Our driver's init sequence (8 reports)

```
Line 1911-1919  →  42 04                       (mandatory sync #1)
Line 1921-1928  →  42 05                       (sync #2)
Line 1929-1935  →  42 00                       (sync #3)
Line 1942-1955  →  40 11 42 7b                 (FFB-enable magic)
Line 1961-1973  →  40 04 00 00                 (autocenter-disable)
Line 1979-1985  →  43 ff                       (gain = 0xff)
```

The order is: 3× `0x42` handshake, then `0x40 0x11 0x42 0x7b` FFB-arm, then
autocenter disable, then gain set to 100%.

## Capture 1 init (T500RS standard rim, t=960.7s)

The actual game-start init is at frames 55-65. The Windows driver sends **only 3
reports** at game start (no re-init of the device itself — the device was already
in advanced mode):

| Frame | t (s) | Bytes | Driver equivalent |
|-------|-------|-------|-------------------|
| 55 | 960.714 | `42 04` | matches our line 1911-1919 |
| 57 | 980.681 | `42 05` | matches our line 1921-1928 |
| 59 | 980.685 | `42 05` | **(extra `42 05` we don't send)** |
| 65 | 980.717 | `01 00 00 40 ...` | FFB effect upload begins immediately |

**Capture 1 does NOT show a per-session `40 11 42 7b`, `40 04 00 00`, or `43 ff`
from the Windows driver.** Either:
- Windows cached these from device plug-in (pre-capture), or
- They are unnecessary on the standard rim.

## Capture 2 init (T500RS F1 rim, t=9.6 s to 12 s)

This is the full plug-in enumeration. **Order is different from our driver.** The
Windows driver sends, in order:

| Frame | t (s) | Bytes | Our driver equivalent | Notes |
|-------|-------|-------|-----------------------|-------|
| 109 | 8.922 | `42 01 00 00 00 00 00 00` | **❌ NOT SENT** | New sync pattern, 8 bytes |
| 183 | 9.828 | `42 01 00 ... 00` (16 B padded) | **❌ NOT SENT** | Same payload, 16-byte variant |
| 220 | 10.926 | `40 11 55 55` | partial match — we send `40 11 42 7b` (different data!) | Range = 0x5555 / 60 ≈ 364° (F1) |
| 257 | 11.669 | `42 04` | ✅ matches our line 1911-1919 | |
| 258 | 11.669 | `40 04 00 00` | ✅ matches our line 1961-1973 | Disable autocenter |
| 261 | 11.677 | `40 03 0d 00` | **❌ NOT SENT by us** | Set autocenter strength = 13 |
| 263 | 11.685 | `43 5a` | ✅ type matches, **value differs** (we send `43 ff`) | Gain = 90 (we send 100%) |
| 265 | 11.686 | `42 05` | ✅ matches our line 1921-1928 | |
| 267 | 11.700 | `42 00` | ✅ matches our line 1929-1935 | |
| 116–198 | 8.96–9.88 | `0a 04 90 03 ...` (×6) | **❌ NOT SENT by us** | Unknown — see `06_unknown_reports.md` |
| 293–297 | 12.4–12.5 | repeat `42 04` / `40 04` / `40 03 0d` / `42 05` / `42 00` | — | Init re-runs ~1s later |

### Δ (deltas vs. our driver)

1. **We do not send `42 01 00 ...`** — capture 2's *first* post-enum sync. This may be a
   no-op (the wheel accepts the `42 04` we send directly) or required for the F1 rim.
   **TODO: hw-verify by commenting out our `42 04` and substituting `42 01 00` then `42 04`.**

2. **Our `40 11 42 7b` is different from Windows' `40 11 55 55`.** Both are subcmd `0x11`
   (range) with different data:
   - Us: `0x7b42` = 31554. If we use the same `value = range * 60` formula as
     `t500rs_set_range()`, range = 31554/60 ≈ **526°** — that's NOT a default range,
     it's a magic number. The driver comment at line 1937 calls it "magic value seen
     in captures that enables FFB on the base" — meaning **the `0x40 0x11 0x42 0x7b`
     at init time is NOT a range command, it's an FFB-enable command that happens to
     reuse the `0x11` subcmd.**
   - Windows: `0x5555` = 21845. Same formula → 21845/60 ≈ **364°**, a real F1 range.
     Windows' `40 11 55 55` IS a range command sent at init.
   
   **Conclusion:** we are sending an FFB-enable magic where Windows sends a range
   command. They are functionally different. Our `0x40 0x11 0x42 0x7b` may be needed
   (driver says mandatory) but it's not what Windows sends. **TODO: hw-verify whether
   FFB works without `40 11 42 7b` on the F1 rim.**

3. **We do not send `40 03 0d 00`** — Windows sets autocenter strength to 13 immediately
   after disabling it. We just disable it. Our `set_autocenter` callback DOES send
   `40 03 XX 00` (line 1731), but only when called by userspace. Windows sends a
   default value at init.

4. **We send `43 ff` (gain 100%); Windows sends `43 5a` (gain 90%).** Our hardcoded
   `0xff` means the user sees 100% device gain on load. Windows ships a more
   conservative 90% default. **TODO: consider changing line 1981 to `0x5a` or making
   it a module param.**

5. **We do not send `0a 04 ...` (×6).** This appears to be F1-attachment
   identification or sub-feature configuration. See `06_unknown_reports.md`.

## Summary table

| Report | Our driver | Capture 1 | Capture 2 | Match? |
|--------|------------|-----------|-----------|--------|
| `42 01 00 ...` | ❌ | ❌ | ✅ | F1-specific? We skip |
| `42 04` | ✅ line 1911 | ✅ | ✅ | ✅ |
| `42 05` | ✅ line 1921 (×1) | ✅ (×2 — extra!) | ✅ | ✅ (we send 1, Win sends 2) |
| `42 00` | ✅ line 1929 | ✅ (later) | ✅ | ✅ |
| `40 11 42 7b` (FFB-arm) | ✅ line 1944 | ❌ | ❌ | **We send magic, Win doesn't** |
| `40 11 55 55` (range init) | ❌ | ❌ | ✅ | **Win sends range, we don't** |
| `40 04 00 00` (AC-disable) | ✅ line 1963 | ❌ | ✅ | ✅ |
| `40 03 0d 00` (AC-strength) | ❌ | ❌ | ✅ | **Win sends default, we don't** |
| `43 ff` (gain 100%) | ✅ line 1980 | ❌ | ❌ | **We send, Win sends `43 5a` instead** |
| `43 5a` (gain 90%) | ❌ | ❌ | ✅ | **Win sends 90%, we hardcode 100%** |
| `0a 04 ...` (F1 setup) | ❌ | ❌ | ✅ (×6) | **F1-specific, we skip entirely** |

## Open questions to verify on hardware

1. Does removing our `0x40 0x11 0x42 0x7b` from init break FFB? If yes, what does
   Windows do instead to arm FFB? Possibly the boot-mode-switch (`0x53`) already
   arms it.
2. Does the F1 rim require `42 01 00` and `0a 04 ...` to function correctly?
3. Is `43 5a` (90%) a safer default than `43 ff` (100%) to avoid users reporting
   "wheel is too strong / clips"?
