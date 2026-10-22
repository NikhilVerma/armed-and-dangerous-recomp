//! Pickups (PickupObject, 17 vtables of "CLASS(PickupObject)", class ids 25..42; game_04.cpp / game_05.cpp): placed in
//! the level, dropped by dying grunts (Character_DropPickup 0x0043e710 with the pick 0x00475a90), made by pickup
//! generators (PickupGenerator 0x00471b70 / 0x00471cb0) or thrown (Q's tea). PickupObject_Load 0x00471e40 reads
//! upward_force (the pop), text_lookup, num_ammo (else num_health), linger_time, fade_time, rotate_rate; the update
//! 0x00472200 spins it, removes it after linger_time (fading over fade_time) and once collected. Touching it
//! (0x00472310): the player gets it through the class's collect slot (vt+0x138: the rules below; refused, the pickup
//! stays), with its text in the feed and its on_enter trigger sent; anything else in its collision group (AI, with
//! ai_can_trigger) through vt+0x13c (only the health pickups give anything: 0x00472850).

use crate::levelreset::LevelAppExt;
use crate::ai::{Grunt, Mind, Player};
use crate::fx::Health;
use crate::gamedata::GameData;
use crate::level::game_to_engine;
use crate::scene::Library;
use bevy::prelude::*;

/// What collecting a pickup does: the collect slot of its class (behaviour, else the template name: the class table
/// 0x0045df60 / 0x00461240).
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Kind {
    /// pickup_*_bullets / rockets / mortars, class 25..30: 0x00472470, 0x004724e0, 0x004724a0, 0x00472520, 0x00472560,
    /// 0x004725a0 (0x004a13a0 into weapon 0..5: 0 machine gun,
    /// 1 sniper, 2 rifle, 3 vindaloo, 4 gurner, 5 sharkgun)
    Ammo(u8),
    /// special weapons, class 31..35: 0x004725e0 (6 guy fawkes), 0x00472630 (7 bunch of fives), 0x004726d0 (8 topsy
    /// turvy), 0x00472680 (9 black hole), 0x00472720 (10 sticky bombs): 0x004a13a0, then 0x004a12a0 selects it
    Special(u8),
    /// pickup_health / pickup_healthytea, class 37: 0x00472800 (the player: num_health x Game+0x55c, 0x00475a30) and
    /// 0x00472850 (anyone else: num_health)
    Health,
    /// pickup_upgrade_weapon (36, 0x00472770): Game+0xdd0, the vindaloo upgraded (weapons/vindaloo.rs); always taken
    VindalooUpgrade,
    /// pickup_vindaloo_weapon (39, 0x00472930): the vindaloo into a free slot (0x004a0ef0) with num_ammo rounds
    /// (0x004a14e0); refused when held or the slots are full. (No level places one.)
    VindalooWeapon,
    /// a class without a collect slot of its own
    Unported,
    /// pickup_jumper_pack (40, 0x00472980; loads through 0x004728e0): the jumper pack (jetpack.rs), refused when he
    /// has one
    JumperPack,
    /// pickup_turbo_boost (41, 0x004729c0): a boost on the pack, refused when pack and boost are both there
    TurboBoost,
    /// pickup_token (42): 0x00472aa0 marks it, its update 0x00472a00 runs the token reward (0x004a07a0 / 0x004a0850)
    Token,
}

impl Kind {
    fn of(class: &str) -> Kind {
        match class {
            "pickup_mgun_bullets" => Kind::Ammo(0),
            "pickup_sniper_bullets" => Kind::Ammo(1),
            "pickup_rifle_bullets" => Kind::Ammo(2),
            "pickup_vindaloo_mortars" => Kind::Ammo(3),
            "pickup_gurner_rockets" => Kind::Ammo(4),
            "pickup_sharkgun_rockets" => Kind::Ammo(5),
            "pickup_guyfawkes" => Kind::Special(6),
            "pickup_bunchoffives" => Kind::Special(7),
            "pickup_topsy_weapon" => Kind::Special(8),
            "pickup_blackhole" => Kind::Special(9),
            "pickup_sticky_bombs" => Kind::Special(10),
            "pickup_health" | "pickup_healthytea" => Kind::Health,
            "pickup_token" => Kind::Token,
            "pickup_upgrade_weapon" => Kind::VindalooUpgrade,
            "pickup_vindaloo_weapon" => Kind::VindalooWeapon,
            "pickup_jumper_pack" => Kind::JumperPack,
            "pickup_turbo_boost" => Kind::TurboBoost,
            _ => Kind::Unported,
        }
    }
}


#[derive(Component)]
pub struct Pickup {
    pub kind: Kind,
    /// +0x49c: num_ammo, or num_health when num_ammo is absent (-1)
    pub amount: f32,
    pub radius: f32,
    /// linger_time (+0x4a0) left, fade_time (+0x4a8) and the fade's progress (+0x4ac)
    pub life: f32,
    pub fade_time: f32,
    pub fading: Option<f32>,
    pub vel: f32,
    pub ground: f32,
    pub text: Option<String>,
    /// rotate_rate (PickupObject_Load 0x00471e40: default 180 degrees/s), radians/s; the spin about game z
    pub spin: f32,
    /// horizontal velocity of a thrown pickup (Q's tea), until it lands
    pub hvel: Vec3,
    /// the TriggerObject part: on_enter (+0x460) sent when the player collects it, ai_can_trigger (AI touch it)
    pub on_enter: i64,
    pub ai: bool,
    /// placed in the level (its persistent key for tokens): no pop to the ground
    pub placed: Option<String>,
    /// its materials are its own (the fade)
    own_materials: bool,
}

