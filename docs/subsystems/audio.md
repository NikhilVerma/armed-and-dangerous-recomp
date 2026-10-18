# Audio: sound bank, playback rules, music, ambience and voice

Status 2026-10-13. Static reading of the recovered code (src/audio/audio_01.cpp, audio_02.cpp, audio_03.cpp,
src/renderer/auto_004fb170.cpp, auto_004fb1f0.cpp, auto_00500230.cpp) and of the lev_1_m1 data. Nothing here was
checked in a running game. "Guess" marks what the code does not prove.

Export for the remaster: `python tools/remaster_export_audio.py <level> [--check N]` writes
`work/remaster/<level>/audio/` (`wav/`, `sounds.json`, `voice.json`, `ambient.json`). The bank layout is in
docs/formats/big-aud.md.

## 1. Formats

| What | Where | Format | Evidence |
| --- | --- | --- | --- |
| Sound bank | `<level>_big08_aud.big` | 0xfc-byte definition header + PCM, or + stream path | loader 0x004fab80 (audio_03.cpp:213) |
| Streamed sounds (flags 0x40) | `GameData\streams\<name>.raw` | headerless PCM, format from the header; file size = header +0xf0 | 0x004fa340 (audio_02.cpp:3678); all 668 lev_1_m1 streams match |
| Movies | `GameData\movies\*.pcv` | AVI: DivX video (`DXGM`), PCM audio 22050 Hz stereo 16-bit | RIFF header of pre_1_1_Opening.pcv |
| Subtitle text | `<level>_big01_text.big` | UTF-16 strings keyed by id; speech id = sound name | 0x00495e00, Text_Lookup 0x00495f00 |
| Language | `GameData\lang.dat` | 4 bytes, `engl` | file content |

Every retail sound is 16-bit PCM: mono 11025 / 22050 / 44100 Hz or stereo 22050 / 44100 Hz. The game builds only
WAVE_FORMAT_PCM buffers (0x004fa6d0 sets the tag to 1). No ADPCM or compressed audio exists; the export needs no
decoder and no ffmpeg (none is on PATH). The `.wav` loader 0x004f49b0 (unused by the levels) would count frames as
IMA ADPCM when cbSize (+0xec) is non-zero (guess); cbSize is 0 everywhere and the exporter refuses a non-zero one.

DirectSound setup (0x004fa8f0): primary buffer 22050 Hz, stereo, 16-bit. Secondary buffers (0x004fa6d0) use
flags 0x100e8 (mono: CTRLVOLUME, CTRLPAN, CTRLFREQUENCY, LOCSOFTWARE, GETCURRENTPOSITION2) or 0x100a8 (stereo, no
pan). **No buffer has DSBCAPS_CTRL3D**: the game does its own 3D (section 3). The listener calls in 0x004f9f70
therefore have no audible effect (guess from the flags).

## 2. Definition header (0xfc bytes; the first 0xfc bytes of the 0x1a0-byte runtime record)

Offsets are the same in the file and in the runtime definition. The defaults come from 0x004f4fe0; the
compiled-definition opcodes (AudioDef_Load 0x004f5010, audio_02.cpp:2822) name the blocks.

| Offset | Type | sounds.json | Meaning | Evidence |
| ---: | --- | --- | --- | --- |
| 0x00 | char[32] | key | name, hash key | 0x004f4cc0 |
| 0x20 | u32 | `flags`, `flag_names` | see the flag table | below |
| 0x24 | u32 | `priority` | default 15. Higher wins an exclusive channel | 0x004f35e0 (`>=` keeps the old voice) |
| 0x28 | f32 | `volume_db` | dB offset (voice +0x10). Footsteps -3, most 0 | 0x004f32f0, 0x004fb170 |
| 0x2c | f32 | `pitch_variation`, `rate_range` | rate uniform in [(1 - v/8) * rate, (1 + v/4) * rate] | 0x004f3a80 (K 0.125, 0.25) |
| 0x30 | f32 | `min_distance` | default 20; start of the distance curve | 0x004fb170, 0x004f4fe0 |
| 0x34 | f32 | `max_distance` | default 500; -40 dB (silent) at and beyond; streamed sounds stop beyond | 0x004fb1f0, 0x004fa210 |
| 0x38..0x4c | f32 x 6 | `envelope` | flag 0x200000: delay, attack, hold, release, fade-out time, level | 0x004f2730, 0x004fb360 |
| 0x50..0x6c | | `unknown_words` | flag 0x400000 block; no reader found; zero in lev_1_m1 | opcode 10 |
| 0x70..0x78 | f32 x 3 | `fade` | flag 0x800000: level, fade-in time, fade-out time; zero in lev_1_m1 | 0x004f28d0, 0x004f2950 |
| 0x7c | char[32] | `intro` | flag 0x1000000: intro sound before the loop body ("none" = no intro) | 0x004fa340 |
| 0x9c | char[32] | `outro` | flag 0x1000000: outro sound on a non-immediate stop | 0x004fb360 |
| 0xbc | char[32] | `follow_on` | flag 0x10000000: sound played when this one ends | 0x004f2a80 |
| 0xdc..0xf4 | WAVEFORMATEX + size + frames | `channels`, `rate`, `bits`, `frames`, `duration` | | 0x004fa6d0 |
| 0xf8 | u32 | | runtime data pointer | 0x004fab80 |

