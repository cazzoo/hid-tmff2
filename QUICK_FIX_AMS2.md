# Quick Fix: Automobilista 2 Force Feedback

## TL;DR - Do This First

### Step 1: Disable Steam Input
1. Right-click **Automobilista 2** in Steam
2. **Properties** → **Controller** tab
3. **Override for Automobilista 2**: **"Disable Steam Input"**

### Step 2: Set Launch Options
1. Still in Properties → **General** tab
2. **Launch Options** field, add:
   ```
   SDL_JOYSTICK_HIDAPI=0 %command%
   ```

### Step 3: In-Game Settings
1. Launch game
2. **Options** → **Controls & Calibration** → **Force Feedback**
3. Set:
   - **FFB Effects Level**: 100%
   - **Master Scale**: 100%
   - **FX Scale**: 100%
   - **Low Force Boost**: 10-20%
4. **Save** and **restart the game**

### Step 4: Test
- Drive a car
- Turn the wheel - you should feel resistance
- Hit a curb - you should feel bumps
- If working: Adjust FFB strength to your preference

## Still Not Working?

### Try Proton Experimental
1. Right-click game → **Properties** → **Compatibility**
2. Check **"Force the use of a specific Steam Play compatibility tool"**
3. Select **"Proton Experimental"**
4. Restart game

### Or Try These Launch Options
```
PROTON_NO_ESYNC=1 SDL_JOYSTICK_HIDAPI=0 %command%
```

## Why This Works

- **Steam Input** blocks direct FFB access → We disable it
- **SDL_JOYSTICK_HIDAPI=0** forces SDL2 to use Linux evdev (which works) instead of HIDAPI (which doesn't)
- **Windows drivers don't work** in Proton - it uses your Linux driver (which is already working!)

## Verify Driver Works

Before blaming the game, test the driver:

```bash
fftest /dev/input/event2
```

If this works (you can feel effects), the driver is fine - it's a game/Proton configuration issue.

## Full Documentation

See **PROTON_FFB_TROUBLESHOOTING.md** for complete guide with all solutions.

---

**Expected Result**: FFB should work exactly like it does in Live For Speed! 🏎️

