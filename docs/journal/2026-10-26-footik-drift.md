# 2026-10-26: foot IK drift

Bug: `footik::place_feet` added the body's drop to the local z of every root bone each frame. A root nothing animates
(Roman's ref_q_00, ref_molmer_00) kept the drop and sank ~6 units/s (~130 below him by t = 11 s), standing still.

Original (0x00477570): the drop is not a per-bone add. `fn_00457820(f+0x10, actor)` sets the model offset
(0, 0, lowest x 0.75 - 0.1 + bob) afresh each frame; nothing accumulates.

Fix: the drop is added only to the root of the leg chain (the hip's topmost ancestor, which the animation rewrites
every frame). Other roots are left as animated.

Test: `engine/playtests/foot_ik_drift.json` stands 11+ s and expects the new readout `ref_q_z` (ref_q_00's local z)
to stay in [-5, 5] and `foot_drop` at -0.1. Passes (ref_q_z 0.0).

Open: unanimated roots no longer follow the drop (<= 0.1 on flat ground, up to ~1.9 on steps); the original moved
the whole model. If it shows, parent such bones under the chain root rather than re-adding the drop.
