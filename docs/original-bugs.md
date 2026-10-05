# Original bugs

Defects found in the original game. Reproduced faithfully and not fixed before M5.

| Bug | Where | Evidence | Reproduced in recovered code |
| --- | --- | --- | --- |
| Game speed and physics may run fast on modern systems | Unknown | PCGamingWiki | n/a |
| Display mode list has no capacity check (150 entries at Renderer+0x9f4) | 0x00508870 | Disassembly: appends while count grows, no bound | **Deviation:** recovered `Renderer_AddDisplayModes` stops at 150. Unreachable with original filters; reachable once `all_modes` widens them. |
| Resolution lock: only 640-1600 wide, 16:9/4:3/5:4 | 0x00508870 | `cmp edx, 0x280` / `cmp edx, 0x640` and aspect tests | Faithful by default; `[display] all_modes` / `width` widen it. |
| Saved resolution in `SavedGames/options.opt` overrides the start-up size | options loader (not yet located) | 1440x1080 reappeared after a windowed run | n/a |
| lev_1_m1 opening: at frame steps below ~11.8 ms (above ~85 fps) the starting token is collected during the opening camera path; its message pauses the world mid-path, the path never sets "finished", game+0xdce (cinematic) stays 1 and the mission start (0x0044daf0) never runs | token pickup 0x00472a00 (collected flag +0x4bf); camera path end check 0x0040f683; mission start 0x0044daf0 | `[test] fixed_dt_ms` sweep at 30 fps: 33.3/12 ms ok, 10.5/9.5/8.4 ms stall. `tools/watch.py` on the camera slot: path time advances by dt and needs 5.0 s; at 10.5 ms updates stop at 4.46 s (~425 frames) when the token message appears. Why the token is collected early: not yet known (suspect player physics drift at small steps). | n/a |
| fx opcodes 0x5d/0x71 test `cmp ecx, 0x5c` in their shared case, which can never be true there: 0x0047bf80 always runs, 0x0047bf20 is unreachable from this path | Fx_RunScript 0x00420770 (0x00422dde) | Disassembly; jump tables 0x00423250 / 0x00423088 | Faithful (src/renderer/render_r2b.cpp) |
| fx opcodes 0x4f..0x53 with a word count above 4 overrun a 4-dword local (`rep movsd`) | Fx_RunScript 0x00420770 | Disassembly | **Deviation:** copies 4 words; same values, because larger counts take the default branch (all 1.0) |
| fx opcodes 0x38..0x3a with a name count of 0 pass a stale stack slot as the picked name | Fx_RunScript 0x00420770 | Disassembly | **Deviation:** passes null; retail data never has a count of 0 |
| fx opcode 0x07 (pop) with nothing pushed, and 0x6a with a null pushed object, dereference null | Fx_RunScript 0x00420770 | Disassembly | Faithful |
| Texture-animation fade-out divides by the fade-out start (end - out) instead of its length | TexAnim_Step 0x00419d30 | Disassembly | Faithful (src/renderer/render_r1.cpp) |
| Nearest env map ignores the object's vt+0x90 output in the distance | EnvMap_FindNearest 0x00418920 | Disassembly | Faithful |
| Emitter spawn stores uninitialised stack as direction (mode 0x40 without 0x200/0x400) or position (default mode) | fn_0041a050 | Disassembly | Faithful |
| fx sound check compares squared distance with +0x4e4/+0x4e8 but ramps on the square root | fn_0041a350 (fx object update) | Disassembly | Faithful |
| Frame-texture name buffer "%s_%d" is only 32 bytes | TexAnim_FindFrameTexture 0x004198c0 | Disassembly | Faithful |
| Fx shader-flag log passes its two arguments in the wrong order for "(%d)...%s"; particle-uv and hit-ground setters print the wrong messages | 0x0041fdb0, 0x0041f410, 0x0041fa60 | Disassembly | Faithful (the log printf 0x00407ff0 is a bare ret) |
| Missing streamer texture: the log call's return value is used as the texture | Fx_SetStreamerTexture 0x0041f5a0 | Disassembly | Faithful |
| fx allocations are cleared before the null check; Fx_StartOnObject does not check Fx_Create's result | 0x0041cc20, 0x0041eb30, 0x0041dc80 | Disassembly | Faithful |
| Fx_StopEmitter errors when all 10 emitters are active and otherwise writes the slot before the first inactive one | 0x0041e3b0 | Disassembly | Faithful |
| Shadow render-to-texture restores render state 4 from a dword whose top three bytes are uninitialised stack | Shadow_RenderTexture 0x00417f20 | Disassembly | **Deviation:** low byte reproduced; a compare mismatch on this one SetRenderState argument is expected |
| Per-option sub-mesh draw reads +8 of a part pointer that can be null | fn_00410fa0 | Disassembly | Faithful (src/renderer/render_r0.cpp) |
| Decals_Spawn calls Decal_Recycle with ecx = 0 when the active list is empty | 0x004165c0 | Disassembly | Faithful |
| HUD marker projection clears flags on four sprites without a null check | Hud_ProjectMarkers 0x00406840 | Disassembly | Faithful |
| Particle position step reads the velocity it just overwrote: (v+v)*dt/2 instead of the average of old and new | Particles_Draw 0x0046d9f0 | Disassembly | Faithful (src/renderer/render_r3.cpp) |
| UI rect outline changes the state block's flags and texture after it was submitted | Ui_DrawRect 0x0046b2c0 | Disassembly | Faithful |
| Blur-rings effect writes each ring's alpha and then forces it to 0 for the next ring | Post_DrawBlurRings 0x0047d230 | Disassembly | Faithful |
| Hex colour parser keeps storing letters after the 6th with no bound (past 20 they overwrite the stack) | Colour_ParseHex 0x004f0b00 | Disassembly | **Deviation:** stops at 20 (src/renderer/render_r4.cpp) |
| Streamer setter sets render flags 0xc but clears only 4 | 0x004952b0 | Disassembly | Faithful |
| Object group draw permanently ORs bit 0x20 and the owner's bits into the shared shader state | Object_DrawGroups 0x0048a540 | Disassembly | Faithful |
| Vertex normals: a vertex with no faces keeps a stale normal | Skin_VertexNormals 0x004ede80 | Disassembly | Faithful |
| Streamer colour/alpha float-to-int results are not clamped | 0x00495080-0x00495200, Streamer_Draw 0x00495380 | Disassembly | Faithful |
| Skin palette holds 255 bones with no bound check on larger skeletons | Skin_Deform 0x004ed8a0 | Disassembly | **Deviation:** stops at 255 |
| A4L4 pixel accumulate adds `byte << 4` to r, g and b without masking off the alpha nibble | PixelAccumulate_A4L4 0x004f0730 | Disassembly | Faithful (src/renderer/render_r7.cpp; the pixel tables may be unused in this build) |
| A8 pixel pack always stores 0 | PixelPack_A8 0x004f0a90 | Disassembly | Faithful |
| 8888/X888 pixel pack ORs components without masking (out-of-range values spill) | 0x004f07f0, 0x004f0830 region | Disassembly | Faithful |
| A blur target that fails to register decrements the count, leaving the last registered target outside it (never released) | PostProcess_CreateTargets 0x0047c920 | Disassembly (**guess**: possible bug) | Faithful |
