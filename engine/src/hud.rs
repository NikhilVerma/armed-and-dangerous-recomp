//! The in-game HUD from the game's own UI data (tools/remaster_export_ui.py; docs/subsystems/hud.md): element
//! positions in the original 640x480 layout (y up), their quad meshes and textures, the bitmap font. Drawn as a
//! 2D overlay, scaled by screen height / 480 with left / right anchors pinned to the screen edges (the remaster's
//! widescreen rule, hook/enhance/ui_aspect.cpp). Health meters empty by scrolling their texture (3.2), text has a
//! 2 px black shadow, lines are 25 x scale px at 480.

use crate::levelreset::LevelAppExt;
use bevy::asset::RenderAssetUsages;
use bevy::camera::visibility::RenderLayers;
use bevy::math::Affine2;
use bevy::mesh::{Indices, PrimitiveTopology};
use bevy::prelude::*;
use bevy::sprite_render::{ColorMaterial, MeshMaterial2d};
use crate::sdffont::FontMaterial;
use serde::Deserialize;
use std::collections::HashMap;
use std::path::Path;

#[derive(Deserialize, Clone)]
struct Quad {
    texture: Option<String>,
    colour: Option<String>,
    vertices: Vec<[f32; 4]>,
    triangles: Vec<[u32; 3]>,
    #[serde(default)]
    z: f32,
}

#[derive(Deserialize, Clone)]
struct ElementDef {
    name: String,
    kind: String,
    #[serde(default)]
    page_name: Option<String>,
    /// null for the few elements with no layout point
    #[serde(default)]
    pos: Option<[f32; 2]>,
    #[serde(default)]
    anchor: Option<String>,
    #[serde(default)]
    model: Option<String>,
    #[serde(default)]
    string: Option<String>,
    #[serde(default)]
    scale: Option<f32>,
    #[serde(default)]
    colour: Option<String>,
    /// the ref node the element sits on (ref_slot_01, ref_weapon_02, ...)
    #[serde(default, rename = "ref")]
    ref_name: Option<String>,
    /// the element's record id and its neighbours' ids (+0x478..+0x488: id, left, right, up, down; -1 none)
    #[serde(default)]
    nav: Option<NavDef>,
}

#[derive(Deserialize, Clone, Copy)]
struct NavDef {
    id: i32,
    left: i32,
    right: i32,
    up: i32,
    down: i32,
}

#[derive(Deserialize)]
struct TexDef {
    file: String,
}

#[derive(Deserialize)]
struct LayoutFile {
    models: HashMap<String, Vec<Quad>>,
    textures: HashMap<String, TexDef>,
    elements: Vec<ElementDef>,
    /// the speaker table 0x005cadd8: subtitle colour per speaker
    #[serde(default)]
    captions: Vec<Caption>,
}

#[derive(Deserialize, Clone)]
struct Caption {
    speaker: String,
    colour: String,
}

#[derive(Deserialize, Clone, Copy)]
struct Glyph {
    x: f32,
    y: f32,
    width: f32,
    height: f32,
    #[serde(default)]
    xoffset: f32,
    xadvance: f32,
}

#[derive(Deserialize)]
struct FontCommon {
    line_height: f32,
    scale_w: f32,
    scale_h: f32,
}

#[derive(Deserialize)]
struct FontFile {
    common: FontCommon,
    chars: HashMap<String, Glyph>,
}

/// The input prompt icons (tools/remaster_export_icons.py: Kenney Input Prompts, CC0), drawn for `<icon=name>`.
#[derive(Deserialize)]
struct IconAtlas {
    width: f32,
    height: f32,
    icons: HashMap<String, [f32; 4]>,
}

/// An extra scale on an element (the pub's award icon growing, Object_SetScale 0x004535d0); 1 when absent.
#[derive(Component, Clone, Copy)]
pub struct ElementScale(pub f32);

/// One drawn HUD element and how to place it.
#[derive(Component, Clone)]
pub struct HudElement {
    pub name: String,
    pub pos: Vec2,
    pub anchor: Anchor,
    /// laid out on the original's whole 640 x 480 (menus, the pub screen), not the letterbox band of the game view
    pub full: bool,
}

#[derive(Clone, Copy, PartialEq)]
pub enum Anchor {
    Left,
    Right,
    Centre,
}

/// A HUD text: its game string id, scale, colour; rebuilt when `text` changes.
#[derive(Component)]
pub struct HudText {
    pub text: String,
    pub built: Option<String>,
    pub scale: f32,
    pub colour: Color,
    /// fit to this many 640-wide units (prompts: 576), 0 = no fit
    pub fit: f32,
}

/// Where a text sits on its position: 0 centred, -1 its left edge, 1 its right edge.
#[derive(Component)]
pub struct TextAlign(pub f32);

/// A built text's size in 640 x 480 units (UiText_Layout 0x00465680 measures it into +0x4d8 / +0x4dc): the menus'
/// hit boxes (gamemenu.rs).
#[derive(Component, Clone, Copy)]
pub struct TextSize(pub Vec2);

/// Meter quads (texture scroll by value).
#[derive(Component)]
pub struct Meter {
    pub material: Handle<ColorMaterial>,
}

#[derive(Resource)]
pub struct HudAssets {
    layout: LayoutFile,
    font: FontFile,
    textures: HashMap<String, Handle<Image>>,
    /// the glyphs (white, coloured by the text mesh's vertex colours) and their black shadow, drawn from the font's
    /// distance field (sdffont.rs: sharp at any resolution)
    text_material: Handle<FontMaterial>,
    shadow_material: Handle<FontMaterial>,
    icons: Option<IconAtlas>,
    icon_material: Option<Handle<ColorMaterial>>,
    /// the level's export folder (models/ and skeletons/ for the crosshairs' bones)
    dir: std::path::PathBuf,
}

/// What the HUD shows this frame (filled by the game systems).
#[derive(Resource, Default)]
pub struct HudState {
    pub visible: bool,
    /// a cinematic (camera path, Game+0xdce): the health meters are hidden, the rest of the HUD stays (hud.md)
    pub cinematic: bool,
    pub health: f32,
    pub q_health: f32,
    pub jonesy_health: f32,
    pub clip: u32,
    pub stickies: u32,
    pub heading: f32,
    /// bearing of the current compass target relative to the camera (radians) and its distance
    pub objective: Option<(f32, f32)>,
    /// new messages for the chat feed (Chat_AddMessage 0x0046aa60: text, colour index into the table of 0x0046a7e0);
    /// `chat` drains them into its three lines
    pub chat_in: Vec<(String, u8)>,
    /// how many primary weapons and specials Roman carries (0x004a1610 / 0x004a1640: the weapon-cycle arrows)
    pub weapons: u32,
    pub specials: u32,
    pub prompt: Option<String>,
    pub briefing: Option<String>,
    pub hit_flash: [f32; 4],
    /// the speech subtitle: text, speaker colour, seconds left (in-game subtitles option)
    pub subtitle: Option<(String, Color, f32)>,
    pub subtitles_off: bool,
    /// the weapon in hand ("rifle", "machinegun"), its reserve, and whether the reserve is unlimited
    pub weapon: &'static str,
    pub ammo: u32,
    pub unlimited: bool,
    /// the ticker bomb: arming progress 0..1 while Use is held; seconds since armed while it ticks
    pub ticker_arming: Option<f32>,
    pub ticker_clock: Option<f32>,
    /// the special in hand (0 sticky bombs, 1 Topsy Turvy) and the Topsy Turvy count
    pub special: u8,
    pub topsy: u32,
    /// the team order's text (ui_teammate_*: its layout string) and seconds left (3.0, 0x00467890)
    pub team: Option<(&'static str, f32)>,
}

pub const HUD_LAYER: usize = 1;

pub struct HudPlugin;

impl Plugin for HudPlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<HudState>()
            .init_level_resource::<PubView>()
            .init_level_resource::<Crosshair>()
            .init_level_resource::<Chat>()
            // in PostUpdate, after every game system of the frame has set HudState (the prompt is written by several
            // unordered Update systems: reading it in Update could see it half-set and flicker), before the
            // transforms propagate
            .add_systems(
                PostUpdate,
                (subtitles, chat, crosshair_aim.run_if(crate::playing), crosshair_pose, place, texts, meters, compass, compass_arms, ticker, pub_view, visibility, log_hud)
                    .chain()
                    .before(bevy::transform::TransformSystems::Propagate),
            );
    }
}

fn parse_colour(s: Option<&str>) -> Color {
    match s.and_then(|s| u32::from_str_radix(s.trim_start_matches('#'), 16).ok()) {
        // black on a textured quad = no tint (the texture's own colour)
        None | Some(0) => Color::WHITE,
        Some(v) => Color::srgb_u8((v >> 16) as u8, (v >> 8) as u8, v as u8),
    }
}

