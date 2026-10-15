//! Particles: the fx objects' particle emitters (opcodes 0x27 / 0x20..0x26 / 0x2b / 0x46..0x49) and the three
//! particle pools (Fx_LoadChunk 0x0041c7e0), simulated and drawn as camera-facing quads (Particles_Draw 0x0046d9f0,
//! Fx_DrawParticles 0x00423820). docs/subsystems/fx-vm.md section 4.
//!
//! The pure parts (emission along a segment, particle set-up, the simulation step, the 0x49 alpha ramp) take the
//! random generator explicitly so they draw in the original's order, and are unit-tested here.

use super::CrtRand;
use super::materials::ParticleMaterial;
use crate::level::game_to_engine;
use bevy::asset::RenderAssetUsages;
use bevy::mesh::{Indices, PrimitiveTopology};
use bevy::prelude::*;

/// Particle pools (Fx_LoadChunk 0x0041c7e0): capacity, bank texture and blend (pool 0 additive, state 0x100;
/// pools 1 and 2 alpha-blended, 0x310: Fx_DrawParticles 0x00423820). The remaster multiplies the capacities (the
/// original refuses particles when a pool is full; docs/engine-limits.md R5).
pub const POOLS: [(usize, &str, bool); 3] = [(2000, "n_fx_bank_1", true), (4000, "n_fx_bank_2", false), (2000, "n_fx_bank_2", false)];
pub const POOL_SCALE: usize = 16;
/// Particle gravity unit ([0x005bcd64]).
const PARTICLE_G: f32 = -64.348_1;

/// A particle emitter of an fx object (0x194 bytes in the original, 10 per object: FxObject_AllocParticleEmitter
/// 0x0041e6b0; set up by Fx_EmitParticle 0x0041e760 and the setters 0x0041e9f0..0x0041f3b0 on the current one,
/// FxObject_CurrentParticleEmitter 0x0041e730).
#[derive(Clone, Debug)]
pub struct Emitter {
    pub pool: usize,
    /// seconds between particles (e+8) and the spawn timer (e+0xc)
    pub interval: f32,
    pub timer: f32,
    /// ranges drawn per particle (defaults of 0x0041e760)
    pub size: (f32, f32),
    pub stretch: (f32, f32),
    pub growth: (f32, f32),
    pub drag: (f32, f32),
    pub speed: (f32, f32),
    pub life: (f32, f32),
    pub cone: (f32, f32),
    pub gravity: (f32, f32),
    /// texture rectangle u0, u1, v0, v1 in the pool's bank (0x2b, Fx_SetParticleUvs 0x0041f3b0)
    pub uv: [f32; 4],
    /// rgb 0..1 (0x2c, 0x0041f410; default 255): used without a 0x49 ramp
    pub colour: Vec3,
    /// cone axis (e+0x80, game space): the previous step's direction; fixed once 0x46 set it (flag 0x200)
    pub dir: Vec3,
    pub fixed_dir: bool,
    /// 0x47 (flag 0x400, Fx_SetParticleAxisRelative 0x0041f0f0): the rotation applied to the object's direction every step
    pub rel: Option<Mat3>,
    /// the object's position at the previous step (e+0x74)
    pub prev_pos: Vec3,
    /// 0x49 alpha ramp (Fx_SetParticleFadeInOut 0x0041eb70, ParticleColourTable_Alloc 0x0041eb30): 12 entries
    pub ramp: Option<[f32; 12]>,
}

impl Emitter {
    /// Fx_EmitParticle 0x0041e760 with flag 0x20: a new emitter following the object, its defaults.
    pub fn new(pool: usize, interval: f32, dir: Vec3, pos: Vec3) -> Self {
        Emitter {
            pool,
            interval: interval.max(1e-4),
            timer: 0.0,
            size: (0.2, 0.3),
            stretch: (1.0, 1.0),
            growth: (0.0, 0.0),
            drag: (1e-6, 1e-6),
            speed: (8.0, 30.0),
            life: (4.0, 7.0),
            cone: (36.0, 36.0),
            gravity: (0.0, 0.0),
            uv: [0.0, 1.0, 0.0, 1.0],
            colour: Vec3::ONE,
            dir,
            fixed_dir: false,
            rel: None,
            prev_pos: pos,
            ramp: None,
        }
    }
}