Flags (+0x20). Counts are for lev_1_m1 (1308 sounds).

| Bit | Name in sounds.json | Count | Meaning | Evidence |
| --- | --- | ---: | --- | --- |
| 0x1 / 0x2 | unique_per_object / unique_restarts | 0 | one voice per (sound, object); 0x2 stops the old one, else its handle is returned | 0x004f32f0 |
| 0x4 | driver_3d_extra | 0 | callback +0x60 / virtual +0x44 (empty on PC) | 0x004f2870 |
| 0x20 | always_set | 1308 | no reader found | survey |
| 0x40 | streamed | 668 | data in streams\, refilled per chunk | 0x004fa340, 0x00500890 |
| 0x200 / 0x400 | speech / speech_alt_colour | 516 / 0 | speech volume group; subtitle callback; 0x400 selects chat colour 4 | 0x004fb170, 0x004fa100, 0x00495e00 |
| bits 11..15 | `exclusive_channel` | 223 (ch 0), 8 (ch 3) | one voice per channel; lowest set bit = channel | 0x004f35e0 |
| 0x10000 | sequence_part | 24 | intro/outro pieces: no buffer at load | 0x004fa7f0 |
| 0x200000 | envelope | 281 | music: 10 s fade-out on stop | 0x004fb360 |
| 0x800000 | fade_in_out | 0 | | 0x004f29d0 |
| 0x1000000 | intro_outro_loop | 15 | loop body with intro/outro | 0x004fa340, 0x004fb360 |
| 0x2000000 | music | 281 | music group; segment variants (`next`) | 0x004fb170, 0x00500a00 |
| 0x4000000 | loop_stream | 284 | guess: long looping streams (music, amb_pub_busy, amb_snow_wind_fields_lp, pubsong_fading); no reader | survey |
| 0x8000000 | music_last_segment | 5 | variant choice with play flag 0x800 takes the second entry | 0x00500a00 |
| 0x10000000 | follow_on | 54 | play +0xbc when done | 0x004f2a80 |
| 0x40000000 | no_group_level | 0 | group level 0 dB | 0x004fb170 |

Looping is **not** a header field. The caller passes it (the low byte of the play call's third argument
becomes voice flag 0x20, 0x004fa210; DirectSound `Play(..., DSBPLAY_LOOPING)` in 0x004fa100). Streamed music
always plays looped and is ended by its segment logic. `sounds.json` gives a `loop` default with
`loop_evidence` (the `_lp` naming convention, flag 0x1000000, music).

## 3. Playback rules

### Play calls (audio object Game+0x2c, vtable 0x0058a230)

| Slot | Function | Use |
| --- | --- | --- |
| +0xd0 | 0x004f4030 -> +0x60 0x004f3c60 | 2D: no position, no distance volume, no pan |
| +0xc4 | 0x004f3670 -> +0x58 0x004f32f0 | attached to an object: follows its audio position (callback 0x0045a4c0, characters use the head bone, 0x0043e450) |
| +0xcc | 0x004f39a0 -> +0x5c 0x004f3740 | at a fixed position |
| +0xc8 | 0x004f32a0 | 2D when the object is the listener (player), else attached |
| +0xd4 | 0x004f4100 -> 0x004f3b10 | music (one current voice +0x90) |
| +0xd8 / +0xdc | 0x004f30e0 / 0x004f4380 | stop by handle / is playing |
| +0xf8 | 0x004f4540 | per-voice dB offset (voice +0x14) |

Fx scripts play sounds with opcode 0x39 (random pick from an inline list) through Fx_PlaySound 0x00420080:
positional on the fx's object via +0xc4, or 2D when the object belongs to the player (0x0045a480).
At most 64 positional voices start (0x004fa210: fails when +0x120 > 0x3f). A stereo sound cannot play
positionally (logged and refused, 0x004fa210).

### Volume (dB, additive)

0x004fb170 sums, in dB:

1. the group level: music (flag 0x2000000) +0x1c, speech (flags & 0x600) +0x20, else sfx +0x18 (0 dB with
   0x40000000);
