//! The remaster's Settings page (`modern_menus`, display/mod.rs): every option of the original's Options pages and
//! of the remaster's own pages (Engine, Display, Detail and Shadows, Effects) and the developer toggles, in tabs.
//! Each tab is one scrolling list (gamemenu/list.rs); each row has a line of help. Changes apply at once and are
//! saved when the page is left.
//!
//! The page is data: [`TABS`] lists the rows, each naming an option the menu already edits (`Opt`, `Flag`) or a page
//! or question it opens. To add a setting, add a row.

use super::list::{ListPos, ROW, TOP};
use super::*;
use crate::devtools::DevToggle;
use crate::display::menu::Stepper;

pub enum Kind {
    Check(Flag),
    Slide(Opt),
    /// opens a page or asks a question
    Open(Act),
}

pub struct Item {
    /// a text id of the game's text bank, or the text itself
    pub label: &'static str,
    pub hint: &'static str,
    pub kind: Kind,
}

pub struct Tab {
    pub name: &'static str,
    pub hint: &'static str,
    pub items: &'static [Item],
}

const fn check(label: &'static str, flag: Flag, hint: &'static str) -> Item {
    Item { label, hint, kind: Kind::Check(flag) }
}

const fn slide(label: &'static str, opt: Opt, hint: &'static str) -> Item {
    Item { label, hint, kind: Kind::Slide(opt) }
}

const fn open(label: &'static str, act: Act, hint: &'static str) -> Item {
    Item { label, hint, kind: Kind::Open(act) }
}

const fn gfx(label: &'static str, key: &'static str, hint: &'static str) -> Item {
    check(label, Flag::Graphics(key), hint)
}

const fn num(label: &'static str, key: &'static str, hint: &'static str) -> Item {
    slide(label, Opt::Number(key), hint)
}

const fn dev(label: &'static str, toggle: DevToggle, hint: &'static str) -> Item {
    check(label, Flag::Dev(toggle), hint)
}

