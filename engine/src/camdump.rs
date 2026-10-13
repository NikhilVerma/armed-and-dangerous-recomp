//! `--camera <and-frame-N.camera.json>`: renders from a camera the original game used (dumped by the hook's test
//! harness, hook/src/testshot.cpp TestDumpCamera), for side-by-side comparisons with the original's captures
//! (tools/render_compare.py). The dump holds Game+0x704, the view matrix Camera_SetupProjection 0x0040f6e0 hands to
//! SetView: rows 0..2 are the camera's right, up and back axes in game space (the camera looks down -z), column 3
//! the translation (-R * position); and the vertical FOV (camera +0xf4). The original projects with the letterboxed
//! view's aspect (viewport aspect / Renderer_GetAspect, 1.0 at 640x480): render the strip itself, 640x372.

use crate::level::game_to_engine;
use bevy::prelude::*;
use serde::Deserialize;

#[derive(Deserialize, Clone)]
pub struct CameraDump {
    pub view: [f32; 16],
    pub fov: f32,
    #[serde(default)]
    pub near: f32,
    #[serde(default)]
    pub far: f32,
}

#[derive(Resource, Clone)]
pub struct FixedCamera {
    pub transform: Transform,
    pub fov: f32,
    pub far: f32,
}

impl CameraDump {
    pub fn load(path: &std::path::Path) -> Self {
        let s = std::fs::read_to_string(path).unwrap_or_else(|e| panic!("{}: {e}", path.display()));
        serde_json::from_str(&s).expect("camera dump")
    }

    /// The camera's transform in engine space.
    pub fn fixed(&self) -> FixedCamera {
        let v = &self.view;
        let r = [Vec3::new(v[0], v[1], v[2]), Vec3::new(v[4], v[5], v[6]), Vec3::new(v[8], v[9], v[10])];
        let t = Vec3::new(v[3], v[7], v[11]);
        // position = -R^T t (game space)
        let pos = -(r[0] * t.x + r[1] * t.y + r[2] * t.z);
        let (up, back) = (r[1], r[2]);
        let c = game_to_engine();
        let transform = Transform::from_translation(c * pos).looking_to(c * -back, c * up);
        FixedCamera { transform, fov: self.fov, far: if self.far > 0.0 { self.far } else { 1000.0 } }
    }
}

pub struct CamDumpPlugin;

impl Plugin for CamDumpPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Last, hold.run_if(resource_exists::<FixedCamera>));
    }
}

/// Holds the 3D camera at the dumped view (after the follow camera and camera paths have run), and the sky dome
/// around it (level.rs follow_camera placed it at the camera's earlier position this frame).
#[allow(clippy::type_complexity)]
fn hold(
    fixed: Res<FixedCamera>,
    mut cam: Query<(&mut Transform, &mut Projection), (With<Camera3d>, Without<crate::level::Background>)>,
    mut sky: Query<&mut Transform, (With<crate::level::Background>, Without<Camera3d>)>,
) {
    for mut s in &mut sky {
        s.translation = fixed.transform.translation;
    }
    for (mut t, mut p) in &mut cam {
        *t = fixed.transform;
        if let Projection::Perspective(pp) = &mut *p {
            pp.fov = fixed.fov;
            pp.far = fixed.far;
            pp.near = 1.0;
        }
    }
}
