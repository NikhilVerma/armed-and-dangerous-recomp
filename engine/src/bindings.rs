//! Key bindings: which keys, mouse buttons and pad buttons drive each of the player's actions, kept in the options
//! (options.json `bindings`) and edited on the Controls page (gamemenu.rs).
//!
//! The original's model (docs/subsystems/qol.md "Bindings", src/ui/ui_01.cpp): every action has two keys, a primary
//! and a secondary (Control +0x1c / +0xa8); the controls pages list the actions of table 0x00588dd8 (Move Forward ..
//! Look Down, Fire, Fire Special; Jump, Next / Prev Weapon, Next / Prev Special, Reload, Zoom, Send / Recall Mates,
//! Activate; an entry is set up by 0x00447d20 / 0x00447e20, the table's empty slots 0x23 hidden), each entry showing "%s or %s" (pms_input_aorb), one key, or "Unassigned" (0x00447e70). Choosing an entry
//! shows "Press Key To Assign" (UI_ShowAssignKeyPrompt 0x00447f50) and the next key pressed after a release
//! (0x00448260: the prompt arms once nothing is held) is bound by 0x004481b0:
//! - Escape cancels; Pause is ignored;
//! - the key is first taken off every action of the table (0x00447fc0: a secondary is cleared, a primary replaced by
//!   the secondary). When the chosen action itself had it, that is all: pressing an action's own key unbinds it;
//! - otherwise an empty primary takes the key, else it becomes the secondary and the old secondary moves up to the
//!   primary: [a, b] + k gives [b, k], [a] + k gives [a, k].
//! The menu then consumes the key (0x00414680), so it does not also act on the page.
//!
//! The remaster keeps that rule for the keyboard and mouse ([`KeyBindings::assign`]) and adds the pad, whose buttons
//! are rebound the same way on the same page while a pad is the device in use (the Controls page shows the bindings
//! of the active device). Each device has its own table, so a key never takes a pad button's place. Menus keep their
//! fixed keys (input.rs `Menus`), so no binding can lock the player out of the menus, and Pause stays on Escape /
//! Options. The sticks always move and look.

use bevy::input::gamepad::GamepadButton;
use bevy::prelude::*;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

/// The actions the player can rebind (the original's table 0x00588dd8, less Prev Weapon / Prev Special: the
/// remaster cycles forwards only).
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Debug, Serialize, Deserialize)]
pub enum Bind {
    MoveForward,
    MoveBack,
    MoveLeft,
    MoveRight,
    TurnLeft,
    TurnRight,
    LookUp,
    LookDown,
    Fire,
    Special,
    Aim,
    Reload,
    Jump,
    SwitchWeapon,
    NextSpecial,
    TeamSend,
    TeamRecall,
    Use,
}

impl Bind {
    pub const ALL: [Bind; 18] = [
        Bind::MoveForward,
        Bind::MoveBack,
        Bind::MoveLeft,
        Bind::MoveRight,
        Bind::TurnLeft,
        Bind::TurnRight,
        Bind::LookUp,
        Bind::LookDown,
        Bind::Fire,
        Bind::Special,
        Bind::Aim,
        Bind::Reload,
        Bind::Jump,
        Bind::SwitchWeapon,
        Bind::NextSpecial,
        Bind::TeamSend,
        Bind::TeamRecall,
        Bind::Use,
    ];

