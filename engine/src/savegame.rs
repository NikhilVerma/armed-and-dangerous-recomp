//! Saved games: a snapshot of the state that matters, in saves/ next to the executable: the six slots of the save /
//! load screens (saves/game1.json .. game6.json: Save Game in the pause menu, Resume Game in the main menu;
//! saveslots.rs) and the quick save (saves/quicksave.json: the checkpoints). Quick Load and Restart from last save
//! load the newest of them. Loading restarts the level in the running game (levelreset.rs) or starts with
//! `--load FILE`; the snapshot is applied once the level is built. The intro (camera path and its white overlay) is
//! not replayed.
//!
//! What is saved, and why (the original's save, src/save/save_01.cpp, serialises the whole object graph through the
//! class descriptor tables: SaveGame_Write 0x0048c620 writes a 0x124-byte header (level, the level manager's index,
//! the difficulty Game+0x550, the format hash, the time, a 128 x 76 thumbnail) and then every object's described
//! members, Havok body states and lists (0x0048b780 / 0x0048bda0); SaveGame_Load 0x0048c9b0 reloads the level and
//! reads them back). The remaster keeps the state a player can see instead of the memory image:
//! - Roman: position, facing, health, weapons and ammo, specials;
//! - the mission (mission.rs MissionSave): one-shot triggers fired, volumes, counters, bomb sites, barracks, pub,
//!   compass, objectives, voice lines played;
//! - placed grunts that died (removed);
//! - placed objects no longer as placed: the template each one is now (a multi-stage husk is restored at its stage:
//!   v_house_2f_a_db, not its first husk) or gone; and the building windows shot out. They are put straight at that
//!   stage (level.rs RestoreStage): no death, so no fx, no score and no send_trigger_on_death is sent again;
//! - the difficulty (Game+0x550, gamedata.rs Difficulty: the header's +0x108, restored by 0x00401f60) and the money
//!   (Game+0x570, part of the Game object's saved members). The original banks the money at +0x57c (0x00401ef0,
//!   after the Stats screen) and every level start takes it back from there (0x00401f10 from Game_StartLevel
//!   0x00403690, whose "-slot" name is a load: SaveGame_Load 0x0048c9b0): a restart costs what was earned since.
//!   Here the money lives in options.json (GameOptions.money) and only the Stats screen changes it, so the bank is
//!   that value; a new game sets 5000 (gamemenu.rs).
//!
//! Not restored (open): the alarm's raised sirens (the original re-creates their fx_siren after a load, 0x0048c530),
//! the camera pitch.
//!
//! Where a save may be made: anywhere from the pause menu (the remaster's default), or, with the option
//! "checkpoint_saves" (options.rs), only where the original offers one: its QuickSave screen is pushed through
//! QuickSave_Request 0x00403560 once the mission start has been shown (MissionObject_ShowStart 0x0044daf0) and when
//! the pub screen is left (0x004690c0). The remaster saves there by itself to the quick save instead of asking (the
//! profile screens are not ported).
//!
//! Limits, labelled: grunts a barracks had already released and that were alive are not restored (the barracks'
//! remaining counts are); companions start at Roman.

use crate::levelreset::LevelAppExt;
use crate::fx::Health;
use crate::level::{InstanceId, RestoreStage, Stage};
use crate::player::Roman;
use bevy::prelude::*;
use serde::{Deserialize, Serialize};
use std::collections::{HashMap, HashSet};
use std::path::PathBuf;

/// The money a new game starts with (0x00401e80 with flag2: Game+0x570 = 5000, banked at +0x57c).
pub const NEW_GAME_MONEY: f32 = 5000.0;

#[derive(Serialize, Deserialize, Default, Clone, Debug, PartialEq)]
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
    /// placed objects no longer as placed: (instance id, the template it is now; None = gone)
    #[serde(default)]
    pub objects: Vec<(u32, Option<String>)>,
    /// building windows shot out: (the building's instance id, the window's index)
    #[serde(default)]
    pub windows: Vec<(u32, u8)>,
    /// Game+0x550
    #[serde(default = "normal")]
    pub difficulty: usize,
    /// Game+0x570
    #[serde(default)]
    pub money: Option<f32>,
    /// Game+0x574: the pub's intro heard in this campaign (pubscreen.rs)
    #[serde(default)]
    pub pub_intro_heard: Option<bool>,
    /// saves written before `objects`: destroyed instance ids, restored at their first husk
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub destroyed: Vec<u32>,
}

fn normal() -> usize {
    crate::gamedata::Difficulty::default().0
}

