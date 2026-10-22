//! The game's own data tables, exported by tools/remaster_export.py: property sets (behaviour, health, damage,
//! fx names, AI parameters), object templates (models, collision model, property set) and fx scripts.
//! The engine reads behaviour from here instead of hard-coding it, so objects act as the game defines them.

use bevy::prelude::*;
use serde::Deserialize;
use serde_json::{Map, Value};
use std::collections::HashMap;
use std::path::Path;

#[derive(Deserialize, Clone)]
pub struct Template {
    pub model: Option<String>,
    pub lods: Vec<String>,
    pub property: String,
    pub collision: Option<String>,
    /// the anim slot every object plays in a loop from load (template slot 1; tools/remaster_export.py)
    #[serde(default)]
    pub default_anim: Option<String>,
}

#[derive(Resource, Default)]
pub struct GameData {
    pub props: HashMap<String, Map<String, Value>>,
    pub templates: HashMap<String, Template>,
    pub fx: HashMap<String, Vec<Value>>,
}

/// One property set (an instance's own values over its template's set), with typed getters (missing keys give
/// the default). The original's readers: the placement first, then the class template (Object_ReadStringProp
/// 0x004533a0, Object_ReadValueProp 0x00453400 / 0x00453450 / 0x004534a0, Object_ReadBoolProp 0x004534f0 over
/// ParamSet_GetString 0x0045dec0, ParamSet_GetValue 0x0045df00, ParamSet_GetBool 0x0045df30, ParamSet_FindValue
/// 0x0045dea0 and Object_GetTemplateString / Value / Bool 0x004532e0 / 0x00453320 / 0x00453360).
#[derive(Clone, Default)]
pub struct Props(pub Map<String, Value>);

impl Props {
    pub fn f(&self, k: &str, default: f32) -> f32 {
        self.0.get(k).and_then(|v| v.as_f64()).map(|v| v as f32).unwrap_or(default)
    }
    pub fn i(&self, k: &str, default: i64) -> i64 {
        self.0.get(k).and_then(|v| v.as_i64().or_else(|| v.as_f64().map(|f| f as i64))).unwrap_or(default)
    }
    pub fn s(&self, k: &str) -> Option<&str> {
        self.0.get(k).and_then(|v| v.as_str()).filter(|s| !s.is_empty() && *s != "none")
    }
    pub fn has(&self, k: &str) -> bool {
        self.0.contains_key(k)
    }
    /// The set as a typed struct (serde, once at load): the struct's `#[serde(default)]`s are the original readers'
    /// code defaults; keys it does not name are ignored, a value of the wrong type falls back to the whole default.
    pub fn typed<T: serde::de::DeserializeOwned + Default>(&self) -> T {
        serde_json::from_value(Value::Object(self.0.clone())).unwrap_or_else(|e| {
            warn!("property set as {}: {e}", std::any::type_name::<T>());
            T::default()
        })
    }
}

/// A property flag stored as a number (0 / 1) or a bool: `#[serde(deserialize_with = "flag")]`.
pub fn flag<'de, D: serde::Deserializer<'de>>(d: D) -> Result<bool, D::Error> {
    let v = Value::deserialize(d)?;
    Ok(v.as_bool().unwrap_or_else(|| v.as_f64().is_some_and(|f| f != 0.0)))
}

/// One difficulty's factors: an entry of the table 0x00589188 (4 entries x 5 floats, read from the exe), which
/// 0x00401f60 copies to Game+0x554.. when the difficulty Game+0x550 is chosen.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct DifficultyFactors {
    /// Game+0x554: the health of the player's side (Ai_InitGeneric 0x004247f0 scales +0x458 / +0x454 by it when vt+0xe4:
    /// the player and the companions; the player's max_health too, player.md 6): easy 2, normal 1, hard 1, very hard 0.5.
    /// Applied to the companions and Roman (ai.rs difficulty_health)
    pub friendly_health: f32,
    /// Game+0x558: the health of the bad guys (the same, when vt+0xe8: grunts, captains; damage.md): easy 0.5,
    /// normal 1, hard and very hard 2 (ai.rs difficulty_health)
    pub badguy_health: f32,
    /// Game+0x55c: the health pickups' amount (pickup.rs)
    pub health_pickups: f32,
    /// Game+0x560: the exponent of the AI's pickup drop roll (Character_DropPickup 0x0043e710: 1 - (1 - p)^this,
    /// pickup.rs)
    pub unknown_560: f32,
    /// Game+0x564: the Stats screen's money lines (statscreen.rs)
    pub money: f32,
}

const fn factors(friendly_health: f32, badguy_health: f32, health_pickups: f32, unknown_560: f32, money: f32) -> DifficultyFactors {
    DifficultyFactors { friendly_health, badguy_health, health_pickups, unknown_560, money }
}

/// The table 0x00589188: easy, normal, hard, very hard.
pub const DIFFICULTIES: [DifficultyFactors; 4] =
    [factors(2.0, 0.5, 2.0, 2.0, 0.5), factors(1.0, 1.0, 1.0, 1.0, 1.0), factors(1.0, 2.0, 0.75, 0.75, 1.2), factors(0.5, 2.0, 0.6, 0.5, 1.5)];

/// The difficulty chosen (Game+0x550): saved with the game (the savegame header +0x108 and progress.dat).
#[derive(Resource, Clone, Copy, Debug, PartialEq, Eq)]
pub struct Difficulty(pub usize);

impl Default for Difficulty {
    /// normal (0x004a3930 resets the progress with 0x00401f60(1))
    fn default() -> Self {
        Difficulty(1)
    }
}

impl Difficulty {
    /// 0x00401f60: an index past the table (>= 4) is normal.
    pub fn select(index: usize) -> Self {
        Difficulty(if index >= DIFFICULTIES.len() { 1 } else { index })
    }
    pub fn factors(self) -> DifficultyFactors {
        DIFFICULTIES[self.0]
    }
}

impl GameData {
    pub fn load(dir: &Path) -> Self {
        let read = |f: &str| std::fs::read_to_string(dir.join(f)).unwrap_or_else(|e| panic!("{f}: {e} (re-run tools/remaster_export.py)"));
        GameData {
            props: serde_json::from_str(&read("properties.json")).expect("properties.json"),
            templates: serde_json::from_str(&read("templates.json")).expect("templates.json"),
            fx: serde_json::from_str(&read("fx.json")).expect("fx.json"),
        }
    }
    pub fn props(&self, set: &str) -> Props {
        Props(self.props.get(&set.to_lowercase()).cloned().unwrap_or_default())
    }
    pub fn template_props(&self, template: &str) -> Props {
        Props(self.templates.get(template).and_then(|t| self.props.get(&t.property)).cloned().unwrap_or_default())
    }
    /// The template's set with the instance's own values over it.
    pub fn instance_props(&self, template: &str, own: &Map<String, Value>) -> Props {
        let mut p = self.template_props(template);
        for (k, v) in own {
            p.0.insert(k.clone(), v.clone());
        }
        p
    }
}
