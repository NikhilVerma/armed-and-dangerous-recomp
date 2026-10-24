# Retexture pipeline

The player's goal (2026-10-10): higher-resolution textures and images that, shrunk back to the original size, look
like the originals. The test: "as the original artists would have made them with modern tools", no new design.

**Tools.** tools/retex/. Images run through the player's Media Lab: C:\video-lab, http://127.0.0.1:8191, OpenAPI
at /openapi.json.

| Tool | Purpose |
| --- | --- |
| medialab.py | API client: queue, wait, download |
| judge.py | PSNR, SSIM and colour shift after shrinking back; detail vs bicubic; crop sheets |
| qwen_round.py | One GEPA-style round: a JSON population of prompt, negative, cfg, steps, seed and strength runs on Qwen. Each is scored on structure (SSIM of both images blurred), colour shift and sharpness, and a crop sheet is written. The agent reads the sheet, diagnoses what drifted and writes the next round. |
| lowfreq.py | Low-frequency lock: the candidate's fine detail on the original's broad colour and values |
| dering.py | Clamps the upscale to the local range of the smooth upscale: no halos |
| backproject.py | Shrunk to the target size, the result reproduces the target |
| upscale_texture.py | The faithful-only pipeline (Nomos2, dering, backproject) for bulk textures |

**Overrides.** Results go to `overrides\textures\<name>.png`, in the texture's stored orientation (the game's DDS
rows are upside down). `[overrides] textures` is on by default in the remaster. The images are game-derived and are
never committed.

## Main menu (AnD_title, 1024x512 -> 4096x2048)

- **Faithful upscalers alone** (round 1). Nomos2 DAT2 was the best: shrunk back 46.6 dB, SSIM 0.998; after
  back-projection 52.9 dB. The player rejected it: "the guns look blurred still". An upscaler cannot recover detail
  the 1024px original never had.
- **Diffusion upscalers** (clarity-free, upscale-creative). They redraw content: a different winged lion, a double
  barrel for the triple barrel. 28-29 dB.
- **Qwen Image 2.1 re-render at source size, prompt and guidance search** (work/retex/rounds/r1..r4):
  - r1: cfg above 1 drifts colour (shift 17-28 of 255) and the direction depends on the prompt. A short "restoration"
    prompt that limits new detail to the objects and keeps flat areas flat is the most faithful (struct 0.99, shift
    0.65).
  - r2: specific weapon detail words raise sharpness, but "blued steel" tints the metal blue and "brass" makes a gold
    mace.
  - r3: colour-safe metal words remove the blue and gold, but "dark gunmetal" darkens the light-grey cannon. Some
    seeds drop a barrel.
  - r4: "every object keeps its own colour and brightness from the source" fixes the cannon. P6, seed 7, cfg 1.0,
    40 steps: struct 0.978, shift 3.66, sharp 1.63.
  - The low-frequency lock (sigma 2) then gives struct 0.997, shift 0.21, sharp 1.60.
- **Upscale of the Qwen render.**
  - Nomos2 4x directly amplifies a faint diagonal latent-grid pattern of the Qwen output into visible crosshatching.
    Rejected.
  - clarity-guided 2x (Qwen result + original), then Nomos2 2x, then dering, then back-projection to the locked Qwen
    render: clean flat areas, crisp guns, cannon and lion.
  - Installed and seen in game at 4K (work/retex/final_*.png).
- The P6 prompt is in work/retex/rounds/r4.json. Next textures should start from it and from cfg 1.0.

## Tiled Qwen reconstruction (current method, 2026-10-10)

Findings on the cannon tile (work/retex/t1). "Overshoot" is edge contrast after shrinking back, relative to the
original; about 1.0-1.3 reads natural.

| Variant | Overshoot | Result |
| --- | --- | --- |
| Base: HAT-L 4x of the Qwen 1024 render, Qwen at 0.7 | 1.83 | Halos, thick outlines |
| "Sharper / crisp" prompts | about 1.55 | Kept the base's woven grid |
| Base: Lanczos 4x of the original, Qwen at 0.85 | 0.98 | Faithful but soft: partial strength keeps the blur |
| **Base: Lanczos 4x of the original, Qwen at 0.95** | **1.20** | **Reconstructs** casting marks, bores, wood grain and chipped paint, shapes kept |
| Same base, Qwen at 1.0 | 1.26 | Starts reshaping small parts |

