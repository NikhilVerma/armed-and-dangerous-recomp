//! The weapon table: one row of names per weapon slot, the player's weapons first, then the AI's. The original
//! builds it in the Game object at start-up (Weapon_BuildTable 0x00402110, from Game_Init 0x004011e0): 28 entries of
//! 0x150 bytes from Game+0x2770, each a set of 0x20-byte name fields copied from the exe's string constants. The
//! slot number is the weapon id the rest of the game uses: the pickups' kinds (pickup.rs: ammo 0..5, specials 6..10),
//! the AI's pickup_drop_specific (an index into the pickup names at Game+0x2890, Ai_InitGeneric 0x004247f0), the pub's
//! weapon info and awards, the HUD icon and crosshair.
//!
//! Field offsets within an entry: +0x00 player object template, +0x20 the name's text id, +0x40 the pub's info text,
//! +0x60 the pub award's UI element, +0x80 the pub award's text, +0xa0 the HUD weapon icon, +0xc0 the crosshair,
//! +0xe0 Trotter's firing animation, +0x100 the zoom camera, +0x120 the ammo / special pickup.
//!
//! Used so far by pickup.rs (the ammo pickup of the gun in hand); the lookups are the API for the ports still to
//! come (the AI's pickup_drop_specific, the specials not on lev_1_m1).
#![allow(dead_code)]

/// One weapon slot's names (None: the original leaves the field empty).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct WeaponSlot {
    /// +0x00: the object template held / thrown (player_machinegun), or the AI weapon's template
    pub template: Option<&'static str>,
    /// +0x20: the text id of its name (weap_machinegun)
    pub name_text: Option<&'static str>,
    /// +0x40: the pub's description text id
    pub pub_info: Option<&'static str>,
    /// +0x60 / +0x80: the pub award's UI element and text (specials only)
    pub award_ui: Option<&'static str>,
    pub award_text: Option<&'static str>,
    /// +0xa0: the HUD's weapon icon element
    pub hud_icon: Option<&'static str>,
    /// +0xc0: the crosshair element (guns only)
    pub crosshair: Option<&'static str>,
    /// +0xe0: Trotter's firing animation
    pub fire_anim: Option<&'static str>,
    /// +0x100: the zoom camera (guns only)
    pub camera: Option<&'static str>,
    /// +0x120: the pickup that refills it
    pub pickup: Option<&'static str>,
}

