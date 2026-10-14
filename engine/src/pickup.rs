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
    /// machine gun rounds (pickup_mgun_bullets num_ammo)
    pub mg_ammo: u32,
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
    /// stays where it is made (a generator's pickup), no fall to the ground
    pub hover: bool,
}

pub struct PickupPlugin;

impl Plugin for PickupPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<SpawnPickup>()
            .add_systems(Update, (drop_on_death, generators, spawn_pickups, collect, placed_pickups, placed_collect).run_if(crate::playing));
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
    // the pick (0x00475a90, weights from the level properties, LevelProperties_Load 0x00423900, defaults x 0.01):
    // health pickup_health_weight_low 0.5 below pickup_low_health_thresh_pct 25 % health, else _normal 0.25; the
    // current weapon's ammo pickup_cur_weapon_weight 0.5 (never for the rifle, weapon 2: it has no ammo to find);
    // the first other weapon (not the rifle) pickup_noncur_weapon_weight 0.25; sticky bombs pickup_sticky_bomb_weight
    // 0.25. x = rand x (sticky + cur + noncur + health): health below its weight, then the current weapon, the other
    // one, else sticky bombs
    let (ratio, mg_owned, mg_in_hand) = match (player.single(), roman.single()) {
        (Ok(h), Ok(r)) => (h.0 / r.max_health.max(1.0), r.mg.is_some(), r.use_mg && r.mg.is_some()),
        _ => (1.0, false, false),
    };
    for (e, g, t) in &grunts {
        if g.mind != Mind::Dead || !seen.insert(e) {
            continue;
        }
        if rng.rand() >= g.drop_percent / 100.0 {
            continue;
        }
        let hw = if ratio < 0.25 { 0.5 } else { 0.25 };
        let (cur, sticky) = (0.5, 0.25);
        let other = if mg_owned && !mg_in_hand { 0.25 } else { 0.0 };
        let x = rng.rand() * (sticky + cur + other + hw);
        let name = if x < hw {
            "pickup_health_1"
        } else if x < hw + cur && mg_in_hand {
            "pickup_mgun_bullets"
        } else if other > 0.0 && x < hw + cur + other {
            "pickup_mgun_bullets"
        } else {
            "pickup_sticky_bombs"
        };
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
                    mg_ammo: p.f("num_ammo", 0.0) as u32 * (name == "pickup_mgun_bullets") as u32,
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
/// Pickup generators (PickupGenerator 0x00471b70 load, 0x00471cb0 update; lev_1_m1's 219 and 220 with the defaults:
/// delay_time 10, no turbo_boost, no generated_ammo_name, no activate trigger): one pickup at a time, 4 units above
/// the generator; once it is taken, delay_time later the next; with no name the current weapon's ammo pickup
/// (0x00475c30: the weapon table's name; rifle -> pickup_rifle_bullets, machine gun -> pickup_mgun_bullets).
fn generators(
    time: Res<Time>,
    lib: Res<crate::skin::AnimLibrary>,
    mut gens: Local<Option<Vec<(Vec3, f32, bool)>>>,
    pickups: Query<&GlobalTransform, With<Pickup>>,
    roman: Query<&crate::player::Roman>,
    mut out: MessageWriter<SpawnPickup>,
) {
    let list = gens.get_or_insert_with(|| {
        let text = std::fs::read_to_string(lib.dir.join("instance_props.json")).unwrap_or_default();
        let v: Vec<serde_json::Value> = serde_json::from_str(&text).unwrap_or_default();
        v.iter()
            .filter(|i| i.get("template").and_then(|t| t.as_str()) == Some("pickup_generator"))
            .filter_map(|i| {
                let p = i.get("pos")?.as_array()?;
                let g = Vec3::new(p[0].as_f64()? as f32, p[1].as_f64()? as f32, p[2].as_f64()? as f32);
                Some((game_to_engine() * g + Vec3::Y * 4.0, 0.0, false))
            })
            .collect()
    });
    let dt = time.delta_secs();
    let mg = roman.single().is_ok_and(|r| r.use_mg && r.mg.is_some());
    for (at, timer, made) in list.iter_mut() {
        // its pickup still there: nothing else happens
        if *made && pickups.iter().any(|p| p.translation().distance(*at) < 3.0) {
            *timer = 10.0;
            continue;
        }
        *timer -= dt;
        if !*made || *timer <= 0.0 {
            *made = true;
            *timer = 10.0;
            let name = if mg { "pickup_mgun_bullets" } else { "pickup_rifle_bullets" };
            out.write(SpawnPickup { name: name.into(), at: *at, vel: Vec3::ZERO, hover: true });
        }
    }
}

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
        let ground = if ev.hover { ev.at.y - 1.0 } else { physics.raycast(ev.at, -Vec3::Y, 400.0).map(|h| h.1.y).unwrap_or(ev.at.y - 4.0) };
        let pe = commands
            .spawn((
                Transform::from_translation(ev.at).with_rotation(game_to_engine()),
                Visibility::default(),
                Pickup {
                    health: p.f("num_health", 0.0),
                    stickies: p.f("num_ammo", 0.0) as u32 * (ev.name == "pickup_sticky_bombs") as u32,
                    mg_ammo: p.f("num_ammo", 0.0) as u32 * (ev.name == "pickup_mgun_bullets") as u32,
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
    ui_texts: Res<crate::gamemenu::Texts>,
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
        // PickupObject_Update 0x00472200: the spin, gone after linger_time (its fade over fade_time, default 1 s,
        // vt+0x100(1 - f), is not ported: the pickup vanishes at once)
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
        if let Some(w) = r.mg.as_mut() {
            w.ammo += p.mg_ammo;
        }
        if let (Some(m), Some(text)) = (mission.as_deref_mut(), p.text.as_ref()) {
            m.feed(&ui_texts.get(text));
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
    mut ms: ResMut<crate::stats::MissionStats>,
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
            // 2500 Loot per token (0x004a0850, ui_03.cpp:3102)
            ms.loot_money += 2500.0;
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
