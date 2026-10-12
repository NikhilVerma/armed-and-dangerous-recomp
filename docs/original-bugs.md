# Original bugs

Defects found in the original game. Reproduced faithfully and not fixed before M5.

| Bug | Where | Evidence | Reproduced in recovered code |
| --- | --- | --- | --- |
| Game speed and physics may run fast on modern systems | PhysicsManager_Step 0x00411d40 (step raised to 1/60 s) | PCGamingWiki; measured x2.41 at 144 Hz (playground physics_rate, 2026-10-12) | Faithful in src/; the remaster's `[limits] physics_fixed_step` steps 1/60 s from an accumulator (x1.00 at 30-240 Hz) |
| Display mode list has no capacity check (150 entries at Renderer+0x9f4) | 0x00508870 | Disassembly: appends while count grows, no bound | **Deviation:** recovered `Renderer_AddDisplayModes` stops at 150. Unreachable with original filters; reachable once `all_modes` widens them. |
| Resolution lock: only 640-1600 wide, 16:9/4:3/5:4 | 0x00508870 | `cmp edx, 0x280` / `cmp edx, 0x640` and aspect tests | Faithful by default; `[display] all_modes` / `width` widen it. |
| Saved resolution in `SavedGames/options.opt` overrides the start-up size | options loader (not yet located) | 1440x1080 reappeared after a windowed run | n/a |
| lev_1_m1 opening: at frame steps below ~11.8 ms (above ~85 fps) the starting token is collected during the opening camera path; its message pauses the world mid-path, the path never sets "finished", game+0xdce (cinematic) stays 1 and the mission start (0x0044daf0) never runs | token pickup 0x00472a00 (collected flag +0x4bf); camera path end check 0x0040f683; mission start 0x0044daf0 | `[test] fixed_dt_ms` sweep at 30 fps: 33.3/12 ms ok, 10.5/9.5/8.4 ms stall. `tools/watch.py` on the camera slot: path time advances by dt and needs 5.0 s; at 10.5 ms updates stop at 4.46 s (~425 frames) when the token message appears. Why the token is collected early: not yet known (suspect player physics drift at small steps). | Faithful in src/. Cause confirmed 2026-10-12: PhysicsManager_Step 0x00411d40 raises steps under 1/60 s to 1/60 s, so physics ran x2.41 at 144 Hz (playground physics_rate). With the remaster switch `[limits] physics_fixed_step` (hook/enhance/limits.cpp) the 144 Hz run reaches the briefing at frame 720 (5.0 s, the path length) and keeps running; the original switch setting stops at frame 423 (2.9 s) and physics never steps again. |
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
| Third voice set lacks an `r < 400` step: its fourth string gets a double share (r 300-499) | 0x004383a0 | Disassembly | Faithful (src/audio/audio_01.cpp) |
| Death line stops +0xb50 then plays without storing the new handle | 0x0043a7b0 | Disassembly | Faithful |
| An argument slot reused as a bool is pushed as a whole dword with only its low byte set | Fx_PlaySound 0x00420080, Music_Update 0x0044f580 | Disassembly | Faithful |
| Query_Init called without its reset flag leaves query bytes +0x3d/+0x3e as uninitialised stack, read later by the owner-chain walk and collector +0x79 | 0x00414110 -> 0x00413cc0, read in 0x00413e10 | Disassembly | **Deviation:** zero-initialised (src/physics/physics_01.cpp); compare differences here are expected |
| Shape types 0x14-0x18 inside the unwrap loop would loop forever | 0x00413830 | Disassembly | Faithful |
| Counter 0x0064a768 wraps at 1, so +0x47c is always 0 | 0x00405d10 | Disassembly | Faithful |
| Collision skin build writes null+8 / null+0x20 when the mesh shape allocation fails | CollisionSkin_Build 0x00412450 | Disassembly | Faithful |
| Turbo-up pick compares abs(y) instead of abs(x) against abs(z) when abs(x) > abs(y) | 0x00475d50 | Disassembly | Faithful (src/anim/anim_01.cpp) |
| BackgroundObject_Load keeps the fog value in an argument slot that becomes the inner loop counter: from the 2nd mesh the fog flag follows "previous mesh had parts" | 0x0040a890 | Disassembly | Faithful (src/game/game_01.cpp) |
| MissionObject update scans for a nearby object (distance^2 vs 1e8) then discards the result | 0x0044db80 | Disassembly | Faithful (src/game/game_02.cpp) |
| UI text records with an unknown class "ui_text" are not skipped (record stream misread); language lookup reads entry 5 of a 5-entry table when the code is unknown | 0x004629b0; 0x00447600 / 0x00447630 | Disassembly | Faithful (src/ui/ui_01.cpp) |
| GenericAI_Update state 0x34 calls virtual +0xd8 on +0x904 with no null test although it can be 0; state 0x3e calls 0x00481ab0 with +0xb54 unchecked; state 0xd2 uses +0x864 unchecked when 0x00470c30 returns 0 | GenericAI_Update 0x0043ee90 (0x0044120a, 0x00441435, 0x00443bdb) | Disassembly | Faithful (src/ai/ai_06.cpp) |
| Stream allocation failure stores null then writes [null+0x10] | 0x0044ac10 | Disassembly | Faithful (src/loader/loader_01.cpp) |
| Unfound material key / relocation entry dereferenced without a null check | 0x0046ed30, 0x004e1ad0 | Disassembly | Faithful |
| Third event handle (+0x540) never closed | 0x004e6070 | Disassembly | Faithful |
| Block count compares only the low byte of total / block size (a quotient of 0x100 gives 0 blocks) | 0x004e5b30 | Disassembly | Faithful |
| Maximum delay read with the "min_delay" key (0x005b93d8) a second time | 0x00493950 | Disassembly | Faithful (src/audio/audio_02.cpp) |
| Stream name buffer is strlen(path) bytes but the copy writes the terminator one byte past the end | 0x004f49b0 | Disassembly | Faithful |
| AudioDef_Load default opcode skips operands by ecx after a call that clobbers it | 0x004f5010 | Disassembly | **Deviation:** skips by the operand size (the clobbered value cannot be reproduced) |
| Voice lookup / voice pool / music voice +0x90 results not null-checked | 0x004f35e0, play paths, 0x004f3b10 | Disassembly | Faithful |
| ObjectSystem_CreateObject: allocation failure leaves a null object that the shared tail writes through; a template name of 32+ characters overflows a 32-byte local (strcpy); a failed template lookup leaves 0 that the "behavior" fallback dereferences | 0x0045f020 | Disassembly | Faithful (src/game/game_04.cpp) |
| Cone push writes the clamped value into the caller's first stack-argument slot | 0x00471810 | Disassembly | **Deviation:** side effect not reproduced |
| Generator weight slot 4 (num_rocket_captain) has no troop type: logs "Generator : unkown troop type" then passes a stale stack slot as the troop object | 0x00404e50 | Disassembly | Faithful (src/ai/ai_01.cpp) |
| Unknown troop type gives troop 0, then 0x0042cc60 writes null+0x858 | 0x0040c590 | Disassembly | Faithful |
| "ref_spawn_0%d" index subtracts only the ground and window counts (roof and enter slots ignored) (**guess**) | 0x0040c850 / 0x0040c910 | Disassembly | Faithful |
| Damage-flag path reads the damage record before its null check | 0x00450da0 | Disassembly | Faithful (src/game/game_03.cpp) |
| After 0x00487da0 frees an unreferenced object, 0x00458dd0 / 0x00458db0 still run on it (**guess**: use after free) | 0x004547b0 | Disassembly | Faithful |
| Chat word wrap drops the character at the overflow point when a line has no space; a 300+ character message copied with wcsncpy(.., 300) is not terminated | 0x0046aa60 | Disassembly | Faithful (src/ui/ui_02.cpp) |
| Object name + "_in"/"_out" copied into a 0x28-byte stack buffer with no length check | 0x00466580, 0x00466650, 0x004666a0 | Disassembly | Faithful |
| Slider divides by (max - min) without a zero check | 0x0046cae0 | Disassembly | Faithful |
| Missing "fx_origin_start_lock_child" clears descriptor +0x74 instead of +0x18 | Object_LoadProperties 0x004517b0 | Disassembly | Faithful (src/physics/physics_02.cpp) |
| Object_ForceGround bisection never ends if the penetration state never changes | 0x00455860 | Disassembly | Faithful |
| Failed Havok allocation: [shape+8] written through null | 0x004517b0, 0x004527d0, 0x004574e0 | Disassembly | Faithful |
| When 0x004f2030 fails, FindClose runs on whatever the (uninitialised) find record holds | 0x00492d80 | Disassembly | **Deviation:** frame zeroed, so FindClose(0) (src/ui/ui_03.cpp) |
| Shop "affordable" branch re-unlocks the "dearest so far" entry on every iteration | 0x004a0850 | Disassembly | Faithful |
| Slot 4 animation entry dereferenced with no null check when slot 4 holds -1 | 0x00475ff0 | Disassembly | Faithful (src/game/game_05.cpp) |
| Random roll of 0 selects no candidate (dec/je countdown starts at 0 and wraps) | 0x0042bb10 | Disassembly | Faithful (src/ai/ai_03.cpp) |
| Second and third releases test +0xab0 after it was cleared (dead code) | 0x0042c0e0 | Disassembly | Faithful |
| IC-animation lookup failure only logs, then +0xa1c is dereferenced | 0x0042c410 | Disassembly | Faithful |
| Target-position locals read uninitialised when there is no target and a1 is set | 0x0042b480 | Disassembly | **Deviation:** frame zeroed |
| Heap allocation failure leaves the list pointer 0, dereferenced anyway | 0x00481220, 0x00482ab0 | Disassembly | Faithful (src/game/game_07.cpp) |
| Hit direction z overwritten with 0.7 before normalising (possibly intended) | 0x004805f0, 0x00482d60 | Disassembly | Faithful |
| "fire" in state 0x6a: counters +0x5bc / +0x5b8 still decremented when 0x0043ec60 refuses; "fire_alt" tests state 0x6a which its own state list excludes (dead branch) | GenericAIObject_HandleEvent 0x0043d180 | Disassembly | Faithful (src/ai/ai_05.cpp) |
| Null message pointer passes the source test and is dereferenced | 0x004371e0 | Disassembly | Faithful |
| Monk teleport clears flag 4 of +0x484 then re-tests it; the `i >= 5` branch can never run | MonkObject_Bamf 0x00436bb0 | Disassembly | Faithful |
| dz computed and stored, then overwritten by 0x0046e2c0(0.7, 0.9) | 0x0043eae0 | Disassembly | Faithful |
| Turret slots +0x804/+0x808/+0x80c released without a null check (a failed create in 0x0049a6a0 would crash) | 0x0049ac60 | Disassembly | Faithful (src/game/game_08.cpp) |
| Direction z never written before normalising | 0x00497210, 0x004972f0 | Disassembly | Faithful |
| Mount animation and Game+0xac8 state set even when mounting fails | 0x00499bd0 | Disassembly | Faithful |
| Grid cell loop covers cx-2..cx+1 (asymmetric) | 0x00497110 | Disassembly | Faithful |
| Weapon fire reads Game+0x550 / byte Game+0x4c2 where the weapon object has its own +0x550 (muzzle effect): likely the wrong base (**guess**) | 0x004a2a40 | Disassembly | Faithful (src/game/game_09.cpp) |
| One PlayerWeapon variant refills the clip without taking rounds from spare ammo (maybe a deliberate infinite reserve) | 0x004a29d0 | Disassembly | Faithful |
| Matrix translation copied without the null check 0x0049c320 has | 0x0049c9b0 | Disassembly | Faithful |
| Twiglet damage tests the damage-info pointer for null, then reads info+0x2c on the null path anyway | 0x0049f530 | Disassembly | Faithful (src/ai/ai_09.cpp) |
| After dropping the bomb reference (may free it via 0x00487da0), virtual +0xd8 / +0xd4 are called on it (possible use after release) | 0x0049f430, 0x0049fe60, Twiglet_Update 0x0049dcf0 state 0x0a | Disassembly | Faithful |
| Twiglet_Update state 0x0a without a bomb can run the "get bomb out" block twice in one frame | 0x0049dcf0 | Disassembly | Faithful |
| With an old partner in +0xb64, the object's own reference count (edi+0x28, Object_Release(edi)) is decremented instead of the partner's | 0x00486ba0 | Disassembly | Faithful (src/ai/ai_08.cpp) |
| Track lookup: on a first-lookup miss the value pushed from ECX (an otherwise unused local slot) is used as a node pointer, i.e. the caller's leftover ECX | 0x004eb5d0 | Disassembly (anim_02) | Latent: the miss path needs edx != 0, and all five original call sites (0x004426e5, 0x00453de6, 0x00485cf6, 0x0049b4f7, 0x0049efab) pass edx = 0 (capstone). Recovered callers pass the site's ECX where known (anim_01: key; ai_08: ebp+0xab8; game_08: 0 on the no-virtual-call paths), 0 where it is a virtual callee's leftover (ai_06, ai_09) |
| No `from` vector: key B's z taken from direction.z * -0.001 instead of the alt vector | 0x00493d00 | Disassembly | Faithful (src/anim/anim_02.cpp) |
| Audio shutdown frees every definition, then the base shutdown 0x004f2590 calls +0x7c for each index while the table still holds the freed pointers (possible use after free) | AudioPC_Shutdown 0x004fafc0 | Disassembly | Faithful (src/audio/audio_03.cpp) |
| Extended definition allocation zeroes only the first `size` bytes of size + 0xfc | AudioPC_LoadDefinitions 0x004fab80 | Disassembly | Faithful |
| DivX error message formatted into a local buffer and never used | 0x0050de70 | Disassembly | Faithful (src/movie/movie_01.cpp) |
| Save path builder ignores its output-size argument; two _makepath calls and strcat are unbounded | 0x0048b260 | Disassembly | Faithful (src/save/save_01.cpp) |
| String options copied with no length bound; options buffer leaks when the image is empty | 0x00462310, 0x00462610 | Disassembly | Faithful |
| Realloc helper zero-fills its new block without a null check | 0x0048c440 | Disassembly | Faithful |
| First iterator pass never releases its reference; record pointer stops advancing after the first null body | 0x0048e4b0, 0x0048d090 | Disassembly | Faithful |
| A* search: an open node reached by a cheaper path gets new f/g/h but keeps its old parent; a cheaper closed node is re-pushed with unchanged costs | 0x00470240 / 0x004706d0 | Disassembly | Faithful (src/ai/ai_07.cpp) |
| A blocked neighbour that is the goal cell ends the search at the current node's parent, dropping the current node | 0x0046fd20 | Disassembly | Faithful |
| Start node flags ORed into the pooled node's old flags, never cleared | 0x0046f6d0 | Disassembly | Faithful |
| Unknown spawn kind (>3) uses a stale register as the spawned object | 0x00446460 | Disassembly | Faithful |
| PMS_Tree_RemoveNode parent test inverted: with a parent the tree root is overwritten with the child; without one it writes address 4 (crash); the parent's child link is never fixed | 0x004e6d80 | Disassembly | Faithful (src/engine/engine_02.cpp; not executed in recorded runs) |
| BigList_Alloc puts only count-1 items per block on the free list (the last item of each block is never used) | 0x004f6020 | Disassembly | Faithful |
| A capacity of 0 does not stop the copy (the decrement wraps) | 0x004e59d0 / 0x004e5a10 | Disassembly | Faithful |
| Loops forever if the head node is locked | 0x004e72b0 | Disassembly | Faithful |
| File_FindFirst's third search passes the buffer still holding root 2 + pattern (lea esi,[esp+0xc] at 0x004f20a4), so it repeats the second search | 0x004f2030 | Disassembly | Faithful (src/loader/loader_02.cpp) |
| Address-range lookup miss: the code still reads through the null record | 0x004e8620, 0x004eae90, 0x004ef620, 0x004f0e60 | Disassembly | Faithful |
| Streamer creation failure: final check reads [null+0x10]; 0x004f5f10 enters a critical section at null+0x520 | 0x004f5dc0, 0x004f5f10 | Disassembly | Faithful |
| Level .big collector's 0x27d4-byte stack frame | 0x004f5ba0 | Disassembly | **Deviation:** allocated on the heap; SEH frame (0x004f5f10) and stack cookies not reproduced |
| Consumed-action scan skips to the next word plus one when no bit at or above the current one is set: action 0x20 (MoveLiftUp) is never released if the first word is empty | 0x004146c0 | Disassembly | Faithful (src/input/input_01.cpp) |
| D-pad "pressed" fires on the release edge (same test as the released query) | 0x004f8b20 | Disassembly | Faithful |
| Tests input code 0x10006 twice and never 0x10005 | 0x004f87e0 | Disassembly | Faithful |
| Shutdown calls SetCapture instead of ReleaseCapture | 0x004f7c20 | Disassembly | Faithful |
| Any binding set of 1 or more is forced to 0 | 0x004144a0 | Disassembly | Faithful |
| GetDeviceState failure (other than lost / not acquired) reads an uninitialised key buffer | 0x004f8530 | Disassembly | **Deviation:** buffer zeroed |
| Failed allocation leads to a null dereference | 0x00403a30, 0x004e43f0 | Disassembly | Faithful (src/engine/engine_01.cpp) |
| Capacity 0 writes to dst[-1]; count 0 writes before the buffer | 0x00403c10, 0x00462060 | Disassembly | Faithful |
| Random spread multiplied by 0.0 (output is just the position) | 0x0040de50 | Disassembly | Faithful |
| Velocity scaled to unit speed; the limit argument is never applied | 0x00455e70 | Disassembly | Faithful |
| vz not recomputed after the higher-arc time is picked | 0x00471a00 | Disassembly | Faithful |
| Euler matrix sets m[15] = 0 instead of 1 | 0x004e3db0 | Disassembly | Faithful |
| Memory taken from the allocator hook after the parent arena fails is still freed to the parent ("couldn't free block") | 0x004e1e90 / 0x004e1f70 | Disassembly | Faithful |
| Look-at with a zero-length forward vector, and 0x44 bytes of a local block, read uninitialised | 0x004e2eb0 / 0x004e3070, 0x00453b90 | Disassembly | **Deviation:** locals zeroed |
| Pub screen state 6 keeps scale z = 1.0 (3, 3, 1) while state 1 sets (3, 3, 3) | PubScreen_HandleInput 0x004695d0 | Disassembly | Faithful (src/engine/missing_01.cpp) |