impl SaveData {
    /// The stage each listed object is restored at.
    pub fn stages(&self) -> HashMap<u32, Stage> {
        let mut out: HashMap<u32, Stage> = self.destroyed.iter().map(|&id| (id, Stage::FirstHusk)).collect();
        for (id, t) in &self.objects {
            out.insert(*id, t.clone().map_or(Stage::Gone, Stage::Template));
        }
        out
    }
}

/// What asks for a save.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub enum SaveSource {
    /// the pause menu's Save Game
    #[default]
    Menu,
    /// a checkpoint (the mission start shown, the pub left)
    Checkpoint,
    /// a playtest 'save' step (always allowed)
    Script,
}

/// Whether a save may be made now: anywhere, or with checkpoint_saves only at the checkpoints (scripts always).
pub fn may_save(checkpoint_saves: bool, source: SaveSource) -> bool {
    match source {
        SaveSource::Menu => !checkpoint_saves,
        SaveSource::Checkpoint => checkpoint_saves,
        SaveSource::Script => true,
    }
}

/// Requests from the menus.
#[derive(Resource, Default)]
pub struct SaveRequest {
    pub save: bool,
    /// write here instead of the quicksave (a save slot, a playtest 'save' step)
    pub path: Option<PathBuf>,
    pub source: SaveSource,
}

/// A save to apply after the level is built (`--load`).
#[derive(Resource)]
pub struct PendingLoad(pub SaveData, pub u32);

/// The saves' folder: saves/ next to the executable (scripted runs use their own build's: tools/playtest.py never
/// runs the player's engine/target).
pub fn saves_dir() -> PathBuf {
    std::env::current_exe().ok().and_then(|p| p.parent().map(|d| d.join("saves"))).unwrap_or_else(|| PathBuf::from("saves"))
}

/// Deletes every save of the folder (a playtest's `clear_saves` step: a script starts from no saves).
pub fn clear_saves() {
    let dir = saves_dir();
    for p in std::iter::once(dir.join("quicksave.json")).chain((1..=SLOTS).map(|n| slot_file(&dir, n))) {
        let _ = std::fs::remove_file(p);
    }
}

/// The quick save (checkpoints with checkpoint_saves; Quick Load).
pub fn save_path() -> PathBuf {
    saves_dir().join("quicksave.json")
}

/// The save / load screens' slots 1..=SLOTS (0x004a2c10: "Game 1".."Game 6", ui_savegame_slot%d), one file each.
pub const SLOTS: u8 = 6;

pub fn slot_path(slot: u8) -> PathBuf {
    slot_file(&saves_dir(), slot)
}

fn slot_file(dir: &std::path::Path, slot: u8) -> PathBuf {
    dir.join(format!("game{slot}.json"))
}

/// What a slot holds (0x004a3020: empty +0x4f8, damaged +0x4f9, else the header's level, difficulty and file time).
#[derive(Clone, Debug, PartialEq)]
pub enum Slot {
    Empty,
    /// a file that does not read as a save
    Damaged,
    Saved { level: String, difficulty: usize, written: std::time::SystemTime },
}

fn read_slot(path: &std::path::Path) -> Slot {
    let Ok(meta) = std::fs::metadata(path) else { return Slot::Empty };
    match read(path) {
        Some(s) => Slot::Saved { level: s.level, difficulty: s.difficulty, written: meta.modified().unwrap_or(std::time::SystemTime::UNIX_EPOCH) },
        None => Slot::Damaged,
    }
}

/// The six slots in order (index 0 is slot 1).
pub fn slots() -> Vec<Slot> {
    slots_in(&saves_dir())
}

fn slots_in(dir: &std::path::Path) -> Vec<Slot> {
    (1..=SLOTS).map(|n| read_slot(&slot_file(dir, n))).collect()
}

/// The newest readable save, the quick save or a slot: what Quick Load and Restart from last save load (the original
/// loads its quick save, 0x004a2e90 / 0x004a2ef0; the remaster's saves are made in slots too, so "last" means newest).
pub fn last_save() -> Option<PathBuf> {
    last_save_in(&saves_dir())
}

fn last_save_in(dir: &std::path::Path) -> Option<PathBuf> {
    std::iter::once(dir.join("quicksave.json"))
        .chain((1..=SLOTS).map(|n| slot_file(dir, n)))
        .filter_map(|p| {
            let t = std::fs::metadata(&p).and_then(|m| m.modified()).ok()?;
            read(&p).map(|_| (t, p))
        })
        .max_by_key(|(t, _)| *t)
        .map(|(_, p)| p)
}

