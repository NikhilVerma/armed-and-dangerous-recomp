//! The in-game HUD from the game's own UI data (tools/remaster_export_ui.py; docs/subsystems/hud.md): element
//! positions in the original 640x480 layout (y up), their quad meshes and textures, the bitmap font. Drawn as a
//! 2D overlay, scaled by screen height / 480 with left / right anchors pinned to the screen edges (the remaster's
//! widescreen rule, hook/enhance/ui_aspect.cpp). Health meters empty by scrolling their texture (3.2), text has a
//! 2 px black shadow, lines are 25 x scale px at 480.

use bevy::asset::RenderAssetUsages;
use bevy::camera::visibility::RenderLayers;
use bevy::math::Affine2;
use bevy::mesh::{Indices, PrimitiveTopology};
use bevy::prelude::*;
use bevy::sprite_render::{ColorMaterial, MeshMaterial2d};
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

/// One drawn HUD element and how to place it.
#[derive(Component, Clone)]
pub struct HudElement {
    pub name: String,
    pub pos: Vec2,
    pub anchor: Anchor,
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

/// Meter quads (texture scroll by value).
#[derive(Component)]
pub struct Meter {
    pub material: Handle<ColorMaterial>,
}

#[derive(Resource)]
pub struct HudAssets {
    layout: LayoutFile,
    font: FontFile,
    font_image: Handle<Image>,
    textures: HashMap<String, Handle<Image>>,
    pub text_material: Handle<ColorMaterial>,
    pub shadow_material: Handle<ColorMaterial>,
}

/// What the HUD shows this frame (filled by the game systems).
#[derive(Resource, Default)]
pub struct HudState {
    pub visible: bool,
    pub health: f32,
    pub q_health: f32,
    pub jonesy_health: f32,
    pub clip: u32,
    pub stickies: u32,
    pub heading: f32,
    /// bearing of the current compass target relative to the camera (radians) and its distance
    pub objective: Option<(f32, f32)>,
    /// up to 3 message lines (chat feed: objectives, subtitles)
    pub messages: Vec<String>,
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
}

pub const HUD_LAYER: usize = 1;

pub struct HudPlugin;

impl Plugin for HudPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<HudState>().init_resource::<PubView>().add_systems(Update, (subtitles, place, texts, meters, compass, ticker, pub_view, visibility).chain());
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
) -> Option<()> {
    let ui = dir.join("ui");
    let layout: LayoutFile = serde_json::from_str(&std::fs::read_to_string(ui.join("layout.json")).ok()?).map_err(|e| warn!("hud: layout.json: {e}")).ok()?;
    let font: FontFile = serde_json::from_str(&std::fs::read_to_string(ui.join("font.json")).ok()?).map_err(|e| warn!("hud: font.json: {e}")).ok()?;
    let font_image = assets.load("ui/font.png");
    let mut textures = HashMap::new();
    for (name, t) in &layout.textures {
        textures.insert(name.clone(), assets.load(format!("ui/{}", t.file)));
    }
    let text_material = materials.add(ColorMaterial { color: Color::WHITE, texture: Some(font_image.clone()), ..default() });
    let shadow_material = materials.add(ColorMaterial { color: Color::BLACK, texture: Some(font_image.clone()), ..default() });
    commands.spawn((
        Camera2d,
        Camera { order: 1, clear_color: ClearColorConfig::None, ..default() },
        RenderLayers::layer(HUD_LAYER),
    ));
    let hud = HudAssets { layout, font, font_image, textures, text_material, shadow_material };

    // the elements of the in-game HUD (page pui_main) this level uses
    const SHOWN: &[&str] = &[
        "ui_weapon_bkd", "ui_player_rifle", "ui_player_grenades", "ui_player_health", "ui_health_q", "ui_health_jonesy",
        "ui_crosshair", "ui_compass", "ui_compass_objective", "ui_ammo_infinity", "ui_ammo_count", "ui_grenade_count",
        "ui_hit_top", "ui_hit_bottom", "ui_hit_left", "ui_hit_right", "ui_player_mg", "ui_clock", "ui_tickerbomb",
        "ui_tickerbomb_set",
    ];
    for e in hud.layout.elements.iter().filter(|e| SHOWN.contains(&e.name.as_str()) && e.pos.is_some() && e.page_name.as_deref().is_none_or(|p| p == "pui_main")) {
        let anchor = match e.anchor.as_deref() {
            Some("left") => Anchor::Left,
            Some("right") => Anchor::Right,
            _ => Anchor::Centre,
        };
        let el = HudElement { name: e.name.clone(), pos: Vec2::from(e.pos.unwrap_or([320.0, 240.0])), anchor };
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
    // the message feed (ui_chat_01..03: 320, 437.3 down, scale 0.8), the prompt (ref_tickerbomb_text 321.6, 329.8,
    // scale 1.0, white) and the briefing (320, 281.1 over ui_text_bkd)
    for (name, pos, scale, colour) in [
        ("ui_message", Vec2::new(320.0, 420.0), 0.8, Color::srgb(1.0, 1.0, 1.0)),
        ("ui_prompt", Vec2::new(321.6, 329.8), 1.0, Color::WHITE),
        ("ui_briefing", Vec2::new(320.0, 281.1), 0.9, Color::srgb_u8(0xd0, 0xd0, 0xff)),
        // speech subtitles (0x00495cb0): size 1.1 fitted to 576 units, centred at x 0.5, y 0.15 of the screen
        ("ui_subtitle", Vec2::new(320.0, 480.0 * 0.15), 1.1, Color::WHITE),
    ] {
        commands.spawn((
            HudElement { name: name.into(), pos, anchor: Anchor::Centre },
            HudText { text: String::new(), built: None, scale, colour, fit: if name == "ui_subtitle" { 576.0 } else { 0.0 } },
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
    for q in quads {
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
        let mut part = commands.spawn((
            Mesh2d(meshes.add(mesh)),
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

/// A pub slot frame: 0..4 the pub's inventory (top row), 5..7 Roman's weapons, 8..12 his specials, 13 ticker bombs
/// (the 14 ref markers of 0x00467fe0, in the bank's order).
#[derive(Component)]
pub struct PubSlot(pub usize);

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

/// What the pub screen shows (gamemenu.rs fills it).
#[derive(Resource, Default)]
pub struct PubView {
    pub open: bool,
    pub message: String,
    pub info: String,
    pub exit_text: String,
    /// the highlighted slot, or the exit button when `on_exit`
    pub highlight: Option<usize>,
    pub on_exit: bool,
    /// (slot, texture) of each item shown
    pub icons: Vec<(usize, &'static str)>,
    /// the award being shown (the suffix of ui_pub_award_*), or empty
    pub award: String,
    pub health: f32,
    pub pool: f32,
    /// bumped when the icons change
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
            HudElement { name: "pub_backdrop".into(), pos: Vec2::new(320.0, 240.0), anchor: Anchor::Centre },
            PubPart,
            crate::gamemenu::MenuOwned,
            Transform::from_xyz(0.0, 0.0, -2.0),
            Visibility::Hidden,
            RenderLayers::layer(HUD_LAYER),
        ))
        .add_child(backdrop);
    let mut slot = 0;
    for e in hud.layout.elements.iter().filter(|e| e.page_name.as_deref() == Some("pui_pub") && e.pos.is_some()) {
        let pos = Vec2::from(e.pos.unwrap_or([320.0, 240.0]));
        let el = HudElement { name: e.name.clone(), pos, anchor: Anchor::Centre };
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
                commands.entity(root).insert(PubSlot(slot));
                slot += 1;
            }
            "ui_pub_cursor" => {
                commands.entity(root).insert(PubHighlight);
            }
            n if n.starts_with("ui_pub_award_") => {
                commands.entity(root).insert(PubAward(n.trim_start_matches("ui_pub_award_").to_owned()));
            }
            _ => {}
        }
        let parts = build_model(commands, meshes, materials, hud, &e.name, &model);
        commands.entity(root).add_children(&parts);
    }
    commands.spawn((
        HudElement { name: "pub_icons".into(), pos: Vec2::new(320.0, 240.0), anchor: Anchor::Centre },
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
    mut parts: Query<(&mut Visibility, Option<&PubHighlight>, Option<&PubAward>), With<PubPart>>,
    mut texts: Query<(&PubText, &mut HudText)>,
    slots: Query<(&PubSlot, &HudElement), Without<PubHighlight>>,
    mut hl: Query<&mut HudElement, With<PubHighlight>>,
    pub_meters: Query<&PubMeter>,
    icons: Query<(Entity, Option<&Children>), With<PubIcons>>,
    mut last: Local<u32>,
) {
    for (mut v, h, a) in &mut parts {
        let show = view.open && (h.is_none() || view.highlight.is_some() || view.on_exit) && a.is_none_or(|a| a.0 == view.award);
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
    let slot_pos = |i: usize| slots.iter().find(|(s, _)| s.0 == i).map(|(_, e)| e.pos);
    for mut e in &mut hl {
        let p = if view.on_exit { Some(Vec2::new(493.072, 416.366)) } else { view.highlight.and_then(slot_pos) };
        if let Some(p) = p {
            e.pos = p;
        }
    }
    for m in &pub_meters {
        let v = if m.pool { view.pool } else { view.health }.clamp(0.0, 1.0);
        if let Some(mut mat) = materials.get_mut(&m.material) {
            mat.uv_transform = Affine2::from_translation(Vec2::new(0.0, -((1.0 - v) * 0.5)));
        }
    }
    let Some(hud) = hud else { return };
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
        for (slot, tex) in &view.icons {
            let (Some(p), Some(img)) = (slot_pos(*slot), hud.textures.get(*tex)) else { continue };
            let size = icon_size(tex);
            let mat = materials.add(ColorMaterial { texture: Some(img.clone()), ..default() });
            let q = commands
                .spawn((Mesh2d(meshes.add(Rectangle::new(size.x, size.y))), MeshMaterial2d(mat), Transform::from_xyz(p.x - 320.0, p.y - 240.0, 12.0), RenderLayers::layer(HUD_LAYER)))
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

#[derive(Component)]
pub struct CompassNeedle;

fn window_size(windows: &Query<&Window>) -> Vec2 {
    windows.single().map(|w| Vec2::new(w.physical_width() as f32, w.physical_height() as f32)).unwrap_or(Vec2::new(1920.0, 1080.0))
}

/// 640x480 (y up) -> overlay coordinates (origin at the centre), scaled by height / 480, anchored.
fn place(windows: Query<&Window>, mut q: Query<(&HudElement, &mut Transform)>) {
    let size = window_size(&windows);
    let s = size.y / 480.0;
    for (e, mut t) in &mut q {
        let x = match e.anchor {
            Anchor::Left => -size.x / 2.0 + e.pos.x * s,
            Anchor::Right => size.x / 2.0 - (640.0 - e.pos.x) * s,
            Anchor::Centre => (e.pos.x - 320.0) * s,
        };
        t.translation = Vec3::new(x, (e.pos.y - 240.0) * s, t.translation.z);
        t.scale = Vec3::new(s, s, 1.0);
    }
}

/// Builds a text mesh: glyph quads in 640x480 units around (0, 0), each line centred, the block centred in y;
/// `<br>` / `\n` break lines, `<rgb=RRGGBB>`..`</rgb>` colour (vertex colours over `base`), `<tab=N>` moves the
/// pen to N widths of 'W' from the line start; other tags are skipped. Also returns the block's size.
fn text_mesh(font: &FontFile, text: &str, scale: f32, offset: Vec2, base: Color) -> (Mesh, Vec2) {
    // in the original a scale-1 line is 25 px at 480 lines (hud.md 3.3)
    let k = scale * 25.0 / font.common.line_height.max(1.0);
    let clean = text.replace("<br>", "\n").replace("<BR>", "\n");
    let w_adv = font.chars.get(&('W' as u32).to_string()).map(|g| g.xadvance * k).unwrap_or(10.0 * k);
    // lay out each line from x = 0: (x, glyph, colour)
    let mut lines: Vec<(Vec<(f32, Glyph, [f32; 4])>, f32)> = Vec::new();
    let mut colour = base.to_linear().to_f32_array();
    for l in clean.split('\n') {
        let mut items = Vec::new();
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
                }
                continue;
            }
            let Some(g) = font.chars.get(&(c as u32).to_string()).copied() else { continue };
            items.push((x, g, colour));
            x += g.xadvance * k;
        }
        lines.push((items, x));
    }
    let (mut pos, mut uv, mut idx, mut col) = (Vec::new(), Vec::new(), Vec::new(), Vec::new());
    let lh = font.common.line_height * k;
    let total_h = lh * lines.len() as f32;
    let max_w = lines.iter().map(|l| l.1).fold(0.0, f32::max);
    for (li, (items, width)) in lines.iter().enumerate() {
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
    }
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
    (m, Vec2::new(max_w, total_h))
}

/// Text elements: the game's values formatted as the drivers do; meshes rebuilt when the text changes.
#[allow(clippy::too_many_arguments)]
fn texts(
    mut commands: Commands,
    hud: Option<Res<HudAssets>>,
    state: Res<HudState>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<ColorMaterial>>,
    mut q: Query<(Entity, &HudElement, &mut HudText, Option<&Children>, Option<&TextAlign>)>,
    assets: Res<AssetServer>,
) {
    let Some(hud) = hud else { return };
    for (e, el, mut t, children, align) in &mut q {
        t.text = match el.name.as_str() {
            // "%d/" + the infinity sign: the rifle's reserve is unlimited (weapon type 2)
            "ui_ammo_count" => if state.unlimited { format!("{}/", state.clip) } else { format!("{}/{}", state.clip, state.ammo) },
            "ui_grenade_count" => format!("{}", state.stickies),
            "ui_message" => state.messages.join("\n"),
            "ui_prompt" => state.prompt.clone().unwrap_or_default(),
            "ui_briefing" => state.briefing.clone().unwrap_or_default(),
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
        let text_mat = materials.add(ColorMaterial { color: Color::WHITE, texture: Some(hud.font_image.clone()), ..default() });
        // shadow 2 px right and down (of 480), drawn first
        let (front_mesh, size) = text_mesh(&hud.font, &t.text, t.scale, Vec2::ZERO, t.colour);
        let (shadow_mesh, _) = text_mesh(&hud.font, &t.text, t.scale, Vec2::new(2.0, -2.0), Color::BLACK);
        let dx = -align.map(|a| a.0).unwrap_or(0.0) * size.x / 2.0;
        let shadow = commands
            .spawn((Mesh2d(meshes.add(shadow_mesh)), MeshMaterial2d(hud.shadow_material.clone()), Transform::from_xyz(dx, 0.0, 20.0), RenderLayers::layer(HUD_LAYER)))
            .id();
        let front = commands
            .spawn((Mesh2d(meshes.add(front_mesh)), MeshMaterial2d(text_mat), Transform::from_xyz(dx, 0.0, 21.0), RenderLayers::layer(HUD_LAYER)))
            .id();
        commands.entity(e).add_children(&[shadow, front]);
        // the message box (ui_mission_briefing, 0x00466060) sits on the band ui_text_bkd: texture b_text_bkd,
        // 856 units wide, stretched vertically to the text (anim 4 = min(text height / 480, 1), 0x00466470); the
        // padding is measured from the original's capture (work/shots/orig_ui_play-480.png: ~12 units)
        if el.name == "ui_briefing" && !t.text.is_empty() {
            let band = materials.add(ColorMaterial { texture: Some(assets.load("ui/textures/b_text_bkd.png")), ..default() });
            let f = commands.spawn((Mesh2d(meshes.add(Rectangle::new(856.0, size.y + 24.0))), MeshMaterial2d(band), Transform::from_xyz(0.0, 0.0, 19.0), RenderLayers::layer(HUD_LAYER))).id();
            commands.entity(e).add_children(&[f]);
        }
        t.built = Some(t.text.clone());
    }
}

/// Speech subtitles: the speaker's colour from the caption table (white when not listed), shown for the line's
/// length; the newest line replaces the one showing.
fn subtitles(time: Res<Time>, mut subs: MessageReader<crate::audio::Subtitle>, hud: Option<Res<HudAssets>>, mut state: ResMut<HudState>) {
    for s in subs.read() {
        if state.subtitles_off || s.text.is_empty() {
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
}

/// Health meters: the texture scrolls by t = clamp((1 - value) * 0.5, 0, 0.5); colour band x = 0.5 below 25 %,
/// 0.25 below 50 %, else 0 (0x00466e70).
fn meters(state: Res<HudState>, mut materials: ResMut<Assets<ColorMaterial>>, q: Query<(&Meter, &ChildOf)>, parents: Query<&HudElement>) {
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
        }
    }
}

/// The compass needle turns with the camera heading; the objective arrow points at the compass target.
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
    mut q: Query<(&HudElement, &mut Visibility), (Without<HudText>, Without<crate::gamemenu::MenuOwned>)>,
    mut texts: Query<(&HudElement, &mut Visibility), (With<HudText>, Without<crate::gamemenu::MenuOwned>)>,
) {
    for (el, mut v) in &mut q {
        let show = state.visible
            && match el.name.as_str() {
                "ui_hit_top" => state.hit_flash[0] > 0.0,
                "ui_hit_bottom" => state.hit_flash[1] > 0.0,
                "ui_hit_left" => state.hit_flash[2] > 0.0,
                "ui_hit_right" => state.hit_flash[3] > 0.0,
                "ui_compass_objective" => state.objective.is_some(),
                "ui_player_rifle" => state.weapon != "machinegun",
                "ui_player_mg" => state.weapon == "machinegun",
                "ui_ammo_infinity" => state.unlimited,
                "ui_clock" | "ui_tickerbomb" => state.ticker_clock.is_some(),
                "ui_tickerbomb_set" => state.ticker_arming.is_some(),
                _ => true,
            };
        *v = if show { Visibility::Inherited } else { Visibility::Hidden };
    }
    for (el, mut v) in &mut texts {
        // paused messages (the briefing box) show over cinematics and pauses too
        let show = state.visible || (el.name == "ui_briefing" && state.briefing.is_some()) || (el.name == "ui_subtitle" && state.subtitle.is_some());
        *v = if show { Visibility::Inherited } else { Visibility::Hidden };
    }
}
