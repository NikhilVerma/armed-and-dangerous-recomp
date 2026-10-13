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

/// The active cheats (Game+0x4c8) and the cheat entry page's state.
#[derive(Resource, Default)]
pub struct Cheats {
    pub active: u32,
    pub typed: String,
    pub result: String,
    pub entering: bool,
}

impl Cheats {
    pub fn on(&self, flag: u32) -> bool {
        self.active & flag != 0
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
    if !c.entering {
        keys.clear();
        return;
    }
    keys.clear();
    // letters and digits by key (works for typed and scripted input), Backspace, Enter to check
    for k in buttons.get_just_pressed() {
        let ch = format!("{k:?}");
        let ch = ch.strip_prefix("Key").or_else(|| ch.strip_prefix("Digit")).filter(|x| x.len() == 1).map(str::to_owned);
        if let Some(ch) = ch {
            if c.typed.len() < 8 {
                c.typed.push_str(&ch);
            }
        }
    }
    if buttons.just_pressed(KeyCode::Backspace) {
        c.typed.pop();
    }
    let enter = buttons.just_pressed(KeyCode::Enter);
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
                        if flag == 0x80 {
                            // List Cheats: the codes known
                            let list: Vec<String> = CHEATS.iter().filter(|x| opts.cheats_known.contains(&x.2)).map(|x| format!("{}: {}", get(x.0), x.1)).collect();
                            c.result = list.join("<br>");
                        }
                        // one-shot effects on activation
                        if on {
                            if let Ok((mut r, mut h)) = roman.single_mut() {
                                if flag == 0x200 {
                                    h.0 = r.max_health;
                                }
                                if flag == 0x100 {
                                    let g = data.props("p_globalsettings");
                                    r.stickies = g.i("stickybomb_max_ammo", 5) as u32;
                                    r.rifle.clip = r.rifle.clip_size;
                                    if let Some(w) = r.mg.as_mut() {
                                        let full = crate::player::machinegun(&data);
                                        w.clip = full.clip;
                                        w.ammo = full.ammo;
                                    }
                                }
                            }
                        }
                        sounds.write(crate::audio::PlaySound::ui("ui_select"));
                    }
                    None => {
                        c.result = get("ui_cheatunknown");
                        sounds.write(crate::audio::PlaySound::ui("ui_cancel"));
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
    if c.on(0x20) || c.on(0x1) {
        if let Some(prev) = *last {
            if h.0 < prev {
                h.0 = prev;
            }
        }
    }
    *last = Some(h.0);
    if c.on(0x10) {
        r.rifle.clip = r.rifle.clip.max(1);
        if let Some(w) = r.mg.as_mut() {
            w.clip = w.clip.max(1);
        }
    }
    let _ = &mut cam;
}

/// Big heads / hands / boots: those bones of every character drawn twice the size (guess at the factor).
fn body_scale(c: Res<Cheats>, rigs: Query<&crate::skin::Animator>, mut joints: Query<&mut Transform>) {

    if !(c.on(0x2) || c.on(0x4) || c.on(0x8)) {
        return;
    }
    for a in &rigs {
        for (i, name) in a.skeleton.names.iter().enumerate() {
            let n = name.trim().to_lowercase();
            let big = (c.on(0x2) && n == "head") || (c.on(0x4) && (n == "l_hand" || n == "r_hand")) || (c.on(0x8) && (n == "l_foot" || n == "r_foot"));
            if big {
                if let Ok(mut t) = joints.get_mut(a.joints[i]) {
                    t.scale = Vec3::splat(2.0);
                }
            }
        }
    }
}