    /// The entry's label: the action's name in the text bank (0x00415720 -> ControlAction* ids).
    pub fn text_id(self) -> &'static str {
        match self {
            Bind::MoveForward => "controlactionmoveforward",
            Bind::MoveBack => "controlactionmovebackward",
            Bind::MoveLeft => "controlactionmoveleft",
            Bind::MoveRight => "controlactionmoveright",
            Bind::TurnLeft => "controlactionturnleft",
            Bind::TurnRight => "controlactionturnright",
            Bind::LookUp => "controlactionlookup",
            Bind::LookDown => "controlactionlookdown",
            Bind::Fire => "controlactionfire",
            Bind::Special => "controlactionfirespecialweapon",
            Bind::Aim => "controlactionzoom",
            Bind::Reload => "controlactionreload",
            Bind::Jump => "controlactionturbojump",
            Bind::SwitchWeapon => "controlactionincweapon",
            Bind::NextSpecial => "controlactionincspecialweapon",
            Bind::TeamSend => "controlactionteammatessend",
            Bind::TeamRecall => "controlactionteammatesreturn",
            Bind::Use => "controlactionuse",
        }
    }

    /// Whether the device rebinds it: the pad's sticks move and look, so its directions are not buttons.
    pub fn on(self, device: Device) -> bool {
        device == Device::Keys || !matches!(self, Bind::MoveForward | Bind::MoveBack | Bind::MoveLeft | Bind::MoveRight | Bind::TurnLeft | Bind::TurnRight | Bind::LookUp | Bind::LookDown)
    }

    /// The game action it drives (input.rs).
    pub fn act(self) -> crate::input::Act {
        use crate::input::Act;
        match self {
            Bind::MoveForward | Bind::MoveBack | Bind::MoveLeft | Bind::MoveRight => Act::Move,
            Bind::TurnLeft | Bind::TurnRight | Bind::LookUp | Bind::LookDown => Act::Look,
            Bind::Fire => Act::Fire,
            Bind::Special => Act::Special,
            Bind::Aim => Act::Aim,
            Bind::Reload => Act::Reload,
            Bind::Jump => Act::Jump,
            Bind::SwitchWeapon => Act::SwitchWeapon,
            Bind::NextSpecial => Act::NextSpecial,
            Bind::TeamSend => Act::TeamSend,
            Bind::TeamRecall => Act::TeamRecall,
            Bind::Use => Act::Use,
        }
    }

    /// The bindable action behind a button action (none for the axes and the fixed ones).
    pub fn of_act(a: crate::input::Act) -> Option<Bind> {
        Bind::ALL.into_iter().find(|b| b.act() == a && !matches!(a, crate::input::Act::Move | crate::input::Act::Look))
    }
}

/// The two kinds of device the bindings are kept for.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub enum Device {
    /// keyboard and mouse
    Keys,
    Pad,
}

impl Device {
    /// How many inputs an action may have: two keys (the original's primary and secondary); three pad buttons (the
    /// pad layout's Next Special is on LB and both D-pad sides).
    pub fn slots(self) -> usize {
        match self {
            Device::Keys => 2,
            Device::Pad => 3,
        }
    }

    /// The device in use now (input.rs: the one that gave input last).
    pub fn active() -> Self {
        if crate::input::pad_active() { Device::Pad } else { Device::Keys }
    }
}

/// One bindable input.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug, Serialize, Deserialize)]
#[serde(try_from = "String", into = "String")]
pub enum Input {
    Key(KeyCode),
    Mouse(MouseButton),
    Pad(GamepadButton),
}

impl Input {
    pub fn device(self) -> Device {
        match self {
            Input::Pad(_) => Device::Pad,
            _ => Device::Keys,
        }
    }

    /// Its name in options.json: the key's code name ("KeyW", "Space"), "Mouse:Left", "Pad:South".
    pub fn name(self) -> String {
        match self {
            Input::Key(k) => key_entry(k).map(|e| e.0.to_owned()).unwrap_or_else(|| format!("{k:?}")),
            Input::Mouse(b) => format!("Mouse:{}", mouse_name(b)),
            Input::Pad(b) => format!("Pad:{b:?}"),
        }
    }

    /// The input as a prompt or a binding entry shows it: an input prompt icon (`<icon=...>`, hud.rs) where the
    /// icon set has one, else its name as the original's key names give it (pms_input_vk_*, pms_input_mouse_*).
    pub fn label(self, playstation: bool) -> String {
        match self {
            Input::Key(k) => match key_entry(k) {
                Some((_, Some(icon), _)) => format!("<icon={icon}>"),
                Some((_, None, text)) => text.to_owned(),
                None => format!("{k:?}"),
            },
            Input::Mouse(MouseButton::Left) => "<icon=mouse_left>".into(),
            Input::Mouse(MouseButton::Right) => "<icon=mouse_right>".into(),
            Input::Mouse(MouseButton::Middle) => "Mouse 3".into(),
            Input::Mouse(MouseButton::Back) => "Mouse 4".into(),
            Input::Mouse(MouseButton::Forward) => "Mouse 5".into(),
            Input::Mouse(MouseButton::Other(n)) => format!("Mouse {}", n as u32 + 3),
            Input::Pad(b) => pad_icon(b, playstation).map(|i| format!("<icon={i}>")).unwrap_or_else(|| format!("{b:?}")),
        }
    }

