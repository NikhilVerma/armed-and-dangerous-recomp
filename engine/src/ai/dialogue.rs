//! Banter: one line of a table said by whoever can say it now. The original picks with Audio_PlayRandomSound
//! 0x00493b70 (a shuffle bag over the table, entries swapped in place, with a "used" count kept per table) and asks a
//! speaker test of the table's caller for each pick until one is accepted:
//!
//! - the kill meter's dismay (MissionObject_Update 0x0044d150, table 0x005ce570, test 0x0044da10, ai.rs `dismay`);
//! - the line after a peasant is thrown out of a building (game_01.cpp:1989, table 0x005fb6d0, test 0x0040d250: the
//!   buildings' toss_peasants, tossed.rs `toss`);
//! - the idle banter (0x0047b730, table 0x005ce350, test 0x0047b520, player.rs `idle_banter`).
//!
//! A test says who speaks: Roman's lines play without a position (audio +0xd0), a companion's on him (+0xcc) and
//! become his current line (+0xb50, kept so the next test sees him talking); an idle "_Spec" line plays without a
//! position too (its flag a3). A test that refuses every line would make the original pick for ever; here the pick
//! gives up after a few rounds of the table (the callers only ask when someone can speak).

use bevy::prelude::*;

/// The kill meter's dismay lines (table 0x005ce570, 16 entries, read from the exe).
pub const DISMAY: [&str; 16] = [
    "ROM50039_Dismay", "ROM50040_Dismay", "ROM50062_Dismay", "ROM50174_Dismay", "ROM50202_Dismay", "ROM50210_Dismay",
    "ROM50045_Ambient", "JON50023_Dismay", "JON50048_Dismay", "JON50049_Dismay", "JON50117_Dismay", "JON50118_Dismay",
    "JON50119_Dismay", "JON50174_Dismay", "Q1150168_Dismay", "Q1150115_Dismay",
];

/// The lines after a peasant is thrown out of a building (table 0x005fb6d0, 15 entries, read from the exe).
pub const TOSSED: [&str; 15] = [
    "JON50172_Dismay", "JON50173_Dismay", "JON50175_Dismay", "JON50176_Dismay", "ROM50037_Dismay", "ROM50038_Dismay",
    "ROM50207_Dismay", "Q1150111_Dismay", "Q1150112_Dismay", "Q1150113_Dismay", "Q1150115_Dismay", "Q1150162_Dismay",
    "Q1150163_Dismay", "Q1150165_Dismay", "Q1150167_Dismay",
];

/// The idle lines (table 0x005ce350, 55 entries, read from the exe): Roman's, Q's and Jonesy's ambient / idle lines.
pub const IDLE: [&str; 55] = [
    "rom50044_Ambient", "rom50047_Ambient", "rom50048_Ambient", "rom50049_Ambient", "rom50050_Ambient", "rom50171_Ambient",
    "rom50178_BaseDefendAmb", "rom50187_BaseDefendAmb", "rom50189_BaseDefendAmb", "rom50190_BaseDefendAmb", "rom50191_BaseDefendAmb",
    "q1150029_Idle", "q1150030_Idle", "q1150027_Ambient", "q1150031_Ambient", "q1150033_Ambient", "q1150034_Ambient",
    "q1150025_Ambient", "q1150035_Ambient", "q1150036_Ambient", "q1150037_Ambient", "q1150041_Ambient", "q1150048_Ambient",
    "q1150049_Ambient", "q1154016_Ambient_Spec", "q1150175_Ambient_Spec", "q1150177_Ambient_Spec", "q1154004_Ambient_Spec",
    "jon50034_Idle", "jon50035_Idle", "jon50036_Idle", "jon50040_Idle", "jon50043_Idle", "jon50027_Ambient", "jon50030_Ambient",
    "jon50031_Ambient", "jon50032_Ambient", "jon50041_Ambient", "jon50046_Ambient", "jon50050_Ambient", "jon50051_Ambient",
    "jon50052_Ambient", "jon50053_Ambient", "jon50054_Ambient", "jon50055_Ambient", "jon50164_Ambient", "jon50182_Ambient",
    "jon50183_Ambient", "jon54001_Ambient_Spec", "jon54012_Ambient_Spec", "jon54014_Ambient_Spec", "jon54017_Ambient_Spec",
    "jon54019_Ambient_Spec", "jon50162_Ambient_Spec", "jon54005_Ambient_Spec",
];

