//! Controllers and the active input device (remaster improvement: the original PC build reads a DirectInput
//! joystick with fixed Xbox-era names; here any SDL / gilrs pad works, and the prompts name the buttons of the
//! device in hand).
//!
//! The pad drives the game's actions with the layout of the hook's gamepad layer (docs/subsystems/qol.md, "Game
//! controllers"): left stick move, right stick turn and look, R2 fire, L2 special weapon, R1 / L1 next weapon,
//! D-pad left / right next special weapon, D-pad up / down send / recall teammates, South jump and menu select, East
//! menu back, West reload, North use, R3 toggle zoom, Start pause. Buttons are fed in as the keyboard / mouse
//! bindings they stand for, so every system that reads those reads the pad too; the sticks are analog
//! (`PadState`, read by player.rs).
//!
//! Stick look as the original's (player.md 3: turn_max_speed 0.3 -> yaw rate axis x 0.3 x 10 = 3 rad/s at full
//! deflection, zoomed 0.05; stick shaping exponent 2, zoomed 3 (0x00414c00, pow taken)); pitch at 0.95 / 0.3 of the
//! turn rate scaled like the mouse's measured pitch:turn (0.57): 1.7 rad/s (**guess**: the pitch path's time scale
//! is not traced).
//!
//! Prompts: `key(Act)` gives the text for a prompt's %s on the active device: the keyboard / mouse binding the
//! player last used for the action (the original shows the first binding, 0x00465280: Use "X", MenuSelect
//! "Mouse 1"), or the pad's button in its face colour (Xbox A / B / X / Y, PlayStation Cross / Circle / Square /
//! Triangle).

use bevy::input::gamepad::{Gamepad, GamepadButton};
use bevy::input::mouse::AccumulatedMouseMotion;
use bevy::prelude::*;
use std::sync::atomic::{AtomicU8, Ordering};

/// 0 keyboard and mouse, 1 Xbox-style pad, 2 PlayStation pad
static DEVICE: AtomicU8 = AtomicU8::new(0);
/// the Use binding last pressed: 0 X (the original's first binding), 1 F
static USE_KEY: AtomicU8 = AtomicU8::new(0);
/// the MenuSelect binding last pressed: 0 Mouse 1 (first binding), 1 Enter, 2 Space
static SELECT_KEY: AtomicU8 = AtomicU8::new(0);

/// The actions prompts name.
#[derive(Clone, Copy)]
pub enum Act {
    Use,
    /// MenuSelect: continue a message, pick in the pub
    Select,
    /// MenuCancel
    Cancel,
    Fire,
}

pub fn pad_active() -> bool {
    DEVICE.load(Ordering::Relaxed) != 0
}

/// The text for a prompt's %s.
pub fn key(a: Act) -> String {
    let dev = DEVICE.load(Ordering::Relaxed);
    if dev == 0 {
        return match a {
            Act::Use => ["X", "F"][USE_KEY.load(Ordering::Relaxed) as usize % 2].into(),
            Act::Select => ["Mouse 1", "Enter", "Space"][SELECT_KEY.load(Ordering::Relaxed) as usize % 3].into(),
            Act::Cancel => "Esc".into(),
            Act::Fire => "Mouse 1".into(),
        };
    }
    let ps = dev == 2;
    // face buttons in their print colours
    let (name, rgb) = match (a, ps) {
        (Act::Use, false) => ("Y", "f0d040"),
        (Act::Use, true) => ("Triangle", "40d0a0"),
        (Act::Select, false) => ("A", "60d060"),
        (Act::Select, true) => ("Cross", "80a8ff"),
        (Act::Cancel, false) => ("B", "ff5050"),
        (Act::Cancel, true) => ("Circle", "ff5050"),
        (Act::Fire, false) => ("RT", "ffffff"),
        (Act::Fire, true) => ("R2", "ffffff"),
    };
    format!("<rgb={rgb}>{name}</rgb>")
}

/// The pad's analog input this frame (dead zones applied; zero while the keyboard and mouse are in use).
#[derive(Resource, Default)]
pub struct PadState {
    /// left stick: x right, y forward, length <= 1
    pub mv: Vec2,
    /// right stick: x right, y up, length <= 1, not yet shaped
    pub look: Vec2,
    zoom: bool,
    menu_dir: Option<KeyCode>,
    count: usize,
    which: Option<Entity>,
}

pub struct InputPlugin;

impl Plugin for InputPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<PadState>().add_systems(PreUpdate, (device, pad).chain().after(bevy::input::InputSystems));
    }
}

/// a radial dead zone, rescaled so the edge of the zone reads 0
fn dead(v: Vec2, zone: f32) -> Vec2 {
    let l = v.length();
    if l <= zone {
        Vec2::ZERO
    } else {
        v / l * ((l - zone) / (1.0 - zone)).min(1.0)
    }
}

/// The active device: the one that gave input last.
fn device(keys: Res<ButtonInput<KeyCode>>, mouse: Res<ButtonInput<MouseButton>>, motion: Res<AccumulatedMouseMotion>, pads: Query<&Gamepad>) {
    if keys.just_pressed(KeyCode::KeyX) {
        USE_KEY.store(0, Ordering::Relaxed);
    }
    if keys.just_pressed(KeyCode::KeyF) {
        USE_KEY.store(1, Ordering::Relaxed);
    }
    if mouse.just_pressed(MouseButton::Left) {
        SELECT_KEY.store(0, Ordering::Relaxed);
    } else if keys.just_pressed(KeyCode::Enter) {
        SELECT_KEY.store(1, Ordering::Relaxed);
    } else if keys.just_pressed(KeyCode::Space) {
        SELECT_KEY.store(2, Ordering::Relaxed);
    }
    for p in &pads {
        let used = p.get_just_pressed().next().is_some() || p.left_stick().length() > 0.5 || p.right_stick().length() > 0.5;
        if used {
            // Sony's USB vendor id
            let ps = p.vendor_id() == Some(0x054c);
            DEVICE.store(if ps { 2 } else { 1 }, Ordering::Relaxed);
            return;
        }
    }
    // the pad's own presses come back as keys (below) one frame later: only real keyboard / mouse input switches
    // back, so a key the pad pressed does not (`pad` presses through ButtonInput after this system)
    if keys.get_just_pressed().next().is_some() || mouse.get_just_pressed().next().is_some() || motion.delta.length() > 3.0 {
        DEVICE.store(0, Ordering::Relaxed);
    }
}

