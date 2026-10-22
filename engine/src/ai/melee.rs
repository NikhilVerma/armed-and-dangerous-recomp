//! The AI's close attack (0x00428160, state 0xb; src/ai/ai_02.cpp, ai_06.cpp:889-925, ai_05.cpp 0x0043eae0). The
//! companions' hand-to-hand takedown (0x00427e80, states 0xc..0xf) is ai/close_kill.rs; this module only has the
//! grunts' own swing. docs/subsystems/grunt-ai.md section 15.
//!
//! A grunt, an archer or a Q112 (vt+0x1dc: GruntObject, BowGruntObject, Q112Object) whose target is within his
//! hand-to-hand reach m_h2hMin (+0x5fc: 20, the Q112 25; GenericAIObject_Construct game_02.cpp:1638 / Q112Object_Slot6
//! ai_04.cpp:2369) at a burst's end with more of the clip left or at the clip's end (the fire cycle 0x00428980,
//! ai_02.cpp:1919 / 1986 -> 0x00428160) swings at it: anim 0x5f / 0x60 (even odds: grunt_bow_hand_to_hand / _2; the
//! Q112's q112_gun_hit for both) looped, facing it. Each "hit" note of the anim (GenericAIObject_HandleEvent 0x0043d180
//! -> 0x0043eae0 with radius +0x600) hurts the target within m_h2hMax (+0x600: 10, the Q112 35) across: 50 with an
//! impact of 6500, the Q112 (an AI whose name holds "q112") 100 and 13000, along the line to it lifted 0.7..0.9 (the
//! knock: Roman's hit handler, player.rs `Roman::knock`). At each loop's end a target m_h2hMax away or more ends it: the
//! fire state (the engine's; in a trench the duck).

use super::Kind;
use bevy::prelude::*;

/// A grunt's hand-to-hand reach (vt+0x1dc and +0x5fc / +0x600): (swing within, hits within).
pub fn h2h(kind: Kind) -> Option<(f32, f32)> {
    match kind {
        Kind::Grunt | Kind::Bow => Some((20.0, 10.0)),
        Kind::Q112 => Some((25.0, 35.0)),
        _ => None,
    }
}

/// 0x00428160: swing at a target within reach (3D, strictly): the anim id (0x5f / 0x60 at even odds: one rand, drawn
/// only when in reach).
pub fn close_attack(kind: Kind, me: Vec3, target: Vec3, rand: impl FnOnce() -> f32) -> Option<u32> {
    let (min, _) = h2h(kind)?;
    (me.distance_squared(target) < min * min).then(|| if rand() < 0.5 { 0x5f } else { 0x60 })
}

/// 0x0043eae0's damage and impact by the attacker's name: "q112" 100 / 13000, else 50 / 6500.
pub fn hit_of(kind: Kind) -> (f32, f32) {
    if kind == Kind::Q112 { (100.0, 13000.0) } else { (50.0, 6500.0) }
}

/// The swing anim's "hit" notes (the scene's export: grunt_bow_hand_to_hand 0.367; _2 0.367 and 0.9; q112_gun_hit
/// 0.667); these defaults when the anim is not in the scene.
pub fn hit_times(lib: &mut crate::skin::AnimLibrary, anim: &str, id: u32) -> Vec<f32> {
    let from_anim: Vec<f32> = lib
        .anim(anim)
        .map(|a| a.notes.iter().filter(|(_, n)| n.split_whitespace().next().is_some_and(|w| w.eq_ignore_ascii_case("hit"))).map(|(t, _)| *t).collect())
        .unwrap_or_default();
    if !from_anim.is_empty() {
        return from_anim;
    }
    if id == 0x60 { vec![0.367, 0.9] } else { vec![0.367] }
}

/// The knock's direction (0x0043eae0: AI -> target, its height replaced by 0.7..0.9 before normalising; `r` in [0, 1);
/// engine axes, y up).
pub fn knock_dir(from: Vec3, to: Vec3, r: f32) -> Vec3 {
    let flat = (to - from).with_y(0.0);
    Vec3::new(flat.x, 0.7 + 0.2 * r, flat.z).normalize_or(Vec3::Y)
}

/// One "hit" of a close attack, applied by `close_hits`.
#[derive(Message, Clone, Copy, Debug)]
pub struct CloseHit {
    pub by: Entity,
    pub target: Entity,
    pub damage: f32,
    pub impact: f32,
    pub dir: Vec3,
}

/// Swings started and hits landed (playtest log `melee`).
#[derive(Resource, Default, Debug)]
pub struct CloseStats {
    pub swings: u32,
    pub hits: u32,
}

/// The hits (the damage record 0x40000 with the AI as source): Roman through his hit handler (the damage, the hit anim
/// and the knock), a companion (his reaction and lines are ai.rs companion_damage's, the attacker offered as his
/// target), anything else with health.
#[allow(clippy::type_complexity)]
pub fn close_hits(
    mut hits: MessageReader<CloseHit>,
    mut health: Query<(&mut crate::fx::Health, Option<&mut crate::player::Roman>, Option<&mut super::Companion>, Has<crate::mission::HitsIgnored>)>,
    mut stats: ResMut<CloseStats>,
) {
    for h in hits.read() {
        let Ok((mut hp, roman, companion, ignored)) = health.get_mut(h.target) else { continue };
        // (Roman's hit handler ignores it after the mission's pre-success: mission.rs HitsIgnored)
        if hp.0 <= 0.0 || ignored {
            continue;
        }
        hp.0 -= h.damage;
        stats.hits += 1;
        if let Some(mut r) = roman {
            r.knock(h.damage, h.impact, h.dir);
        }
        if let Some(mut c) = companion {
            c.attacker = Some(h.by);
        }
    }
}

/// The playtest state's `melee`: the companions' takedowns (ai/close_kill.rs: tries, started = grabbed, paired = the
/// IC anims begun, kills) and the AI's swings and hits.
pub fn log(close: Res<CloseStats>, kills: Option<Res<super::close_kill::CloseKills>>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    let k = kills.map(|k| (k.tries, k.grabbed, k.started, k.kills)).unwrap_or_default();
    state.0.insert(
        "melee".into(),
        serde_json::json!({"tries": k.0, "started": k.1, "paired": k.2, "kills": k.3, "swings": close.swings, "close_hits": close.hits}),
    );
}

#[cfg(test)]
mod tests {
    use super::*;

    /// 0x00428160: within m_h2hMin (3D, strictly) by class; no rand drawn out of reach.
    #[test]
    fn the_swing_needs_the_reach() {
        let mut drawn = false;
        assert_eq!(close_attack(Kind::Grunt, Vec3::ZERO, Vec3::new(25.0, 0.0, 0.0), || { drawn = true; 0.1 }), None);
        assert!(!drawn);
        assert_eq!(close_attack(Kind::Q112, Vec3::ZERO, Vec3::new(20.0, 10.0, 0.0), || 0.1), Some(0x5f));
        assert_eq!(close_attack(Kind::Bow, Vec3::ZERO, Vec3::new(5.0, 0.0, 0.0), || 0.7), Some(0x60));
        assert_eq!(close_attack(Kind::Captain, Vec3::ZERO, Vec3::new(5.0, 0.0, 0.0), || 0.1), None);
        assert_eq!(hit_of(Kind::Q112), (100.0, 13000.0));
        assert_eq!(hit_of(Kind::Bow), (50.0, 6500.0));
    }
}
