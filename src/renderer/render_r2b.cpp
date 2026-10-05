// Recovered: the fx script interpreter. S1 (pending compare), Approach B.
//   0x00420770 Fx_RunScript
//
// Evidence: strings "Fx System got unknown opcode for fx\n%s\nop %d last op %d" (0x005b7e7c),
// "Fx : Invalid lifeSpan range (using min)\n%d > %d" (0x005b7eb4), "Fx : Pushing object w/o a previous Pop\nFx
// %s\nObject %s" (0x005b7ee4); the script format in docs/formats/big-fx.md; only caller 0x00423360 (the fx update,
// state 0 after the start delay).
//
// Frame (original, B = esp after `push ebp` at 0x004207f8): ebp = script pointer; [B+0x10] = top node of the
// object push stack (node = {next, object}; also the `edi` of most callees); [B+0x14] (= esi) = current object;
// [B+0x6c] = first node of the push stack; [B+0x1c] / [B+0x2c] = first / second name list (opcode 0x104);
// [B+0x22] = stop byte, [B+0x23] = finished byte (the return value); [B+0x94] = current opcode; edx = previous
// opcode (-1 before the first). Each loop iteration reloads edi = [B+0x10] and ebx = arg0 (0x00420822).
//
// Dispatch (0x00420830..0x00420852): op = u32 at ebp, ebp += 4; op - 4 > 0x100 -> unknown; else
// movzx eax, byte [op - 4 + 0x00423250]; jmp [eax*4 + 0x00423088]. The jump table (read from the image) maps
// every opcode to its own case except 0x4f..0x53 (one case, then a compare on the opcode), 0x5c/0x70 and
// 0x5d/0x71 (one case each, compare on 0x5c), and 0x7a..0x103 (unknown). The switch below uses the opcode values.
//
// Operand notation: w = u32 word, s = string slot ((strlen + 0x20) & ~0x1f bytes), L = u32 count + count strings,
// W = u32 count + count words. r(a,b) = 0x0046e2c0 random float in [a,b] (rand()); "cur" = current object,
// "top" = top node of the push stack, "obj" = top->object (+4), "mgr" = arg0 (fx manager), "fx" = arg1 (fx
// instance), "list1"/"list2" = name lists set by 0x104. "if cur" = the case does nothing (operands still
// consumed) without a current object. Game = *(0x005fe490).
//
// | op   | operands  | action                                                                  | callee(s)          |
// |------|-----------|-------------------------------------------------------------------------|--------------------|
// | 0x04 | -         | end: stop + finished                                                    |                    |
// | 0x05 | s         | create object `s`, it becomes cur                                       | 0x004234d0         |
// | 0x06 | -         | if cur: push cur (log when the stack is not empty)                      | 0x00423430         |
// | 0x07 | -         | if cur: release obj (0x004540a0), pop, cur = 0                          | 0x004540a0 0x423470|
// | 0x08 | w w       | life span r(min,max) (log + max = min when max < min); if cur           | 0x0041d110         |
// | 0x09 | w w w w   | r(w0,w1), r(w2,w3); if cur                                              | 0x0041dba0         |
// | 0x0a | w w       | r(w0,w1); if cur                                                        | 0x0041dc30         |
// | 0x0b | w w       | r = r(w0,w1); if cur: (mgr, r, r)                                       | 0x0041d170         |
// | 0x0c | w w w w   | r(w0,w1), r(w2,w3); if cur                                              | 0x0041d170         |
// | 0x0d | w w w w   | r(w0,w1), r(w2,w3); if cur                                              | 0x0041d210         |
// | 0x0e | w w w w   | r(w0,w1), r(w2,w3); if cur                                              | 0x0041d2c0         |
// | 0x0f | s         | if cur: start fx `s` (list1, list2, 0, 0); clears both lists            | 0x0041dd50         |
// | 0x10 | w w       | r(w0,w1); if cur: point from list1 (0x0041cba0); clears list1           | 0x0041e0c0         |
// | 0x11 | w w       | r(w0,w1); if cur                                                        | 0x0041de10         |
// | 0x12 | w w       | pause r(w0,w1) (always), stop                                           | 0x0041dea0         |
// | 0x13 | s         | create attached object `s` at a point of list1, becomes cur; clr list1  | 0x00423680         |
// | 0x14 | -         | if cur                                                                  | 0x0041df20         |
// | 0x15 | -         | wait (always), stop                                                     | 0x0041dee0         |
// | 0x16 | -         | if cur: set invisible (obj vtable +0xf4)                                | 0x0041e280         |
// | 0x17 | s w w     | r(w0,w1); if cur: (fx, s, r, 0, 0, 0x80)                                | 0x0041e4d0         |
// | 0x18 | s w w     | r(w0,w1); if cur: (fx, s, r, list1, 0, 0x40); clears list1              | 0x0041e4d0         |
// | 0x19 | s w w     | r(w0,w1); if cur: (fx, s, r, list1, list2, 2); clears both lists        | 0x0041e4d0         |
// | 0x1a | w*6       | r(w0,w1), r(w2,w3), r(w4,w5); if cur                                    | 0x0041f8f0         |
// | 0x1b | s w*6     | create fx `s` at (r(w0,w1), r(w2,w3), r(w4,w5)) (always, flags 0x28)    | 0x0041cc60         |
// | 0x1c | w w       | r(w0,w1); if cur                                                        | 0x0041f970         |
// | 0x1d | s         | if cur                                                                  | 0x0041fd10         |
// | 0x1e | w w       | r(w0,w1); if cur: point from list1 (0x0041cba0); clears list1           | 0x0041dfb0         |
// | 0x1f | w w w     | r(w1,w2); if cur: 0x0041e760(.., w0, r, 0, 4, 0, 0)                     | 0x0041e760         |
// | 0x20 | w w       | if cur                                                                  | 0x0041e9f0         |
// | 0x21 | w w       | if cur                                                                  | 0x0041ea40         |
// | 0x22 | w w       | if cur                                                                  | 0x0041ea90         |
// | 0x23 | w w       | if cur                                                                  | 0x0041f340         |
// | 0x24 | w w       | if cur                                                                  | 0x0041ee50         |
// | 0x25 | w w       | if cur                                                                  | 0x0041eae0         |
// | 0x26 | w w       | if cur                                                                  | 0x0041f2f0         |
// | 0x27 | w w w     | r(w1,w2); if cur: 0x0041e760(.., w0, r, 0, 0x20, 0, 0)                  | 0x0041e760         |
// | 0x28 | w w w     | r(w1,w2); if cur: 0x0041e760(.., w0, r, list1, 0x40, 0, 0); clr list1   | 0x0041e760         |
// | 0x29 | w w w     | r(w1,w2); if cur: camera (Game+0x748 vt +0x54) pos, z += [0x005bcb68]   | 0x0041e760         |
// | 0x2a | w w       | r(w0,w1); if cur                                                        | 0x0041e3b0         |
// | 0x2b | w w w w   | if cur                                                                  | 0x0041f3b0         |
// | 0x2c | w w w w   | if cur                                                                  | 0x0041f410         |
// | 0x2d | w*9       | r(w1,w2); if cur: 0x0041e760 with {w3,w5,w7}, {w4,w6,w8}, {0,0,1}       | 0x0041e760         |
// | 0x2e | -         | if cur: flag 0x400                                                      | 0x0041fd70         |
// | 0x2f | s         | if cur: start fx `s` (list1, list2, 1, 0); clears both lists            | 0x0041dd50         |
// | 0x30 | s w w     | r(w0,w1); if cur: (fx, s, r, list1, list2, 1); clears both lists        | 0x0041e4d0         |
// | 0x31 | w*6       | r(w0,w1), r(w2,w3), r(w4,w5); if cur                                    | 0x0041f850         |
// | 0x32 | -         | if cur                                                                  | 0x0041faf0         |
// | 0x33 | -         | if cur                                                                  | 0x0041fc20         |
// | 0x34 | -         | if cur                                                                  | 0x0041fbd0         |
// | 0x35 | s w w     | if cur                                                                  | 0x0041fc70         |
// | 0x36 | s w*8     | r(w0,w1); if cur: {w2,w4,w6}, {w3,w5,w7}, lists, 0x100; clears lists    | 0x0041e610         |
// | 0x37 | s         | create object `s`, becomes cur; clears list1                            | 0x004234d0         |
// | 0x38 | L w       | pick one name at random (always): (mgr, 0, 0, 0, w)                     | 0x00420080         |
// | 0x39 | L w       | pick one name; if cur: point of list1, (.., 1, 0, w); clears list1      | 0x0041cba0 0x420080|
// | 0x3a | L w       | pick one name; if cur: point of list1, (.., 1, 1, w); clears list1      | 0x0041cba0 0x420080|
// | 0x3b | w w w     | if cur                                                                  | 0x004201f0         |
// | 0x3c | -         | if cur                                                                  | 0x0041fa60         |
// | 0x3d | -         | if cur                                                                  | 0x0041f9d0         |
// | 0x3e | -         | if cur                                                                  | 0x00420260         |
// | 0x3f | s w*4     | always (top tested by the callee)                                       | 0x0041ff60         |
// | 0x40 | w*6       | if cur: uv scroll setup (weather.md)                                    | 0x00420670         |
// | 0x41 | w*5       | if cur: {w1,w3}, {w2,w4}, 0, 0                                          | 0x004202b0         |
// | 0x42 | w*6       | if cur: {w1,w2}, {w3,w4}, w5, 1                                         | 0x004202b0         |
// | 0x43 | w*6       | if cur: {w1,w2}, {w3,w4}, w5, 3                                         | 0x004202b0         |
// | 0x44 | w*6       | if cur: {w1,w2}, {w3,w4}, w5, 2                                         | 0x004202b0         |
// | 0x45 | w*5       | if cur                                                                  | 0x00420450         |
// | 0x46 | w w w     | if cur                                                                  | 0x0041eef0         |
// | 0x47 | w w w     | if cur                                                                  | 0x0041f0f0         |
// | 0x48 | w w       | if cur                                                                  | 0x0041eea0         |
// | 0x49 | w w w     | if cur                                                                  | 0x0041eb70         |
// | 0x4a | w w w     | if cur: {w0,w1,w2}, list1; clears list1                                 | 0x0041da90         |
// | 0x4b | -         | if cur: flag 0x2000000                                                  | 0x0041fd70         |
// | 0x4c | -         | if cur: flag 0x4000000                                                  | 0x0041fd70         |
// | 0x4d | -         | if cur: 0x0041f500(...)->+0x58 = 0                                      | 0x0041f500         |
// | 0x4e | -         | if cur: 0x0041f500(...)->+0x58 = point of list1; clears list1           | 0x41f500 0x41cba0  |
// | 0x4f | W         | 4 floats (1 -> x,x,x,x; 2 -> lerp; 4 as is; else 1,1,1,1); if cur       | 0x0041f6d0         |
// | 0x50 | W         | as 0x4f                                                                 | 0x0041f710         |
// | 0x51 | W         | as 0x4f                                                                 | 0x0041f750         |
// | 0x52 | W         | as 0x4f                                                                 | 0x0041f790         |
// | 0x53 | W         | as 0x4f                                                                 | 0x0041f7d0         |
// | 0x54 | w         | if cur                                                                  | 0x0041f810         |
// | 0x55 | s         | if cur                                                                  | 0x0041f5a0         |
// | 0x56 | w*4       | if cur: {w0..w3}                                                        | 0x0041f640         |
// | 0x57 | w         | if cur                                                                  | 0x0041f690         |
// | 0x58 | w w w     | if cur: {w0,w1,w2}                                                      | 0x0041d8f0         |
// | 0x59 | w w w     | if cur: {w0,w1,w2}                                                      | 0x0041d980         |
// | 0x5a | w         | if cur                                                                  | 0x0041da10         |
// | 0x5b | w*4       | if cur && obj: obj+0x634 = obj, {w0..w3} (Game+0xa9c)                   | 0x0047be00         |
// | 0x5c | w         | if cur && obj: obj+0x634 = obj, &w0                                     | 0x0047be60         |
// | 0x70 | w         | as 0x5c                                                                 | 0x0047bec0         |
// | 0x5d | w*4       | if cur && obj: obj+0x634 = obj, {.5,.5,w0,w1,.5,.5,w2,w3}               | 0x0047bf80         |
// | 0x71 | w*4       | as 0x5d (the 0x5c compare never matches: always 0x0047bf80)             | 0x0047bf80         |
// | 0x5e | w w w     | if cur && obj && obj+0x634                                              | 0x0047c1a0         |
// | 0x5f | w w       | if cur && obj && obj+0x634                                              | 0x0047c0a0         |
// | 0x60 | -         | if cur && obj && obj+0x634: release, obj+0x634 = 0                      | 0x0041bd30         |
// | 0x61 | w w       | if cur && obj && obj+0x634                                              | 0x0047c140         |
// | 0x62 | w w       | if cur                                                                  | 0x0041fe00         |
// | 0x63 | s w*4     | if cur                                                                  | 0x0041e310         |
// | 0x64 | w*6       | r(w0,w1), r(w2,w3), r(w4,w5); if cur                                    | 0x0041e1e0         |
// | 0x65 | w w s     | if cur && obj: obj pos (vt +0x54), texture lookup (result unused)       | 0x004881c0         |
// | 0x66 | -         | if cur                                                                  | 0x0041e990         |
// | 0x67 | w*7       | if cur: w0, {r(w1,w2), r(w3,w4), r(w5,w6)}                              | 0x0041d360         |
// | 0x68 | w*7       | if cur: as 0x67                                                         | 0x0041d440         |
// | 0x69 | w*7       | if cur: as 0x67                                                         | 0x0041d560         |
// | 0x6a | w s       | if cur && top: decal size w0, texture s at obj (vt +0x60), Game+0xa7c   | 0x004165c0         |
// | 0x6b | w*5       | if cur: {w0,w1,w2}, w3, w4                                              | 0x0041d640         |
// | 0x6c | w*5       | if cur: {w0,w1,w2}, w3, w4                                              | 0x0041d700         |
// | 0x6d | w*5       | if cur: {w0,w1,w2}, w3, w4                                              | 0x0041d830         |
// | 0x6e | -         | if cur: flag 0x200                                                      | 0x0041fdb0         |
// | 0x6f | -         | if cur: flag 0x400                                                      | 0x0041fdb0         |
// | 0x72 | w w       | always: (Game; w0, w1)                                                  | 0x00403aa0         |
// | 0x73 | s         | always: camera path `s` (Game+0xa68, 1, 0) (weather.md)                 | 0x0040ff50         |
// | 0x74 | s         | always: camera path `s` (Game+0xa68, 2, 0)                              | 0x0040ff50         |
// | 0x75 | -         | if cur                                                                  | 0x0041ddc0         |
// | 0x76 | w*4       | if cur                                                                  | 0x0041fb00         |
// | 0x77 | w*4       | if cur && obj: obj+0x634 = obj, {0xffRRGGBB from w0..w2, w3}            | 0x0047bda0         |
// | 0x78 | s         | if cur: create fx `s` at cur's position (cur vt +0x54, flags 0x28)      | 0x0041cc60         |
// | 0x79 | -         | if cur                                                                  | 0x0041e230         |
// | 0x104| L         | name list: list1 if empty, else list2                                   |                    |
// | else |           | log "unknown opcode" and carry on                                       | 0x00407ff0         |
#include <cstring>

