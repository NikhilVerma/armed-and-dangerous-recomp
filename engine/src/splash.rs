//! The front end's plain screens: the legal splash before the intro movies, the loading screen of a level change and
//! the exit screen.
//!
//! - **Legal splash** ("LegalText", UI_Init 0x0048ff90: a timed screen of layer `b_legal`, 4.0 s, flags 0x23a0). The
//!   timed screen (0x00493520 builds it: its duration +0x68, the next screen +0x6c) is replaced or popped once its
//!   time reaches the duration (0x00493590). Game_StartLevel 0x00403690 pushes MainMenu, the intro movies, then
//!   LegalText on the first front-end load, so it shows first (capture work/shots/origmovA frame 300): the 512 x 256
//!   texture at its own size in the 640 x 480 screen over black. As every screen it fades in over 0.25 s and out over
//!   0.5 s (0x00490cb0 / 0x00490e90, gamemenu.rs), and Back pops it (UiScreen_Update 0x004929e0: the cancel), which
//!   skips it; the remaster's Select / a click (a trackpad tap) skip it too. Then the intro movies play
//!   (cutscenes.rs).
//! - **Loading screen** (LoadingScreen_Draw 0x00449ac0, the "Loading" screen, pui_loading): the
//!   ad image (b_ad_image) over black, a progress bar (x 0.245..0.755, y 0.175..0.225 of the screen in
//!   0x80008000, the fill from x 0.25 to 0.25 + progress x 0.5, y 0.1825..0.2175 in 0xc000c000) and, when there is
//!   no image, the loading text (UI_GetLoadingText 0x00447600: the language's record of table 0x00588e78,
//!   "Loading...", scale 1.5, #a0ffa0, at (0.5, 0.139)). The screen's y is taken from the bottom, as the texts'
//!   (guess: no capture of the loading screen). The remaster shows it from the frame a level change is asked for
//!   (levelreset.rs: the rebuild waits one frame so this screen is on show through it) until the new level has
//!   settled; the progress is the settling's.
//! - **Exit screen** ("ExitMenu", 0x00493770: no layer, flags 0x23a0; its update 0x00493790 calls the game's quit,
//!   vt+0x40). Yes to "Exit the game?" changes to it: the menu fades to black (0.5 s) and the game quits.
//!
//! The legal and exit screens carry flag 0x2000: focused, they hold the music (0x004928f0 -> 0x0044f510: the music
//! voice paused and Music_Update locked, counted; 0x0044f540 lets go): gameaudio.rs `Music::hold`.
//!
//! API: [`SplashPlugin`], [`ShowLegal`] (cutscenes.rs sends it at boot), [`Splash`].

use crate::AppState;
use crate::gamemenu::{GameMenu, MenuOwned, Page};
use crate::hud::{Anchor, HUD_LAYER, HudElement, HudText};
use bevy::asset::LoadState;
use bevy::camera::visibility::RenderLayers;
use bevy::prelude::*;
use bevy::sprite_render::{AlphaMode2d, ColorMaterial, MeshMaterial2d};

/// The LegalText screen's time (UI_Init 0x0048ff90: 4.0).
pub const LEGAL_SECONDS: f32 = 4.0;
/// The screen fades (gamemenu.rs: 0x00490cb0 out over 0.5 s, 0x00490e90 in over 0.25 s).
const FADE_IN: f32 = 0.25;
const FADE_OUT: f32 = 0.5;
/// Over the menus (their fade quad is at z 90).
const SPLASH_Z: f32 = 95.0;

/// Show the legal splash, then play `then` (the intro movies).
#[derive(Message, Clone)]
pub struct ShowLegal {
    pub then: crate::movie::PlayMovies,
}

/// A timed screen on show (0x00493520's fields: the duration +0x68; the screen's time +0x50).
#[derive(Clone)]
pub struct TimedScreen {
    pub name: &'static str,
    pub duration: f32,
    pub time: f32,
    /// seconds into the fade-out once its time is up (or it was skipped)
    pub leaving: Option<f32>,
    then: Option<crate::movie::PlayMovies>,
}

impl TimedScreen {
    /// The black over it: fading in (alpha t^3 of the menus' fade, 1 -> 0 over 0.25 s) and out (0 -> 1 over 0.5 s).
    pub fn black(&self) -> f32 {
        let v = match self.leaving {
            Some(l) => (l / FADE_OUT).min(1.0),
            None => (1.0 - self.time / FADE_IN).max(0.0),
        };
        v * v * v
    }
}

