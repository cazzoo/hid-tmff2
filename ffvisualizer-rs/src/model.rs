//! Force model – computes instantaneous net torque from dispatched FF effect
//! parameters and play-state.  This is the *model of intent*, not a motor
//! current measurement.  See PLAN.md §5.

use std::collections::HashMap;
use std::time::Instant;

use crate::types::{
    EffectInfo, EffectParams, EffectType, MAX_LEVEL,
    SampleSnapshot, TYPE_NAMES, Waveform,
};

fn norm(v: i16) -> f32 {
    v as f32 / MAX_LEVEL
}

fn wave(waveform: Waveform, ph: f32) -> f32 {
    let p = ph.rem_euclid(1.0);
    match waveform {
        Waveform::Sine     => (2.0 * std::f32::consts::PI * p).sin(),
        Waveform::Square   => if p < 0.5 { 1.0 } else { -1.0 },
        Waveform::Triangle => {
            if p < 0.5 { 4.0 * p - 1.0 } else { 3.0 - 4.0 * p }
        }
        Waveform::SawUp    => 2.0 * p - 1.0,
        Waveform::SawDown  => 1.0 - 2.0 * p,
        Waveform::Unknown  => 0.0,
    }
}

fn envelope_factor(
    env: &EffectParams,
    age_ms: f32,
    eff_len_ms: u32,
) -> f32 {
    let al      = env.attack_length  as f32;
    let alev    = env.attack_level   as f32 / MAX_LEVEL;
    let fl      = env.fade_length    as f32;
    let flev    = env.fade_level     as f32 / MAX_LEVEL;

    if al > 0.0 && age_ms < al {
        alev + (1.0 - alev) * (age_ms / al)
    } else {
        let mut mag = 1.0;
        if fl > 0.0 && eff_len_ms > 0 {
            let remain = eff_len_ms as f32 - age_ms;
            if 0.0 <= remain && remain < fl {
                mag = flev + (1.0 - flev) * (remain / fl);
            }
        }
        mag
    }
}

/// Project a direction (Linux FF heading 0..0xFFFF) onto the wheel's signed axis.
/// direction 0 → +1, direction 0x8000 → -1.
#[inline]
pub fn dir_proj(direction: u16) -> f32 {
    (direction as f32) * 2.0 * std::f32::consts::PI / 65536.0
}

// ---------------------------------------------------------------------------
// Playing-state entry
// ---------------------------------------------------------------------------

#[derive(Debug, Clone)]
pub struct Playing {
    pub effect_id: i32,
    pub repeat: u16,
    pub start: Instant,
}

// ---------------------------------------------------------------------------
// Force model
// ---------------------------------------------------------------------------

#[derive(Debug)]
pub struct ForceModel {
    effects:    HashMap<i32, EffectParams>,
    playing:    HashMap<i32, Playing>,
    gain:       f32,
    autocenter: f32,
    position:   f32,
    velocity:   f32,
    _last_pos:  f32,
    _last_pos_t:Instant,
}

impl Default for ForceModel {
    fn default() -> Self {
        Self {
            effects:    HashMap::new(),
            playing:    HashMap::new(),
            gain:       1.0,
            autocenter: 0.0,
            position:   0.0,
            velocity:   0.0,
            _last_pos:  0.0,
            _last_pos_t: Instant::now(),
        }
    }
}

impl ForceModel {
    pub fn new() -> Self { Self::default() }

    // ---- mutators (called by proxy) ----

    pub fn set_effect(&mut self, id: i32, params: EffectParams) {
        let mut p = params;
        p.id = id;
        self.effects.insert(id, p);
    }

    pub fn erase_effect(&mut self, id: i32) {
        self.effects.remove(&id);
        self.playing.remove(&id);
    }

    pub fn play(&mut self, id: i32, value: u16) {
        self.playing.insert(id, Playing {
            effect_id: id,
            repeat: if value == 0xFFFF { 0 } else { value },
            start: Instant::now(),
        });
    }

    pub fn stop(&mut self, id: i32) {
        self.playing.remove(&id);
    }

    pub fn set_gain(&mut self, value: u16) {
        self.gain = (value as f32 / 65535.0).clamp(0.0, 1.0);
    }

    pub fn set_autocenter(&mut self, value: u16) {
        self.autocenter = (value as f32 / 65535.0).clamp(0.0, 1.0);
    }

    pub fn update_position(&mut self, normalized: f32) {
        let now = Instant::now();
        let dt = now.duration_since(self._last_pos_t).as_secs_f32();
        if dt > 0.001 {
            self.velocity = ((normalized - self._last_pos) / dt).clamp(-2.0, 2.0);
        }
        self._last_pos = normalized.clamp(-1.0, 1.0);
        self._last_pos_t = now;
        self.position = self._last_pos;
    }

    // ---- evaluation ----