#[derive(Clone, Debug)]
pub struct Particle {
    pub pos: Vec3,
    pub prev: Vec3,
    pub vel: Vec3,
    pub life: f32,
    /// time carried into the first step (p+0xc): the particle's age at the end of the frame it was born in
    pub carried: f32,
    pub size: f32,
    pub growth: f32,
    pub drag: f32,
    pub stretch: f32,
    pub gravity: f32,
    pub uv: [f32; 4],
    pub colour: Vec3,
    pub ramp: Option<[f32; 12]>,
    pub ramp_pos: f32,
    pub ramp_rate: f32,
}

/// The three pools: live particles (the original's live lists), their draw meshes, the refusal count.
pub struct Pools {
    pub live: Vec<Vec<Particle>>,
    pub meshes: Vec<Handle<Mesh>>,
    pub dropped: usize,
}

impl Pools {
    pub fn capacity(pool: usize) -> usize {
        POOLS[pool].0 * POOL_SCALE
    }
    pub fn count(&self) -> usize {
        self.live.iter().map(Vec::len).sum()
    }
}

/// Spawns the pool meshes (one per pool, drawn with its bank texture and blend).
pub fn setup_pools(commands: &mut Commands, assets: &AssetServer, meshes: &mut Assets<Mesh>, materials: &mut Assets<ParticleMaterial>) -> Pools {
    let mut handles = Vec::new();
    for (_, tex, additive) in POOLS {
        let mut mesh = Mesh::new(PrimitiveTopology::TriangleList, RenderAssetUsages::default());
        mesh.insert_attribute(Mesh::ATTRIBUTE_POSITION, Vec::<[f32; 3]>::new());
        mesh.insert_attribute(Mesh::ATTRIBUTE_UV_0, Vec::<[f32; 2]>::new());
        mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR, Vec::<[f32; 4]>::new());
        mesh.insert_indices(Indices::U32(Vec::new()));
        let h = meshes.add(mesh);
        let mat = materials.add(ParticleMaterial { bank: assets.load(crate::graphics::texture_path(tex)), additive, params: Vec4::new(if additive { 1.0 } else { 2.2 }, 0.0, 0.0, 0.0) });
        commands.spawn((Mesh3d(h.clone()), MeshMaterial3d(mat), Transform::IDENTITY, bevy::camera::visibility::NoFrustumCulling, bevy::light::NotShadowCaster, Name::new("fx_pool")));
        handles.push(h);
    }
    Pools { live: POOLS.iter().map(|(n, _, _)| Vec::with_capacity(n * POOL_SCALE / 4)).collect(), meshes: handles, dropped: 0 }
}

/// One emitter step of an object (0x0041a050, mode 0x20: the emitter follows the object): the cone axis is the
/// object's direction (-column 1 of its matrix, 0x00453150; rotated by the 0x47 matrix when set) unless 0x46 fixed
/// it, and particles spawn along the object's movement this frame (ParticleEmitter_SpawnAlong 0x0046e650 via
/// FxSystem_SpawnInPool2 0x0041f4b0, the set-up callback 0x004193a0).
pub fn step_emitter(pools: &mut Pools, rng: &mut CrtRand, em: &mut Emitter, obj_pos: Vec3, obj_dir: Vec3, dt: f32) {
    let dir = if em.fixed_dir {
        em.dir
    } else {
        match em.rel {
            Some(m) => m * obj_dir,
            None => obj_dir,
        }
    };
    emit_along(pools, rng, em, em.prev_pos, obj_pos, em.dir, dir, dt);
    if !em.fixed_dir {
        em.dir = dir;
    }
    em.prev_pos = obj_pos;
}