const fn o(s: &'static str) -> Option<&'static str> {
    if s.is_empty() { None } else { Some(s) }
}

/// A player gun: template, name, pub info, HUD icon, crosshair, fire anim, camera, pickup.
#[allow(clippy::too_many_arguments)]
const fn gun(t: &'static str, n: &'static str, i: &'static str, h: &'static str, x: &'static str, f: &'static str, c: &'static str, p: &'static str) -> WeaponSlot {
    WeaponSlot { template: o(t), name_text: o(n), pub_info: o(i), award_ui: None, award_text: None, hud_icon: o(h), crosshair: o(x), fire_anim: o(f), camera: o(c), pickup: o(p) }
}

/// A player special: template, name, pub info, award UI, award text, HUD icon, fire anim, pickup.
#[allow(clippy::too_many_arguments)]
const fn special(t: &'static str, n: &'static str, i: &'static str, au: &'static str, at: &'static str, h: &'static str, f: &'static str, p: &'static str) -> WeaponSlot {
    WeaponSlot { template: o(t), name_text: o(n), pub_info: o(i), award_ui: o(au), award_text: o(at), hud_icon: o(h), crosshair: None, fire_anim: o(f), camera: None, pickup: o(p) }
}

/// An AI weapon: its template only.
const fn ai(t: &'static str) -> WeaponSlot {
    WeaponSlot { template: o(t), name_text: None, pub_info: None, award_ui: None, award_text: None, hud_icon: None, crosshair: None, fire_anim: None, camera: None, pickup: None }
}

pub const MACHINEGUN: usize = 0;
pub const SNIPERGUN: usize = 1;
pub const RIFLE: usize = 2;
pub const STICKY_BOMB: usize = 10;
pub const TICKER_BOMB: usize = 11;
/// The player's weapons: guns 0..=5, specials 6..=10, the ticker bomb 11 (award and icon only).
pub const PLAYER_SLOTS: std::ops::Range<usize> = 0..12;

/// Weapon_BuildTable 0x00402110, in slot order (Game+0x2770 + slot * 0x150).
pub const WEAPON_TABLE: [WeaponSlot; 28] = [
    gun("player_machinegun", "weap_machinegun", "pub_info_machinegun", "ui_player_mg", "ui_mg_crosshair", "trotter_fire_mgun", "Camera_machinegun", "pickup_mgun_bullets"),
    gun("player_snipergun", "weap_snipergun", "pub_info_snipergun", "ui_player_sniper", "ui_sniper_crosshair", "trotter_fire_single", "Camera_sniper", "pickup_sniper_bullets"),
    gun("player_rifle", "weap_rifle", "pub_info_rifle", "ui_player_rifle", "ui_crosshair", "trotter_fire_single", "Camera_rifle", "pickup_rifle_bullets"),
    gun("player_rlvindaloo", "weap_rlvindaloo", "pub_info_rlvindaloo", "ui_player_rlvindaloo", "ui_vindaloo_crosshair", "trotter_fire_gurner", "camera_vindaloo", "pickup_vindaloo_mortars"),
    gun("player_rlgurner", "weap_rlgurner", "pub_info_rlgurner", "ui_player_rlgurner", "ui_gurner_crosshair", "trotter_fire_gurner", "camera_gurner", "pickup_gurner_rockets"),
    gun("player_sharkgun", "weap_sharkgun", "pub_info_sharkgun", "ui_player_sharkgun", "ui_sharkgun_crosshair", "trotter_fire_gurner", "camera_gurner", "pickup_sharkgun_rockets"),
    special("player_guyfawkes_bomb", "weap_guyfawkes", "pub_info_guyfawkes", "ui_pub_award_guyfawkes", "pub_award_guyfawkes", "ui_player_guyfawkes", "", "pickup_guyfawkes"),
    special("player_rabbiter", "weap_bunchoffives", "pub_info_bunchoffives", "ui_pub_award_bof", "pub_award_bof", "ui_player_bunchoffives", "trotter_fire_gurner", "pickup_bunchoffives"),
    special("player_topsyturvy", "weap_topsyturvy", "pub_info_topsyturvy", "ui_pub_award_topsy", "pub_award_topsy", "ui_player_topsy", "trotter_fire_single", "pickup_topsyturvy"),
    special("player_blackhole_box", "weap_blackhole", "pub_info_blackhole", "ui_pub_award_blackhole", "pub_award_blackhole", "ui_player_blackhole", "trotter_fire_gurner", "pickup_blackhole"),
    special("player_sticky_bomb", "weap_stickybomb", "pub_info_sticky", "ui_pub_award_stickybomb", "pub_award_stickybomb", "ui_player_grenades", "", "pickup_sticky_bombs"),
    special("", "", "", "ui_pub_award_tickerbomb", "pub_award_tickerbomb", "ui_tickerbomb", "", ""),
    ai("molmer_pistol"),
    ai("q_chaingun"),
    ai("grunt_bow"),
    ai("grunt_rifle"),
    ai("captain_mg"),
    ai("captain_rocketlauncher"),
    ai("captain_snipergun"),
    ai("q112_chaingun"),
    ai("saucer_missile_launcher"),
    ai("mini_zeppelin_turret"),
    ai("colossus_turret"),
    ai("shrubpatrolgun"),
    ai(""),
    ai("monk_gun"),
    ai("monk_gun2"),
    ai(""),
];

/// The slot whose template is `name` (a weapon object's slot).
pub fn slot_of_template(name: &str) -> Option<usize> {
    WEAPON_TABLE.iter().position(|w| w.template == Some(name))
}

/// The slot a pickup refills.
pub fn slot_of_pickup(name: &str) -> Option<usize> {
    WEAPON_TABLE.iter().position(|w| w.pickup == Some(name))
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The table's shape: 28 entries filling Game+0x2770..+0x4c30 (0x930 dwords cleared by 0x00402110).
    #[test]
    fn shape() {
        assert_eq!(WEAPON_TABLE.len() * 0x150, 0x930 * 4);
        assert!(WEAPON_TABLE[PLAYER_SLOTS].iter().all(|w| w.award_ui.is_some() || w.crosshair.is_some()));
        assert!(WEAPON_TABLE[PLAYER_SLOTS.end..].iter().all(|w| w.name_text.is_none() && w.pickup.is_none()));
    }

    /// Values copied at their offsets (spot checks of 0x00402110's copies: offset in Game, string).
    #[test]
    fn values() {
        let at = |off: usize| (off - 0x2770) / 0x150;
        assert_eq!(WEAPON_TABLE[at(0x2770)].template, Some("player_machinegun"));
        assert_eq!(WEAPON_TABLE[at(0x2890)].pickup, Some("pickup_mgun_bullets"));
        assert_eq!(WEAPON_TABLE[at(0x2ad0)].crosshair, Some("ui_crosshair"));
        assert_eq!(WEAPON_TABLE[at(0x2b10)].camera, Some("Camera_rifle"));
        assert_eq!(WEAPON_TABLE[at(0x3180)].fire_anim, Some("trotter_fire_gurner"));
        assert_eq!(WEAPON_TABLE[at(0x3530)].hud_icon, Some("ui_player_grenades"));
        assert_eq!(WEAPON_TABLE[at(0x3680)].hud_icon, Some("ui_tickerbomb"));
        assert_eq!(WEAPON_TABLE[at(0x3b20)].template, Some("grunt_rifle"));
        assert_eq!(WEAPON_TABLE[at(0x4990)].template, Some("monk_gun2"));
        assert_eq!(WEAPON_TABLE[at(0x46f0)].template, None);
    }

    /// The slot numbers the pickups use (pickup.rs Kind: ammo 0..5, specials 6..10).
    #[test]
    fn slots() {
        assert_eq!(slot_of_pickup("pickup_mgun_bullets"), Some(MACHINEGUN));
        assert_eq!(slot_of_pickup("pickup_sniper_bullets"), Some(SNIPERGUN));
        assert_eq!(slot_of_pickup("pickup_rifle_bullets"), Some(RIFLE));
        assert_eq!(slot_of_pickup("pickup_sharkgun_rockets"), Some(5));
        assert_eq!(slot_of_pickup("pickup_guyfawkes"), Some(6));
        assert_eq!(slot_of_pickup("pickup_blackhole"), Some(9));
        assert_eq!(slot_of_pickup("pickup_sticky_bombs"), Some(STICKY_BOMB));
        assert_eq!(slot_of_template("player_sticky_bomb"), Some(STICKY_BOMB));
        assert_eq!(slot_of_template("grunt_bow"), Some(14));
        assert_eq!(WEAPON_TABLE[TICKER_BOMB].award_text, Some("pub_award_tickerbomb"));
    }
}