/// The splash on show, if any.
#[derive(Resource, Default)]
pub struct Splash {
    pub showing: Option<TimedScreen>,
}

impl Splash {
    pub fn active(&self) -> bool {
        self.showing.is_some()
    }
}

/// Menus take no input while a splash is on (it is the focused screen).
pub fn no_splash(s: Res<Splash>) -> bool {
    !s.active()
}

pub struct SplashPlugin;

impl Plugin for SplashPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Splash>()
            .add_message::<ShowLegal>()
            .add_systems(Startup, spawn)
            .add_systems(Update, (legal, draw_legal, loading_screen, exit_screen, log_state).chain().before(crate::gamemenu::MenuSystems));
    }
}

/// The legal splash's quads.
#[derive(Component)]
struct LegalRoot;
#[derive(Component)]
struct LegalBlack(Handle<ColorMaterial>);

/// The loading screen's parts.
#[derive(Component)]
struct LoadingRoot;
#[derive(Component)]
struct LoadingImage(Handle<Image>);
#[derive(Component)]
struct LoadingFill;
/// The bar's two quads (shown only once there is progress)
#[derive(Component)]
struct LoadingBar;
#[derive(Component)]
struct LoadingText;

fn root(commands: &mut Commands, name: &str, marker: impl Bundle) -> Entity {
    commands
        .spawn((
            HudElement { name: name.into(), pos: Vec2::new(320.0, 240.0), anchor: Anchor::Centre, full: true },
            marker,
            MenuOwned,
            Transform::from_xyz(0.0, 0.0, SPLASH_Z),
            Visibility::Hidden,
            RenderLayers::layer(HUD_LAYER),
        ))
        .id()
}

fn quad(commands: &mut Commands, meshes: &mut Assets<Mesh>, materials: &mut Assets<ColorMaterial>, size: Vec2, at: Vec3, colour: Color, texture: Option<Handle<Image>>) -> (Entity, Handle<ColorMaterial>) {
    let mat = materials.add(ColorMaterial { color: colour, texture, alpha_mode: AlphaMode2d::Blend, ..default() });
    let e = commands.spawn((Mesh2d(meshes.add(Rectangle::new(size.x, size.y))), MeshMaterial2d(mat.clone()), Transform::from_translation(at), RenderLayers::layer(HUD_LAYER))).id();
    (e, mat)
}

fn spawn(mut commands: Commands, assets: Res<AssetServer>, mut meshes: ResMut<Assets<Mesh>>, mut materials: ResMut<Assets<ColorMaterial>>) {
    // the legal splash: black, b_legal at its 512 x 256 in the middle, the fade's black over it
    let r = root(&mut commands, "splash_legal", LegalRoot);
    let (back, _) = quad(&mut commands, &mut meshes, &mut materials, Vec2::new(4000.0, 480.0), Vec3::ZERO, Color::BLACK, None);
    let (art, _) = quad(&mut commands, &mut meshes, &mut materials, Vec2::new(512.0, 256.0), Vec3::new(0.0, 0.0, 0.5), Color::WHITE, Some(assets.load("ui/textures/b_legal.png")));
    let (black, mat) = quad(&mut commands, &mut meshes, &mut materials, Vec2::new(4000.0, 480.0), Vec3::new(0.0, 0.0, 1.0), Color::BLACK, None);
    commands.entity(black).insert(LegalBlack(mat));
    commands.entity(r).add_children(&[back, art, black]);

    // the loading screen: black, the ad image (2:1) across a 16:9 width, the bar and its fill, the text
    let image: Handle<Image> = assets.load("ui/textures/b_ad_image.png");
    let r = root(&mut commands, "splash_loading", LoadingRoot);
    let (back, _) = quad(&mut commands, &mut meshes, &mut materials, Vec2::new(4000.0, 480.0), Vec3::ZERO, Color::BLACK, None);
    let (ad, _) = quad(&mut commands, &mut meshes, &mut materials, Vec2::new(480.0 * 16.0 / 9.0, 240.0 * 16.0 / 9.0), Vec3::new(0.0, 0.0, 0.5), Color::WHITE, Some(image.clone()));
    commands.entity(ad).insert(LoadingImage(image));
    // bar: x 0.245..0.755, y 0.175..0.225 (y from the bottom: 84..108 of 480)
    let (bar, _) = quad(&mut commands, &mut meshes, &mut materials, Vec2::new(0.51 * 640.0, 0.05 * 480.0), Vec3::new(0.0, 0.2 * 480.0 - 240.0, 1.0), Color::srgba(0.0, 0x80 as f32 / 255.0, 0.0, 0x80 as f32 / 255.0), None);
    let (fill, _) = quad(&mut commands, &mut meshes, &mut materials, Vec2::new(1.0, 0.035 * 480.0), Vec3::new(0.0, 0.2 * 480.0 - 240.0, 1.5), Color::srgba(0.0, 0xc0 as f32 / 255.0, 0.0, 0xc0 as f32 / 255.0), None);
    commands.entity(fill).insert((LoadingFill, LoadingBar));
    commands.entity(bar).insert(LoadingBar);
    commands.entity(r).add_children(&[back, ad, bar, fill]);
    // the text (0x00495e80: colour 0xa0ffa0, size 1.5, at (0.5, 0.139), centred)
    commands.spawn((
        HudElement { name: "splash_loading_text".into(), pos: Vec2::new(320.0, 0.139 * 480.0), anchor: Anchor::Centre, full: true },
        HudText { text: loading_text(None).into(), built: None, scale: 1.5, colour: Color::srgb_u8(0xa0, 0xff, 0xa0), fit: 0.0 },
        LoadingText,
        MenuOwned,
        Transform::from_xyz(0.0, 0.0, SPLASH_Z + 2.0),
        Visibility::Hidden,
        RenderLayers::layer(HUD_LAYER),
    ));
}

