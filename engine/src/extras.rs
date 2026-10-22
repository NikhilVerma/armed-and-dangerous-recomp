//! The Extras menu's Replay Level and Cutscenes pages: what the profile has unlocked (docs/subsystems/campaign.md
//! "Front end").
//!
//! The original keeps unlock tables in the profile by category (Unlock_Add 0x004a07a0, the test 0x004a0720 with the
//! name filter 0x004a0690; progress.dat): 0 the movies seen (the PlayMovie screen adds a pre_ / post_ movie when it
//! opens it, ui_03.cpp:2206), 1 the levels reached (MissionObject_ShowStart 0x0044daf0 adds "lev_<digit>..." when the
//! mission start shows), 3 the bonus levels and 4 the features bought with tokens (pickup.rs `token_walk`). The
//! Extras rows open with the token unlockables ui_extrasmenu_replay / ui_extrasmenu_cutscene (UiScreen_InitItem cases 41 / 11) or the cheats that unlock
//! everything (Game+0x4c8 bit 11 MISSIONS, bit 12 AMATINEE). Replay Level lists the levels reached ("ui_listbox_
//! missionlist", 0x00492c90 mode 1) and the bonus levels ("ui_listbox_bonusmissions", mode 3); a chosen level is played
//! in replay mode (Game+0x568 bit 2: back to the title after its Stats, campaign.rs). Cutscenes lists the movies seen
//! ("ui_listbox_movielist", 0x00492d80), sorted by chapter, each by its title ("<name>.vid" in the text bank).
//!
//! The remaster keeps the records in options.json (`GameOptions::levels_reached`, `movies_seen`) next to the tokens,
//! as it keeps the tokens; the original's profile choice before the list ("ChooseGame") has no counterpart (the
//! remaster has one profile).
//!
//! API: [`ExtrasPlugin`] (records what is reached and seen), [`Unlocks`] (what the pages list).

use bevy::prelude::*;

/// The campaign's 21 levels in order (the mission list's entries).
pub const LEVELS: [&str; 21] = [
    "lev_1_m1", "lev_1_m1_b", "lev_1_m3", "lev_1_m4", "lev_2_m1", "lev_2_m2", "lev_2_m3", "lev_2_m4", "lev_3_m1", "lev_3_m2", "lev_3_m3", "lev_3_m4_bd", "lev_4_m1", "lev_4_m2",
    "lev_4_m3", "lev_4_m5", "lev_5_m1", "lev_5_m2", "lev_5_m3", "lev_5_m4", "lev_5_m5",
];
/// The bonus levels (the bonus list's entries; lev_bonus_m4 is in the text bank but neither in the token table nor
/// exported).
pub const BONUS_LEVELS: [&str; 4] = ["lev_bonus_m1", "lev_bonus_m2", "lev_bonus_m3", "lev_bonus_m4"];
/// The story movies in the list's order (0x00492d80's entry ids: chapter, then pre_ before post_).
pub const MOVIES: [&str; 22] = [
    "pre_1_1_opening", "post_1_1b_polewart", "post_1_3_cave", "pre_2_1_sacred_lamb", "post_2_1_return_lamb", "post_2_2_village", "post_2_3_king_and_henry",
    "post_2_4_town", "post_3_1_shrubs", "post_3_2_trenches", "post_3_3_stig_and_lady", "post_3_4_keys_roughcut", "post_4_1_wall_very_roughcut", "post_4_2_abbey_zoltan",
    "post_4_3_stig_caught", "pre_5_1_salad_eaters", "post_5_1_leper_girl", "post_5_2_lily_taken", "post_5_3_stig_ransom", "post_5_4_desert", "pre_5_5_jerosa",
    "post_5_5_the_end",
];
/// The cheats that unlock every level / movie (Game+0x4c8 bits 11 and 12; cheats.rs).
const ALL_LEVELS_CHEAT: u32 = crate::cheats::ALL_LEVELS;
const ALL_MOVIES_CHEAT: u32 = crate::cheats::ALL_MOVIES;

/// Category 1's name filter (0x004a0690 kind 1): "lev_" and a digit (the bonus levels are category 3).
fn is_campaign_level(name: &str) -> bool {
    name.len() > 4 && name[..4].eq_ignore_ascii_case("lev_") && name.as_bytes()[4].is_ascii_digit()
}

