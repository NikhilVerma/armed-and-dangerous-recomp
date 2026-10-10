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