    fn contribution(&self, e: &EffectParams, age: f32, _p: &Playing) -> f32 {
        let dp = dir_proj(e.direction);
        let eff_len = e.length as f32;
        let delay   = e.delay as f32;

        if age < delay { return 0.0; }

        let active_age = age - delay;

        match e.effect_type {
            EffectType::Constant => {
                norm(e.level.unwrap_or(0)) * dp
            }

            EffectType::Periodic => {
                let period = e.period.unwrap_or(0) as f32;
                let mag    = norm(e.magnitude.unwrap_or(0));
                let off    = norm(e.offset.unwrap_or(0));
                let phase0 = (e.phase.unwrap_or(0) as f32) / 36000.0;
                let ph = if period > 0.0 {
                    (phase0 + active_age / period).rem_euclid(1.0)
                } else {
                    phase0
                };
                let envf = envelope_factor(e, active_age, e.length);
                (off + mag * wave(e.waveform.unwrap_or(Waveform::Sine), ph) * envf) * dp
            }

            EffectType::Ramp => {
                let s = norm(e.start_level.unwrap_or(0));
                let en = norm(e.end_level.unwrap_or(0));
                let frac = (active_age / eff_len).clamp(0.0, 1.0);
                let envf = envelope_factor(e, active_age, e.length);
                (s + (en - s) * frac) * envf * dp
            }

            EffectType::Spring => {
                let center   = norm(e.center.unwrap_or(0) as i16);
                let deadband = norm(e.deadband.unwrap_or(0) as i16);
                let err      = center - self.position;
                if err.abs() < deadband { return 0.0; }
                let coeff = if err > 0.0 { norm(e.right_coeff.unwrap_or(0)) }
                           else            { norm(e.left_coeff.unwrap_or(0)) };
                let sat  = if err > 0.0 { e.right_saturation.unwrap_or(32767) as f32 / MAX_LEVEL }
                           else          { e.left_saturation.unwrap_or(32767) as f32  / MAX_LEVEL };
                let raw  = coeff * (err - deadband.signum() * deadband);
                raw.clamp(-sat, sat)
            }

            EffectType::Damper => {
                let coeff = norm(e.right_coeff.unwrap_or(0));
                let sat   = e.right_saturation.unwrap_or(32767) as f32 / MAX_LEVEL;
                let raw   = -coeff * self.velocity;
                raw.clamp(-sat, sat)
            }

            EffectType::Friction => {
                let coeff = norm(e.right_coeff.unwrap_or(0));
                let sat   = e.right_saturation.unwrap_or(32767) as f32 / MAX_LEVEL;
                if self.velocity.abs() < 0.01 { return 0.0; }
                let raw = -coeff * self.velocity.signum();
                raw.clamp(-sat, sat)
            }

            EffectType::Inertia => {
                let coeff = norm(e.right_coeff.unwrap_or(0));
                let sat   = e.right_saturation.unwrap_or(32767) as f32 / MAX_LEVEL;
                let raw   = -0.5 * coeff * self.velocity;
                raw.clamp(-sat, sat)
            }

            EffectType::Rumble => {
                let strong = norm(e.strong_magnitude.unwrap_or(0) as i16);
                let weak   = norm(e.weak_magnitude.unwrap_or(0) as i16);
                (strong + weak * 0.5) * 0.5 * dp
            }
        }
    }

    pub fn sample(&mut self) -> SampleSnapshot {
        let now = Instant::now();
        let mut comps       = Vec::new();
        let mut playing_list = Vec::new();
        let mut net = 0.0f32;
        let mut last_dir = 0u16;

        for (eid, p) in self.playing.iter() {
            let Some(e) = self.effects.get(eid) else { continue; };
            let age_ms = now.duration_since(p.start).as_millis() as f32;

            let eff_len = e.length as f32;
            if eff_len > 0.0
                && age_ms > eff_len + e.delay as f32
                && p.repeat != 0 && p.repeat != 0xFFFF
            {
                continue;
            }

            let c = self.contribution(e, age_ms, p);
            net += c;
            last_dir = e.direction;

            let name = TYPE_NAMES
                .iter()
                .find(|(k, _)| *k == e.effect_type.code())
                .map(|(_, v)| *v)
                .unwrap_or_else(|| "Unknown");

            comps.push(crate::types::Component {
                id:           p.effect_id,
                effect_type:  e.effect_type,
                name,
                contrib:      c,
                repeat:       p.repeat,
            });

            let info = EffectInfo::from_effect(e, age_ms as u32);
            // Fill repeat from Playing state
            // (EffectInfo has repeat field; we'll leave the TODO here)
            playing_list.push(info);
        }

        net = net.clamp(-1.0, 1.0) * self.gain;

        SampleSnapshot {
            ts:             std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|d| d.as_secs_f64()).unwrap_or(0.0),
            torque:         net,
            strength:       net.abs(),
            direction_deg:  (last_dir as f32) * 360.0 / 65536.0,
            position:       self.position,
            velocity:       self.velocity,
            gain:           self.gain,
            autocenter:     self.autocenter,
            components:     comps,
            playing:        playing_list,
        }
    }

    pub fn playing_ids(&self) -> Vec<i32> {
        self.playing.keys().copied().collect()
    }

    pub fn effects(&self) -> &HashMap<i32, EffectParams> {
        &self.effects
    }
}
