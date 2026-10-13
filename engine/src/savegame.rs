//! Save Game / Quick Load (pause menu) and Restart from last save (mission failed): a snapshot of the state that
//! matters, in saves/quicksave.json next to the executable. Loading starts the game again in a fresh process with
//! `--load FILE`; the snapshot is applied once the level is built: Roman (position, facing, health, weapons and
//! ammo, specials), the mission (one-shot triggers fired, bomb sites, barracks, pub, compass, objectives, voice
//! lines played), placed grunts that died (removed), placed objects that were destroyed (their husk, no explosion).
//! The intro (camera path and its white overlay) is not replayed.
//!
//! Not the original's save format (docs/remaster-engine.md). Limits, labelled: grunts a barracks had already
//! released and that were alive are not restored (the barracks' remaining counts are); companions start at Roman.

use crate::fx::{Died, Health};
use crate::level::InstanceId;
use crate::player::Roman;
use bevy::prelude::*;
use serde::{Deserialize, Serialize};
use std::path::PathBuf;

#[derive(Serialize, Deserialize, Default, Clone)]
pub struct SaveData {
    pub level: String,
    pub roman_feet: [f32; 3],
    pub yaw: f32,
    pub health: f32,
    pub rifle_clip: u32,
    pub mg: Option<(u32, u32)>,
    pub use_mg: bool,
    pub stickies: u32,
    pub topsy: u32,
    pub mission: crate::mission::MissionSave,
    pub dead_grunts: Vec<u32>,
    pub destroyed: Vec<u32>,
}

/// Requests from the menus.
#[derive(Resource, Default)]
pub struct SaveRequest {
    pub save: bool,
}

/// A save to apply after the level is built (`--load`).
#[derive(Resource)]
pub struct PendingLoad(pub SaveData, pub u32);

pub fn save_path() -> PathBuf {
    std::env::current_exe().ok().and_then(|p| p.parent().map(|d| d.join("saves").join("quicksave.json"))).unwrap_or_else(|| PathBuf::from("quicksave.json"))
}

pub fn has_save() -> bool {
    save_path().exists()
}

pub fn read(path: &std::path::Path) -> Option<SaveData> {
    serde_json::from_str(&std::fs::read_to_string(path).ok()?).ok()
}

pub struct SaveGamePlugin;

impl Plugin for SaveGamePlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<SaveRequest>().init_resource::<PlacedIds>().add_systems(Update, (record_placed, save, apply));
    }
}

#[allow(clippy::too_many_arguments)]
fn save(
    mut req: ResMut<SaveRequest>,
    mission: Option<ResMut<crate::mission::Mission>>,
    roman: Query<(&Roman, &Transform, &Health)>,
    grunts: Query<(&InstanceId, &crate::ai::Grunt)>,
    objects: Query<(&InstanceId, &crate::level::Placed)>,
    destructible: Query<&crate::fx::Destructible>,
    data: Res<crate::gamedata::GameData>,
    scene_ids: Res<PlacedIds>,
) {
    if !std::mem::take(&mut req.save) {
        return;
    }
    let (Some(mut m), Ok((r, t, h))) = (mission, roman.single()) else { return };
    let present: std::collections::HashSet<u32> = objects.iter().map(|(i, _)| i.0).collect();
    // destroyed: a placed object now carrying its husk template, or gone from the level
    let mut destroyed: Vec<u32> = objects
        .iter()
        .filter(|(i, p)| scene_ids.0.get(&i.0).is_some_and(|tpl| tpl != &p.template))
        .map(|(i, _)| i.0)
        .collect();
    destroyed.extend(scene_ids.0.keys().filter(|id| !present.contains(id) && !scene_ids.1.contains(id)).copied());
    let _ = (&destructible, &data);
    let s = SaveData {
        level: "lev_1_m1".into(),
        roman_feet: (crate::level::game_to_engine().inverse() * t.translation).to_array(),
        yaw: r.yaw,
        health: h.0,
        rifle_clip: r.rifle.clip,
        mg: r.mg.as_ref().map(|w| (w.clip, w.ammo)),
        use_mg: r.use_mg,
        stickies: r.stickies,
        topsy: r.topsy,
        mission: m.snapshot(),
        dead_grunts: grunts.iter().filter(|(_, g)| g.mind == crate::ai::Mind::Dead).map(|(i, _)| i.0).collect(),
        destroyed,
    };
    let path = save_path();
    if let Some(d) = path.parent() {
        let _ = std::fs::create_dir_all(d);
    }
    match serde_json::to_string_pretty(&s).map(|j| std::fs::write(&path, j)) {
        Ok(Ok(())) => {
            m.feed("Game saved");
            m.log.push(format!("saved to {}", path.display()));
        }
        _ => warn!("save: could not write {}", path.display()),
    }
}

fn record_placed(mut ids: ResMut<PlacedIds>, q: Query<(&InstanceId, &crate::level::Placed), Added<crate::level::Placed>>) {
    for (i, p) in &q {
        ids.0.entry(i.0).or_insert_with(|| p.template.clone());
    }
}

/// The placed objects at level start (instance id -> template) and the placed grunts, to tell what was destroyed.
#[derive(Resource, Default)]
pub struct PlacedIds(pub std::collections::HashMap<u32, String>, pub std::collections::HashSet<u32>);

#[allow(clippy::too_many_arguments)]
fn apply(
    mut commands: Commands,
    pending: Option<ResMut<PendingLoad>>,
    mut mission: Option<ResMut<crate::mission::Mission>>,
    mut roman: Query<(&mut Roman, &mut Transform, &mut Health), Without<crate::ai::Grunt>>,
    mut grunts: Query<(Entity, &InstanceId, &mut crate::ai::Grunt, &mut Health, Option<&crate::physics::Body>), Without<Roman>>,
    objects: Query<(Entity, &InstanceId), With<crate::level::Placed>>,
    mut died: MessageWriter<Died>,
    mut physics: NonSendMut<crate::physics::Physics>,
    data: Res<crate::gamedata::GameData>,
) {
    let Some(mut p) = pending else { return };
    // wait two frames after the level exists (placed objects and the rig are spawned by then)
    let (Some(m), Ok((mut r, mut t, mut h))) = (mission.as_deref_mut(), roman.single_mut()) else { return };
    p.1 += 1;
    if p.1 < 3 {
        return;
    }
    let s = p.0.clone();
    m.restore(&s.mission);
    let feet = crate::level::game_to_engine() * Vec3::from(s.roman_feet);
    physics.place_walker(&r.walker, feet);
    t.translation = feet;
    r.yaw = s.yaw;
    h.0 = s.health;
    r.rifle.clip = s.rifle_clip;
    r.mg = s.mg.map(|(clip, ammo)| {
        let mut w = crate::player::machinegun(&data);
        w.clip = clip;
        w.ammo = ammo;
        w
    });
    r.use_mg = s.use_mg && r.mg.is_some();
    r.stickies = s.stickies;
    r.topsy = s.topsy;
    for (e, i, mut g, mut gh, body) in &mut grunts {
        if s.dead_grunts.contains(&i.0) {
            if let Some(b) = body {
                physics.remove(b.id);
            }
            g.mind = crate::ai::Mind::Dead;
            g.dead_said = true;
            gh.0 = 0.0;
            commands.entity(e).despawn();
        }
    }
    for (e, i) in &objects {
        if s.destroyed.contains(&i.0) {
            commands.entity(e).remove::<crate::fx::Destructible>();
            died.write(Died(e));
        }
    }
    m.log.push("loaded the saved game".into());
    m.feed("Game loaded");
    commands.remove_resource::<PendingLoad>();
}