impl Pickup {
    /// PickupObject_Load 0x00471e40 from the pickup's properties.
    fn new(class: &str, p: &crate::gamedata::Props) -> Pickup {
        let ammo = p.f("num_ammo", -1.0);
        Pickup {
            kind: Kind::of(class),
            amount: if ammo == -1.0 { p.f("num_health", 0.0) } else { ammo },
            radius: p.f("sphere_radius", 4.0) + 4.0,
            life: p.f("linger_time", 10.0),
            fade_time: p.f("fade_time", 1.0),
            fading: None,
            vel: p.f("upward_force", 70.0),
            ground: 0.0,
            text: p.s("text_lookup").map(str::to_owned),
            spin: p.f("rotate_rate", 180.0).to_radians(),
            hvel: Vec3::ZERO,
            on_enter: p.f("on_enter", 0.0) as i64,
            ai: p.f("ai_can_trigger", 0.0) != 0.0,
            placed: None,
            own_materials: false,
        }
    }
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

/// A pickup generator (PickupGenerator, vtable 0x005a6258; PickupGenerator_Load 0x00471b70).
struct Generator {
    id: u32,
    pos: Vec3,
    /// delay_time (+0x464, 10), turbo_boost (+0x46c), generated_ammo_name (+0x470), listen_activate_trigger (+0x468)
    delay: f32,
    turbo: bool,
    name: Option<String>,
    listen: i64,
    /// the countdown +0x460 (0 at load), "made one" +0x46d, the pickup it made +0x490
    timer: f32,
    made: bool,
    current: Option<Entity>,
}

#[derive(Resource, Default)]
struct Generators(Option<Vec<Generator>>);

pub struct PickupPlugin;

impl Plugin for PickupPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<SpawnPickup>()
            .init_level_resource::<Generators>()
            .add_systems(Update, (drop_on_death, generators, spawn_pickups, placed_pickups, collect, state_log).chain().run_if(crate::playing))
            .add_systems(Update, playtest_steps.run_if(resource_exists::<crate::playtest::Playtest>));
    }
}

/// A pickup of template `name` at `at` (engine space), with its model.
#[allow(clippy::too_many_arguments)]
fn spawn_one(commands: &mut Commands, data: &GameData, lib: &mut Library, assets: &AssetServer, meshes: &mut Assets<Mesh>, materials: &mut Assets<StandardMaterial>, physics: &crate::physics::Physics, ev: &SpawnPickup) -> Option<Entity> {
    let tpl = data.templates.get(&ev.name)?;
    let p = data.template_props(&ev.name);
    let class = p.s("behavior").map(str::to_owned).unwrap_or_else(|| ev.name.clone());
    let mut pk = Pickup::new(&class, &p);
    // a thrown or dropped pickup gets the velocity it is given (the drop: upward_force, 0x00458440 at load)
    if ev.vel != Vec3::ZERO || ev.hover {
        pk.vel = ev.vel.y;
    }
    pk.hvel = Vec3::new(ev.vel.x, 0.0, ev.vel.z);
    // the ground under the point (from above it, the ray would land it on a roof or tree)
    pk.ground = if ev.hover { ev.at.y - 1.0 } else { physics.raycast(ev.at, -Vec3::Y, 400.0).map(|h| h.1.y).unwrap_or(ev.at.y - 4.0) };
    let pe = commands.spawn((Transform::from_translation(ev.at).with_rotation(game_to_engine()), Visibility::default(), pk)).id();
    if let Some(m) = tpl.model.as_ref().and_then(|m| lib.load_model(m, false, assets, meshes, materials)) {
        for (mesh, mat) in &m.parts {
            let c = commands.spawn((Mesh3d(mesh.clone()), MeshMaterial3d(mat.clone()))).id();
            commands.entity(pe).add_child(c);
        }
    }
    Some(pe)
}