    pub fn binding(self) -> bevy_enhanced_input::prelude::Binding {
        match self {
            Input::Key(k) => k.into(),
            Input::Mouse(b) => b.into(),
            Input::Pad(b) => b.into(),
        }
    }
}

impl From<Input> for String {
    fn from(i: Input) -> String {
        i.name()
    }
}

impl TryFrom<String> for Input {
    type Error = String;
    fn try_from(s: String) -> Result<Self, String> {
        if let Some(m) = s.strip_prefix("Mouse:") {
            return MOUSE.iter().find(|(_, n)| *n == m).map(|(b, _)| Input::Mouse(*b)).ok_or(s);
        }
        if let Some(p) = s.strip_prefix("Pad:") {
            return PAD.iter().find(|b| format!("{b:?}") == p).map(|b| Input::Pad(*b)).ok_or(s);
        }
        KEYS.iter().find(|e| e.1 == s).map(|e| Input::Key(e.0)).ok_or(s)
    }
}

const MOUSE: [(MouseButton, &str); 5] = [(MouseButton::Left, "Left"), (MouseButton::Right, "Right"), (MouseButton::Middle, "Middle"), (MouseButton::Back, "Back"), (MouseButton::Forward, "Forward")];

fn mouse_name(b: MouseButton) -> String {
    MOUSE.iter().find(|m| m.0 == b).map(|m| m.1.to_owned()).unwrap_or_else(|| format!("{b:?}"))
}

/// The pad buttons that can be bound (Start stays Pause).
const PAD: [GamepadButton; 14] = [
    GamepadButton::South,
    GamepadButton::East,
    GamepadButton::West,
    GamepadButton::North,
    GamepadButton::LeftTrigger,
    GamepadButton::RightTrigger,
    GamepadButton::LeftTrigger2,
    GamepadButton::RightTrigger2,
    GamepadButton::DPadUp,
    GamepadButton::DPadDown,
    GamepadButton::DPadLeft,
    GamepadButton::DPadRight,
    GamepadButton::LeftThumb,
    GamepadButton::RightThumb,
];

/// A pad button's prompt icon (the Kenney Input Prompts names, tools/remaster_export_icons.py).
pub fn pad_icon(b: GamepadButton, playstation: bool) -> Option<&'static str> {
    use GamepadButton as B;
    let (ps, xb) = match b {
        B::South => ("playstation_button_color_cross", "xbox_button_color_a"),
        B::East => ("playstation_button_color_circle", "xbox_button_color_b"),
        B::West => ("playstation_button_color_square", "xbox_button_color_x"),
        B::North => ("playstation_button_color_triangle", "xbox_button_color_y"),
        B::RightTrigger2 => ("playstation_trigger_r2", "xbox_rt"),
        B::LeftTrigger2 => ("playstation_trigger_l2", "xbox_lt"),
        B::RightTrigger => ("playstation_trigger_r1", "xbox_rb"),
        B::LeftTrigger => ("playstation_trigger_l1", "xbox_lb"),
        B::Start => ("playstation5_button_options", "xbox_button_menu"),
        B::Select => ("playstation5_button_create", "xbox_button_view"),
        B::DPadUp => ("playstation_dpad_up", "xbox_dpad_up"),
        B::DPadDown => ("playstation_dpad_down", "xbox_dpad_down"),
        B::DPadLeft => ("playstation_dpad_left", "xbox_dpad_left"),
        B::DPadRight => ("playstation_dpad_right", "xbox_dpad_right"),
        B::LeftThumb => ("playstation_button_l3", "xbox_ls"),
        B::RightThumb => ("playstation_button_r3", "xbox_rs"),
        _ => return None,
    };
    Some(if playstation { ps } else { xb })
}

