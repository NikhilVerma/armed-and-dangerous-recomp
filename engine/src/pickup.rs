//! Pickups dropped by dying grunts (Drop on death, cleanup_01.cpp:96-130: chance pickup_drop_percent / 100; the
//! auto pick gives the player what he needs: health when hurt, else sticky bombs, guess at the rule), from the
//! pickup templates' property sets: num_health / num_ammo, sphere_radius, linger_time, upward_force (the pop),
//! text_lookup (a message line on collection). Collected by walking into them.

use crate::ai::{Grunt, Mind, Player};
use crate::fx::Health;
use crate::gamedata::GameData;
use crate::level::game_to_engine;
use crate::scene::Library;
use bevy::prelude::*;

#[derive(Component)]
pub struct Pickup {
    pub health: f32,
    pub stickies: u32,
    pub radius: f32,
    pub life: f32,
    pub vel: f32,
    pub ground: f32,
    pub text: Option<String>,
    /// rotate_rate (PickupObject_Load 0x00471e40: default 180 degrees/s), radians/s; the spin about game z
    pub spin: f32,
    /// horizontal velocity of a thrown pickup (Q's tea), until it lands
    pub hvel: Vec3,
}

/// A pickup made at run time and thrown (Q's tea: mode 9 of 0x00485910 creates "pickup_healthytea" and sets its
/// velocity): template name, position, velocity (engine space).
#[derive(Message, Clone)]
pub struct SpawnPickup {
    pub name: String,
    pub at: Vec3,
    pub vel: Vec3,
}

pub struct PickupPlugin;

impl Plugin for PickupPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<SpawnPickup>()
            .add_systems(Update, (drop_on_death, spawn_pickups, collect, placed_pickups, placed_collect).run_if(crate::playing));
    }
}

#[allow(clippy::too_many_arguments)]
fn drop_on_death(
    mut commands: Commands,
    mut seen: Local<std::collections::HashSet<Entity>>,
    grunts: Query<(Entity, &Grunt, &GlobalTransform)>,
    player: Query<&Health, With<Player>>,
    roman: Query<&crate::player::Roman>,
    data: Res<GameData>,
    mut lib: ResMut<Library>,
    assets: Res<AssetServer>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    mut rng: ResMut<crate::fx::FxStats>,
    physics: NonSend<crate::physics::Physics>,
) {
    let hurt = match (player.single(), roman.single()) {
        (Ok(h), Ok(r)) => h.0 < r.max_health * 0.75,
        _ => true,
    };
    for (e, g, t) in &grunts {
        if g.mind != Mind::Dead || !seen.insert(e) {
            continue;
        }
        if rng.rand() >= g.drop_percent / 100.0 {
            continue;
        }
        let name = if hurt { "pickup_health_1" } else { "pickup_sticky_bombs" };
        let Some(tpl) = data.templates.get(name) else { continue };
        let p = data.template_props(name);
        let at = t.translation() + Vec3::Y * 4.0;
        // the ground under the drop point (from above it, the ray would land it on a roof or tree over the grunt)
        let ground = physics.raycast(at, -Vec3::Y, 200.0).map(|h| h.1.y).unwrap_or(at.y - 4.0);
        let pe = commands
            .spawn((
                Transform::from_translation(at).with_rotation(game_to_engine()),
                Visibility::default(),
                Pickup {
                    health: p.f("num_health", 0.0),
                    stickies: p.f("num_ammo", 0.0) as u32 * (name == "pickup_sticky_bombs") as u32,
                    radius: p.f("sphere_radius", 4.0) + 4.0,
                    life: p.f("linger_time", 300.0),
                    vel: p.f("upward_force", 70.0),
                    ground,
                    text: p.s("text_lookup").map(str::to_owned),
                    spin: p.f("rotate_rate", 180.0).to_radians(),
                    hvel: Vec3::ZERO,
                },
            ))
            .id();
        if let Some(m) = tpl.model.as_ref().and_then(|m| lib.load_model(m, false, &assets, &mut meshes, &mut materials)) {
            for (mesh, mat) in &m.parts {
                let c = commands.spawn((Mesh3d(mesh.clone()), MeshMaterial3d(mat.clone()))).id();
                commands.entity(pe).add_child(c);
            }
        }
    }
}

