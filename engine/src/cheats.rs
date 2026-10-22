//! Cheats (Options > Cheats, page pui_cheats): type a code; Cheats_Check 0x004107c0 compares it (case-insensitive)
//! with the 12 codes of the table 0x00589090 {text id, code, flag} (read from the exe); a match marks the cheat known
//! (Game+0x48c, kept in options.json) and toggles it (Game+0x4c8), showing "%s activated." / "%s deactivated."
//! (ui_cheaton / ui_cheatoff); no match: "Invalid cheat code." (ui_cheatunknown).
//!
//! Effects, ported where the meaning is plain from the names: full ammo and full health (on activation), infinite
//! ammo (clips are not used up), invincible / god mode (Roman takes no damage), big heads / hands / boots (those
//! bones drawn twice the size: guess at the factor), Topsy Turvy screen (the view upside down). Play Any Level /
//! View Any Movie have nothing to unlock in this one-level build; List Cheats shows the codes known.

use bevy::input::keyboard::{Key, KeyboardInput};
use bevy::prelude::*;

/// (text id, code, flag), table 0x00589090.
pub const CHEATS: [(&str, &str, u32); 12] = [
    ("ui_cheat_fullammo", "SHOOTMOR", 0x100),
    ("ui_cheat_fullhealth", "AMBULNCE", 0x200),
    ("ui_cheat_bighead", "VERYHIIQ", 0x2),
    ("ui_cheat_bighands", "EATGRENZ", 0x4),
    ("ui_cheat_bigboots", "SHOESIZE", 0x8),
    ("ui_cheat_infammo", "NDMRBLTS", 0x10),
    ("ui_cheat_invincible", "SHIELDME", 0x20),
    ("ui_cheat_godmode", "PRAYTOME", 0x1),
    ("ui_cheat_unlocklevels", "MISSIONS", 0x800),
    ("ui_cheat_unlockmovies", "AMATINEE", 0x1000),
    ("ui_cheat_flipscreen", "WAAAAAAH", 0x4000),
    ("ui_cheat_showall", "DAJIGSUP", 0x80),
];

/// NDMRBLTS (Game+0x175 set by the flag): clips and specials are not used up (0x004a2450, 0x004a1970, 0x004a1aa0).
pub const INFINITE_AMMO: u32 = 0x10;
/// SHOOTMOR: every weapon filled on activation.
pub const FULL_AMMO: u32 = 0x100;
/// AMBULNCE: health back to the maximum on activation.
pub const FULL_HEALTH: u32 = 0x200;
/// PRAYTOME / SHIELDME: Roman takes no damage.
pub const GOD_MODE: u32 = 0x1;
pub const INVINCIBLE: u32 = 0x20;
/// VERYHIIQ / EATGRENZ / SHOESIZE: big heads, hands, boots.
pub const BIG_HEADS: u32 = 0x2;
pub const BIG_HANDS: u32 = 0x4;
pub const BIG_BOOTS: u32 = 0x8;
/// MISSIONS / AMATINEE: every level / movie listed in Extras.
pub const ALL_LEVELS: u32 = 0x800;
pub const ALL_MOVIES: u32 = 0x1000;
/// WAAAAAAH: the view upside down (camera.rs).
pub const FLIP_SCREEN: u32 = 0x4000;

/// SHOOTMOR's fill: every carried gun full, every special held (and the sticky bombs) to its maximum.
pub fn full_ammo(arsenal: &mut crate::weapons::Arsenal, data: &crate::gamedata::GameData) {
    for id in crate::weapons::WeaponId::ALL {
        if let Some(w) = arsenal.gun_mut(id) {
            w.set_ammo(-2);
        }
    }
    for s in crate::weapons::SpecialId::ALL {
        if arsenal.count(s) > 0 || s == crate::weapons::SpecialId::StickyBomb {
            arsenal.set_count(s, crate::weapons::spec::special_max(data, s));
        }
    }
}

/// AMBULNCE's fill: health to Roman's maximum.
pub fn full_health(r: &crate::player::Roman, h: &mut crate::fx::Health) {
    h.0 = r.max_health;
}

/// Cheat_FlagByName 0x00410e00: a cheat's flag by its text id (_stricmp over the table), 0 when none matches.
pub fn flag_by_name(id: &str) -> u32 {
    CHEATS.iter().find(|c| c.0.eq_ignore_ascii_case(id)).map(|c| c.2).unwrap_or(0)
}

/// Cheat_IsUnlocked 0x00410ea0: the cheat's flag is in the known mask Game+0x48c (options.json `cheats_known`).
pub fn is_unlocked(id: &str, opts: &crate::options::GameOptions) -> bool {
    let f = flag_by_name(id);
    f != 0 && opts.cheats_known.contains(&f)
}

/// Cheat_Unlock 0x00410e40 (from the token reward 0x004a0850, enable 0): the flag joins Game+0x48c (saved with the
/// options, Options_Apply 0x00401ce0); with `enable` it is also switched on (Game+0x4c8). False for an unknown id.
pub fn unlock(id: &str, enable: bool, opts: &mut crate::options::GameOptions, cheats: Option<&mut Cheats>) -> bool {
    let f = flag_by_name(id);
    if f == 0 {
        return false;
    }
    if !opts.cheats_known.contains(&f) {
        opts.cheats_known.push(f);
        opts.save();
    }
    if enable {
        if let Some(c) = cheats {
            c.active |= f;
        }
    }
    true
}

/// The active cheats (Game+0x4c8) and the cheat entry page's state.
#[derive(Resource, Default)]
pub struct Cheats {
    pub active: u32,
    pub typed: String,
    pub result: String,
    pub entering: bool,
    /// check `typed` now (gamemenu.rs: a click on a cheat's name in the grid, Cheats_Check 0x004107c0 state 1)
    pub submit: bool,
}

