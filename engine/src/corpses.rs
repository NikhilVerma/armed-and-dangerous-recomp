//! Dead characters' bodies: how long they lie, how they fade and how they go.
//!
//! The original (docs/subsystems/decals-bodies.md section 2): every dead character carries a corpse timer, time left
//! (+0x9f0, the game's member table names it m_deathTimer: tools/struct_atlas.py) and fade length (+0x9f4), counted down by Corpse_CountDown 0x0042eba0 from the generic AI update
//! 0x0043ee90. Within the last fade-length seconds the alpha is 1 - (fade - left) / fade (the set-alpha slot vt+0x100);
//! at 0 the character is removed (Object_Kill 0x00455640: vt+0xdc, then Object_SpawnHusk 0x00455660 plays its
//! fx_death and makes its husk).
//! - A ragdoll (Character_GoRagdoll 0x00431310, state 400): 5.0 s / 0.5 s from the moment it goes limp, so the body
//!   lies 4.5 s and fades over the last 0.5 s.
//! - An animated death (0x00427100 and the other death paths, state 0x191): 0.5 s / 0.5 s, counted only once the
//!   death anim has played out (the update's anim-done flag), so the body fades as soon as it has landed.
//! - At most four ragdolls at once: GoRagdoll counts the characters already down (state 400, 0x00431000) and makes the
//!   death animated when more than three are (`ragdoll_limit`): down = state 400 or +0x421 set (m_hkSimulateEnable:
//!   Havok simulating the body), among the characters whose vt+0x10c holds (meaning open). Only corpses count here:
//!   a tied peasant's ragdoll (peasant.rs) does not (guess).
//!
//! Measured on the original (work/oracle/courtyard, lev_1_m1): a ragdolled grunt leaves the AI list 4.96 s after its
//! death (state 400 at 3.72 s, last listed at 8.68 s); animated deaths 1.06..1.43 s after state 0x191 (the death anim
//! plus 0.5 s).
//!
//! Remaster (graphics.rs, the Remaster preset; the Original keeps the timings above exactly): bodies stay
//! `corpse_time` seconds and fade over `corpse_fade`, at most `corpse_keep` of them (the oldest starts fading when one
//! more lies down), and a ragdoll that has come to rest after the original's 5 s gives its physics bodies back
//! (`Settled`: its pose kept, no longer counted against `ragdoll_limit`).
//!
//! API: insert a [`Corpse`] on a dead character ([`Corpse::ragdoll`], [`Corpse::animated`], [`Corpse::lying`]); the
//! plugin counts it down, fades it ([`crate::camfade::ObjectAlpha`]) and removes it with everything it owns: its
//! entity tree, its Jolt ragdoll or body, plus the fx_death and husk of its [`Remains`]. [`Corpses::ragdolls`] says
//! how many ragdolls are down, for the ragdoll limit.

use crate::levelreset::LevelAppExt;
use crate::physics::{Body, Physics};
use crate::ragdoll::Ragdoll;
use bevy::prelude::*;

/// The original's timers (0x00431310 and 0x00427100): (time left, fade length) in seconds.
const RAGDOLL_TIMER: (f32, f32) = (5.0, 0.5);
const ANIMATED_TIMER: (f32, f32) = (0.5, 0.5);

/// A dead character's body on its way out.
#[derive(Component, Clone, Debug)]
pub struct Corpse {
    stage: Stage,
    /// +0x9f0 m_deathTimer: seconds until removal once counting
    left: f32,
    /// +0x9f4: the fade over the last seconds
    fade: f32,
    /// counted against the ragdoll limit while lying (state 400 in the original)
    ragdoll: bool,
    /// order of death (the keep cap fades the oldest first); set when the plugin first sees the corpse
    order: Option<u64>,
    /// seconds since it began counting
    age: f32,
    /// its timer is not the original's corpse timer (a body gone at once): the remaster's lifetime does not apply
    fixed: bool,
}

#[derive(Clone, Debug, PartialEq)]
enum Stage {
    /// the death anim of `slot` still playing (state 0x191 counts only once it has ended); the anim it plays
    Dying { slot: usize, anim: Option<String> },
    /// counting down
    Lying,
}

impl Corpse {
    /// A ragdoll's body (GoRagdoll 0x00431310: 5.0 s, the last 0.5 s fading), counting from now.
    pub fn ragdoll() -> Self {
        Self::counting(RAGDOLL_TIMER, true)
    }