/// The keys that can be bound: (key, its name in options.json, its prompt icon, its name where there is no icon).
macro_rules! keys {
    ($(($k:ident, $icon:expr, $text:expr)),* $(,)?) => {
        const KEYS: &[(KeyCode, &str, Option<&str>, &str)] = &[$((KeyCode::$k, stringify!($k), $icon, $text)),*];
    };
}

keys![
    (KeyA, Some("keyboard_a"), "A"), (KeyB, Some("keyboard_b"), "B"), (KeyC, Some("keyboard_c"), "C"), (KeyD, Some("keyboard_d"), "D"),
    (KeyE, Some("keyboard_e"), "E"), (KeyF, Some("keyboard_f"), "F"), (KeyG, Some("keyboard_g"), "G"), (KeyH, Some("keyboard_h"), "H"),
    (KeyI, Some("keyboard_i"), "I"), (KeyJ, Some("keyboard_j"), "J"), (KeyK, Some("keyboard_k"), "K"), (KeyL, Some("keyboard_l"), "L"),
    (KeyM, Some("keyboard_m"), "M"), (KeyN, Some("keyboard_n"), "N"), (KeyO, Some("keyboard_o"), "O"), (KeyP, Some("keyboard_p"), "P"),
    (KeyQ, Some("keyboard_q"), "Q"), (KeyR, Some("keyboard_r"), "R"), (KeyS, Some("keyboard_s"), "S"), (KeyT, Some("keyboard_t"), "T"),
    (KeyU, Some("keyboard_u"), "U"), (KeyV, Some("keyboard_v"), "V"), (KeyW, Some("keyboard_w"), "W"), (KeyX, Some("keyboard_x"), "X"),
    (KeyY, Some("keyboard_y"), "Y"), (KeyZ, Some("keyboard_z"), "Z"),
    (Digit0, Some("keyboard_0"), "0"), (Digit1, Some("keyboard_1"), "1"), (Digit2, Some("keyboard_2"), "2"), (Digit3, Some("keyboard_3"), "3"),
    (Digit4, Some("keyboard_4"), "4"), (Digit5, Some("keyboard_5"), "5"), (Digit6, Some("keyboard_6"), "6"), (Digit7, Some("keyboard_7"), "7"),
    (Digit8, Some("keyboard_8"), "8"), (Digit9, Some("keyboard_9"), "9"),
    (F1, Some("keyboard_f1"), "F1"), (F2, Some("keyboard_f2"), "F2"), (F3, Some("keyboard_f3"), "F3"), (F4, Some("keyboard_f4"), "F4"),
    (F5, Some("keyboard_f5"), "F5"), (F6, Some("keyboard_f6"), "F6"), (F7, Some("keyboard_f7"), "F7"), (F8, Some("keyboard_f8"), "F8"),
    (F9, Some("keyboard_f9"), "F9"), (F10, Some("keyboard_f10"), "F10"), (F11, Some("keyboard_f11"), "F11"), (F12, Some("keyboard_f12"), "F12"),
    (ArrowUp, Some("keyboard_arrow_up"), "Up"), (ArrowDown, Some("keyboard_arrow_down"), "Down"),
    (ArrowLeft, Some("keyboard_arrow_left"), "Left"), (ArrowRight, Some("keyboard_arrow_right"), "Right"),
    (Space, Some("keyboard_space"), "Space"), (Enter, Some("keyboard_enter"), "Enter"), (Tab, Some("keyboard_tab"), "Tab"),
    (Backspace, Some("keyboard_backspace"), "Backspace"), (Delete, Some("keyboard_delete"), "Delete"), (Insert, Some("keyboard_insert"), "Insert"),
    (Home, Some("keyboard_home"), "Home"), (End, Some("keyboard_end"), "End"), (PageUp, Some("keyboard_page_up"), "Page Up"),
    (PageDown, Some("keyboard_page_down"), "Page Down"), (CapsLock, Some("keyboard_capslock"), "Caps Lock"),
    (ShiftLeft, Some("keyboard_shift"), "Shift"), (ShiftRight, Some("keyboard_shift"), "Right Shift"),
    (ControlLeft, Some("keyboard_ctrl"), "Control"), (ControlRight, Some("keyboard_ctrl"), "Right Control"),
    (AltLeft, Some("keyboard_alt"), "Alt"), (AltRight, Some("keyboard_alt"), "Right Alt"),
    (Minus, Some("keyboard_minus"), "-"), (Equal, Some("keyboard_equals"), "="), (BracketLeft, Some("keyboard_bracket_open"), "["),
    (BracketRight, Some("keyboard_bracket_close"), "]"), (Backslash, Some("keyboard_slash_back"), "\\"), (Semicolon, Some("keyboard_semicolon"), ";"),
    (Quote, Some("keyboard_apostrophe"), "'"), (Comma, Some("keyboard_comma"), ","), (Period, Some("keyboard_period"), "."),
    (Slash, Some("keyboard_slash_forward"), "/"), (Backquote, Some("keyboard_tilde"), "`"),
    (Numpad0, None, "Num 0"), (Numpad1, None, "Num 1"), (Numpad2, None, "Num 2"), (Numpad3, None, "Num 3"), (Numpad4, None, "Num 4"),
    (Numpad5, None, "Num 5"), (Numpad6, None, "Num 6"), (Numpad7, None, "Num 7"), (Numpad8, None, "Num 8"), (Numpad9, None, "Num 9"),
    (NumpadEnter, Some("keyboard_numpad_enter"), "Num Enter"), (NumpadAdd, Some("keyboard_numpad_plus"), "Num +"),
    (NumpadSubtract, None, "Num -"), (NumpadMultiply, None, "Num *"), (NumpadDivide, None, "Num /"), (NumpadDecimal, None, "Num ."),
];

