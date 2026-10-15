//! The end-of-mission Stats screen (docs/subsystems/win-sequence.md; update 0x00494440, on show 0x00494050), shown
//! when the win message ("Well Done! ...") is dismissed (0x0044a230 state 10), then the next level (state 11).
//!
//! Layout in the original's 640 x 480 (y up): title ui_stats_title at (320, 421), scale 1.4, white; four bars
//! (ui_progressbar: label right-aligned before the bar, bar x 248..446, fill from x 251, 192 wide, value at x 459,
//! #a0ffa0): shots fired 328.1, shots hit 304.1 ("%d (%d%%)"), enemies killed 280.1, head shots 256.1; money lines
//! (ui_textoption, scale 1.2, #a0a0ff; label ending at x 320, value from x 336): kills 177.8, accidents 153.8,
//! rescues 129.8, loot 105.8; the wanted poster (b_wanted_reward, "$%d" of the money) at (320, 252.44), spinning in
//! over the lines; the prompt ui_stats_ok at y 72, #c0c0c0, scale 1.2, blinking, only while waiting.
//!
//! Phases: 0 reveal over 3 s (bars shown = min(max x reveal, value), money x reveal, ui_move ticks at most every
//! 0.1 s); 1 wait for MenuSelect; 2 the poster over 0.5 s (rotation v x 6.8068 rad, size 1.5 - v^2), ui_select;
//! 3 the money counts up over 3 s; 4 wait; 5 leave. MenuSelect jumps the current phase to its end.
//!
//! The next level (next_level_name, lev_1_m1_b) is not built yet: the game returns to the title screen (the
//! original's lev_shell fallback when a level fails to load) with the money kept (options.json).

use crate::levelreset::LevelAppExt;
use crate::AppState;
use crate::hud::{Anchor, HUD_LAYER, HudElement, HudText, TextAlign};
use bevy::camera::visibility::RenderLayers;
use bevy::prelude::*;
use bevy::sprite_render::{ColorMaterial, MeshMaterial2d};

const GREEN: Color = Color::srgb(0xa0 as f32 / 255.0, 1.0, 0xa0 as f32 / 255.0);
const BLUE: Color = Color::srgb(0xa0 as f32 / 255.0, 0xa0 as f32 / 255.0, 1.0);
const GREY: Color = Color::srgb(0xc0 as f32 / 255.0, 0xc0 as f32 / 255.0, 0xc0 as f32 / 255.0);
const BAR_Y: [f32; 4] = [328.1, 304.1, 280.1, 256.1];
const MONEY_Y: [f32; 4] = [177.8, 153.8, 129.8, 105.8];
const POSTER: Vec2 = Vec2::new(320.0, 252.44);

#[derive(Resource, Default)]
pub struct StatScreen {
    pub open: bool,
    shown: bool,
    phase: u8,
    reveal: f32,
    /// the money lines after the difficulty factor (Normal 1.0: Game+0x564, table 0x00589188)
    money_lines: [f32; 4],
    from: f32,
    to: f32,
    tick: f32,
    last_values: [i64; 8],
    frame: u32,
    left: bool,
}

/// What each entity of the screen shows.
#[derive(Component, Clone, Copy, PartialEq)]
enum StatPart {
    Root,
    BarValue(usize),
    BarFill(usize),
    Money(usize),
    Poster,
    PosterQuad,
    PosterText,
    Prompt,
}

pub struct StatScreenPlugin;

impl Plugin for StatScreenPlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<StatScreen>().add_systems(Update, (open, update).chain());
    }
}

fn spawn_text(commands: &mut Commands, name: &str, text: &str, pos: Vec2, scale: f32, colour: Color, align: f32, part: StatPart) -> Entity {
    commands
        .spawn((
            HudElement { name: format!("stats_{name}"), pos, anchor: Anchor::Centre, full: true },
            HudText { text: text.to_owned(), built: None, scale, colour, fit: 0.0 },
            TextAlign(align),
            part,
            Transform::default(),
            Visibility::Inherited,
            RenderLayers::layer(HUD_LAYER),
            crate::gamemenu::MenuOwned,
        ))
        .id()
}

