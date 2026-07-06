//! Shared types, effect constants, and the JSON-serializable sample snapshot.

use serde::Serialize;

// ---------------------------------------------------------------------------
// Linux input.h FF effect-type codes
// ---------------------------------------------------------------------------

pub const FF_CONSTANT: u16  = 0x00;
pub const FF_RAMP: u16      = 0x03;
pub const FF_SPRING: u16    = 0x40;
pub const FF_FRICTION: u16  = 0x41;
pub const FF_DAMPER: u16    = 0x42;
pub const FF_INERTIA: u16   = 0x43;
pub const FF_RUMBLE: u16    = 0x50;
pub const FF_PERIODIC: u16  = 0x05;

pub const FF_SQUARE: u16   = 0x58;
pub const FF_TRIANGLE: u16 = 0x59;
pub const FF_SINE: u16     = 0x5A;
pub const FF_SAW_UP: u16   = 0x5B;
pub const FF_SAW_DOWN: u16 = 0x5C;

/// Special EV_FF "command" codes.
pub const FF_GAIN: u16       = 0x60;
pub const FF_AUTOCENTER: u16 = 0x61;

pub const MAX_LEVEL: f32 = 32767.0;

pub const EFFECT_TYPES: [u16; 9] = [
    FF_CONSTANT, FF_RAMP, FF_PERIODIC,
    FF_SPRING, FF_DAMPER, FF_FRICTION, FF_INERTIA,
    FF_RUMBLE,
];

// ---------------------------------------------------------------------------
// Human-readable names (used in UI and websocket JSON)
// ---------------------------------------------------------------------------

pub const TYPE_NAMES: &[(u16, &str)] = &[
    (FF_CONSTANT,  "Constant"),
    (FF_PERIODIC,  "Periodic"),
    (FF_RAMP,      "Ramp"),
    (FF_SPRING,    "Spring"),
    (FF_DAMPER,    "Damper"),
    (FF_FRICTION,  "Friction"),
    (FF_INERTIA,   "Inertia"),
    (FF_RUMBLE,    "Rumble"),
];

pub const WAVE_NAMES: &[(u16, &str)] = &[
    (FF_SQUARE,   "Square"),
    (FF_TRIANGLE, "Triangle"),
    (FF_SINE,     "Sine"),
    (FF_SAW_UP,   "Saw Up"),
    (FF_SAW_DOWN, "Saw Down"),
];

pub fn type_name(t: u16) -> &'static str {
    TYPE_NAMES.iter().find(|(k, _)| *k == t).map(|(_, v)| *v).unwrap_or_else(|| {
        let mut h = itoa::Buffer::new();
        h.format(t)
    })
}

pub fn wave_name(w: u16) -> &'static str {
    WAVE_NAMES.iter().find(|(k, _)| *k == w).map(|(_, v)| *v).unwrap_or("—")
}

// ---------------------------------------------------------------------------
// Effect type enum (for the force model)
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum EffectType {
    Constant,
    Ramp,
    Periodic,
    Spring,
    Damper,
    Friction,
    Inertia,
    Rumble,
}

impl EffectType {
    pub fn from_code(code: u16) -> Option<Self> {
        Some(match code {
            FF_CONSTANT => EffectType::Constant,
            FF_RAMP     => EffectType::Ramp,
            FF_PERIODIC => EffectType::Periodic,
            FF_SPRING   => EffectType::Spring,
            FF_DAMPER   => EffectType::Damper,
            FF_FRICTION => EffectType::Friction,
            FF_INERTIA  => EffectType::Inertia,
            FF_RUMBLE   => EffectType::Rumble,
            _           => return None,
        })
    }