/// A key's (name, icon, text), when it can be bound.
fn key_entry(k: KeyCode) -> Option<(&'static str, Option<&'static str>, &'static str)> {
    KEYS.iter().find(|e| e.0 == k).map(|e| (e.1, e.2, e.3))
}

/// The bindings of both devices, per action in the order they were bound (the first is the primary).
#[derive(Clone, PartialEq, Debug, Serialize, Deserialize)]
#[serde(default)]
pub struct KeyBindings {
    pub keys: BTreeMap<Bind, Vec<Input>>,
    pub pad: BTreeMap<Bind, Vec<Input>>,
}

impl Default for KeyBindings {
    /// Keyboard and mouse: the original's defaults (Controls_LoadDefaultBindings 0x004144a0, table 0x00588ed8;
    /// player.md 2); the pad: the remaster's twin-stick layout (input.rs).
    fn default() -> Self {
        use GamepadButton as B;
        use Input::{Key as K, Mouse as M, Pad as P};
        let keys = [
            (Bind::MoveForward, vec![K(KeyCode::KeyW)]),
            (Bind::MoveBack, vec![K(KeyCode::KeyS)]),
            (Bind::MoveLeft, vec![K(KeyCode::KeyA)]),
            (Bind::MoveRight, vec![K(KeyCode::KeyD)]),
            (Bind::TurnLeft, vec![K(KeyCode::ArrowLeft)]),
            (Bind::TurnRight, vec![K(KeyCode::ArrowRight)]),
            (Bind::LookUp, vec![K(KeyCode::ArrowUp)]),
            (Bind::LookDown, vec![K(KeyCode::ArrowDown)]),
            (Bind::Fire, vec![M(MouseButton::Left)]),
            (Bind::Special, vec![M(MouseButton::Right)]),
            (Bind::Aim, vec![K(KeyCode::KeyZ), K(KeyCode::KeyE)]),
            (Bind::Reload, vec![K(KeyCode::KeyR)]),
            (Bind::Jump, vec![K(KeyCode::Space), M(MouseButton::Middle)]),
            (Bind::SwitchWeapon, vec![K(KeyCode::KeyQ)]),
            (Bind::NextSpecial, vec![K(KeyCode::Tab)]),
            (Bind::TeamSend, vec![K(KeyCode::Digit1)]),
            (Bind::TeamRecall, vec![K(KeyCode::Digit3)]),
            (Bind::Use, vec![K(KeyCode::KeyX), K(KeyCode::KeyF)]),
        ];
        let pad = [
            (Bind::Fire, vec![P(B::RightTrigger2)]),
            (Bind::Special, vec![P(B::RightTrigger)]),
            (Bind::Aim, vec![P(B::LeftTrigger2)]),
            (Bind::Reload, vec![P(B::East)]),
            (Bind::Jump, vec![P(B::South)]),
            (Bind::SwitchWeapon, vec![P(B::North)]),
            (Bind::NextSpecial, vec![P(B::LeftTrigger), P(B::DPadRight), P(B::DPadLeft)]),
            (Bind::TeamSend, vec![P(B::DPadUp)]),
            (Bind::TeamRecall, vec![P(B::DPadDown)]),
            (Bind::Use, vec![P(B::West)]),
        ];
        KeyBindings { keys: keys.into_iter().collect(), pad: pad.into_iter().collect() }
    }
}