pub fn has_save() -> bool {
    last_save().is_some()
}

/// Deletes a slot's save (0x004a2e40: the slot's files, then the slot refilled).
pub fn delete_slot(slot: u8) {
    let p = slot_path(slot);
    if let Err(e) = std::fs::remove_file(&p) {
        warn!("save: could not delete {}: {e}", p.display());
    }
}

pub fn read(path: &std::path::Path) -> Option<SaveData> {
    serde_json::from_str(&std::fs::read_to_string(path).ok()?).ok()
}

pub struct SaveGamePlugin;

impl Plugin for SaveGamePlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<SaveRequest>()
            .init_level_resource::<PlacedIds>()
            .init_level_resource::<Checkpoints>()
            .init_resource::<crate::gamedata::Difficulty>()
            // a dead grunt restored is despawned before the death triggers are read (else its send_trigger_on_death
            // would go out again on load)
            .add_systems(Update, (record_placed, save, apply.before(crate::mission::troop_deaths)))
            .add_systems(PostUpdate, (checkpoints, log_state));
    }
}

/// The state log's "game" (playtest expect steps): the difficulty, the money, the save rule.
fn log_state(state: Option<ResMut<crate::playtest::PlayState>>, difficulty: Res<crate::gamedata::Difficulty>, opts: Res<crate::options::GameOptions>, time: Res<Time<Real>>, mut next: Local<f32>) {
    let Some(mut state) = state else { return };
    state.0.insert("game".into(), serde_json::json!({ "difficulty": difficulty.0, "money": opts.money, "checkpoint_saves": opts.checkpoint_saves }));
    // "saves": each slot's state and which file is the newest save (1..6 a slot, 0 the quick save), twice a second
    if time.elapsed_secs() < *next {
        return;
    }
    *next = time.elapsed_secs() + 0.5;
    let slots: Vec<&str> = slots().iter().map(|s| match s {
        Slot::Empty => "empty",
        Slot::Damaged => "damaged",
        Slot::Saved { .. } => "saved",
    }).collect();
    let newest = last_save().map(|p| (1..=SLOTS).find(|n| slot_path(*n) == p).unwrap_or(0));
    state.0.insert("saves".into(), serde_json::json!({ "slots": slots, "newest": newest }));
}

/// The placed objects at level start (instance id -> template) and the building windows (building id, index), to
/// tell what was destroyed.
#[derive(Resource, Default)]
pub struct PlacedIds {
    pub objects: HashMap<u32, String>,
    pub windows: HashSet<(u32, u8)>,
}

fn record_placed(
    mut ids: ResMut<PlacedIds>,
    q: Query<(&InstanceId, &crate::level::Placed), Added<crate::level::Placed>>,
    windows: Query<&crate::level::Window, Added<crate::level::Window>>,
    buildings: Query<&InstanceId>,
) {
    for (i, p) in &q {
        ids.objects.entry(i.0).or_insert_with(|| p.template.clone());
    }
    for w in &windows {
        if let Ok(b) = buildings.get(w.building) {
            ids.windows.insert((b.0, w.index));
        }
    }
}

/// The objects whose stage differs from the level's (instance id -> its template now, None = gone), sorted.
fn object_stages(placed: &HashMap<u32, String>, now: &HashMap<u32, String>) -> Vec<(u32, Option<String>)> {
    let mut out: Vec<(u32, Option<String>)> = placed
        .iter()
        .filter_map(|(id, tpl)| match now.get(id) {
            Some(t) if t == tpl => None,
            Some(t) => Some((*id, Some(t.clone()))),
            None => Some((*id, None)),
        })
        .collect();
    out.sort();
    out
}