/// Opens the screen when the win message has been dismissed.
#[allow(clippy::too_many_arguments)]
fn open(
    mut commands: Commands,
    mut s: ResMut<StatScreen>,
    mission: Option<Res<crate::mission::Mission>>,
    state: Res<State<AppState>>,
    mut next: ResMut<NextState<AppState>>,
    mut menu: ResMut<crate::gamemenu::GameMenu>,
    mut vtime: ResMut<Time<Virtual>>,
    stats: Res<crate::stats::MissionStats>,
    texts: Res<crate::gamemenu::Texts>,
    assets: Res<AssetServer>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<ColorMaterial>>,
    (opts, difficulty): (Res<crate::options::GameOptions>, Res<crate::gamedata::Difficulty>),
) {
    let Some(m) = mission else { return };
    if s.shown || m.outcome != crate::mission::Outcome::Won || m.showing.is_some() || *state.get() != AppState::Playing {
        return;
    }
    s.shown = true;
    s.open = true;
    s.phase = 0;
    s.reveal = 0.0;
    s.money_lines = [stats.kills_money, stats.accidents_money, stats.rescues_money, stats.loot_money].map(|v| v * difficulty.factors().money);
    s.from = opts.money;
    s.to = (opts.money + s.money_lines.iter().map(|v| v.trunc()).sum::<f32>()).max(0.0);
    menu.page = crate::gamemenu::Page::Stats;
    next.set(AppState::Paused);
    vtime.pause();
    info!("EVENT stats screen: {:?}", *stats);

    // the page: the shell background (b_background_01 at #969696) over black, then the texts
    let root = commands
        .spawn((
            HudElement { name: "stats_root".into(), pos: Vec2::new(320.0, 240.0), anchor: Anchor::Centre, full: true },
            StatPart::Root,
            Transform::default(),
            Visibility::Inherited,
            RenderLayers::layer(HUD_LAYER),
            crate::gamemenu::MenuOwned,
        ))
        .id();
    let black = materials.add(ColorMaterial { color: Color::BLACK, ..default() });
    let bg = materials.add(ColorMaterial { color: Color::srgb(0.59, 0.59, 0.59), texture: Some(assets.load("ui/textures/b_background_01.png")), ..default() });
    let b0 = commands.spawn((Mesh2d(meshes.add(Rectangle::new(4000.0, 480.0))), MeshMaterial2d(black), Transform::from_xyz(0.0, 0.0, 1.0), RenderLayers::layer(HUD_LAYER))).id();
    let b1 = commands.spawn((Mesh2d(meshes.add(Rectangle::new(854.0, 480.0))), MeshMaterial2d(bg), Transform::from_xyz(0.0, 0.0, 2.0), RenderLayers::layer(HUD_LAYER))).id();
    commands.entity(root).add_children(&[b0, b1]);

    spawn_text(&mut commands, "title", &texts.get("ui_stats_title"), Vec2::new(320.0, 421.0), 1.4, Color::WHITE, 0.0, StatPart::Root);
    let labels = ["ui_stats_shotsfired", "ui_stats_shotshit", "ui_stats_enemieskilled", "ui_stats_headshots"];
    for (i, l) in labels.iter().enumerate() {
        let y = BAR_Y[i];
        spawn_text(&mut commands, "bar_label", &texts.get(l), Vec2::new(240.0, y), 1.0, GREEN, 1.0, StatPart::Root);
        spawn_text(&mut commands, "bar_value", "", Vec2::new(459.0, y), 1.0, GREEN, -1.0, StatPart::BarValue(i));
        // the bar (0x0046d130: three rectangles through Ui_DrawPixelRect 0x0046b200, the frame, the fill to the
        // position and the rest): frame 0xff204000, fill 0xff40c040
        let frame = materials.add(ColorMaterial { color: Color::srgb_u8(0x20, 0x40, 0x00), ..default() });
        let fill = materials.add(ColorMaterial { color: Color::srgb_u8(0x40, 0xc0, 0x40), ..default() });
        let bar = commands
            .spawn((
                HudElement { name: "stats_bar".into(), pos: Vec2::new(347.0, y), anchor: Anchor::Centre, full: true },
                StatPart::Root,
                Transform::default(),
                Visibility::Inherited,
                RenderLayers::layer(HUD_LAYER),
                crate::gamemenu::MenuOwned,
            ))
            .id();
        let f = commands.spawn((Mesh2d(meshes.add(Rectangle::new(198.0, 18.0))), MeshMaterial2d(frame), Transform::from_xyz(0.0, 0.0, 14.0), RenderLayers::layer(HUD_LAYER))).id();
        let g = commands
            .spawn((Mesh2d(meshes.add(Rectangle::new(1.0, 12.0))), MeshMaterial2d(fill), Transform::from_xyz(-96.0, 0.0, 15.0), RenderLayers::layer(HUD_LAYER), StatPart::BarFill(i)))
            .id();
        commands.entity(bar).add_children(&[f, g]);
    }
    let money = ["ui_stats_kills", "ui_stats_accidents", "ui_stats_saved", "ui_stats_loot"];
    for (i, l) in money.iter().enumerate() {
        let y = MONEY_Y[i];
        spawn_text(&mut commands, "money_label", &texts.get(l), Vec2::new(320.0, y), 1.2, BLUE, 1.0, StatPart::Root);
        spawn_text(&mut commands, "money_value", "", Vec2::new(336.0, y), 1.2, BLUE, -1.0, StatPart::Money(i));
    }
    // the wanted poster (0x004948d0; its widget 0x00494790 takes the texture b_wanted_reward, 0x0058fb40): 256 x 320 at
    // the end, with the money in black
    let poster_mat = materials.add(ColorMaterial { texture: Some(assets.load("textures/b_wanted_reward.dds")), ..default() });
    let poster = commands
        .spawn((
            HudElement { name: "stats_poster".into(), pos: POSTER, anchor: Anchor::Centre, full: true },
            StatPart::Poster,
            // over the lines (the original draws it last), its money text over it
            Transform::from_xyz(0.0, 0.0, 30.0),
            Visibility::Hidden,
            RenderLayers::layer(HUD_LAYER),
            crate::gamemenu::MenuOwned,
        ))
        .id();
    let q = commands.spawn((Mesh2d(meshes.add(Rectangle::new(512.0, 640.0))), MeshMaterial2d(poster_mat), Transform::IDENTITY, RenderLayers::layer(HUD_LAYER), StatPart::PosterQuad)).id();
    commands.entity(poster).add_children(&[q]);
    let pt = spawn_text(&mut commands, "poster_text", "", POSTER, 2.0, Color::BLACK, 0.0, StatPart::PosterText);
    commands.entity(pt).insert(Transform::from_xyz(0.0, 0.0, 40.0));
    spawn_text(&mut commands, "prompt", "", Vec2::new(320.0, 72.0), 1.2, GREY, 0.0, StatPart::Prompt);
}