Partial strength keeps whatever the base has. Start from the plain original, never from a sharpened image, and
give Qwen room to rebuild (0.95). The prompt asks for reconstruction ("a blurry enlargement ... reconstruct the
detail the original painting would have had ... do not sharpen") and the negative names oversharpening, halos,
outlines and grid patterns.

### Experimenting by hand (Media Lab UI)

- **Tile input:** work/retex/t1/base_orig.png. It is the cannon region of a Lanczos 4x of the original, 1216x704.
  Any 1216x704 crop of a 4x resize works the same way.
- **Settings:**

  | Setting | Value |
  | --- | --- |
  | Engine | Qwen Image 2.1 (`qwen-image-21`) |
  | Output size | source |
  | Edit strength | 0.95 |
  | cfg | 1.0 |
  | Steps | 30 |
  | Seed | 7 |

- **Prompt and negative:** the PROMPT and NEGATIVE constants in tools/retex/qwen_tiles.py, also in
  work/retex/t1/prompt.txt.

### Whole image (script)

```
.venv/Scripts/python.exe tools/retex/qwen_tiles.py work/retex/title_up.png OUT.png --grid 4x4 --scale 4 --overlap 256 --strength 0.95 --sigma 6 --steps 30 --blend seam [--prompt-file P.txt] [--negative-file N.txt] [--seed 7]
```

- Tiles are cached in OUT_tiles/: a re-run with the same OUT reuses finished tiles. Use a new OUT name after
  changing the prompt.
- **Blending:**
  1. Each tile is colour-locked to the base (sigma 6).
  2. Overlaps (256 px) are joined along the least-difference path, excluding each tile's outer 64 px (Qwen is not
     edge-aware), with a ±12 px feather.
  3. Rows first, then the row strips.

## Policy and method from 2026-10-10 evening (the player)