impl KeyBindings {
    fn table(&self, d: Device) -> &BTreeMap<Bind, Vec<Input>> {
        match d {
            Device::Keys => &self.keys,
            Device::Pad => &self.pad,
        }
    }

    fn table_mut(&mut self, d: Device) -> &mut BTreeMap<Bind, Vec<Input>> {
        match d {
            Device::Keys => &mut self.keys,
            Device::Pad => &mut self.pad,
        }
    }

    /// The inputs of an action on a device (primary first).
    pub fn get(&self, b: Bind, d: Device) -> &[Input] {
        self.table(d).get(&b).map(Vec::as_slice).unwrap_or(&[])
    }

    /// Binds `input` to `bind` by the original's rule (0x004481b0 with 0x00447fc0, module comment): taken off every
    /// action of its device first; an action that had it is left without it, else it is added, the oldest input
    /// dropped when the action is full. Returns whether `bind` lost it (pressed its own key: unbound).
    pub fn assign(&mut self, bind: Bind, input: Input) -> bool {
        let d = input.device();
        let mut lost = false;
        for (b, inputs) in self.table_mut(d).iter_mut() {
            let before = inputs.len();
            inputs.retain(|i| *i != input);
            lost |= *b == bind && inputs.len() != before;
        }
        if !lost {
            let v = self.table_mut(d).entry(bind).or_default();
            if v.len() >= d.slots() {
                v.remove(0);
            }
            v.push(input);
        }
        lost
    }

    /// A bindings file from before an action existed gets that action's default inputs (when nothing else took
    /// them); inputs of the wrong device are dropped.
    pub fn normalized(mut self) -> Self {
        let def = KeyBindings::default();
        for d in [Device::Keys, Device::Pad] {
            self.table_mut(d).retain(|b, v| {
                v.retain(|i| i.device() == d);
                b.on(d)
            });
            for b in Bind::ALL.into_iter().filter(|b| b.on(d)) {
                if !self.table(d).contains_key(&b) {
                    let free: Vec<Input> = def.get(b, d).iter().copied().filter(|i| !self.table(d).values().any(|v| v.contains(i))).collect();
                    self.table_mut(d).insert(b, free);
                }
            }
        }
        self
    }

    /// The state log's view: per device, action -> input names.
    pub fn log(&self) -> serde_json::Value {
        let side = |t: &BTreeMap<Bind, Vec<Input>>| serde_json::Value::Object(t.iter().map(|(b, v)| (format!("{b:?}"), v.iter().map(|i| i.name()).collect::<Vec<_>>().into())).collect());
        serde_json::json!({ "keys": side(&self.keys), "pad": side(&self.pad) })
    }
}

