//! The Engine page (Options > Engine; gamemenu.rs draws it): the old remaster's engine-limit and input options
//! (its Remaster page's `[limits]`, `[persistence]` and `[input]` rows) over the graphics presets' settings
//! (physicsstep.rs, pools.rs, debris.rs, cursor.rs). Checkboxes are graphics settings by name
//! (`graphics::menu::flag` / `toggle`); the steppers here step a numeric setting through a list of values.
//! Every change is stored as a change to the preset (options.json `graphics`), so the preset row reads "(custom)".

use crate::graphics::{GraphicsSettings, resolve, set_change};
use crate::options::GameOptions;

/// A stepper: the setting's name, its label and the values offered (with their texts).
pub struct Stepper {
    pub key: &'static str,
    pub label: &'static str,
    pub values: &'static [(f64, &'static str)],
}

/// The page's steppers, top to bottom.
pub const STEPPERS: [Stepper; 7] = [
    // Havok's 4 iterations (the old remaster 4..16) are Jolt's 10 here: the Original's value is Jolt's default
    Stepper { key: "physics_solver_iterations", label: "Solver Iterations", values: &[(5.0, "5"), (10.0, "10 (original)"), (20.0, "20"), (40.0, "40")] },
    Stepper { key: "physics_body_capacity", label: "Physics Bodies", values: &[(10240.0, "10240 (original)"), (32768.0, "32768"), (65536.0, "65536")] },
    // the old remaster's list (Original (160), 512, 1024, 2048)
    Stepper { key: "decal_pool", label: "Decal Pool", values: &[(160.0, "160 (original)"), (512.0, "512"), (1024.0, "1024"), (2048.0, "2048")] },
    Stepper { key: "particle_pool_scale", label: "Particle Pool", values: &[(1.0, "x1 (original)"), (2.0, "x2"), (4.0, "x4"), (8.0, "x8"), (16.0, "x16")] },
    Stepper { key: "path_nodes", label: "Path Search", values: &[(2000.0, "2000"), (16000.0, "16000"), (150000.0, "150000"), (300000.0, "300000")] },
    Stepper { key: "debris_time", label: "Debris Stays", values: &[(0.0, "whole level"), (30.0, "30 s"), (60.0, "60 s"), (120.0, "120 s"), (300.0, "300 s")] },
    Stepper { key: "debris_max", label: "Debris Kept", values: &[(100.0, "100"), (500.0, "500"), (1000.0, "1000"), (4000.0, "4000")] },
];

/// The page's checkboxes: (label, setting).
pub const CHECKS: [(&str, &str); 5] = [
    ("Windows Pointer", "hardware_cursor"),
    ("Keep Mouse In Window", "clip_cursor"),
    ("Physics At Real Speed", "physics_fixed_step"),
    ("Smooth Physics", "physics_interpolation"),
    ("Keep Debris", "debris_keep"),
];

pub fn stepper(key: &str) -> Option<&'static Stepper> {
    STEPPERS.iter().find(|s| s.key == key)
}

fn current(o: &GameOptions, key: &str) -> Option<f64> {
    let s: GraphicsSettings = resolve(o.graphics_preset, &o.graphics);
    serde_json::to_value(&s).ok()?.get(key)?.as_f64()
}

/// How many values a stepper offers.
pub fn count(key: &str) -> usize {
    stepper(key).map_or(1, |s| s.values.len())
}

/// The value offered nearest the setting's (a value set in options.json need not be on the list).
pub fn index(o: &GameOptions, key: &str) -> usize {
    let (Some(s), Some(v)) = (stepper(key), current(o, key)) else { return 0 };
    s.values.iter().enumerate().min_by(|a, b| (a.1.0 - v).abs().total_cmp(&(b.1.0 - v).abs())).map_or(0, |(i, _)| i)
}

pub fn set(o: &mut GameOptions, key: &str, i: usize) {
    let Some(s) = stepper(key) else { return };
    let v = s.values[i.min(s.values.len() - 1)].0;
    // integers stay integers in the options file (the settings are u32 or f32)
    let value = if v.fract() == 0.0 && key != "debris_time" { serde_json::Value::from(v as u64) } else { serde_json::Value::from(v) };
    set_change(o.graphics_preset, &mut o.graphics, key, value);
}

/// The value's text: the list's own, or the number when it is off the list.
pub fn text(o: &GameOptions, key: &str) -> String {
    let (Some(s), Some(v)) = (stepper(key), current(o, key)) else { return String::new() };
    s.values.iter().find(|(x, _)| (x - v).abs() < 1e-6).map_or_else(|| format!("{v}"), |(_, t)| (*t).to_owned())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::graphics::Preset;

    /// Each stepper and checkbox names a setting of the presets; stepping stores a change, stepping back to the
    /// preset's value removes it.
    #[test]
    fn rows_name_settings() {
        let all = serde_json::to_value(GraphicsSettings::remaster()).unwrap();
        for s in &STEPPERS {
            assert!(all.get(s.key).is_some_and(|v| v.is_number()), "{}", s.key);
        }
        for (_, k) in CHECKS {
            assert!(all.get(k).is_some_and(|v| v.is_boolean()), "{k}");
        }
        let mut o = GameOptions::default();
        assert_eq!(text(&o, "decal_pool"), "1024");
        set(&mut o, "decal_pool", 0);
        assert_eq!((resolve(o.graphics_preset, &o.graphics).pools.decal_pool, text(&o, "decal_pool").as_str()), (160, "160 (original)"));
        set(&mut o, "decal_pool", 2);
        assert!(o.graphics.is_empty());
        set(&mut o, "debris_time", 2);
        assert_eq!(resolve(Preset::Remaster, &o.graphics).debris.debris_time, 60.0);
        // a value off the list: the nearest index, its own text
        o.graphics.insert("path_nodes".into(), 5000.into());
        assert_eq!((index(&o, "path_nodes"), text(&o, "path_nodes").as_str()), (0, "5000"));
        crate::graphics::menu::toggle(&mut o, "physics_interpolation");
        assert!(!resolve(Preset::Remaster, &o.graphics).physics.physics_interpolation);
    }
}
