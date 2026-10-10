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
