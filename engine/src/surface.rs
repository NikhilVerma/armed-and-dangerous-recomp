//! Surface materials: what a foot stands on or a shot hits, for the material variants of the fx that go with it
//! (footsteps, slides, impacts, debris landing).
//!
//! The original: every physics mesh part carries a Havok material named after the skin material it was built from
//! (the mesh's material-name dictionary +0x70, a material record's +0x14 id; MeshPart_LoadMaterial 0x004122f0 keeps the
//! name in material +0x10). A ground contact (0x00454940 / 0x00454b20, an object's vt+0xb8) or a hit hands that name to
//! Fx_Create 0x0041cc60 with flag 0x800, which tries `<fx>_<token>` for each "_" token of the name in turn (strtok with
//! "_", 0x005b9300), then `<fx>_<the object's "material" property>` (0x004532e0), then the plain fx. So
//! "Water_shallow_path_streambed" gives fx_fs_roman_run_shallow, "Grass_path" _grass, "snow_nonpath_01" _snow, and
//! "Rock_ground" (lev_2_m3) the plain fx_fs_roman_run, whose steps are the dirt ones.
//!
//! Here: a static mesh body keeps its triangles' surface names ([`SurfaceMap`], from the model's submeshes: the
//! export's "surface", tools/remaster_export.py); [`crate::physics::Physics::surface`] names the [`Surface`] at a hit
//! point; [`Surface::variant`] picks the fx; [`StartSurfaceFx`] starts an fx with its variant resolved against the
//! level's fx bank.

use bevy::prelude::*;
use std::collections::HashMap;
use std::ops::Range;

/// What a point of a body is made of: the mesh surface's name there (a static mesh's material) and the object's own
/// "material" property (placed objects; "grunt" for characters).
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Surface {
    pub mesh: Option<String>,
    pub property: Option<String>,
}

impl Surface {
    /// An object's property only (a character, an object without a mesh surface).
    pub fn of_property(property: &str) -> Surface {
        Surface { mesh: None, property: Some(property.to_owned()) }
    }

    /// The fx Fx_Create picks with flag 0x800: `<base>_<token>` for the first "_" token of the mesh surface's name
    /// with such an fx, else `<base>_<property>` when that fx exists, else `base`. `exists` asks the fx bank (names are
    /// compared in lower case: the bank's keys are; the original's lookup case is a guess).
    pub fn variant(&self, base: &str, exists: impl Fn(&str) -> bool) -> String {
        let base = base.to_lowercase();
        let tokens = self.mesh.iter().flat_map(|m| m.split('_')).filter(|t| !t.is_empty());
        for t in tokens.chain(self.property.as_deref()) {
            let name = format!("{base}_{}", t.to_lowercase());
            if exists(&name) {
                return name;
            }
        }
        base
    }
}

/// A static mesh's surfaces: each triangle's surface name, found again from a point on it.
pub struct SurfaceMap {
    names: Vec<String>,
    /// the triangles (engine space) with their name's index; empty when every triangle has the same name
    tris: Vec<([Vec3; 3], u16)>,
    /// triangles by grid cell (x, z) of `CELL` units
    grid: HashMap<(i32, i32), Vec<u32>>,
}

/// The map's grid cell size (game units).
const CELL: f32 = 64.0;
/// Triangles this much farther than the nearest still count as touching the point (game units).
const TIE: f32 = 0.05;

fn cell(v: f32) -> i32 {
    (v / CELL).floor() as i32
}

impl SurfaceMap {
    /// From a mesh's vertices (engine space), triangles and named triangle ranges; None without names.
    pub fn new(vertices: &[Vec3], triangles: &[[u32; 3]], named: &[(Range<u32>, String)]) -> Option<SurfaceMap> {
        let mut names: Vec<String> = Vec::new();
        for (_, n) in named {
            if !names.contains(n) {
                names.push(n.clone());
            }
        }
        if names.is_empty() {
            return None;
        }
        let mut map = SurfaceMap { names, tris: Vec::new(), grid: HashMap::new() };
        if map.names.len() == 1 {
            return Some(map);
        }
        for (range, n) in named {
            let k = map.names.iter().position(|x| x == n).unwrap_or(0) as u16;
            for t in triangles.get(range.start as usize..range.end as usize).unwrap_or(&[]) {
                let Some(p) = t.iter().map(|&i| vertices.get(i as usize).copied()).collect::<Option<Vec<Vec3>>>() else { continue };
                let i = map.tris.len() as u32;
                map.tris.push(([p[0], p[1], p[2]], k));
                let (lo, hi) = (p[0].min(p[1]).min(p[2]), p[0].max(p[1]).max(p[2]));
                for x in cell(lo.x)..=cell(hi.x) {
                    for z in cell(lo.z)..=cell(hi.z) {
                        map.grid.entry((x, z)).or_default().push(i);
                    }
                }
            }
        }
        Some(map)
    }

    /// The surface name at a point on (or next to) the mesh: the nearest triangle's in the point's grid cell; of
    /// triangles as near (within `TIE` units: a rail lying on the planks of lev_5_m3's bridge touches them where
    /// Roman walks) the most level one, the floor a foot stands on (the meshes' windings vary, so up or down alike).
    pub fn at(&self, p: Vec3) -> Option<&str> {
        if self.tris.is_empty() {
            return self.names.first().map(String::as_str);
        }
        let cands = self.grid.get(&(cell(p.x), cell(p.z)))?;
        let near: Vec<(f32, f32, u16)> = cands
            .iter()
            .map(|&i| &self.tris[i as usize])
            .map(|(t, k)| (closest_on_triangle(p, t).distance(p), (t[1] - t[0]).cross(t[2] - t[0]).normalize_or_zero().y.abs(), *k))
            // (a degenerate triangle, a sliver of the exported strips, gives no distance)
            .filter(|n| n.0.is_finite())
            .collect();
        let best = near.iter().map(|n| n.0).min_by(f32::total_cmp)?;
        let (_, _, k) = near.iter().filter(|n| n.0 <= best + TIE).max_by(|a, b| a.1.total_cmp(&b.1))?;
        self.names.get(*k as usize).map(String::as_str)
    }
}

