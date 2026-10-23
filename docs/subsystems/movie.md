# Movies (cut scenes)

The game's movies are `GameData/movies/*.pcv`: AVI files with MPEG-4 Part 2 video (fourcc `DXGM`, 512 x 308,
square pixels, 30 fps) and PCM s16le audio (22050 Hz stereo for the story movies, 44100 Hz stereo for the two
logos, 22050 Hz mono for the credits; `Pub_01.pcv` is silent). 26 files, 22 of them with a subtitle table in the
UI bank (docs/formats/big-ui.md). The `.txt` files next to some of them are DivX encoder logs, not subtitles.

## 1. The original

Recovered code: archive/src/movie/movie_01.cpp (the movie object), archive/src/renderer/auto_00503900.cpp (the movie manager),
archive/src/ui/ui_03.cpp 0x00493150..0x004933e0 (the movie screens), archive/src/renderer/render_r4.cpp 0x00493440 / 0x00495cb0
(the draw and the subtitles), archive/src/game/start_level.cpp:120 (the boot list).

**Player.** The movie manager (vtable 0x0058982c, singleton 0x0063a0b8) opens only `.pcv` files
(MovieManager_Open 0x005039f0) as movie objects (vtable 0x0057d598) played through DivxMediaLib.dll:

| Address | What |
|---|---|
| Movie_OpenDivx 0x0050de70 | opens the file, an RGBA frame buffer, the audio stream; seconds per frame = 1 / fps |
| Movie_Update 0x0050e110 | adds the frame time; while 0.9 of a frame ([0x005bcc38]) is due, consumes a frame and decodes it; uploads the last one |
| Movie_DecodeFrame 0x0050e410 | next frame; at the end of the file: ended (flag 1), or back to frame 0 when looping (flag 0x10) |
| Movie_StreamAudio 0x0050e1a0 | streams audio chunks; keeps the audio on the picture by its playback rate (below) |
| 0x0050e740 / 0x0050e3b0 | video time (current frame / fps) / audio time (bytes played / bytes per second) |
| Movie_Draw 0x0050e520, Movie_UploadFrame 0x0050e620 | one textured quad; the frame copied bottom-up into a texture |

Audio follows the picture: d = audio time - video time; outside +-0.05 s the target rate is
1 / (1 + 0.05 min(((d - 0.05) 2.2222)^2, 1)) when the audio is ahead, 1 + 0.05 min(((d + 0.05) 2.2222)^2, 1)
when behind; the rate moves 2.5 % of the way to it each update and becomes the stream's frequency.

