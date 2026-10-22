//! The shrub patrol (ShrubPatrol, vtable 0x005a7e98, behaviour shrubpatrol; lev_3_m4_bd, lev_4_m5, lev_5_m5): Roman's
//! allies, parachuted over the base by their script (mover_shrubpatrol_*: "fxlockedbone fx_parachute_2", "anim_loop
//! shrubpatrol_parachute_loop", then "ready_guard"), who then stand guard with their machine gun (shrubpatrolgun at
//! "ref_rhand", ShrubPatrol_Slot6 0x00436630) against the army: their enemy list is the AI list World+0xe7c, the grunts
//! (the reverse of the grunts', 0x00436630). They also join the player's side, World+0xe88, the grunts' enemy list
//! +0x604 (ai_04.cpp:3115-3118), so a grunt could take one as a target; but every level with shrub patrols (lev_3_m4_bd,
//! lev_4_m5, lev_5_m5) sets the mission's ai_target_tag 1, under which AI_FindTarget 0x00426920 takes only objects of
//! that tag, and the patrols carry none: nothing in the army aims at them, and their max_health 10000 is never
//! touched (no damage taken here).
//!
//! The guard: the nearest live grunt within fire_range_min..max (35..250) is the target; it turns to it and fires the
//! fire anims (shrubpatrol_fire_*: "fire" notes, GenericAIObject_HandleEvent 0x0043d180) in bursts by its fire
//! counters (fire_num_rounds 30..40 a clip, fire_num_bursts 5..10, burst_delay 0.25..1, clip_delay 1..2:
//! 0x00427600 / 0x004292f0, simplified: rounds spread evenly over the bursts), shrubpatrol_mgun_bullet with its
//! inaccuracy (fire_inaccuracy_xy / _height) at the target's chest. No target: shrubpatrol_stand.
//!
//! Its script starts through icobject.rs (mode trigger_ic; hidden until then).

use bevy::prelude::*;

#[derive(Component, Debug)]
pub struct ShrubPatrol {
    guarding: bool,
    target: Option<Entity>,
    range: (f32, f32),
    inaccuracy: (f32, f32),
    rounds: (i64, i64),
    bursts: (i64, i64),
    burst_delay: (f32, f32),
    clip_delay: (f32, f32),
    /// rounds left in the clip, in the burst, and the pause before the next
    left: i64,
    burst_left: i64,
    pause: f32,
    gun: Option<Entity>,
    pub shots: u32,
}

pub struct ShrubPatrolPlugin;

impl Plugin for ShrubPatrolPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, (setup, ready, guard, fire, log).chain().run_if(crate::playing))
            .add_systems(PostUpdate, friendly_fire.run_if(crate::playing));
    }
}

fn setup(
    mut commands: Commands,
    data: Res<crate::gamedata::GameData>,
    q: Query<(Entity, &Name, &crate::skin::Animator), (With<crate::level::InstanceId>, Without<ShrubPatrol>)>,
    mut kit: crate::army::ModelKit,
    mut seen: crate::levelreset::PerLevel<std::collections::HashSet<Entity>>,
    mut rng: ResMut<crate::fx::FxStats>,
) {
    let seen = seen.get();
    for (e, name, a) in &q {
        if !seen.insert(e) {
            continue;
        }
        let p = data.template_props(name.as_str());
        if p.s("behavior") != Some("shrubpatrol") {
            continue;
        }
        // the gun in the right hand (0x004a0ea0 "ref_rhand")
        let hand = a.skeleton.index.get("ref_rhand").and_then(|&i| a.joints.get(i).copied());
        let gun = data.templates.get("shrubpatrolgun").and_then(|t| t.model.clone()).zip(hand).and_then(|(m, h)| {
            let g = kit.spawn(&mut commands, &m, Transform::IDENTITY, true)?;
            commands.entity(h).add_child(g);
            Some(g)
        });
        let pair = |a: &str, b: &str, d: (f32, f32)| (p.f(a, d.0), p.f(b, d.1));
        let ipair = |a: &str, b: &str, d: (i64, i64)| (p.i(a, d.0), p.i(b, d.1));
        commands.entity(e).insert(ShrubPatrol {
            guarding: false,
            target: None,
            range: pair("fire_range_min", "fire_range_max", (35.0, 250.0)),
            inaccuracy: (p.f("fire_inaccuracy_xy", 1.0), p.f("fire_inaccuracy_height", 0.5)),
            rounds: ipair("fire_num_rounds_min", "fire_num_rounds_max", (30, 40)),
            bursts: ipair("fire_num_bursts_min", "fire_num_bursts_max", (5, 10)),
            burst_delay: pair("fire_burst_delay_min", "fire_burst_delay_max", (0.25, 1.0)),
            clip_delay: pair("fire_clip_delay_min", "fire_clip_delay_max", (1.0, 2.0)),
            left: 0,
            burst_left: 0,
            pause: 0.0,
            gun,
            shots: 0,
        });
        // its voice: SP1 or SP2, an even roll at its init (Ai_InitGeneric 0x004247f0, ai_01.cpp:2438: (rand x 100) >> 15
        // < 50 sets +0x484 0x200000, the "second voice" that says the SP1500xx lines: set 0)
        commands.entity(e).insert(crate::voices::ShrubVoice(if rng.rand() < 0.5 { 0 } else { 1 }));
    }
}