#[allow(clippy::too_many_arguments)]
fn drop_on_death(
    mut seen: crate::levelreset::PerLevel<std::collections::HashSet<Entity>>,
    grunts: Query<(Entity, &Grunt, &GlobalTransform)>,
    player: Query<&Health, With<Player>>,
    roman: Query<&crate::player::Roman>,
    mut rng: ResMut<crate::fx::FxStats>,
    mut out: MessageWriter<SpawnPickup>,
    (difficulty, mission): (Res<crate::gamedata::Difficulty>, Option<Res<crate::mission::Mission>>),
) {
    // Character_DropPickup 0x0043e710 drops nothing while 0x0044e600 holds (Roman there, the mission's
    // start_in_turret_tag set: the defence levels)
    if mission.is_some_and(|m| m.start_in_turret) && !player.is_empty() {
        return;
    }
    // the roll's exponent: Game+0xc60 (1.0 in every traced run, docs/structs/Game.md) x Game+0x560, the
    // difficulty's (easy 2, normal 1, hard 0.75, very hard 0.5)
    let exponent = difficulty.factors().unknown_560;
    // the pick (0x00475a90, weights from the level properties, LevelProperties_Load 0x00423900, defaults x 0.01):
    // health pickup_health_weight_low 0.5 below pickup_low_health_thresh_pct 25 % health, else _normal 0.25; the
    // current weapon's ammo pickup_cur_weapon_weight 0.5 (never for the rifle, weapon 2: it has no ammo to find);
    // the first other weapon (not the rifle) pickup_noncur_weapon_weight 0.25; sticky bombs pickup_sticky_bomb_weight
    // 0.25. x = rand x (sticky + cur + noncur + health): health below its weight, then the current weapon, the other
    // one, else sticky bombs
    // 0x00475a90: the gun in hand (not the rifle: weapon 2 has no ammo to find) and the first other carried gun that is
    // not the rifle
    use crate::weapons::WeaponId;
    let (ratio, cur_gun, other_gun) = match (player.single(), roman.single()) {
        (Ok(h), Ok(r)) => {
            let cur = r.arsenal.in_hand_id();
            let other = r.arsenal.guns().map(|g| g.id).find(|&id| Some(id) != cur && id != WeaponId::Rifle);
            (h.0 / r.max_health.max(1.0), cur.filter(|&id| id != WeaponId::Rifle), other)
        }
        _ => (1.0, None, None),
    };
    let seen = seen.get();
    for (e, g, t) in &grunts {
        if g.mind != Mind::Dead || !seen.insert(e) {
            continue;
        }
        // Character_Die 0x004319e0 -> Character_DropPickup 0x0043e710: pickup_drop_percent p, the chance
        // 1 - (1 - p)^exponent
        let chance = 1.0 - (1.0 - g.drop_percent / 100.0).max(0.0).powf(exponent);
        if rng.rand() >= chance {
            continue;
        }
        // a pickup_drop_specific (+0xb2c >= 0, or -2 / -3 for the health pickups) instead of the player's need
        if let Some(name) = g.drop_specific {
            out.write(SpawnPickup { name: name.into(), at: t.translation() + Vec3::Y * 4.0, vel: Vec3::ZERO, hover: false });
            continue;
        }
        let hw = if ratio < 0.25 { 0.5 } else { 0.25 };
        let (cur, sticky) = (0.5, 0.25);
        let other = if other_gun.is_some() { 0.25 } else { 0.0 };
        let x = rng.rand() * (sticky + cur + other + hw);
        let ammo = |id: WeaponId| id.table().pickup.unwrap_or("pickup_sticky_bombs");
        let name = match (cur_gun, other_gun) {
            _ if x < hw => "pickup_health_1",
            (Some(c), _) if x < hw + cur => ammo(c),
            (_, Some(o)) if x < hw + cur + other => ammo(o),
            _ => "pickup_sticky_bombs",
        };
        // the pop: upward_force (PickupObject_Load's push)
        out.write(SpawnPickup { name: name.into(), at: t.translation() + Vec3::Y * 4.0, vel: Vec3::ZERO, hover: false });
    }
}

/// The ammo pickup of the gun in hand (the weapon table's Game+0x2890 + id x 0x150).
fn ammo_pickup(r: &crate::player::Roman) -> Option<&'static str> {
    r.arsenal.in_hand_id().and_then(|id| id.table().pickup)
}

/// 0x00475c70: the named weapon's or special's record is the player's and empty (a gun carried with no spare; a
/// special in the special list with none left).
fn out_of(r: &crate::player::Roman, name: &str) -> bool {
    if let Some(g) = r.arsenal.guns().find(|g| g.id.table().pickup == Some(name)) {
        return g.ammo == 0;
    }
    crate::weapons::SpecialId::ALL.into_iter().find(|s| s.table().pickup == Some(name)).is_some_and(|s| r.arsenal.lists(s) && r.arsenal.count(s) == 0)
}