2. the master offset +0x24, unless flag 0x800000;
3. `volume_db` (voice +0x10), the per-play offset (voice +0x14, slot +0xf8), the envelope offset (voice +0x90);
4. for positional voices (voice flag 0x10): `curve(distance, min_distance, max_distance)`.

The driver (0x004fa1d0) sets `IDirectSoundBuffer::SetVolume(dB * 100)`, or DSBVOLUME_MIN (-10000) when the sum
is at or below -40 dB (K 0x005bcb64 = -40.0, K 0x005bcb60 = 100.0).

**Distance curve** (0x004f4ec0): 0 dB at or below `lo`, -40 dB (silent) at or beyond `hi`, between them the
cubic Bezier of the control values (0, -5, -10, -40) at t = (x - lo) / (hi - lo). The basis matrix at 0x0057e7b0
is the Bezier basis (-1 3 -3 1 / 3 -6 3 0 / -3 3 0 0 / 1 0 0 0), Spline_Coefficients 0x0046d630. So:

    dB(t) = -15 t (1 - t)^2 - 30 t^2 (1 - t) - 40 t^3      (t = 0.5: -10.6 dB)

Distances are game units, measured from the listener (the camera position, 0x004f9f70) to the voice position
(0x004fb1f0).

**Pan** (0x004fb1f0): the source position in listener space (camera matrix +0xc4), pan = clamp(x / distance,
-0.9, 0.9) * 10000 + 0.5, truncated (0 below 1e-4 distance). No Doppler, no elevation, no HRTF.

**Group levels from the options**: the menu values Game+0x490 m_soundVolume (sfx), +0x494 m_voiceVolume (speech)
and +0x498 m_musicVolume (music), default 0.8 (docs/subsystems/menus.md), go through slots +0x88 / +0x90 / +0x8c
(ui_01.cpp:159). Each setter stores `curve(1 - value, 0, 1)`: the default 0.8 is -3.2 dB, 0.5 is -10.6 dB, 0 is
silent.

### Pitch

Rate = header rate, randomised by `pitch_variation` (0x004f3a80, `rand()`, uniform). A global scale +0x14 (slot
+0x94) multiplies non-music rates (0x004f2f50). Slot +0xf4 shifts one voice (0x004f4760).

### Priority and exclusive channels

Bits 11..15 select one of five channels (handles at +0x38 + 4i). When the channel's voice still plays and its
priority is `>=` the new one's, the new sound is not played and the old handle is returned; otherwise the old
voice stops (0x004f35e0, 0x004f3670). In lev_1_m1, channel 0 holds the teammate and Roman lines (`jon*`,
`rom*`, `q11*`; priorities 12..28) and channel 3 the hand-to-hand grunts (`*_hth*`, priority 14). The character
voice code also keeps per-character handles (+0xb4c scream, +0xb50 line) and shared delays (Game+0xb44, +0xb64,
+0xb94) so lines do not overlap (section 5).

### Envelope, fades, sequences

- Envelope (flag 0x200000, 0x004f2730): level 0 until `delay`, ramps up over `attack`, 1 for `hold`, down to
  `level` over `release`; a stop (0x004fb360, not immediate) sets a fade-out of `fade_out` seconds. The offset is
  (1 - level) * -40 dB. Music uses only `fade_out` = 10 s, `level` = 1.
- Intro/outro loops (flag 0x1000000, e.g. `fire_large_lp` intro none, outro `fire_large_end`;
  `alm_airraid_lp` intro `alm_airraid_start`): played through a stream reader even for in-memory sounds
  (0x004fa340): intro once, then the body looped; a non-immediate stop queues the outro (0x004fb360).
- Follow-on (flag 0x10000000): when the voice ends, the sound named at +0xbc is played with the same object or
  position and takes over the handle (0x004f2a80). Used for conversations (`jon54012_ambient_spec` ->
  `q1154013_ambient` -> `jon54013_ambient`) and the pub lines (-> `pubsong_fading`).

## 4. Music

Dynamic music, Music_Update 0x0044f580 (audio_01.cpp:1660): per level a 7-entry table at 0x005ce4e0 +
0x1c * (Game+0x2708 - 1). For level 1: `l1amb`, `stealth`, `skirm2`, `l1bat`, `skirm1`, `l1vic`, `l1def` (state
0..6; -1 silence; strings read from game.unpacked.exe). The request goes to slot +0xd4 (0x004f3b10).

**Inputs.** The record is Game+0xa88 (+0 timer, +4 time played, +8 hold, +0xc handle, +0x10/+0x11/+0x12
victory / its cl / defeat, +0x14/+0x18 near / far counts, +0x1c state, +0x20 lock). Thresholds are level
properties read by LevelProperties_Load 0x00424530 (render_r6.cpp:1098-1104), lev_1_m1 values from
p_globalsettings (code default in brackets):