/// UI_GetLoadingText 0x00447600: the text of the language's record in table 0x00588e78 (the language code of
/// 0x00447590: "engl", "germ", "fren", "span", "ital"; any other: the table's end record, "Loading...").
pub fn loading_text(language: Option<&str>) -> &'static str {
    match language {
        Some("germ") => "Laden...",
        Some("fren") => "Chargement en cours...",
        Some("span") => "Cargando...",
        Some("ital") => "Caricamento...",
        _ => "Loading...",
    }
}

/// The timed screen's update (0x00493590): its time runs, at the duration (or on Back / Select / a click) it fades out
/// and is popped; its next screen is the intro movies. Its focus holds the music (flag 0x2000).
#[allow(clippy::too_many_arguments)]
fn legal(
    time: Res<Time<Real>>,
    mut ev: MessageReader<ShowLegal>,
    mut splash: ResMut<Splash>,
    ctl: Res<crate::input::Controls>,
    mouse: Res<ButtonInput<MouseButton>>,
    mut music: ResMut<crate::gameaudio::Music>,
    mut play: MessageWriter<crate::movie::PlayMovies>,
    (playtest, mut stepped): (Option<Res<crate::playtest::Playtest>>, Local<bool>),
) {
    for e in ev.read() {
        if splash.showing.is_none() {
            music.hold();
        }
        splash.showing = Some(TimedScreen { name: "LegalText", duration: LEGAL_SECONDS, time: 0.0, leaving: None, then: Some(e.then.clone()) });
        info!("EVENT splash: LegalText shown ({LEGAL_SECONDS} s)");
    }
    let Some(s) = splash.showing.as_mut() else { return };
    // a scripted run steps game time by a fixed amount a frame; the real clock would make the splash's length
    // depend on the machine's load (remaster-engine.md "Traps")
    *stepped = playtest.is_some();
    let dt = if *stepped { 1.0 / 60.0 } else { time.delta_secs().min(0.1) };
    s.time += dt;
    let skip = ctl.pressed(crate::input::Act::Back) || ctl.pressed(crate::input::Act::Select) || mouse.just_pressed(MouseButton::Left);
    if s.leaving.is_none() && (s.time >= s.duration || (skip && s.time > 0.05)) {
        s.leaving = Some(0.0);
        info!("EVENT splash: {} {}", s.name, if s.time >= s.duration { "timed out" } else { "skipped" });
    }
    if let Some(l) = s.leaving.as_mut() {
        *l += dt;
        if *l >= FADE_OUT {
            if let Some(then) = s.then.take() {
                play.write(then);
            }
            splash.showing = None;
            music.release();
        }
    }
}

fn draw_legal(splash: Res<Splash>, mut root: Query<&mut Visibility, With<LegalRoot>>, black: Query<&LegalBlack>, mut materials: ResMut<Assets<ColorMaterial>>) {
    for mut v in &mut root {
        let want = if splash.active() { Visibility::Inherited } else { Visibility::Hidden };
        if *v != want {
            *v = want;
        }
    }
    if let Some(s) = splash.showing.as_ref() {
        for b in &black {
            if let Some(mut m) = materials.get_mut(&b.0) {
                m.color.set_alpha(s.black());
            }
        }
    }
}