/// Pickup generators (PickupGenerator_Update 0x00471cb0): nothing while listen_activate_trigger is pending
/// (PickupGenerator_OnTrigger 0x00471c30 clears it) or while its pickup is there; else the countdown runs and, at 0
/// (or before the first), the template: turbo generators "pickup_turbo_boost" unless the pack has its boost (0x0047b8e0:
/// pack and boost; no level places a turbo generator); generated_ammo_name (after
/// the first only while the player is out of that weapon's ammo, 0x00475c70); else the current weapon's ammo pickup
/// when that weapon's spare ammo is 0 (0x00475c30). None: retry in 1 s; one: made 4 units above the generator, the
/// countdown back at delay_time. (The coverage runs, rifle in hand, call 0x00475c30 every frame of both lev_1_m1
/// generators and make nothing: the rifle's reserve is unlimited.)
#[allow(clippy::too_many_arguments)]
fn generators(
    time: Res<Time>,
    mut gens: ResMut<Generators>,
    lib_dir: Res<crate::skin::AnimLibrary>,
    pickups: Query<(), With<Pickup>>,
    roman: Query<(&crate::player::Roman, Option<&crate::jetpack::JetPack>)>,
    mut triggers: MessageReader<crate::mission::SendTrigger>,
    mut commands: Commands,
    data: Res<GameData>,
    (mut lib, assets, mut meshes, mut materials): (ResMut<Library>, Res<AssetServer>, ResMut<Assets<Mesh>>, ResMut<Assets<StandardMaterial>>),
    physics: NonSend<crate::physics::Physics>,
    mission: Option<ResMut<crate::mission::Mission>>,
) {
    let list = gens.0.get_or_insert_with(|| {
        let text = std::fs::read_to_string(lib_dir.dir.join("instance_props.json")).unwrap_or_default();
        let v: Vec<serde_json::Value> = serde_json::from_str(&text).unwrap_or_default();
        v.iter()
            .filter(|i| i.get("template").and_then(|t| t.as_str()) == Some("pickup_generator"))
            .filter_map(|i| {
                let p = i.get("pos")?.as_array()?;
                let g = Vec3::new(p[0].as_f64()? as f32, p[1].as_f64()? as f32, p[2].as_f64()? as f32);
                let own = i.get("props").and_then(|p| p.as_object()).cloned().unwrap_or_default();
                let r = data.instance_props("pickup_generator", &own);
                Some(Generator {
                    id: i.get("id").and_then(|v| v.as_u64()).unwrap_or(0) as u32,
                    pos: game_to_engine() * g,
                    delay: r.f("delay_time", 10.0),
                    turbo: r.f("turbo_boost", 0.0) != 0.0,
                    name: r.s("generated_ammo_name").filter(|s| !s.is_empty()).map(str::to_owned),
                    listen: r.f("listen_activate_trigger", 0.0) as i64,
                    timer: 0.0,
                    made: false,
                    current: None,
                })
            })
            .collect()
    });
    let mut logs = Vec::new();
    for crate::mission::SendTrigger(t) in triggers.read() {
        for g in list.iter_mut() {
            if g.listen != 0 && *t == g.listen {
                g.listen = 0;
                logs.push(format!("pickup generator {} activated", g.id));
            }
        }
    }
    let dt = time.delta_secs();
    let (r, jet) = roman.single().map_or((None, None), |(r, j)| (Some(r), j));
    for g in list.iter_mut() {
        if g.listen != 0 {
            continue;
        }
        if g.current.is_some_and(|e| !pickups.contains(e)) {
            g.current = None;
        }
        if g.current.is_some() {
            continue;
        }
        g.timer -= dt;
        if g.timer > 0.0 && g.made {
            continue;
        }
        let name: Option<String> = if g.turbo {
            (!jet.is_some_and(|j| j.pack && j.boost)).then(|| "pickup_turbo_boost".to_owned())
        } else if let Some(n) = &g.name {
            // 0x00475c70: the named weapon is the player's and its ammo is out
            (!g.made || r.is_some_and(|r| out_of(r, n))).then(|| n.clone())
        } else {
            // 0x00475c30: the current weapon's spare ammo (0x004a1360(0x1c)) is 0; the rifle's never runs out
            r.filter(|r| r.arsenal.in_hand().is_some_and(|w| w.ammo == 0 && !w.spec.unlimited)).and_then(ammo_pickup).map(str::to_owned)
        };
        let Some(name) = name else {
            g.timer = 1.0;
            continue;
        };
        g.timer = g.delay;
        g.made = true;
        g.current = spawn_one(&mut commands, &data, &mut lib, &assets, &mut meshes, &mut materials, &physics, &SpawnPickup { name: name.clone(), at: g.pos + Vec3::Y * 4.0, vel: Vec3::ZERO, hover: true });
        logs.push(format!("pickup generator {} made {name}", g.id));
    }
    if let Some(mut m) = mission {
        m.log.extend(logs);
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
        spawn_one(&mut commands, &data, &mut lib, &assets, &mut meshes, &mut materials, &physics, ev);
    }
}

