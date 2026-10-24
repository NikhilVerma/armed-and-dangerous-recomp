//! The developer overlays (top right): the frame rate and frame time, and a small state panel (the level, the
//! mission's state and objective, Roman's position in game units, health, the weapon in hand and its ammo). Also the
//! egui zoom for every developer screen: the window runs at scale factor 1, so egui is scaled with the window height
//! (1080 lines = 1.0) to stay readable at 4K.

use bevy::diagnostic::{DiagnosticsStore, FrameTimeDiagnosticsPlugin};
use bevy::prelude::*;
use bevy_egui::{EguiContexts, EguiPrimaryContextPass, egui};

pub struct OverlayPlugin;

impl Plugin for OverlayPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(EguiPrimaryContextPass, (zoom, draw).chain());
    }
}

/// egui's zoom from the window height.
pub fn zoom(mut contexts: EguiContexts, windows: Query<&Window>) -> Result {
    let Ok(w) = windows.single() else { return Ok(()) };
    let z = (w.height() / 1080.0).clamp(1.0, 4.0);
    let ctx = contexts.ctx_mut()?;
    if (ctx.zoom_factor() - z).abs() > 0.01 {
        ctx.set_zoom_factor(z);
    }
    Ok(())
}

#[allow(clippy::too_many_arguments)]
fn draw(
    mut contexts: EguiContexts,
    opts: Res<crate::options::GameOptions>,
    diag: Res<DiagnosticsStore>,
    levels: Res<crate::campaign::Levels>,
    mission: Option<Res<crate::mission::Mission>>,
    roman: Query<(&crate::player::Roman, &Transform, &crate::fx::Health)>,
) -> Result {
    let d = opts.dev;
    if !(d.show_fps || d.show_state) {
        return Ok(());
    }
    let ctx = contexts.ctx_mut()?;
    egui::Area::new(egui::Id::new("dev_overlay")).anchor(egui::Align2::RIGHT_TOP, [-10.0, 10.0]).interactable(false).show(ctx, |ui| {
        egui::Frame::popup(ui.style()).show(ui, |ui| {
            if d.show_fps {
                let fps = diag.get(&FrameTimeDiagnosticsPlugin::FPS).and_then(|d| d.smoothed()).unwrap_or(0.0);
                let ms = diag.get(&FrameTimeDiagnosticsPlugin::FRAME_TIME).and_then(|d| d.smoothed()).unwrap_or(0.0);
                ui.monospace(format!("{fps:6.1} fps  {ms:6.2} ms"));
            }
            if d.show_state {
                ui.monospace(format!("level    {}", levels.current_name()));
                if let Some(m) = &mission {
                    ui.monospace(format!("mission  {:?}{}", m.outcome, if m.started() { "" } else { " (not started)" }));
                    let obj: String = m.objectives.lines().next().unwrap_or("").chars().take(48).collect();
                    if !obj.is_empty() {
                        ui.monospace(format!("goal     {obj}"));
                    }
                }
                if let Ok((r, t, h)) = roman.single() {
                    let g = crate::level::game_to_engine().inverse() * t.translation;
                    ui.monospace(format!("roman    {:.0} {:.0} {:.0}", g.x, g.y, g.z));
                    ui.monospace(format!("health   {:.0} / {:.0}", h.0, r.max_health));
                    let gun = r.arsenal.in_hand().map(|g| format!("{} {} + {}", g.id.name(), g.clip, g.ammo)).unwrap_or_else(|| "none".into());
                    ui.monospace(format!("weapon   {gun}"));
                    ui.monospace(format!("special  {:?} x{}", r.arsenal.special, r.arsenal.count(r.arsenal.special)));
                }
            }
        });
    });
    Ok(())
}