impl Cheats {
    pub fn on(&self, flag: u32) -> bool {
        self.active & flag != 0
    }

    /// Enter a code as if typed on the Cheats page and confirmed (scripted runs, a console): checked next frame by
    /// `entry` with the same rules and messages as typing it.
    pub fn enter(&mut self, code: &str) {
        self.typed = code.to_owned();
        self.submit = true;
    }
}

pub struct CheatsPlugin;

impl Plugin for CheatsPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Cheats>()
            .add_systems(Update, (entry, effects))
            .add_systems(PostUpdate, body_scale.after(crate::skin::AnimateSet).before(bevy::transform::TransformSystems::Propagate));
    }
}

/// Typing on the cheat page: letters (8 max, as the codes), Backspace, Enter to check.
#[allow(clippy::too_many_arguments)]
fn entry(
    mut c: ResMut<Cheats>,
    mut keys: MessageReader<KeyboardInput>,
    buttons: Res<ButtonInput<KeyCode>>,
    mut opts: ResMut<crate::options::GameOptions>,
    texts: Option<Res<crate::gamemenu::Texts>>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut roman: Query<(&mut crate::player::Roman, &mut crate::fx::Health)>,
    data: Res<crate::gamedata::GameData>,
) {
    // a code entered from outside the page (`Cheats::enter`) is checked without reading the keyboard
    let entered = !c.entering && c.submit;
    if !c.entering && !entered {
        keys.clear();
        return;
    }
    keys.clear();
    // letters and digits by key (works for typed and scripted input), Backspace, Enter to check
    for k in buttons.get_just_pressed().filter(|_| !entered) {
        let ch = format!("{k:?}");
        let ch = ch.strip_prefix("Key").or_else(|| ch.strip_prefix("Digit")).filter(|x| x.len() == 1).map(str::to_owned);
        if let Some(ch) = ch {
            if c.typed.len() < 8 {
                c.typed.push_str(&ch);
            }
        }
    }
    if !entered && buttons.just_pressed(KeyCode::Backspace) {
        c.typed.pop();
    }
    let enter = buttons.just_pressed(KeyCode::Enter) || std::mem::take(&mut c.submit);
    for _ in 0..usize::from(enter) {
        let k = Key::Enter;
        match &k {
            Key::Enter => {
                let typed = std::mem::take(&mut c.typed);
                let get = |id: &str| texts.as_ref().map(|t| t.get(id)).unwrap_or_else(|| id.to_owned());
                match CHEATS.iter().find(|(_, code, _)| code.eq_ignore_ascii_case(&typed)) {
                    Some(&(id, _, flag)) => {
                        if !opts.cheats_known.contains(&flag) {
                            opts.cheats_known.push(flag);
                            opts.save();
                        }
                        c.active ^= flag;
                        let on = c.on(flag);
                        c.result = get(if on { "ui_cheaton" } else { "ui_cheatoff" }).replacen("%s", &get(id), 1);
                        // List Cheats (0x80): the page's grid then shows every cheat (gamemenu.rs, 0x00410b20)
                        // one-shot effects on activation
                        if on {
                            if let Ok((mut r, mut h)) = roman.single_mut() {
                                if flag == FULL_HEALTH {
                                    full_health(&r, &mut h);
                                }
                                if flag == FULL_AMMO {
                                    full_ammo(&mut r.arsenal, &data);
                                }
                            }
                        }
                        // a match plays the select set (0x00491850), a miss the cancel sound (0x00491870)
                        sounds.write(crate::gamemenu::MenuSound::Select.sound());
                    }
                    None => {
                        c.result = get("ui_cheatunknown");
                        sounds.write(crate::audio::PlaySound::menu("ui_cancel"));
                    }
                }
            }
            _ => {}
        }
    }
}

/// Continuous effects: Roman takes no damage (invincible / god mode), clips are not used up (infinite ammo), the
/// view upside down (Topsy Turvy screen).
fn effects(
    c: Res<Cheats>,
    mut roman: Query<(&mut crate::player::Roman, &mut crate::fx::Health)>,
    mut last: Local<Option<f32>>,
    mut cam: Query<&mut Transform, With<Camera3d>>,
) {
    let Ok((mut r, mut h)) = roman.single_mut() else { return };
    if c.on(INVINCIBLE) || c.on(GOD_MODE) {
        if let Some(prev) = *last {
            if h.0 < prev {
                h.0 = prev;
            }
        }
    }
    *last = Some(h.0);
    if c.on(INFINITE_AMMO) {
        for id in crate::weapons::WeaponId::ALL {
            if let Some(w) = r.arsenal.gun_mut(id) {
                w.clip = w.clip.max(1);
            }
        }
    }
    let _ = &mut cam;
}

/// Big heads / hands / boots: those bones of every character drawn twice the size (guess at the factor).
fn body_scale(c: Res<Cheats>, rigs: Query<&crate::skin::Animator>, mut joints: Query<&mut Transform>) {

    if !(c.on(BIG_HEADS) || c.on(BIG_HANDS) || c.on(BIG_BOOTS)) {
        return;
    }
    for a in &rigs {
        for (i, name) in a.skeleton.names.iter().enumerate() {
            let n = name.trim().to_lowercase();
            let big = (c.on(BIG_HEADS) && n == "head") || (c.on(BIG_HANDS) && (n == "l_hand" || n == "r_hand")) || (c.on(BIG_BOOTS) && (n == "l_foot" || n == "r_foot"));
            if big {
                if let Ok(mut t) = joints.get_mut(a.joints[i]) {
                    t.scale = Vec3::splat(2.0);
                }
            }
        }
    }
}