#[allow(clippy::too_many_arguments)]
fn spawn_pickups(
    mut commands: Commands,
    mut events: MessageReader<SpawnPickup>,
    data: Res<GameData>,
    mut lib: ResMut<Library>,
    assets: Res<AssetServer>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    physics: NonSend<crate::physics::Physics>,
) {
    for ev in events.read() {
        let Some(tpl) = data.templates.get(&ev.name) else { continue };
        let p = data.template_props(&ev.name);
        let ground = physics.raycast(ev.at, -Vec3::Y, 400.0).map(|h| h.1.y).unwrap_or(ev.at.y - 4.0);
        let pe = commands
            .spawn((
                Transform::from_translation(ev.at).with_rotation(game_to_engine()),
                Visibility::default(),
                Pickup {
                    health: p.f("num_health", 0.0),
                    stickies: 0,
                    radius: p.f("sphere_radius", 4.0) + 4.0,
                    life: p.f("linger_time", 300.0),
                    vel: ev.vel.y,
                    ground,
                    text: p.s("text_lookup").map(str::to_owned),
                    spin: p.f("rotate_rate", 180.0).to_radians(),
                    hvel: Vec3::new(ev.vel.x, 0.0, ev.vel.z),
                },
            ))
            .id();
        if let Some(m) = tpl.model.as_ref().and_then(|m| lib.load_model(m, false, &assets, &mut meshes, &mut materials)) {
            for (mesh, mat) in &m.parts {
                let c = commands.spawn((Mesh3d(mesh.clone()), MeshMaterial3d(mat.clone()))).id();
                commands.entity(pe).add_child(c);
            }
        }
    }
}

#[allow(clippy::too_many_arguments)]
fn collect(
    mut commands: Commands,
    time: Res<Time>,
    mut q: Query<(Entity, &mut Pickup, &mut Transform)>,
    mut player: Query<(&GlobalTransform, &mut Health, &mut crate::player::Roman), With<Player>>,
    mission: Option<ResMut<crate::mission::Mission>>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
) {
    let dt = time.delta_secs();
    let Ok((pt, mut h, mut r)) = player.single_mut() else { return };
    let mut mission = mission;
    for (e, mut p, mut t) in &mut q {
        // the pop: upward_force up, the world's gravity (64.348 units/s^2) back down to the ground; the spin about
        // the vertical at rotate_rate (0x00472170: the matrix spun about z by the angle +0x4b4)
        p.vel -= 64.348 * dt;
        t.translation.y = (t.translation.y + p.vel * dt).max(p.ground + 1.0);
        t.translation += p.hvel * dt;
        if t.translation.y <= p.ground + 1.0 {
            p.vel = 0.0;
            p.hvel = Vec3::ZERO;
        }
        t.rotate_y(dt * p.spin);
        p.life -= dt;
        if p.life <= 0.0 {
            commands.entity(e).despawn();
            continue;
        }
        if t.translation.distance(pt.translation() + Vec3::Y * 4.0) > p.radius {
            continue;
        }
        if p.health > 0.0 && h.0 >= r.max_health {
            continue; // full health: leave it
        }
        h.0 = (h.0 + p.health).min(r.max_health);
        r.stickies += p.stickies;
        if let (Some(m), Some(text)) = (mission.as_deref_mut(), p.text.as_ref()) {
            m.feed(text);
        }
        // Pickup_PlayHealthSound 0x00472790: Roman's "ROM50193_Ambient" for Q's tea, else the pickup sound
        if p.text.as_deref() == Some("pickup_healthytea") {
            sounds.write(crate::audio::PlaySound::at("ROM50193_Ambient", pt.translation()));
        } else {
            sounds.write(crate::audio::PlaySound::ui(if p.health > 0.0 { "ui_pickup_health" } else { "ui_pickup_ammo_1" }));
        }
        commands.entity(e).despawn();
    }
}

/// A pickup placed in the level (PickupObject, behaviour pickup_*): spins about the vertical at rotate_rate
/// (0x00472170) and is taken when Roman comes within its sphere_radius.
#[derive(Component)]
pub struct PlacedPickup {
    pub behavior: String,
    pub radius: f32,
    pub spin: f32,
    pub text: Option<String>,
    pub key: String,
}

/// The unlockables bought with tokens (table 0x0057e878, 20 x 0x1c bytes: type 0 level / 1 cheat / 2 feature, name
/// id, description id, cost per difficulty; 99 = never), and the total (0x005cadd0).
const UNLOCKS: [(&str, &str, [u32; 4]); 20] = [
    ("ui_extrasmenu_replay", "token_feature", [1, 1, 1, 1]),
    ("ui_extrasmenu_cutscene", "token_feature", [2, 2, 1, 1]),
    ("ui_cheat_bighead", "token_cheat", [3, 3, 1, 1]),
    ("lev_bonus_m1", "token_level", [4, 4, 1, 1]),
    ("ui_diff_3", "token_difflvl", [7, 6, 2, 1]),
    ("ui_cheat_bighands", "token_cheat", [10, 9, 3, 1]),
    ("ui_cheat_bigboots", "token_cheat", [14, 10, 4, 2]),
    ("lev_bonus_m2", "token_level", [18, 12, 5, 3]),
    ("ui_cheat_unlockmovies", "token_cheat", [21, 14, 6, 4]),
    ("ui_diff_4", "token_difflvl", [99, 16, 7, 5]),
    ("lev_bonus_m3", "token_level", [99, 18, 9, 6]),
    ("ui_cheat_fullammo", "token_cheat", [99, 21, 11, 7]),
    ("ui_cheat_fullhealth", "token_cheat", [99, 99, 13, 8]),
    ("ui_cheat_infammo", "token_cheat", [99, 99, 15, 9]),
    ("ui_cheat_invincible", "token_cheat", [99, 99, 17, 11]),
    ("ui_cheat_unlocklevels", "token_cheat", [99, 99, 19, 15]),
    ("ui_cheat_godmode", "token_cheat", [99, 99, 21, 19]),
    ("ui_cheat_flipscreen", "token_cheat", [99, 99, 99, 21]),
    ("ui_cheat_showall", "token_cheat", [99, 99, 99, 99]),
    ("ui_extrasmenu_credits", "token_feature", [99, 99, 99, 99]),
];
const TOKENS_TOTAL: u32 = 21;
/// Game+0x550 difficulty index (guess: 1 = normal, the default)
const DIFFICULTY: usize = 1;