/// Loads the layout and font and spawns the lev_1_m1 HUD elements under a 2D overlay camera.
pub fn spawn(
    commands: &mut Commands,
    dir: &Path,
    assets: &AssetServer,
    meshes: &mut Assets<Mesh>,
    materials: &mut Assets<ColorMaterial>,
    (images, font_materials): (&mut Assets<Image>, &mut Assets<FontMaterial>),
) -> Option<()> {
    let ui = dir.join("ui");
    let layout: LayoutFile = serde_json::from_str(&std::fs::read_to_string(ui.join("layout.json")).ok()?).map_err(|e| warn!("hud: layout.json: {e}")).ok()?;
    let font: FontFile = serde_json::from_str(&std::fs::read_to_string(ui.join("font.json")).ok()?).map_err(|e| warn!("hud: font.json: {e}")).ok()?;
    let font_field = images.add(crate::sdffont::font_field(&ui.join("font.png"))?);
    let mut textures = HashMap::new();
    for (name, t) in &layout.textures {
        textures.insert(name.clone(), assets.load(format!("ui/{}", t.file)));
    }
    let text_material = font_materials.add(FontMaterial { color: LinearRgba::WHITE, field: font_field.clone() });
    let shadow_material = font_materials.add(FontMaterial { color: LinearRgba::BLACK, field: font_field });
    commands.spawn((
        Camera2d,
        Camera { order: 1, clear_color: ClearColorConfig::None, ..default() },
        RenderLayers::layer(HUD_LAYER),
    ));
    let icons: Option<IconAtlas> = std::fs::read_to_string(ui.join("icons.json")).ok().and_then(|s| serde_json::from_str(&s).map_err(|e| warn!("hud: icons.json: {e}")).ok());
    let icon_material = icons.as_ref().map(|_| materials.add(ColorMaterial { color: Color::WHITE, texture: Some(assets.load("ui/icons.png")), ..default() }));
    let hud = HudAssets { layout, font, textures, text_material, shadow_material, icons, icon_material, dir: dir.to_path_buf() };

    // the elements of the in-game HUD (page pui_main) this level uses
    const SHOWN: &[&str] = &[
        "ui_weapon_bkd", "ui_player_rifle", "ui_player_grenades", "ui_player_health", "ui_health_q", "ui_health_jonesy",
        "ui_crosshair", "ui_compass", "ui_compass_objective", "ui_ammo_infinity", "ui_ammo_count", "ui_grenade_count",
        "ui_hit_top", "ui_hit_bottom", "ui_hit_left", "ui_hit_right", "ui_player_mg", "ui_player_topsy", "ui_clock", "ui_tickerbomb",
        "ui_tickerbomb_set", "ui_mg_crosshair", "ui_crosshair_friendly", "ui_special_crosshair",
        // the weapon-cycle arrows (0x00463fa0, the HUD's +0x5c..+0x78 by name, strings 0x005a0dac..0x005a0d20)
        "ui_select_up_on", "ui_select_down_on", "ui_select_left_on", "ui_select_right_on", "ui_select_up_off", "ui_select_down_off",
        "ui_select_left_off", "ui_select_right_off",
        // the message feed's three lines (Chat_Update 0x0046a820 finds ui_chat_01..03 by name)
        "ui_chat_01", "ui_chat_02", "ui_chat_03",
    ];
    for e in hud.layout.elements.iter().filter(|e| SHOWN.contains(&e.name.as_str()) && e.pos.is_some() && e.page_name.as_deref().is_none_or(|p| p == "pui_main")) {
        let anchor = match e.anchor.as_deref() {
            // the chat lines are centred on x 320 (hud.md 4, open question 6: ui_aspect.cpp lists them left)
            _ if e.name.starts_with("ui_chat_") => Anchor::Centre,
            Some("left") => Anchor::Left,
            Some("right") => Anchor::Right,
            _ => Anchor::Centre,
        };
        let el = HudElement { name: e.name.clone(), pos: Vec2::from(e.pos.unwrap_or([320.0, 240.0])), anchor, full: false };
        if e.kind == "text" {
            commands.spawn((
                el,
                HudText { text: String::new(), built: None, scale: e.scale.unwrap_or(1.0), colour: parse_colour(e.colour.as_deref()), fit: 0.0 },
                Transform::default(),
                Visibility::default(),
                RenderLayers::layer(HUD_LAYER),
            ));
            continue;
        }
        let Some(quads) = e.model.as_ref().and_then(|m| hud.layout.models.get(m)) else { continue };
        let root = commands.spawn((el, Transform::default(), Visibility::default(), RenderLayers::layer(HUD_LAYER))).id();
        let _ = quads;
        let parts = build_model(commands, meshes, materials, &hud, &e.name, e.model.as_deref().unwrap_or_default());
        commands.entity(root).add_children(&parts);
    }
    spawn_pub(commands, meshes, materials, &hud, &ui);
    // the prompt (ref_tickerbomb_text 321.6, 329.8, scale 1.0; its colour the prompt text's own, `prompt_style`,
    // fitted to 576 units: 0x00465720) and the briefing (320, 281.1 over ui_text_bkd)
    for (name, pos, scale, colour) in [
        ("ui_prompt", Vec2::new(321.6, 329.8), 1.0, Color::WHITE),
        ("ui_briefing", Vec2::new(320.0, 281.1), 0.9, Color::srgb_u8(0xd0, 0xd0, 0xff)),
        // speech subtitles (0x00495cb0): size 1.1 fitted to 576 units, centred at x 0.5, y 0.15 of the screen
        ("ui_subtitle", Vec2::new(320.0, 480.0 * 0.15), 1.1, Color::WHITE),
        // the team order texts ui_teammate_* (ref_team_text 320, 47.5; 0.9 #ffff80), one at a time
        ("ui_team", Vec2::new(320.0, 47.5), 0.9, Color::srgb_u8(0xff, 0xff, 0x80)),
    ] {
        commands.spawn((
            HudElement { name: name.into(), pos, anchor: Anchor::Centre, full: false },
            HudText { text: String::new(), built: None, scale, colour, fit: if name == "ui_subtitle" || name == "ui_prompt" { 576.0 } else { 0.0 } },
            Transform::default(),
            Visibility::default(),
            RenderLayers::layer(HUD_LAYER),
        ));
    }
    commands.insert_resource(hud);
    Some(())
}

/// The quads of a UI bank model as children (mesh, texture or flat colour, the original's z order); the parts
/// that are driven get their markers (health meter, compass needle, clock hand, arming bar, pub meters).
fn build_model(commands: &mut Commands, meshes: &mut Assets<Mesh>, materials: &mut Assets<ColorMaterial>, hud: &HudAssets, el_name: &str, model: &str) -> Vec<Entity> {
    let mut out = Vec::new();
    let Some(quads) = hud.layout.models.get(model) else { return out };
    let bones = if CROSSHAIRS.contains(&el_name) { cross_bones(&hud.dir, model) } else { Vec::new() };
    for (qi, q) in quads.iter().enumerate() {
        if q.vertices.is_empty() {
            continue;
        }
        let mut mesh = Mesh::new(PrimitiveTopology::TriangleList, RenderAssetUsages::default());
        mesh.insert_attribute(Mesh::ATTRIBUTE_POSITION, q.vertices.iter().map(|v| [v[0], v[1], 0.0]).collect::<Vec<_>>());
        // exported uv: v = 0 is the bottom row of the upright image; Bevy samples v = 0 at the top
        mesh.insert_attribute(Mesh::ATTRIBUTE_UV_0, q.vertices.iter().map(|v| [v[2], 1.0 - v[3]]).collect::<Vec<_>>());
        mesh.insert_indices(Indices::U32(q.triangles.iter().flatten().copied().collect()));
        let tex = q.texture.as_ref().and_then(|t| hud.textures.get(t)).cloned();
        // textured quads keep the texture's colour (vertex colours are alpha / unused there); flat quads use theirs,
        // stored blue-first (the compass's objective half-disc #001eff is the orange of the original captures)
        let color = if tex.is_some() {
            Color::WHITE
        } else {
            let c = parse_colour(q.colour.as_deref().or(Some("#ffffff"))).to_srgba();
            let a = if el_name == "ui_compass_objective" { 0.55 } else { 1.0 };
            Color::srgba(c.blue, c.green, c.red, a)
        };
        let mat = materials.add(ColorMaterial { color, texture: tex, alpha_mode: bevy::sprite_render::AlphaMode2d::Blend, ..default() });
        let mesh = meshes.add(mesh);
        let mut part = commands.spawn((
            Mesh2d(mesh.clone()),
            MeshMaterial2d(mat.clone()),
            // the original's z order: more negative z draws further back
            Transform::from_xyz(0.0, 0.0, 10.0 + q.z),
            RenderLayers::layer(HUD_LAYER),
        ));
        match (q.texture.as_deref(), el_name) {
            (Some("b_health"), _) => {
                part.insert(Meter { material: mat });
            }
            (None, "ui_compass") => {
                part.insert(CompassNeedle);
            }
            (None, "ui_clock") => {
                part.insert(ClockHand);
            }
            (Some("b_tickerbomb_set"), _) => {
                part.insert(TickerBar { material: mat });
            }
            (Some("b_health_level"), "ui_pub_health" | "ui_pub_healthpool") => {
                part.insert(PubMeter { pool: el_name == "ui_pub_healthpool", material: mat });
            }
            _ => {}
        }
        if let Some(b) = bones.get(qi).filter(|b| b.len() == q.vertices.len()) {
            let verts = q.vertices.iter().zip(b).map(|(v, (bone, bind))| (Vec2::new(v[0], v[1]), bone.clone(), *bind)).collect();
            part.insert(CrossPart { element: el_name.to_owned(), mesh: mesh.clone(), verts, applied: None });
        }
        // the objective pointer's fan: its arms close with the distance (0x00466790, anim 4)
        if el_name == "ui_compass_objective" {
            if let Some(rig) = arm_rig(&hud.dir, model).into_iter().nth(qi).filter(|r| r.len() == q.vertices.len()) {
                let verts = q.vertices.iter().zip(rig).map(|(v, w)| (Vec2::new(v[0], v[1]), w)).collect();
                part.insert(ArmPart { mesh, verts, applied: None });
            }
        }
        out.push(part.id());
    }
    out
}

/// The ticker-bomb clock's hand (the flat part of ui_clock, bone Bone01 of the ui_clock anim).
#[derive(Component)]
pub struct ClockHand;

/// The ticker-bomb arming bar (b_tickerbomb_set, a slide meter like health: 0x004672d0).
#[derive(Component)]
pub struct TickerBar {
    material: Handle<ColorMaterial>,
}

/// The pub screen (page pui_pub): every part, shown while the screen is open.
#[derive(Component)]
pub struct PubPart;

/// What a pub slot holds (the marker kinds +0x4a4 that 0x00467fe0 gives the 14 ref markers): Roman's weapons
/// (ref_weapon_*: 0), his specials (ref_sp_weapon_*: 1), the pub's inventory (ref_slot_*: 2), the ticker bombs
/// (ref_mission_bombs: 3).
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub enum PubSlotKind {
    Weapon,
    Special,
    Shop,
    Bombs,
}

impl PubSlotKind {
    fn of_ref(r: &str) -> Option<Self> {
        if r.starts_with("ref_weapon_") {
            Some(Self::Weapon)
        } else if r.starts_with("ref_sp_weapon_") {
            Some(Self::Special)
        } else if r.starts_with("ref_slot_") {
            Some(Self::Shop)
        } else if r == "ref_mission_bombs" {
            Some(Self::Bombs)
        } else {
            None
        }
    }
}

/// A pub slot frame (a ui_slot on one of the 14 ref markers), in the bank's order within its kind.
#[derive(Component, Clone, Copy)]
pub struct PubSlot {
    pub kind: PubSlotKind,
    /// its place among the slots of its kind (the bank's order: ref_slot_01 first)
    pub order: usize,
    /// the slot's record id and its neighbours' (id, left, right, up, down; -1 none): the keys move the cursor
    /// along them (0x004695d0 with 0x00463b90)
    pub nav: [i32; 5],
}

#[derive(Component)]
pub struct PubHighlight;

/// The big icon of an award (ui_pub_award_<item>), shown with its line.
#[derive(Component)]
pub struct PubAward(pub String);

#[derive(Component)]
pub struct PubMeter {
    pool: bool,
    material: Handle<ColorMaterial>,
}

