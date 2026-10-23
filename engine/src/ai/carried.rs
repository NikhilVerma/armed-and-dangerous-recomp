//! The companions knocked out of their update: stunned or carried (MolmerObject_Slot47 0x00485910 modes 5 and 10;
//! the damage message's flags 0x20 and 0x800 in GenericAIObject_Slot52 0x00430340; archive/src/ai/ai_04.cpp, ai_06.cpp,
//! ai_08.cpp).
//!
//! - **Stun** (record flag 0x20: the Topsy Turvy bomb's area damage, 0x004a1aa0 -> AreaDamage_Apply 0x0047f020 with
//!   topsy_turvy_radius): asked first (vt+0x1c0, Q111Object_Slot112 0x00486c70: refused on the tower task, flag
//!   0x40000); a teammate (vt+0xe4) gets +0x878 = 0.001..0.5 s and +0x87c = topsy_turvy_duration (Game+0xc7c, 5 s); in
//!   an IC anim or a close kill (state 0xc) it is let go at once (vt+0x1b0), his hang (0x71) starting there.
//! - **Pull** (record flag 0x800, the holder at +0x30): asked first (vt+0x254, Slot149 0x00486c80, the same answer); a
//!   holder replaces the old one (and nothing else when there was one); a teammate gets +0x878 = 0.5..1.5 s and stays
//!   where he is (the knock toward the holder is the enemies'). Nothing in the remaster pulls yet (the twiglets'
//!   leap / attach is unreached in the shipped game, twiglet.rs): the message is here for it.
//! - **Mode 5** (entered from any mode when +0x878 > 0, the mode kept in +0xb58): +0x87c counts down (twice a frame
//!   while +0x878 does); when +0x878 runs out the companion hangs: anim 0x71 (vt+0x1b0 0x0042fe50: a teammate's state
//!   0xeb), then 0x72 looped at each end (Q: q111_hanging both; Jonesy: molmer_hang_into, molmer_hanging). With
//!   +0x87c at -1 (K 0x005bcb5c) or below and no live holder: Jonesy plays 0x71 once more (mode 10, its end awaited),
//!   Q (the player's 0x00475d40) gets up at once; then the stand set and the mode he left, his target lost (0x0042bc60).
//!   Team orders wait meanwhile (0x00485120: either of the pair in mode 5 / 10).
//! Guess: before the hang (+0x878 > 0, at most 1.5 s) the companion stands where he is (the original runs the generic
//! state he was in).

use super::Companion;
use crate::fx::FxStats;
use crate::levelreset::LevelAppExt;
use bevy::prelude::*;

/// What knocked a companion.
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum KnockKind {
    /// flag 0x20 (the Topsy Turvy bomb), for `duration` seconds (topsy_turvy_duration)
    Stun { duration: f32 },
    /// flag 0x800: held by `holder` while it lives
    Pull { holder: Entity },
}

/// A knock on a companion (the damage message with flag 0x20 / 0x800).
#[derive(Message, Clone, Copy, Debug)]
pub struct Knock {
    pub companion: Entity,
    pub kind: KnockKind,
}

/// The +0x87c line the stun waits to pass (K 0x005bcb5c).
const UP_AT: f32 = -1.0;

/// A companion in modes 5 / 10.
#[derive(Component, Debug)]
pub struct Knocked {
    /// +0x878: time to the hang
    to_hang: f32,
    /// +0x87c: the stun's clock
    left: f32,
    /// +0x870: the holder of a pull
    holder: Option<Entity>,
    /// the hang anims: which (0x71 into, 0x72 loop) and time into it
    hang: Option<(u8, f32)>,
    /// mode 10: the last 0x71 before getting up (Jonesy)
    getting_up: bool,
}

#[derive(Resource, Default)]
pub struct KnockStats {
    pub knocked: u32,
    pub recovered: u32,
}

pub struct CarriedPlugin;

impl Plugin for CarriedPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<Knock>()
            .init_level_resource::<KnockStats>()
            .add_systems(Update, (topsy_knocks, knock, update, log).chain().run_if(crate::playing));
    }
}

/// The hang anims of a companion (his table's 0x71 / 0x72).
fn hang_anim(q: bool, id: u8) -> &'static str {
    match (q, id) {
        (true, _) => "q111_hanging",
        (false, 0x71) => "molmer_hang_into",
        (false, _) => "molmer_hanging",
    }
}

/// The Topsy Turvy bomb's area damage reaches the companions too (0x0047f020 over the AI list with flag 0x20): those
/// within topsy_turvy_radius of Roman when it goes off (topsy.rs throws the grunts at the same moment).
fn topsy_knocks(
    topsy: Res<crate::topsy::Topsy>,
    mut was: Local<bool>,
    data: Res<crate::gamedata::GameData>,
    roman: Query<&GlobalTransform, With<crate::player::Roman>>,
    companions: Query<(Entity, &GlobalTransform), With<Companion>>,
    mut out: MessageWriter<Knock>,
) {
    let fired = topsy.phase == crate::topsy::Phase::Hang;
    if fired && !*was {
        let g = data.props("p_globalsettings");
        let radius = g.f("topsy_turvy_radius", 100.0);
        if let Ok(rt) = roman.single() {
            for (e, ct) in &companions {
                // (the same reach as the grunts': within the radius of the bounds' surface, topsy.rs)
                if ct.translation().distance(rt.translation()) - 4.66 <= radius {
                    out.write(Knock { companion: e, kind: KnockKind::Stun { duration: topsy.duration } });
                }
            }
        }
    }
    *was = fired;
}

