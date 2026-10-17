//! Roman's weapons: what he carries and what is in his hands (the PlayerObject's weapon state +0xc24,
//! game_09.cpp 723-1990).
//!
//! The original keeps two slot lists of weapon ids (init 0x004a0d00 / reset 0x004a0fb0): the primary list holds
//! three guns, the special list the specials, each with a current slot and a pending switch; the ammo records (clip
//! +0, spare +4, by id) live beside them. Here: [`Arsenal`] with three gun slots ([`Gun`]: its typed data and its
//! ammo record) and a count per special.
//!
//! The ids are the weapon table's slots (weapontable.rs, Weapon_BuildTable 0x00402110): guns 0..=5, specials 6..=10.

use super::spec::GunSpec;
use crate::weapontable::{WEAPON_TABLE, WeaponSlot};
use bevy::prelude::*;

/// A player gun, by its weapon table slot.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Reflect, serde::Serialize, serde::Deserialize)]
pub enum WeaponId {
    /// the Flemming machine gun (player_machinegun)
    Machinegun = 0,
    /// the Cyclops sniper rifle (player_snipergun)
    Snipergun = 1,
    /// the Hawkings rifle (player_rifle)
    Rifle = 2,
    /// the Vindaloo rocket launcher (player_rlvindaloo)
    Vindaloo = 3,
    /// the Gurner personal mortar (player_rlgurner)
    Gurner = 4,
    /// the Land Shark gun (player_sharkgun)
    Sharkgun = 5,
}

impl WeaponId {
    pub const ALL: [WeaponId; 6] = [WeaponId::Machinegun, WeaponId::Snipergun, WeaponId::Rifle, WeaponId::Vindaloo, WeaponId::Gurner, WeaponId::Sharkgun];

    /// The weapon table slot (the id the original passes around).
    pub fn slot(self) -> usize {
        self as usize
    }

    pub fn from_slot(slot: usize) -> Option<Self> {
        Self::ALL.get(slot).copied()
    }

    /// The name the mission's properties use (player_add_<name>, player_set_ammo_<name>, p_globalsettings'
    /// <name>_max_clip / _max_ammo / _range) and the state log shows.
    pub fn name(self) -> &'static str {
        match self {
            WeaponId::Machinegun => "machinegun",
            WeaponId::Snipergun => "snipergun",
            WeaponId::Rifle => "rifle",
            WeaponId::Vindaloo => "vindaloo",
            WeaponId::Gurner => "gurner",
            WeaponId::Sharkgun => "sharkgun",
        }
    }

    /// By the mission's name or the pub's (the pub info's has_* / the weapon objects: "rlvindaloo", "rlgurner").
    pub fn from_name(name: &str) -> Option<Self> {
        match name {
            "rlvindaloo" => Some(WeaponId::Vindaloo),
            "rlgurner" => Some(WeaponId::Gurner),
            n => Self::ALL.into_iter().find(|w| w.name() == n),
        }
    }

    /// The weapon table's names (template, HUD icon, crosshair, zoom camera, ammo pickup).
    pub fn table(self) -> &'static WeaponSlot {
        &WEAPON_TABLE[self.slot()]
    }

    /// The object template held in the hand (player_rlvindaloo ...).
    pub fn template(self) -> &'static str {
        self.table().template.unwrap_or_default()
    }

    /// The launchers (3..=5): Roman's fire anim is the recoil set (trotter_fire_rr, anim state 2) and each shot pushes
    /// him back (0x004788a0's jump table 0x0047a920).
    pub fn launcher(self) -> bool {
        matches!(self, WeaponId::Vindaloo | WeaponId::Gurner | WeaponId::Sharkgun)
    }
}

/// A special weapon, by its weapon table slot.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Default, Reflect, serde::Serialize, serde::Deserialize)]
pub enum SpecialId {
    /// the Guy Fawkes traitor bomb (player_guyfawkes_bomb)
    GuyFawkes = 6,
    /// the knockout bomb / punching glove (player_rabbiter)
    BunchOfFives = 7,
    /// the Topsy Turvy bomb (player_topsyturvy, topsy.rs)
    TopsyTurvy = 8,
    /// the world's smallest black hole (player_blackhole_box)
    BlackHole = 9,
    /// the sticky bombs (player_sticky_bomb, sticky.rs)
    #[default]
    StickyBomb = 10,
}

impl SpecialId {
    pub const ALL: [SpecialId; 5] = [SpecialId::GuyFawkes, SpecialId::BunchOfFives, SpecialId::TopsyTurvy, SpecialId::BlackHole, SpecialId::StickyBomb];