#[allow(clippy::too_many_arguments)]
fn update(
    time: Res<Time<Real>>,
    mut s: ResMut<StatScreen>,
    stats: Res<crate::stats::MissionStats>,
    ctl: Res<crate::input::Controls>,
    mouse: Res<ButtonInput<MouseButton>>,
    texts: Res<crate::gamemenu::Texts>,
    mut parts: Query<(&StatPart, &mut HudText, &mut HudElement, &mut Visibility)>,
    mut fills: Query<(&StatPart, &mut Transform), (Without<HudText>, Without<HudElement>)>,
    mut posters: Query<(&StatPart, &mut Visibility), (With<HudElement>, Without<HudText>)>,
    mut text_tf: Query<(&StatPart, &mut Transform), With<HudText>>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut opts: ResMut<crate::options::GameOptions>,
    mission: Option<Res<crate::mission::Mission>>,
    play: Option<ResMut<crate::playtest::PlayState>>,
    mut restart: ResMut<crate::levelreset::RestartLevel>,
) {
    if !s.open {
        return;
    }
    // the state log's "stats" (playtest expect steps): the phase (0 counters, 1 wait, 2 poster, 3 money, 4 wait, 5
    // leave) and the money shown
    if let Some(mut p) = play {
        p.0.insert("stats".into(), serde_json::json!({ "phase": s.phase, "to": s.to }));
    }
    let dt = time.delta_secs();
    s.frame = s.frame.wrapping_add(1);
    let confirm = ctl.pressed(crate::input::Act::Select) || mouse.just_pressed(MouseButton::Left);
    // the lines (0x00494310 every frame of phase 0): the bars through 0x00494160, shown = min(max x reveal, value),
    // the label "%d" / "%d (%d%%)" (Stepper_SetLabelA 0x0046d110) and the bar's position shown / max (the stepper's
    // change callback Stepper_Notify 0x0046d030: none on these bars); the money lines through 0x00494280, "$%d" /
    // "-$%d" of the line x reveal (FitText_SetTextA 0x0046d5c0, fitted by FitText_Layout 0x0046d4b0)
    let bar_vals = |r: f32| -> [(f32, f32); 4] {
        let fired = stats.shots_fired as f32;
        let total = stats.enemies_total.max(stats.enemies_killed) as f32;
        let killed = stats.enemies_killed as f32;
        [
            ((fired * r).min(fired), fired),
            ((fired * r).min(stats.shots_hit as f32), fired),
            ((total * r).min(killed), total),
            ((killed * r).min(stats.head_shots as f32), killed),
        ]
    };
    let mut ticked = false;
    match s.phase {
        0 | 3 => {
            s.reveal = if confirm { 1.0 } else { (s.reveal + dt / 3.0).min(1.0) };
            if s.reveal >= 1.0 {
                s.phase += 1;
                s.reveal = 0.0;
                if s.phase == 4 {
                    opts.money = s.to;
                }
            }
            ticked = true;
        }
        2 => {
            s.reveal = if confirm { 1.0 } else { (s.reveal + dt * 2.0).min(1.0) };
            if s.reveal >= 1.0 {
                sounds.write(crate::audio::PlaySound::ui("ui_select"));
                s.phase = 3;
                s.reveal = 0.0;
            }
        }
        1 | 4 => {
            if confirm {
                s.phase += 1;
                s.reveal = 0.0;
            }
        }
        _ => {}
    }
    // phase 5: leave for the next level
    if s.phase >= 5 && !s.left {
        s.left = true;
        opts.save();
        let next = mission.as_ref().and_then(|m| m.next_level.clone()).unwrap_or_else(|| "lev_shell".into());
        info!("EVENT stats done: money {} -> next level {next} (not built: back to the title)", s.to);
        restart.to = Some(crate::levelreset::RestartTo::Title);
        return;
    }
    let r0 = if s.phase == 0 { s.reveal } else { 1.0 };
    let bars = bar_vals(r0);
    let money_r = if s.phase == 0 { s.reveal } else { 1.0 };
    // the poster's spin fraction (phase 2: 0x00494870 stores it clamped to 0..1)
    let poster_v = match s.phase {
        0 | 1 => 0.0,
        2 => s.reveal,
        _ => 1.0,
    };
    let total = if s.phase == 3 { s.from + (s.to - s.from) * s.reveal } else if s.phase >= 4 { s.to } else { s.from };
    // ui_move ticks while a shown value changes, at most every 0.1 s
    let vals = [bars[0].0 as i64, bars[1].0 as i64, bars[2].0 as i64, bars[3].0 as i64, (s.money_lines[0] * money_r) as i64, (s.money_lines[1] * money_r) as i64, (s.money_lines[3] * money_r) as i64, total as i64];
    s.tick -= dt;
    if ticked && vals != s.last_values && s.tick <= 0.0 {
        sounds.write(crate::audio::PlaySound::ui("ui_move"));
        s.tick = 0.1;
    }
    s.last_values = vals;
    let money = |v: f32| if v < 0.0 { format!("-${}", (-v) as i64) } else { format!("${}", v as i64) };
    let waiting = s.phase == 1 || s.phase == 4;
    for (p, mut t, mut el, mut vis) in &mut parts {
        let text = match *p {
            StatPart::BarValue(i) => {
                let (shown, max) = bars[i];
                if i == 1 {
                    let pct = if max > 0.0 { (shown / max * 100.0) as i64 } else { 0 };
                    format!("{} ({}%)", shown as i64, pct)
                } else {
                    format!("{}", shown as i64)
                }
            }
            StatPart::Money(i) => money(s.money_lines[i] * money_r),
            StatPart::PosterText => {
                *vis = if poster_v > 0.0 { Visibility::Inherited } else { Visibility::Hidden };
                // on the poster's render target at (0.5, 0.2) (WantedReward_Draw 0x00494b70): between the picture and
                // "REWARD", 0.3 of the poster's height below its middle, turning with it
                let size = 1.5 - poster_v * poster_v;
                let a = poster_v * 6.8068;
                el.pos = POSTER + Vec2::from_angle(a).rotate(Vec2::new(0.0, -0.3 * 640.0 * size));
                if (t.scale - 3.2 * size).abs() > 1e-3 {
                    t.scale = 3.2 * size;
                    t.built = None;
                }
                money(total)
            }
            StatPart::Prompt => {
                // blinks by frame count: shown while (frame & 31) < 24
                *vis = if waiting && (s.frame & 31) < 24 { Visibility::Inherited } else { Visibility::Hidden };
                texts.get("ui_stats_ok").replace("%s", &crate::input::key(crate::input::Act::Select))
            }
            _ => continue,
        };
        if t.text != text {
            t.text = text;
            t.built = None;
        }
    }
    for (p, mut tr) in &mut fills {
        match *p {
            StatPart::BarFill(i) => {
                let (shown, max) = bars[i];
                let frac = if max > 0.0 { (shown / max).clamp(0.0, 1.0) } else { 0.0 };
                tr.scale.x = (192.0 * frac).max(0.01);
                tr.translation.x = -96.0 + 96.0 * frac;
            }
            // the 512 x 640 quad is 256 x 320 at the end (size 0.5), turned v x 6.8068 rad (30 degrees at the end)
            StatPart::PosterQuad => {
                let size = 1.5 - poster_v * poster_v;
                tr.rotation = Quat::from_rotation_z(poster_v * 6.8068);
                // (y flipped: the DDS rows run top-down)
                tr.scale = Vec3::new(size, -size, 1.0);
            }
            _ => {}
        }
    }
    for (p, mut tr) in &mut text_tf {
        if *p == StatPart::PosterText {
            tr.rotation = Quat::from_rotation_z(poster_v * 6.8068);
        }
    }
    for (p, mut vis) in &mut posters {
        if *p == StatPart::Poster {
            *vis = if poster_v > 0.0 { Visibility::Inherited } else { Visibility::Hidden };
        }
    }
}