/// The player collects (vt+0x138 of the class): true when he took it.
fn give(p: &Pickup, r: &mut crate::player::Roman, h: &mut Health, jet: Option<&mut crate::jetpack::JetPack>, data: &GameData, health_scale: f32, upgrade: &mut crate::weapons::VindalooUpgrade) -> bool {
    use crate::weapons::{Gun, GunSpec, SpecialId, WeaponId};
    let amount = p.amount.max(0.0) as u32;
    if amount == 0 && matches!(p.kind, Kind::Ammo(_) | Kind::Special(_)) {
        // 0x004a13a0 refuses an amount of 0 or -1
        return false;
    }
    match p.kind {
        // the gun's record (0x004a13a0): its spare fills to max ammo, the rest to the clip; the record exists for every
        // weapon (the ammo waits for the gun): the remaster keeps none for a gun not carried, so it is just taken
        Kind::Ammo(slot) => match WeaponId::from_slot(slot as usize).and_then(|id| r.arsenal.gun_mut(id)) {
            Some(w) => w.add_ammo(amount),
            None => true,
        },
        // specials: refused at the maximum (the descriptor's max ammo, +0x144), else up to it; 0x004a12a0 makes it
        // the special in hand
        Kind::Special(slot) => match SpecialId::from_slot(slot as usize) {
            Some(s) => {
                if !r.arsenal.add_special(s, amount, crate::weapons::spec::special_max(data, s)) {
                    return false;
                }
                r.arsenal.special = s;
                true
            }
            None => true,
        },
        Kind::VindalooUpgrade => {
            upgrade.0 = true;
            true
        }
        Kind::VindalooWeapon => {
            let mut g = Gun { id: WeaponId::Vindaloo, spec: GunSpec::load(data, WeaponId::Vindaloo), clip: 0, ammo: 0, timer: 0.0 };
            g.set_ammo(amount as i64);
            r.arsenal.add(g)
        }
        Kind::Unported => true,
        Kind::JumperPack => jet.is_some_and(crate::jetpack::give_pack),
        Kind::TurboBoost => jet.is_some_and(crate::jetpack::give_boost),
        // 0x00472800 -> 0x00475a30: refused at full health, else up to the maximum
        Kind::Health => {
            if h.0 >= r.max_health {
                return false;
            }
            // scaled by difficulty (Game+0x55c, gamedata.rs DIFFICULTIES)
            h.0 = (h.0 + p.amount * health_scale).min(r.max_health);
            true
        }
        Kind::Token => true,
    }
}