| Game | Property | lev_1_m1 |
| --- | --- | --- |
| +0xd6c | music_nearby_suspense (far count limit) | 2 [2] |
| +0xd70 | music_agro_battle (near count limit) | 4 [4] |
| +0xd80 | music_nearby_range | 100 [100] |
| +0xd74 | music_agro_range | 500000 [500000] |
| +0xd84 | music_play_time | 600 [600] |
| +0xd88 / +0xd8c | music_play_delay_min / max | 60 / 300 [30 / 60] |

+0xd78 / +0xd7c are the game times of the last battle / stealth track (0 = never), re-armed after 180 s
(0x005bcbb8 = 180.0). The reset 0x0044f4c0 (level start) sets state -1, hold 1 s, timers and requests 0,
+0xd78 = +0xd7c = 0 and stops the music.

**Per frame** (called from Game_Update 0x00402e80; returns while paused, Game+0x2684, and while the lock +0x20 is
set): with state -1 and music still playing (a fade-out) nothing happens. timer -= dt; played += dt while music
plays, else 0. While hold > 0: hold -= dt and return. While a track is queued (audio +0x94): hold = 10 s and
return. After `music_play_time` (600 s) of continuous music the default becomes silence. Victory / defeat:
only while music plays and the state is not 5/6 (and not base defend 0x0044e600): defeat -> 6, else victory ->
5. Otherwise, when timer <= 0 (redrawn uniform 0.5..1 s, 0x0046e2c0), count World+0xe7c (the grunts; vt+0xe4
false, vt+0xe8 true, alive 0x00454780): **near** = AI (vt+0x10c) with a target (+0x588, 0x0042c6f0) within
music_agro_range (effectively all), **far** = any within music_nearby_range (100). Grunt vtables 0x5a9cf0 /
0x5abb88 have vt+0xe4 = 0x00435250 (false), vt+0xe8 = 0x00419280 (!vt+0xe4), vt+0x10c = 0x00433fa0 (true).

| State | Rule |
| --- | --- |
| -1 | near >= 4: battle; far >= 2: stealth when the last one is 180 s old, else l1amb (arming +0xd7c when 0); else l1amb |
| 0 l1amb | near >= 4: battle; far >= 2 and 180 s since the last stealth: stealth |
| 1 stealth | near >= 4: battle; far == 0: silence |
| 2 skirm2 / 4 skirm1 | near >= 2: l1bat when the last battle is 180 s old; else stealth (if 180 s old) or, with near = far = 0, silence |
| 3 l1bat | near >= 2: skirm1 when the last battle is 180 s old; else as above |
| 5 / 6 | when the jingle has ended: requests cleared, silence |

battle = skirm2 when the last battle is 180 s old (re-armed), else l1bat (arming +0xd78 when 0). A new track sets
hold = 10 s; silence sets hold = time played clamped to [delay_min, delay_max] (60..300 s in lev_1_m1; 60 s after
a jingle, since played is 0 then) and stops the music with immediate = 1: voice +0x64 |= 0x800, the queue
cleared, the envelope's 10 s fade-out (0x004f30e0 -> 0x004f26a0). The play call's fourth argument is a flag
byte (victory: !+0x11, defeat: 1) that sets voice flag 0x8000000 in 0x004f3b10; no reader of that voice flag was
found (open).

**Requests.** 0x0044f560 (eax = record, cl): +0x10 = 1, +0x11 = cl, hold = 0. Callers: the ui_ear trigger handler
0x0046afa0 with cl = 1 when the trigger marked an objective (objective_trigger_receive_0N with a non-zero
objective_tag_0N) and shows that ear's text (text_trigger_receive_0N), and the level state machine 0x0044a390
state 2 (mission success) with cl = 0. 0x0044f570: +0x12 = 1, hold = 0; callers the player's death vt+0xd4
0x0047ae40 and 0x0044a390 states 3..8 (mission failed). So the **objective jingle** is the `l1vic` track: in
lev_1_m1 ui_ear 65 requests it on trigger 900 (leaving the pub, `l1m1_sub2`, tag 55) and trigger 901 (the first
gate blown, `l1m1_sub3`, tag 56); ui_ear 253 (trigger 7, `l1m3_bomb`) has no objective. The request waits while
the game is paused (the message pauses it) and while no music plays; it then queues `l1vic` for the next
segment boundary; `l1vic.next = [none]` ends the music after its 13.7 s, then 60 s of silence.

**Segment resolution details** (Audio_ResolveSound 0x00500a00, disassembly 0x00500ae6..0x00500ce7): `params`
is voice +0x64 and params[2] (voice +0x6c) the segment that just ended. A queued track equal to that segment
clears the queue and ends the music. With a queue: the first `next` entry whose base (cut at the last `_`)
is the track; else the first entry whose own `next` has such an entry (two-step route); else the first entry.
The queue is **not** cleared there: it is cleared only when a segment whose base is the queued track ends, and
that segment is then chosen again (it plays twice; static reading, not observed at run time: the sound log
hook sees only voice starts, not stream continuations). Without a queue: `none` ends; a stopped voice (+0x64
0x800) at a segment with flag 0x8000000 (music_last_segment, e.g. `l1amb_033`) takes the second entry
(`l1amb_loopend`).