/// Who says a line.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Speaker {
    Roman,
    Jonesy,
    Q,
}

/// A line a test accepted: who says it and whether it plays at the speaker (else without a position).
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Said {
    pub line: &'static str,
    pub speaker: Speaker,
    pub positional: bool,
}

/// A companion as the speaker tests see him (0x00475d30 Jonesy / 0x00475d40 Q of the player, alive).
#[derive(Clone, Copy, Debug)]
pub struct Present {
    /// his current line (+0xb50) still plays (audio +0xdc)
    pub speaking: bool,
    /// seconds since he last moved or fired (now - Game+0xb0c / +0xafc)
    pub still: f32,
}

/// What the speaker tests ask about the cast.
#[derive(Clone, Copy, Debug, Default)]
pub struct Cast {
    pub roman_alive: bool,
    /// Roman's idle time (+0xe5c)
    pub roman_idle: f32,
    pub jonesy: Option<Present>,
    pub q: Option<Present>,
    /// a base-defence level (0x0044e600: the player in the base turret)
    pub base_defence: bool,
    /// the level's world number (Game+0x2708: lev_5_* is 5)
    pub world: u32,
}

/// The idle banter's limit (0x005bccbc): 30 s still (or Roman 30 s idle).
pub const IDLE_AFTER: f32 = 30.0;

/// The level's world number (Game+0x2708) from its folder's name: lev_N_* is N, the bonus levels 0.
pub fn world_of(dir: &std::path::Path) -> u32 {
    let name = dir.file_name().and_then(|n| n.to_str()).unwrap_or("");
    name.strip_prefix("lev_").and_then(|r| r.split('_').next()).and_then(|n| n.parse().ok()).unwrap_or(0)
}

/// The kill meter's speaker test 0x0044da10: an 'R' line is Roman's; a 'J' line needs Jonesy there and not talking,
/// any other line Q the same; the companions say theirs at themselves.
pub fn dismay_speaker(line: &'static str, cast: &Cast) -> Option<Said> {
    companion_or_roman(line, cast)
}

/// The tossed peasant's speaker test 0x0040d250: as the dismay's, and JON50172_Dismay / Q1150162_Dismay only while the
/// level's list +0xf04 is not empty (`listed`: which list that is was not traced).
pub fn tossed_speaker(line: &'static str, cast: &Cast, listed: bool) -> Option<Said> {
    let said = companion_or_roman(line, cast)?;
    let exclusive = match said.speaker {
        Speaker::Roman => false,
        Speaker::Jonesy => line.eq_ignore_ascii_case("JON50172_Dismay"),
        Speaker::Q => line.eq_ignore_ascii_case("Q1150162_Dismay"),
    };
    (!exclusive || listed).then_some(said)
}

fn companion_or_roman(line: &'static str, cast: &Cast) -> Option<Said> {
    match line.as_bytes().first() {
        Some(b'R') => Some(Said { line, speaker: Speaker::Roman, positional: false }),
        Some(b'J') => cast.jonesy.filter(|p| !p.speaking).map(|_| Said { line, speaker: Speaker::Jonesy, positional: true }),
        _ => cast.q.filter(|p| !p.speaking).map(|_| Said { line, speaker: Speaker::Q, positional: true }),
    }
}