/// The token message (fn_004a0850): "token_got" with the count, then the unlockable just paid for (the dearest
/// affordable, compared with the count before) or the next one with what it still needs.
pub fn token_message(texts: &crate::gamemenu::Texts, count: u32) -> String {
    let fill = |s: String, args: &[String]| {
        let mut out = s;
        for a in args {
            out = out.replacen("%d", a, 1).replacen("%s", a, 1);
        }
        out
    };
    let got = texts.get("token_got").replacen("%d", &count.to_string(), 1).replacen("%d", &TOKENS_TOTAL.to_string(), 1);
    let cost = |u: &(&str, &str, [u32; 4])| u.2[DIFFICULTY];
    let unlocked = UNLOCKS.iter().filter(|u| cost(u) == count).max_by_key(|u| cost(u));
    let next = UNLOCKS.iter().filter(|u| cost(u) > count && cost(u) != 99).min_by_key(|u| cost(u));
    let line = if let Some(u) = unlocked {
        fill(texts.get("token_justunlocked"), &[texts.get(u.0), texts.get(u.1)])
    } else if let Some(u) = next {
        let need = cost(u) - count;
        if need == 1 {
            fill(texts.get("token_tounlock_single"), &[texts.get(u.0), texts.get(u.1)])
        } else {
            fill(texts.get("token_tounlock"), &[need.to_string(), texts.get(u.0), texts.get(u.1)])
        }
    } else {
        String::new()
    };
    if line.is_empty() { got } else { format!("{got}<br>{line}") }
}

fn placed_pickups(
    mut commands: Commands,
    q: Query<(Entity, &crate::level::Placed, &crate::level::InstanceId), Added<crate::level::Placed>>,
    data: Res<GameData>,
    opts: Res<crate::options::GameOptions>,
) {
    for (e, placed, id) in &q {
        let p = data.template_props(&placed.template);
        let Some(b) = p.s("behavior").filter(|b| b.starts_with("pickup")) else { continue };
        let key = format!("lev_1_m1:{}", id.0);
        if b == "pickup_token" && opts.tokens_taken.contains(&key) {
            commands.entity(e).despawn();
            continue;
        }
        commands.entity(e).insert(PlacedPickup {
            behavior: b.to_owned(),
            radius: p.f("sphere_radius", 4.0) + 4.0,
            spin: p.f("rotate_rate", 180.0).to_radians(),
            text: p.s("text_lookup").map(str::to_owned),
            key,
        });
    }
}

#[allow(clippy::too_many_arguments)]
fn placed_collect(
    mut commands: Commands,
    time: Res<Time>,
    mut q: Query<(Entity, &PlacedPickup, &mut Transform)>,
    player: Query<&GlobalTransform, With<crate::player::Roman>>,
    mut mission: Option<ResMut<crate::mission::Mission>>,
    mut opts: ResMut<crate::options::GameOptions>,
    texts: Option<Res<crate::gamemenu::Texts>>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
) {
    let dt = time.delta_secs();
    let Ok(pt) = player.single() else { return };
    for (e, p, mut t) in &mut q {
        // about game z = engine y
        t.rotate_y(p.spin * dt);
        if t.translation.distance(pt.translation()) > p.radius + 4.0 {
            continue;
        }
        commands.entity(e).despawn();
        sounds.write(crate::audio::PlaySound::ui("ui_pickup_ammo_1"));
        if let (Some(m), Some(text)) = (mission.as_deref_mut(), p.text.as_ref()) {
            m.feed(text);
        }
        if p.behavior == "pickup_token" {
            opts.tokens += 1;
            opts.tokens_taken.push(p.key.clone());
            opts.save();
            if let (Some(m), Some(tx)) = (mission.as_deref_mut(), texts.as_ref()) {
                let msg = token_message(tx, opts.tokens);
                m.log.push(format!("token {} found ({} of {TOKENS_TOTAL})", p.key, opts.tokens));
                m.message(&msg, 0.0);
            }
        }
    }
}
