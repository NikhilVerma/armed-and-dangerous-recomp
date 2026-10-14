//! Markers and the alarm chain (docs/subsystems/mission.md "lev_1_m1 alarm chain").
//!
//! - Markers (template `marker`): a grunt in mode trigger_marker, once awake (its trigger, a target found, or hurt:
//!   ai_01.cpp:2498-2531), runs to a marker of its marker_tag_min..max (AI_FollowMarkerSpline 0x0042d1b0; ai.rs
//!   Mind::ToMarker). On arrival (Marker_ProcessArrival 0x0044ba30): with an alarm_tag, the object of that tag joins the
//!   siren list World+0xef0 once: fx_siren on it and the runner's Help bark (vt+0x264, 0x00438f90); then the action:
//!   hide (FindHidingSpot 0x0042a7b0; no cover slots here: its fallback, a wander 60..100), trigger_charge (at the
//!   player, 0x0042ce70), else the scatter wander scatter_min..max (0x0042d430; ready_roaming / follow are not parsed
//!   on PC).
//! - The alarm controller (0x00404e50, ai_01.cpp:468-660) while a siren is listed: every alarm_min..max_delay (2..4 s)
//!   one troop while fewer than alarm_generator_max_troops (10) of its troops live, from the generator nearest the
//!   player that he does not see (180 degree cone and a sight ray) and is not one of the last 3 used (unless only those
//!   remain); the troop type weighted by the generator's num_regular / num_captain. Troops hunt the player (their tags
//!   12..14 lead to trigger_charge markers or a wander: guess, the spline choice is not traced).
//! - A destroyed siren leaves the list (guess: Object_Remove unlinks it; unverified) and its loop stops.

use crate::level::{InstanceId, game_to_engine};
use bevy::prelude::*;
use std::collections::{HashMap, VecDeque};

pub struct Marker {
    pub id: u32,
    pub pos: Vec3,
    pub tag: i64,
    pub alarm_tag: i64,
    pub action: String,
    pub scatter: (f32, f32),
}

struct Generator {
    id: u32,
    pos: Vec3,
    /// (template, weight): num_regular -> grunt_regular, num_captain -> captain
    weights: Vec<(&'static str, f32)>,
}

#[derive(Resource, Default)]
pub struct Markers {
    pub markers: Vec<Marker>,
    generators: Vec<Generator>,
    /// instance ids by their `tag` property (alarm targets)
    tagged: HashMap<i64, Vec<u32>>,
    sirens: Vec<Entity>,
    timer: f32,
    recent: VecDeque<u32>,
    max_troops: usize,
    delay: (f32, f32),
}

impl Markers {
    pub fn load(dir: &std::path::Path) -> Self {
        let mut m = Markers { max_troops: 10, delay: (2.0, 4.0), ..default() };
        let Ok(text) = std::fs::read_to_string(dir.join("instance_props.json")) else { return m };
        let Ok(list) = serde_json::from_str::<Vec<serde_json::Value>>(&text) else { return m };
        let num = |p: &serde_json::Value, k: &str| p.get(k).and_then(|v| v.as_f64()).unwrap_or(0.0);
        for v in &list {
            let id = v.get("id").and_then(|x| x.as_u64()).unwrap_or(0) as u32;
            let tpl = v.get("template").and_then(|x| x.as_str()).unwrap_or("");
            let pos = v.get("pos").and_then(|p| p.as_array()).map(|a| Vec3::new(a[0].as_f64().unwrap_or(0.0) as f32, a[1].as_f64().unwrap_or(0.0) as f32, a[2].as_f64().unwrap_or(0.0) as f32)).unwrap_or_default();
            let pos = game_to_engine() * pos;
            let p = v.get("props").cloned().unwrap_or_default();
            let tag = num(&p, "tag") as i64;
            if tag != 0 {
                m.tagged.entry(tag).or_default().push(id);
            }
            match tpl {
                "marker" => m.markers.push(Marker {
                    id,
                    pos,
                    tag: num(&p, "marker_tag") as i64,
                    alarm_tag: num(&p, "alarm_tag") as i64,
                    action: p.get("action").and_then(|x| x.as_str()).unwrap_or("").to_owned(),
                    scatter: (num(&p, "scatter_min") as f32, num(&p, "scatter_max") as f32),
                }),
                "grunt_generator" => {
                    let mut w = Vec::new();
                    if num(&p, "num_regular") > 0.0 {
                        w.push(("grunt_regular", num(&p, "num_regular") as f32));
                    }
                    if num(&p, "num_captain") > 0.0 {
                        w.push(("captain", num(&p, "num_captain") as f32));
                    }
                    if !w.is_empty() {
                        m.generators.push(Generator { id, pos, weights: w });
                    }
                }
                _ => {}
            }
        }
        // the mission's alarm settings (Game+0x26d4, +0xb3c / +0xb40)
        if let Ok(mt) = std::fs::read_to_string(dir.join("mission.json")).map(|t| serde_json::from_str::<serde_json::Value>(&t).unwrap_or_default()) {
            let s = |k: &str| mt.pointer(&format!("/mission/resolved/{k}")).and_then(|v| v.as_f64());
            if let Some(n) = s("alarm_generator_max_troops") {
                m.max_troops = n as usize;
            }
            if let (Some(a), Some(b)) = (s("alarm_min_delay"), s("alarm_max_delay")) {
                m.delay = (a as f32, b as f32);
            }
        }
        info!("alarm: {} markers, {} generators, max troops {}", m.markers.len(), m.generators.len(), m.max_troops);
        m
    }