/// A binding entry's text (0x00447e70): both inputs as "%s or %s" (pms_input_aorb), a lone one, or "Unassigned"
/// (pms_input_unassigned); the pad's third button joins the second.
pub fn entry_text(inputs: &[Input], playstation: bool, texts: &crate::gamemenu::Texts) -> String {
    let l: Vec<String> = inputs.iter().map(|i| i.label(playstation)).collect();
    match l.as_slice() {
        [] => texts.get("pms_input_unassigned"),
        [a] => a.clone(),
        [a, rest @ ..] => texts.get("pms_input_aorb").replacen("%s", a, 1).replacen("%s", &rest.join(" "), 1),
    }
}

/// A rebinding in progress on the Controls page (the entry's flags +0x474 0x4000 waiting, +0x58c armed).
#[derive(Clone, Copy, PartialEq, Debug)]
pub struct Capture {
    pub bind: Bind,
    pub device: Device,
    /// every input was up since the prompt opened: the next press is taken (0x00448260 arms +0x58c)
    pub armed: bool,
}

/// What a capture does with this frame's input (0x00448260): keep waiting, cancel (Escape), or bind.
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum CaptureStep {
    Wait,
    Arm,
    Cancel,
    Bind(Input),
}

impl Capture {
    /// `held`: anything of the capture's device is held; `pressed`: the first input pressed this frame (of any
    /// device), `cancel`: Escape (or the pad's Start) pressed.
    pub fn step(&self, held: bool, pressed: Option<Input>, cancel: bool) -> CaptureStep {
        if !self.armed {
            return if held || pressed.is_some() || cancel { CaptureStep::Wait } else { CaptureStep::Arm };
        }
        if cancel {
            return CaptureStep::Cancel;
        }
        match pressed {
            Some(i) if i.device() == self.device => CaptureStep::Bind(i),
            _ => CaptureStep::Wait,
        }
    }
}