    /// An animated death (0x00427100: 0.5 s, all of it fading) whose countdown starts when the anim on `slot` ends.
    pub fn animated(slot: usize) -> Self {
        Corpse { stage: Stage::Dying { slot, anim: None }, left: ANIMATED_TIMER.0, fade: ANIMATED_TIMER.1, ragdoll: false, order: None, age: 0.0, fixed: false }
    }

    /// A body that lies `total` seconds from now, the last `fade` of them fading; `ragdoll`: counted as a ragdoll down
    /// (a critter's GoRagdoll death drawn with anims).
    pub fn lying(total: f32, fade: f32, ragdoll: bool) -> Self {
        Self::counting((total, fade), ragdoll)
    }

    /// A body removed on the next update, whatever the settings (a grunt the Topsy Turvy bomb threw into the sky).
    pub fn gone() -> Self {
        Corpse { fixed: true, ..Self::counting((0.0, 0.0), false) }
    }

    fn counting((left, fade): (f32, f32), ragdoll: bool) -> Self {
        Corpse { stage: Stage::Lying, left, fade, ragdoll, order: None, age: 0.0, fixed: false }
    }

    /// Its timer is running (an animated death's anim has played out).
    pub fn is_lying(&self) -> bool {
        self.stage == Stage::Lying
    }

    /// Down as a ragdoll (0x00431000: state 400): counts against the ragdoll limit.
    pub fn is_ragdoll_down(&self) -> bool {
        self.ragdoll && self.stage == Stage::Lying
    }

    /// The body's alpha now (Corpse_CountDown: 1 - (fade - left) / fade within the fade).
    fn alpha(&self) -> f32 {
        if self.stage != Stage::Lying || self.left > self.fade || self.fade <= 0.0 {
            1.0
        } else {
            1.0 - (self.fade - self.left) / self.fade
        }
    }

    /// Starts the fade now if it has not started (the keep cap's oldest body).
    fn hurry(&mut self) {
        if self.stage == Stage::Lying {
            self.left = self.left.min(self.fade);
        }
    }

    fn fading(&self) -> bool {
        self.stage == Stage::Lying && self.left <= self.fade
    }
}

/// What a character leaves when its body goes (Object_SpawnHusk 0x00455660): its `fx_death` and `husk` properties
/// (Object_LoadBaseProps 0x00451360; the husk picked at load). No shipped character template sets either; a mod may.
#[derive(Component, Clone, Debug, Default)]
pub struct Remains {
    pub fx_death: Option<String>,
    pub husk: Option<String>,
}

/// A ragdoll at rest whose physics bodies went back (the Remaster's long-lying bodies): its bones keep their pose.
#[derive(Component)]
pub struct Settled;

/// The corpse rules in effect (graphics.rs `corpse_*` and `ragdoll_limit`).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CorpseRules {
    /// seconds a body lies once counting (0: the original's timers)
    pub time: f32,
    /// the fade (0: the original's)
    pub fade: f32,
    /// bodies lying at once (0: no cap, as the original)
    pub keep: u32,
    /// ragdolls down at once (the original: 4)
    pub ragdoll_limit: u32,
}

impl CorpseRules {
    pub fn of(s: &crate::graphics::GraphicsSettings) -> Self {
        CorpseRules { time: s.corpse_time.max(0.0), fade: s.corpse_fade.max(0.0), keep: s.corpse_keep, ragdoll_limit: s.ragdoll_limit }
    }

    /// The timer a corpse starts counting with: the original's, or the remaster's lifetime (never shorter).
    fn timer(&self, left: f32, fade: f32) -> (f32, f32) {
        let fade = if self.fade > 0.0 { self.fade } else { fade };
        let left = if self.time > 0.0 { self.time.max(left) } else { left };
        (left, fade.min(left))
    }

    /// Whether ragdolls settle (give their physics back) once at rest: only when bodies outlive the original's 5 s.
    fn settles(&self) -> bool {
        self.time > RAGDOLL_TIMER.0
    }
}

/// The level's bodies (a level resource): the death counter and the placed characters removed (savegame.rs keeps
/// them dead).
#[derive(Resource, Default)]
pub struct Corpses {
    next_order: u64,
    /// instance ids of placed characters whose bodies were removed
    pub removed_instances: Vec<u32>,
    /// bodies removed this level (playtest state)
    pub removed: u32,
}

impl Corpses {
    /// Ragdolls down now (the original's 0x00431000 count over the AI list).
    pub fn ragdolls<'a>(corpses: impl IntoIterator<Item = &'a Corpse>) -> u32 {
        corpses.into_iter().filter(|c| c.is_ragdoll_down()).count() as u32
    }

    /// Whether one more character may go ragdoll (GoRagdoll 0x00431310: `down <= limit - 1`).
    pub fn may_ragdoll(down: u32, rules: &CorpseRules) -> bool {
        down < rules.ragdoll_limit
    }
}

