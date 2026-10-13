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
}

#[derive(Resource, Default)]
pub struct GameData {
    pub props: HashMap<String, Map<String, Value>>,
    pub templates: HashMap<String, Template>,
    pub fx: HashMap<String, Vec<Value>>,
}

/// One property set, with typed getters (missing keys give the default).
pub struct Props<'a>(pub Option<&'a Map<String, Value>>);

impl Props<'_> {
    pub fn f(&self, k: &str, default: f32) -> f32 {
        self.0.and_then(|m| m.get(k)).and_then(|v| v.as_f64()).map(|v| v as f32).unwrap_or(default)
    }
    pub fn s(&self, k: &str) -> Option<&str> {
        self.0.and_then(|m| m.get(k)).and_then(|v| v.as_str()).filter(|s| !s.is_empty() && *s != "none")
    }
    pub fn has(&self, k: &str) -> bool {
        self.0.is_some_and(|m| m.contains_key(k))
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
    pub fn props(&self, set: &str) -> Props<'_> {
        Props(self.props.get(&set.to_lowercase()))
    }
    pub fn template_props(&self, template: &str) -> Props<'_> {
        Props(self.templates.get(template).and_then(|t| self.props.get(&t.property)))
    }
}