/// Touching pickups (0x00472310) and their update (PickupObject_Update 0x00472200): the pop and spin, linger and fade,
/// the player's collect and the AI's (health only, 0x00472850).
#[allow(clippy::too_many_arguments)]
fn collect(
    mut commands: Commands,
    time: Res<Time>,
    mut q: Query<(Entity, &mut Pickup, &mut Transform, Option<&Children>)>,
    mut player: Query<(&GlobalTransform, &mut Health, &mut crate::player::Roman, Option<&mut crate::jetpack::JetPack>), With<Player>>,
    mut ai: Query<(&GlobalTransform, &mut Health, &Grunt), Without<Player>>,
    mut mission: Option<ResMut<crate::mission::Mission>>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    (ui_texts, data, mut opts, mut ms, difficulty): (Res<crate::gamemenu::Texts>, Res<GameData>, ResMut<crate::options::GameOptions>, ResMut<crate::stats::MissionStats>, Res<crate::gamedata::Difficulty>),
    mut out: MessageWriter<crate::mission::SendTrigger>,
    mut mats: Query<&mut MeshMaterial3d<StandardMaterial>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    (mut cheats, mut upgrade): (ResMut<crate::cheats::Cheats>, ResMut<crate::weapons::VindalooUpgrade>),
) {
    let dt = time.delta_secs();
    let Ok((pt, mut h, mut r, mut jet)) = player.single_mut() else { return };
    let me = pt.translation() + Vec3::Y * 4.0;
    for (e, mut p, mut t, children) in &mut q {
        // the pop: upward_force up, the world's gravity (64.348 units/s^2) back down to the ground
        if p.placed.is_none() {
            p.vel -= 64.348 * dt;
            t.translation.y = (t.translation.y + p.vel * dt).max(p.ground + 1.0);
            t.translation += p.hvel * dt;
            if t.translation.y <= p.ground + 1.0 {
                p.vel = 0.0;
                p.hvel = Vec3::ZERO;
            }
        }
        // the spin about the vertical at rotate_rate (0x00472170 / 0x004720e0: the matrix spun about z by +0x4b4)
        t.rotate_y(dt * p.spin);
        // linger_time, then the fade over fade_time (vt+0x100(1 - f); under 0.01 s it goes at once)
        p.life -= dt;
        if p.life <= 0.0 && p.fading.is_none() {
            if p.fade_time < 0.01 {
                commands.entity(e).despawn();
                continue;
            }
            p.fading = Some(0.0);
        }
        let fade_step = dt / p.fade_time.max(0.01);
        if let Some(f) = p.fading.as_mut() {
            *f += fade_step;
            let f = *f;
            if f >= 1.0 {
                commands.entity(e).despawn();
                continue;
            }
            // the pickups' models draw with StandardMaterial: own copies, blended
            for c in children.into_iter().flatten() {
                if let Ok(mut mm) = mats.get_mut(*c) {
                    if !p.own_materials {
                        if let Some(copy) = materials.get(&mm.0).cloned() {
                            mm.0 = materials.add(StandardMaterial { alpha_mode: AlphaMode::Blend, ..copy });
                        }
                    }
                    if let Some(mut m) = materials.get_mut(&mm.0) {
                        m.base_color.set_alpha(1.0 - f);
                    }
                }
            }
            p.own_materials = true;
            commands.entity(e).insert(crate::camfade::ObjectAlpha(1.0 - f));
        }
        let at = t.translation;
        if at.distance(me) <= p.radius {
            // the player (vt+0x138): refused, it stays
            if !give(&p, &mut r, &mut h, jet.as_deref_mut(), &data, difficulty.factors().health_pickups, &mut upgrade) {
                continue;
            }
            if let (Some(m), Some(text)) = (mission.as_deref_mut(), p.text.as_ref()) {
                m.feed(&ui_texts.get(text));
            }
            if p.on_enter != 0 {
                out.write(crate::mission::SendTrigger(p.on_enter));
            }
            // Pickup_PlayHealthSound 0x00472790: Roman's "ROM50193_Ambient" for Q's tea, else the pickup sound;
            // Pickup_PlayAmmoSound 0x00472400
            if p.text.as_deref() == Some("pickup_healthytea") {
                sounds.write(crate::audio::PlaySound::at("ROM50193_Ambient", pt.translation()));
            } else {
                sounds.write(crate::audio::PlaySound::ui(if p.kind == Kind::Health { "ui_pickup_health" } else { "ui_pickup_ammo_1" }));
            }
            if p.kind == Kind::Token {
                // 0x00472a00 after 0x00472aa0: the token reward
                award_token(p.placed.clone(), TokenReward { opts: &mut opts, stats: &mut ms, texts: &ui_texts, difficulty: *difficulty, cheats: &mut cheats, mission: mission.as_deref_mut() });
            }
            if let Some(m) = mission.as_deref_mut() {
                m.log.push(format!("pickup collected: {:?} {}", p.kind, p.amount));
            }
            commands.entity(e).despawn();
            continue;
        }
        // anything else in its collision group (ai_can_trigger): vt+0x13c, only the health pickup's gives; never the
        // thrower (+0x4b8, set by 0x00472080, refused by 0x004720c0, released by 0x00472030): companions do not
        // take pickups here
        // (0x00472850: the raw num_health, refused when dead or at full health)
        if p.ai && p.kind == Kind::Health {
            for (gt, mut gh, g) in &mut ai {
                if g.mind == Mind::Dead || gh.0 <= 0.0 || gh.0 >= g.max_health {
                    continue;
                }
                if at.distance(gt.translation() + Vec3::Y * 4.0) <= p.radius {
                    gh.0 = (gh.0 + p.amount).min(g.max_health);
                    sounds.write(crate::audio::PlaySound::at("ui_pickup_health", at));
                    commands.entity(e).despawn();
                    break;
                }
            }
        }
    }
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

/// The tokens an unlockable costs at this difficulty (None: not in the table, or never: 99).
pub fn unlock_cost(name: &str, difficulty: crate::gamedata::Difficulty) -> Option<u32> {
    UNLOCKS.iter().find(|u| u.0.eq_ignore_ascii_case(name)).map(|u| u.2[difficulty.0.min(3)]).filter(|&c| c != 99)
}

/// The token message (fn_004a0850): "token_got" with the count, then the unlockable just paid for (the dearest
/// affordable, compared with the count before) or the next one with what it still needs; and that unlockable.
pub fn token_message(texts: &crate::gamemenu::Texts, count: u32, difficulty: crate::gamedata::Difficulty) -> (String, Option<&'static (&'static str, &'static str, [u32; 4])>) {
    let fill = |s: String, args: &[String]| {
        let mut out = s;
        for a in args {
            out = out.replacen("%d", a, 1).replacen("%s", a, 1);
        }
        out
    };
    let got = texts.get("token_got").replacen("%d", &count.to_string(), 1).replacen("%d", &TOKENS_TOTAL.to_string(), 1);
    let cost = |u: &(&str, &str, [u32; 4])| u.2[difficulty.0];
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
    (if line.is_empty() { got } else { format!("{got}<br>{line}") }, unlocked)
}

/// What the token reward changes: the profile, the level's Loot, the cheats an unlockable pays for, the mission's
/// message.
pub struct TokenReward<'a> {
    pub opts: &'a mut crate::options::GameOptions,
    pub stats: &'a mut crate::stats::MissionStats,
    pub texts: &'a crate::gamemenu::Texts,
    pub difficulty: crate::gamedata::Difficulty,
    pub cheats: &'a mut crate::cheats::Cheats,
    pub mission: Option<&'a mut crate::mission::Mission>,
}

/// The token reward (0x004a07a0 with category 2 -> 0x004a0850): the count (Game+0x200), the level's token taken
/// (`key`), 2500 Loot, the unlockable paid for (a cheat: Cheat_Unlock 0x00410e40 with enable 0), the paused message.
/// A placed token's pickup (0x00472a00) and a shot shootable_token (on_death_unlock_level, game_03.cpp:938-948,
/// icobject.rs) both give it.
pub fn award_token(key: Option<String>, r: TokenReward) {
    r.stats.loot_money += 2500.0;
    r.opts.tokens += 1;
    if let Some(k) = &key {
        r.opts.tokens_taken.push(k.clone());
    }
    r.opts.save();
    let (msg, unlocked) = token_message(r.texts, r.opts.tokens, r.difficulty);
    if let Some(u) = unlocked.filter(|u| u.1 == "token_cheat") {
        crate::cheats::unlock(u.0, false, r.opts, Some(r.cheats));
    }
    if let Some(m) = r.mission {
        m.log.push(format!("token {} found ({} of {TOKENS_TOTAL})", key.unwrap_or_default(), r.opts.tokens));
        m.message(&msg, 0.0);
    }
}

