//! Input: the game's actions, bound to keyboard / mouse and gamepads through bevy_enhanced_input (remaster
//! improvement: the original PC build reads a DirectInput joystick with fixed Xbox-era names).
//!
//! Three contexts, one active at a time: `Gameplay` (in play), `Menus` (front end, pause, pub screen) and `Message`
//! (a paused mission message). Systems read the resulting `Controls` resource, not raw devices.
//!
//! Keyboard and mouse keep the original's default bindings (player.md 2: table 0x00588ed8). The pad layout is a
//! modern twin-stick one (agreed with the player 2026-10-14):
//!
//! | Pad (DualSense / Xbox) | In play | In menus |
//! |---|---|---|
//! | Left stick | move (analog) | navigate |
//! | Right stick | camera | |
//! | R2 / RT | fire | |
//! | L2 / LT | aim / zoom (held) | |
//! | R1 / RB | throw special (sticky bomb, Topsy Turvy) | |
//! | L1 / LB, D-pad left / right | next special weapon | navigate |
//! | Cross / A | jump | confirm, continue |
//! | Square / X | use (hold for ticker bombs) | |
//! | Triangle / Y | switch weapon | |
//! | Circle / B | reload | back |
//! | D-pad up / down | send / recall teammates | navigate |
//! | Options / Menu | pause | back |
//!
//! Stick look as the original's analog turn (player.md 3: turn_max_speed 0.3 -> 3 rad/s at full deflection, zoomed
//! 0.05; shaping exponent 2, zoomed 3, 0x00414c00 pow taken); pitch 1.7 rad/s (**guess**). Applied in player.rs.
//!
//! Gamepads count only while the game window has focus (the player may be playing something else), and scripted
//! runs (--play) read only their virtual pad.
//!
//! Prompts: `key(Act)` gives the text for a prompt's %s on the active device (`hud.rs` draws `<icon=...>` tags with
//! the Kenney Input Prompts icons when present): the keyboard / mouse binding the player last used for the action
//! (the original shows the first binding, 0x00465280: Use "X", MenuSelect "Mouse 1", formatted into the prompt by
//! 0x004652b0 when the keys change), or the pad's button.

use bevy::input::gamepad::{Gamepad, GamepadButton};
use bevy::input::mouse::AccumulatedMouseMotion;
use bevy::prelude::*;
use bevy::window::PrimaryWindow;
use bevy_enhanced_input::prelude::*;
use std::sync::atomic::{AtomicU8, Ordering};

/// 0 keyboard and mouse, 1 Xbox-style pad, 2 PlayStation pad
static DEVICE: AtomicU8 = AtomicU8::new(0);
/// the Use key last pressed: 0 X (the original's first binding), 1 F
static USE_KEY: AtomicU8 = AtomicU8::new(0);
/// the MenuSelect binding last pressed: 0 Mouse 1 (first binding), 1 Enter, 2 Space
static SELECT_KEY: AtomicU8 = AtomicU8::new(0);

/// The pad last used (rumble goes to it).
#[derive(Resource, Default)]
pub struct ActivePad(pub Option<Entity>);

/// The game's actions.
#[derive(Clone, Copy, PartialEq, Eq, Debug, Component)]
pub enum Act {
    // play
    Move,
    Look,
    Fire,
    Aim,
    Special,
    NextSpecial,
    SwitchWeapon,
    Jump,
    Use,
    Reload,
    TeamSend,
    TeamRecall,
    Pause,
    // menus
    Nav,
    Select,
    Back,
    // a paused message
    Continue,
}

const ACTS: usize = Act::Continue as usize + 1;

/// One action's state this frame.
#[derive(Clone, Copy, Default, Debug)]
pub struct ActState {
    pub value: Vec2,
    pub down: bool,
    pub pressed: bool,
    pub released: bool,
}