/// Runs the Controls page's capture: arms it once all is released, then binds the first press (or cancels), saves
/// the options and consumes the input so it does not also act on the page.
#[allow(clippy::too_many_arguments)]
pub fn capture(
    mut menu: ResMut<crate::gamemenu::GameMenu>,
    mut opts: ResMut<crate::options::GameOptions>,
    keys: Res<ButtonInput<KeyCode>>,
    mouse: Res<ButtonInput<MouseButton>>,
    pads: Query<(Entity, &bevy::input::gamepad::Gamepad)>,
    active: Res<crate::input::ActivePad>,
    playtest: Option<Res<crate::playtest::Playtest>>,
    mut controls: ResMut<crate::input::Controls>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
) {
    let Some(c) = menu.capture else { return };
    // the pad that counts: a scripted run's own, else the one in use
    let pad = playtest.as_ref().map_or(active.0, |p| p.pad_entity());
    let pad = pads.iter().find(|(e, _)| Some(*e) == pad).map(|(_, p)| p);
    let held = match c.device {
        Device::Keys => keys.get_pressed().next().is_some() || mouse.get_pressed().next().is_some(),
        Device::Pad => pad.is_some_and(|p| p.get_pressed().next().is_some()),
    };
    let pressed = keys
        .get_just_pressed()
        .find(|k| key_entry(**k).is_some())
        .map(|k| Input::Key(*k))
        .or_else(|| mouse.get_just_pressed().find(|b| MOUSE.iter().any(|m| m.0 == **b)).map(|b| Input::Mouse(*b)))
        .or_else(|| pad.and_then(|p| p.get_just_pressed().find(|b| PAD.contains(b)).map(|b| Input::Pad(*b))));
    // Escape cancels (0x00448260: key 1), Pause is ignored (0xc5); the pad's Options (its pause) cancels too
    let cancel = keys.just_pressed(KeyCode::Escape) || pad.is_some_and(|p| p.just_pressed(GamepadButton::Start));
    match c.step(held, pressed, cancel) {
        CaptureStep::Wait => {}
        CaptureStep::Arm => menu.capture = Some(Capture { armed: true, ..c }),
        CaptureStep::Cancel => {
            menu.capture = None;
            controls.consume();
            sounds.write(crate::audio::PlaySound::menu("ui_cancel"));
            info!("EVENT controls: {:?} rebinding cancelled", c.bind);
        }
        CaptureStep::Bind(i) => {
            let lost = opts.bindings.assign(c.bind, i);
            opts.save();
            menu.capture = None;
            controls.consume();
            sounds.write(crate::audio::PlaySound::menu("ui_select"));
            info!("EVENT controls: {:?} {} {}", c.bind, if lost { "unbound from" } else { "bound to" }, i.name());
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn k(c: KeyCode) -> Input {
        Input::Key(c)
    }

    #[test]
    fn assign_follows_the_original_rule() {
        let mut b = KeyBindings::default();
        // [Z, E] + K -> [E, K] (the old secondary becomes the primary)
        assert!(!b.assign(Bind::Aim, k(KeyCode::KeyK)));
        assert_eq!(b.get(Bind::Aim, Device::Keys), &[k(KeyCode::KeyE), k(KeyCode::KeyK)]);
        // [R] + T -> [R, T]
        b.assign(Bind::Reload, k(KeyCode::KeyT));
        assert_eq!(b.get(Bind::Reload, Device::Keys), &[k(KeyCode::KeyR), k(KeyCode::KeyT)]);
        // a key taken from another action leaves it: Q from Next Weapon to Fire
        b.assign(Bind::Fire, k(KeyCode::KeyQ));
        assert_eq!(b.get(Bind::SwitchWeapon, Device::Keys), &[] as &[Input]);
        assert_eq!(b.get(Bind::Fire, Device::Keys), &[Input::Mouse(MouseButton::Left), k(KeyCode::KeyQ)]);
        // an action's own key unbinds it
        assert!(b.assign(Bind::Fire, k(KeyCode::KeyQ)));
        assert_eq!(b.get(Bind::Fire, Device::Keys), &[Input::Mouse(MouseButton::Left)]);
        // the pad has its own table and three slots
        b.assign(Bind::Jump, Input::Pad(GamepadButton::West));
        assert_eq!(b.get(Bind::Use, Device::Pad), &[] as &[Input]);
        assert_eq!(b.get(Bind::Jump, Device::Pad), &[Input::Pad(GamepadButton::South), Input::Pad(GamepadButton::West)]);
        assert_eq!(b.get(Bind::Jump, Device::Keys), &[k(KeyCode::Space), Input::Mouse(MouseButton::Middle)]);
    }

    #[test]
    fn names_round_trip_and_old_files_get_new_actions() {
        let b = KeyBindings::default();
        let json = serde_json::to_string(&b).unwrap();
        let back: KeyBindings = serde_json::from_str(&json).unwrap();
        assert_eq!(back, b);
        // a file that only rebinds Fire to K: the rest default, except where K was
        let mut partial: KeyBindings = serde_json::from_str(r#"{"keys": {"Fire": ["KeyK"]}, "pad": {}}"#).unwrap();
        partial = partial.normalized();
        assert_eq!(partial.get(Bind::Fire, Device::Keys), &[k(KeyCode::KeyK)]);
        assert_eq!(partial.get(Bind::Use, Device::Keys), &[k(KeyCode::KeyX), k(KeyCode::KeyF)]);
        assert_eq!(partial.get(Bind::Jump, Device::Pad), &[Input::Pad(GamepadButton::South)]);
        assert!(serde_json::from_str::<Input>("\"Mouse:Middle\"").is_ok());
        assert!(serde_json::from_str::<Input>("\"NoSuchKey\"").is_err());
    }

    #[test]
    fn capture_arms_after_a_release_and_binds_the_device_s_press() {
        let c = Capture { bind: Bind::Fire, device: Device::Keys, armed: false };
        // the Enter (or click) that opened the prompt is still down: wait
        assert_eq!(c.step(true, None, false), CaptureStep::Wait);
        assert_eq!(c.step(false, None, false), CaptureStep::Arm);
        let c = Capture { armed: true, ..c };
        assert_eq!(c.step(false, Some(Input::Pad(GamepadButton::South)), false), CaptureStep::Wait);
        assert_eq!(c.step(true, Some(k(KeyCode::KeyK)), false), CaptureStep::Bind(k(KeyCode::KeyK)));
        assert_eq!(c.step(true, Some(k(KeyCode::Escape)), true), CaptureStep::Cancel);
    }
}