/// "ready_guard" ends the parachute script (GenericAI: vt+0x1ac(1), the guard state): it stands where it landed.
fn ready(mut commands: Commands, mut notes: MessageReader<crate::mover::MoverNote>, mut q: Query<(&mut ShrubPatrol, &mut crate::skin::Animator)>) {
    for n in notes.read() {
        if !n.text.eq_ignore_ascii_case("ready_guard") {
            continue;
        }
        let Ok((mut s, mut a)) = q.get_mut(n.entity) else { continue };
        s.guarding = true;
        commands.entity(n.entity).remove::<crate::mover::Mover>();
        a.play(0, "shrubpatrol_stand", true, crate::skin::Mask::All);
        info!("EVENT shrubpatrol: {:?} landed, on guard", n.entity);
    }
}

fn roll(rng: &mut crate::fx::FxStats, (lo, hi): (i64, i64)) -> i64 {
    lo + ((rng.rand() * (hi - lo + 1) as f32) as i64).min(hi - lo)
}

/// The guard: a target, the facing, the burst counters and the fire anims.
fn guard(
    time: Res<Time>,
    mut q: Query<(Entity, &mut ShrubPatrol, &GlobalTransform, &mut Transform, &mut crate::skin::Animator)>,
    grunts: Query<(Entity, &crate::ai::Grunt, &GlobalTransform), Without<ShrubPatrol>>,
    mut rng: ResMut<crate::fx::FxStats>,
    mut cues: MessageWriter<crate::voices::VoiceCue>,
) {
    let dt = time.delta_secs();
    for (e, mut s, gt, mut tr, mut a) in &mut q {
        if !s.guarding {
            continue;
        }
        let me = gt.translation();
        let in_range = |p: Vec3| {
            let d = (p - me).with_y(0.0).length();
            d >= s.range.0 && d <= s.range.1
        };
        // keep a live target in range, else the nearest
        let keep = s.target.and_then(|t| grunts.get(t).ok()).filter(|(_, g, p)| g.mind != crate::ai::Mind::Dead && in_range(p.translation()));
        if keep.is_none() {
            s.target = grunts
                .iter()
                .filter(|(_, g, p)| g.mind != crate::ai::Mind::Dead && in_range(p.translation()))
                .min_by(|x, y| x.2.translation().distance(me).total_cmp(&y.2.translation().distance(me)))
                .map(|(e, ..)| e);
        }
        let Some(tp) = s.target.and_then(|t| grunts.get(t).ok()).map(|(_, _, p)| p.translation()) else {
            a.play(0, "shrubpatrol_stand", true, crate::skin::Mask::All);
            continue;
        };
        let to = (tp - me).with_y(0.0);
        if to.length() > 1.0 {
            tr.rotation = Quat::from_rotation_y(to.x.atan2(to.z)) * crate::level::game_to_engine();
        }
        s.pause -= dt;
        if s.pause > 0.0 {
            a.play(0, "shrubpatrol_stand", true, crate::skin::Mask::All);
            continue;
        }
        if s.left <= 0 {
            s.left = roll(&mut rng, s.rounds);
        }
        if s.burst_left <= 0 {
            let bursts = roll(&mut rng, s.bursts).max(1);
            s.burst_left = (s.left + bursts - 1) / bursts;
            // the fire anim starts with a target: the Fight cue (ShrubPatrol +0x13c: its taunt 0x0043a310, voices.rs)
            cues.write(crate::voices::VoiceCue { who: e, cue: crate::voices::Cue::ShrubTaunt });
        }
        // the corner by the target's height (low: the bottom corners)
        let corner = if tp.y < me.y { "shrubpatrol_fire_bl" } else { "shrubpatrol_fire_tl" };
        a.play(0, corner, true, crate::skin::Mask::All);
    }
}