/// The point of a triangle nearest to `p` (Ericson, Real-Time Collision Detection 5.1.5).
fn closest_on_triangle(p: Vec3, [a, b, c]: &[Vec3; 3]) -> Vec3 {
    let (ab, ac, ap) = (*b - *a, *c - *a, p - *a);
    let (d1, d2) = (ab.dot(ap), ac.dot(ap));
    if d1 <= 0.0 && d2 <= 0.0 {
        return *a;
    }
    let bp = p - *b;
    let (d3, d4) = (ab.dot(bp), ac.dot(bp));
    if d3 >= 0.0 && d4 <= d3 {
        return *b;
    }
    let vc = d1 * d4 - d3 * d2;
    if vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0 {
        return *a + ab * (d1 / (d1 - d3));
    }
    let cp = p - *c;
    let (d5, d6) = (ab.dot(cp), ac.dot(cp));
    if d6 >= 0.0 && d5 <= d6 {
        return *c;
    }
    let vb = d5 * d2 - d1 * d6;
    if vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0 {
        return *a + ac * (d2 / (d2 - d6));
    }
    let va = d3 * d6 - d5 * d4;
    if va <= 0.0 && d4 - d3 >= 0.0 && d5 - d6 >= 0.0 {
        return *b + (*c - *b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    }
    let denom = 1.0 / (va + vb + vc);
    *a + ab * (vb * denom) + ac * (vc * denom)
}

/// Starts `fx` with its material variant for `surface` (Fx_Create flag 0x800): `fx.name` is the base name.
#[derive(Message, Clone)]
pub struct StartSurfaceFx {
    pub fx: crate::fx::StartFx,
    pub surface: Surface,
}

pub struct SurfacePlugin;

impl Plugin for SurfacePlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<StartSurfaceFx>().add_systems(Update, resolve.before(crate::fxvm::FxVmSet));
    }
}

/// The variants resolved against the level's fx bank, started as plain fx.
fn resolve(mut ev: MessageReader<StartSurfaceFx>, data: Option<Res<crate::gamedata::GameData>>, mut fx: MessageWriter<crate::fx::StartFx>) {
    let Some(data) = data else {
        ev.clear();
        return;
    };
    for e in ev.read() {
        let name = e.surface.variant(&e.fx.name, |n| data.fx.contains_key(n));
        fx.write(crate::fx::StartFx { name, ..e.fx.clone() });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn variants_by_token_then_property_then_base() {
        let have = |n: &str| ["fx_fs_roman_run_shallow", "fx_fs_roman_run_grass", "fx_fs_roman_run_snow", "fx_hit_metal"].contains(&n);
        let s = |m: &str| Surface { mesh: Some(m.into()), property: None };
        assert_eq!(s("Water_shallow_path_streambed").variant("fx_fs_roman_run", have), "fx_fs_roman_run_shallow");
        assert_eq!(s("Grass_path").variant("FX_fs_roman_run", have), "fx_fs_roman_run_grass");
        assert_eq!(s("Rock_ground").variant("fx_fs_roman_run", have), "fx_fs_roman_run");
        assert_eq!(Surface { mesh: Some("Rock_ground".into()), property: Some("metal".into()) }.variant("fx_hit", have), "fx_hit_metal");
        assert_eq!(Surface::default().variant("fx_fs_roman_run", have), "fx_fs_roman_run");
    }

    #[test]
    fn map_finds_the_triangle_under_a_point() {
        // two ground triangles side by side (engine space: y up), "snow" left of x = 10, "ice" right
        let v = [Vec3::new(0.0, 0.0, 0.0), Vec3::new(10.0, 0.0, 0.0), Vec3::new(0.0, 0.0, 10.0), Vec3::new(20.0, 0.0, 0.0), Vec3::new(10.0, 0.0, 10.0)];
        let t = [[0, 1, 2], [1, 3, 4]];
        let m = SurfaceMap::new(&v, &t, &[(0..1, "snow_nonpath_01".into()), (1..2, "ice".into())]).unwrap();
        assert_eq!(m.at(Vec3::new(2.0, 0.01, 2.0)), Some("snow_nonpath_01"));
        assert_eq!(m.at(Vec3::new(15.0, 0.0, 2.0)), Some("ice"));
        // a degenerate triangle touching the point (zero area) does not take it
        let v2 = [v[0], v[1], v[2], v[3], v[4], Vec3::new(2.0, 0.0, 2.0)];
        let m2 = SurfaceMap::new(&v2, &[[0, 1, 2], [5, 5, 5]], &[(0..1, "Wood_path_planks".into()), (1..2, "Metal_path_rails".into())]).unwrap();
        assert_eq!(m2.at(Vec3::new(2.0, 0.0, 2.0)), Some("Wood_path_planks"));
        let one = SurfaceMap::new(&v, &t, &[(0..2, "Grass_path".into())]).unwrap();
        assert_eq!(one.at(Vec3::new(500.0, 0.0, 0.0)), Some("Grass_path"));
    }
}
