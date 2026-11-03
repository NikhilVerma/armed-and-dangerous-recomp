//! This module enlarges the game's picture with Apple's MetalFX upscaler when the render scale is below 1.
//!
//! A lower render scale cools a laptop, because the graphics chip draws fewer pixels. The smaller picture still has to
//! fill the window. Other systems stretch it with a bilinear filter, which looks soft, and MetalFX's spatial scaler
//! rebuilds the edges instead.
//!
//! The scaler runs as the 3D view's last step, so TAA and the post effects stay at the lower resolution. That keeps the
//! saving. The result goes into a window-sized image, which the HUD camera shows (display/render_scale.rs).
//!
//! The crate `bevy_metalfx` runs the post effects at full size, which cost far more in tests (docs/macos.md, "Heat").

use crate::display::render_scale::{MetalFxActive, ScaledView};
use bevy::camera::NormalizedRenderTarget;
use bevy::core_pipeline::schedule::Core3d;
use bevy::core_pipeline::upscaling::upscaling;
use bevy::prelude::*;
use bevy::render::camera::ExtractedCamera;
use bevy::render::extract_resource::ExtractResourcePlugin;
use bevy::render::render_asset::RenderAssets;
use bevy::render::render_resource::CommandEncoderDescriptor;
use bevy::render::renderer::{RenderContext, RenderDevice, ViewQuery};
use bevy::render::texture::GpuImage;
use bevy::render::RenderApp;
use objc2::rc::Retained;
use objc2::runtime::ProtocolObject;
use objc2_metal::{MTLCommandBuffer, MTLDevice, MTLPixelFormat, MTLTexture};
use objc2_metal_fx::{MTLFXSpatialScaler, MTLFXSpatialScalerBase, MTLFXSpatialScalerColorProcessingMode, MTLFXSpatialScalerDescriptor};
use std::sync::atomic::Ordering;

pub struct MetalFxUpscalerPlugin;

impl Plugin for MetalFxUpscalerPlugin {
    fn build(&self, app: &mut App) {
        let active = MetalFxActive::default();
        app.insert_resource(active.clone()).add_plugins(ExtractResourcePlugin::<ScaledView>::default());
        if let Some(render_app) = app.get_sub_app_mut(RenderApp) {
            render_app.insert_resource(active).add_systems(Core3d, upscale.after(upscaling));
        }
    }
}

/// A scaler is built for one pair of sizes, so a new size needs a new scaler.
struct SizedScaler {
    scaler: Retained<ProtocolObject<dyn MTLFXSpatialScaler>>,
    input: UVec2,
    output: UVec2,
}

// Metal objects are safe to use from any thread, and only this one system touches the scaler.
unsafe impl Send for SizedScaler {}
unsafe impl Sync for SizedScaler {}

/// Returns `None` when the GPU has no MetalFX support, which can happen on older Intel Macs.
fn new_scaler(device: &RenderDevice, input: UVec2, output: UVec2) -> Option<SizedScaler> {
    // SAFETY: the device outlives this call, and no encoder holds wgpu's lock here.
    let hal_device = unsafe { device.wgpu_device().as_hal::<wgpu_hal::metal::Api>() }?;
    let raw_device: &ProtocolObject<dyn MTLDevice> = hal_device.raw_device();
    // SAFETY: plain property calls on a descriptor created here.
    let scaler = unsafe {
        let descriptor = MTLFXSpatialScalerDescriptor::new();
        descriptor.setInputWidth(input.x as usize);
        descriptor.setInputHeight(input.y as usize);
        descriptor.setOutputWidth(output.x as usize);
        descriptor.setOutputHeight(output.y as usize);
        descriptor.setColorTextureFormat(MTLPixelFormat::RGBA8Unorm_sRGB);
        descriptor.setOutputTextureFormat(MTLPixelFormat::RGBA8Unorm_sRGB);
        descriptor.setColorProcessingMode(MTLFXSpatialScalerColorProcessingMode::Perceptual);
        descriptor.newSpatialScalerWithDevice(raw_device)
    }?;
    Some(SizedScaler { scaler, input, output })
}

/// Draws the game view's finished picture into the window-sized image. It runs once per camera and acts only for the
/// camera that draws into the game view's image.
fn upscale(
    view: ViewQuery<&'static ExtractedCamera>,
    scaled: Res<ScaledView>,
    active: Res<MetalFxActive>,
    images: Res<RenderAssets<GpuImage>>,
    device: Res<RenderDevice>,
    mut current: Local<Option<SizedScaler>>,
    mut ctx: RenderContext,
) {
    let (Some(input_handle), Some(output_handle)) = (&scaled.image, &scaled.upscaled) else { return };
    let Some(NormalizedRenderTarget::Image(target)) = &view.into_inner().target else { return };
    if target.handle != *input_handle {
        return;
    }
    let (Some(input), Some(output)) = (images.get(input_handle), images.get(output_handle)) else { return };
    let (input_size, output_size) = (input.size_2d(), output.size_2d());

    if !current.as_ref().is_some_and(|c| c.input == input_size && c.output == output_size) {
        *current = new_scaler(&device, input_size, output_size);
        if current.is_none() {
            warn!("display: MetalFX is not available on this GPU, using the plain stretch");
            active.unavailable.store(true, Ordering::Relaxed);
            return;
        }
    }
    let Some(SizedScaler { scaler, .. }) = current.as_ref() else { return };

    // wgpu's lock can't be held twice, so the texture handles are taken and released before the encoder is opened.
    // SAFETY: both textures live for the whole frame, and each guard ends with its statement.
    let (Some(color), Some(out)) = (
        unsafe { input.texture.as_hal::<wgpu_hal::metal::Api>() }.map(|t| Retained::from(raw_texture(&t))),
        unsafe { output.texture.as_hal::<wgpu_hal::metal::Api>() }.map(|t| Retained::from(raw_texture(&t))),
    ) else {
        return;
    };

    // MetalFX needs a command buffer with no open pass, so it gets an encoder of its own.
    let mut encoder = device.wgpu_device().create_command_encoder(&CommandEncoderDescriptor { label: Some("metalfx_upscale") });
    // SAFETY: the textures are in the formats and sizes the scaler was built for, and no pass is open.
    unsafe {
        encoder.as_hal_mut::<wgpu_hal::metal::Api, _, ()>(|hal| {
            let Some(buffer) = hal.and_then(|e| e.raw_command_buffer()) else { return };
            let buffer: &ProtocolObject<dyn MTLCommandBuffer> = buffer;
            scaler.setColorTexture(Some(&color));
            scaler.setOutputTexture(Some(&out));
            scaler.setInputContentWidth(input_size.x as usize);
            scaler.setInputContentHeight(input_size.y as usize);
            scaler.encodeToCommandBuffer(buffer);
        });
    }
    ctx.add_command_buffer(encoder.finish());
}

fn raw_texture(texture: &wgpu_hal::metal::Texture) -> &ProtocolObject<dyn MTLTexture> {
    texture.raw_handle()
}
