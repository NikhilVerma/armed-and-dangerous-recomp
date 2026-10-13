//! Sound the way lev_1_m1 plays it, from the game's data: anim note tracks (FX <fx> / fx_material <fx> + the
//! surface material: footsteps, foley, weapon handling, run through the fx VM), ambient emitters (ambient.json:
//! a world loop in 2D, positional one-shots within max_distance after oneshot_delay as the game runs it), and the
//! music segment graph (sounds.json `next`: at a segment's end the entry whose name starts with the wanted state,
//! else the first; states l1amb / l1bat / l1vic / l1def: docs/subsystems/audio.md, Music_Update 0x0044f580).

use crate::audio::{PlaySound, SoundBank};
use crate::fx::StartFx;
use crate::skin::AnimNote;
use bevy::prelude::*;
use serde::Deserialize;

pub struct GameAudioPlugin;

impl Plugin for GameAudioPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, (notes, ambient, music));
    }
}

/// FX <name>: the fx at the character; fx_material <name>: + "_" + the surface (lev_1_m1 ground: snow, guess:
/// the material lookup under the feet is not ported).
fn notes(mut ev: MessageReader<AnimNote>, q: Query<&GlobalTransform>, mut fx: MessageWriter<StartFx>) {
    for n in ev.read() {
        let mut it = n.text.split_whitespace();
        let (Some(kind), Some(name)) = (it.next(), it.next()) else { continue };
        let Ok(t) = q.get(n.entity) else { continue };
        let name = name.to_lowercase();
        let name = match kind.to_lowercase().as_str() {
            "fx" | "fxlocked" => name,
            "fx_material" => format!("{name}_snow"),
            _ => continue,
        };
        fx.write(StartFx { name, at: t.translation(), rotation: Quat::IDENTITY, follow: None });
    }
}

#[derive(Deserialize, Clone)]
struct AmbientDef {
    position: [f32; 3],
    world: bool,
    #[serde(default)]
    loops: Vec<String>,
    #[serde(default)]
    oneshots: Vec<String>,
    #[serde(default)]
    max_distance: Option<f32>,
    #[serde(default)]
    oneshot_delay: Option<(f32, f32)>,
}

#[derive(Deserialize)]
struct AmbientFile {
    ambients: Vec<AmbientDef>,
}

struct Emitter {
    def: AmbientDef,
    pos: Vec3,
    timer: f32,
    looping: bool,
}

fn ambient(
    mut started: Local<Option<Vec<Emitter>>>,
    launch: Option<Res<crate::Launch>>,
    time: Res<Time>,
    listener: Query<&GlobalTransform, With<SpatialListener>>,
    mut sounds: MessageWriter<PlaySound>,
    mut rng: ResMut<crate::fx::FxStats>,
) {
    let Some(launch) = launch else { return };
    let ems = started.get_or_insert_with(|| {
        std::fs::read_to_string(launch.scene.join("audio").join("ambient.json"))
            .ok()
            .and_then(|t| serde_json::from_str::<AmbientFile>(&t).ok())
            .map(|f| {
                f.ambients
                    .into_iter()
                    .map(|d| Emitter { pos: crate::level::game_to_engine() * Vec3::from(d.position), timer: 1.0, looping: false, def: d })
                    .collect()
            })
            .unwrap_or_default()
    });
    let Ok(l) = listener.single() else { return };
    for e in ems.iter_mut() {
        let near = e.def.world || e.def.max_distance.is_none_or(|m| e.pos.distance(l.translation()) < m);
        // one random loop is kept running (world emitters in 2D)
        if !e.looping && !e.def.loops.is_empty() && near {
            let i = (rng.rand() * e.def.loops.len() as f32) as usize % e.def.loops.len();
            let mut s = if e.def.world { PlaySound::ui(e.def.loops[i].clone()) } else { PlaySound::at(e.def.loops[i].clone(), e.pos) };
            s.looped = Some(true);
            sounds.write(s);
            e.looping = true;
        }
        if e.def.oneshots.is_empty() || !near {
            continue;
        }
        e.timer -= time.delta_secs();
        if e.timer <= 0.0 {
            let (a, b) = e.def.oneshot_delay.unwrap_or((5.0, 10.0));
            e.timer = rng.range(a, b.max(a));
            let i = (rng.rand() * e.def.oneshots.len() as f32) as usize % e.def.oneshots.len();
            // positional ambients get the distance curve but play 2D, no pan (0x00500520)
            sounds.write(PlaySound::ui(e.def.oneshots[i].clone()));
        }
    }
}

#[derive(Default)]
struct MusicState {
    segment: Option<String>,
    left: f32,
}

fn music(
    mut st: Local<MusicState>,
    time: Res<Time<Real>>,
    bank: Option<Res<SoundBank>>,
    grunts: Query<&crate::ai::Grunt>,
    mission: Option<Res<crate::mission::Mission>>,
    mut sounds: MessageWriter<PlaySound>,
    state: Res<State<crate::AppState>>,
) {
    let Some(bank) = bank else { return };
    if *state.get() == crate::AppState::Title {
        return;
    }
    let want = match mission.as_ref().map(|m| m.outcome) {
        Some(crate::mission::Outcome::Won) => "l1vic",
        Some(crate::mission::Outcome::Failed) => "l1def",
        _ if grunts.iter().any(|g| g.target.is_some() && matches!(g.mind, crate::ai::Mind::Fire | crate::ai::Mind::Chase)) => "l1bat",
        _ => "l1amb",
    };
    st.left -= time.delta_secs();
    if st.left > 0.0 {
        return;
    }
    let next = match &st.segment {
        None => Some(want.to_string()),
        Some(cur) => bank.sounds.get(cur).and_then(|d| d.next.iter().find(|x| x.starts_with(want)).or_else(|| d.next.first()).cloned()),
    };
    let Some(seg) = next.filter(|s| s != "none") else {
        st.segment = None;
        st.left = 1e9;
        return;
    };
    let dur = bank.sounds.get(&seg).map(|d| d.duration).unwrap_or(4.0);
    sounds.write(PlaySound::ui(seg.clone()));
    st.segment = Some(seg);
    st.left = dur;
}