/// Seconds a ragdoll may keep moving after the original's lifetime before it settles anyway (a body rocking on a
/// slope; Remaster only).
const SETTLE_MAX: f32 = 15.0;

pub struct CorpsePlugin;

impl Plugin for CorpsePlugin {
    fn build(&self, app: &mut App) {
        // the removal in Last: every command the frame's systems queued for the body has been applied by then
        app.init_level_resource::<Corpses>()
            .add_systems(Update, (count_down, keep_cap).chain().in_set(CorpseSet))
            .add_systems(Last, (remove, log.run_if(resource_exists::<crate::playtest::Playtest>)).chain());
    }
}

/// The corpse countdown and the keep cap.
#[derive(SystemSet, Clone, Debug, PartialEq, Eq, Hash)]
pub struct CorpseSet;

/// Corpse_CountDown 0x0042eba0 for every body: an animated death waits for its anim's end, then the timer runs; the
/// alpha follows it.
#[allow(clippy::type_complexity)]
fn count_down(
    mut commands: Commands,
    time: Res<Time>,
    settings: Res<crate::graphics::GraphicsSettings>,
    lib: Res<crate::skin::AnimLibrary>,
    mut physics: NonSendMut<Physics>,
    mut corpses: ResMut<Corpses>,
    mut q: Query<(Entity, &mut Corpse, Option<&crate::skin::Animator>, Option<&Ragdoll>, Option<&crate::camfade::ObjectAlpha>)>,
) {
    let rules = CorpseRules::of(&settings);
    let dt = time.delta_secs();
    for (e, mut c, animator, ragdoll, alpha) in &mut q {
        if c.order.is_none() {
            c.order = Some(corpses.next_order);
            corpses.next_order += 1;
            if c.stage == Stage::Lying && !c.fixed {
                (c.left, c.fade) = rules.timer(c.left, c.fade);
            }
        }
        if let Stage::Dying { slot, anim } = &mut c.stage {
            // the anim the death plays: the first one seen on its slot (or a later change: the death anim is chosen
            // by the anim system a frame after the death)
            let Some(a) = animator else {
                c.stage = Stage::Lying;
                continue;
            };
            let now = a.layers.get(*slot).map(|l| l.anim.clone());
            if *anim != now {
                *anim = now;
                continue;
            }
            if !a.finished(*slot, &lib) {
                continue;
            }
            c.stage = Stage::Lying;
            (c.left, c.fade) = rules.timer(c.left, c.fade);
        }
        c.age += dt;
        c.left = (c.left - dt).max(0.0);
        let a = c.alpha();
        if a < 1.0 && alpha.is_none_or(|o| o.0 != a) {
            commands.entity(e).try_insert(crate::camfade::ObjectAlpha(a));
        }
        // the Remaster's long-lying ragdoll gives its bodies back once at rest (or after SETTLE_MAX)
        if let Some(rd) = ragdoll.filter(|_| rules.settles() && c.ragdoll && c.age >= RAGDOLL_TIMER.0) {
            let calm = physics.world.ragdoll(rd.id).map_or(true, |r| !r.is_active() || r.is_calm(0.05, 0.1));
            if calm || c.age >= RAGDOLL_TIMER.0 + SETTLE_MAX {
                let _ = physics.world.remove_ragdoll(rd.id);
                c.ragdoll = false;
                commands.entity(e).try_remove::<Ragdoll>().try_insert(Settled);
            }
        }
    }
}

/// The Remaster's cap: with more than `corpse_keep` bodies lying (not yet fading), the oldest start fading.
fn keep_cap(settings: Res<crate::graphics::GraphicsSettings>, mut q: Query<&mut Corpse>) {
    let keep = CorpseRules::of(&settings).keep as usize;
    if keep == 0 {
        return;
    }
    let mut lying: Vec<(u64, Mut<Corpse>)> = q.iter_mut().filter(|c| c.stage == Stage::Lying && !c.fading()).map(|c| (c.order.unwrap_or(0), c)).collect();
    if lying.len() <= keep {
        return;
    }
    lying.sort_by_key(|(o, _)| *o);
    let over = lying.len() - keep;
    for (_, c) in lying.iter_mut().take(over) {
        c.hurry();
    }
}