#[allow(clippy::too_many_arguments)]
fn save(
    mut req: ResMut<SaveRequest>,
    mission: Option<ResMut<crate::mission::Mission>>,
    roman: Query<(&Roman, &Transform, &Health)>,
    grunts: Query<(&InstanceId, &crate::ai::Grunt)>,
    objects: Query<(&InstanceId, &crate::level::Placed)>,
    windows: Query<&crate::level::Window>,
    buildings: Query<&InstanceId>,
    scene_ids: Res<PlacedIds>,
    (opts, difficulty, levels, pub_lines): (Res<crate::options::GameOptions>, Res<crate::gamedata::Difficulty>, Res<crate::campaign::Levels>, Res<crate::pubscreen::PubLines>),
) {
    if !std::mem::take(&mut req.save) {
        return;
    }
    let source = std::mem::take(&mut req.source);
    let (Some(mut m), Ok((r, t, h))) = (mission, roman.single()) else { return };
    if !may_save(opts.checkpoint_saves, source) {
        m.feed("Saves are made at checkpoints");
        return;
    }
    let now: HashMap<u32, String> = objects.iter().map(|(i, p)| (i.0, p.template.clone())).collect();
    let standing: HashSet<(u32, u8)> = windows.iter().filter_map(|w| buildings.get(w.building).ok().map(|b| (b.0, w.index))).collect();
    let mut broken: Vec<(u32, u8)> = scene_ids.windows.difference(&standing).copied().collect();
    broken.sort();
    let s = SaveData {
        // the level loaded (the header's +0, 0x0044a660): a load enters it first (levelreset.rs)
        level: levels.current_name(),
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
        objects: object_stages(&scene_ids.objects, &now),
        windows: broken,
        difficulty: difficulty.0,
        money: Some(opts.money),
        pub_intro_heard: Some(opts.pub_intro_heard || pub_lines.intro_said),
        destroyed: Vec::new(),
    };
    let path = req.path.take().unwrap_or_else(save_path);
    if let Some(d) = path.parent() {
        let _ = std::fs::create_dir_all(d);
    }
    match serde_json::to_string_pretty(&s).map(|j| std::fs::write(&path, j)) {
        Ok(Ok(())) => {
            m.feed("Game saved");
            m.log.push(format!("saved to {} ({source:?})", path.display()));
        }
        _ => warn!("save: could not write {}", path.display()),
    }
}

/// The checkpoints seen (a load counts the mission start as seen: restoring the mission marks it started).
#[derive(Resource, Default)]
pub struct Checkpoints {
    pub start_seen: bool,
}

/// The original's save points (see the module doc): the mission start shown, the pub screen left.
fn checkpoints(mission: Option<Res<crate::mission::Mission>>, mut seen: ResMut<Checkpoints>, mut req: ResMut<SaveRequest>, opts: Res<crate::options::GameOptions>, pending: Option<Res<PendingLoad>>) {
    let Some(m) = mission else { return };
    if pending.is_some() {
        return;
    }
    let start = m.started() && !seen.start_seen;
    seen.start_seen |= m.started();
    if (start || m.pub_exit_pending) && opts.checkpoint_saves && !req.save {
        req.save = true;
        req.source = SaveSource::Checkpoint;
    }
}

#[allow(clippy::too_many_arguments)]
fn apply(
    mut commands: Commands,
    pending: Option<ResMut<PendingLoad>>,
    mut mission: Option<ResMut<crate::mission::Mission>>,
    mut roman: Query<(&mut Roman, &mut Transform, &mut Health), Without<crate::ai::Grunt>>,
    mut grunts: Query<(Entity, &InstanceId, &mut crate::ai::Grunt, &mut Health, Option<&crate::physics::Body>), Without<Roman>>,
    objects: Query<(Entity, &InstanceId), With<crate::level::Placed>>,
    windows: Query<(Entity, &crate::level::Window)>,
    mut restore: MessageWriter<RestoreStage>,
    mut physics: NonSendMut<crate::physics::Physics>,
    data: Res<crate::gamedata::GameData>,
    (mut opts, mut difficulty, mut seen): (ResMut<crate::options::GameOptions>, ResMut<crate::gamedata::Difficulty>, ResMut<Checkpoints>),
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
    seen.start_seen = true;
    *difficulty = crate::gamedata::Difficulty::select(s.difficulty);
    if let Some(money) = s.money {
        opts.money = money;
    }
    if let Some(heard) = s.pub_intro_heard {
        opts.pub_intro_heard = heard;
    }
    let feet = crate::level::game_to_engine() * Vec3::from(s.roman_feet);
    physics.place_walker(&r.walker, feet);
    t.translation = feet;
    r.yaw = s.yaw;
    r.twist_reset = true;
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
    let stages = s.stages();
    let mut building_ids: HashMap<Entity, u32> = HashMap::new();
    for (e, i) in &objects {
        building_ids.insert(e, i.0);
        if let Some(stage) = stages.get(&i.0) {
            restore.write(RestoreStage { entity: e, stage: stage.clone() });
        }
    }
    // windows shot out (a destroyed building's windows go with its RestoreStage)
    for (we, w) in &windows {
        if building_ids.get(&w.building).is_some_and(|b| s.windows.contains(&(*b, w.index))) {
            restore.write(RestoreStage { entity: we, stage: Stage::Gone });
        }
    }
    m.log.push("loaded the saved game".into());
    m.feed("Game loaded");
    commands.remove_resource::<PendingLoad>();
}