/// A pub text that changes: "message" (awards, notices), "info" (the highlighted weapon), "exit".
#[derive(Component)]
pub struct PubText(pub &'static str);

#[derive(Component)]
pub struct PubIcons;

/// One item icon of the pub screen (its index in PubView::icons).
#[derive(Component)]
struct PubIcon(usize);

/// What the pub screen shows (pubscreen.rs fills it); positions in the 640 x 480 layout.
#[derive(Resource, Default)]
pub struct PubView {
    pub open: bool,
    pub message: String,
    pub info: String,
    pub exit_text: String,
    /// where the cursor (ui_pub_cursor) is
    pub cursor: Vec2,
    /// (position, texture) of each item shown
    pub icons: Vec<(Vec2, &'static str)>,
    /// the award being shown (the suffix of ui_pub_award_*), or empty; where and how big
    pub award: String,
    pub award_pos: Vec2,
    pub award_scale: f32,
    pub health: f32,
    pub pool: f32,
    /// bumped when the icons' textures change
    pub version: u32,
}

/// Spawns the pub page: art, slots, bars and fixed labels from the bank; the changing texts at their bank texts'
/// positions (the award / notice line at pub_notify_new, the weapon info at pub_info_*, the exit prompt).
fn spawn_pub(commands: &mut Commands, meshes: &mut Assets<Mesh>, materials: &mut Assets<ColorMaterial>, hud: &HudAssets, ui: &Path) {
    let strings: HashMap<String, String> = std::fs::read_to_string(ui.join("texts.json")).ok().and_then(|s| serde_json::from_str(&s).ok()).unwrap_or_default();
    // guess: a dark backdrop behind the pub art (what shows through its open middle in the original is not known)
    let shade = materials.add(ColorMaterial { color: Color::srgba(0.0, 0.0, 0.0, 0.75), ..default() });
    let backdrop = commands.spawn((Mesh2d(meshes.add(Rectangle::new(4000.0, 480.0))), MeshMaterial2d(shade), Transform::from_xyz(0.0, 0.0, 1.0), RenderLayers::layer(HUD_LAYER))).id();
    commands
        .spawn((
            HudElement { name: "pub_backdrop".into(), pos: Vec2::new(320.0, 240.0), anchor: Anchor::Centre, full: true },
            PubPart,
            crate::gamemenu::MenuOwned,
            Transform::from_xyz(0.0, 0.0, -2.0),
            Visibility::Hidden,
            RenderLayers::layer(HUD_LAYER),
        ))
        .add_child(backdrop);
    let mut order: HashMap<PubSlotKind, usize> = HashMap::new();
    for e in hud.layout.elements.iter().filter(|e| e.page_name.as_deref() == Some("pui_pub") && e.pos.is_some()) {
        let pos = Vec2::from(e.pos.unwrap_or([320.0, 240.0]));
        let el = HudElement { name: e.name.clone(), pos, anchor: Anchor::Centre, full: true };
        if e.kind == "text" {
            let n = e.name.as_str();
            let (text, marker) = if n == "pub_info_rifle" {
                (String::new(), Some("info"))
            } else if n.starts_with("pub_info_") || n.starts_with("pub_award_") || (n.starts_with("pub_notify_") && n != "pub_notify_new" && n != "pub_notify_exit") {
                continue;
            } else if n == "pub_notify_new" {
                (String::new(), Some("message"))
            } else if n == "pub_notify_exit" {
                (String::new(), Some("exit"))
            } else {
                (strings.get(n).cloned().unwrap_or_default(), None)
            };
            let scale = e.scale.filter(|s| *s > 0.0).unwrap_or(0.8);
            let mut c = commands.spawn((
                el,
                HudText { text, built: None, scale, colour: parse_colour(e.colour.as_deref()), fit: 0.0 },
                PubPart,
                crate::gamemenu::MenuOwned,
                Transform::from_xyz(0.0, 0.0, 3.0),
                Visibility::Hidden,
                RenderLayers::layer(HUD_LAYER),
            ));
            if let Some(m) = marker {
                c.insert(PubText(m));
            }
            continue;
        }
        let Some(model) = e.model.clone().filter(|m| hud.layout.models.contains_key(m)) else { continue };
        // 0x00467fe0 hides ui_pub_highlight and moves the cursor (ui_pub_cursor, model ui_pub_select)
        if e.name == "ui_pub_highlight" {
            continue;
        }
        // the interior behind everything, the cursor over the slots
        let z = match e.name.as_str() {
            "ui_pub_interior" => -1.0,
            "ui_pub_cursor" => 1.0,
            _ => 0.0,
        };
        let root = commands
            .spawn((el, PubPart, crate::gamemenu::MenuOwned, Transform::from_xyz(0.0, 0.0, z), Visibility::Hidden, RenderLayers::layer(HUD_LAYER)))
            .id();
        match e.name.as_str() {
            "ui_slot" => {
                if let Some(kind) = e.ref_name.as_deref().and_then(PubSlotKind::of_ref) {
                    let nav = e.nav.map_or([-1; 5], |n| [n.id, n.left, n.right, n.up, n.down]);
                    let n = order.entry(kind).or_default();
                    commands.entity(root).insert(PubSlot { kind, order: *n, nav });
                    *n += 1;
                }
            }
            "ui_pub_cursor" => {
                commands.entity(root).insert(PubHighlight);
            }
            n if n.starts_with("ui_pub_award_") => {
                commands.entity(root).insert((PubAward(n.trim_start_matches("ui_pub_award_").to_owned()), ElementScale(1.0)));
            }
            _ => {}
        }
        let parts = build_model(commands, meshes, materials, hud, &e.name, &model);
        commands.entity(root).add_children(&parts);
    }
    commands.spawn((
        HudElement { name: "pub_icons".into(), pos: Vec2::new(320.0, 240.0), anchor: Anchor::Centre, full: true },
        PubPart,
        PubIcons,
        crate::gamemenu::MenuOwned,
        Transform::from_xyz(0.0, 0.0, 2.0),
        Visibility::Hidden,
        RenderLayers::layer(HUD_LAYER),
    ));
}

/// The size of an item icon in a 96 x 48 slot, by texture (the textures' aspect).
fn icon_size(tex: &str) -> Vec2 {
    match tex {
        "k_player_rifle" => Vec2::new(77.0, 19.0),
        "k_player_topsyturvybomb" => Vec2::new(40.0, 40.0),
        _ => Vec2::new(72.0, 36.0),
    }
}

#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn pub_view(
    mut commands: Commands,
    view: Res<PubView>,
    hud: Option<Res<HudAssets>>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<ColorMaterial>>,
    mut parts: Query<(&mut Visibility, Option<&PubAward>), With<PubPart>>,
    mut texts: Query<(&PubText, &mut HudText)>,
    mut hl: Query<&mut HudElement, With<PubHighlight>>,
    mut awards: Query<(&mut HudElement, &mut ElementScale), (With<PubAward>, Without<PubHighlight>)>,
    pub_meters: Query<&PubMeter>,
    icons: Query<(Entity, Option<&Children>), With<PubIcons>>,
    mut icon_tf: Query<(&PubIcon, &mut Transform)>,
    mut last: crate::levelreset::PerLevel<u32>,
) {
    for (mut v, a) in &mut parts {
        let show = view.open && a.is_none_or(|a| a.0 == view.award);
        *v = if show { Visibility::Inherited } else { Visibility::Hidden };
    }
    if !view.open {
        return;
    }
    for (p, mut t) in &mut texts {
        let s = match p.0 {
            "message" => &view.message,
            "info" => &view.info,
            _ => &view.exit_text,
        };
        if t.text != *s {
            t.text = s.clone();
        }
    }
    for mut e in &mut hl {
        e.pos = view.cursor;
    }
    for (mut e, mut k) in &mut awards {
        e.pos = view.award_pos;
        k.0 = view.award_scale;
    }
    for m in &pub_meters {
        let v = if m.pool { view.pool } else { view.health }.clamp(0.0, 1.0);
        if let Some(mut mat) = materials.get_mut(&m.material) {
            mat.uv_transform = Affine2::from_translation(Vec2::new(0.0, -((1.0 - v) * 0.5)));
        }
    }
    // the icons move (a bought weapon into Roman's slot): placed every frame, rebuilt when the textures change
    for (i, mut t) in &mut icon_tf {
        if let Some((p, _)) = view.icons.get(i.0) {
            t.translation.x = p.x - 320.0;
            t.translation.y = p.y - 240.0;
        }
    }
    let Some(hud) = hud else { return };
    let last = last.get();
    if *last == view.version {
        return;
    }
    *last = view.version;
    for (root, children) in &icons {
        if let Some(ch) = children {
            for c in ch.iter() {
                commands.entity(c).despawn();
            }
        }
        for (i, (p, tex)) in view.icons.iter().enumerate() {
            let Some(img) = hud.textures.get(*tex) else { continue };
            let size = icon_size(tex);
            let mat = materials.add(ColorMaterial { texture: Some(img.clone()), ..default() });
            let q = commands
                .spawn((
                    Mesh2d(meshes.add(Rectangle::new(size.x, size.y))),
                    MeshMaterial2d(mat),
                    Transform::from_xyz(p.x - 320.0, p.y - 240.0, 12.0),
                    PubIcon(i),
                    RenderLayers::layer(HUD_LAYER),
                ))
                .id();
            commands.entity(root).add_child(q);
        }
    }
}

/// The ticker-bomb clock (the ui_clock anim: the hand from 12 o'clock clockwise to 6 in 5 s, then still; the
/// export's rotation keys are conjugated, hence 90 - 36 t degrees) and the arming bar (fills left to right).
fn ticker(state: Res<HudState>, mut hands: Query<&mut Transform, With<ClockHand>>, bars: Query<&TickerBar>, mut materials: ResMut<Assets<ColorMaterial>>) {
    let t = state.ticker_clock.unwrap_or(0.0).min(5.0);
    for mut h in &mut hands {
        h.rotation = Quat::from_rotation_z((90.0 - 36.0 * t).to_radians());
    }
    let v = state.ticker_arming.unwrap_or(0.0).clamp(0.0, 1.0);
    for b in &bars {
        if let Some(mut mat) = materials.get_mut(&b.material) {
            mat.uv_transform = Affine2::from_translation(Vec2::new(0.0, -((1.0 - v) * 0.5)));
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The message feed: Chat (ui_02.cpp:1995-2270, the object Game+0xaac).

/// The chat colour table (Chat_Init 0x0046a7e0: 0xff000000, 0xff1fffff, 0xfff0f01f, 0xffff1fff, 0xff1fffff,
/// 0xffff0000; the text colour's alpha is forced to 0xff by UiText_Draw). Pickups add their lines with 1
/// (0x00472310), weapon changes with 2 (game_09.cpp:1730).
const CHAT_COLOURS: [u32; 6] = [0x000000, 0x1fffff, 0xf0f01f, 0xff1fff, 0x1fffff, 0xff0000];

/// The three chat lines (Chat +0x24, 3 x 0x100 wide characters): text, colour index (+0x18), game time added
/// (+0x624). Cleared by Chat_Clear 0x0046acb0 (Default).
#[derive(Resource, Default)]
pub struct Chat {
    lines: Vec<(String, u8, f32)>,
}

impl Chat {
    /// Chat_AddLine 0x0046a980: the first empty line, or the feed scrolls up one line first; an empty text is stored
    /// as " " (0x005a05e0), at most 0xff characters.
    fn add_line(&mut self, text: &str, colour: u8, now: f32) {
        if self.lines.len() == 3 {
            self.lines.remove(0);
        }
        let t: String = if text.is_empty() { " ".into() } else { text.chars().take(0xff).collect() };
        self.lines.push((t, colour, now));
    }

    /// Chat_AddMessage 0x0046aa60: word wrap at 490 units (0x005bccc4) measured at scale 1.0. While the rest is too
    /// wide, a prefix grows one character at a time; the line breaks at the last space whose prefix fits (the space is
    /// dropped), else at the first character that overflows, which is lost (an original bug, docs/original-bugs.md:
    /// reproduced). The text is cut to 300 characters first (wcsncpy 0x12c).
    fn add_message(&mut self, font: &FontFile, text: &str, colour: u8, now: f32) {
        const WRAP: f32 = 490.0;
        let buf: Vec<char> = text.chars().take(0x12c).collect();
        let width = |s: &[char]| text_width(font, &s.iter().collect::<String>(), 1.0);
        let mut line = 0usize;
        while line < buf.len() && width(&buf[line..]) > WRAP {
            let rest = &buf[line..];
            if rest.len() < 2 {
                break;
            }
            let (mut space, mut overflow, mut space_w) = (None::<usize>, None::<usize>, 0.0f32);
            let mut at_space = false;
            let mut p = 1usize;
            loop {
                let prefix_w = width(&rest[..p]);
                if overflow.is_none() && prefix_w > WRAP {
                    overflow = Some(p);
                }
                if rest[p] == ' ' {
                    if prefix_w > WRAP && space.is_some() && space_w <= WRAP {
                        at_space = true;
                        break;
                    }
                    space_w = prefix_w;
                    space = Some(p);
                }
                p += 1;
                if p >= rest.len() {
                    break;
                }
            }
            let _ = at_space;
            if let Some(s) = space {
                self.add_line(&rest[..s].iter().collect::<String>(), colour, now);
                line += s + 1;
            } else if let Some(o) = overflow {
                self.add_line(&rest[..o].iter().collect::<String>(), colour, now);
                line += o + 1;
            } else {
                break;
            }
        }
        if line < buf.len() {
            self.add_line(&buf[line..].iter().collect::<String>(), colour, now);
        }
    }
}

/// Chat_Update 0x0046a820 (each frame): the oldest line scrolls off once it is 6 s old (0x005bccf0, game time:
/// paused with the game); new messages from `HudState::chat_in`.
fn chat(time: Res<Time>, mut state: ResMut<HudState>, mut chat: ResMut<Chat>, hud: Option<Res<HudAssets>>) {
    let now = time.elapsed_secs();
    if chat.lines.first().is_some_and(|l| now - l.2 >= 6.0) {
        chat.lines.remove(0);
    }
    let Some(hud) = hud else {
        state.chat_in.clear();
        return;
    };
    for (text, colour) in std::mem::take(&mut state.chat_in) {
        chat.add_message(&hud.font, &text, colour, now);
    }
}

/// The width of a text in 640 x 480 units at `scale` (the widest line), laid out as `text_mesh` does (the
/// renderer's measure, virtual +0x1c0 / 0x00495fa0).
fn text_width(font: &FontFile, text: &str, scale: f32) -> f32 {
    let k = scale * 25.0 / font.common.line_height.max(1.0);
    let w_adv = font.chars.get(&('W' as u32).to_string()).map(|g| g.xadvance * k).unwrap_or(10.0 * k);
    let icon = font.common.line_height * k * 1.3 * 1.1;
    let mut best = 0.0f32;
    for l in text.replace("<br>", "\n").replace("<BR>", "\n").split('\n') {
        let mut x = 0.0f32;
        let mut chars = l.chars();
        while let Some(c) = chars.next() {
            if c == '<' {
                let tag: String = chars.by_ref().take_while(|t| *t != '>').collect();
                let tl = tag.to_lowercase();
                if let Some(n) = tl.strip_prefix("tab=") {
                    x = x.max(n.trim().parse::<f32>().unwrap_or(0.0) * w_adv);
                } else if tl.starts_with("icon=") {
                    x += icon;
                }
                continue;
            }
            if let Some(g) = font.chars.get(&(c as u32).to_string()) {
                x += g.xadvance * k;
            }
        }
        best = best.max(x);
    }
    best
}

/// UiText_FitScale 0x00465720: the largest scale up to `scale` at which the text is at most `width` units wide,
/// binary-searched 16 times between 0.1 x scale and scale (the first try returns at once when it fits).
fn fit_scale(font: &FontFile, text: &str, scale: f32, width: f32) -> f32 {
    if width <= 0.0 {
        return scale;
    }
    let (mut hi, mut lo, mut cur) = (scale, scale * 0.1, scale);
    let mut result = lo;
    for i in 0..16 {
        if text_width(font, text, cur) <= width {
            result = cur;
            if i == 0 {
                return result;
            }
            lo = cur;
        } else {
            hi = cur;
        }
        cur = (hi + lo) * 0.5;
    }
    result
}

/// A prompt's own text element (ui_tickerbomb_hint white, ui_pub_entry_hint / ui_vehicle_* #a0ffa0, ...): the bank
/// text whose words around `%s` match the prompt (the drivers 0x00467450, 0x00467b60, 0x004662a0, 0x00466210 show
/// their own ui_text; mission.rs hands the formatted string).
fn prompt_style<'a>(layout: &'a LayoutFile, prompt: &str) -> Option<&'a ElementDef> {
    // the longest match wins (a bare "%s" text matches anything)
    layout
        .elements
        .iter()
        .filter(|e| e.kind == "text" && e.page_name.as_deref() == Some("pui_main"))
        .filter_map(|e| {
            let s = e.string.as_deref().filter(|s| !s.is_empty())?;
            let fits = match s.split_once("%s") {
                Some((a, b)) => prompt.starts_with(a) && prompt.ends_with(b) && prompt.len() >= a.len() + b.len(),
                None => prompt == s,
            };
            fits.then_some((s.len(), e))
        })
        .max_by_key(|x| x.0)
        .map(|x| x.1)
}

// ---------------------------------------------------------------------------------------------------------------
// The objective pointer (0x00466790): a fan of eight arms (bones arm01..arm08 of ui_compass_objective's skin) that
// its anim closes as Roman nears the target: frame (anim 4) = 1 - min(distance x 0.002, 1) (0x005bcc90); at 0 the
// bind pose, a half disc; at 1 every arm at 90 degrees, a line.

/// Per vertex of a part: the (bone name, weight, bind rotation) of each influence.
type ArmRig = Vec<(String, f32, Quat)>;

/// The objective pointer's part: layout vertices with their bone weights; rewritten when the frame changes.
#[derive(Component)]
pub struct ArmPart {
    mesh: Handle<Mesh>,
    verts: Vec<(Vec2, ArmRig)>,
    applied: Option<f32>,
}

/// Per part of a model, per vertex: its influences from models/<model>.json / .bin (joints u16 x 4 at `joints`,
/// weights f32 x 4 at `weights`) and skeletons/<model>.json (each bone's bind rotation).
fn arm_rig(dir: &Path, model: &str) -> Vec<Vec<ArmRig>> {
    #[derive(Deserialize)]
    struct SkBone {
        name: String,
        r: [f32; 4],
    }
    #[derive(Deserialize)]
    struct Sk {
        bones: Vec<SkBone>,
    }
    #[derive(Deserialize)]
    struct Sub {
        vertices: usize,
        #[serde(default)]
        joints: Option<usize>,
        #[serde(default)]
        weights: Option<usize>,
    }
    #[derive(Deserialize)]
    struct Md {
        submeshes: Vec<Sub>,
    }
    let json = |sub: &str| std::fs::read_to_string(dir.join(sub).join(format!("{model}.json"))).ok();
    let (Some(sk), Some(md)) = (json("skeletons").and_then(|t| serde_json::from_str::<Sk>(&t).ok()), json("models").and_then(|t| serde_json::from_str::<Md>(&t).ok())) else {
        return Vec::new();
    };
    let bin = std::fs::read(dir.join("models").join(format!("{model}.bin"))).unwrap_or_default();
    let u16_at = |o: usize| bin.get(o..o + 2).map(|b| u16::from_le_bytes([b[0], b[1]]));
    let f32_at = |o: usize| bin.get(o..o + 4).map(|b| f32::from_le_bytes([b[0], b[1], b[2], b[3]]));
    md.submeshes
        .iter()
        .map(|sm| {
            (0..sm.vertices)
                .map(|v| {
                    let (Some(jo), Some(wo)) = (sm.joints, sm.weights) else { return Vec::new() };
                    (0..4)
                        .filter_map(|i| {
                            let j = u16_at(jo + v * 8 + i * 2)? as usize;
                            let w = f32_at(wo + v * 16 + i * 4)?;
                            let b = sk.bones.get(j).filter(|_| w > 0.0)?;
                            Some((b.name.to_lowercase(), w, Quat::from_array(b.r).normalize()))
                        })
                        .collect()
                })
                .collect()
        })
        .collect()
}

/// Poses the objective pointer: frame = 1 - min(distance x 0.002, 1), each vertex turned by its arms' anim rotation
/// at frame x duration relative to their bind rotation (the arms sit at the centre).
fn compass_arms(state: Res<HudState>, lib: Option<ResMut<crate::skin::AnimLibrary>>, mut parts: Query<&mut ArmPart>, mut meshes: ResMut<Assets<Mesh>>) {
    let Some(mut lib) = lib else { return };
    let Some((_, dist)) = state.objective else { return };
    let frame = 1.0 - (dist * 0.002).min(1.0);
    let Some(anim) = lib.anim("ui_compass_objective") else { return };
    let t = frame * anim.duration;
    for mut p in &mut parts {
        if p.applied.is_some_and(|a| (a - frame).abs() < 1e-4) {
            continue;
        }
        p.applied = Some(frame);
        let pos: Vec<[f32; 3]> = p
            .verts
            .iter()
            .map(|(v, rig)| {
                if rig.is_empty() {
                    return [v.x, v.y, 0.0];
                }
                let total: f32 = rig.iter().map(|r| r.1).sum::<f32>().max(1e-6);
                let mut out = Vec2::ZERO;
                for (bone, w, bind) in rig {
                    let q = anim.rotation(bone, t).unwrap_or(*bind);
                    out += (q * bind.inverse() * v.extend(0.0)).truncate() * (*w / total);
                }
                [out.x, out.y, 0.0]
            })
            .collect();
        if let Some(mut m) = meshes.get_mut(&p.mesh) {
            m.insert_attribute(Mesh::ATTRIBUTE_POSITION, pos);
        }
    }
}

#[derive(Component)]
pub struct CompassNeedle;

fn window_size(windows: &Query<&Window>) -> Vec2 {
    windows.single().map(|w| Vec2::new(w.physical_width() as f32, w.physical_height() as f32)).unwrap_or(Vec2::new(1920.0, 1080.0))
}

/// 640x480 (y up) -> overlay coordinates (origin at the centre), scaled by height / 480, anchored.
fn place(windows: Query<&Window>, mut q: Query<(&HudElement, &mut Transform, Option<&ElementScale>)>) {
    let size = window_size(&windows);
    // the original draws the game between letterbox bars (rows 54..426 of its 480: camera +0x?? letterbox 0.1125 /
    // 0.775) and its in-game HUD inside that band, near the band's edges; the remaster has no bars, so the band maps
    // to the whole window (the player: the HUD sat too far in from the borders). Menus and the pub screen fill the
    // original's whole 640 x 480 and keep that mapping. The scale is capped by the width (tall windows).
    let s_menu = size.y / 480.0;
    let s_game = (size.y / 372.0).min(size.x / 640.0);
    for (e, mut t, k) in &mut q {
        let s = if e.full { s_menu } else { s_game };
        let x = match e.anchor {
            Anchor::Left => -size.x / 2.0 + e.pos.x * s,
            Anchor::Right => size.x / 2.0 - (640.0 - e.pos.x) * s,
            Anchor::Centre => (e.pos.x - 320.0) * s,
        };
        t.translation = Vec3::new(x, (e.pos.y - 240.0) * s, t.translation.z);
        let k = k.map_or(1.0, |k| k.0);
        t.scale = Vec3::new(s * k, s * k, 1.0);
    }
}

/// Builds a text mesh: glyph quads in 640x480 units around (0, 0), each line centred, the block centred in y;
/// `<br>` / `\n` break lines, `<rgb=RRGGBB>`..`</rgb>` colour (vertex colours over `base`), `<tab=N>` moves the
/// pen to N widths of 'W' from the line start, `<icon=name>` places an input prompt icon (a square of 1.3 line
/// heights, in the returned icon mesh; without the atlas it is skipped); other tags are skipped. Also returns the
/// block's size.
fn text_mesh(font: &FontFile, icons: Option<&IconAtlas>, text: &str, scale: f32, offset: Vec2, base: Color) -> (Mesh, Vec2, Option<Mesh>) {
    // in the original a scale-1 line is 25 px at 480 lines (hud.md 3.3)
    let k = scale * 25.0 / font.common.line_height.max(1.0);
    let clean = text.replace("<br>", "\n").replace("<BR>", "\n");
    let w_adv = font.chars.get(&('W' as u32).to_string()).map(|g| g.xadvance * k).unwrap_or(10.0 * k);
    // lay out each line from x = 0: (x, glyph, colour)
    let mut lines: Vec<(Vec<(f32, Glyph, [f32; 4])>, f32, Vec<(f32, [f32; 4])>)> = Vec::new();
    let icon_size = font.common.line_height * k * 1.3;
    let mut colour = base.to_linear().to_f32_array();
    for l in clean.split('\n') {
        let mut items = Vec::new();
        let mut line_icons = Vec::new();
        let mut x = 0.0f32;
        let mut chars = l.chars().peekable();
        while let Some(c) = chars.next() {
            if c == '<' {
                let mut tag = String::new();
                for t in chars.by_ref() {
                    if t == '>' {
                        break;
                    }
                    tag.push(t);
                }
                let tl = tag.to_lowercase();
                if let Some(v) = tl.strip_prefix("rgb=") {
                    if let Ok(v) = u32::from_str_radix(v.trim(), 16) {
                        colour = Color::srgb_u8((v >> 16) as u8, (v >> 8) as u8, v as u8).to_linear().to_f32_array();
                    }
                } else if tl == "/rgb" {
                    colour = base.to_linear().to_f32_array();
                } else if let Some(n) = tl.strip_prefix("tab=") {
                    x = x.max(n.trim().parse::<f32>().unwrap_or(0.0) * w_adv);
                } else if let Some(r) = tl.strip_prefix("icon=").and_then(|n| icons.and_then(|a| a.icons.get(n.trim()))) {
                    line_icons.push((x + icon_size * 0.05, *r));
                    x += icon_size * 1.1;
                }
                continue;
            }
            let Some(g) = font.chars.get(&(c as u32).to_string()).copied() else { continue };
            items.push((x, g, colour));
            x += g.xadvance * k;
        }
        lines.push((items, x, line_icons));
    }
    let (mut pos, mut uv, mut idx, mut col) = (Vec::new(), Vec::new(), Vec::new(), Vec::new());
    let lh = font.common.line_height * k;
    let total_h = lh * lines.len() as f32;
    let max_w = lines.iter().map(|l| l.1).fold(0.0, f32::max);
    let (mut ipos, mut iuv, mut iidx) = (Vec::new(), Vec::new(), Vec::new());
    for (li, (items, width, line_icons)) in lines.iter().enumerate() {
        let left = -width / 2.0 + offset.x;
        let top = total_h / 2.0 - li as f32 * lh + offset.y;
        for (x, g, c) in items {
            if g.width > 0.0 {
                let (x0, x1) = (left + x + g.xoffset * k, left + x + (g.xoffset + g.width) * k);
                let (y0, y1) = (top - g.height * k, top);
                let b = pos.len() as u32;
                pos.extend([[x0, y0, 0.0], [x1, y0, 0.0], [x1, y1, 0.0], [x0, y1, 0.0]]);
                let (u0, u1) = (g.x / font.common.scale_w, (g.x + g.width) / font.common.scale_w);
                let (v0, v1) = (g.y / font.common.scale_h, (g.y + g.height) / font.common.scale_h);
                uv.extend([[u0, v1], [u1, v1], [u1, v0], [u0, v0]]);
                col.extend([*c; 4]);
                idx.extend([b, b + 1, b + 2, b, b + 2, b + 3]);
            }
        }
        if let Some(a) = icons {
            for (x, r) in line_icons {
                let mid = top - lh / 2.0;
                let (x0, x1) = (left + x, left + x + icon_size);
                let (y0, y1) = (mid - icon_size / 2.0, mid + icon_size / 2.0);
                let b = ipos.len() as u32;
                ipos.extend([[x0, y0, 0.0], [x1, y0, 0.0], [x1, y1, 0.0], [x0, y1, 0.0]]);
                let (u0, u1) = (r[0] / a.width, (r[0] + r[2]) / a.width);
                let (v0, v1) = (r[1] / a.height, (r[1] + r[3]) / a.height);
                iuv.extend([[u0, v1], [u1, v1], [u1, v0], [u0, v0]]);
                iidx.extend([b, b + 1, b + 2, b, b + 2, b + 3]);
            }
        }
    }
    let icon_mesh = (!ipos.is_empty()).then(|| {
        let mut m = Mesh::new(PrimitiveTopology::TriangleList, RenderAssetUsages::default());
        m.insert_attribute(Mesh::ATTRIBUTE_POSITION, ipos);
        m.insert_attribute(Mesh::ATTRIBUTE_UV_0, iuv);
        m.insert_indices(Indices::U32(iidx));
        m
    });
    if pos.is_empty() {
        pos.extend([[0.0f32; 3]; 3]);
        uv.extend([[0.0f32; 2]; 3]);
        col.extend([[0.0f32; 4]; 3]);
        idx.extend([0, 1, 2]);
    }
    let mut m = Mesh::new(PrimitiveTopology::TriangleList, RenderAssetUsages::default());
    m.insert_attribute(Mesh::ATTRIBUTE_POSITION, pos);
    m.insert_attribute(Mesh::ATTRIBUTE_UV_0, uv);
    m.insert_attribute(Mesh::ATTRIBUTE_COLOR, col);
    m.insert_indices(Indices::U32(idx));
    (m, Vec2::new(max_w, total_h), icon_mesh)
}

/// Text elements: the game's values formatted as the drivers do; meshes rebuilt when the text changes.
#[allow(clippy::too_many_arguments)]
fn texts(
    mut commands: Commands,
    hud: Option<Res<HudAssets>>,
    state: Res<HudState>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<ColorMaterial>>,
    mut q: Query<(Entity, &HudElement, &mut HudText, Option<&Children>, Option<&TextAlign>, Option<&RenderLayers>)>,
    (assets, chat): (Res<AssetServer>, Res<Chat>),
) {
    let Some(hud) = hud else { return };
    for (e, el, mut t, children, align, layers) in &mut q {
        // a text on another layer (the movies' subtitles: movie.rs) draws its glyphs there
        let layer = layers.cloned().unwrap_or(RenderLayers::layer(HUD_LAYER));
        t.text = match el.name.as_str() {
            // 0x00465a20: "%d/%d" (clip / reserve), or "%d/" + the infinity sign (ui_ammo_infinity) for weapon type 2,
            // whose reserve is unlimited (the rifle in lev_1_m1)
            "ui_ammo_count" => if state.unlimited { format!("{}/", state.clip) } else { format!("{}/{}", state.clip, state.ammo) },
            // 0x00465ad0: "%d", the current special's count
            "ui_grenade_count" => format!("{}", if state.special == 1 { state.topsy } else { state.stickies }),
            // Chat_Update 0x0046a820: line i in ui_chat_0i, coloured by its index into the chat colour table
            n if n.starts_with("ui_chat_0") => {
                let i = n.trim_start_matches("ui_chat_0").parse::<usize>().unwrap_or(1).saturating_sub(1);
                match chat.lines.get(i) {
                    Some((s, c, _)) => {
                        let v = CHAT_COLOURS[(*c as usize).min(5)];
                        let colour = Color::srgb_u8((v >> 16) as u8, (v >> 8) as u8, v as u8);
                        if colour != t.colour {
                            t.colour = colour;
                            t.built = None;
                        }
                        s.clone()
                    }
                    None => String::new(),
                }
            }
            "ui_prompt" => {
                let p = state.prompt.clone().unwrap_or_default();
                // the prompt's own text colour (white for the ticker bomb's, #a0ffa0 for the pub's and the mounted gun's)
                let colour = prompt_style(&hud.layout, &p).map(|e| parse_colour(e.colour.as_deref())).unwrap_or(Color::WHITE);
                if colour != t.colour {
                    t.colour = colour;
                    t.built = None;
                }
                p
            }
            "ui_briefing" => state.briefing.clone().unwrap_or_default(),
            "ui_team" => state.team.and_then(|(n, _)| hud.layout.elements.iter().find(|e| e.name == n)).and_then(|e| e.string.clone()).unwrap_or_default(),
            "ui_subtitle" => match &state.subtitle {
                Some((s, c, _)) => {
                    if *c != t.colour {
                        t.colour = *c;
                        t.built = None;
                    }
                    s.clone()
                }
                None => String::new(),
            },
            _ => t.text.clone(),
        };
        if t.built.as_deref() == Some(t.text.as_str()) {
            continue;
        }
        if let Some(ch) = children {
            for c in ch.iter() {
                commands.entity(c).despawn();
            }
        }
        // fitted to its box (UiText_FitToBox 0x004656f0 / UiText_FitScale 0x00465720: the prompts' 576 units)
        let scale = fit_scale(&hud.font, &t.text, t.scale, t.fit);
        // shadow 2 px right and down (of 480), drawn first
        let (front_mesh, size, icon_mesh) = text_mesh(&hud.font, hud.icons.as_ref(), &t.text, scale, Vec2::ZERO, t.colour);
        let (shadow_mesh, _, _) = text_mesh(&hud.font, hud.icons.as_ref(), &t.text, scale, Vec2::new(2.0, -2.0), Color::BLACK);
        let dx = -align.map(|a| a.0).unwrap_or(0.0) * size.x / 2.0;
        let shadow = commands
            .spawn((Mesh2d(meshes.add(shadow_mesh)), MeshMaterial2d(hud.shadow_material.clone()), Transform::from_xyz(dx, 0.0, 20.0), layer.clone()))
            .id();
        let front = commands
            .spawn((Mesh2d(meshes.add(front_mesh)), MeshMaterial2d(hud.text_material.clone()), Transform::from_xyz(dx, 0.0, 21.0), layer.clone()))
            .id();
        commands.entity(e).add_children(&[shadow, front]).insert(TextSize(size));
        if let (Some(m), Some(mat)) = (icon_mesh, hud.icon_material.clone()) {
            let icon = commands.spawn((Mesh2d(meshes.add(m)), MeshMaterial2d(mat), Transform::from_xyz(dx, 0.0, 22.0), layer.clone())).id();
            commands.entity(e).add_children(&[icon]);
        }
        // the message box (ui_mission_briefing, 0x00466060, sizing its band through 0x00464400) sits on the band
        // ui_text_bkd: texture b_text_bkd,
        // 856 units wide, stretched vertically to the text (anim 4 = min(text height / 480, 1), 0x00466470); the
        // padding is measured from the original's capture (work/shots/orig_ui_play-480.png: ~12 units)
        if el.name == "ui_briefing" && !t.text.is_empty() {
            let band = materials.add(ColorMaterial { texture: Some(assets.load("ui/textures/b_text_bkd.png")), ..default() });
            let f = commands.spawn((Mesh2d(meshes.add(Rectangle::new(856.0, size.y + 24.0))), MeshMaterial2d(band), Transform::from_xyz(0.0, 0.0, 19.0), layer.clone())).id();
            commands.entity(e).add_children(&[f]);
        }
        t.built = Some(t.text.clone());
    }
}

/// Speech subtitles: the speaker's colour from the caption table (white when not listed), shown for the line's
/// length; the newest line replaces the one showing.
fn subtitles(time: Res<Time>, mut subs: MessageReader<crate::audio::Subtitle>, hud: Option<Res<HudAssets>>, mut state: ResMut<HudState>) {
    for s in subs.read() {
        // the original shows speech in the top feed (mission.rs messages), not on a bottom line
        if true || state.subtitles_off || s.text.is_empty() {
            continue;
        }
        let colour = hud
            .as_ref()
            .and_then(|h| s.speaker.as_ref().and_then(|sp| h.layout.captions.iter().find(|c| c.speaker.eq_ignore_ascii_case(sp))))
            .map(|c| parse_colour(Some(&c.colour)))
            .unwrap_or(Color::WHITE);
        info!("subtitle: {:?}: {}", s.speaker, s.text);
        state.subtitle = Some((s.text.clone(), colour, s.duration));
    }
    if let Some(sub) = state.subtitle.as_mut() {
        sub.2 -= time.delta_secs();
        if sub.2 <= 0.0 {
            state.subtitle = None;
        }
    }
    if let Some(team) = state.team.as_mut() {
        team.1 -= time.delta_secs();
        if team.1 <= 0.0 {
            state.team = None;
        }
    }
}

/// Health meters (the meter init 0x00466c20 names the target: ui_player_health 0, ui_health_enemy 1, ui_health_q 2,
/// the others 3, Jonesy): the texture scrolls by t = clamp((1 - value) * 0.5, 0, 0.5); colour band x = 0.5 below 25 %,
/// 0.25 below 50 %, else 0 (0x00466e70).
fn meters(state: Res<HudState>, mut materials: ResMut<Assets<ColorMaterial>>, q: Query<(&Meter, &ChildOf)>, parents: Query<&HudElement>, time: Res<Time<Real>>, mut pulse: Local<(f32, bool)>) {
    // the player's meter pulses at or below 20% (0x00466e70: alpha up 8 / s, down 2 / s)
    if state.health <= 0.2 && state.health > 0.0 {
        let dt = time.delta_secs();
        if pulse.1 {
            pulse.0 += 8.0 * dt;
            if pulse.0 >= 1.0 {
                pulse.0 = 1.0;
                pulse.1 = false;
            }
        } else {
            pulse.0 -= 2.0 * dt;
            if pulse.0 <= 0.0 {
                pulse.0 = 0.0;
                pulse.1 = true;
            }
        }
    } else {
        *pulse = (1.0, false);
    }
    for (m, parent) in &q {
        let Ok(el) = parents.get(parent.parent()) else { continue };
        let v = match el.name.as_str() {
            "ui_player_health" => state.health,
            "ui_health_q" => state.q_health,
            "ui_health_jonesy" => state.jonesy_health,
            _ => continue,
        }
        .clamp(0.0, 1.0);
        let band = if v < 0.25 { 0.5 } else if v < 0.5 { 0.25 } else { 0.0 };
        let t = ((1.0 - v) * 0.5).clamp(0.0, 0.5);
        if let Some(mut mat) = materials.get_mut(&m.material) {
            // texture v runs downward in Bevy: scrolling "up" in the original's uv is -t here
            mat.uv_transform = Affine2::from_translation(Vec2::new(band, -t));
            if el.name == "ui_player_health" {
                mat.color.set_alpha(pulse.0);
            }
        }
    }
}

/// The compass needle turns with the camera heading (0x00466700: anim 4 = 1 - (pi - atan2(dir.y, dir.x)) / 2 pi of the
/// camera direction, the ui_compass anim one turn); the objective arrow points at the compass target (0x00466790).
fn compass(state: Res<HudState>, mut needles: Query<&mut Transform, With<CompassNeedle>>, mut q: Query<(&HudElement, &mut Visibility, &Children)>, mut parts: Query<&mut Transform, Without<CompassNeedle>>) {
    for mut t in &mut needles {
        let z = t.translation.z;
        t.rotation = Quat::from_rotation_z(state.heading);
        t.translation.z = z;
    }
    for (el, mut vis, children) in &mut q {
        if el.name == "ui_compass_objective" {
            *vis = if state.objective.is_some() { Visibility::Inherited } else { Visibility::Hidden };
            if let Some((bearing, _)) = state.objective {
                for c in children.iter() {
                    if let Ok(mut t) = parts.get_mut(c) {
                        t.rotation = Quat::from_rotation_z(bearing);
                    }
                }
            }
        }
    }
}

#[allow(clippy::type_complexity)]
fn visibility(
    state: Res<HudState>,
    xhair: Res<Crosshair>,
    mut q: Query<(&HudElement, &mut Visibility), (Without<HudText>, Without<crate::gamemenu::MenuOwned>)>,
    mut texts: Query<(&HudElement, &mut Visibility), (With<HudText>, Without<crate::gamemenu::MenuOwned>)>,
    (ctl, app, vtime): (Res<crate::input::Controls>, Res<State<crate::AppState>>, Res<Time<Virtual>>),
) {
    // 0x00463fa0: the weapon-cycle arrows. "off" with more than one special (up / down: 0x004a1640) or primary weapon
    // (left / right: 0x004a1610); "on" while that cycle button is held: IncSpecialWeapon 0x10 up, DecSpecialWeapon
    // 0x11 down, DecWeapon 0xf left, IncWeapon 0xe right (player.md 2; the remaster binds Next Special and Next
    // Weapon only). All off in a cut scene (g+0x26cc) and while paused (g+0x2684, hud.md 8).
    // paused: the game's pause count Game+0x2684 (Game_SetPaused 0x004031e0: any menu screen, 0x00490a40, or a
    // paused message, 0x00403410); the crosshairs (0x00406790) and the objects with +0x474 0x20 (the prompts:
    // 0x00465230, UiPass_Draw ui_pass.cpp:98) are not drawn then
    let paused = *app.get() != crate::AppState::Playing || vtime.is_paused();
    let arrows = state.visible && !state.cinematic && !paused;
    let (multi_special, multi_weapon) = (arrows && state.specials > 1, arrows && state.weapons > 1);
    for (el, mut v) in &mut q {
        let show = state.visible
            && match el.name.as_str() {
                // the hit indicators (init 0x00467010: top 1, bottom 2, left 4, right 8 of the hit mask Game+0x26d0;
                // 0x004670f0 shows each for Game+0xcb8 s: player.rs damage_flash_duration)
                "ui_hit_top" => state.hit_flash[0] > 0.0,
                "ui_hit_bottom" => state.hit_flash[1] > 0.0,
                "ui_hit_left" => state.hit_flash[2] > 0.0,
                "ui_hit_right" => state.hit_flash[3] > 0.0,
                "ui_compass_objective" => state.objective.is_some(),
                "ui_player_rifle" => state.weapon != "machinegun",
                "ui_player_mg" => state.weapon == "machinegun",
                "ui_player_grenades" => state.special == 0,
                "ui_player_topsy" => state.special == 1,
                "ui_ammo_infinity" => state.unlimited,
                "ui_clock" | "ui_tickerbomb" => state.ticker_clock.is_some(),
                "ui_tickerbomb_set" => state.ticker_arming.is_some(),
                "ui_player_health" | "ui_health_q" | "ui_health_jonesy" => !state.cinematic,
                // Hud_ProjectMarkers 0x00406840: the current weapon's crosshair (list +0x30c by weapon index; the
                // rifle's hidden over a friendly), ui_crosshair_friendly (+0x37c) over a friendly, ui_special_crosshair
                // (+0x38c) while a special is selected; all only while 0x00406790 allows (no cinematic, no pause)
                "ui_crosshair" => !state.cinematic && !paused && state.weapon != "machinegun" && !xhair.friendly,
                "ui_mg_crosshair" => !state.cinematic && !paused && state.weapon == "machinegun",
                "ui_crosshair_friendly" => !state.cinematic && !paused && xhair.friendly,
                "ui_special_crosshair" => !state.cinematic && !paused,
                "ui_select_up_off" | "ui_select_down_off" => multi_special,
                "ui_select_left_off" | "ui_select_right_off" => multi_weapon,
                "ui_select_up_on" => multi_special && ctl.down(crate::input::Act::NextSpecial),
                "ui_select_right_on" => multi_weapon && ctl.down(crate::input::Act::SwitchWeapon),
                "ui_select_down_on" | "ui_select_left_on" => false,
                n if n.starts_with("ui_health") => !state.cinematic,
                _ => true,
            };
        *v = if show { Visibility::Inherited } else { Visibility::Hidden };
    }
    for (el, mut v) in &mut texts {
        // paused messages (the briefing box) show over cinematics and pauses too
        let show = (state.visible && !(paused && PAUSE_HIDDEN.contains(&el.name.as_str())))
            || (el.name == "ui_briefing" && state.briefing.is_some())
            || (el.name == "ui_subtitle" && state.subtitle.is_some());
        *v = if show { Visibility::Inherited } else { Visibility::Hidden };
    }
}

/// The state log's "hud" (playtest expect steps): the chat lines and their colour indices, the prompt and its
/// colour, the weapon-cycle arrows shown, the objective pointer's frame.
#[allow(clippy::type_complexity)]
fn log_hud(
    state: Res<HudState>,
    chat: Res<Chat>,
    hud: Option<Res<HudAssets>>,
    els: Query<(&HudElement, &Visibility, Option<&HudText>), Without<crate::gamemenu::MenuOwned>>,
    play: Option<ResMut<crate::playtest::PlayState>>,
) {
    let Some(mut play) = play else { return };
    let mut arrows: Vec<&str> = els.iter().filter(|(e, v, _)| e.name.starts_with("ui_select_") && **v != Visibility::Hidden).map(|(e, _, _)| e.name.as_str()).collect();
    arrows.sort();
    let prompt_colour = els.iter().find(|(e, _, _)| e.name == "ui_prompt").and_then(|(_, _, t)| t).map(|t| {
        let c = t.colour.to_srgba();
        format!("#{:02x}{:02x}{:02x}", (c.red * 255.0).round() as u8, (c.green * 255.0).round() as u8, (c.blue * 255.0).round() as u8)
    });
    let prompt_scale = match (&hud, &state.prompt) {
        (Some(h), Some(p)) => fit_scale(&h.font, p, 1.0, 576.0),
        _ => 1.0,
    };
    play.0.insert(
        "hud".into(),
        serde_json::json!({
            "visible": state.visible,
            "chat": chat.lines.iter().map(|l| l.0.clone()).collect::<Vec<_>>(),
            // as a string: a two-number array would read as a range in an expect step
            "chat_colours": chat.lines.iter().map(|l| l.1.to_string()).collect::<Vec<_>>().join(","),
            "chat_lines": chat.lines.len(),
            "prompt": state.prompt.clone().unwrap_or_default(),
            "prompt_shown": els.iter().any(|(e, v, _)| e.name == "ui_prompt" && *v != Visibility::Hidden),
            "crosshair_shown": els.iter().any(|(e, v, _)| (e.name == "ui_crosshair" || e.name == "ui_mg_crosshair") && *v != Visibility::Hidden),
            "prompt_colour": prompt_colour,
            "prompt_scale": (prompt_scale * 1000.0).round() / 1000.0,
            "arrows": arrows,
            "weapons": state.weapons,
            "specials": state.specials,
            "objective_frame": state.objective.map(|o| ((1.0 - (o.1 * 0.002).min(1.0)) * 1000.0).round() / 1000.0),
        }),
    );
}

// ---------------------------------------------------------------------------------------------------------------
// The crosshair: Hud_UpdateCrosshair 0x00406f50 (src/ui/ui_01.cpp:312) picks what is under it, Hud_ProjectMarkers
// 0x00406840 (src/renderer/render_r0.cpp:143) shows the current weapon's crosshair and plays its `<name>_in` /
// `<name>_out` anim (0x00466650 / 0x004666a0) when the state changes. docs/subsystems/hud.md section 11.

/// The HUD texts hidden while the game is paused (+0x474 0x20: the prompts, inited by 0x00465230 and the classes
/// built on it, 0x00467420 / 0x00467790).
const PAUSE_HIDDEN: &[&str] = &["ui_prompt"];

/// The crosshair elements whose parts follow their skin's bones through the `_in` / `_out` anims.
const CROSSHAIRS: &[&str] = &["ui_crosshair", "ui_mg_crosshair", "ui_sniper_crosshair"];

/// What the crosshair's aim ray found this frame (the HUD object Game+0xa6c).
#[derive(Resource)]
pub struct Crosshair {
    /// word hud +0x392: the object under the crosshair is a target (the crosshair closes: `<name>_in`)
    pub on: bool,
    /// byte hud +0x394: it is a friendly (an AI teammate, vt+0x10c and vt+0xe4: Q, Jonesy; or a peasant)
    pub friendly: bool,
    /// hud +0x5c: where the ray hit, else its end (start + direction x the weapon's range)
    pub point: Option<Vec3>,
    /// hud +0x58: what it hit
    pub object: Option<Entity>,
    /// word hud +0x390: the state the anims last showed; 2 = none yet / the weapon changed (every crosshair plays
    /// `_out`, then the current one the state's anim)
    shown: u8,
    weapon: &'static str,
    /// per element: playing `_in` (true) or `_out`, seconds since it started (Anim_Set flags 0x80: once, held)
    anims: HashMap<String, (bool, f32)>,
}

impl Default for Crosshair {
    fn default() -> Self {
        // the HUD object's constructor 0x004065d0 sets +0x390 = 2 (game_01.cpp:1463); its reset 0x00406690 clears the
        // targets (+0, +0x58, +0x84, the lock slots), as a new Crosshair
        Crosshair { on: false, friendly: false, point: None, object: None, shown: 2, weapon: "", anims: HashMap::new() }
    }
}

/// How a level object answers the crosshair's test (0x00406f50, ui_01.cpp:557-588): by its class (behaviour, the
/// class id +0x418) and its damage flags. Vtable slots read from the exe: DefaultGameObject 0x005a2bf0 has vt+0x110
/// true, BuildingObject 0x005aeeb8 vt+0x114 true; trees 0x005a2e60, kegs 0x005a4300 neither.
#[derive(Component, Clone, Copy, PartialEq, Debug)]
pub enum AimClass {
    /// vt+0x110: on when takes_damage (+0x4d0 bit 0, game_03.cpp:508)
    Default { takes_damage: bool },
    /// vt+0x114: with takes_damage (+0x460 0x1000), for weapons 0 / 2 (machine gun, rifle) takes_bullet_damage
    /// (0x2000), for the other weapons always
    Building { takes_damage: bool, takes_bullet: bool },
    /// class 0x91 twiglet_generator, 0x37 pub, 0x4e stig_mgun_turret, 0x4f forge_cannon: never on
    Never,
    /// class 0x35 peasant: never on, but friendly (+0x394)
    Peasant,
    /// every other class (trees, kegs, rocket tower turrets, ...): on
    Other,
}

impl AimClass {
    pub fn of(p: &crate::gamedata::Props) -> Self {
        match p.s("behavior").unwrap_or("") {
            // no behaviour: DefaultGameObject_Load 0x0044fd20 (takes_damage default 0)
            "" => AimClass::Default { takes_damage: p.f("takes_damage", 0.0) != 0.0 },
            // BuildingObject 0x0040ad80 (ai_01.cpp:961: takes_damage default 1, takes_bullet_damage 0)
            "building" => AimClass::Building { takes_damage: p.f("takes_damage", 1.0) != 0.0, takes_bullet: p.f("takes_bullet_damage", 0.0) != 0.0 },
            "twiglet_generator" | "pub" | "stig_mgun_turret" | "forge_cannon" => AimClass::Never,
            "peasant" => AimClass::Peasant,
            _ => AimClass::Other,
        }
    }

    /// `gun`: the weapon is 0 or 2 (the machine gun or the rifle: both of the player's guns in lev_1_m1)
    fn on(self, gun: bool) -> bool {
        match self {
            AimClass::Default { takes_damage } => takes_damage,
            AimClass::Building { takes_damage, takes_bullet } => takes_damage && (takes_bullet || !gun),
            AimClass::Never | AimClass::Peasant => false,
            AimClass::Other => true,
        }
    }
}

/// A crosshair part whose vertices follow its skin's bones: per vertex the layout position, the bone (lower case)
/// and that bone's bind position; the mesh is rewritten when the pose changes.
#[derive(Component)]
pub struct CrossPart {
    element: String,
    mesh: Handle<Mesh>,
    verts: Vec<(Vec2, Option<String>, Vec2)>,
    applied: Option<(bool, f32)>,
}

/// Per layout part of a crosshair model, per vertex: its bone (lower case) and that bone's bind position (x, y), from
/// models/<model>.json / .bin (joints: u16 x 4 per vertex, all weight on the first) and skeletons/<model>.json
/// (bind = inverse of the inverse bind matrix). ui_crosshair: Box01..04 = the left, top, right, bottom arm (bind
/// +-1). Parts exported without blend data are rigid on their skin mesh's one bone (the big05_skin mesh bone lists):
/// ui_mg_crosshair's red corners Circle01..04 on "target", the blue ring m_center on "center".
fn cross_bones(dir: &Path, model: &str) -> Vec<Vec<(Option<String>, Vec2)>> {
    #[derive(Deserialize)]
    struct SkBone {
        name: String,
    }
    #[derive(Deserialize)]
    struct Sk {
        bones: Vec<SkBone>,
        inverse_bind: Vec<[f32; 16]>,
    }
    #[derive(Deserialize)]
    struct Sub {
        vertices: usize,
        #[serde(default)]
        joints: Option<usize>,
    }
    #[derive(Deserialize)]
    struct Md {
        submeshes: Vec<Sub>,
    }
    let json = |sub: &str, ext: &str| std::fs::read_to_string(dir.join(sub).join(format!("{model}.{ext}"))).ok();
    let (Some(sk), Some(md)) = (
        json("skeletons", "json").and_then(|t| serde_json::from_str::<Sk>(&t).ok()),
        json("models", "json").and_then(|t| serde_json::from_str::<Md>(&t).ok()),
    ) else {
        return Vec::new();
    };
    let bin = std::fs::read(dir.join("models").join(format!("{model}.bin"))).unwrap_or_default();
    let bone = |i: usize| -> (Option<String>, Vec2) {
        let bind = sk.inverse_bind.get(i).map(|m| Mat4::from_cols_array(m).inverse().w_axis.truncate().truncate()).unwrap_or(Vec2::ZERO);
        (sk.bones.get(i).map(|b| b.name.to_lowercase()), bind)
    };
    md.submeshes
        .iter()
        .enumerate()
        .map(|(si, sm)| match sm.joints {
            Some(off) => (0..sm.vertices)
                .map(|v| bin.get(off + v * 8..off + v * 8 + 2).map(|b| bone(u16::from_le_bytes([b[0], b[1]]) as usize)).unwrap_or((None, Vec2::ZERO)))
                .collect(),
            None => {
                let name = match (model, si) {
                    ("ui_mg_crosshair", 0..=3) => "target",
                    ("ui_mg_crosshair", _) => "center",
                    _ => "",
                };
                let b = sk.bones.iter().position(|b| b.name.eq_ignore_ascii_case(name)).map(bone).unwrap_or((None, Vec2::ZERO));
                vec![b; sm.vertices]
            }
        })
        .collect()
}

enum AimHit {
    Object(Option<Entity>, oxijolt::prelude::BodyId),
    Grunt(Entity),
    Friend(Entity),
}

/// Hud_UpdateCrosshair 0x00406f50's aim ray and state (ui_01.cpp:519-588): from the camera (20 ahead unless the
/// camera is zooming, 0x0040fba0 = camera +0x6c), along its direction, as long as the current weapon's range
/// (Game+0x28b8 + index * 0x150: machinegun_range / rifle_range), against list World+0xe9c (every collidable object
/// and every AI, ignoring the player: 0x00413d40) at detail level 2 (characters by their collision parts). The hit
/// object decides word +0x392 (on target) and byte +0x394 (friendly); Hud_ProjectMarkers then plays the anims.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn crosshair_aim(
    mut xhair: ResMut<Crosshair>,
    state: Res<HudState>,
    physics: NonSend<crate::physics::Physics>,
    data: Res<crate::gamedata::GameData>,
    cam: Query<&GlobalTransform, With<crate::player::FollowCam>>,
    roman: Query<(Entity, &crate::player::Roman)>,
    grunts: Query<(Entity, &crate::physics::Body, &GlobalTransform, Option<&crate::skin::Animator>), With<crate::ai::Grunt>>,
    companions: Query<(Entity, &crate::ai::Companion, &GlobalTransform, Option<&crate::skin::Animator>)>,
    classes: Query<&AimClass>,
    placed: Query<(Entity, &crate::level::Placed)>,
    joints: Query<&GlobalTransform>,
    hulls: Option<Res<crate::hitparts::HitHulls>>,
    (binds, mounted): (Res<Assets<bevy::mesh::skinning::SkinnedMeshInverseBindposes>>, Res<crate::turret::Mounted>),
) {
    let Ok(c) = cam.single() else { return };
    let (eye, dir) = (c.translation(), c.forward().as_vec3());
    let (roman_e, zoomed) = roman.single().map(|(e, r)| (Some(e), r.zoom > 0.0)).unwrap_or((None, false));
    let start = if zoomed { eye } else { eye + dir * 20.0 };
    let gun_mg = state.weapon == "machinegun";
    let g = data.props("p_globalsettings");
    let range = g.f(if gun_mg { "machinegun_range" } else { "rifle_range" }, 10.0);
    // on the mounted gun (Game+0xac8 = 4) the ray is 2000 long (0x00406f50)
    let range = if mounted.0.is_some() { 2000.0 } else { range };
    let end = start + dir * range;
    let mut best: Option<(f32, AimHit)> = None;
    // the level and the objects: the physics ray, through Roman and the grunts' capsules (their parts are below)
    let grunt_bodies: std::collections::HashSet<_> = grunts.iter().map(|g| g.1.id).collect();
    let (mut from, mut left) = (start, range);
    while left > 0.0 {
        let Some((b, at, _)) = physics.raycast(from, dir, left) else { break };
        let e = physics.bodies.get(&b).copied();
        if grunt_bodies.contains(&b) || (e.is_some() && e == roman_e) {
            left -= at.distance(from) + 0.05;
            from = at + dir * 0.05;
            continue;
        }
        best = Some((at.distance(start), AimHit::Object(e, b)));
        break;
    }
    // the AIs (Ai_InitGeneric 0x004247f0 puts every AI in +0xe9c): their collision parts posed by the skeleton
    if let Some(h) = hulls.as_ref() {
        let near = |p: Vec3| (start + dir * (p - start).dot(dir).clamp(0.0, range)).distance(p) < 15.0;
        let mut test = |parts: &[crate::hitparts::Hull], a: &crate::skin::Animator, hit: AimHit| {
            if parts.is_empty() {
                return;
            }
            if let Some((f, _)) = crate::hitparts::hit(parts, a, &joints, &binds, start, end) {
                let d = f * range;
                if best.as_ref().is_none_or(|(bd, _)| d < *bd) {
                    best = Some((d, hit));
                }
            }
        };
        for (e, _, t, a) in &grunts {
            if let Some(a) = a.filter(|_| near(t.translation() + Vec3::Y * 5.0)) {
                test(&h.grunt, a, AimHit::Grunt(e));
            }
        }
        for (e, c, t, a) in &companions {
            if let Some(a) = a.filter(|_| near(t.translation() + Vec3::Y * 5.0)) {
                test(if c.q { &h.q111 } else { &h.molmer }, a, AimHit::Friend(e));
            }
        }
    }
    // +0x392: an AI is a target (its class is none of the five excluded); +0x394: an AI teammate (vt+0x10c and
    // vt+0xe4: Q111Object 0x005a6df8, MolmerObject 0x005ae5f8; grunts 0x005a9cf0 have vt+0xe4 false)
    let (on, friendly, object) = match best.as_ref().map(|b| &b.1) {
        Some(AimHit::Grunt(e)) => (true, false, Some(*e)),
        Some(AimHit::Friend(e)) => (true, true, Some(*e)),
        Some(AimHit::Object(e, b)) => {
            let e = e.or_else(|| placed.iter().find(|(_, p)| p.collider == Some(*b)).map(|(e, _)| e));
            let class = e.and_then(|e| classes.get(e).ok().copied());
            (class.is_some_and(|c| c.on(true)), class == Some(AimClass::Peasant), e)
        }
        None => (false, false, None),
    };
    if std::env::var("CROSSDBG").is_ok() && (on != xhair.on || friendly != xhair.friendly || object != xhair.object) {
        let what = match best.as_ref().map(|b| &b.1) {
            Some(AimHit::Grunt(_)) => "grunt".to_owned(),
            Some(AimHit::Friend(e)) => format!("companion q={}", companions.get(*e).is_ok_and(|c| c.1.q)),
            Some(AimHit::Object(..)) => object.and_then(|e| placed.get(e).ok()).map_or("level".to_owned(), |p| p.1.template.clone()),
            None => "nothing".to_owned(),
        };
        info!("CROSSDBG on {on} friendly {friendly} {what} at {:.1}", best.as_ref().map_or(-1.0, |b| b.0));
    }
    xhair.on = on;
    xhair.friendly = friendly;
    xhair.object = object;
    xhair.point = Some(start + dir * best.as_ref().map_or(range, |b| b.0));
    // Hud_ProjectMarkers 0x00406840: a weapon change sets +0x390 = 2 (0x004a16b0, game_09.cpp:1758); with 2 every crosshair plays
    // `_out`, then a state change plays the current one's `_in` (on) or `_out` (off) from its start
    if xhair.weapon != state.weapon {
        xhair.weapon = state.weapon;
        xhair.shown = 2;
    }
    if state.visible && !state.cinematic {
        if xhair.shown == 2 {
            for n in CROSSHAIRS {
                xhair.anims.insert((*n).to_owned(), (false, 0.0));
            }
        }
        let s = on as u8;
        if s != xhair.shown {
            let cur = if gun_mg { "ui_mg_crosshair" } else { "ui_crosshair" };
            xhair.anims.insert(cur.to_owned(), (on, 0.0));
            xhair.shown = s;
        }
    }
}

/// Poses the crosshair parts by their anim (`<element>_in` / `_out`), bone translation minus its bind position.
fn crosshair_pose(time: Res<Time>, mut xhair: ResMut<Crosshair>, lib: Option<ResMut<crate::skin::AnimLibrary>>, mut parts: Query<&mut CrossPart>, mut meshes: ResMut<Assets<Mesh>>) {
    let Some(mut lib) = lib else { return };
    let dt = time.delta_secs();
    for (_, t) in xhair.anims.values_mut() {
        *t += dt;
    }
    for mut p in &mut parts {
        let Some(&(on, t)) = xhair.anims.get(&p.element) else { continue };
        let Some(anim) = lib.anim(&format!("{}_{}", p.element, if on { "in" } else { "out" })) else { continue };
        let t = t.min(anim.duration);
        if p.applied == Some((on, t)) {
            continue;
        }
        p.applied = Some((on, t));
        let pos: Vec<[f32; 3]> = p
            .verts
            .iter()
            .map(|(v, bone, bind)| {
                let o = bone.as_ref().and_then(|b| anim.translation(b, t)).map(|a| a.truncate() - *bind).unwrap_or(Vec2::ZERO);
                [v.x + o.x, v.y + o.y, 0.0]
            })
            .collect();
        if let Some(mut m) = meshes.get_mut(&p.mesh) {
            m.insert_attribute(Mesh::ATTRIBUTE_POSITION, pos);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A monospace font: every printable character 10 units wide at scale 1 (line height 25).
    fn font() -> FontFile {
        let g = Glyph { x: 0.0, y: 0.0, width: 9.0, height: 25.0, xoffset: 0.0, xadvance: 10.0 };
        FontFile { common: FontCommon { line_height: 25.0, scale_w: 256.0, scale_h: 512.0 }, chars: (32u32..127).map(|c| (c.to_string(), g)).collect() }
    }

    /// Chat_AddMessage 0x0046aa60: breaks at the last space whose prefix fits 490 units (49 characters here).
    #[test]
    fn chat_wraps_at_spaces() {
        let mut c = Chat::default();
        let words = "aaaa ".repeat(15);
        c.add_message(&font(), words.trim_end(), 1, 0.0);
        assert_eq!(c.lines.len(), 2);
        assert_eq!(c.lines[0].0, "aaaa ".repeat(9) + "aaaa");
        assert_eq!(c.lines[1].0.len(), 74 - 50);
        assert!(c.lines.iter().all(|l| l.1 == 1));
    }

    /// Without a space the line breaks at the first overflowing character, which is lost (original bug).
    #[test]
    fn chat_drops_the_overflow_character() {
        let mut c = Chat::default();
        let s: String = (0..60).map(|i| char::from(b'a' + (i % 26) as u8)).collect();
        c.add_message(&font(), &s, 2, 0.0);
        assert_eq!(c.lines.len(), 2);
        // the prefix of 50 characters is the first wider than 490 units: it is kept (500 units: the line overflows by
        // a character) and the 51st character, where the line is cut, is lost
        assert_eq!(c.lines[0].0.chars().count(), 50);
        assert_eq!(c.lines[1].0, s[51..]);
    }

    /// Three lines at most: a fourth scrolls the oldest off (Chat_AddLine 0x0046a980).
    #[test]
    fn chat_keeps_three_lines() {
        let mut c = Chat::default();
        for t in ["one", "two", "three", "four"] {
            c.add_message(&font(), t, 1, 0.0);
        }
        assert_eq!(c.lines.iter().map(|l| l.0.as_str()).collect::<Vec<_>>(), ["two", "three", "four"]);
    }

    /// UiText_FitScale 0x00465720: the prompt fitted to 576 units.
    #[test]
    fn fit_to_576() {
        let f = font();
        assert_eq!(fit_scale(&f, "short", 1.0, 576.0), 1.0);
        let long = "x".repeat(80);
        let s = fit_scale(&f, &long, 1.0, 576.0);
        assert!(text_width(&f, &long, s) <= 576.0 && s > 0.71, "{s}");
    }
}