    pub fn slot(self) -> usize {
        self as usize
    }

    pub fn from_slot(slot: usize) -> Option<Self> {
        Self::ALL.into_iter().find(|s| s.slot() == slot)
    }

    /// The mission's name (player_set_num_<name>, p_globalsettings' <name>_max_ammo).
    pub fn name(self) -> &'static str {
        match self {
            SpecialId::GuyFawkes => "guyfawkes",
            SpecialId::BunchOfFives => "bunchoffives",
            SpecialId::TopsyTurvy => "topsyturvy",
            SpecialId::BlackHole => "blackhole",
            SpecialId::StickyBomb => "stickybomb",
        }
    }

    /// By the mission's name or the pub's ("topsy", "bof").
    pub fn from_name(name: &str) -> Option<Self> {
        match name {
            "topsy" => Some(SpecialId::TopsyTurvy),
            "bof" => Some(SpecialId::BunchOfFives),
            n => Self::ALL.into_iter().find(|s| s.name() == n),
        }
    }

    pub fn table(self) -> &'static WeaponSlot {
        &WEAPON_TABLE[self.slot()]
    }

    fn index(self) -> usize {
        self.slot() - 6
    }
}

/// One carried gun: its data and its ammo record (the original's record by id: clip +0, spare +4).
#[derive(Clone, Debug)]
pub struct Gun {
    pub id: WeaponId,
    pub spec: GunSpec,
    pub clip: u32,
    pub ammo: u32,
    /// the recycle gate: seconds until the next shot may leave (0x004a1e40: time since the last fire > recycle_time)
    pub timer: f32,
}

impl Gun {
    /// A gun filled to its maximum (0x004a14e0 with -2: clip and spare to the descriptor's max clip / max ammo).
    pub fn full(id: WeaponId, spec: GunSpec) -> Self {
        Gun { id, clip: spec.max_clip, ammo: spec.max_ammo, spec, timer: 0.0 }
    }

    /// 0x004a14e0 with n >= 0: clip = min(n, max clip), the rest spare. -2 fills both, -1 leaves the record (empty on a
    /// new player).
    pub fn set_ammo(&mut self, n: i64) {
        match n {
            -2 => (self.clip, self.ammo) = (self.spec.max_clip, self.spec.max_ammo),
            -1 => {}
            n => {
                let n = n.max(0) as u32;
                self.clip = n.min(self.spec.max_clip);
                self.ammo = n - self.clip;
            }
        }
    }

    /// A slot counts as usable when its record holds clip + spare != 0 (0x004a0c10 / 0x004a0c70).
    pub fn usable(&self) -> bool {
        self.clip > 0 || self.ammo > 0
    }

    /// The ammo pickup's add (0x004a13a0): the spare fills up to max ammo, the rest goes to the clip (capped at max
    /// clip); refused when both are full. The rifle's unlimited spare counts as full.
    pub fn add_ammo(&mut self, amount: u32) -> bool {
        let (max_ammo, max_clip) = (self.spec.max_ammo, self.spec.max_clip);
        let spare_full = self.spec.unlimited || self.ammo >= max_ammo;
        if spare_full && self.clip >= max_clip {
            return false;
        }
        let take = if self.spec.unlimited { 0 } else { max_ammo.saturating_sub(self.ammo).min(amount) };
        self.ammo += take;
        self.clip = (self.clip + (amount - take)).min(max_clip);
        true
    }

    /// The reload's refill (0x004a22e0; the rifle's 0x004a29d0 leaves its spare as it is).
    pub fn refill(&mut self) {
        let n = (self.spec.max_clip.saturating_sub(self.clip)).min(self.ammo);
        self.clip += n;
        if !self.spec.unlimited {
            self.ammo -= n;
        }
    }

    /// Can reload (0x004a22a0): spare ammo and the clip below the maximum.
    pub fn can_reload(&self) -> bool {
        self.ammo > 0 && self.clip < self.spec.max_clip
    }
}

/// How many guns Roman carries (the primary list's 3 slots, 0x004a0b10).
pub const GUN_SLOTS: usize = 3;

/// Roman's weapons (the weapon state +0xc24).
#[derive(Clone, Debug, Default)]
pub struct Arsenal {
    slots: [Option<Gun>; GUN_SLOTS],
    /// the primary list's current slot (+0x24)
    current: usize,
    /// each special's count (the ammo records' spare of ids 6..10)
    specials: [u32; 5],
    /// the specials in the special list: once given (a count set above 0), a special stays listed when used up
    /// (0x004a14e0 removes it only when set to 0 outright: `set_count`)
    listed: [bool; 5],
    /// the special in hand (the special list's current slot +0x54)
    pub special: SpecialId,
}