/// The actions' states this frame (filled after bevy_enhanced_input evaluated the active context).
#[derive(Resource, Default)]
pub struct Controls {
    acts: [ActState; ACTS],
    /// menu navigation steps this frame (stick / D-pad / keys with key repeat): -1, 0, 1 on each axis
    pub step: IVec2,
    nav_held: f32,
    /// the Gameplay context is active (in play, no paused message)
    pub active_play: bool,
    /// the active context changed: buttons held across the change count only after a release (the Options press
    /// that opens the pause menu is not also the menu's Back; the R2 that dismisses a message does not also fire)
    switched: bool,
    blocked: [bool; ACTS],
}

impl Controls {
    pub fn get(&self, a: Act) -> ActState {
        self.acts[a as usize]
    }
    pub fn down(&self, a: Act) -> bool {
        self.acts[a as usize].down
    }
    pub fn pressed(&self, a: Act) -> bool {
        self.acts[a as usize].pressed
    }
    pub fn released(&self, a: Act) -> bool {
        self.acts[a as usize].released
    }
    pub fn axis(&self, a: Act) -> Vec2 {
        self.acts[a as usize].value
    }
}

#[derive(InputAction)]
#[action_output(Vec2)]
struct MoveA;
#[derive(InputAction)]
#[action_output(Vec2)]
struct LookA;
#[derive(InputAction)]
#[action_output(Vec2)]
struct NavA;
#[derive(InputAction)]
#[action_output(bool)]
struct Btn;

/// Contexts (components on their own entities).
#[derive(Component)]
pub struct Gameplay;
#[derive(Component)]
pub struct Menus;
#[derive(Component)]
pub struct Message;

pub struct InputPlugin;

/// The device and context choice (scripted input is fed in before it).
#[derive(SystemSet, Debug, Clone, PartialEq, Eq, Hash)]
pub struct InputDevices;

impl Plugin for InputPlugin {
    fn build(&self, app: &mut App) {
        app.add_plugins(EnhancedInputPlugin)
            .add_input_context::<Gameplay>()
            .add_input_context::<Menus>()
            .add_input_context::<Message>()
            .init_resource::<Controls>()
            .init_resource::<ActivePad>()
            .add_systems(Update, rumble.run_if(in_state(crate::AppState::Playing)))
            .add_systems(Startup, spawn_contexts)
            .add_systems(PreUpdate, (device, contexts).chain().in_set(InputDevices).after(bevy::input::InputSystems).before(EnhancedInputSystems::Prepare))
            .add_systems(PreUpdate, collect.after(EnhancedInputSystems::Apply));
    }
}

/// The analog dead zone of every movement and look axis (0x3e570a3d = 0.21 passed to Controls_AxisMax 0x00414a30 by
/// the player update and Camera_Update), per axis.
const AXIS_DEAD_ZONE: DeadZone = DeadZone { kind: DeadZoneKind::Axial, lower_threshold: 0.21, upper_threshold: 1.0 };

fn btn(a: Act, binds: impl Bundle) -> impl Bundle {
    (Action::<Btn>::new(), a, binds)
}