pub const TABS: [Tab; 8] = [
    Tab {
        name: "Game",
        hint: "Subtitles, saving and help with aiming.",
        items: &[
            check("ui_controls_autoaim", Flag::AutoAim, "Pulls your shots toward the enemy under the crosshair. Fixed above Normal difficulty."),
            check("ui_sfxmenu_subtitles_ingame", Flag::SubsGame, "Shows what the characters say during play."),
            check("ui_sfxmenu_subtitles_movie", Flag::SubsMovie, "Shows what the characters say in cut-scenes."),
            check("Checkpoint Saves Only", Flag::CheckpointSaves, "On: save only where the original lets you. Off: save anywhere from the pause menu."),
            open("ui_optionsmenu_reset", Act::Ask(Ask::ResetExtras), "Locks the extras again."),
            open("ui_extrasmenu_credits", Act::Go(Page::Credits), "The people who made Armed and Dangerous."),
        ],
    },
    Tab {
        name: "Controls",
        hint: "Mouse, pointer and key bindings.",
        items: &[
            slide("ui_controls_mousescale", Opt::MouseSens, "How far the view turns when you move the mouse."),
            check("ui_controls_flipmousey", Flag::InvertMouse, "Moving the mouse up looks down."),
            gfx("Raw Mouse Input", "raw_mouse", "Reads the mouse or trackpad itself, not the system pointer (macOS)."),
            gfx("Windows Pointer", "hardware_cursor", "Uses the system's pointer in the menus. Off: the game draws its own."),
            gfx("Keep Mouse In Window", "clip_cursor", "The pointer cannot leave the game's window."),
            open("Key Bindings ...", Act::Go(Page::Controls), "Choose the keys and buttons for each action."),
            open("ui_controls_reset", Act::Ask(Ask::ResetControls), "Back to the original keys, buttons and mouse settings."),
        ],
    },
    Tab {
        name: "Audio",
        hint: "Volumes.",
        items: &[
            slide("Master Volume", Opt::MasterVolume, "Over everything below."),
            slide("ui_sfxmenu_soundvolume", Opt::Effects, "Guns, explosions and the world."),
            slide("ui_sfxmenu_musicvolume", Opt::Music, "The score."),
            slide("ui_sfxmenu_voicevolume", Opt::Voice, "Speech in play and in cut-scenes."),
        ],
    },
    Tab {
        name: "Display",
        hint: "Window, resolution, view and text size.",
        items: &[
            slide("Display Mode", Opt::Display(Stepper::DisplayMode), "Fullscreen changes the monitor's mode. Borderless covers the desktop."),
            slide("ui_gfxmenu_resolution", Opt::Resolution, "The window's size, or the monitor's mode in Fullscreen."),
            slide("Render Scale", Opt::Display(Stepper::RenderScale), "The 3D picture's size against the window's. Above 1 is sharper and slower."),
            slide("Frame Rate Limit", Opt::Display(Stepper::FpsLimit), "Frames per second at most. Off: as fast as the machine goes."),
            slide("ui_gfxmenu_brightness", Opt::Brightness, "1.5 is the original's picture."),
            check("Gamma Brightness", Flag::Display("brightness_shader"), "Brightness works as the original's gamma ramp, in a window too."),
            slide("Field Of View", Opt::Display(Stepper::Fov), "1 is the original's view. Higher shows more to the sides."),
            check("FOV When Zoomed", Flag::Display("fov_zoom"), "The field of view setting also widens scopes and weapon cameras."),
            check("Widescreen (Hor+)", Flag::Display("hor_plus"), "Wide screens see more. Off: the 4:3 picture is stretched, as in 2003."),
            check("Cinematic Letterbox", Flag::Display("letterbox"), "The original's black bands above and below the picture."),
            check("4:3 HUD And Menus", Flag::Display("ui_anchor"), "Keeps the HUD and menus in proportion on wide screens."),
            slide("Text Size", Opt::Display(Stepper::TextScale), "Makes the game's text larger."),
            check("Text Follows Resolution", Flag::Display("text_relative"), "Text keeps its size at every resolution. Off: tiny at 4K, as the original."),
            check("All Resolutions", Flag::Display("all_modes"), "Lists every mode the monitor has. Off: the original's short list."),
            check("Sharp On High-DPI", Flag::Display("dpi_aware"), "Draws at the screen's real pixels. Takes effect at the next start."),
            check("Modern Menus", Flag::Display("modern_menus"), "These menus. Off: the original's pages (Options, Display, to turn it back on)."),
        ],
    },
    Tab {
        name: "Graphics",
        hint: "The picture: presets, textures, shadows and detail.",
        items: &[
            slide("Preset", Opt::Preset, "Remaster: the new look. Original: the 2003 game. Choosing one drops your changes."),
            slide("Anti-Aliasing", Opt::AntiAliasing, "Smooths jagged edges."),
            slide("ui_gfxmenu_drawdistance", Opt::DrawDistance, "How far scenery is drawn."),
            gfx("Remaster Textures", "remaster_textures", "The upscaled texture pack. Takes effect when a level loads."),
            check("ui_gfxmenu_shadows", Flag::Shadows, "Shadows from the sun."),
            gfx("Soft Shadows", "soft_shadows", "Shadow edges blur with distance from what casts them."),
            num("Shadow Darkness", "shadow_strength", "0 is the original's tone."),
            gfx("Ambient Occlusion", "ambient_occlusion", "Darkens corners and creases."),
            gfx("Soft Particles", "soft_particles", "Smoke and fire fade where they meet the ground."),
            gfx("Tessellated Characters", "tessellation", "Rounds off the characters' models."),
            num("Tessellation Detail", "tessellation_level", "How finely models are rounded."),
            gfx("Tessellated Scenery", "tessellation_scenery", "Rounds off rocks, trees and buildings."),
            num("Terrain Detail", "terrain_detail", "Adds fine shape to the landscape."),
            gfx("Ground Detail", "ground_detail", "Close-up rock, dirt, grass and snow on the landscape."),
            gfx("Always Highest Detail", "lod_max", "Never swaps in the simpler far models."),
            num("Detail Distance", "lod_scale", "How far away the detailed models last."),
            gfx("Smooth Foliage Edges", "alpha_coverage", "Leaves and fences get smooth edges."),
            num("Texture Sharpness", "aa_mip_bias", "Lower is sharper in the distance and can shimmer."),
        ],
    },
    Tab {
        name: "Effects",
        hint: "Fog, blur, snow and the final picture.",
        items: &[
            gfx("Height Fog", "fog_modern", "Fog that gathers in valleys and catches the sun."),
            gfx("Motion Blur", "shutter_speed", "Blurs fast movement, like a film camera."),
            gfx("Contact Shadows", "ao_contact", "Small shadows where things touch."),
            gfx("Filmic Tone Mapping", "tonemap_aces", "A film-like response to bright light."),
            gfx("Dithering", "dither", "Hides banding in skies and fog."),
            gfx("Smooth Pause Blur", "blur_smooth", "A soft, even blur behind menus and messages."),
            gfx("GPU Snow", "snow", "Real falling flakes in the snow levels."),
            num("Snow Amount", "snow_amount", "How many flakes fall."),
        ],
    },
    Tab {
        name: "Engine",
        hint: "The engine's limits. The original's values are marked.",
        items: &[
            gfx("Physics At Real Speed", "physics_fixed_step", "Physics runs at the original's rate at any frame rate."),
            gfx("Smooth Physics", "physics_interpolation", "Draws moving things between physics steps."),
            slide("Solver Iterations", Opt::Choice("physics_solver_iterations"), "More is steadier stacks and ragdolls, and slower."),
            slide("Physics Bodies", Opt::Choice("physics_body_capacity"), "How many physics objects a level may hold."),
            slide("Decal Pool", Opt::Choice("decal_pool"), "Bullet holes and scorch marks kept at once."),
            slide("Particle Pool", Opt::Choice("particle_pool_scale"), "Room for smoke, sparks and fire."),
            slide("Path Search", Opt::Choice("path_nodes"), "How far the characters can plan a route."),
            gfx("Keep Debris", "debris_keep", "Wreckage stays instead of fading."),
            slide("Debris Stays", Opt::Choice("debris_time"), "How long wreckage lies."),
            slide("Debris Kept", Opt::Choice("debris_max"), "Pieces of wreckage kept at once."),
        ],
    },
    Tab {
        name: "Flags",
        hint: "Switches for testing. They change the game's rules.",
        items: &[
            dev("God Mode", DevToggle::GodMode, "Roman takes no damage."),
            dev("Invincible", DevToggle::Invincible, "Roman and his friends take no damage."),
            dev("Infinite Ammo", DevToggle::InfiniteAmmo, "Guns never run dry."),
            dev("Unlock Everything", DevToggle::UnlockAll, "Every level and cut-scene in Extras."),
            dev("Big Heads", DevToggle::BigHeads, "The original's cheat."),
            dev("Big Hands", DevToggle::BigHands, "The original's cheat."),
            dev("Big Boots", DevToggle::BigBoots, "The original's cheat."),
            dev("Topsy Turvy Screen", DevToggle::TopsyScreen, "Turns the picture upside down."),
            dev("Show Frame Rate", DevToggle::ShowFps, "Frames per second and frame time in a corner."),
            dev("Show State", DevToggle::ShowState, "The developer's state overlay."),
            open("ui_optionsmenu_cheats", Act::Go(Page::Cheats), "Type the original's cheat codes."),
        ],
    },
];

