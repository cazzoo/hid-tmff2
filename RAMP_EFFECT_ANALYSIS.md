# T500RS Ramp Effect Analysis

**Date**: 2025-10-02  
**Source**: Windows USB capture (t500rs_windows_20251002_235917.pcapng)

---

## Key Finding

**The T500RS does NOT support native ramp effects!**

Instead, Windows **simulates** ramp effects by continuously sending Report 0x04 packets with incrementing/decrementing values.

---

## Windows Ramp Implementation

### Upload Sequence

1. **Report 0x02** - Envelope (all zeros for basic ramp)
   ```
   021c00000000000000
   ```

2. **Report 0x04** - Initial ramp parameters
   ```
   040e000000005d0c
   ```
   - Bytes 2-3: Start value (0x0000)
   - Bytes 4-5: Current value (0x0000)
   - Bytes 6-7: Duration (0x0c5d = 3165ms)

3. **Report 0x01** - Effect upload
   ```
   010024405d0c00ffff0e001c000000
   ```
   - Effect ID: 0x00
   - Type: 0x24 (ramp/sawtooth down)
   - Bytes 4-5: Duration (0x0c5d = 3165ms)
   - Bytes 6-9: Unknown (might be max values)

4. **Report 0x41** - Start effect
   ```
   41004101
   ```

### Dynamic Ramp Updates

After starting, Windows sends **Report 0x04 repeatedly** with incrementing values:

```
Time      Packet Data           Bytes 2-3  Bytes 4-5  Interpretation
--------  --------------------  ---------  ---------  --------------
31.327s   040e000101005d0c      0x0001     0x0001     Level = 1
31.335s   040e000102005d0c      0x0001     0x0002     Level = 2
31.359s   040e000405005d0c      0x0004     0x0005     Level = 5
31.367s   040e000607005d0c      0x0006     0x0007     Level = 7
31.379s   040e000707005d0c      0x0007     0x0007     Level = 7
31.387s   040e000909005d0c      0x0009     0x0009     Level = 9
...
31.687s   040e002122005d0c      0x0021     0x0022     Level = 34
```

**Statistics:**
- **29 packets** sent over **~10.6 seconds**
- **~365ms between packets** (average)
- Values increment from **0x00 to 0x22** (0 to 34)

---

## Implementation Requirements

To properly implement ramp effects on Linux, we need:

### 1. Background Thread for Ramp Updates

Create a dedicated thread that:
- Monitors active ramp effects
- Calculates current ramp level based on elapsed time
- Sends Report 0x04 periodically (~100-200ms intervals)
- Stops when effect ends or is stopped

### 2. Ramp State Tracking

For each ramp effect, track:
- Start level
- End level
- Duration (milliseconds)
- Start time
- Current level
- Active/inactive state

### 3. Report 0x04 Format

```c
unsigned char buf[9];
buf[0] = 0x04;
buf[1] = 0x0e;
buf[2] = current_level & 0xff;         // Current level low byte
buf[3] = (current_level >> 8) & 0xff;  // Current level high byte
buf[4] = current_level & 0xff;         // Repeat (or target?)
buf[5] = (current_level >> 8) & 0xff;  // Repeat (or target?)
buf[6] = duration & 0xff;              // Duration low byte
buf[7] = (duration >> 8) & 0xff;       // Duration high byte
buf[8] = 0x00;                         // Padding
```

### 4. Level Calculation

```c
// Calculate current level based on elapsed time
float progress = (float)(current_time - start_time) / duration;
if (progress > 1.0f) progress = 1.0f;

int current_level = start_level + (int)((end_level - start_level) * progress);
```

### 5. Update Frequency

- Send Report 0x04 every **100-200ms**
- Windows uses ~365ms but we can be more responsive
- Stop sending when effect ends or is stopped

---

## Pseudo-Code Implementation

```c
// Ramp effect state
struct ramp_state {
    int active;
    int start_level;
    int end_level;
    unsigned long duration_ms;
    struct timespec start_time;
};

// Ramp update thread
void *ramp_update_thread(void *arg) {
    while (running) {
        pthread_mutex_lock(&effects_lock);
        
        for (int i = 0; i < MAX_EFFECTS; i++) {
            if (effects[i].active && effects[i].effect.type == FF_RAMP) {
                // Calculate elapsed time
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                unsigned long elapsed_ms = /* calculate */;
                
                // Calculate current level
                float progress = (float)elapsed_ms / duration_ms;
                if (progress > 1.0f) progress = 1.0f;
                
                int current_level = start + (end - start) * progress;
                
                // Send Report 0x04
                send_ramp_update(i, current_level, duration_ms);
            }
        }
        
        pthread_mutex_unlock(&effects_lock);
        
        // Sleep for 100ms
        usleep(100000);
    }
    
    return NULL;
}
```

---

## Next Steps

1. **Add ramp state tracking** to effect structure
2. **Create ramp update thread** that runs continuously
3. **Implement Report 0x04 sending** with calculated levels
4. **Test with different durations** (2s, 5s, 8s, 10s)
5. **Test ramp up and ramp down**
6. **Test with envelope parameters** (attack/fade)

---

## Notes

- The T500RS firmware doesn't have native ramp support
- All ramp effects must be simulated by the driver
- This is similar to how periodic effects might work (continuous updates)
- The update frequency can be tuned for smoothness vs CPU usage
- Consider using a timer instead of sleep for more precise timing

---

## Conclusion

Ramp effects require **active driver participation** - they cannot be "fire and forget" like constant force or spring effects. The driver must continuously update the force level to create the ramping effect.

This is a significant architectural change but is necessary for proper ramp support.