/// The actions and their bindings (keyboard and mouse: the original's defaults, Controls_LoadDefaultBindings
/// 0x004144a0 from table 0x00588ed8; pads: the remaster's layout, module comment).
fn spawn_contexts(mut commands: Commands) {
    use GamepadButton as B;
    commands.spawn((
        Gameplay,
        ContextActivity::<Gameplay>::INACTIVE,
        actions!(Gameplay[
            // each axis past the original's dead zone 0.21, rescaled linearly (InputPC_AxisValue 0x004f9560 with the
            // callers' 0.21, 0x3e570a3d); the vector's response is player.rs move_curve
            (Action::<MoveA>::new(), Act::Move, AXIS_DEAD_ZONE, Bindings::spawn((Cardinal::wasd_keys(), Axial::left_stick()))),
            // look: the right stick, and the arrow keys as the original's secondary TurnLeft / TurnRight / LookUp /
            // LookDown bindings (table 0x00588ed8: digital, full deflection)
            (Action::<LookA>::new(), Act::Look, AXIS_DEAD_ZONE, Bindings::spawn((Axial::right_stick(), Cardinal::arrows()))),
            btn(Act::Fire, bindings![MouseButton::Left, B::RightTrigger2]),
            btn(Act::Aim, bindings![KeyCode::KeyZ, KeyCode::KeyE, B::LeftTrigger2]),
            btn(Act::Special, bindings![MouseButton::Right, B::RightTrigger]),
            btn(Act::NextSpecial, bindings![KeyCode::Tab, B::LeftTrigger, B::DPadRight, B::DPadLeft]),
            btn(Act::SwitchWeapon, bindings![KeyCode::KeyQ, B::North]),
            btn(Act::Jump, bindings![KeyCode::Space, MouseButton::Middle, B::South]),
            btn(Act::Use, bindings![KeyCode::KeyX, KeyCode::KeyF, B::West]),
            btn(Act::Reload, bindings![KeyCode::KeyR, B::East]),
            btn(Act::TeamSend, bindings![KeyCode::Digit1, B::DPadUp]),
            btn(Act::TeamRecall, bindings![KeyCode::Digit3, B::DPadDown]),
            btn(Act::Pause, bindings![KeyCode::Escape, B::Start]),
        ]),
    ));
    commands.spawn((
        Menus,
        ContextActivity::<Menus>::ACTIVE,
        actions!(Menus[
            (Action::<NavA>::new(), Act::Nav, DeadZone { lower_threshold: 0.5, ..default() },
                Bindings::spawn((Cardinal::arrows(), Cardinal::wasd_keys(), Cardinal::dpad(), Axial::left_stick()))),
            btn(Act::Select, bindings![KeyCode::Enter, KeyCode::Space, B::South]),
            btn(Act::Back, bindings![KeyCode::Escape, MouseButton::Right, B::East, B::Start]),
        ]),
    ));
    commands.spawn((
        Message,
        ContextActivity::<Message>::INACTIVE,
        actions!(Message[
            // MenuSelect, MenuCancel or Fire dismiss it (0x00462790, ui_01.cpp:2250)
            btn(Act::Continue, bindings![KeyCode::Enter, MouseButton::Left, B::South, B::East, B::RightTrigger2]),
        ]),
    ));
}

/// Which context is active, and which pads count.
#[allow(clippy::too_many_arguments)]
fn contexts(
    mut commands: Commands,
    state: Res<State<crate::AppState>>,
    menu: Res<crate::gamemenu::GameMenu>,
    mission: Option<Res<crate::mission::Mission>>,
    playtest: Option<Res<crate::playtest::Playtest>>,
    window: Query<&Window, With<PrimaryWindow>>,
    mut sources: ResMut<ActionSources>,
    ctx: Query<(Entity, Has<Gameplay>, Has<Menus>, Has<Message>, Option<&GamepadDevice>)>,
    mut last: Local<Option<(bool, bool, bool)>>,
    (mut controls, movie): (ResMut<Controls>, Option<Res<crate::movie::MoviePlayer>>),
) {
    let showing = mission.as_ref().is_some_and(|m| m.showing.is_some());
    // a full-window movie takes the menus' buttons (select / back skip it: movie.rs)
    let movie = movie.is_some_and(|m| m.takes_input());
    let play = *state.get() == crate::AppState::Playing && menu.page != crate::gamemenu::Page::Pub && !movie;
    let want = (play && !showing, !play, play && showing);
    controls.active_play = want.0;
    if last.is_some_and(|l| l != want) {
        controls.switched = true;
    }
    let pad = match playtest.as_ref() {
        Some(p) => p.pad_entity().map_or(GamepadDevice::None, GamepadDevice::Single),
        None => GamepadDevice::Any,
    };
    // the player may be in another program: pads only reach a focused window (scripted runs are never focused)
    let focused = playtest.is_some() || window.single().is_ok_and(|w| w.focused);
    sources.gamepad_button = focused;
    sources.gamepad_axis = focused;
    for (e, g, m, msg, dev) in &ctx {
        if dev != Some(&pad) {
            commands.entity(e).insert(pad);
        }
        if *last != Some(want) {
            if g {
                commands.entity(e).insert(ContextActivity::<Gameplay>::new(want.0));
            }
            if m {
                commands.entity(e).insert(ContextActivity::<Menus>::new(want.1));
            }
            if msg {
                commands.entity(e).insert(ContextActivity::<Message>::new(want.2));
            }
        }
    }
    *last = Some(want);
}