/// The damage handler's 0x20 / 0x800 branches for a companion.
#[allow(clippy::type_complexity)]
fn knock(
    mut commands: Commands,
    mut knocks: MessageReader<Knock>,
    mut q: Query<(&mut Companion, Option<&mut Knocked>, Has<super::tower::OnTask>, Option<&super::close_kill::Killing>)>,
    mut rng: ResMut<FxStats>,
    mut stats: ResMut<KnockStats>,
) {
    for k in knocks.read() {
        let Ok((mut c, knocked, on_task, killing)) = q.get_mut(k.companion) else { continue };
        // Slot112 / Slot149: not on the tower task; not dead
        if on_task || c.dead.is_some() {
            continue;
        }
        let mut state = knocked.map(|k| (k.left, k.holder, k.hang, k.getting_up)).unwrap_or((0.0, None, None, false));
        let to_hang = match k.kind {
            KnockKind::Stun { duration } => {
                state.0 = duration;
                rng.range(0.001, 0.5)
            }
            KnockKind::Pull { holder } => {
                let had = state.1.replace(holder).is_some();
                if had {
                    commands.entity(k.companion).insert(Knocked { to_hang: 0.0, left: state.0, holder: state.1, hang: state.2, getting_up: state.3 });
                    continue;
                }
                rng.range(0.5, 1.5)
            }
        };
        // in an IC anim or a close kill (state 0xc): let go of it and hang at once (vt+0x1b0 0x0042fe50: a teammate's anim
        // 0x71, state 0xeb, in the same call that set +0x878; ai_04.cpp:314-342)
        let mut hang = state.2;
        if killing.is_some() {
            commands.entity(k.companion).remove::<(super::close_kill::Killing, crate::mover::Mover)>();
            c.ic = false;
            hang = Some((0x71, 0.0));
        }
        c.moving = false;
        c.firing = false;
        commands.entity(k.companion).insert(Knocked { to_hang, left: state.0, holder: state.1, hang, getting_up: false });
        stats.knocked += 1;
        info!("EVENT companions: {} knocked ({:?})", if c.q { "Q" } else { "Jonesy" }, k.kind);
    }
}

/// Modes 5 / 10.
fn update(
    mut commands: Commands,
    time: Res<Time>,
    mut lib: ResMut<crate::skin::AnimLibrary>,
    mut q: Query<(Entity, &mut Knocked, &mut Companion)>,
    alive: Query<&crate::fx::Health>,
    mut stats: ResMut<KnockStats>,
) {
    let dt = time.delta_secs();
    for (e, mut k, mut c) in &mut q {
        if c.dead.is_some() {
            c.pose = None;
            commands.entity(e).remove::<Knocked>();
            continue;
        }
        c.moving = false;
        c.firing = false;
        // the hang anims: each end of 0x71 (or of the loop) plays 0x72 (state 0xeb)
        if let Some((id, t)) = k.hang {
            let len = lib.anim(hang_anim(c.q, id)).map(|a| a.duration).unwrap_or(1.0);
            let t = t + dt;
            if t >= len && k.getting_up {
                // mode 10's end: up
                c.recover();
                commands.entity(e).remove::<Knocked>();
                stats.recovered += 1;
                info!("EVENT companions: {} gets up", if c.q { "Q" } else { "Jonesy" });
                continue;
            }
            k.hang = Some(if t >= len { (0x72, 0.0) } else { (id, t) });
        }
        if k.getting_up {
            c.pose = k.hang.map(|(id, _)| (hang_anim(c.q, id), false));
            continue;
        }
        k.left -= dt;
        if k.to_hang > 0.0 {
            k.to_hang -= dt;
            if k.to_hang <= 0.0 {
                // vt+0x1b0: anim 0x71 (one already playing goes on, GenericAI_PlayAnim with its own id)
                if !k.hang.is_some_and(|(id, _)| id == 0x71) {
                    k.hang = Some((0x71, 0.0));
                }
            } else {
                k.left -= dt;
            }
        }
        c.pose = k.hang.map(|(id, _)| (hang_anim(c.q, id), id == 0x72));
        let held = k.holder.is_some_and(|h| alive.get(h).is_ok_and(|h| h.0 > 0.0));
        if k.left > UP_AT || held {
            continue;
        }
        if c.q {
            // the player's Q gets up at once
            c.recover();
            commands.entity(e).remove::<Knocked>();
            stats.recovered += 1;
            info!("EVENT companions: Q gets up");
        } else {
            // mode 10: 0x71 once more, then up
            k.getting_up = true;
            k.hang = Some((0x71, 0.0));
            c.pose = Some((hang_anim(false, 0x71), false));
        }
    }
}

/// The playtest state's `knocked`: companions knocked and recovered, and per companion whether he hangs now.
fn log(stats: Res<KnockStats>, q: Query<(&Companion, Option<&Knocked>)>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    let mut v = serde_json::json!({"knocked": stats.knocked, "recovered": stats.recovered});
    for (c, k) in &q {
        let s = match k {
            None => "free",
            Some(k) if k.getting_up => "getting_up",
            Some(k) if k.hang.is_some() => "hanging",
            Some(_) => "stunned",
        };
        v[if c.q { "q" } else { "jonesy" }] = s.into();
    }
    state.0.insert("knocked".into(), v);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hang_anims_by_companion() {
        assert_eq!(hang_anim(true, 0x71), "q111_hanging");
        assert_eq!(hang_anim(true, 0x72), "q111_hanging");
        assert_eq!(hang_anim(false, 0x71), "molmer_hang_into");
        assert_eq!(hang_anim(false, 0x72), "molmer_hanging");
    }
}