#[cfg(test)]
mod tests {
    use super::*;

    fn map(v: &[(u32, &str)]) -> HashMap<u32, String> {
        v.iter().map(|(i, t)| (*i, t.to_string())).collect()
    }

    /// A house at its third stage, a removed arrow, an untouched oil pump: only the first two are listed, with the
    /// stage the house is at now.
    #[test]
    fn stages_recorded() {
        let placed = map(&[(174, "v_house_2f_a"), (258, "bigredarrow"), (224, "oilpump")]);
        let now = map(&[(174, "v_house_2f_a_db"), (224, "oilpump")]);
        assert_eq!(object_stages(&placed, &now), vec![(174, Some("v_house_2f_a_db".into())), (258, None)]);
    }

    /// The save file round trip: every field back as written; an old save (destroyed ids only) restores first husks.
    #[test]
    fn round_trip() {
        let s = SaveData {
            level: "lev_1_m1".into(),
            roman_feet: [1.0, 2.0, 3.0],
            yaw: 0.5,
            health: 220.0,
            rifle_clip: 4,
            mg: Some((60, 140)),
            use_mg: true,
            stickies: 7,
            topsy: 1,
            dead_grunts: vec![12, 13],
            objects: vec![(131, Some("boom_island_d".into())), (174, Some("v_house_2f_a_db".into())), (258, None)],
            windows: vec![(170, 2)],
            difficulty: 2,
            money: Some(5120.0),
            ..Default::default()
        };
        let back: SaveData = serde_json::from_str(&serde_json::to_string(&s).unwrap()).unwrap();
        assert_eq!(back, s);
        let st = back.stages();
        assert_eq!(st.get(&174), Some(&Stage::Template("v_house_2f_a_db".into())));
        assert_eq!(st.get(&258), Some(&Stage::Gone));
        let old: SaveData = serde_json::from_str(r#"{"level":"lev_1_m1","roman_feet":[0,0,0],"yaw":0,"health":300,"rifle_clip":5,"mg":null,"use_mg":false,"stickies":0,"topsy":0,"mission":{"triggers_done":[],"sites":[],"barracks":[],"pub_done":false,"pub_pool":null,"compass_tag":0,"tickers":0,"objectives":"","objectives_raw":"","ears_done":[]},"dead_grunts":[],"destroyed":[258]}"#).unwrap();
        assert_eq!(old.stages().get(&258), Some(&Stage::FirstHusk));
        assert_eq!((old.difficulty, old.money), (1, None));
    }

    /// The slot files: empty, damaged (not a save), saved; the newest readable save wins, a damaged file never.
    #[test]
    fn slot_files() {
        let dir = std::env::temp_dir().join(format!("and_slots_test_{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let save = |p: PathBuf, health: f32| std::fs::write(p, serde_json::to_string(&SaveData { level: "lev_1_m1".into(), health, difficulty: 2, ..Default::default() }).unwrap()).unwrap();
        assert_eq!(slots_in(&dir), vec![Slot::Empty; SLOTS as usize]);
        assert_eq!(last_save_in(&dir), None);
        save(slot_file(&dir, 2), 100.0);
        std::thread::sleep(std::time::Duration::from_millis(20));
        save(slot_file(&dir, 1), 200.0);
        std::thread::sleep(std::time::Duration::from_millis(20));
        std::fs::write(slot_file(&dir, 4), "not a save").unwrap();
        let s = slots_in(&dir);
        assert!(matches!(&s[0], Slot::Saved { level, difficulty: 2, .. } if level == "lev_1_m1"));
        assert!(matches!(s[1], Slot::Saved { .. }));
        assert_eq!((s[2].clone(), s[3].clone()), (Slot::Empty, Slot::Damaged));
        assert_eq!(last_save_in(&dir), Some(slot_file(&dir, 1)), "slot 1 was written last; the damaged slot 4 is not a save");
        save(dir.join("quicksave.json"), 300.0);
        assert_eq!(last_save_in(&dir), Some(dir.join("quicksave.json")));
        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Save anywhere by default; with checkpoint_saves only at checkpoints; scripts always.
    #[test]
    fn save_rules() {
        assert!(may_save(false, SaveSource::Menu));
        assert!(!may_save(false, SaveSource::Checkpoint));
        assert!(!may_save(true, SaveSource::Menu));
        assert!(may_save(true, SaveSource::Checkpoint));
        assert!(may_save(true, SaveSource::Script) && may_save(false, SaveSource::Script));
    }
}
