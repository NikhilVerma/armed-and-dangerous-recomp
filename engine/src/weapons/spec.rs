//! The weapons' typed data, read once from the level's property sets (serde, `Props::typed`): the gun object's set
//! (p_player_<template>: WeaponObject load 0x004a1bf0, PlayerWeapon load 0x004a2150, PlayerVindaloo load 0x004a24d0)
//! and the weapon table's numbers from p_globalsettings (the descriptor's max clip +0x140, max ammo +0x144 and range
//! +0x148: <name>_max_clip / _max_ammo / _range, LevelProperties render_r6.cpp:1060-1070).

use super::arsenal::{SpecialId, WeaponId};
use crate::gamedata::{GameData, flag};
use serde::Deserialize;

fn one() -> f32 {
    1.0
}
fn reload_default() -> f32 {
    1.5
}
fn six() -> f32 {
    6.0
}

/// A gun object's own properties (p_player_rifle ...), with the readers' code defaults.
#[derive(Deserialize, Clone, Debug, PartialEq)]
pub struct GunProps {
    /// "recycle_time" (+0x540, 1.0): seconds between shots
    #[serde(default = "one")]
    pub recycle_time: f32,
    /// "clip_reload_time" (+0x558, 1.5)
    #[serde(default = "reload_default")]
    pub clip_reload_time: f32,
    /// "projectile_name" (+0x460): the projectile's template (its set is p_<name>)
    #[serde(default)]
    pub projectile_name: String,
    /// "muzzle_fx_name" (+0x500) on "muzzle_bone_name" (+0x4e0)
    #[serde(default)]
    pub muzzle_fx_name: String,
    /// "rapid_fire" (flag 2): the muzzle fx is held while firing (0x004a1f20 / 0x004a1dd0)
    #[serde(default, deserialize_with = "flag")]
    pub rapid_fire: bool,
    /// "two_handed" (flag 1)
    #[serde(default, deserialize_with = "flag")]
    pub two_handed: bool,
    /// PlayerVindaloo 0x004a24d0: "start_angle_yaw" (+0x568) / "start_angle_pitch" (+0x564), degrees (default 6): the
    /// rocket fan's spread
    #[serde(default = "six")]
    pub start_angle_yaw: f32,
    #[serde(default = "six")]
    pub start_angle_pitch: f32,
}

impl Default for GunProps {
    fn default() -> Self {
        GunProps { recycle_time: 1.0, clip_reload_time: 1.5, projectile_name: String::new(), muzzle_fx_name: String::new(), rapid_fire: false, two_handed: false, start_angle_yaw: 6.0, start_angle_pitch: 6.0 }
    }
}

/// Everything a carried gun needs.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct GunSpec {
    /// its property set's name (p_player_rifle)
    pub props: String,
    pub gun: GunProps,
    /// the projectile's property set (p_player_rifle_bullet)
    pub projectile: String,
    pub max_clip: u32,
    pub max_ammo: u32,
    /// the weapon's range (<name>_range: the crosshair ray's length, the target passes' range test 0x00408330; the
    /// sniper bullet's flight 0x004a2a70)
    pub range: f32,
    /// machinegun_spread_pct (the machine gun's scatter, 0x0047f4e0); 0 for the others
    pub spread: f32,
    /// the rifle's spare is never spent (its reload 0x004a29d0); the HUD shows "%d/" and the infinity sign
    pub unlimited: bool,
}

impl GunSpec {
    /// The gun's data in this level.
    pub fn load(data: &GameData, id: WeaponId) -> Self {
        let props = format!("p_{}", id.template());
        let gun: GunProps = data.props(&props).typed();
        let g = data.props("p_globalsettings");
        let n = id.name();
        // (the descriptors' defaults: LevelProperties_Load reads every *_max_* with 0 and every range with 10)
        let projectile = if gun.projectile_name.is_empty() { String::new() } else { format!("p_{}", gun.projectile_name) };
        GunSpec {
            props,
            projectile,
            max_clip: g.i(&format!("{n}_max_clip"), 0).max(0) as u32,
            max_ammo: g.i(&format!("{n}_max_ammo"), 0).max(0) as u32,
            range: g.f(&format!("{n}_range"), 10.0),
            spread: if id == WeaponId::Machinegun { g.f("machinegun_spread_pct", 1.0) } else { 0.0 },
            unlimited: id == WeaponId::Rifle,
            gun,
        }
    }
}

/// A special's maximum count (the descriptor's max ammo +0x144: <name>_max_ammo).
pub fn special_max(data: &GameData, s: SpecialId) -> u32 {
    data.props("p_globalsettings").i(&format!("{}_max_ammo", s.name()), 1).max(0) as u32
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::gamedata::Props;

    /// The typed read: the set's values over the code defaults, 0 / 1 flags as bools.
    #[test]
    fn gun_props_from_a_set() {
        let p = Props(serde_json::json!({"behavior": "player_rlvindaloo", "recycle_time": 1.0, "projectile_name": "vindaloo_missile", "two_handed": 1, "start_angle_yaw": 10.0, "clip_reload_time": 1}).as_object().unwrap().clone());
        let g: GunProps = p.typed();
        assert_eq!(g.projectile_name, "vindaloo_missile");
        assert!(g.two_handed && !g.rapid_fire);
        assert_eq!((g.start_angle_yaw, g.start_angle_pitch, g.clip_reload_time), (10.0, 6.0, 1.0));
        let empty: GunProps = Props::default().typed();
        assert_eq!(empty, GunProps::default());
    }
}