/// Feeds the pad into the game: buttons as the bindings they stand for, sticks into `PadState`.
#[allow(clippy::too_many_arguments)]
fn pad(
    pads: Query<(Entity, &Gamepad)>,
    mut st: ResMut<PadState>,
    mut keys: ResMut<ButtonInput<KeyCode>>,
    mut mouse: ResMut<ButtonInput<MouseButton>>,
    state: Res<State<crate::AppState>>,
    menu: Res<crate::gamemenu::GameMenu>,
) {
    st.mv = Vec2::ZERO;
    st.look = Vec2::ZERO;
    let n = pads.iter().count();
    if n != st.count {
        info!("input: {n} gamepad(s)");
        st.count = n;
    }
    // the pad in use: the last one pressed or pushed (a second pad, or a virtual one, may be connected)
    for (e, p) in &pads {
        if p.get_just_pressed().next().is_some() || p.left_stick().length() > 0.5 || p.right_stick().length() > 0.5 || p.get(GamepadButton::RightTrigger2).unwrap_or(0.0) > 0.3 {
            st.which = Some(e);
        }
    }
    let Some(p) = st.which.and_then(|e| pads.get(e).ok().map(|(_, p)| p)).or_else(|| pads.iter().next().map(|(_, p)| p)) else { return };
    let in_menu = *state.get() != crate::AppState::Playing || menu.page == crate::gamemenu::Page::Pub;
    use GamepadButton as B;
    let edge = |keys: &mut ButtonInput<KeyCode>, b: B, k: KeyCode| {
        if p.just_pressed(b) {
            keys.press(k);
        } else if p.just_released(b) {
            keys.release(k);
        }
    };
    // South: jump in play, select in menus and on messages
    edge(&mut keys, B::South, KeyCode::Space);
    edge(&mut keys, B::South, KeyCode::Enter);
    edge(&mut keys, B::West, KeyCode::KeyR);
    edge(&mut keys, B::North, KeyCode::KeyF);
    edge(&mut keys, B::RightTrigger, KeyCode::KeyQ);
    edge(&mut keys, B::LeftTrigger, KeyCode::KeyQ);
    edge(&mut keys, B::Start, KeyCode::Escape);
    edge(&mut keys, B::Select, KeyCode::KeyH);
    edge(&mut keys, B::DPadUp, KeyCode::Digit1);
    edge(&mut keys, B::DPadDown, KeyCode::Digit3);
    if in_menu {
        edge(&mut keys, B::East, KeyCode::Escape);
        edge(&mut keys, B::DPadUp, KeyCode::ArrowUp);
        edge(&mut keys, B::DPadDown, KeyCode::ArrowDown);
        edge(&mut keys, B::DPadLeft, KeyCode::ArrowLeft);
        edge(&mut keys, B::DPadRight, KeyCode::ArrowRight);
        // the left stick steps the menus too, one step per push
        let s = p.left_stick();
        let dir = if s.y > 0.6 {
            Some(KeyCode::ArrowUp)
        } else if s.y < -0.6 {
            Some(KeyCode::ArrowDown)
        } else if s.x > 0.6 {
            Some(KeyCode::ArrowRight)
        } else if s.x < -0.6 {
            Some(KeyCode::ArrowLeft)
        } else {
            None
        };
        if dir != st.menu_dir {
            if let Some(k) = st.menu_dir {
                keys.release(k);
            }
            if let Some(k) = dir {
                keys.press(k);
            }
            st.menu_dir = dir;
        }
        return;
    }
    if let Some(k) = st.menu_dir.take() {
        keys.release(k);
    }
    edge(&mut keys, B::DPadLeft, KeyCode::Tab);
    edge(&mut keys, B::DPadRight, KeyCode::Tab);
    // triggers as the mouse buttons (past 30 %: qol.md)
    let trig = |b: B| p.get(b).unwrap_or(0.0) > 0.3;
    for (b, m) in [(B::RightTrigger2, MouseButton::Left), (B::LeftTrigger2, MouseButton::Right)] {
        let down = trig(b);
        if down && !mouse.pressed(m) {
            mouse.press(m);
        } else if !down && mouse.pressed(m) && pad_active() {
            mouse.release(m);
        }
    }
    // R3 toggles zoom (ToggleZoom bound: game_06.cpp:2819); held as the Zoom key while on
    if p.just_pressed(B::RightThumb) {
        st.zoom = !st.zoom;
        if st.zoom {
            keys.press(KeyCode::KeyZ);
        } else {
            keys.release(KeyCode::KeyZ);
        }
    }
    if std::env::var("AIDBG").is_ok() && (p.left_stick() != Vec2::ZERO || p.right_stick() != Vec2::ZERO) {
        info!("AIDBG pad {:?} move {:?} look {:?} which {:?}", st.which, p.left_stick(), p.right_stick(), st.which);
    }
    st.mv = dead(p.left_stick(), 0.2);
    st.look = dead(p.right_stick(), 0.15);
}