/// The idle banter's speaker test 0x0047b520 (disassembled: no recovered source):
/// - a base-defence level takes only the "_BaseDefend" lines (Roman's, without a position); elsewhere never those;
/// - 'r' (Roman): only when he has been idle more than 30 s; rom50171_ (it names both) needs Q and Jonesy there;
/// - 'q': only when Q has been still more than 30 s; a "_Spec" line also needs Roman alive and Jonesy there, and plays
///   without a position; on world 5 not q1150025_Ambient nor q1150035_Ambient;
/// - 'j': only when Jonesy has been still more than 30 s; a "_Spec" line and jon50182_Ambient also need Roman alive and Q
///   there; a "_Spec" line plays without a position.
pub fn idle_speaker(line: &'static str, cast: &Cast) -> Option<Said> {
    let base_line = line.contains("_BaseDefend");
    if cast.base_defence {
        return base_line.then_some(Said { line, speaker: Speaker::Roman, positional: false });
    }
    if base_line {
        return None;
    }
    let spec = line.contains("_Spec");
    match line.as_bytes().first() {
        Some(b'r') => {
            if !(cast.roman_idle > IDLE_AFTER) {
                return None;
            }
            (!line.contains("rom50171_") || (cast.q.is_some() && cast.jonesy.is_some())).then_some(Said { line, speaker: Speaker::Roman, positional: false })
        }
        Some(b'q') => {
            let q = cast.q.filter(|q| q.still > IDLE_AFTER)?;
            let _ = q;
            if spec {
                (cast.roman_alive && cast.jonesy.is_some()).then_some(())?;
            } else if cast.world == 5 && (line.eq_ignore_ascii_case("q1150025_Ambient") || line.eq_ignore_ascii_case("q1150035_Ambient")) {
                return None;
            }
            Some(Said { line, speaker: Speaker::Q, positional: !spec })
        }
        _ => {
            cast.jonesy.filter(|j| j.still > IDLE_AFTER)?;
            if spec || line.eq_ignore_ascii_case("jon50182_Ambient") {
                (cast.roman_alive && cast.q.is_some()).then_some(())?;
            }
            Some(Said { line, speaker: Speaker::Jonesy, positional: !spec })
        }
    }
}

/// One table's shuffle bag (0x00493b70 with its "used" count, 0x0063a18c / 0x0063a1e0 / 0x0063a184): each pick first
/// swaps the entry last drawn back from the front (the one `used` from the end), then up to three times (half the
/// unused entries, at most 3) swaps a random unused entry to the front, and draws the front. When every entry has been
/// drawn the count starts again. A refused pick is drawn again the same way.
#[derive(Clone, Debug)]
pub struct LineBag {
    order: Vec<&'static str>,
    used: usize,
}