/// The fire anims' "fire" notes: a round at the target (0x00428580-style aim error), the counters down.
#[allow(clippy::too_many_arguments)]
fn fire(
    mut commands: Commands,
    mut notes: MessageReader<crate::skin::AnimNote>,
    mut q: Query<(Entity, &mut ShrubPatrol, &GlobalTransform)>,
    targets: Query<&GlobalTransform, Without<ShrubPatrol>>,
    data: Res<crate::gamedata::GameData>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut rng: ResMut<crate::fx::FxStats>,
) {
    let bullet = data.props("p_shrubpatrol_mgun_bullet");
    for n in notes.read() {
        if n.text.trim() != "fire" {
            continue;
        }
        let Ok((e, mut s, gt)) = q.get_mut(n.entity) else { continue };
        let (Some(t), true) = (s.target, s.guarding && s.pause <= 0.0) else { continue };
        let Ok(tp) = targets.get(t).map(|p| p.translation() + Vec3::Y * 6.0) else { continue };
        let from = s.gun.and_then(|g| targets.get(g).ok()).map(|g| g.translation()).unwrap_or(gt.translation() + Vec3::Y * 10.0);
        let (xy, h) = s.inaccuracy;
        let aim = tp + Vec3::new(rng.range(-xy, xy), rng.range(-h, h), rng.range(-xy, xy));
        crate::player::spawn_projectile(&mut commands, &mut fx, &bullet, from, (aim - from).normalize_or(Vec3::Z), true, Some(e));
        s.shots += 1;
        s.left -= 1;
        s.burst_left -= 1;
        if s.left <= 0 {
            s.pause = rng.range(s.clip_delay.0, s.clip_delay.1);
        } else if s.burst_left <= 0 {
            s.pause = rng.range(s.burst_delay.0, s.burst_delay.1);
        }
    }
}

/// The shrub patrol's body (guess: a sphere of this radius round its chest, 6 above its feet; its collision model is not
/// read).
const BODY_RADIUS: f32 = 4.0;

/// The player's shots that reach a shrub patrol stop on its body, which takes no damage from him (the damage filter,
/// 0x00430340: the same side), and it calls out (its friendly-fire slot +0x150, 0x0043a480: voices.rs, vo_delay).
fn friendly_fire(
    mut commands: Commands,
    time: Res<Time>,
    shots: Query<(Entity, &crate::player::Projectile, &Transform)>,
    roman: Query<Entity, With<crate::player::Roman>>,
    shrubs: Query<(Entity, &GlobalTransform), With<ShrubPatrol>>,
    mut cues: MessageWriter<crate::voices::VoiceCue>,
    mut hits: Local<u32>,
    state: Option<ResMut<crate::playtest::PlayState>>,
) {
    let Ok(player) = roman.single() else { return };
    let dt = time.delta_secs();
    for (e, p, t) in &shots {
        if p.owner != Some(player) {
            continue;
        }
        let (to, from) = (t.translation, t.translation - p.vel * dt);
        for (s, gt) in &shrubs {
            if hits_body(from, to, gt.translation()) {
                commands.entity(e).despawn();
                cues.write(crate::voices::VoiceCue { who: s, cue: crate::voices::Cue::ShrubFriendlyFire });
                *hits += 1;
                break;
            }
        }
    }
    if let Some(mut st) = state {
        st.0.insert("shrub_hits".into(), (*hits).into());
    }
}

/// Whether a shot's step from `from` to `to` passes through the body of a shrub patrol standing at `feet`.
fn hits_body(from: Vec3, to: Vec3, feet: Vec3) -> bool {
    let c = feet + Vec3::Y * 6.0;
    let seg = to - from;
    let u = ((c - from).dot(seg) / seg.length_squared().max(1e-6)).clamp(0.0, 1.0);
    (from + seg * u).distance(c) < BODY_RADIUS
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_shot_through_the_chest_hits_one_past_the_shoulder_misses() {
        let feet = Vec3::new(0.0, 0.0, 0.0);
        assert!(hits_body(Vec3::new(-10.0, 6.0, 0.0), Vec3::new(10.0, 6.0, 0.0), feet));
        assert!(!hits_body(Vec3::new(-10.0, 6.0, 5.0), Vec3::new(10.0, 6.0, 5.0), feet));
        // the step ends short of it: no hit yet
        assert!(!hits_body(Vec3::new(-30.0, 6.0, 0.0), Vec3::new(-10.0, 6.0, 0.0), feet));
    }
}

fn log(q: Query<&ShrubPatrol>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    if q.is_empty() {
        return;
    }
    let guarding = q.iter().filter(|s| s.guarding).count();
    let shots: u32 = q.iter().map(|s| s.shots).sum();
    state.0.insert("shrubpatrol".into(), serde_json::json!({ "count": q.iter().count(), "guarding": guarding, "shots": shots }));
}