#include "game.h"
#include "game/game_object.h"
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}

// 0x00407ff0: the game's debug log, compiled out (a bare `ret`), cdecl, variadic.
template <class... A>
void FxLog(A... args)
{
    reinterpret_cast<void(__cdecl*)(...)>(0x00407ff0)(args...);
}

// 0x0046e2c0 (stack lo, hi; caller pops; st0): (hi - lo) * rand() * [0x005bcbc0] + lo. Its `ecx input` is only a
// `push ecx` that reserves the slot for rand()'s result, so its value is never read: pass 0.
float RandRange(uint32_t lo, uint32_t hi)
{
    return orig::fn_0046e2c0(0u, lo, hi);
}
}  // namespace

// 0x00420770 (stack: manager, fx; callee pops 8; returns al; only caller 0x00423360, which reads al only).
// The fx script interpreter. When the fx's attach target (+0x6c) or owner (+0xb4) is set and no longer answers its
// virtual +0xd8, that object loses a reference (+0x28; at zero and still not answering +0xd8 it is destroyed by
// 0x00487da0), the field is cleared and 1 is returned. Otherwise it runs the script from fx+4 until an end (0x04),
// pause (0x12) or wait (0x15) instruction, stores the script position back in fx+4, pops any objects still pushed
// and returns 1 when the script ended (or fx+0x14 bit 8 is set), else 0. See the opcode table above.
uint32_t Fx_RunScript(void* manager, void* fx)
{
    // 0x00420787..0x004207b7: target +0x6c, then owner +0xb4.
    void** slot = nullptr;
    {
        void* target = Field<void*>(fx, 0x6c);
        if (target && !VCall<bool>(target, 0xd8 / 4))
            slot = &Field<void*>(fx, 0x6c);
        else
        {
            void* owner = Field<void*>(fx, 0xb4);
            if (owner && !VCall<bool>(owner, 0xd8 / 4))
                slot = &Field<void*>(fx, 0xb4);
        }
    }
    if (slot)
    {
        // 0x004207b9..0x004207ea
        void* obj = *slot;
        if (--Field<int32_t>(obj, 0x28) == 0 && !VCall<bool>(obj, 0xd8 / 4))
            orig::fn_00487da0(u(obj));
        *slot = nullptr;
        return 1;
    }

    // 0x004207ed..0x0042081c
    const uint8_t* p = Field<const uint8_t*>(fx, 4);
    bool stop = false;      // [B+0x22]
    bool finished = false;  // [B+0x23]
    uint32_t stackTop = 0;  // [B+0x10] top node {next, object}
    uint32_t stackHead = 0; // [B+0x6c] first node
    void* cur = nullptr;    // [B+0x14] / esi
    uint32_t list1 = 0;     // [B+0x1c]
    uint32_t list2 = 0;     // [B+0x2c]
    Field<void*>(manager, 8) = fx;
    uint32_t lastOp = 0xffffffffu;  // edx
    const uint32_t mgr = u(manager);
    const uint32_t fxu = u(fx);

    auto W = [&]() -> uint32_t {
        uint32_t w;
        std::memcpy(&w, p, 4);
        p += 4;
        return w;
    };
    auto S = [&]() -> const char* {
        const char* s = reinterpret_cast<const char*>(p);
        p += (std::strlen(s) + 0x20) & ~0x1fu;
        return s;
    };
    auto F = [](float f) { return orig::f2u(f); };
    // Name of the opcodes 0x38..0x3a: count, then count strings; one picked by rand() * count >> 15 (imul, sar).
    // The original keeps the pick in a stack slot ([B+0x100] / +0x108 / +0x110) that is not written when the count
    // is 0; that case passes null here (not in retail data).
    auto Pick = [&]() -> uint32_t {
        uint32_t count = W();
        int32_t idx = static_cast<int32_t>(static_cast<uint32_t>(orig::fn_005391cd()) * count) >> 15;
        uint32_t picked = 0;
        for (uint32_t i = 0; i < count; ++i)
        {
            if (i == static_cast<uint32_t>(idx))
                picked = u(p);
            S();
        }
        return picked;
    };

    while (true)
    {
        // 0x00420822..0x00420852: edi = top, ebx = manager.
        const uint32_t top = stackTop;
        const uint32_t op = W();  // [B+0x94]
        switch (op)
        {
        case 0x04:  // 0x0042089e
            stop = true;
            finished = true;
            break;

        case 0x05:  // 0x004208ad
        {
            const char* name = S();
            cur = reinterpret_cast<void*>(orig::fn_004234d0(fxu, mgr, u(name), 0u, 0u, 0u));
            break;
        }
        case 0x37:  // 0x00420927
        {
            const char* name = S();
            cur = reinterpret_cast<void*>(orig::fn_004234d0(fxu, mgr, u(name), 0u, 0u, 0u));
            list1 = 0;
            break;
        }
        case 0x13:  // 0x004208e4
        {
            const char* name = S();
            cur = reinterpret_cast<void*>(orig::fn_00423680(fxu, mgr, u(name), list1));
            list1 = 0;
            break;
        }

        case 0x06:  // 0x00420968 push
            if (cur)
            {
                if (top)
                    FxLog(Field<uint32_t>(g_game, 0x34), 10u, 0x005b7ee4u, fxu + 0x24,
                          Field<uint32_t>(Field<void*>(cur, 0x414), 0x10) + 4);
                orig::fn_00423430(u(&stackTop), u(cur), mgr, u(&stackHead));
            }
            break;

        case 0x07:  // 0x004209c6 pop
            if (cur)
            {
                orig::fn_004540a0(Field<uint32_t>(reinterpret_cast<void*>(top), 4));
                orig::fn_00423470(u(&stackHead), u(&stackTop), mgr);
                cur = nullptr;
            }
            break;

        case 0x08:  // 0x004209fa life span
        {
            uint32_t lo = W();
            uint32_t hi = W();
            float flo, fhi;
            std::memcpy(&flo, &lo, 4);
            std::memcpy(&fhi, &hi, 4);
            if (fhi < flo)  // fcomp; test ah, 5; jp (ordered and below)
            {
                FxLog(Field<uint32_t>(g_game, 0x34), 10u, 0x005b7eb4u, static_cast<double>(flo),
                      static_cast<double>(fhi));
                hi = lo;
            }
            float r = RandRange(lo, hi);
            if (cur)
                orig::fn_0041d110(top, mgr, F(r));
            break;
        }

        case 0x09:  // 0x00421123
        {
            uint32_t w0 = W(), w1 = W();
            float r1 = RandRange(w0, w1);
            uint32_t w2 = W(), w3 = W();
            float r2 = RandRange(w2, w3);
            if (cur)
                orig::fn_0041dba0(top, mgr, F(r1), F(r2));
            break;
        }
        case 0x0a:  // 0x00421196
        {
            uint32_t w0 = W(), w1 = W();
            float r = RandRange(w0, w1);
            if (cur)
                orig::fn_0041dc30(top, mgr, F(r));
            break;
        }
        case 0x0b:  // 0x00420a80
        {
            uint32_t w0 = W(), w1 = W();
            float r = RandRange(w0, w1);
            if (cur)
                orig::fn_0041d170(top, mgr, F(r), F(r));
            break;
        }
        case 0x0c:  // 0x00420ad8
        case 0x0d:  // 0x00420b69
        case 0x0e:  // 0x00420bf7
        {
            uint32_t w0 = W(), w1 = W(), w2 = W(), w3 = W();
            float r1 = RandRange(w0, w1);
            float r2 = RandRange(w2, w3);
            if (cur)
            {
                if (op == 0x0c)
                    orig::fn_0041d170(top, mgr, F(r1), F(r2));
                else if (op == 0x0d)
                    orig::fn_0041d210(top, mgr, F(r1), F(r2));
                else
                    orig::fn_0041d2c0(top, mgr, F(r1), F(r2));
            }
            break;
        }

        case 0x0f:  // 0x00421226
        case 0x2f:  // 0x004211d7
        {
            // 0x0041dd50(eax = name, esi = top, edi = manager; list1, list2, 0 / 1, 0)
            const char* name = S();
            if (cur)
                orig::fn_0041dd50(u(name), top, mgr, list1, list2, op == 0x2f ? 1u : 0u, 0u);
            list1 = 0;
            list2 = 0;
            break;
        }

        case 0x10:  // 0x00421379
        case 0x1e:  // 0x004213d7
        {
            uint32_t w0 = W(), w1 = W();
            float r = RandRange(w0, w1);
            if (cur)
            {
                // 0x0041cba0(eax = list1; manager): its ecx is a pushed slot, never read (see fx.cpp).
                uint32_t point = orig::fn_0041cba0(list1, 0u, mgr);
                if (op == 0x10)
                    orig::fn_0041e0c0(point, top, mgr, F(r));
                else
                    orig::fn_0041dfb0(point, top, mgr, F(r));
            }
            list1 = 0;
            break;
        }

        case 0x11:  // 0x00421260
        {
            uint32_t w0 = W(), w1 = W();
            float r = RandRange(w0, w1);
            if (cur)
                orig::fn_0041de10(top, mgr, F(r));
            break;
        }
        case 0x12:  // 0x0042142f pause
        {
            uint32_t w0 = W(), w1 = W();
            float r = RandRange(w0, w1);
            orig::fn_0041dea0(fxu, top, mgr, F(r));
            stop = true;
            break;
        }
        case 0x14:  // 0x0042148e
            if (cur)
                orig::fn_0041df20(fxu, top, mgr);
            break;
        case 0x15:  // 0x00421474 wait
            orig::fn_0041dee0(top, mgr, fxu);
            stop = true;
            break;
        case 0x16:  // 0x004214aa invisible
            if (cur)
                orig::fn_0041e280(top, mgr);
            break;
        case 0x79:  // 0x004214bf
            if (cur)
                orig::fn_0041e230(top, mgr);
            break;

        case 0x63:  // 0x004214d4
        {
            const char* name = S();
            uint32_t w0 = W(), w1 = W(), w2 = W(), w3 = W();
            if (cur)
                orig::fn_0041e310(mgr, w0, top, u(name), w1, w2, w3);
            break;
        }
        case 0x2a:  // 0x00421536
        {
            uint32_t w0 = W(), w1 = W();
            float r = RandRange(w0, w1);
            if (cur)
                orig::fn_0041e3b0(top, mgr, F(r));
            break;
        }

        case 0x17:  // 0x00421586
        case 0x18:  // 0x004215ff
        case 0x19:  // 0x00421679
        case 0x30:  // 0x004217df
        {
            // 0x0041e4d0(ebx = manager, edi = top; fx, name, r, a3, a4, flags)
            const char* name = S();
            uint32_t w0 = W(), w1 = W();
            float r = RandRange(w0, w1);
            if (cur)
            {
                if (op == 0x17)
                    orig::fn_0041e4d0(mgr, top, fxu, u(name), F(r), 0u, 0u, 0x80u);
                else if (op == 0x18)
                    orig::fn_0041e4d0(mgr, top, fxu, u(name), F(r), list1, 0u, 0x40u);
                else
                    orig::fn_0041e4d0(mgr, top, fxu, u(name), F(r), list1, list2, op == 0x19 ? 2u : 1u);
            }
            if (op == 0x18)
                list1 = 0;
            else if (op != 0x17)
            {
                list1 = 0;
                list2 = 0;
            }
            break;
        }

        case 0x36:  // 0x004216fd
        {
            const char* name = S();
            uint32_t w0 = W(), w1 = W();
            float r = RandRange(w0, w1);
            uint32_t a[3], b[3];  // [B+0x330] / [B+0x3c0]
            a[0] = W();
            b[0] = W();
            a[1] = W();
            b[1] = W();
            a[2] = W();
            b[2] = W();
            if (cur)
                // 0x0041e610(eax = list2, ecx = list1, edx = top, esi = b; manager, fx, name, r, a, 0x100)
                orig::fn_0041e610(list2, list1, top, u(b), mgr, fxu, u(name), F(r), u(a), 0x100u);
            list1 = 0;
            list2 = 0;
            break;
        }

        case 0x27:  // 0x00421863
        case 0x1f:  // 0x004218c7
        case 0x28:  // 0x00421ade
        {
            uint32_t w0 = W(), w1 = W(), w2 = W();
            float r = RandRange(w1, w2);
            if (cur)
            {
                // 0x0041e760(eax = 0; manager, fx, top, w0, r, a5, flags, 0, 0)
                if (op == 0x27)
                    orig::fn_0041e760(0u, mgr, fxu, top, w0, F(r), 0u, 0x20u, 0u, 0u);
                else if (op == 0x1f)
                    orig::fn_0041e760(0u, mgr, fxu, top, w0, F(r), 0u, 4u, 0u, 0u);
                else
                    orig::fn_0041e760(0u, mgr, fxu, top, w0, F(r), list1, 0x40u, 0u, 0u);
            }
            if (op == 0x28)
                list1 = 0;
            break;
        }
        case 0x29:  // 0x0042192b
        {
            uint32_t w0 = W(), w1 = W(), w2 = W();
            float r = RandRange(w1, w2);
            if (cur)
            {
                // 0x00421967..0x004219e4: camera (Game+0x748) virtual +0x54 fills pos ([B+0x2dc]); pos.z +=
                // [0x005bcb68]; up = {0, 0, 1} ([B+0x39c]). eax and the 8th stack argument are both pos.
                alignas(4) uint32_t pos[17] = {};
                uint32_t up[3] = {0u, 0u, 0x3f800000u};
                VCall<void>(Field<void*>(g_game, 0x748), 0x54 / 4, pos);
                float z;
                std::memcpy(&z, &pos[2], 4);
                z = z + GameGlobal<float>(0x005bcb68);
                std::memcpy(&pos[2], &z, 4);
                orig::fn_0041e760(u(pos), mgr, fxu, top, w0, F(r), 0u, 0u, u(pos), u(up));
            }
            break;
        }
        case 0x2d:  // 0x004219ee
        {
            uint32_t w0 = W(), w1 = W(), w2 = W();
            uint32_t a[3], b[3];  // [B+0x33c] / [B+0x354]
            a[0] = W();
            b[0] = W();
            a[1] = W();
            b[1] = W();
            a[2] = W();
            b[2] = W();
            float r = RandRange(w1, w2);
            if (cur)
            {
                uint32_t up[3] = {0u, 0u, 0x3f800000u};  // [B+0x3b4]
                orig::fn_0041e760(u(a), mgr, fxu, top, w0, F(r), 0u, 0u, u(b), u(up));
            }
            break;
        }

        case 0x66:  // 0x00421b49
            if (cur)
                orig::fn_0041e990(mgr, top);
            break;

        case 0x20:  // 0x00421b6a
        case 0x21:  // 0x00421ba9
        case 0x22:  // 0x00421be8
        case 0x23:  // 0x00421c27
        case 0x24:  // 0x00421c66
        case 0x48:  // 0x00421ca5
        case 0x25:  // 0x00421ce4
        case 0x26:  // 0x00421d6c
        {
            // (eax = top, esi = manager; w0, w1)
            uint32_t w0 = W(), w1 = W();
            if (cur)
            {
                switch (op)
                {
                case 0x20: orig::fn_0041e9f0(top, mgr, w0, w1); break;
                case 0x21: orig::fn_0041ea40(top, mgr, w0, w1); break;
                case 0x22: orig::fn_0041ea90(top, mgr, w0, w1); break;
                case 0x23: orig::fn_0041f340(top, mgr, w0, w1); break;
                case 0x24: orig::fn_0041ee50(top, mgr, w0, w1); break;
                case 0x48: orig::fn_0041eea0(top, mgr, w0, w1); break;
                case 0x25: orig::fn_0041eae0(top, mgr, w0, w1); break;
                default: orig::fn_0041f2f0(top, mgr, w0, w1); break;
                }
            }
            break;
        }
        case 0x49:  // 0x00421d23
        case 0x46:  // 0x00421dab
        case 0x47:  // 0x00421df4
        {
            // (eax = top, ecx = manager; w0, w1, w2)
            uint32_t w0 = W(), w1 = W(), w2 = W();
            if (cur)
            {
                if (op == 0x49)
                    orig::fn_0041eb70(top, mgr, w0, w1, w2);
                else if (op == 0x46)
                    orig::fn_0041eef0(top, mgr, w0, w1, w2);
                else
                    orig::fn_0041f0f0(top, mgr, w0, w1, w2);
            }
            break;
        }
        case 0x2b:  // 0x00421e3d
        case 0x2c:  // 0x00421ea6
        {
            uint32_t w0 = W(), w1 = W(), w2 = W(), w3 = W();
            if (cur)
            {
                if (op == 0x2b)
                    orig::fn_0041f3b0(top, mgr, w0, w1, w2, w3);
                else
                    orig::fn_0041f410(top, mgr, w0, w1, w2, w3);
            }
            break;
        }

        case 0x31:  // 0x00421f0f
        case 0x1a:  // 0x00421fa6
        case 0x64:  // 0x004212aa
        {
            uint32_t w0 = W(), w1 = W();
            float r1 = RandRange(w0, w1);
            uint32_t w2 = W(), w3 = W();
            float r2 = RandRange(w2, w3);
            uint32_t w4 = W(), w5 = W();
            float r3 = RandRange(w4, w5);
            if (cur)
            {
                if (op == 0x31)
                    orig::fn_0041f850(top, mgr, F(r1), F(r2), F(r3));
                else if (op == 0x1a)
                    orig::fn_0041f8f0(top, mgr, F(r1), F(r2), F(r3));
                else
                {
                    uint32_t v[3] = {F(r1), F(r2), F(r3)};  // [B+0x3a8]
                    orig::fn_0041e1e0(top, mgr, u(v));
                }
            }
            break;
        }

        case 0x78:  // 0x0042203d
        {
            const char* name = S();
            if (cur)
            {
                // Record [B+0x4c8]: +0 flags 0x28 (position at +0xc), +4 0; +0xc position from cur's
                // virtual +0x54. The original leaves the other bytes as stack garbage; 0x0041cc60 reads only
                // +0, +4 and +0xc..+0x14 for these flags.
                alignas(4) uint32_t rec[0x20] = {};
                VCall<void>(cur, 0x54 / 4, &rec[3]);
                rec[1] = 0;
                rec[0] = 0x28;
                orig::fn_0041cc60(0u, mgr, u(name), u(rec));  // al = 0
            }
            break;
        }
        case 0x1b:  // 0x00422099
        {
            const char* name = S();
            uint32_t w0 = W(), w1 = W(), w2 = W(), w3 = W(), w4 = W(), w5 = W();
            alignas(4) uint32_t rec[0x20] = {};  // [B+0x448]
            rec[3] = F(RandRange(w0, w1));
            rec[4] = F(RandRange(w2, w3));
            rec[5] = F(RandRange(w4, w5));
            rec[1] = 0;
            rec[0] = 0x28;
            orig::fn_0041cc60(0u, mgr, u(name), u(rec));  // al = 0
            break;
        }

        case 0x1c:  // 0x00422183
        {
            uint32_t w0 = W(), w1 = W();
            float r = RandRange(w0, w1);
            if (cur)
                orig::fn_0041f970(top, mgr, F(r));
            break;
        }
        case 0x3d:  // 0x004221d4
            if (cur)
                orig::fn_0041f9d0(top, mgr);
            break;
        case 0x3c:  // 0x004221f6
            if (cur)
                orig::fn_0041fa60(top, mgr);
            break;
        case 0x32:  // 0x00422218
            if (cur)
                orig::fn_0041faf0(mgr, top);
            break;
        case 0x76:  // 0x00422239
        {
            uint32_t w0 = W(), w1 = W(), w2 = W(), w3 = W();
            if (cur)
                orig::fn_0041fb00(top, mgr, w0, w1, w2, w3);
            break;
        }
        case 0x34:  // 0x0042229e
            if (cur)
                orig::fn_0041fbd0(top, mgr);
            break;
        case 0x33:  // 0x004222bb
            if (cur)
                orig::fn_0041fc20(top, mgr);
            break;
        case 0x35:  // 0x004222d8
        {
            const char* name = S();
            uint32_t w0 = W(), w1 = W();
            if (cur)
                orig::fn_0041fc70(top, u(name), mgr, w0, w1);
            break;
        }
        case 0x1d:  // 0x00422325
        {
            const char* name = S();
            if (cur)
                orig::fn_0041fd10(mgr, top, u(name));
            break;
        }
        case 0x62:  // 0x00422362
        {
            uint32_t w0 = W(), w1 = W();
            if (cur)
                orig::fn_0041fe00(top, w1, mgr, w0, w1);  // ecx still holds w1 (a pushed slot there)
            break;
        }
        case 0x6e:  // 0x00422385
            if (cur)
                orig::fn_0041fdb0(top, 0x200u, mgr);
            break;
        case 0x6f:  // 0x0042239f
            if (cur)
                orig::fn_0041fdb0(top, 0x400u, mgr);
            break;
        case 0x2e:  // 0x004223b9
            if (cur)
                orig::fn_0041fd70(top, 0x400u, mgr);
            break;
        case 0x4b:  // 0x004223d3
            if (cur)
                orig::fn_0041fd70(top, 0x2000000u, mgr);
            break;
        case 0x4c:  // 0x004223ed
            if (cur)
                orig::fn_0041fd70(top, 0x4000000u, mgr);
            break;

        case 0x3f:  // 0x00422407 (no cur test)
        {
            const char* name = S();
            uint32_t w0 = W(), w1 = W(), w2 = W(), w3 = W();
            orig::fn_0041ff60(u(name), top, w0, mgr, w2, w3, w1);
            break;
        }

        case 0x38:  // 0x0042246e (no cur test, list1 kept)
        {
            uint32_t picked = Pick();  // [B+0x100]
            uint32_t w = W();
            orig::fn_00420080(top, picked, mgr, 0u, 0u, 0u, w);
            break;
        }
        case 0x39:  // 0x004224e2
        case 0x3a:  // 0x0042256b
        {
            uint32_t picked = Pick();  // [B+0x108] / [B+0x110]
            uint32_t w = W();
            if (cur)
            {
                uint32_t point = orig::fn_0041cba0(list1, 0u, mgr);
                orig::fn_00420080(top, picked, mgr, point, 1u, op == 0x3a ? 1u : 0u, w);
            }
            list1 = 0;
            break;
        }

        case 0x3b:  // 0x004225dc
        {
            uint32_t w0 = W(), w1 = W(), w2 = W();
            if (cur)
                orig::fn_004201f0(top, mgr, w0, w1, w2);
            break;
        }
        case 0x3e:  // 0x00422624
            if (cur)
                orig::fn_00420260(top, mgr);
            break;
        case 0x40:  // 0x00422639
        {
            uint32_t w0 = W(), w1 = W(), w2 = W(), w3 = W(), w4 = W(), w5 = W();
            if (cur)
                orig::fn_00420670(mgr, w0, top, w1, w2, w3, w4, w5);
            break;
        }
        case 0x45:  // 0x004226b9
        {
            uint32_t w0 = W(), w1 = W(), w2 = W(), w3 = W(), w4 = W();
            if (cur)
                orig::fn_00420450(mgr, w0, top, w1, w2, w3, w4);
            break;
        }
        case 0x41:  // 0x00422724
        {
            uint32_t w0 = W();
            uint32_t a[2], b[2];  // [B+0x308] / [B+0x2e8]
            a[0] = W();
            b[0] = W();
            a[1] = W();
            b[1] = W();
            if (cur)
                orig::fn_004202b0(mgr, w0, top, u(a), u(b), 0u, 0u);
            break;
        }
        case 0x42:  // 0x00422788
        case 0x43:  // 0x004227fa
        case 0x44:  // 0x0042286c
        {
            uint32_t w0 = W();
            uint32_t a[2], b[2];
            a[0] = W();
            a[1] = W();
            b[0] = W();
            b[1] = W();
            uint32_t w5 = W();
            if (cur)
                orig::fn_004202b0(mgr, w0, top, u(a), u(b), w5, op == 0x42 ? 1u : op == 0x43 ? 3u : 2u);
            break;
        }

        case 0x4d:  // 0x004228de
            if (cur)
                Field<uint32_t>(reinterpret_cast<void*>(orig::fn_0041f500(top, mgr)), 0x58) = 0;
            break;
        case 0x4e:  // 0x004228fa
            if (cur)
            {
                void* r = reinterpret_cast<void*>(orig::fn_0041f500(top, mgr));
                Field<uint32_t>(r, 0x58) = orig::fn_0041cba0(list1, 0u, mgr);
            }
            list1 = 0;
            break;

        case 0x4f:  // 0x00422926 (opcodes 0x4f..0x53)
        case 0x50:
        case 0x51:
        case 0x52:
        case 0x53:
        {
            // A count of more than 4 words overran the original's 4-dword local ([B+0x30]) into the next stack
            // slots (rep movsd); those words then fall to the default branch. Only 4 are copied here.
            uint32_t count = W();
            uint32_t v[4];  // [B+0x30]
            if (count != 0)
            {
                std::memcpy(v, p, (count < 4 ? count : 4) * 4);
                p += count * 4;
            }
            if (count == 1)
            {
                v[3] = v[0];
                v[2] = v[0];
                v[1] = v[0];
            }
            else if (count == 2)
            {
                // 0x00422976: {x, (y - x) * [0x005bcc60] + x, (that + y) * [0x005bcb4c], y}
                float x, y;
                std::memcpy(&x, &v[0], 4);
                std::memcpy(&y, &v[1], 4);
                v[3] = v[1];
                float t = (y - x) * GameGlobal<float>(0x005bcc60) + x;
                float t2 = (t + y) * GameGlobal<float>(0x005bcb4c);
                std::memcpy(&v[1], &t, 4);
                std::memcpy(&v[2], &t2, 4);
            }
            else if (count != 4)
            {
                v[3] = v[2] = v[1] = v[0] = 0x3f800000u;
            }
            if (cur)
            {
                if (op == 0x4f)
                    orig::fn_0041f6d0(top, mgr, u(v));
                else if (op == 0x50)
                    orig::fn_0041f710(top, mgr, u(v));
                else if (op == 0x51)
                    orig::fn_0041f750(top, mgr, u(v));
                else if (op == 0x52)
                    orig::fn_0041f790(top, mgr, u(v));
                else
                    orig::fn_0041f7d0(top, mgr, u(v));
            }
            break;
        }

        case 0x54:  // 0x00422a5d
        {
            uint32_t w0 = W();
            if (cur)
                orig::fn_0041f810(top, mgr, w0);
            break;
        }
        case 0x55:  // 0x00422a8e
        {
            const char* name = S();
            if (cur)
                orig::fn_0041f5a0(top, u(name), mgr);
            break;
        }
        case 0x56:  // 0x00422ac1
        {
            uint32_t v[4];  // [B+0x3cc]
            v[0] = W();
            v[1] = W();
            v[2] = W();
            v[3] = W();
            if (cur)
                orig::fn_0041f640(top, u(v), mgr);
            break;
        }
        case 0x57:  // 0x00422b1d
        {
            uint32_t w0 = W();
            if (cur)
                orig::fn_0041f690(top, mgr, w0);
            break;
        }
        case 0x58:  // 0x00421075
        case 0x59:  // 0x004210b9
        {
            uint32_t v[3];  // [B+0x41c] / [B+0x42c]
            v[0] = W();
            v[1] = W();
            v[2] = W();
            if (cur)
            {
                if (op == 0x58)
                    orig::fn_0041d8f0(u(v), top, mgr);
                else
                    orig::fn_0041d980(u(v), top, mgr);
            }
            break;
        }
        case 0x5a:  // 0x004210fd
        {
            uint32_t w0 = W();
            if (cur)
                orig::fn_0041da10(top, mgr, w0);
            break;
        }
        case 0x4a:  // 0x0042102a
        {
            uint32_t v[3];  // [B+0x40c]
            v[0] = W();
            v[1] = W();
            v[2] = W();
            if (cur)
                orig::fn_0041da90(top, u(v), mgr, list1);
            list1 = 0;
            break;
        }

        case 0x67:  // 0x00420c7c
        case 0x68:  // 0x00420d4c
        case 0x69:  // 0x00420e1c
        {
            uint32_t w0 = W(), w1 = W(), w2 = W(), w3 = W(), w4 = W(), w5 = W(), w6 = W();
            if (cur)
            {
                uint32_t v[3];  // [B+0x384] / [B+0x390] / [B+0x348]
                v[0] = F(RandRange(w1, w2));
                v[1] = F(RandRange(w3, w4));
                v[2] = F(RandRange(w5, w6));
                if (op == 0x67)
                    orig::fn_0041d360(top, w0, u(v), mgr);
                else if (op == 0x68)
                    orig::fn_0041d440(top, w0, u(v), mgr);
                else
                    orig::fn_0041d560(top, w0, u(v), mgr);
            }
            break;
        }
        case 0x6b:  // 0x00420eec
        case 0x6c:  // 0x00420f56
        case 0x6d:  // 0x00420fc0
        {
            uint32_t v[3];  // [B+0x360] / [B+0x378] / [B+0x36c]
            v[0] = W();
            v[1] = W();
            v[2] = W();
            uint32_t w3 = W(), w4 = W();
            if (cur)
            {
                if (op == 0x6b)
                    orig::fn_0041d640(top, u(v), mgr, w3, w4);
                else if (op == 0x6c)
                    orig::fn_0041d700(top, u(v), mgr, w3, w4);
                else
                    orig::fn_0041d830(top, u(v), mgr, w3, w4);
            }
            break;
        }
        case 0x75:  // 0x0042124b
            if (cur)
                orig::fn_0041ddc0(top, mgr);
            break;

        case 0x6a:  // 0x00422b45 decal
        {
            uint32_t w0 = W();
            const char* name = S();
            if (cur && top)
            {
                // obj virtual +0x60 takes no arguments: the four pushes before it (0, 0, name, w0) and its
                // result are 0x004165c0's five stack arguments (ret 0x14). obj is not null-checked.
                void* obj = Field<void*>(reinterpret_cast<void*>(top), 4);
                uint32_t at = VCall<uint32_t>(obj, 0x60 / 4);
                orig::fn_004165c0(Field<uint32_t>(g_game, 0xa7c), at, w0, u(name), 0u, 0u);
            }
            break;
        }
        case 0x65:  // 0x00422bad
        {
            p += 8;  // two words, not read
            const char* name = S();
            if (cur && top)
            {
                void* obj = Field<void*>(reinterpret_cast<void*>(top), 4);
                if (obj)
                {
                    alignas(4) uint32_t pos[17];  // [B+0x43c]
                    VCall<void>(obj, 0x54 / 4, pos);
                    // 0x004881c0(ecx = name, esi = Game+0xa78): a bank lookup by name, no stack arguments; its
                    // result is discarded.
                    orig::fn_004881c0(u(name), Field<uint32_t>(g_game, 0xa78), 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u);
                }
            }
            break;
        }
        case 0x77:  // 0x00422c0c
        {
            uint32_t w0 = W(), w1 = W(), w2 = W(), w3 = W();
            if (cur && top)
            {
                void* obj = Field<void*>(reinterpret_cast<void*>(top), 4);
                if (obj)
                {
                    uint32_t rec[2];  // [B+0x2f8]: 0xffRRGGBB, w3
                    rec[0] = ((((w0 & 0xffu) | 0xffffff00u) << 8 | (w1 & 0xffu)) << 8) | (w2 & 0xffu);
                    rec[1] = w3;
                    Field<void*>(obj, 0x634) = obj;
                    orig::fn_0047bda0(Field<uint32_t>(g_game, 0xa9c), u(rec), u(obj));
                }
            }
            break;
        }
        case 0x5b:  // 0x00422c8d
        {
            uint32_t v[4];  // [B+0x3fc]
            v[0] = W();
            v[1] = W();
            v[2] = W();
            v[3] = W();
            if (cur && top)
            {
                void* obj = Field<void*>(reinterpret_cast<void*>(top), 4);
                if (obj)
                {
                    Field<void*>(obj, 0x634) = obj;
                    orig::fn_0047be00(Field<uint32_t>(g_game, 0xa9c), u(v), u(obj));
                }
            }
            break;
        }
        case 0x5c:  // 0x00422d04
        case 0x70:
        {
            uint32_t w0 = W();  // [B+0x18]
            if (cur && top)
            {
                void* obj = Field<void*>(reinterpret_cast<void*>(top), 4);
                if (obj)
                {
                    Field<void*>(obj, 0x634) = obj;
                    if (op == 0x5c)
                        orig::fn_0047be60(Field<uint32_t>(g_game, 0xa9c), u(obj), u(&w0));
                    else
                        orig::fn_0047bec0(Field<uint32_t>(g_game, 0xa9c), u(obj), u(&w0));
                }
            }
            break;
        }
        case 0x5d:  // 0x00422d5a
        case 0x71:
        {
            uint32_t w0 = W(), w1 = W(), w2 = W(), w3 = W();
            // [B+0x3dc]: {0.5, 0.5, w0, w1, 0.5, 0.5, w2, w3}
            uint32_t rec[8] = {0x3f000000u, 0x3f000000u, w0, w1, 0x3f000000u, 0x3f000000u, w2, w3};
            if (cur && top)
            {
                void* obj = Field<void*>(reinterpret_cast<void*>(top), 4);
                if (obj)
                {
                    Field<void*>(obj, 0x634) = obj;
                    // 0x00422dd5: `cmp ecx, 0x5c` while this case only runs for 0x5d / 0x71: never equal
                    // (original bug, docs-worthy), so 0x0047bf20 is unreachable from here.
                    if (op == 0x5c)
                        orig::fn_0047bf20(Field<uint32_t>(g_game, 0xa9c), u(obj), u(rec));
                    else
                        orig::fn_0047bf80(Field<uint32_t>(g_game, 0xa9c), u(obj), u(rec));
                }
            }
            break;
        }
        case 0x5e:  // 0x00422e09
        {
            uint32_t w0 = W(), w1 = W(), w2 = W();
            if (cur && top)
            {
                void* obj = Field<void*>(reinterpret_cast<void*>(top), 4);
                if (obj && Field<uint32_t>(obj, 0x634))
                    orig::fn_0047c1a0(Field<uint32_t>(g_game, 0xa9c), 1u, Field<uint32_t>(obj, 0x634), w0, w1,
                                      w2);
            }
            break;
        }
        case 0x5f:  // 0x00422e84
        {
            uint32_t w0 = W(), w1 = W();
            if (cur && top)
            {
                void* obj = Field<void*>(reinterpret_cast<void*>(top), 4);
                if (obj && Field<uint32_t>(obj, 0x634))
                    orig::fn_0047c0a0(Field<uint32_t>(g_game, 0xa9c), 1u, Field<uint32_t>(obj, 0x634), w0, w1);
            }
            break;
        }
        case 0x61:  // 0x00422eed
        {
            uint32_t w0 = W(), w1 = W();
            if (cur && top)
            {
                void* obj = Field<void*>(reinterpret_cast<void*>(top), 4);
                if (obj && Field<uint32_t>(obj, 0x634))
                    orig::fn_0047c140(Field<uint32_t>(g_game, 0xa9c), 1u, Field<uint32_t>(obj, 0x634), w0, w1);
            }
            break;
        }
        case 0x60:  // 0x00422f4e
            if (cur && top)
            {
                void* obj = Field<void*>(reinterpret_cast<void*>(top), 4);
                if (obj && Field<uint32_t>(obj, 0x634))
                {
                    orig::fn_0041bd30(u(obj));
                    Field<uint32_t>(obj, 0x634) = 0;
                }
            }
            break;

        case 0x72:  // 0x00422f91
        {
            uint32_t w0 = W(), w1 = W();
            orig::fn_00403aa0(u(g_game), w0, w1);
            break;
        }
        case 0x73:  // 0x00422fbc
        case 0x74:  // 0x00422fec
        {
            const char* name = S();
            orig::fn_0040ff50(u(name), Field<uint32_t>(g_game, 0xa68), op == 0x73 ? 1u : 2u, 0u);
            break;
        }

        case 0x104:  // 0x00420859 name list
        {
            if (list1 == 0)
                list1 = u(p);
            else
                list2 = u(p);
            uint32_t count = W();
            for (uint32_t i = 0; i < count; ++i)
                S();
            break;
        }

        default:  // 0x00423000 (op - 4 > 0x100, or table case 0x71: 0x7a..0x103). No level argument here.
            FxLog(Field<uint32_t>(g_game, 0x34), 0x005b7e7cu, fxu + 0x24, op, lastOp);
            break;
        }

        // 0x00423024
        lastOp = op;
        if (stop)
            break;
    }

    // 0x00423037..0x00423076
    if (Field<uint32_t>(fx, 0x14) & 0x100)
        finished = true;
    Field<const uint8_t*>(fx, 4) = p;
    while (stackTop)
        orig::fn_00423470(u(&stackHead), u(&stackTop), mgr);
    return finished ? 1u : 0u;  // eax = [B+0x10] (0) with al = the finished byte
}

static uint32_t Fx_RunScript_Abi(uint32_t /*edx: overwritten at 0x00420811 before any read*/, uint32_t a0,
                                 uint32_t a1)
{
    return Fx_RunScript(reinterpret_cast<void*>(a0), reinterpret_cast<void*>(a1));
}
ABI_ENTRY_00420770(Fx_RunScript_Entry, Fx_RunScript_Abi)
HOOK(0x00420770, "Fx_RunScript", Fx_RunScript_Entry, nullptr);