    pub fn code(self) -> u16 {
        match self {
            EffectType::Constant  => FF_CONSTANT,
            EffectType::Ramp      => FF_RAMP,
            EffectType::Periodic  => FF_PERIODIC,
            EffectType::Spring    => FF_SPRING,
            EffectType::Damper    => FF_DAMPER,
            EffectType::Friction  => FF_FRICTION,
            EffectType::Inertia   => FF_INERTIA,
            EffectType::Rumble    => FF_RUMBLE,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum Waveform {
    Square,
    Triangle,
    Sine,
    SawUp,
    SawDown,
    Unknown,
}

impl Waveform {
    pub fn from_code(code: u16) -> Self {
        match code {
            FF_SINE     => Waveform::Sine,
            FF_SQUARE   => Waveform::Square,
            FF_TRIANGLE => Waveform::Triangle,
            FF_SAW_UP   => Waveform::SawUp,
            FF_SAW_DOWN => Waveform::SawDown,
            _           => Waveform::Unknown,
        }
    }
}

/// Normalize a value from signed 16-bit range to [-1.0, 1.0].
#[inline]
pub fn norm16(v: i16) -> f32 {
    (v as f32) / MAX_LEVEL
}

/// Clamp a signed contribution to [-1.0, 1.0].
#[inline]
pub fn clamp1(v: f32) -> f32 {
    if v > 1.0 { 1.0 } else if v < -1.0 { -1.0 } else { v }
}

// ---------------------------------------------------------------------------
// Force model primitive data
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Serialize)]
pub struct EffectParams {
    pub id: i32,
    #[serde(serialize_with = "serialize_effect_type")]
    pub effect_type: EffectType,
    pub direction: u16,
    pub length: u32,
    pub delay: u32,

    // FF_CONSTANT
    pub level: Option<i16>,

    // FF_RAMP
    pub start_level: Option<i16>,
    pub end_level: Option<i16>,

    // FF_PERIODIC
    pub waveform: Option<Waveform>,
    pub period: Option<u16>,
    pub magnitude: Option<i16>,
    pub offset: Option<i16>,
    pub phase: Option<u16>,

    // FF_SPRING / FF_DAMPER / FF_FRICTION / FF_INERTIA (condition[])
    pub right_saturation: Option<u16>,
    pub left_saturation: Option<u16>,
    pub right_coeff: Option<i16>,
    pub left_coeff: Option<i16>,
    pub deadband: Option<u16>,
    pub center: Option<i16>,

    // FF_RUMBLE
    pub strong_magnitude: Option<u16>,
    pub weak_magnitude: Option<u16>,

    // Envelope
    pub attack_length: u16,
    pub attack_level: u16,
    pub fade_length: u16,
    pub fade_level: u16,
}

fn serialize_effect_type<S>(t: &EffectType, s: S) -> Result<S::Ok, S::Error>
where
    S: serde::Serializer,
{
    s.serialize_u16(t.code())
}

// ---------------------------------------------------------------------------
// Playing-state entry
// ---------------------------------------------------------------------------

#[derive(Debug, Clone)]
pub struct Playing {
    pub effect_id: i32,
    pub repeat: u16,
    pub start_mono: u32,  // monotonic time at play event (ms)
}

// ---------------------------------------------------------------------------
// 60 Hz JSON snapshot pushed to the browser
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Serialize)]
pub struct SampleSnapshot {
    pub ts: f64,
    pub torque: f32,
    pub strength: f32,
    pub direction_deg: f32,
    pub position: f32,
    pub velocity: f32,
    pub gain: f32,
    pub autocenter: f32,
    pub components: Vec<Component>,
    pub playing: Vec<EffectInfo>,
}

#[derive(Debug, Clone, Serialize)]
pub struct Component {
    pub id: i32,
    #[serde(serialize_with = "serialize_effect_type")]
    pub effect_type: EffectType,
    pub name: &'static str,
    pub contrib: f32,
    pub repeat: u16,
}

#[derive(Debug, Clone, Serialize)]
pub struct EffectInfo {
    pub id: i32,
    #[serde(serialize_with = "serialize_effect_type")]
    pub effect_type: EffectType,
    pub name: &'static str,
    pub direction: u16,
    pub age_ms: u32,
    pub length_ms: u32,
    pub repeat: u16,
    pub waveform: Option<&'static str>,
    pub level: Option<i16>,
    pub magnitude: Option<i16>,
    pub start_level: Option<i16>,
    pub end_level: Option<i16>,
    pub center: Option<i16>,
}

impl EffectInfo {
    pub fn from_effect(e: &EffectParams, age_ms: u32) -> Self {
        EffectInfo {
            id: e.id,
            effect_type: e.effect_type,
            name: type_name(e.effect_type.code()),
            direction: e.direction,
            age_ms,
            length_ms: e.length,
            repeat: 0,
            waveform: e.waveform.map(|w| match w {
                Waveform::Sine     => "Sine",
                Waveform::Square   => "Square",
                Waveform::Triangle => "Triangle",
                Waveform::SawUp    => "Saw Up",
                Waveform::SawDown  => "Saw Down",
                Waveform::Unknown  => "—",
            }),
            level: e.level,
            magnitude: e.magnitude,
            start_level: e.start_level,
            end_level: e.end_level,
            center: e.center,
        }
    }
}