/// Whether the level's token is found (0x004a0720(level, 2): the level's name in the unlock table 2). The remaster keys
/// the taken tokens "<level>:<id>" (one placed token per level, none on the defence levels, whose token is the bombers'
/// shootable_token): any key of the level counts.
pub fn level_token_found(opts: &crate::options::GameOptions, level_dir: &std::path::Path) -> bool {
    let prefix = format!("{}:", token_level(level_dir));
    opts.tokens_taken.iter().any(|k| k.starts_with(&prefix))
}

/// The key of a level's token that is not a placed pickup (a shot shootable_token): "<level>:shot".
pub fn shot_token_key(level_dir: &std::path::Path) -> String {
    format!("{}:shot", token_level(level_dir))
}

/// The level part of a legacy token key (options.rs `tokens_keyed_by_level`): taken on some level, with that id.
pub const ANY_LEVEL: &str = "any";

/// A placed pickup's persistent key: its level (the export folder's name) and instance id. It used to say "lev_1_m1"
/// on every level, so a token taken on one level hid the token of the same id on every other.
fn token_key(level_dir: &std::path::Path, id: u32) -> String {
    format!("{}:{id}", token_level(level_dir))
}

/// The level part of a token key: the export folder's name.
fn token_level(level_dir: &std::path::Path) -> &str {
    level_dir.file_name().and_then(|n| n.to_str()).unwrap_or("level")
}

/// A pickup placed in the level (PickupObject, behaviour pickup_*, or an ammo pickup's template name): the same
/// pickup, staying where it is placed. A token already taken removes itself (0x00472a00: 0x004a0720 asks whether the
/// level's token was found).
fn placed_pickups(
    mut commands: Commands,
    q: Query<(Entity, &crate::level::Placed, &crate::level::InstanceId, &Transform), Added<crate::level::Placed>>,
    data: Res<GameData>,
    opts: Res<crate::options::GameOptions>,
    lib_dir: Res<crate::skin::AnimLibrary>,
    mut own: crate::levelreset::PerLevel<Option<std::collections::HashMap<u32, serde_json::Map<String, serde_json::Value>>>>,
) {
    if q.is_empty() {
        return;
    }
    // the instances' own property values over the template's (an instance's on_enter: lev_4_m3's jumper packs send
    // 3997)
    let own = own.get().get_or_insert_with(|| {
        let text = std::fs::read_to_string(lib_dir.dir.join("instance_props.json")).unwrap_or_default();
        let v: Vec<serde_json::Value> = serde_json::from_str(&text).unwrap_or_default();
        v.iter()
            .filter_map(|i| Some((i.get("id")?.as_u64()? as u32, i.get("props").and_then(|p| p.as_object()).cloned().unwrap_or_default())))
            .collect()
    });
    for (e, placed, id, t) in &q {
        let p = own.get(&id.0).map_or_else(|| data.template_props(&placed.template), |o| data.instance_props(&placed.template, o));
        let class = p.s("behavior").map(str::to_owned).unwrap_or_else(|| placed.template.clone());
        if !class.starts_with("pickup") || class == "pickup_generator" {
            continue;
        }
        let key = token_key(&lib_dir.dir, id.0);
        let legacy = format!("{ANY_LEVEL}:{}", id.0);
        if class == "pickup_token" && (opts.tokens_taken.contains(&key) || opts.tokens_taken.contains(&legacy)) {
            commands.entity(e).despawn();
            continue;
        }
        let mut pk = Pickup::new(&class, &p);
        pk.placed = Some(key);
        pk.ground = t.translation.y - 1.0;
        commands.entity(e).insert(pk);
    }
}

/// The playtest state: live pickups by kind and the generators.
fn state_log(q: Query<&Pickup>, gens: Res<Generators>, state: Option<ResMut<crate::playtest::PlayState>>, opts: Res<crate::options::GameOptions>) {
    let Some(mut state) = state else { return };
    state.0.insert("tokens".into(), opts.tokens.into());
    state.0.insert("cheats_known".into(), opts.cheats_known.len().into());
    let mut kinds = serde_json::Map::new();
    for p in &q {
        let k = format!("{:?}", p.kind).to_lowercase().replace(['(', ')'], "");
        let n = kinds.get(&k).and_then(|v| v.as_u64()).unwrap_or(0);
        kinds.insert(k, (n + 1).into());
    }
    state.0.insert("pickups".into(), q.iter().count().into());
    state.0.insert("pickup_kinds".into(), kinds.into());
    let g: serde_json::Map<String, serde_json::Value> = gens
        .0
        .iter()
        .flatten()
        .map(|g| (g.id.to_string(), serde_json::json!({"has": g.current.is_some(), "made": g.made, "listening": g.listen != 0, "timer": (g.timer * 10.0).round() / 10.0})))
        .collect();
    state.0.insert("generators".into(), g.into());
}