**Screens.** "PlayMovie" (flags 0x8000), "PlayMovieNoSkip" (0xc000: 0x4000 = no skip) and "AttractMode" (0x8050)
are movie screens (0x00493150) with a play list ("a,b;c", 0x004932e0 / 0x00493270). Their update (0x004933e0)
starts the next movie when the current one ended or input bits 0x30 (the menu's select / back) are set, and pops
the screen when none is left. The draw (0x00493440) centres the picture with square pixels across the 640-wide
screen, or 0.8 of it with flag 0x8000 (width * 5 / 4 passed to the fit, 0x004937a0), then the subtitle.

**Subtitles** (0x00495cb0): the line at the video time (0x00495c50: binary search, start <= t <= end) of the
movie's table (0x00495bd0 finds it by file name), drawn in its speaker's style from the table 0x005cadd8 (24-byte
entries: name, colour, flags (bit 0 always shown), size, x, y): fitted to 576 units, centred at (x, y) of the screen
(y up). Lines of speakers without bit 0 show only with the movie subtitle option (Game+0x176). Character lines sit
at (0.5, 0.15), in the black band under the picture; `Info` / `InfoHigh` / `InfoLow` / `SmallInfo` captions (place
names, the opening's "Book of Rule" page) are always shown, inside the picture.

**When movies play.**
- Boot: Game_StartLevel 0x00403690 pushes MainMenu, then PlayMovie with `LucasAnim,pm_logo,pre_1_1_opening`
  (0x005bc108), then LegalText (4.0 s), on the first front-end load (shell flag 2). LegalText is on top, so it shows
  first (capture work/shots/origmovA frame 300), then the three movies, then the main menu.
- Level start: MissionObject_Update 0x0044db80 pushes the mission's `start_movie`, except on `lev_1_m1`
  (0x005b1738): a new game goes from the main menu straight to lev_1_m1's fly-in.
- Level end: the end sequence 0x0044a230 pushes Stats, then PlayMovie with `success_movie` (lev_1_m1: none).
- The pub screen (0x004930c0) plays `pub_01` (0x0058fd4c) looped behind its art (no flag 0x8000: full width).
- The credits screen (0x00415940) plays `credits` (0x005b9740) behind the scrolling text.

## 2. Measured on the original

`work/shots/origmovA` (31 frames, 300..4800 every 150 of the hook's fixed 60 Hz clock, the boot with movies on;
`python tools/shot.py --scenario menu --frames 300,450,...,4800 --frames-total 4820 --set test.no_movies=0`
(the frame list written out) reproduces it; the capture was made in a separate instance):
- the picture is 512 x 308 at x 64..575, rows 86..393 of 640 x 480 (1:1 pixels), the subtitles below it;
- 75 movie frames per 150 game frames (the clock is frame time);
- `python tools/movie_compare.py pre_1_1_opening "work/shots/origmovA/*.png" --max-frames 3000`: every capture of the
  opening matches an exported frame (mae 1.5..6 of 255: DivX vs ffmpeg decoding), and at each of the 12 sampled
  times the subtitle band shows text exactly when the table has a line due (`Agh. Better hurry,` at 23.5 s,
  `There! I think that's it.` at 31.0 s, `Ah, cozy!` at 38.5 s, Jonesy's and Q's lines at 46.0 / 48.5 s; none at
  26.0 / 28.5 / 33.5 / 36.0 / 41.0 / 43.5 s).

## 3. The remaster

**Data.** `python tools/remaster_export_movies.py [names]` (ffmpeg) writes `work/remaster/movies/<name>/`:
`video.rmjv` (every frame a baseline JPEG, q 3, with an index), `audio.wav` (as stored), `movie.json` (rate, frame
count, the subtitle lines with their speakers' styles, from the exported UI layout). All 26 movies: 3.3 GB.
`--check` re-reads them.

Why Motion-JPEG: no maintained Bevy 0.20 video crate decodes in pure Rust (bevy_av1: AV1 / IVF; bevy_movie_player:
FFmpeg; bevy_video, bevy_h264: openh264, a C library, Bevy 0.9 / 0.13). zune-jpeg (pure Rust, already in the tree
through `image`) decodes a 512 x 308 frame in about a millisecond; every frame is a key frame, so starting at any
time, skipping and looping are exact. An upscaled set (a later enhancement) only needs a new export.

**Engine.** `engine/src/movie.rs` (game-neutral `MoviePlugin`):
- API: `PlayMovies { names, skippable, looped, view, start_at }` (`PlayMovies::list("a,b")` is the movie screen),
  `StopMovies`, `MovieEvent::{Started, Ended { skipped, missing }, ListDone}`, `MoviePlayer::{busy, takes_input}`,
  the run condition `no_movie_input`, `MovieSettings { dir, subtitles, volume, skip_prompt, skip_text }`;
- `MovieView::FullWindow { width }` draws on its own camera (order 10, layer 6) over a black backdrop; `Behind
  { width, z }` draws on the HUD layer at z (the pub's and the credits' movies);
- the clock, the decode, the audio rate control and the subtitle lookup as the original (section 1), with real
  frame time; a missing movie is passed over (as a failed open); remaster additions: only the last due frame is
  decoded, an audio gap over 0.5 s is closed by seeking, a click also skips, and the skip prompt "<button> Skip"
  (the active device's Select button icon, input.rs `key`) shows for the first 4 s and after the mouse moves;
- the layout keeps the original's 640 x 480 screen scaled to fit the window (centred): the picture 512 units wide
  for the movie screens, 640 for page movies; subtitles are HUD texts (hud.rs) at the speaker's (x, y).

`engine/src/cutscenes.rs` (A&D): the boot list before the title menu (once a session: a level restart,
levelreset.rs, keeps the boot flag; tool runs skip it; a title playtest asks with `"intro": true`), the level's start movie (none on
lev_1_m1), the pub and credits page movies, the game paused during a full-window movie, the options (movie
subtitles = Game+0x176, now the audio page's "Subtitles (cutscene)" checkbox; volume = the master volume, a guess:
the stream is in no volume group the options set). `Cutscenes::end_movie` is ready for the level change once a
next level is built.

Hooks in other modules: input.rs (`contexts`: a full-window movie takes the Menus context), gamemenu.rs (`navigate`
waits for `no_movie_input`; a level restart keeps the boot flag; the movie subtitle checkbox), hud.rs (a text's glyphs
take its entity's render layer), playtest.rs (`movie` steps, `"intro"`, `until: "movie_done"`), main.rs
(`--no-intro`, `--movies DIR`; default `<scene>/../movies`).

**Tests** (tools/regress.py SUITE): `movie_intro` (the boot list in order, frame and audio time in step, the skip
with Enter / the pad's South / a click, the menu untouched after), `movie_subs` (lines due at 23.0 / 30.5 s, none in
the gaps or for the missing sup_021, the always-shown `sup_002` with the option off, a speaker line hidden with it,
the natural end), `movie_screens` (an in-level movie pauses the game and Esc skips it, the pub loop wraps and Enter
does not skip it, a missing movie is passed over), `movie_credits`, `movie_pub`; unit tests in movie.rs (the line
search, the list split) and cutscenes.rs (lev_1_m1's start movie).

Engine against the original, same frame: `tools/movie_compare.py` on the engine's 640 x 480 shots (movie_subs
`rom01006`, `rom01007`): picture mae 0.1..0.5 against the export, the same subtitle lines; side by side with the
original's frames 3150 / 3600 the picture, the subtitle's place, size and colour match; the engine's glyphs are a
little bolder (its 2-pixel shadow, hud.rs) and sit about 1 pixel higher.

## 4. Open questions

- The LegalText splash (b_legal, 4.0 s, timed screen 0x00493520) before the intro movies is not shown: its art
  is not in the exported UI bank.
- The movie stream's volume group (master only here: guess).
- Does the credits screen end with its movie (the base movie-screen update pops the screen when the list ends) or
  does it override slot 1? The remaster leaves the credits page by input only.
- Movie select / Attract mode (the "AttractMode" screen, the extras' movie list, the AMATINEE cheat's unlock) are not
  ported: there is no extras movie page yet.
- The original's skip uses MenuSelect / MenuCancel (input bits 0x30, guess from the screen's update); the remaster
  binds those to Enter, Space, Esc, right click and the pad's South / East / Start.
