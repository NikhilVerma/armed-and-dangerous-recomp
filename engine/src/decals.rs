//! Decals (fx op 0x6a: Decals_Spawn 0x004165c0, Decal_Build 0x00417260, Decals_Update 0x00416440, Decals_Draw
//! 0x00416330): a box of +-size along the fx object's three axes (its matrix columns, unnormalised) around its
//! position; the landscape's triangles inside it are clipped to the box (Sutherland-Hodgman against the six planes, no
//! normal test) and drawn with the texture: uv from the two box coordinates with the largest range over the clipped
//! vertices, u_k = a_k . (x - p) / (2 size) + 0.5 (the stretch by the third coordinate, 0x004175c0, taken on the
//! normalised coordinate: guess). Texture x (1, 1, 1, fade), alpha-blended, no depth write, clamped, fogged; the
//! texture sampled as stored (premultiplied textures blended straight, as the original). 160 decals, the oldest
//! recycled at once; no expiry. Not ported: decals on placed static objects (0x00416f50), the pressure fade.

use bevy::asset::RenderAssetUsages;
use bevy::image::{ImageAddressMode, ImageLoaderSettings, ImageSampler, ImageSamplerDescriptor};
use bevy::mesh::{Indices, PrimitiveTopology};
use bevy::platform::collections::HashMap;
use bevy::prelude::*;

const CELL: f32 = 32.0;
const POOL: usize = 160;

/// The landscape's triangles (game space), bucketed by x/y cells.
#[derive(Resource)]
pub struct DecalWorld {
    verts: Vec<Vec3>,
    tris: Vec<[u32; 3]>,
    cells: HashMap<(i32, i32), Vec<u32>>,
}

impl DecalWorld {
    pub fn new(verts: &[Vec3], tris: &[[u32; 3]]) -> Self {
        let mut cells: HashMap<(i32, i32), Vec<u32>> = HashMap::default();
        for (i, t) in tris.iter().enumerate() {
            let (a, b, c) = (verts[t[0] as usize], verts[t[1] as usize], verts[t[2] as usize]);
            let lo = a.min(b).min(c);
            let hi = a.max(b).max(c);
            for x in (lo.x / CELL).floor() as i32..=(hi.x / CELL).floor() as i32 {
                for y in (lo.y / CELL).floor() as i32..=(hi.y / CELL).floor() as i32 {
                    cells.entry((x, y)).or_default().push(i as u32);
                }
            }
        }
        DecalWorld { verts: verts.to_vec(), tris: tris.to_vec(), cells }
    }
}

/// A decal to make (game space placement, half size, texture).
#[derive(Message, Clone)]
pub struct SpawnDecal {
    pub place: Mat4,
    pub size: f32,
    pub texture: String,
}

#[derive(Resource, Default)]
struct Decals {
    live: std::collections::VecDeque<Entity>,
}

pub struct DecalPlugin;

impl Plugin for DecalPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<SpawnDecal>().init_resource::<Decals>().add_systems(Update, spawn);
    }
}

fn clip(poly: Vec<Vec3>, n: Vec3, c: f32) -> Vec<Vec3> {
    let mut out = Vec::with_capacity(poly.len() + 2);
    for i in 0..poly.len() {
        let (a, b) = (poly[i], poly[(i + 1) % poly.len()]);
        let (da, db) = (n.dot(a) + c, n.dot(b) + c);
        if da >= 0.0 {
            out.push(a);
        }
        if (da >= 0.0) != (db >= 0.0) {
            out.push(a + (b - a) * (da / (da - db)));
        }
    }
    out
}

