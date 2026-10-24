//! The UI's additive blend: a HUD part whose translucent material has blend mode 0 (the weapon-cycle arrows'
//! "off" state, the hit markers, the auto-aim ring) is drawn SRCALPHA / ONE: the draw state 0x004897d0
//! (render_r4.cpp:548) gives mode 0 the state bits 0x100, which select the blend table 0x0057d9e0 entry 1 {src 5
//! SRCALPHA, dest 2 ONE, op ADD} (read from the exe; entry 3, mode 2, is SRCALPHA / INVSRCALPHA, the ColorMaterial's
//! Blend; entry 4, mode 1, DESTCOLOR / ZERO, which no UI model uses). Bevy's 2D materials only alpha-blend, so this
//! is a small material of its own: the texture times `color`, added to what is behind it in proportion to its alpha.
//!
//! API: [`HudBlendPlugin`]; [`AddMaterial`].

use bevy::mesh::MeshVertexBufferLayoutRef;
use bevy::prelude::*;
use bevy::render::render_resource::{AsBindGroup, BlendComponent, BlendFactor, BlendOperation, BlendState, RenderPipelineDescriptor, SpecializedMeshPipelineError};
use bevy::shader::ShaderRef;
use bevy::sprite_render::{AlphaMode2d, Material2d, Material2dKey, Material2dPipeline, Material2dPlugin};

pub struct HudBlendPlugin;

impl Plugin for HudBlendPlugin {
    fn build(&self, app: &mut App) {
        bevy::asset::embedded_asset!(app, "hudblend.wesl");
        app.add_plugins(Material2dPlugin::<AddMaterial>::default());
    }
}

/// A textured UI part drawn additively: texture x `color` (its alpha the material's opacity), SRCALPHA / ONE.
#[derive(Asset, AsBindGroup, TypePath, Debug, Clone)]
pub struct AddMaterial {
    #[uniform(0)]
    pub color: LinearRgba,
    #[texture(1)]
    #[sampler(2)]
    pub texture: Handle<Image>,
}

impl Material2d for AddMaterial {
    fn fragment_shader() -> ShaderRef {
        ShaderRef::Path(bevy::asset::AssetPath::from_path_buf(bevy::asset::embedded_path!("hudblend.wesl")).with_source("embedded"))
    }
    fn alpha_mode(&self) -> AlphaMode2d {
        // drawn in the transparent phase, in z order with the other HUD parts
        AlphaMode2d::Blend
    }
    fn specialize(_: &Material2dPipeline, descriptor: &mut RenderPipelineDescriptor, _: &MeshVertexBufferLayoutRef, _: Material2dKey<Self>) -> Result<(), SpecializedMeshPipelineError> {
        let add = BlendComponent { src_factor: BlendFactor::SrcAlpha, dst_factor: BlendFactor::One, operation: BlendOperation::Add };
        let keep = BlendComponent { src_factor: BlendFactor::Zero, dst_factor: BlendFactor::One, operation: BlendOperation::Add };
        if let Some(fragment) = descriptor.fragment.as_mut() {
            for target in fragment.targets.iter_mut().flatten() {
                target.blend = Some(BlendState { color: add, alpha: keep });
            }
        }
        Ok(())
    }
}