Remaster port: engine/src/gameaudio.rs (`Music`, music_state = 0x0044f580, music_voice = the resolver), requests
from engine/src/mission.rs (objective) and the mission outcome / Roman's death. Original check (shot.py level
scenario, test.log_sounds): `l1amb` starts at frame 59 (0.98 s); the remaster starts it at 1.03 s.

The tracks are **segment graphs**, not single files. Each music definition (flag 0x2000000, 281 in lev_1_m1,
stereo 44100 Hz, 2..35 s each) has a `next` list of variants (`+0x17c` count, names at `+0x180`). When a segment
ends, Audio_ResolveSound 0x00500a00 picks the next one from its `next` list: the entry whose name, cut at the last
`_`, equals the queued request (audio +0x94, set when a new state is requested while music plays), else the first
entry (the track continues: `l1amb_002` -> `l1amb_003`); `none` ends the music. Example: `l1amb_002.next =
[l1amb_003, stealth_022, skirm2_009, l1bat_044, l1vic, l1def]`. Transitions therefore happen only at segment
boundaries. A stop fades out over 10 s (envelope). The full graph is in sounds.json (`next`).

The level-6 row of the table holds `ROM50039_Dismay` and similar lines (guess: a separate table that follows).

## 5. Voice lines and subtitles

**Which sounds are speech**: flags & 0x600 (516 in lev_1_m1). Names are `<speaker><number>_<category>`:
`ca1`/`ca2` captains (Von Hell / Von Murder), `gr1`/`gr2`/`gr3` grunts (Seamus / Patrick / Tommy), `jon`
Jonesy, `q11` Q, `rom` Roman, `pe`/`pm` peasants, `gol` Goliath, `sp` shrub patrol, `mo` Molmer.

**Subtitle text**: the text bank entry whose id is the sound name (case-insensitive), written `Speaker: line`
(312 of 516 lines have one; screams, hurt grunts, `_hth` and `_ic` effort sounds have none). The DirectSound
driver calls the start callback +0x68 for every speech sound it starts (0x004fa100, `(flags >> 10) & 1` as the
second argument). That callback is **0x00495e00** (installed by Game_InitApp 0x00401fb0 as block+0x44, passed to
the sound system as its 10th init argument, engine_01.cpp:2328): when Game+0x177 (m_SubtitlesGame) is set, it
looks the sound name up with Text_Lookup 0x00495f00 (quiet) and, when found, adds it to the chat feed Game+0xaac
with Chat_AddMessage 0x0046aa60 (word wrap at 490 units), colour index 4 with flag 0x400 or 1 otherwise. This
corrects docs/subsystems/qol.md section 6, which found no reader of +0x177. Movie subtitles are separate:
big12_ui tables, 0x00495cb0, option +0x176 (docs/formats/big-ui.md).

**Triggers** (voice.json `triggers`):

- Sound ears (`sound_ear` instances, SoundEar setup 0x00497750, trigger handler 0x00497900): when the
  instance's `trigger_id` message arrives, it picks among the lines it has (`roman_sound`, `q_sound`,
  `jonsey_sound`, `ai_sound`) whose speaker is alive and within `max_distance` (default 300) of the player, at
  random (`rand`), or the nearest when none is in range. Roman's line plays 2D (+0xd0); the others play on the
  speaker (+0xc4). The ear removes itself unless `reapeat` (sic). Details (game_08.cpp:718): a line counts only
  when the **placement** sets it (Object_ReadStringProp 0x004533a0 returns 1; template "none" does not). Roman's
  line is always one choice (no distance test). Q is the player's 0x00475d40, Jonesy 0x00475d30, the AI the
  object tagged `ai_tag` (0x004592e0); each is dropped when absent or dead (0x00454780) and counted when its
  squared distance to the player is below max_distance^2. With no choice but someone present, the nearest
  (strictly smaller distance) is taken; with nobody at all the ear removes itself even with `reapeat`.
  `pick = (rand() * count) >> 15` then maps literally (0x00497bdc..0x00497cc3): with Roman, 0 = Roman, 1 =
  first in range of Q / Jonesy / AI, 2 = Jonesy when both Q and Jonesy are in range else AI; without Roman, 0 =
  first in range, 1 = Jonesy when both else AI. The triggers come from trigger volumes
  (`bb_40x40` / `bb_300x300` `on_enter`) or objects (`send_trigger_on_death`), listed as `trigger_senders`.
  lev_1_m1 has 7 ears (e.g. trigger 711: `jon50001_l1_ambient` / `rom50001_l1_ambient`, sent by
  `bb_300x300` 269 on enter and `snow_pipe_curved` 96 on death).