/// Category 0's filter: a story movie ("pre_" / "post_").
fn is_story_movie(name: &str) -> bool {
    let n = name.to_ascii_lowercase();
    n.starts_with("pre_") || n.starts_with("post_")
}

/// What the Extras pages offer now.
pub struct Unlocks {
    /// the Replay Level row is enabled
    pub replay: bool,
    /// the Cutscenes row is enabled
    pub movies: bool,
    /// the levels and bonus levels that can be replayed (exported ones only)
    pub levels: Vec<&'static str>,
    pub bonus: Vec<&'static str>,
    pub movie_list: Vec<&'static str>,
    /// the Difficulty page's rows that can be chosen (easy .. insane): Over The Top and Insane once bought
    /// (UiScreen_InitItem 0x00491b70 case 71: a text id whose digit is '3' or more needs 0x004a0720(id, 4))
    pub difficulty: [bool; 4],
}

impl Unlocks {
    pub fn new(opts: &crate::options::GameOptions, cheats: &crate::cheats::Cheats, levels: &crate::campaign::Levels) -> Self {
        // categories 3 / 4: the unlockables the tokens bought (`GameOptions::token_unlocks`)
        let bought = |name: &str| opts.token_unlocks.iter().any(|n| n.eq_ignore_ascii_case(name));
        let all_levels = cheats.on(ALL_LEVELS_CHEAT);
        let all_movies = cheats.on(ALL_MOVIES_CHEAT);
        let reached = |l: &&&str| all_levels || opts.levels_reached.iter().any(|r| r.eq_ignore_ascii_case(l));
        Unlocks {
            replay: all_levels || bought("ui_extrasmenu_replay"),
            movies: all_movies || bought("ui_extrasmenu_cutscene"),
            levels: LEVELS.iter().filter(reached).filter(|l| levels.dir(l).is_some()).copied().collect(),
            bonus: BONUS_LEVELS.iter().filter(|l| all_levels || bought(l)).filter(|l| levels.dir(l).is_some()).copied().collect(),
            movie_list: MOVIES.iter().filter(|m| all_movies || opts.movies_seen.iter().any(|s| s.eq_ignore_ascii_case(m))).copied().collect(),
            difficulty: [true, true, bought("ui_diff_3"), bought("ui_diff_4")],
        }
    }
}

pub struct ExtrasPlugin;

impl Plugin for ExtrasPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, (record_level, record_movies));
    }
}

/// Category 1: the level is reached once its mission start shows (0x0044daf0).
fn record_level(mission: Option<Res<crate::mission::Mission>>, levels: Res<crate::campaign::Levels>, mut opts: ResMut<crate::options::GameOptions>, mut done: crate::levelreset::PerLevel<bool>) {
    let done = done.get();
    if *done || !mission.is_some_and(|m| m.started()) {
        return;
    }
    *done = true;
    let level = levels.current_name();
    if is_campaign_level(&level) && !opts.levels_reached.iter().any(|l| l.eq_ignore_ascii_case(&level)) {
        opts.levels_reached.push(level);
        opts.save();
    }
}

/// Category 0: a story movie is seen once it opens (the PlayMovie screen, ui_03.cpp:2206).
fn record_movies(mut events: MessageReader<crate::movie::MovieEvent>, mut opts: ResMut<crate::options::GameOptions>) {
    let mut changed = false;
    for e in events.read() {
        if let crate::movie::MovieEvent::Started(name) = e {
            if is_story_movie(name) && !opts.movies_seen.iter().any(|m| m.eq_ignore_ascii_case(name)) {
                opts.movies_seen.push(name.to_ascii_lowercase());
                changed = true;
            }
        }
    }
    if changed {
        opts.save();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// 0x004a0690's filters: kind 1 "lev_" and a digit, kind 0 "pre_" / "post_".
    #[test]
    fn unlock_filters() {
        assert!(is_campaign_level("lev_1_m1") && is_campaign_level("lev_3_m4_bd"));
        assert!(!is_campaign_level("lev_bonus_m1") && !is_campaign_level("lev_shell"));
        assert!(is_story_movie("pre_1_1_opening") && is_story_movie("POST_5_5_the_end"));
        assert!(!is_story_movie("pub_01") && !is_story_movie("credits") && !is_story_movie("pm_logo"));
    }
}