/// What the loading screen shows now (the state log).
#[derive(Resource, Default)]
struct LoadingShown {
    shown: bool,
    image: bool,
    progress: f32,
}

/// The loading screen: from the frame a level change is asked for to the end of the settling (levelreset.rs).
#[allow(clippy::too_many_arguments)]
fn loading_screen(
    state: Res<State<AppState>>,
    restart: Res<crate::levelreset::RestartLevel>,
    settle: crate::levelreset::SettleProgress,
    assets: Res<AssetServer>,
    mut roots: Query<&mut Visibility, (With<LoadingRoot>, Without<LoadingText>)>,
    images: Query<&LoadingImage>,
    mut fill: Query<&mut Transform, With<LoadingFill>>,
    mut bars: Query<&mut Visibility, (With<LoadingBar>, Without<LoadingRoot>, Without<LoadingText>)>,
    mut text: Query<&mut Visibility, (With<LoadingText>, Without<LoadingRoot>)>,
    mut shown: Local<LoadingShown>,
    play: Option<ResMut<crate::playtest::PlayState>>,
) {
    let on = restart.to.is_some() || *state.get() == AppState::Loading;
    // LoadingScreen_Draw: the image when there is one, else the text
    let image = images.iter().next().is_some_and(|i| !matches!(assets.load_state(&i.0), LoadState::Failed(_)));
    let progress = if on { settle.progress() } else { 0.0 };
    let vis = |b: bool| if b { Visibility::Inherited } else { Visibility::Hidden };
    for mut v in &mut roots {
        if *v != vis(on) {
            *v = vis(on);
        }
    }
    for mut v in &mut text {
        if *v != vis(on && !image) {
            *v = vis(on && !image);
        }
    }
    // the bar only once the progress is not 0 (0x00449ac0: both quads under `+0x54 != 0`)
    for mut v in &mut bars {
        if *v != vis(progress != 0.0) {
            *v = vis(progress != 0.0);
        }
    }
    // the fill from x 0.25 to 0.25 + progress x 0.5 of the screen (0x005bcb4c = 0.5, 0x005bcbe4 = 0.25)
    let w = (progress * 0.5 * 640.0).max(0.01);
    for mut t in &mut fill {
        t.scale.x = w;
        t.translation.x = 0.25 * 640.0 + w / 2.0 - 320.0;
    }
    if on && !shown.shown {
        info!("EVENT loading screen: shown ({})", if image { "ad image" } else { loading_text(None) });
    }
    *shown = LoadingShown { shown: on, image, progress };
    if let Some(mut p) = play {
        p.0.insert("loading_screen".into(), serde_json::json!({"shown": shown.shown, "image": shown.image && on, "text": on && !image, "progress": (progress * 100.0).round() / 100.0}));
    }
}

/// The exit screen (0x00493770 / 0x00493790): once the menu has faded to it, the game quits; it holds the music
/// while it is up (flag 0x2000).
fn exit_screen(menu: Res<GameMenu>, mut music: ResMut<crate::gameaudio::Music>, mut held: Local<bool>, mut exit: MessageWriter<AppExit>) {
    let up = menu.page == Page::Exit;
    if up && !*held {
        *held = true;
        music.hold();
        info!("EVENT exit screen: shown");
    }
    if up && menu.on_screen() == Some(Page::Exit) {
        info!("EVENT exit screen: the game quits");
        exit.write(AppExit::Success);
    }
}

fn log_state(splash: Res<Splash>, music: Res<crate::gameaudio::Music>, play: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut p) = play else { return };
    let v = splash.showing.as_ref().map(|s| serde_json::json!({"name": s.name, "time": (s.time * 100.0).round() / 100.0, "leaving": s.leaving.is_some()}));
    p.0.insert("splash".into(), v.unwrap_or(serde_json::Value::Null));
    p.0.insert("music_held".into(), music.held().into());
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fades_and_text() {
        let mut s = TimedScreen { name: "LegalText", duration: LEGAL_SECONDS, time: 0.0, leaving: None, then: None };
        assert_eq!(s.black(), 1.0);
        s.time = FADE_IN;
        assert_eq!(s.black(), 0.0);
        s.leaving = Some(FADE_OUT / 2.0);
        assert!((s.black() - 0.125).abs() < 1e-6);
        assert_eq!(loading_text(Some("fren")), "Chargement en cours...");
        assert_eq!(loading_text(Some("dflt")), "Loading...");
    }
}