/// ParticleEmitter_SpawnAlong 0x0046e650: timer -= dt; while it is negative, a particle at the point of the segment
/// pos0 -> pos1 (and the axis dir0 -> dir1) where it was due (t = the timer before / dt, growing by interval / dt),
/// carrying its age this frame (-timer - dt into the first step: Particles_Draw adds dt), then timer += interval.
/// A full pool (Particle_Alloc 0x0046d910 returns 0) skips the set-up, so no random numbers are drawn for it.
#[allow(clippy::too_many_arguments)]
pub fn emit_along(pools: &mut Pools, rng: &mut CrtRand, em: &mut Emitter, pos0: Vec3, pos1: Vec3, dir0: Vec3, dir1: Vec3, dt: f32) {
    if dt <= 0.0 {
        return;
    }
    let inv = 1.0 / dt;
    let step = em.interval * inv;
    let mut t = inv * em.timer;
    em.timer -= dt;
    if em.timer >= 0.0 {
        return;
    }
    let cap = Pools::capacity(em.pool);
    loop {
        let w = 1.0 - t;
        if pools.live[em.pool].len() < cap {
            let at = pos0 * w + pos1 * t;
            let axis = dir0 * w + dir1 * t;
            let p = init_particle(rng, em, at, axis, -em.timer - dt);
            pools.live[em.pool].push(p);
        } else {
            pools.dropped += 1;
        }
        t += step;
        em.timer += em.interval;
        if em.timer >= 0.0 {
            break;
        }
    }
}

/// ParticleEmitter_InitParticle 0x004193a0 (missing_02.cpp:93): life, then the velocity (Particle_RandomConeVelocity
/// 0x0046e320: a tilt in [0, cone] about a perpendicular of the axis, a spin in [0, 2 pi) about the axis, a speed),
/// the colour (a 0x49 ramp: position 10 falling to 0 at death; else the emitter colour, opaque), size, growth (a
/// shrink to 0 at death when the range is reversed, no random number), drag, stretch, gravity, uvs; then 0x0046e2f0
/// (life = min(life, ramp position / rate)). The axis is used as given (a lerp of two unit vectors: its length
/// scales the speed, as the original's unnormalised transform does).
pub fn init_particle(rng: &mut CrtRand, e: &Emitter, at: Vec3, axis: Vec3, carried: f32) -> Particle {
    let mut life = rng.r(e.life.0, e.life.1);
    let cone = rng.r(e.cone.0, e.cone.1).to_radians();
    let tilt = rng.r(0.0, cone);
    let spin = rng.r(0.0, std::f32::consts::TAU);
    let speed = rng.r(e.speed.0, e.speed.1);
    let len = axis.length();
    let n = if len > 1e-6 { axis / len } else { Vec3::Z };
    let perp = n.any_orthonormal_vector();
    let dir = Quat::from_axis_angle(n, spin) * (Quat::from_axis_angle(perp, tilt) * n);
    let size = rng.r(e.size.0, e.size.1);
    let growth = if e.growth.0 > e.growth.1 { -(size / life.max(1e-6)) } else { rng.r(e.growth.0, e.growth.1) };
    let drag = rng.r(e.drag.0, e.drag.1);
    let stretch = rng.r(e.stretch.0, e.stretch.1);
    let gravity = rng.r(e.gravity.0, e.gravity.1) * PARTICLE_G;
    let (ramp_pos, ramp_rate) = if e.ramp.is_some() { (10.0, 10.0 / life.max(1e-6)) } else { (1.0, 0.0) };
    if ramp_rate > 0.0 {
        life = life.min(ramp_pos / ramp_rate);
    }
    Particle { pos: at, prev: at, vel: dir * (speed * len), life, carried, size, growth, drag, stretch, gravity, uv: e.uv, colour: e.colour, ramp: e.ramp, ramp_pos, ramp_rate }
}

/// Particles_Draw 0x0046d9f0's update (render_r3.cpp:836): t = dt + the carried time; life -= t (released at <= 0:
/// Particle_Release 0x0046d9a0);
/// size += t * growth; ramp position -= t * rate; v -= v * drag * min(t, 1 / drag); vz += t * gravity; the position
/// moves by (v + v) * t / 2 with the new velocity. dt = 0 (the game paused) leaves everything as it is.
pub fn simulate(pools: &mut Pools, dt: f32) {
    if dt <= 0.0 {
        return;
    }
    for pool in &mut pools.live {
        pool.retain_mut(|p| {
            let t = dt + p.carried;
            p.carried = 0.0;
            p.life -= t;
            if p.life <= 0.0 {
                return false;
            }
            p.size += t * p.growth;
            p.ramp_pos -= t * p.ramp_rate;
            let k = t.min(1.0 / p.drag);
            p.vel -= p.vel * p.drag * k;
            p.vel.z += t * p.gravity;
            p.prev = p.pos;
            p.pos += p.vel * t;
            true
        });
    }
}