/// Object_Kill 0x00455640 for bodies whose time ran out: the ragdoll or body leaves the physics world, the fx_death
/// plays and the husk appears where the body was, and the entity goes with everything under it.
#[allow(clippy::type_complexity)]
fn remove(
    mut commands: Commands,
    mut physics: NonSendMut<Physics>,
    mut corpses: ResMut<Corpses>,
    q: Query<(Entity, &Corpse, &GlobalTransform, Option<&Ragdoll>, Option<&Body>, Option<&Remains>, Option<&crate::level::InstanceId>)>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut husks: MessageWriter<crate::level::SpawnHusk>,
) {
    for (e, c, t, ragdoll, body, remains, id) in &q {
        if c.stage != Stage::Lying || c.left > 0.0 {
            continue;
        }
        if let Some(rd) = ragdoll {
            let _ = physics.world.remove_ragdoll(rd.id);
        }
        if let Some(b) = body {
            physics.remove(b.id);
        }
        if let Some(r) = remains {
            let (_, rotation, at) = t.to_scale_rotation_translation();
            if let Some(name) = &r.fx_death {
                fx.write(crate::fx::StartFx { name: name.clone(), at, rotation, follow: None, position_only: false });
            }
            if let Some(h) = &r.husk {
                husks.write(crate::level::SpawnHusk { template: h.clone(), world: t.to_matrix() });
            }
        }
        if let Some(i) = id {
            corpses.removed_instances.push(i.0);
        }
        corpses.removed += 1;
        commands.entity(e).try_despawn();
    }
}

/// The playtest state: "corpses" {"lying": bodies now, "ragdolls": ragdolls down, "settled", "removed": this level}.
fn log(q: Query<(&Corpse, Has<Settled>)>, corpses: Res<Corpses>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    let ragdolls = Corpses::ragdolls(q.iter().map(|(c, _)| c));
    let settled = q.iter().filter(|(_, s)| *s).count();
    state.0.insert("corpses".into(), serde_json::json!({"lying": q.iter().count(), "ragdolls": ragdolls, "settled": settled, "removed": corpses.removed}));
}

#[cfg(test)]
mod tests {
    use super::*;

    const ORIGINAL: CorpseRules = CorpseRules { time: 0.0, fade: 0.0, keep: 0, ragdoll_limit: 4 };

    /// Corpse_CountDown 0x0042eba0 over a ragdoll's 5.0 / 0.5: opaque for 4.5 s, then 1 - (fade - left) / fade.
    #[test]
    fn ragdoll_timer_and_fade() {
        let mut c = Corpse::ragdoll();
        (c.left, c.fade) = ORIGINAL.timer(c.left, c.fade);
        assert_eq!((c.left, c.fade), (5.0, 0.5));
        c.left = 4.6;
        assert_eq!(c.alpha(), 1.0);
        c.left = 0.25;
        assert!((c.alpha() - 0.5).abs() < 1e-6);
        c.left = 0.0;
        assert_eq!(c.alpha(), 0.0);
    }

    /// An animated death's 0.5 / 0.5 fades from the first counted frame; it does not count while its anim plays.
    #[test]
    fn animated_waits_and_fades_at_once() {
        let mut c = Corpse::animated(0);
        assert_eq!(c.alpha(), 1.0, "still dying");
        assert!(!c.is_ragdoll_down());
        c.stage = Stage::Lying;
        c.left = 0.4;
        assert!(c.alpha() < 1.0);
    }

    /// The remaster's lifetime and fade replace the original's, never shortening them; a ragdoll settles only then.
    #[test]
    fn remaster_rules() {
        let r = CorpseRules { time: 60.0, fade: 1.5, keep: 48, ragdoll_limit: 16 };
        assert_eq!(r.timer(5.0, 0.5), (60.0, 1.5));
        assert_eq!(r.timer(0.5, 0.5), (60.0, 1.5));
        assert!(r.settles() && !ORIGINAL.settles());
        assert_eq!(CorpseRules { time: 2.0, ..r }.timer(5.0, 0.5), (5.0, 1.5));
    }

    /// GoRagdoll 0x00431310: `down <= 3` with the original's four.
    #[test]
    fn ragdoll_limit() {
        assert!(Corpses::may_ragdoll(3, &ORIGINAL));
        assert!(!Corpses::may_ragdoll(4, &ORIGINAL));
        let down = [Corpse::ragdoll(), Corpse::ragdoll(), Corpse::animated(0), Corpse::lying(1.0, 0.5, false)];
        assert_eq!(Corpses::ragdolls(&down), 2);
    }

    /// The keep cap hurries the oldest: its left drops to the fade.
    #[test]
    fn hurry_starts_the_fade() {
        let mut c = Corpse::lying(60.0, 1.5, false);
        c.hurry();
        assert_eq!(c.left, 1.5);
        assert!(c.fading());
    }
}