- **Policy.** Every texture whose upscaled size fits Qwen's single-image limit (2048x2048 with
  `qwen_output_size=custom`) is remastered with Qwen in one pass. Larger ones wait ("take a stock and figure out
  later"). Some creativity is accepted, "as long as it's not a complete crapshoot".
- **Settings** (the player's), input = the original texture PNG, transparency included:

  | Setting | Value |
  | --- | --- |
  | Engine | qwen-image-21 |
  | Output size | custom, at the final size (e.g. 2048x2048, or 1024x2048 for 1:2 portraits) |
  | Edit strength | 1.0 |
  | cfg | 2 |
  | Steps | 25 |
  | Seed | 0 |

- **Prompt:** "Convert this video game asset into an upscaled realistic version of its original intention using
  modern physically based material rendering, correct shadows, correct lighting, accurate details without modifying
  the actual contents of the image itself".
- **Negative:** altered, reinterpreted, restyled, redesigned, recolored, relit, reframed, cropped, added or removed
  elements, invented or hallucinated detail, added texture, grain, pattern or outlines, changed texture, material,
  medium, proportions or text, garbled text, upscaling and compression artifacts, oversharpening, halos, ringing,
  over-smoothing, watermark, signature.
- **Findings:**
  - `custom` at the final size keeps the original's colours and design far better than `model` (about 1 MP). In
    `model` mode Q's gold teeth turned white and the face dark steel.
  - `custom` keeps transparency natively; `model` returned an opaque black background.
  - Portraits: Q (b_q_face, 2048), Jonesy (b_jonesy_face, 1024x2048, from 64x128) and the monk (b_monk_face, seed 0
    of 2) were installed. Q and Jonesy were seen on the level-1 HUD at 4K.
  - Bark (B_T_tree_bark_1) gains detail but is recoloured warm brown to grey: materials whose colour carries the
    look need a colour lock or prompt work. Not installed.
  - Results and screenshots: OneDrive\AnD-retexture-test\results-characters (the shared folder with the player).

## Image-specific descriptions (2026-10-10, the player: "slower but way better")

Prompt = the generic prompt + a description of the image (tools/retex/qwen_assets.py; specs in
work/retex/specific/spec_r*.json). Same seed, so each comparison isolates the description:
- **Colour drift fixed.** Bark returns to warm brown (generic: grey). Q returns to white metal with a blue sheen and
  gold teeth.
- **Invented elements removed.** The grey stones behind the compass ring are gone.
- **Lighting matters as much as parts.** Jonesy round 1 (parts only) came out evenly lit and pale, with big lips.
  Round 2 ("lit from above, face mostly in shadow, thin mouth with a gold highlight") matches. Likewise the monk's
  black eye band. A description should cover subject, parts, colours, lighting and contrast, and transparency.
- **Repeating textures lose their wrap** (bark edge mismatch 18 vs neighbours 8). tools/retex/tileable.py
  (offset blend) restores it (7.5). A 2x2 repeated input to Qwen would be better.
- **Scale-up.** Later the descriptions can come from cheap Haiku subagents, one per texture (the player's plan).
- **Installed and seen in game at 4K:** Q, Jonesy, compass, bark (and the monk, from levels 4-1/4-2). Results:
  OneDrive\AnD-retexture-test\results-specific-prompts.

## Batch (2026-10-10/11, /goal: upscale as many textures as reasonable)

tools/retex/batch.py, with prepare / run / post / sheet and reject.py.
- **Inventory:** 524 unique texture names across the 25 levels. Same name means same image (md5), so one override
  serves every level.
- **Sizes:** 4x while the result stays <= 1024 per side, otherwise 2x (max 2048), smallest first. 2048x2048 jobs take
  2.5-5 min each on the shared 4090.
- **Descriptions** come from Haiku subagents (about 25 previews each). They also classify each texture and skip glows,
  smoke, fire, flat colours, normal maps, text and many-piece sheets.
- **Results:** 212 installed, 253 skipped, 36 rejected after contact-sheet review and in-game checks, 14 deferred
  (1024 px sources).
- **Post-processing lessons:**
  - Re-apply the original alpha.
  - Bleed colour into soft alpha edges from Qwen's opaque pixels (purple fringes otherwise).
  - Do NOT bleed when the original alpha is graded everywhere (n_peasant: HUD figures washed out).
  - Colour-lock every kind to the original's broad colours (sigma 2 original px for surfaces, 3 for others).
    Without it, dark materials got darker or grey/blue, and a bush turned autumn orange (shift 67 -> < 20).
  - Make repeating textures seamless by an edge-matching ramp. The offset blend moved a keg's skull into the
    corners.
- **Rejection patterns:**
  - Layout changes on 3D-model texture sheets (UVs break).
  - Lost fine regular patterns (weave, small bricks).
  - Sources under 16 px (Qwen invents content).
- **In game:** levels 1-5, 149-160 overrides loaded each, no errors. Screenshots:
  OneDrive\AnD-retexture-test\batch.
- **Hazards:**
  - Describing subagents overwrite exclusion files, so run() re-asserts the hand-tuned and lettered textures every
    pass.
  - Names differing only in case (N_T_iron / N_t_iron) share one file on Windows. All such pairs hold identical
    images.

## Proposed recipe (2026-10-11, agreed with the player; not yet run as a batch)

Found by sweep (tools/retex/sweep.py, results in work/retex/batch/sweep/, montages in
OneDrive\AnD-retexture-test\settings-test\) on wood_crate, b_grunt_front, k_player_rifle, b_stone_dark,
x_dirtrocks1, k_pub_sign, b_a_leaves_1, fx_smoke_brown:

1. Canvas: SwinIR-L realSR 4x (`003_realSR_BSRGAN_DFOWMFC_s64w8_SwinIR-L_x4_PSNR.pth`, Media Lab upscale-faithful)
   of the original, resized (Lanczos) to the target size, original alpha resized in. It removes the DXT/compression
   blocks; HAT-L and Nomos2 canvases sharpen them and Qwen then paints them as specks (crate, rifle).
2. Qwen Image 2.1 with that canvas as the only image: qwen_edit_strength 0.9, image_cfg 7 (5 = slightly more
   faithful; the player measured 10 fine, 20 broken), 20 steps, seed 0, qwen_output_size source, no negative,
   the player's prompt: "Enhance this image to high resolution with rich fine details. Sharpen textures and surfaces,
   add natural microdetails, and improve clarity and definition across the image while preserving the original
   composition, lighting, atmosphere, and visual style. The input may be an illustration or a real photograph;
   preserve the original style."
3. No per-texture descriptions: Haiku descriptions were sometimes wrong and Qwen follows the text (x_dirtrocks1 was
   painted as orange dirt with rock piles at every strength).
4. Post: batch.normalise_lighting then batch.normalise_saturation (broad luminance and chroma from the original,
   hues and detail from Qwen); fidelity gate (sweep.fidelity, alpha-weighted SSIM at the original's size) as a
   safety net; skip normalisation when the gate fails.
5. Normal maps: their own fixed prompt (batch.NORMAL_PROMPT), renormalised in post.

Measured (fidelity / detail vs Lanczos): b_grunt_front 0.93-0.95 / 1.6-1.7, wood_crate 0.93-0.95 / 2.5-3.4,
k_player_rifle 0.73-0.75 / 3.0 (alpha cut-out; visually exact). Rejected on the way: strength 1.0 from the small
original (redraws layout), Lanczos canvas (stays blurry at <=0.95, drifts at cfg >= 7 on UV sheets), canvas +
original as a second reference (no gain), cfg 2 (adds little).

### Recipe as agreed (2026-10-11 evening, supersedes the SwinIR canvas above)

The player's settings merged with the sweep: Media Lab jobs 20261011-175746-526642 and 20261011-180026-97e788.

1. Canvas: SeedVR2 `seedvr2_model=3b`, `seedvr2_color=none`, no blend, `seedvr2_scale` = target / original (exact
   target size), original alpha kept.
2. Qwen Image 2.1 with TWO images: the SeedVR2 canvas first (size, layout), the original low-res texture second
   (the sharp reference). qwen_edit_strength 0.9, image_cfg 5 (the player: "5 seems to be the best subjective
   strength"), 25 steps, qwen_output_size source, no negative, the player's "Enhance this image..." prompt unchanged.
3. Colour and lighting correction afterwards as a separate pass (batch.normalise_lighting / normalise_saturation),
   not part of the generation.
4. Normal maps: own prompt (batch.NORMAL_PROMPT).

## Data textures and small originals (2026-10-12)

- Terrain blend masks (b_a_blend*), lightmaps (*_ground_light, *_lightmap_*) and ground masks are data, not pictures:
  they stay original (recipe EXCLUDE). Upscaled blend masks made the dirt layer show as hard dark smears in the snow.
- The "under 32 px stays original" rule is checked on the shortest side: 16-px tracer strips (fx_trail_*) enlarged 16x
  had green fringes and black halos. 25 overrides were removed (list in work/retex/excluded/removed_2026-10-12.txt).
- `[overrides] exclude=name;name` leaves single textures original for an A/B in the playground; `uv_checker=` swaps
  them for a UV grid to find which texture a surface uses.

## Engine conversion: the low-frequency lock (2026-10-24)

tools/remaster_textures.py (the overrides made ready for the engine) locked every override texel-exactly to the
original until 2026-10-24: cand + up(orig - down(cand)). That keeps only detail finer than one original texel, so a 2x
override (originals of 512 px and more: the lev_5_m2 ground l5_m2_ground_1/2, rock faces) came out as the original
upscaled, the human: "floor textures are not swapped at all". It now uses this document's low-frequency lock (sigma
2 original texels, blurred at the original's size: cand + up(blur(orig - down(cand)))), falling back to the
texel-exact lock where the result's shrunk-back shift exceeds 20 (effects, sprites, glyphs whose shapes moved); the
fidelity gate (12) applies to the exact lock. Of 24 levels' ~6,200 converted textures, about three quarters take the
low-frequency lock. Every texture a material uses as its blend mask is data and stays original (l5_m2_ground_m was
enlarged by the AI before: the name rule knew only `*_mask`).

Regenerated with the recipe's faithful route (SeedVR2 7b, recipe.finish) for the textures failing the gate:
fx_glass_shattered now passes (shift 10.8, work/retexfix/overrides_new); b_lbutton, b_rbutton, b_weapons,
n_a_t_fencing still fail (hard-edged glyphs and strips: bicubic up and box down are not inverses at their edges), and
fx_parachute_strings' upscale lost its thin strings (structure 0.43). work/retexfix/coverage.md lists the rest:
originals under 32 px (the recipe's MIN_SIDE), data, normal maps, the font.

## Walk-video fixes (2026-10-25, findings B1 / B3 / B5, work/texfix/)

- **Effect textures stay original**: the bank's premultiplied (DXT2) textures and the fx_* / fire_* families, unless
  a world model (or its texture2) uses them. Drawn additively, a retexture's dark end showed multiplied over a dark
  scene: fx_wind_spray, the lev_5 storm's spray dome, drew teal blotches with stair-step edges over everything (B1).
  The audit of every level's fx retextures also found redrawn rims and seams: fx_glow_red's black ring, fx_flash_2's
  bright edge column, fx_wing_stream's wrap edges 96 -> 5-9 (a dark seam in the scrolling trail), fx_m_muzzle_flash's
  edge column.
- **BC blocks from tools/bcn.py** instead of Pillow's encoder (a third of a dark texture's blocks in BC1's 3-colour
  mode, loose endpoints: green / teal steps in dark gradients); 4-colour mode, principal-axis endpoints refined by
  least squares (fx_wind_spray: mean error 1.77 -> 1.15, chroma 1.27 -> 0.88).
- **tools/data/retexture_rejects.json**: overrides judged wrong in play, with the evidence; `original` keeps the
  original (b_level1_clouds: the river surface of lev_4_m3 painted with white blobs, B3), `exact` takes the
  texel-exact lock (2m1_ground3, b_stone: the low-frequency lock kept their brushed streaks / leathery folds, the
  streaky rock of lev_3_m1 / 3_m2, B5; pack v1's exact lock did not). Regenerate them with the recipe, then drop
  their entries.
- A change of the script, bcn.py or the rejects file makes every output stale: re-run each level (not done on the
  shared work/remaster/*/textures_remaster on 2026-10-25; private copies in work/texfix/scenes).