/// Playtest steps: `pickup` (a pickup of that template at Roman's feet), `loadout`: {"guns": ["sharkgun", "rifle"] (the
/// slots in order, each full; the first in hand), "in_hand": "rifle", "<gun>": its spare ammo (giving the gun), "<gun>_clip",
/// "<special>": its count (stickybomb / topsyturvy / blackhole / guyfawkes / bunchoffives), "special": the special in
/// hand, "upgrade": the vindaloo upgrade, "health"}; the older keys "stickies", "topsy", "mg", "mg_clip", "use_mg",
/// "rifle_clip" still work.
fn playtest_steps(
    mut pt: ResMut<crate::playtest::Playtest>,
    mut roman: Query<(&GlobalTransform, &mut crate::player::Roman, &mut Health)>,
    mut out: MessageWriter<SpawnPickup>,
    data: Res<GameData>,
    mut upgrade: ResMut<crate::weapons::VindalooUpgrade>,
) {
    use crate::weapons::{Gun, GunSpec, SpecialId, WeaponId};
    let Ok((t, mut r, mut h)) = roman.single_mut() else { return };
    for name in std::mem::take(&mut pt.pickups) {
        out.write(SpawnPickup { name, at: t.translation() + Vec3::Y * 4.0, vel: Vec3::ZERO, hover: true });
    }
    for l in std::mem::take(&mut pt.loadouts) {
        let n = |k: &str| l.get(k).and_then(|v| v.as_f64());
        let full = |id: WeaponId| Gun::full(id, GunSpec::load(&data, id));
        if let Some(list) = l.get("guns").and_then(|v| v.as_array()) {
            let specials = r.arsenal.clone();
            r.arsenal.clear();
            for id in list.iter().filter_map(|v| v.as_str()).filter_map(WeaponId::from_name) {
                r.arsenal.add(full(id));
            }
            r.arsenal.equip_slot(0);
            for s in SpecialId::ALL {
                r.arsenal.set_count(s, specials.count(s));
            }
            r.arsenal.special = specials.special;
        }
        // the older keys
        for (key, id) in [("mg", WeaponId::Machinegun)] {
            if let Some(v) = n(key) {
                if !r.arsenal.has(id) && !r.arsenal.add(full(id)) {
                    let old = r.arsenal.at(2).map(|g| g.id);
                    r.arsenal.replace(old, full(id));
                }
                if let Some(w) = r.arsenal.gun_mut(id) {
                    w.ammo = v as u32;
                }
            }
        }
        for id in WeaponId::ALL {
            if let Some(v) = n(id.name()) {
                if !r.arsenal.has(id) && !r.arsenal.add(full(id)) {
                    let old = r.arsenal.at(2).map(|g| g.id);
                    r.arsenal.replace(old, full(id));
                }
                if let Some(w) = r.arsenal.gun_mut(id) {
                    w.ammo = v as u32;
                }
            }
            let clip_key = if id == WeaponId::Machinegun { "mg_clip".to_string() } else { format!("{}_clip", id.name()) };
            if let (Some(v), Some(w)) = (n(&clip_key), r.arsenal.gun_mut(id)) {
                w.clip = v as u32;
            }
        }
        if let Some(v) = l.get("use_mg").and_then(|v| v.as_bool()) {
            r.arsenal.equip(if v { WeaponId::Machinegun } else { WeaponId::Rifle });
        }
        if let Some(id) = l.get("in_hand").and_then(|v| v.as_str()).and_then(WeaponId::from_name) {
            r.arsenal.equip(id);
        }
        for (key, s) in [("stickies", SpecialId::StickyBomb), ("topsy", SpecialId::TopsyTurvy)].into_iter().chain(SpecialId::ALL.map(|s| (s.name(), s))) {
            if let Some(v) = n(key) {
                r.arsenal.set_count(s, v as u32);
            }
        }
        if let Some(s) = l.get("special").and_then(|v| v.as_str()).and_then(SpecialId::from_name) {
            r.arsenal.special = s;
        }
        if let Some(v) = l.get("upgrade").and_then(|v| v.as_bool()) {
            upgrade.0 = v;
        }
        if let Some(v) = n("health") {
            h.0 = v as f32;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn gun(clip: u32, ammo: u32, unlimited: bool) -> crate::weapons::Gun {
        let spec = crate::weapons::GunSpec { max_clip: if unlimited { 8 } else { 100 }, max_ammo: if unlimited { 62 } else { 1000 }, unlimited, ..Default::default() };
        crate::weapons::Gun { id: crate::weapons::WeaponId::Machinegun, spec, clip, ammo, timer: 0.0 }
    }

    #[test]
    fn ammo_fills_spare_then_clip() {
        let mut w = gun(10, 990, false);
        assert!(w.add_ammo(42));
        assert_eq!((w.ammo, w.clip), (1000, 42));
        let mut full = gun(100, 1000, false);
        assert!(!full.add_ammo(42), "refused when both are full");
        let mut rifle = gun(3, 62, true);
        assert!(rifle.add_ammo(16));
        assert_eq!(rifle.clip, 8);
        assert!(!rifle.add_ammo(16));
    }

    #[test]
    fn kinds_by_class() {
        assert_eq!(Kind::of("pickup_sticky_bombs"), Kind::Special(10));
        assert_eq!(Kind::of("pickup_health"), Kind::Health);
        assert_eq!(Kind::of("pickup_rifle_bullets"), Kind::Ammo(2));
        assert_eq!(Kind::of("pickup_token"), Kind::Token);
    }
}