impl LineBag {
    pub fn new(table: &[&'static str]) -> Self {
        LineBag { order: table.to_vec(), used: 0 }
    }

    /// Draws until `accept` takes a line (its answer is returned with it); `rand` is the game's rand() / 32768, a value
    /// in [0, 1). None when the table is empty or nothing was taken in a few rounds of it.
    pub fn pick<R>(&mut self, mut rand: impl FnMut() -> f32, mut accept: impl FnMut(&'static str) -> Option<R>) -> Option<(&'static str, R)> {
        let count = self.order.len();
        for _ in 0..count.max(1) * 8 {
            if count == 0 {
                return None;
            }
            if count > 1 {
                if self.used >= count {
                    self.used = 0;
                } else if self.used > 0 {
                    self.order.swap(0, count - self.used);
                }
                let unused = count - self.used;
                if unused > 1 {
                    for _ in 0..unused.div_ceil(2).min(3) {
                        let k = ((rand() * unused as f32) as usize).min(unused - 1);
                        if k != 0 {
                            self.order.swap(0, k);
                        }
                    }
                }
            }
            self.used += 1;
            let line = self.order[0];
            if let Some(r) = accept(line) {
                return Some((line, r));
            }
        }
        None
    }
}

/// The bags of the three tables (their used counts are the game's globals: kept for the level).
#[derive(Resource)]
pub struct Bags {
    pub dismay: LineBag,
    pub tossed: LineBag,
    pub idle: LineBag,
}

impl Default for Bags {
    fn default() -> Self {
        Bags { dismay: LineBag::new(&DISMAY), tossed: LineBag::new(&TOSSED), idle: LineBag::new(&IDLE) }
    }
}

/// The request for a line a test accepted: at the speaker's position, else without one.
pub fn request(said: Said, at: Option<Vec3>) -> crate::audio::PlaySound {
    match at.filter(|_| said.positional) {
        Some(p) => crate::audio::PlaySound::at(said.line, p),
        None => crate::audio::PlaySound::ui(said.line),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn seq(v: &[f32]) -> impl FnMut() -> f32 + '_ {
        let mut i = 0;
        move || {
            let x = v[i % v.len()];
            i += 1;
            x
        }
    }

    /// 0x00493b70: with rand always 0 nothing is swapped forward, and the entry drawn last goes back to its place
    /// before the next draw, so the bag walks the table from its end: A, D, C, B; then (all used) the count restarts
    /// without the swap back and the front, B, is drawn again (the original's repeat at a round's end).
    #[test]
    fn the_bag_draws_each_entry_once_before_repeating() {
        let mut b = LineBag::new(&["A", "B", "C", "D"]);
        let mut got = Vec::new();
        for _ in 0..5 {
            got.push(b.pick(seq(&[0.0]), |l| Some(l)).unwrap().0);
        }
        assert_eq!(got, ["A", "D", "C", "B", "B"]);
        // any rand: four draws give four different lines
        let mut b = LineBag::new(&["A", "B", "C", "D"]);
        let mut r = seq(&[0.9, 0.3, 0.6, 0.1, 0.75]);
        let mut got: Vec<&str> = (0..4).map(|_| b.pick(&mut r, |l| Some(l)).unwrap().0).collect();
        got.sort();
        assert_eq!(got, ["A", "B", "C", "D"]);
    }

    /// A refused line is drawn past; a test that refuses everything ends the pick.
    #[test]
    fn the_bag_skips_refused_lines() {
        let mut b = LineBag::new(&DISMAY);
        let (line, said) = b.pick(seq(&[0.5, 0.2]), |l| dismay_speaker(l, &Cast { roman_alive: true, ..default() })).unwrap();
        assert!(line.starts_with("ROM") && said.speaker == Speaker::Roman && !said.positional);
        assert!(LineBag::new(&["Q1150168_Dismay"]).pick(seq(&[0.5]), |l| dismay_speaker(l, &Cast::default())).is_none());
    }

    /// 0x0044da10 / 0x0040d250: Roman always; a companion only when there and not already talking.
    #[test]
    fn dismay_and_tossed_speakers() {
        let quiet = Present { speaking: false, still: 0.0 };
        let talking = Present { speaking: true, still: 0.0 };
        let cast = Cast { jonesy: Some(quiet), q: Some(talking), ..default() };
        assert_eq!(dismay_speaker("JON50023_Dismay", &cast).map(|s| s.speaker), Some(Speaker::Jonesy));
        assert!(dismay_speaker("Q1150168_Dismay", &cast).is_none());
        assert!(dismay_speaker("ROM50039_Dismay", &Cast::default()).is_some());
        assert!(tossed_speaker("JON50172_Dismay", &cast, false).is_none());
        assert!(tossed_speaker("JON50172_Dismay", &cast, true).is_some());
        assert!(tossed_speaker("JON50173_Dismay", &cast, false).is_some());
    }

    /// 0x0047b520: each speaker's own 30 s, the base-defence lines only there, the pair lines, world 5's exclusions.
    #[test]
    fn idle_speaker_rules() {
        let still = Present { speaking: false, still: 31.0 };
        let busy = Present { speaking: false, still: 5.0 };
        let mut cast = Cast { roman_alive: true, roman_idle: 0.0, jonesy: Some(still), q: Some(busy), world: 2, ..default() };
        assert!(idle_speaker("rom50044_Ambient", &cast).is_none());
        assert!(idle_speaker("q1150029_Idle", &cast).is_none());
        assert_eq!(idle_speaker("jon50034_Idle", &cast), Some(Said { line: "jon50034_Idle", speaker: Speaker::Jonesy, positional: true }));
        assert_eq!(idle_speaker("jon54001_Ambient_Spec", &cast).map(|s| s.positional), Some(false));
        cast.q = None;
        assert!(idle_speaker("jon54001_Ambient_Spec", &cast).is_none());
        assert!(idle_speaker("jon50182_Ambient", &cast).is_none());
        assert!(idle_speaker("jon50183_Ambient", &cast).is_some());
        cast.roman_idle = 40.0;
        assert!(idle_speaker("rom50171_Ambient", &cast).is_none());
        assert!(idle_speaker("rom50044_Ambient", &cast).is_some());
        assert!(idle_speaker("rom50178_BaseDefendAmb", &cast).is_none());
        cast.base_defence = true;
        assert!(idle_speaker("rom50044_Ambient", &cast).is_none());
        assert!(idle_speaker("rom50178_BaseDefendAmb", &cast).is_some());
        let cast = Cast { roman_alive: true, q: Some(still), jonesy: None, world: 5, ..default() };
        assert!(idle_speaker("q1150025_Ambient", &cast).is_none());
        assert!(idle_speaker("q1150027_Ambient", &cast).is_some());
        assert!(idle_speaker("q1154016_Ambient_Spec", &cast).is_none());
    }
}
