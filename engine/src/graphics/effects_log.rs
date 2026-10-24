//! The playtest state log's "render_fx": what the old remaster's effects (height_fog.rs, ao_extras.rs,
//! motion_blur.rs, present.rs) put on the 3D camera and the game materials, so a script checks that a setting reaches
//! the renderer (`{"expect": {"render_fx.fog_density": 1.0}}`, engine/playtests/gfx_effects*.json).

use crate::gamematerial::GameMaterial;
use crate::playtest::{PlayState, Playtest};
use bevy::core_pipeline::prepass::{DepthPrepass, MotionVectorPrepass};
use bevy::prelude::*;

pub struct EffectsLogPlugin;

impl Plugin for EffectsLogPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Last, log_effects.run_if(resource_exists::<Playtest>));
    }
}

#[allow(clippy::type_complexity)]
fn log_effects(
    cams: Query<
        (&Camera, Option<&super::present::PresentPass>, Option<&super::motion_blur::ObjectMotionBlur>, Has<bevy::camera::Hdr>, Has<DepthPrepass>, Has<MotionVectorPrepass>),
        With<Camera3d>,
    >,
    materials: Res<Assets<GameMaterial>>,
    lights: Option<Res<crate::gamematerial::GameLights>>,
    ssao: Res<super::ao_extras::SsaoSource>,
    mut state: ResMut<PlayState>,
) {
    let Some((_, present, blur, hdr, depth, motion)) = cams.iter().find(|c| c.0.is_active) else { return };
    let r = |v: f32| ((v as f64) * 10000.0).round() / 10000.0;
    // the first world material under the level's fog (the sky has none)
    let mat = materials.iter().map(|(_, m)| &m.params).find(|p| p.fog_range.y > p.fog_range.x);
    let fog = mat.map(|p| p.height_fog).unwrap_or_default();
    let ao = mat.map(|p| p.ao_extras).unwrap_or_default();
    state.0.insert(
        "render_fx".into(),
        serde_json::json!({
            "hdr": hdr,
            "aces": present.is_some_and(|p| p.aces != 0),
            "dither": present.is_some_and(|p| p.dither != 0),
            "motion_blur": blur.is_some(),
            "blur_exposure": r(blur.map_or(0.0, |b| b.uniform.exposure)),
            "prepass": depth && motion,
            "fog_modern": fog.x > 0.0,
            "fog_density": r(fog.x),
            "fog_height": r(fog.y),
            "fog_sun": r(fog.z),
            "fog_reference": lights.and_then(|l| l.fog_reference).map(r),
            "ao_contact": r(ao.x),
            "ao_snow": r(ao.y),
            "xegtao": ssao.applied().is_some(),
            "ao_power": ssao.applied().map(|c| r(c.power)),
            "ao_falloff": ssao.applied().map(|c| r(c.falloff)),
        }),
    );
}