/// The 0x49 alpha ramp (Fx_SetParticleFadeInOut 0x0041eb70): 12 entries, index 10 at birth, 0 at death; entry
/// 9 - i holds the alpha at age i * 0.1 * T (T = the mean life): 0 before the fade-in start, rising to 1 by the
/// fade-in end, 1 until the fade-out start, falling to 0 by the shortest life.
pub fn build_ramp(in0: f32, in1: f32, out0: f32, life: (f32, f32)) -> [f32; 12] {
    let t_life = (life.0 + life.1) * 0.5;
    let step = 0.1 * t_life;
    let mut ramp = [0.0f32; 12];
    ramp[10] = if in1 == 0.0 { 1.0 } else { 0.0 };
    let (mut n_in, mut n_out) = (0.0f32, 0.0f32);
    for i in 0..10 {
        let t = i as f32 * step;
        let a = if t < in0 {
            0.0
        } else if t < in1 {
            n_in += 1.0;
            (n_in / ((in1 - in0) / step)).min(1.0)
        } else if t < out0 {
            1.0
        } else if t < life.0 {
            n_out += 1.0;
            (1.0 - n_out / ((life.0 - out0) / step)).max(0.0)
        } else {
            0.0
        };
        ramp[9 - i] = a;
    }
    ramp
}

/// A particle's alpha: the ramp lerped between floor(position) and the next entry; 1 without a ramp.
pub fn alpha_of(p: &Particle) -> f32 {
    match &p.ramp {
        None => 1.0,
        Some(r) => {
            let x = p.ramp_pos.clamp(0.0, 10.999);
            let j = x.floor() as usize;
            let fr = x - j as f32;
            r[j] + (r[(j + 1).min(11)] - r[j]) * fr
        }
    }
}

/// The quads of one pool (Particles_Draw 0x0046d9f0): centre = the position plus half the last move (the original's
/// `pos + (pos - prev) / 2`), corners +-right +-up of the camera times the size; with stretch, each corner moves along
/// the move projected on the view plane by c * stretch (c = the corner's direction along it, -sqrt 2 .. sqrt 2),
/// trailing corners fading by (1 + 0.7071 c). Engine space out.
pub fn build_quads(pool: &[Particle], right: Vec3, up: Vec3, out: &mut QuadBuffers) {
    let axes = game_to_engine();
    for p in pool {
        let motion = axes * (p.pos - p.prev);
        let centre = axes * p.pos + motion * 0.5;
        let d = Vec3::new(motion.dot(right), motion.dot(up), 0.0);
        let dl = d.length();
        let a = alpha_of(p);
        let base = out.pos.len() as u32;
        let s = p.size.max(0.0);
        let c = [p.colour.x, p.colour.y, p.colour.z];
        // corners (-r,-u) (-r,+u) (+r,+u) (+r,-u) with uv (u0,v0) (u0,v1) (u1,v1) (u1,v0)
        for (cx, cy, u, v) in [(-1.0f32, -1.0f32, p.uv[0], p.uv[2]), (-1.0, 1.0, p.uv[0], p.uv[3]), (1.0, 1.0, p.uv[1], p.uv[3]), (1.0, -1.0, p.uv[1], p.uv[2])] {
            let mut w = centre + right * cx * s + up * cy * s;
            let mut alpha = a;
            if p.stretch > 0.0 && dl > 1e-6 {
                let cdir = (cx * d.x + cy * d.y) / dl;
                w += (right * d.x + up * d.y) * cdir * p.stretch;
                if cdir < 0.0 {
                    alpha *= (1.0 + 0.7071 * cdir).max(0.0);
                }
            }
            out.pos.push(w.to_array());
            out.uv.push([u, v]);
            out.col.push([c[0], c[1], c[2], alpha]);
        }
        // counter-clockwise as seen from the camera (the material culls back faces)
        out.idx.extend_from_slice(&[base, base + 2, base + 1, base, base + 3, base + 2]);
    }
}

#[derive(Default)]
pub struct QuadBuffers {
    pub pos: Vec<[f32; 3]>,
    pub uv: Vec<[f32; 2]>,
    pub col: Vec<[f32; 4]>,
    pub idx: Vec<u32>,
}