/// The labels' right edge, the checkbox's centre and the gauge's item point (its frame starts 77 right of it, where
/// the checkbox starts).
const LABEL_X: f32 = 250.0;
const BOX_X: f32 = 272.0;
const GAUGE_X: f32 = 187.0;
const LABEL_FIT: f32 = 215.0;

impl Spawner<'_, '_, '_> {
    /// The Settings page: the tab bar, every tab's rows (the open tab's are shown: `MenuRow::live`), the help line, Back.
    pub(super) fn settings_page(&mut self) {
        let p = Page::Settings;
        let n = TABS.len();
        for (i, tab) in TABS.iter().enumerate() {
            let x = 320.0 + (i as f32 - (n - 1) as f32 / 2.0) * 74.0;
            self.hint = tab.hint;
            self.row(p, tab.name, Vec2::new(x, 438.0), 0.9, GREEN, Act::Tab(i), 0.0);
            self.rule(p, Vec2::new(x, 423.0), 56.0, YELLOW, PartKind::TabMark(i));
        }
        self.rule(p, Vec2::new(320.0, 408.0), 600.0, Color::srgb_u8(0x20, 0x40, 0x00), PartKind::Fixed);
        self.list_frame(p);
        for (t, tab) in TABS.iter().enumerate() {
            for (slot, item) in tab.items.iter().enumerate() {
                let y = TOP - ROW * slot as f32;
                self.list = Some(ListPos { tab: Some(t), slot });
                self.hint = item.hint;
                match item.kind {
                    Kind::Check(flag) => self.check_at(p, item.label, BOX_X, LABEL_X, 1.0, LABEL_FIT, y, Some(flag), false),
                    Kind::Slide(opt) => self.slider_at(p, item.label, Vec2::new(LABEL_X, y), 1.0, LABEL_FIT, Vec2::new(GAUGE_X, y), 1.0, opt, true),
                    Kind::Open(act) => self.row(p, item.label, Vec2::new(BOX_X - 8.0, y), 1.0, ORANGE, act, -1.0),
                }
            }
        }
        self.hint_line(p);
        self.row(p, "ui_shellmenu_back", Vec2::new(320.0, 48.0), 1.4, YELLOW, Act::Back, 0.0);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::graphics::GraphicsSettings;

    /// Every row names a real setting, has a help line, and no option is on the page twice (the menu finds a row's
    /// gauge and value by its option).
    #[test]
    fn rows_name_settings() {
        let all = serde_json::to_value(GraphicsSettings::remaster()).unwrap();
        let mut seen: Vec<String> = Vec::new();
        for tab in &TABS {
            assert!(!tab.items.is_empty() && !tab.hint.is_empty(), "{}", tab.name);
            for item in tab.items {
                assert!(!item.hint.is_empty() && item.hint.len() <= 90, "{}", item.label);
                let id = match item.kind {
                    Kind::Check(f) => {
                        if let Flag::Graphics(k) | Flag::Display(k) = f {
                            assert!(all.get(k).is_some_and(|v| v.is_boolean() || v.is_number()), "{k}");
                        }
                        format!("{f:?}")
                    }
                    Kind::Slide(o) => {
                        match o {
                            Opt::Number(k) => assert!(crate::graphics::menu::stepper(k).is_some(), "{k}"),
                            Opt::Choice(k) => assert!(crate::enginepage::stepper(k).is_some(), "{k}"),
                            _ => {}
                        }
                        format!("{o:?}")
                    }
                    Kind::Open(a) => format!("{a:?}"),
                };
                assert!(!seen.contains(&id), "{id} is on the page twice");
                seen.push(id);
            }
        }
    }
}
