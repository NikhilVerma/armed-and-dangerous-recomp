//! Menus with bevy_egui (community plugin): title menu, pause menu (Esc), a live stats panel (Tab).

use crate::levelreset::LevelAppExt;
use crate::AppState;
use crate::ai::AiStats;
use crate::fx::FxStats;
use crate::level::LevelInfo;
use crate::physics::Physics;
use bevy::diagnostic::{DiagnosticsStore, FrameTimeDiagnosticsPlugin};
use bevy::prelude::*;
use bevy_egui::{EguiContexts, EguiPrimaryContextPass, egui};

#[derive(Resource)]
pub struct MenuState {
    pub stats: bool,
}

pub struct MenuPlugin;

impl Plugin for MenuPlugin {
    fn build(&self, app: &mut App) {
        app.insert_level_resource(|| MenuState { stats: true })
            .add_systems(EguiPrimaryContextPass, ui)
            .add_systems(Update, keys);
    }
}

fn keys(keys: Res<ButtonInput<KeyCode>>, state: Res<State<AppState>>, mut next: ResMut<NextState<AppState>>, mut menu: ResMut<MenuState>) {
    // Esc and the menus themselves: gamemenu.rs (the game's own menus); F3 toggles this debug stats panel
    let _ = (&state, &mut next);
    if keys.just_pressed(KeyCode::F3) {
        menu.stats = !menu.stats;
    }
}

#[allow(clippy::too_many_arguments)]
fn ui(
    mut contexts: EguiContexts,
    state: Res<State<AppState>>,
    mut next: ResMut<NextState<AppState>>,
    mut menu: ResMut<MenuState>,
    mut ai: ResMut<AiStats>,
    fx: Res<FxStats>,
    level: Res<LevelInfo>,
    physics: NonSend<Physics>,
    diag: Res<DiagnosticsStore>,
    mut exit: MessageWriter<AppExit>,
    mut time: ResMut<Time<Virtual>>,
    vm: Option<Res<crate::fxvm::FxVm>>,
    rows: Query<&crate::gamemenu::MenuRow>,
) -> Result {
    let game_menus = !rows.is_empty();
    let ctx = contexts.ctx_mut()?;
    match state.get() {
        // the title / pause windows only when the game menus are not available (benchmark, fx test)
        AppState::Title | AppState::Paused if !game_menus => {
            let title = *state.get() == AppState::Title;
            if !title {
                time.pause();
            }
            egui::Window::new(if title { "Armed and Dangerous" } else { "Paused" })
                .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
                .collapsible(false)
                .resizable(false)
                .show(ctx, |ui| {
                    ui.set_min_width(320.0);
                    if title {
                        ui.label("Remaster engine MVP: Bevy 0.20 renderer, Jolt Physics 5.6, retail level data.");
                        ui.label(format!("Level {}: {} objects, {} grunts, {} destructible", level.name, level.instances, level.grunts, level.destructibles));
                        ui.separator();
                    }
                    if ui.button(if title { "Play" } else { "Resume" }).clicked() {
                        time.unpause();
                        next.set(AppState::Playing);
                    }
                    ui.checkbox(&mut ai.enabled, "Grunt AI");
                    ui.checkbox(&mut menu.stats, "Stats panel (Tab)");
                    ui.separator();
                    ui.label("Right mouse: look   WASD / Q E: move   Shift: fast");
                    ui.label("Left mouse: shoot   F: throw tickerbomb   G: airstrike   Esc: pause");
                    ui.separator();
                    if ui.button("Quit").clicked() {
                        exit.write(AppExit::Success);
                    }
                });
        }
        _ => {}
    }
    if menu.stats {
        let fps = diag.get(&FrameTimeDiagnosticsPlugin::FPS).and_then(|d| d.smoothed()).unwrap_or(0.0);
        let ms = diag.get(&FrameTimeDiagnosticsPlugin::FRAME_TIME).and_then(|d| d.smoothed()).unwrap_or(0.0);
        egui::Window::new("Stats").anchor(egui::Align2::LEFT_TOP, [8.0, 8.0]).resizable(false).show(ctx, |ui| {
            ui.monospace(format!("{fps:6.1} fps  {ms:5.2} ms"));
            ui.monospace(format!("draws {} (before batching), {} triangles", level.draws, level.triangles));
            ui.monospace(format!("Jolt: {} dynamic, {} static ({} tris), step {:.2} ms", physics.bodies.len(), level.static_bodies, level.static_triangles, physics.step_ms));
            ui.monospace(format!("AI: {} alive, {} down, {} shots, {:.2} ms", ai.alive, ai.dead, ai.shots, ai.ms));
            ui.monospace(format!("explosions {}, objects destroyed {}", fx.explosions, fx.deaths));
            if let Some(vm) = &vm {
                ui.monospace(format!("fx VM: {} scripts, {} objects, {} particles ({} refused), {:.2} ms", vm.stats.instances, vm.stats.objects, vm.stats.particles, vm.stats.dropped, vm.stats.ms));
            }
        });
    }
    Ok(())
}