    /// A marker of a tag in min..=max (the first found: lev_1_m1 has one per tag).
    pub fn find(&self, tags: (i64, i64), pick: f32) -> Option<usize> {
        let c: Vec<usize> = self.markers.iter().enumerate().filter(|(_, m)| m.tag >= tags.0 && m.tag <= tags.1).map(|(i, _)| i).collect();
        if c.is_empty() { None } else { Some(c[((pick * c.len() as f32) as usize).min(c.len() - 1)]) }
    }
}

/// A grunt reached its marker.
#[derive(Message, Clone)]
pub struct MarkerArrival {
    pub marker: usize,
    pub at: Vec3,
    pub voice_set: u8,
}

/// A troop of the alarm controller (counted toward its cap while alive).
#[derive(Component)]
pub struct AlarmTroop;

pub struct AlarmPlugin;

impl Plugin for AlarmPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Markers>().add_message::<MarkerArrival>().add_systems(Update, (arrivals, sirens_gone, controller).chain().run_if(crate::playing));
    }
}

/// Arrival at an alarm marker: its alarm object (by tag) joins the siren list once, with fx_siren and the Help bark.
fn arrivals(
    mut m: ResMut<Markers>,
    mut ev: MessageReader<MarkerArrival>,
    ids: Query<(Entity, &InstanceId)>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
) {
    for a in ev.read() {
        let alarm = m.markers[a.marker].alarm_tag;
        if alarm == 0 {
            continue;
        }
        let tagged = m.tagged.get(&alarm).cloned().unwrap_or_default();
        for (e, i) in &ids {
            // (a destroyed alarm object without a husk is gone)
            if !tagged.contains(&i.0) || m.sirens.contains(&e) {
                continue;
            }
            m.sirens.push(e);
            fx.write(crate::fx::StartFx { name: "fx_siren".into(), at: Vec3::ZERO, rotation: Quat::IDENTITY, follow: Some(e), position_only: false });
            sounds.write(crate::audio::PlaySound::at(crate::ai::voice("Help", a.voice_set), a.at));
            info!("EVENT alarm: siren {} raised (marker {})", i.0, m.markers[a.marker].id);
        }
    }
}

/// A destroyed siren leaves the list and its loop stops.
fn sirens_gone(mut m: ResMut<Markers>, mut died: MessageReader<crate::fx::Died>, mut stop: MessageWriter<crate::audio::StopSound>) {
    for d in died.read() {
        if let Some(i) = m.sirens.iter().position(|&e| e == d.0) {
            m.sirens.remove(i);
            stop.write(crate::audio::StopSound("alm_airraid_lp".into()));
            info!("EVENT alarm: siren destroyed");
        }
    }
}

/// The alarm controller (0x00404e50).
#[allow(clippy::too_many_arguments)]
fn controller(
    time: Res<Time>,
    mut m: ResMut<Markers>,
    mut rng: ResMut<crate::fx::FxStats>,
    troops: Query<&crate::fx::Health, With<AlarmTroop>>,
    player: Query<(&GlobalTransform, &crate::player::Roman)>,
    physics: NonSend<crate::physics::Physics>,
    mut spawn: MessageWriter<crate::level::SpawnGrunt>,
) {
    if m.sirens.is_empty() || m.generators.is_empty() {
        return;
    }
    m.timer -= time.delta_secs();
    if m.timer > 0.0 {
        return;
    }
    m.timer = rng.range(m.delay.0, m.delay.1);
    if troops.iter().filter(|h| h.0 > 0.0).count() >= m.max_troops {
        return;
    }
    let Ok((pt, r)) = player.single() else { return };
    let pp = pt.translation();
    // seen by the player (0x00404d00): in his 180 degree view and in sight of his chest
    let fwd = Quat::from_rotation_y(r.yaw) * Vec3::NEG_Z;
    let seen = |g: &Generator| (g.pos - pp).with_y(0.0).normalize_or_zero().dot(fwd) > 0.0 && crate::ai::line_of_sight(&physics, g.pos, pp);
    let mut order: Vec<usize> = (0..m.generators.len()).filter(|&i| !seen(&m.generators[i])).collect();
    order.sort_by(|&a, &b| (m.generators[a].pos - pp).with_y(0.0).length().total_cmp(&(m.generators[b].pos - pp).with_y(0.0).length()));
    let fresh: Vec<usize> = order.iter().copied().filter(|&i| !m.recent.contains(&m.generators[i].id)).collect();
    let Some(&gi) = fresh.first().or(order.first()) else { return };
    let g = &m.generators[gi];
    let total: f32 = g.weights.iter().map(|w| w.1).sum();
    let mut pick = rng.rand() * total;
    let mut tpl = g.weights[0].0;
    for (t, w) in &g.weights {
        if pick < *w {
            tpl = t;
            break;
        }
        pick -= w;
    }
    let (id, at) = (g.id, g.pos);
    spawn.write(crate::level::SpawnGrunt { at: at + Vec3::Y * 2.0, template: tpl.into(), alarm: true, ..default() });
    m.recent.push_back(id);
    if m.recent.len() > 3 {
        m.recent.pop_front();
    }
    info!("EVENT alarm: generator {id} sent a {tpl}");
}