/// Copies the evaluated actions into `Controls`: held (Controls_IsHeld 0x00414810), pressed this frame
/// (Controls_IsPressed 0x00414730) and released (Controls_IsReleased 0x004148f0) on any binding, axes the larger of
/// the bindings' (Controls_AxisMax 0x00414a30 / 0x00414aa0). Buttons held across a context change are consumed until
/// released (the original consumes an action that did its job, Controls_Consume 0x00414680, and frees it once every
/// binding is up, 0x004146c0, polled each frame by Controls_Poll 0x004145c0). Menu navigation steps with key repeat:
/// the first repeat after 0.25 s, then every 0.1 s (the menu input 0x00491140 through 0x00491090: first delay 0.25,
/// repeat 0.1).
fn collect(time: Res<Time<Real>>, mut c: ResMut<Controls>, acts: Query<(&Act, &ActionValue, &ActionEvents, &TriggerState)>) {
    let mut out = [ActState::default(); ACTS];
    for (a, v, ev, st) in &acts {
        let value = match *v {
            ActionValue::Bool(b) => Vec2::new(b as u8 as f32, 0.0),
            ActionValue::Axis1D(x) => Vec2::new(x, 0.0),
            ActionValue::Axis2D(v) => v,
            ActionValue::Axis3D(v) => v.truncate(),
        };
        out[*a as usize] = ActState {
            value,
            down: *st == TriggerState::Fired,
            pressed: ev.contains(ActionEvents::START),
            released: ev.contains(ActionEvents::COMPLETE),
        };
    }
    if std::mem::take(&mut c.switched) {
        c.blocked = [true; ACTS];
    }
    for (i, st) in out.iter_mut().enumerate() {
        // axes (move, look, menu navigation) carry on
        if matches!(i, x if x == Act::Move as usize || x == Act::Look as usize || x == Act::Nav as usize) {
            c.blocked[i] = false;
        }
        if c.blocked[i] && (st.down || st.pressed) {
            *st = ActState::default();
        } else {
            c.blocked[i] = false;
        }
    }
    if std::env::var("AIDBG").is_ok() {
        for (i, st) in out.iter().enumerate() {
            if st.pressed {
                info!("AIDBG input pressed {i} play {}", c.active_play);
            }
        }
    }
    let nav = out[Act::Nav as usize].value;
    let dir = if nav.length() > 0.5 {
        if nav.y.abs() >= nav.x.abs() { IVec2::new(0, nav.y.signum() as i32) } else { IVec2::new(nav.x.signum() as i32, 0) }
    } else {
        IVec2::ZERO
    };
    let prev = c.nav_held;
    c.step = IVec2::ZERO;
    if dir == IVec2::ZERO {
        c.nav_held = 0.0;
    } else {
        let t = prev + time.delta_secs();
        let fire = prev == 0.0 || (t >= 0.25 && ((t - 0.25) / 0.1).floor() > ((prev - 0.25).max(-0.1) / 0.1).floor());
        if fire {
            c.step = dir;
        }
        c.nav_held = t.max(1e-6);
    }
    c.acts = out;
}

pub fn pad_active() -> bool {
    DEVICE.load(Ordering::Relaxed) != 0
}

