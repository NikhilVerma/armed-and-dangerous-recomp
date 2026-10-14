//! The game's random numbers: MSVC CRT rand() (guess for 0x005391cd, docs/subsystems/fx-vm.md) and
//! Rand_FloatRange 0x0046e2c0. Shared by every system that must draw in the original's order (fx scripts, husk
//! picks, the mission's set-up, the grunts' parameters).

/// MSVC CRT rand(): a 32-bit LCG, 15-bit results. Seeded with 1 as the CRT is at start-up.
#[derive(Clone)]
pub struct CrtRand(pub(crate) u32);

impl Default for CrtRand {
    fn default() -> Self {
        CrtRand(0)
    }
}

impl CrtRand {
    pub fn seeded(seed: u32) -> Self {
        CrtRand(seed)
    }
    /// The next rand(): 0..=0x7fff.
    pub fn rand(&mut self) -> u32 {
        self.0 = self.0.wrapping_mul(214013).wrapping_add(2531011);
        (self.0 >> 16) & 0x7fff
    }
    /// Rand_FloatRange 0x0046e2c0: (b - a) * rand() / 32768 + a ([0x005bcbc0] = 1/32768), so in [a, b).
    pub fn r(&mut self, a: f32, b: f32) -> f32 {
        (b - a) * self.rand() as f32 * (1.0 / 32768.0) + a
    }
    /// An index into a list of `count` entries as the game picks one: (rand() * count) >> 15
    /// (FxNameList_PickRandom 0x0041cba0, opcodes 0x38..0x3a).
    pub fn index(&mut self, count: usize) -> usize {
        (self.rand() as usize * count) >> 15
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crt_sequence() {
        // MSVC rand() after srand(1): 41, 18467, 6334, 26500
        let mut r = CrtRand::seeded(1);
        assert_eq!([r.rand(), r.rand(), r.rand(), r.rand()], [41, 18467, 6334, 26500]);
    }

    #[test]
    fn float_range_and_index() {
        let mut r = CrtRand::seeded(1);
        let v = r.r(2.0, 4.0); // rand 41
        assert!((v - (2.0 + 2.0 * 41.0 / 32768.0)).abs() < 1e-6);
        let mut r = CrtRand::seeded(1);
        r.rand();
        assert_eq!(r.index(3), (18467 * 3) >> 15); // 1
        for _ in 0..1000 {
            assert!(r.index(5) < 5);
            let x = r.r(-1.0, 1.0);
            assert!((-1.0..1.0).contains(&x));
        }
    }
}