impl Arsenal {
    pub fn has(&self, id: WeaponId) -> bool {
        self.slot_of(id).is_some()
    }

    fn slot_of(&self, id: WeaponId) -> Option<usize> {
        self.slots.iter().position(|g| g.as_ref().is_some_and(|g| g.id == id))
    }

    pub fn gun(&self, id: WeaponId) -> Option<&Gun> {
        self.slots.iter().flatten().find(|g| g.id == id)
    }

    pub fn gun_mut(&mut self, id: WeaponId) -> Option<&mut Gun> {
        self.slots.iter_mut().flatten().find(|g| g.id == id)
    }

    /// The guns in slot order.
    pub fn guns(&self) -> impl Iterator<Item = &Gun> {
        self.slots.iter().flatten()
    }

    /// The slots as the pub shows them (None: empty).
    pub fn slot_ids(&self) -> [Option<WeaponId>; GUN_SLOTS] {
        std::array::from_fn(|i| self.slots[i].as_ref().map(|g| g.id))
    }

    /// The gun in hand (0x004a1660 / 0x004a15f0), if any.
    pub fn in_hand(&self) -> Option<&Gun> {
        self.slots[self.current].as_ref()
    }

    pub fn in_hand_mut(&mut self) -> Option<&mut Gun> {
        self.slots[self.current].as_mut()
    }

    pub fn in_hand_id(&self) -> Option<WeaponId> {
        self.in_hand().map(|g| g.id)
    }

    /// Adds a gun into the first free slot (0x004a0ef0 / 0x004a0b80); false when it is held already or the slots are
    /// full.
    pub fn add(&mut self, gun: Gun) -> bool {
        if self.has(gun.id) {
            return false;
        }
        match self.slots.iter().position(Option::is_none) {
            Some(i) => {
                self.slots[i] = Some(gun);
                true
            }
            None => false,
        }
    }

    /// Replaces the slot holding `old` with `gun` (0x004a0f70: the pub's swap), or adds it when `old` is not held.
    /// Returns the gun taken out.
    pub fn replace(&mut self, old: Option<WeaponId>, gun: Gun) -> Option<Gun> {
        match old.and_then(|o| self.slot_of(o)) {
            Some(i) => self.slots[i].replace(gun),
            None => {
                self.add(gun);
                None
            }
        }
    }

    /// Takes a gun out of its slot (0x004a0f30: the mission's player_remove_*); the hand moves to the previous usable
    /// slot when it was in hand (0x004a12e0).
    pub fn remove(&mut self, id: WeaponId) -> Option<Gun> {
        let i = self.slot_of(id)?;
        let g = self.slots[i].take();
        if i == self.current {
            if let Some(j) = self.step(-1) {
                self.current = j;
            }
        }
        g
    }

    /// Puts the slot holding `id` in hand (0x004a10c0 -> 0x004a10e0).
    pub fn equip(&mut self, id: WeaponId) -> bool {
        match self.slot_of(id) {
            Some(i) => {
                self.current = i;
                true
            }
            None => false,
        }
    }

    /// The next (+1) / previous (-1) usable slot from the current one, wrapping (0x004a0c10 / 0x004a0c70); None when
    /// it would be the current one.
    pub fn step(&self, dir: i32) -> Option<usize> {
        (1..GUN_SLOTS as i32).map(|k| (self.current as i32 + dir * k).rem_euclid(GUN_SLOTS as i32) as usize).find(|&i| self.slots[i].as_ref().is_some_and(Gun::usable))
    }

    /// The gun in that slot.
    pub fn at(&self, slot: usize) -> Option<&Gun> {
        self.slots.get(slot).and_then(Option::as_ref)
    }

    pub fn equip_slot(&mut self, slot: usize) {
        if self.slots.get(slot).is_some_and(Option::is_some) {
            self.current = slot;
        }
    }

    /// How many guns are carried (0x004a1610: the HUD's left / right arrows).
    pub fn gun_count(&self) -> usize {
        self.slots.iter().flatten().count()
    }

    pub fn count(&self, s: SpecialId) -> u32 {
        self.specials[s.index()]
    }

    /// Sets a special's count (0x004a14e0): above 0 it joins the special list, 0 takes it out.
    pub fn set_count(&mut self, s: SpecialId, n: u32) {
        self.specials[s.index()] = n;
        self.listed[s.index()] = n > 0;
    }