/// The active device: the one that gave input last (pads only while they count, as above). The original makes the
/// device that pressed something active and reads only it (Controls +4, Controls_Poll 0x004145c0, Controls_Lock
/// 0x00414590 to listen to all); the remaster reads every device and uses the active one for prompts and rumble.
fn device(
    keys: Res<ButtonInput<KeyCode>>,
    mouse: Res<ButtonInput<MouseButton>>,
    motion: Res<AccumulatedMouseMotion>,
    pads: Query<(Entity, &Gamepad)>,
    playtest: Option<Res<crate::playtest::Playtest>>,
    window: Query<&Window, With<PrimaryWindow>>,
    mut active: ResMut<ActivePad>,
) {
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
    let focused = playtest.is_some() || window.single().is_ok_and(|w| w.focused);
    let only = playtest.as_ref().map(|p| p.pad_entity());
    for (e, p) in &pads {
        if !focused || only.is_some_and(|o| o != Some(e)) {
            continue;
        }
        let used = p.get_just_pressed().next().is_some() || p.left_stick().length() > 0.5 || p.right_stick().length() > 0.5;
        if used {
            // Sony's USB vendor id
            let ps = p.vendor_id() == Some(0x054c);
            DEVICE.store(if ps { 2 } else { 1 }, Ordering::Relaxed);
            active.0 = Some(e);
            return;
        }
    }
    if keys.get_just_pressed().next().is_some() || mouse.get_just_pressed().next().is_some() || motion.delta.length() > 3.0 {
        DEVICE.store(0, Ordering::Relaxed);
    }
}

/// A pad button's prompt: an icon from the Kenney Input Prompts pack (hud.rs draws `<icon=name>`), falling back to
/// its name when the icons are missing.
fn pad_button(ps: bool, b: GamepadButton) -> String {
    use GamepadButton as B;
    let (ps_icon, xb_icon) = match b {
        B::South => ("playstation_button_color_cross", "xbox_button_color_a"),
        B::East => ("playstation_button_color_circle", "xbox_button_color_b"),
        B::West => ("playstation_button_color_square", "xbox_button_color_x"),
        B::North => ("playstation_button_color_triangle", "xbox_button_color_y"),
        B::RightTrigger2 => ("playstation_trigger_r2", "xbox_rt"),
        B::LeftTrigger2 => ("playstation_trigger_l2", "xbox_lt"),
        B::RightTrigger => ("playstation_trigger_r1", "xbox_rb"),
        B::LeftTrigger => ("playstation_trigger_l1", "xbox_lb"),
        B::Start => ("playstation5_button_options", "xbox_button_menu"),
        B::DPadUp => ("playstation_dpad_up", "xbox_dpad_up"),
        B::DPadDown => ("playstation_dpad_down", "xbox_dpad_down"),
        _ => ("playstation_button_cross", "xbox_button_a"),
    };
    format!("<icon={}>", if ps { ps_icon } else { xb_icon })
}

/// The text for a prompt's %s: the binding of `a` on the device in use.
pub fn key(a: Act) -> String {
    use GamepadButton as B;
    let dev = DEVICE.load(Ordering::Relaxed);
    if dev == 0 {
        // key and mouse button icons
        let k = match a {
            Act::Use => ["keyboard_x", "keyboard_f"][USE_KEY.load(Ordering::Relaxed) as usize % 2],
            Act::Select | Act::Continue => ["mouse_left", "keyboard_enter", "keyboard_space"][SELECT_KEY.load(Ordering::Relaxed) as usize % 3],
            Act::Back | Act::Pause => "keyboard_escape",
            Act::Fire => "mouse_left",
            Act::Special => "mouse_right",
            Act::Jump => "keyboard_space",
            Act::Reload => "keyboard_r",
            Act::SwitchWeapon => "keyboard_q",
            Act::NextSpecial => "keyboard_tab",
            Act::Aim => "keyboard_z",
            Act::TeamSend => "keyboard_1",
            Act::TeamRecall => "keyboard_3",
            Act::Move => return "<icon=keyboard_w><icon=keyboard_a><icon=keyboard_s><icon=keyboard_d>".into(),
            Act::Look => "mouse_move",
            Act::Nav => return String::new(),
        };
        return format!("<icon={k}>");
    }
    let b = match a {
        Act::Select | Act::Continue | Act::Jump => B::South,
        Act::Back | Act::Reload => B::East,
        Act::Use => B::West,
        Act::SwitchWeapon => B::North,
        Act::Fire => B::RightTrigger2,
        Act::Aim => B::LeftTrigger2,
        Act::Special => B::RightTrigger,
        Act::NextSpecial => B::LeftTrigger,
        Act::Pause => B::Start,
        Act::TeamSend => B::DPadUp,
        Act::TeamRecall => B::DPadDown,
        Act::Move => return format!("<icon={}>", if dev == 2 { "playstation_stick_l" } else { "xbox_stick_l" }),
        Act::Look => return format!("<icon={}>", if dev == 2 { "playstation_stick_r" } else { "xbox_stick_r" }),
        Act::Nav => return String::new(),
    };
    pad_button(dev == 2, b)
}