/// Rebuilds the pool meshes from the live particles, facing the 3D camera.
pub fn draw_particles(vm: Res<super::FxVm>, mut meshes: ResMut<Assets<Mesh>>, cam: Query<&GlobalTransform, With<Camera3d>>) {
    let Ok(c) = cam.single() else { return };
    let (right, up) = (*c.right(), *c.up());
    for (pool, handle) in vm.pools.live.iter().zip(&vm.pools.meshes) {
        let Some(mut mesh) = meshes.get_mut(handle) else { continue };
        let mut q = QuadBuffers { pos: Vec::with_capacity(pool.len() * 4), uv: Vec::with_capacity(pool.len() * 4), col: Vec::with_capacity(pool.len() * 4), idx: Vec::with_capacity(pool.len() * 6) };
        build_quads(pool, right, up, &mut q);
        if q.pos.is_empty() {
            // an empty mesh is refused by the renderer: one zero-area triangle
            q.pos.extend([[0.0f32; 3]; 3]);
            q.uv.extend([[0.0f32; 2]; 3]);
            q.col.extend([[0.0f32; 4]; 3]);
            q.idx.extend([0, 1, 2]);
        }
        mesh.insert_attribute(Mesh::ATTRIBUTE_POSITION, q.pos);
        mesh.insert_attribute(Mesh::ATTRIBUTE_UV_0, q.uv);
        mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR, q.col);
        mesh.insert_indices(Indices::U32(q.idx));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn pools() -> Pools {
        Pools { live: vec![Vec::new(), Vec::new(), Vec::new()], meshes: Vec::new(), dropped: 0 }
    }

    #[test]
    fn emits_one_per_interval_with_birth_age() {
        let mut p = pools();
        let mut rng = CrtRand::seeded(1);
        let mut e = Emitter::new(1, 1.0 / 64.0, Vec3::Z, Vec3::ZERO);
        // one frame of 1/16 s: due at 0, 1/64, 2/64, 3/64 -> 4 particles
        emit_along(&mut p, &mut rng, &mut e, Vec3::ZERO, Vec3::new(4.0, 0.0, 0.0), Vec3::Z, Vec3::Z, 1.0 / 16.0);
        assert_eq!(p.live[1].len(), 4);
        // the first was due at the start of the frame: at pos0, aged the whole frame (carried 0, the step adds dt)
        let first = &p.live[1][0];
        assert!(first.pos.x.abs() < 1e-6 && first.carried.abs() < 1e-6, "{first:?}");
        // the last 1/64 s before the end, three quarters along
        let last = &p.live[1][3];
        assert!((last.pos.x - 3.0).abs() < 1e-5, "{}", last.pos.x);
        assert!((1.0 / 16.0 + last.carried - 1.0 / 64.0).abs() < 1e-6, "age {}", 1.0 / 16.0 + last.carried);
        // the timer ends at 0: the next particle is due at the start of the next frame
        emit_along(&mut p, &mut rng, &mut e, Vec3::ZERO, Vec3::ZERO, Vec3::Z, Vec3::Z, 1.0 / 128.0);
        assert_eq!(p.live[1].len(), 5);
        emit_along(&mut p, &mut rng, &mut e, Vec3::ZERO, Vec3::ZERO, Vec3::Z, Vec3::Z, 1.0 / 128.0);
        assert_eq!(p.live[1].len(), 5);
        emit_along(&mut p, &mut rng, &mut e, Vec3::ZERO, Vec3::ZERO, Vec3::Z, Vec3::Z, 1.0 / 128.0);
        assert_eq!(p.live[1].len(), 6);
    }

    #[test]
    fn full_pool_drops_without_drawing_random_numbers() {
        let mut p = pools();
        p.live[0] = vec![init_particle(&mut CrtRand::seeded(9), &Emitter::new(0, 1.0, Vec3::Z, Vec3::ZERO), Vec3::ZERO, Vec3::Z, 0.0); Pools::capacity(0)];
        let mut rng = CrtRand::seeded(1);
        let mut e = Emitter::new(0, 0.01, Vec3::Z, Vec3::ZERO);
        emit_along(&mut p, &mut rng, &mut e, Vec3::ZERO, Vec3::ZERO, Vec3::Z, Vec3::Z, 0.03);
        assert!(p.dropped >= 2);
        assert_eq!(rng.rand(), CrtRand::seeded(1).rand());
    }

    #[test]
    fn cone_speed_and_lifetime() {
        let mut rng = CrtRand::seeded(3);
        let mut e = Emitter::new(1, 0.01, Vec3::Z, Vec3::ZERO);
        e.cone = (10.0, 10.0);
        e.speed = (20.0, 30.0);
        e.life = (2.0, 2.0);
        for _ in 0..200 {
            let p = init_particle(&mut rng, &e, Vec3::ZERO, Vec3::Z, 0.0);
            let s = p.vel.length();
            assert!((20.0..30.0).contains(&s), "{s}");
            assert!(p.vel.normalize().dot(Vec3::Z) >= 10f32.to_radians().cos() - 1e-4);
        }
        // lives exactly its life (2 s) at 60 fps
        let mut pools = pools();
        pools.live[1].push(init_particle(&mut rng, &e, Vec3::ZERO, Vec3::Z, 0.0));
        let mut t = 0.0;
        while !pools.live[1].is_empty() {
            simulate(&mut pools, 1.0 / 60.0);
            t += 1.0 / 60.0;
        }
        assert!((t - 2.0f32).abs() < 1.5 / 60.0, "{t}");
    }

    #[test]
    fn gravity_drag_and_shrink() {
        let mut rng = CrtRand::seeded(5);
        let mut e = Emitter::new(1, 0.01, Vec3::Z, Vec3::ZERO);
        e.speed = (0.0, 0.0);
        e.gravity = (1.0, 1.0);
        e.growth = (1.0, 0.0); // reversed: shrinks to 0 at death
        e.life = (1.0, 1.0);
        e.size = (2.0, 2.0);
        let mut pools = pools();
        pools.live[1].push(init_particle(&mut rng, &e, Vec3::ZERO, Vec3::Z, 0.0));
        simulate(&mut pools, 0.5);
        let p = &pools.live[1][0];
        assert!((p.vel.z - 0.5 * PARTICLE_G).abs() < 1e-3);
        assert!((p.size - 1.0).abs() < 1e-4);
        // drag 2: v -= v * 2 * min(t, 0.5)
        let mut q = p.clone();
        q.vel = Vec3::X * 10.0;
        q.drag = 2.0;
        q.gravity = 0.0;
        pools.live[1] = vec![q];
        simulate(&mut pools, 0.25);
        assert!((pools.live[1][0].vel.x - 5.0).abs() < 1e-4);
    }

    #[test]
    fn ramp_fades_in_and_out() {
        // fx_keg_trail: in 0 .. 0.1, out from 0.2, life 1.5 .. 2
        let r = build_ramp(0.0, 0.1, 0.2, (1.5, 2.0));
        assert_eq!(r[11], 0.0);
        assert_eq!(r[10], 0.0);
        assert!(r[9] > 0.0 && r[9] <= 1.0);
        assert_eq!(r[0], 0.0);
        let mut p = init_particle(&mut CrtRand::seeded(1), &Emitter { ramp: Some(r), ..Emitter::new(1, 1.0, Vec3::Z, Vec3::ZERO) }, Vec3::ZERO, Vec3::Z, 0.0);
        p.ramp_pos = 10.0;
        assert_eq!(alpha_of(&p), r[10]);
        p.ramp_pos = 0.0;
        assert_eq!(alpha_of(&p), r[0]);
    }

    #[test]
    fn stretched_quad_trails_behind() {
        let mut p = init_particle(&mut CrtRand::seeded(1), &Emitter::new(0, 1.0, Vec3::Z, Vec3::ZERO), Vec3::ZERO, Vec3::Z, 0.0);
        p.size = 1.0;
        p.stretch = 1.0;
        p.prev = Vec3::ZERO;
        // a move along engine +x (game: x stays x)
        p.pos = game_to_engine().inverse() * Vec3::new(2.0, 0.0, 0.0);
        let mut q = QuadBuffers::default();
        build_quads(&[p], Vec3::X, Vec3::Y, &mut q);
        assert_eq!(q.pos.len(), 4);
        // trailing (-x) corners fade, leading ones keep alpha 1
        assert!(q.col[0][3] < 1.0 && q.col[2][3] == 1.0);
        // the leading corner reaches past the centre (3 = pos + half move) by size + the stretch
        assert!(q.pos[2][0] > 4.0, "{:?}", q.pos[2]);
    }
}
