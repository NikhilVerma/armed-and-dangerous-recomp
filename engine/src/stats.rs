//! The mission statistics the Stats screen shows at a win (docs/subsystems/win-sequence.md): Game+0x2718 shots
//! fired, +0x271c shots hit, +0x270c enemies killed of +0x2710, +0x2714 head shots, and the money lines +0x2724
//! kills, +0x2728 accidents, +0x272c rescues, +0x2730 loot; Game+0x570 the money carried between levels.

use crate::levelreset::LevelAppExt;
use bevy::prelude::*;

#[derive(Resource, Default, Clone, Debug)]
pub struct MissionStats {
    /// Game+0x2718: the player's shots (PlayerObject_Update game_06.cpp:2972, 2999)
    pub shots_fired: u32,
    /// Game+0x271c: player shots that hit a character (Projectile_ApplyHitDamage 0x0047eec0, game_06.cpp:1564)
    pub shots_hit: u32,
    /// Game+0x2714: of those, on the head or neck (game_06.cpp:1579)
    pub head_shots: u32,
    /// Game+0x270c: enemies killed, any cause (AI death 0x00431720, ai_04.cpp:597)
    pub enemies_killed: u32,
    /// Game+0x2710: enemies spawned or counted (ai_01.cpp:282 / 1520 / 2327, game_03.cpp:1101)
    pub enemies_total: u32,
    /// Game+0x2724 / +0x2728: a killed object's or enemy's score_value, >= 0 to Kills, < 0 to Accidents
    /// (0x00450ee0 game_03.cpp:978, 0x00431720, building ai_01.cpp:1920)
    pub kills_money: f32,
    pub accidents_money: f32,
    /// Game+0x272c: peasants banked (none on lev_1_m1)
    pub rescues_money: f32,
    /// Game+0x2730: the mission's score_value at the win (game_02.cpp:2262), tokens 2500 each (0x004a0850)
    pub loot_money: f32,
    /// Game+0x570: the money carried from level to level
    pub money: f32,
}

impl MissionStats {
    /// A killed thing's score_value to Kills or Accidents.
    pub fn killed(&mut self, score: f32) {
        if score >= 0.0 {
            self.kills_money += score;
        } else {
            self.accidents_money += score;
        }
    }
}

pub struct StatsPlugin;

impl Plugin for StatsPlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<MissionStats>().add_systems(Update, (count_enemies, player_counts, state_log));
    }
}

/// The player's shot counters (player.rs keeps them as it fires and hits).
fn player_counts(p: Res<crate::player::PlayerStats>, mut s: ResMut<MissionStats>) {
    s.shots_fired = p.shots;
    s.shots_hit = p.hits;
    s.head_shots = p.head_shots;
}

/// Every grunt that appears counts toward the enemy total.
fn count_enemies(added: Query<(), Added<crate::ai::Grunt>>, mut s: ResMut<MissionStats>) {
    s.enemies_total += added.iter().count() as u32;
}

/// The playtest state's "mstats": the Stats screen's counters and money lines.
fn state_log(s: Res<MissionStats>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    state.0.insert(
        "mstats".into(),
        serde_json::json!({"kills": s.kills_money, "accidents": s.accidents_money, "rescues": s.rescues_money, "loot": s.loot_money, "enemies_killed": s.enemies_killed, "enemies_total": s.enemies_total}),
    );
}