/// Every binding of `a` on the device in use, for the controls page (gamemenu.rs shows two as "%s or %s", as the
/// original's key binding entries 0x00447e70 show both keys of an action). Keyboard and mouse: the bindings of
/// `spawn_contexts` (keep in step); a pad: its one button (`key`).
pub fn keys(a: Act) -> Vec<String> {
    if DEVICE.load(Ordering::Relaxed) != 0 {
        return vec![key(a)];
    }
    let icon = |n: &str| format!("<icon={n}>");
    match a {
        Act::Use => vec![icon("keyboard_x"), icon("keyboard_f")],
        Act::Aim => vec![icon("keyboard_z"), icon("keyboard_e")],
        // no icon for the middle button in the exported set
        Act::Jump => vec![icon("keyboard_space"), "Mouse 3".into()],
        Act::Select => vec![icon("keyboard_enter"), icon("keyboard_space")],
        Act::Continue => vec![icon("keyboard_enter"), icon("mouse_left")],
        Act::Back => vec![icon("keyboard_escape"), icon("mouse_right")],
        _ => vec![key(a)],
    }
}

/// Rumble on the pad in use (remaster improvement; the original drives DirectInput force feedback from
/// Rumble_Update, not ported): a short light pulse per shot, a heavier one when Roman is hit (by the damage), and
/// explosions by distance (full within 50 units, none beyond 300).
#[allow(clippy::too_many_arguments)]
fn rumble(
    active: Res<ActivePad>,
    playtest: Option<Res<crate::playtest::Playtest>>,
    window: Query<&Window, With<PrimaryWindow>>,
    roman: Query<(&crate::player::Roman, &crate::fx::Health, &GlobalTransform)>,
    mut blasts: MessageReader<crate::fx::Explode>,
    mut out: MessageWriter<bevy::input::gamepad::GamepadRumbleRequest>,
    mut last: Local<(f32, f32)>,
) {
    use bevy::input::gamepad::{GamepadRumbleIntensity, GamepadRumbleRequest};
    use std::time::Duration;
    let Ok((r, h, t)) = roman.single() else {
        blasts.clear();
        return;
    };
    let (prev_fire, prev_health) = std::mem::replace(&mut *last, (r.since_fire, h.0));
    let focused = window.single().is_ok_and(|w| w.focused);
    let Some(pad) = active.0.filter(|_| pad_active() && (focused || playtest.is_some())) else {
        blasts.clear();
        return;
    };
    let mut add = |strong: f32, weak: f32, secs: f32| {
        if std::env::var("AIDBG").is_ok() {
            info!("AIDBG rumble strong {strong:.2} weak {weak:.2} for {secs:.2} s");
        }
        out.write(GamepadRumbleRequest::Add {
            gamepad: pad,
            intensity: GamepadRumbleIntensity { strong_motor: strong.clamp(0.0, 1.0), weak_motor: weak.clamp(0.0, 1.0) },
            duration: Duration::from_secs_f32(secs),
        });
    };
    if r.since_fire < prev_fire {
        add(0.0, 0.35, 0.07);
    }
    if h.0 < prev_health && h.0 > 0.0 {
        let d = prev_health - h.0;
        add((d / 60.0).min(0.9), 0.4, 0.25);
    }
    for b in blasts.read() {
        let dist = b.at.distance(t.translation());
        if dist < 300.0 && b.area_damage > 0.0 {
            let k = (1.0 - (dist - 50.0).max(0.0) / 250.0).clamp(0.0, 1.0);
            add(0.8 * k, 0.5 * k, 0.15 + 0.35 * k);
        }
    }
}