#[allow(clippy::too_many_arguments)]
fn spawn(
    mut commands: Commands,
    mut events: MessageReader<SpawnDecal>,
    world: Option<Res<DecalWorld>>,
    mut decals: ResMut<Decals>,
    assets: Res<AssetServer>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    mut textures: Local<HashMap<String, Handle<StandardMaterial>>>,
) {
    let Some(world) = world else {
        events.clear();
        return;
    };
    for ev in events.read() {
        let p = ev.place.w_axis.truncate();
        let axes = [ev.place.x_axis.truncate(), ev.place.y_axis.truncate(), ev.place.z_axis.truncate()];
        let s = ev.size;
        // the six planes: a.x + (s - a.p) >= 0 and -a.x + (s + a.p) >= 0
        let planes: Vec<(Vec3, f32)> = axes.iter().flat_map(|a| [(*a, s - a.dot(p)), (-*a, s + a.dot(p))]).collect();
        let reach = axes.iter().map(|a| a.length()).fold(0.0, f32::max) * s * 1.8;
        let mut verts: Vec<Vec3> = Vec::new();
        let mut idx: Vec<u32> = Vec::new();
        let mut seen = std::collections::HashSet::new();
        'cells: for x in ((p.x - reach) / CELL).floor() as i32..=((p.x + reach) / CELL).floor() as i32 {
            for y in ((p.y - reach) / CELL).floor() as i32..=((p.y + reach) / CELL).floor() as i32 {
                for &ti in world.cells.get(&(x, y)).map(|v| v.as_slice()).unwrap_or(&[]) {
                    if !seen.insert(ti) {
                        continue;
                    }
                    let t = world.tris[ti as usize];
                    let mut poly = vec![world.verts[t[0] as usize], world.verts[t[1] as usize], world.verts[t[2] as usize]];
                    for (n, c) in &planes {
                        if poly.is_empty() {
                            break;
                        }
                        poly = clip(poly, *n, *c);
                    }
                    if poly.len() < 3 {
                        continue;
                    }
                    // capacity 128 vertices / 384 indices: the build stops there
                    if verts.len() + poly.len() > 128 || idx.len() + (poly.len() - 2) * 3 > 384 {
                        break 'cells;
                    }
                    let base = verts.len() as u32;
                    verts.extend(poly.iter());
                    for k in 1..poly.len() as u32 - 1 {
                        idx.extend_from_slice(&[base, base + k, base + k + 1]);
                    }
                }
            }
        }
        if idx.is_empty() {
            continue;
        }
        // uv: the two box coordinates with the largest range
        let coords: Vec<[f32; 3]> = verts.iter().map(|v| axes.map(|a| a.dot(*v - p) / (2.0 * s))).collect();
        let range = |k: usize| {
            let (lo, hi) = coords.iter().fold((f32::MAX, f32::MIN), |(lo, hi), c| (lo.min(c[k]), hi.max(c[k])));
            hi - lo
        };
        let mut order = [0usize, 1, 2];
        order.sort_by(|a, b| range(*b).total_cmp(&range(*a)));
        let (ku, kv, kw) = (order[0].min(order[1]), order[0].max(order[1]), order[2]);
        let uv: Vec<[f32; 2]> = coords
            .iter()
            .map(|c| {
                let (u, v, w) = (c[ku], c[kv], c[kw]);
                let r = (u * u + v * v).sqrt();
                let k = if r > 1e-6 { (u * u + v * v + w * w).sqrt() / r } else { 1.0 };
                [u * k + 0.5, v * k + 0.5]
            })
            .collect();
        let normals = vec![[0.0f32, 0.0, 1.0]; verts.len()];
        let mut mesh = Mesh::new(PrimitiveTopology::TriangleList, RenderAssetUsages::default());
        mesh.insert_attribute(Mesh::ATTRIBUTE_POSITION, verts.iter().map(|v| v.to_array()).collect::<Vec<_>>());
        mesh.insert_attribute(Mesh::ATTRIBUTE_NORMAL, normals);
        mesh.insert_attribute(Mesh::ATTRIBUTE_UV_0, uv);
        mesh.insert_indices(Indices::U32(idx));
        let mat = textures
            .entry(ev.texture.to_lowercase())
            .or_insert_with(|| {
                let tex = assets.load_builder().with_settings(|s: &mut ImageLoaderSettings| {
                    s.sampler = ImageSampler::Descriptor(ImageSamplerDescriptor {
                        address_mode_u: ImageAddressMode::ClampToEdge,
                        address_mode_v: ImageAddressMode::ClampToEdge,
                        ..ImageSamplerDescriptor::linear()
                    });
                }).load(format!("textures/{}.dds", ev.texture.to_lowercase()));
                materials.add(StandardMaterial {
                    base_color_texture: Some(tex),
                    unlit: true,
                    alpha_mode: AlphaMode::Blend,
                    double_sided: true,
                    cull_mode: None,
                    // coplanar with the surface: a bias (the original draws it with LESSEQUAL and no bias; with 4 the
                    // landscape won the depth test)
                    depth_bias: 1000.0,
                    ..default()
                })
            })
            .clone();
        let e = commands
            .spawn((
                Mesh3d(meshes.add(mesh)),
                MeshMaterial3d(mat),
                Transform::from_rotation(crate::level::game_to_engine()),
                bevy::light::NotShadowCaster,
            ))
            .id();
        decals.live.push_back(e);
        if decals.live.len() > POOL {
            if let Some(old) = decals.live.pop_front() {
                commands.entity(old).despawn();
            }
        }
    }
}