    /// Adds to a special's count up to `max` (0x004a13a0: refused when already at the maximum).
    pub fn add_special(&mut self, s: SpecialId, n: u32, max: u32) -> bool {
        let c = self.count(s);
        if c >= max {
            return false;
        }
        self.specials[s.index()] = (c + n).min(max);
        self.listed[s.index()] = true;
        true
    }

    /// The special is in the special list (given once, maybe used up since).
    pub fn lists(&self, s: SpecialId) -> bool {
        self.listed[s.index()]
    }

    /// One special used up (unless the infinite ammo cheat, Game+0x175, says otherwise: the caller's choice).
    pub fn spend(&mut self, s: SpecialId) -> bool {
        let n = &mut self.specials[s.index()];
        if *n == 0 {
            return false;
        }
        *n -= 1;
        true
    }

    /// The special in hand and its count.
    pub fn special_in_hand(&self) -> (SpecialId, u32) {
        (self.special, self.count(self.special))
    }

    /// The specials with some left, in slot order (the special list: 0x004a1640 counts them for the HUD's arrows).
    pub fn specials(&self) -> impl Iterator<Item = (SpecialId, u32)> + '_ {
        SpecialId::ALL.into_iter().map(|s| (s, self.count(s))).filter(|(_, n)| *n > 0)
    }

    /// The next (+1) / previous (-1) special with some left after the one in hand, wrapping; None when there is no
    /// other (0x004a0bb0 / 0x004a0be0 on the special list).
    pub fn next_special(&self, dir: i32) -> Option<SpecialId> {
        let n = SpecialId::ALL.len() as i32;
        let at = self.special.index() as i32;
        (1..n).map(|k| SpecialId::ALL[(at + dir * k).rem_euclid(n) as usize]).find(|s| self.count(*s) > 0)
    }

    /// Empties every slot (a level's new player, 0x00475650).
    pub fn clear(&mut self) {
        *self = Arsenal::default();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn gun(id: WeaponId) -> Gun {
        Gun::full(id, GunSpec { max_clip: 10, max_ammo: 50, ..GunSpec::default() })
    }

    /// The three primary slots: adds fill the first free one, a fourth is refused, the swap replaces in place.
    #[test]
    fn three_slots_and_the_swap() {
        let mut a = Arsenal::default();
        assert!(a.add(gun(WeaponId::Machinegun)));
        assert!(!a.add(gun(WeaponId::Machinegun)), "held already");
        assert!(a.add(gun(WeaponId::Rifle)));
        assert!(a.add(gun(WeaponId::Vindaloo)));
        assert!(!a.add(gun(WeaponId::Sharkgun)), "full");
        let out = a.replace(Some(WeaponId::Rifle), gun(WeaponId::Sharkgun));
        assert_eq!(out.map(|g| g.id), Some(WeaponId::Rifle));
        assert_eq!(a.slot_ids(), [Some(WeaponId::Machinegun), Some(WeaponId::Sharkgun), Some(WeaponId::Vindaloo)]);
    }

    /// Next / previous skip empty and unusable slots and wrap (0x004a0c10 / 0x004a0c70).
    #[test]
    fn cycling_skips_unusable() {
        let mut a = Arsenal::default();
        a.add(gun(WeaponId::Machinegun));
        let mut empty = gun(WeaponId::Snipergun);
        (empty.clip, empty.ammo) = (0, 0);
        a.add(empty);
        a.add(gun(WeaponId::Gurner));
        assert_eq!(a.step(1), Some(2));
        a.equip_slot(2);
        assert_eq!(a.step(1), Some(0));
        assert_eq!(a.step(-1), Some(0));
    }

    /// Specials cycle among the ones with some left.
    #[test]
    fn special_cycle() {
        let mut a = Arsenal::default();
        a.set_count(SpecialId::StickyBomb, 5);
        assert_eq!(a.next_special(1), None);
        a.set_count(SpecialId::BlackHole, 1);
        a.set_count(SpecialId::GuyFawkes, 1);
        assert_eq!(a.next_special(1), Some(SpecialId::GuyFawkes));
        assert_eq!(a.next_special(-1), Some(SpecialId::BlackHole));
    }

    /// The ammo add and the set: -2 fills, n splits into clip and spare.
    #[test]
    fn ammo_records() {
        let mut g = gun(WeaponId::Machinegun);
        g.set_ammo(23);
        assert_eq!((g.clip, g.ammo), (10, 13));
        assert!(g.add_ammo(40));
        assert_eq!((g.clip, g.ammo), (10, 50));
        assert!(!g.add_ammo(1));
        g.clip = 2;
        g.refill();
        assert_eq!((g.clip, g.ammo), (10, 42));
    }
}