- Character code, by category: captain lines 0x00437510..0x004382e0, grunt lines 0x004383a0..0x00439f00, Jonesy
  0x0044ea50..0x0044ed50, Q 0x00487050..0x004876a0 and 0x004857e0, Goliath 0x00434180 / 0x00439f70, shrub patrol
  0x0043a310 / 0x0043a480, Molmer 0x0043a5e0..0x0043a7b0, peasants 0x0044d1f0 / 0x00439f70, pub 0x00468dd0. Each
  picks one line of a set with `(rand() * range) >> 15`. Rules: one scream per character (+0xb4c), one line
  (+0xb50); a shared delay Game+0xb44 (min/max at +0xb48/+0xb4c) gates help/opengate/taunt-style lines, Game+0xb64
  hurt/dead/sticky; per-character delays (+0xb48: 1 s captains, 4 s grunts, 10 s Jonesy/Q) gate short hurt
  sounds; a grunt's death line plays once (state flag 0x400). The voice set (`gr1`/`gr2`/`gr3`) comes from the
  character's flags +0x484 & 0x200000 and +0x488 & 0x20000000. Which AI state calls which method is not traced
  (they are virtual slots of the character classes).
- Code that names the line: exporter field `exe_string` (the string in game.unpacked.exe and the tracked
  function whose code pushes its address).
- Fx scripts with opcode 0x39, started by animation events (`FX <name>` in big03_anim): the `_hth*` hand-to-hand
  efforts, the `_ic` lines, the shark screams.
- Follow-on chains (flag 0x10000000).

Eight lev_1_m1 lines have no trigger found (`jon50024/37/38/39/47/56_ambient`, `q1150117_defend`,
`rom50078_ambient`).

## 6. Ambient sounds

Two mechanisms:

1. **`ambient` instances** (property set `p_ambient`; loader Object_LoadAmbientSoundProperties 0x00493950,
   runtime AmbientSounds 0x00500230..0x00500520, auto_00500230.cpp). Properties: `world` (bool), `loop1..10`,
   `oneshot1..10`, `min_distance` (100), `max_distance` (300), `min_delay` (5 in p_ambient), `max_delay` (10).
   - Each instance becomes one emitter (0x005002e0) at its position. `world` = 1: no position (2D, no distance).
   - Per frame (0x00500520, called from the voice update 0x004f2a80 with the frame time): a positional emitter
     farther than `max_distance` from the listener stops both of its sounds. Otherwise, when no loop runs, one
     random `loopN` starts (2D, looping) and the timer resets. The volume offset = curve(distance, min, max)
     (dB, section 3) is applied to both sounds every frame (+0xf8). Positional emitters are still played 2D
     (no pan): the third argument of +0x13c is always 0.
   - One-shots: while a one-shot plays nothing else happens; when none plays (and the game is not paused, audio
     +0xa8) the timer counts down by the frame time and on expiry a random `oneshotN` starts (2D) and the timer is
     redrawn uniform in [min, max] (0x00500520, `ResetTimer`). The emitter's initial interval is 20..45 s, then
     replaced by the delays when the instance has one-shots.
   - **Original bug** (0x00493950): both bounds read the key `min_delay` (string 0x005b93d8; the maximum's default
     is 5). With p_ambient's `min_delay` 5 the interval is a fixed 5 s, not 5..10 s. `ambient.json` gives both
     `oneshot_delay` (as the game runs) and `oneshot_delay_authored`. Not fixed (AGENTS.md); logging it in
     docs/original-bugs.md is left to the caller.
   - lev_1_m1: one `world` emitter looping `amb_snow_wind_fields_lp` (stereo, 31 s), four positional emitters of
     animal one-shots (`amb_oneshot_elk`, `_finch`, `_penguin`, `_gull_1/2`) with 100/300 distances.
2. **Object loops**: `attach_audio_loop` on a property set (e.g. `p_oilpump` -> `amb_oil_pump_lp`) is started
   attached (+0xc4, looping) by the object update 0x004505a0 (game_03.cpp:646); `amb_pub_exterior_lp` by
   0x0046a790; fire loops by fx scripts.

## 7. Verification (lev_1_m1, 2026-10-13)

`remaster_export_audio.py lev_1_m1 --check 30`: 1308 sounds exported (237 MB PCM), 0 missing streams. All 1308
files were also scanned: none shows a garbage signature (white-noise spectrum, DC offset, wrong rate). Flags:
silent placeholders (`blank_vo`, `fol_bodyslide_*_end` 0.1 s of zeros, `skirm2_loopend` -73 dB), mechanical
clicks with mostly high-frequency energy (`wpn_*_load`, `ui_move`, `wpn_ticker`), and speech/gunfire mastered to
full scale (0.03..0.36 % of samples at +-32767 in the source data, copied unchanged).

