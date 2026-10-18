//! The AI's situation lines that other systems cue: the wall assault's (climb taunt, ladder placed, open gate) and the
//! shrub patrol's fight taunt. The grunts' own cues (spotted, fight, hurt, dead, taunt, help, screams) are ai.rs's.
//!
//! The original's voice methods (src/audio/audio_01.cpp), each a slot of the character's vtable, each picking one line
//! of a fixed list by (rand() x range) >> 15 for the speaker's voice set and keeping it as the speaker's line (+0xb50):
//! - ClimbTaunt (+0x16c, 0x00438e80 -> 0x00438cb0): an army grunt arriving at his ladder or wall scale (GenericAI_Update
//!   states 0xaa / 0xac, ai_06.cpp:2014-2024), under the shared vo_delay (Game+0xb44, 30..45 s); "GR1/2/3..._ClimbTaunt".
//! - LadderPlaced (+0x170, 0x00439dc0 -> 0x00438c40): the grunt who planted a ladder, as he starts up it (state 0xab,
//!   ai_06.cpp:2034); no delay; "GR..._LadderPlaced", one line per set.
//! - OpenGate (+0x174): a grunt sent through the opened gate (the army breach 0x00429a50), under vo_delay; grunts
//!   0x00438c10 -> 0x00438a60 "GR..._OpenGate", captains 0x00437760 "CA150053_OpenGate" / "CA250037/38_OpenGate".
//! - The shrub patrol's Fight cue (+0x13c, ShrubPatrol 0x0043a310): "SP1500xx_Taunt" / "SP2500xx_Taunt", under
//!   vo_delay, as its fire anim starts with a target; its friendly-fire line (+0x150, 0x0043a480: "SP1500xx_FriendlyFire" /
//!   "SP2500xx_FriendlyFire", vo_delay) when the player's shots hit it (shrubpatrol.rs friendly_fire).
//!
//! A level's bank holds the lines its characters use; a cue whose lines the bank lacks plays nothing (audio.rs
//! resolve). API: [`VoiceCue`] (send it), [`VoicesPlugin`].

use crate::ai::{Barks, Delay, Grunt, Kind};
use bevy::prelude::*;

/// A situation line to say.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Cue {
    ClimbTaunt,
    LadderPlaced,
    OpenGate,
    /// the shrub patrol's fight taunt
    ShrubTaunt,
    /// the shrub patrol shot by the player (ShrubPatrol +0x150, 0x0043a480: SP1 / SP2 FriendlyFire, vo_delay)
    ShrubFriendlyFire,
}

impl Cue {
    /// The bank category.
    fn category(self) -> &'static str {
        match self {
            Cue::ClimbTaunt => "ClimbTaunt",
            Cue::LadderPlaced => "LadderPlaced",
            Cue::OpenGate => "OpenGate",
            Cue::ShrubTaunt => "Taunt",
            Cue::ShrubFriendlyFire => "FriendlyFire",
        }
    }

    /// Under the shared vo_delay (Game+0xb44), or said whenever cued.
    fn delayed(self) -> bool {
        !matches!(self, Cue::LadderPlaced)
    }
}

/// `who` says the line of `cue`.
#[derive(Message, Clone, Copy, Debug)]
pub struct VoiceCue {
    pub who: Entity,
    pub cue: Cue,
}

/// A shrub patrol's voice: the first (SP1, "SecondVoice" of the original's test) or the second set (SP2).
#[derive(Component, Clone, Copy, Debug)]
pub struct ShrubVoice(pub u8);

/// The lines said so far, by cue (the state log).
#[derive(Resource, Default)]
struct VoiceLog {
    said: std::collections::BTreeMap<String, u32>,
    last: String,
}

pub struct VoicesPlugin;

impl Plugin for VoicesPlugin {
    fn build(&self, app: &mut App) {
        use crate::levelreset::LevelAppExt;
        app.add_level_message::<VoiceCue>().init_level_resource::<VoiceLog>().add_systems(PostUpdate, (speak, log_state).chain());
    }
}

/// The speaker's line request: the class's voice lists (grunts and archers GR1..3 by their set, captains CA1 / CA2, the
/// shrub patrol SP1 / SP2).
fn line_for(cue: Cue, kind: Option<Kind>, set: u8) -> String {
    let cat = cue.category();
    match (cue, kind) {
        // the captains' own open-gate lines (0x00437760); their sets: CA1 with +0x484 bit 21 (the grunts' GR1 bit)
        (Cue::OpenGate, Some(Kind::Captain)) => format!("@captain:{cat}:{}", if set == 0 { 0 } else { 1 }),
        (Cue::ShrubTaunt | Cue::ShrubFriendlyFire, _) => format!("@shrub:{cat}:{set}"),
        _ => crate::ai::voice(cat, set),
    }
}

#[allow(clippy::too_many_arguments)]
fn speak(
    mut ev: MessageReader<VoiceCue>,
    mut grunts: Query<(&mut Grunt, &GlobalTransform)>,
    shrubs: Query<(&ShrubVoice, &GlobalTransform), Without<Grunt>>,
    mut barks: ResMut<Barks>,
    time: Res<Time>,
    mut rng: ResMut<crate::fx::FxStats>,
    voices: crate::audio::PlayingVoices,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut said: ResMut<VoiceLog>,
) {
    let now = time.elapsed_secs();
    for c in ev.read() {
        let speaker = if let Ok((g, gt)) = grunts.get(c.who) {
            if g.mind == crate::ai::Mind::Dead {
                continue;
            }
            Some((Some(g.kind), g.voice_set, gt.translation()))
        } else {
            shrubs.get(c.who).ok().map(|(v, gt)| (None, v.0, gt.translation()))
        };
        let Some((kind, set, at)) = speaker else { continue };
        if c.cue.delayed() && !barks.take(Delay::Vo, now, &mut rng) {
            continue;
        }
        let name = line_for(c.cue, kind, set);
        let req = crate::audio::PlaySound::at(name.clone(), at).looping(false);
        // kept as his line (+0xb50)
        let req = match grunts.get_mut(c.who) {
            Ok((mut g, _)) => g.speech.line(&voices, req),
            Err(_) => req,
        };
        sounds.write(req);
        info!("EVENT voice: {:?} says {name}", c.who);
        *said.said.entry(c.cue.category().to_lowercase()).or_default() += 1;
        said.last = name;
    }
}

fn log_state(said: Res<VoiceLog>, play: Option<ResMut<crate::playtest::PlayState>>) {
    if let Some(mut p) = play {
        p.0.insert("voices".into(), serde_json::json!({"said": said.said, "last": said.last}));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lines_by_class() {
        assert_eq!(line_for(Cue::OpenGate, Some(Kind::Captain), 0), "@captain:OpenGate:0");
        assert_eq!(line_for(Cue::OpenGate, Some(Kind::Captain), 2), "@captain:OpenGate:1");
        assert_eq!(line_for(Cue::OpenGate, Some(Kind::Grunt), 2), "@grunt:OpenGate:2");
        assert_eq!(line_for(Cue::ShrubTaunt, None, 1), "@shrub:Taunt:1");
        assert!(!Cue::LadderPlaced.delayed());
    }
}
