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