## 8. Remaster port (engine/src/audio.rs, gameaudio.rs; 2026-10-14)

`audio.rs` is the sound system as a self-contained module (AudioBankPlugin): a game gives it a bank (sounds.json,
voice.json) and sends messages; everything per sound is data from the bank. `gameaudio.rs` is lev_1_m1's use of it
(anim notes, ambient emitters, music, Roman as the listener's body, the playtest hooks).

| Original | Remaster |
| --- | --- |
| Play slots +0xc4 / +0xcc / +0xd0 (2D, attached, at a point) | `PlaySound::on / at / ui`; `SoundId` as the handle |
| Volume 0x004fb170: group + master + def + offset + envelope + distance, silent at -40 dB | `Voice::level_db`, recomputed every frame (options act on playing sounds too) |
| Group setters 0x004f42e0 / 0x004f4260 / 0x004f42a0: curve(1 - value) | `Mixer::group_db`; 0 dB at the default 0.8 (the remaster's loudness), the original's curve around it, 0 = silent |
| Distance curve 0x004f4ec0 on positional voices | applied (before: Bevy's spatial sinks never got the curve, positional sounds played at full volume at any distance) |
| Pan 0x004fb1f0 (clamp(x / d, -0.9, 0.9) x 10000, very hard) | Bevy's spatial pan (two ears); its 1 / d^2 falloff made inert (spatial scale 1e-4) |
| Exclusive channels and priority 0x004f35e0 | `Channels`: a busy channel with a priority at least the request's drops it, else its voice stops |
| Pitch variation 0x004f3a80 | `PlaybackSettings.speed` in [1 - v/8, 1 + v/4] (19 lev_1_m1 sounds have v = 1) |
| Intro / loop / outro 0x004fa340 / 0x004fb360 | looping: intro, then the body; a stop (not immediate) plays the outro. Not looping: the intro once. player.rs / mission.rs no longer play the `_start` / `_end` pieces themselves |
| Follow-on 0x004f2a80 | on a voice's natural end, the follow-on plays on the same place with the same id (not after a stop) |
| Envelope 0x004f2730 / stop fade 0x004fb360 | `Envelope::level_at / fade_level`; a stop of an enveloped voice fades it (music: 10 s), a segment that follows continues the fade |
| Streamed positional voice out of range (0x004fa210 / 0x004fb1f0) | a one-shot beyond max distance does not start / stops; a loop pauses until back in range |
| Stereo voices cannot play in 3D (0x004fa210) | refused (`refused.stereo_3d`) |
| Game pause pauses every voice (0x004031e0 -> +0xe0 / +0xe4) | `GamePause`: voices pause with `Time<Virtual>`; those started during the pause and the flagged music play on |
| Voice flag 0x8000000 (music play flag) | resolved: the pause 0x004f2d60 skips such voices, so the mission's victory / defeat jingles play through the pause; the objective jingle (cl = 1) pauses |
| Stop-all-but-music 0x004f24e0 | `StopSound(ALL_BUT_MUSIC)` (immediate: no outros) |
| Stop an object's voices 0x004f3170 | `StopSoundsOn(entity)` (level.rs on a death); a followed entity that disappears ends its loop (with its outro) |
| Menu sounds 0x00491760 (group 0: offset -sfx) | `PlaySound::menu`: 0 dB whatever the sfx volume (gamemenu.rs, cheats.rs) |
| Fx opcodes 0x38 / 0x39 / 0x3a (Fx_PlaySound 0x00420080) | 0x38 now plays 2D (gate and house explosions); 0x3a on the player plays 2D through `ListenerBody` |
| 2D on the player (0x004f32a0, 0x0045a480) | `ListenerBody { radius }` on Roman: positional requests on him or within 8 units play 2D (debt: the remaster's callers give positions, not owners) |
| AmbientSounds 0x00500520 | ported as the original runs it: timer starts at 0 (first one-shot at once), counts only while no one-shot plays and the game runs, out of range stops both sounds, 2D with the curve as the voice offset |
| 64 positional voices (0x004fa210) | lifted (docs/engine-limits.md) |
| Characters' voice handles +0xb4c scream / +0xb50 line (0x004383a0, 0x004385c0, 0x00438810, 0x004396a0, 0x00439890) | `audio::Speech` (a grunt's `speech`, ai.rs / topsy.rs): no second scream while one plays (General, Sticky, Topsy), the sticky scream then leaves death_delay as it is; Dead is refused while a scream plays (DeadAtPosition instead); either death line stops the line first; Hurt and Taunt are kept as the line and the taunt waits for a silent line. "Plays" = a voice with the handle, or requested < 0.1 s ago (`PlayingVoices`) |
| Music table 0x005ce4e0 by world (Game+0x2708 = the mission's env_type) | `TRACKS[world]`: l2amb .. l5amb on the later worlds (before: level 1's row everywhere, so no music outside lev_1_*); the bonus levels' worlds 3 / 2 / 4 |
| fx_origin_start_lock (Object_LoadProperties 0x004517b0, every object class) | vehicles/mod.rs `attach_start_lock_fx` for every placed object (before: the bosses only): the shrub bombers' veh_bomber_lp, the pickups' fx_peasant, lanterns, monks, doormats |
| Surface materials of footsteps / impacts (Fx_Create 0x0041cc60 flag 0x800, MeshPart_LoadMaterial 0x004122f0) | surface.rs: the skin materials' names per triangle (export "surface") -> `Physics::surface`; the variant is the first "_" token with an fx, then the object's material property, then the plain fx (before: "snow" everywhere: dirt / grass / stone / wood / shallow water levels stepped on snow) |
| SheepObject (vtable 0x005a8d00: PenguinObject's methods, its own anims and lines) | level.rs `Critter` / charanim.rs `critters`: graze / scan / bleat (before: the template's default anim, sheep_bleat looping: every sheep bleated every 1.6 s); a generic_txt sheep loaded after a start_in_turret mission walks 5..10 s between grazes |

Oracle sound events (engine/src/oracle.rs, tools/oracle_diff.py "events.sounds"): the remaster lists every play call that
reaches the driver's start, as the original's StartVoice log does (before the driver's refusals of far streamed
one-shots and stereo sounds in 3D: the zeppelins' creaks far away are listed in both), from the first frame (the
bank's list was switched on in the trace's first Last pass, after the level's first-frame loops: the pubs'
amb_pub_exterior_lp, the oil pumps' and the world ambient loops were never listed). A sequence's body or outro and
the music's next segment are the sound system's own starts and are not listed (the original's trace does not see
them).

Not ported: the captains' and companions' own voice handles (only the grunt cue sites of ai.rs and topsy.rs keep
theirs; the alarm's Help in alarm.rs has no grunt to hold it); DeadAtPosition's play at a point for the gr1 / gr3 sets
(on the grunt here). The global pitch scale
(time-scale fx 0x72, unused in lev_1_m1). The focused-screen music lock (0x0044f510 / 0x0044f540). All dispositions:
docs/port/audio.csv (`python tools/port_queue.py audio --all`: 0 left).

Tests (tools/regress.py SUITE, engine/playtests): `audio_distance` (curve at 5 / 45 / 100 units, the listener's
body, streamed out of range, stereo refused), `audio_groups` (option volumes on playing sounds, speech / music
groups), `audio_voice` (subtitle, channel busy / equal priority refused, higher priority takes over, follow-on chain,
no follow-on after a stop), `audio_ear` (a Roman-only ear, removal without `reapeat`, the nearest speaker when none is
in range, a streamed line beyond max distance refused), `audio_music` (l1amb, the victory request queued for the
boundary, l1vic through a pause, silence after), `audio_stopall` (intro / body / outro, stop-all-but-music, the game
pause), `audio_ambient` (an emitter's first one-shot at once, the next after the one-shot plus the 5 s delay, stopped
out of range), `audio_mg_bursts` (Roman's machine gun loop in bursts of every length: a stop always ends it, never two loops; a
sequence's intro stays until its body starts, so a stop between them is not lost); unit tests in audio.rs (curve, group levels, envelope, pitch range, the grunt pools last / butlast).
The playtest state log has an `audio` field (started / refused / playing / level_db / paused by name, channels,
group levels, music, ambient emitters) and an `audio` step (play / stop / stop_all_but_music / mixer / music /
pause); an expect value of null means "absent".

## Open questions

- Def flag 0x20 (set on all) and 0x4000000 (music and long ambience): no reader found.
- Which code calls each character voice slot. (Music_Update's thresholds: resolved, section 4.)
- Whether the "segment of the queued track plays twice" reading of 0x00500a00 (section 4) holds at run time. (The
  reader of voice flag 0x8000000: the pause 0x004f2d60, section 8.)
- Which UI screens carry flag 0x2000 (their focus pauses the music and locks Music_Update, 0x004928f0).
- Whether the game clock 0x00604a98 (the 180 s re-arm in Music_Update) counts from level start or app start.
- The audio +0x8c word of the bank (big-aud.md).
- Triggers of the eight untriggered lev_1_m1 lines (section 5).
- Whether the DirectSound 3D listener calls matter on any hardware path (no CTRL3D buffer is ever created).
