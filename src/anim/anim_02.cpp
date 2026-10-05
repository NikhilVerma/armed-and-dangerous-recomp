// Recovered: animation layer, part 2 (batch anim_02, 0x00493d00-0x004ed840). Static recovery 2026-10-05,
// compile-checked only (the game was not run); the oracle (tools/compare.py) is still to do.
//
// Conventions as in src/anim/anim_01.cpp: every function's comment gives its address, its register/stack ABI (as the
// original reads it), what it does and the evidence. "guess" marks interpretations not proven by strings or behaviour.
//
// Shared structures seen in this file (from the disassembly; names are guesses unless stated):
// - counted list (the "element i" idiom, as in anim_01): count at +0x1c, data flag +0x20, +0x24 -> +0x10 = item array;
//   an item is a header whose word at +2 is the offset of its payload (payload = item + [item+2]).
// - name lookup: list +0x24 is an iterator {+0 count, +8 index, +0xc miss flag, +0x10 items, +0x18 key} filled and
//   searched by 0x004e0f40 (esi = iterator).
// - key track ("channel", used by 0x004eb780): +1 byte pre-wrap mode, +2 byte post-wrap mode (2/3 loop, 4 ping-pong),
//   +3 byte flags (bit 2 = no wrapping), +4 sampler callback (cdecl (out, record)), +8 key count, +0xc key pointers
//   (each key starts with its int time).
// - sample record (0x24 bytes): +0/+4 copied from the animation (+0x14/+0x18), +8 key index, +0xc next key index,
//   +0x10 key, +0x14 next key, +0x18 time, +0x1c fraction, +0x20 1 - fraction.
#include <cmath>
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
inline void* P(uint32_t v)
{
    return reinterpret_cast<void*>(v);
}
inline uint8_t* B(uint32_t v)
{
    return reinterpret_cast<uint8_t*>(v);
}
inline float bf(uint32_t v)
{
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
inline uint32_t fb(float f)
{
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}
// A float constant of the game's .rdata (0x0056c05c = 0.0, 0x005bcb2c = 1.0, 0x005bcb4c = 0.5, 0x005bcb7c = 3.0,
// 0x005bcdd0 = -0.001, 0x005bcd3c = 0.001, 0x005bcbe4 = 0.25).
inline float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
// 0x0056a7c8 (_ftol): truncates st0.
inline int32_t Ftol(float x)
{
    return static_cast<int32_t>(static_cast<int64_t>(x));
}
inline uint32_t& D(uint8_t* p, uint32_t o)
{
    return Field<uint32_t>(p, o);
}
inline float& F(uint8_t* p, uint32_t o)
{
    return Field<float>(p, o);
}
// Payload of a list item: item + [item+2].
inline uint32_t Payload(uint32_t item)
{
    return Field<uint32_t>(P(item + Field<uint16_t>(P(item), 2)), 0);
}
// The "element i of a counted list" accessor with the count read once by the caller.
inline uint32_t ListAtN(uint8_t* list, uint32_t i, uint32_t count)
{
    if (i < count && Field<uint32_t>(list, 0x20) != 0)
        return Field<uint32_t*>(Field<uint8_t*>(list, 0x24), 0x10)[i];
    return 0;
}
// Name lookup through the list's iterator and 0x004e0f40: the found item, or 0 (miss flag, empty list, null item).
inline uint32_t ListFind(uint8_t* list, uint32_t key)
{
    const uint32_t count = Field<uint32_t>(list, 0x1c);
    if (!count || !Field<uint32_t>(list, 0x20))
        return 0;
    uint8_t* it = Field<uint8_t*>(list, 0x24);
    Field<uint32_t>(it, 0) = count;
    Field<uint32_t>(it, 0x18) = key;
    orig::fn_004e0f40(u(it));
    if (Field<uint32_t>(it, 0xc))
        return 0;
    return Field<uint32_t*>(it, 0x10)[Field<uint32_t>(it, 8)];
}
// The game's log (0x004e0b30, cdecl, variadic); returns its EAX.
template <class... A>
inline uint32_t LogPrintf(uint32_t format, A... args)
{
    return reinterpret_cast<uint32_t(__cdecl*)(uint32_t, ...)>(0x004e0b30)(format, args...);
}
typedef int(__stdcall* ReadFileFn)(uint32_t, void*, uint32_t, uint32_t*, void*);
typedef int(__stdcall* CloseHandleFn)(uint32_t);

// Calls a key track's sampler callback (track +4, cdecl (out, record)) with the registers the original callers
// leave: eax = record, ecx = out, edx = record (0x004eb780 keeps it), ebx/esi/edi as given (the samplers seen read
// only their stack arguments).
uint32_t CallSampler(uint32_t fn, uint32_t outPtr, uint32_t rec, uint32_t ebxValue, uint32_t esiValue, uint32_t ediValue)
{
    uint32_t result;
    __asm {
        push ebx
        push esi
        push edi
        mov ebx, ebxValue
        mov esi, esiValue
        mov edi, ediValue
        mov eax, rec
        mov ecx, outPtr
        mov edx, eax
        push eax
        push ecx
        call fn
        add esp, 8
        mov result, eax
        pop edi
        pop esi
        pop ebx
    }
    return result;
}
// 0x004eba50 (cdecl (t, a, b); returns st0): the time warp of the curve samplers (not in symbols/functions.csv).
float CurveParam(float t, uint32_t a, uint32_t b)
{
    float r;
    __asm {
        push b
        push a
        push t
        mov eax, 0x004eba50
        call eax
        add esp, 12
        fstp r
    }
    return r;
}
// 0x004eb930 (eax = argument block; returns eax = the block): the multi-component curve evaluator (not in
// symbols/functions.csv).
uint32_t CurveEval(uint32_t block)
{
    uint32_t r;
    __asm {
        mov eax, block
        mov ecx, 0x004eb930
        call ecx
        mov r, eax
    }
    return r;
}
// 0x004f15a0 (ecx = q1, edx = q0, esi = out; stack: t; caller pops): quaternion interpolation (guess: slerp).
uint32_t QuatSlerp(uint32_t ecxValue, uint32_t edxValue, uint32_t outPtr, uint32_t t)
{
    uint32_t r;
    __asm {
        push esi
        mov ecx, ecxValue
        mov edx, edxValue
        mov esi, outPtr
        push t
        mov eax, 0x004f15a0
        call eax
        add esp, 4
        mov r, eax
        pop esi
    }
    return r;
}
// 0x004f16b0 (ecx, edx = tangent quaternions; stack: out, q0, q1, t; caller pops): guess: squad.
uint32_t QuatSquad(uint32_t ecxValue, uint32_t edxValue, uint32_t outPtr, uint32_t q0, uint32_t q1, uint32_t t)
{
    uint32_t r;
    __asm {
        push t
        push q1
        push q0
        push outPtr
        mov ecx, ecxValue
        mov edx, edxValue
        mov eax, 0x004f16b0
        call eax
        add esp, 16
        mov r, eax
    }
    return r;
}
// 0x004f1920 (ecx = out quaternion, edi = packed 32-bit quaternion; returns eax = out): unpacks (guess).
uint32_t QuatUnpack(uint32_t outPtr, uint32_t packed)
{
    uint32_t r;
    __asm {
        push edi
        mov ecx, outPtr
        mov edi, packed
        mov eax, 0x004f1920
        call eax
        mov r, eax
        pop edi
    }
    return r;
}
}  // namespace

// Prototypes (functions of this file call each other directly).
uint32_t fn_00493f70(uint8_t* player);
uint32_t AnimTrack_FindKeys(uint8_t* track, uint8_t* rec);
uint32_t fn_004eb310(uint8_t* node, uint8_t* target, uint32_t a0);
uint32_t fn_004eb3f0(uint32_t a0, uint8_t* key);
uint32_t fn_004eb670(uint32_t key);

// ================================================================================================================
// Clip player (0x00493d00 family)
// ================================================================================================================

// 0x00493d00 (eax = position (vec3); stack: player, direction, target, from (vec3 or 0), alt (vec3); caller pops;
// executed). Builds a two-key path in the player's track (0x00493f70 creates it): key A (player+4 -> +0xc): time 0,
// tangent = direction * -0.001 (and its negation), position; key B (+0x10): either (position - from) with z = 0,
// normalised (0x004e82b0) and scaled by 0.001, or (alt.x, alt.y, direction.z) * -0.001 (original quirk: z comes from
// the direction, not from alt), its negation, and the target. Key B's time = ftol(|target - position|xy + 0.5) * 1000
// (at least 1); then samples the path at 10 points (frame = ftol(i * 0.25 * time + 0.5), sampler of player+4 via
// 0x004eb780) and, when the polyline length (xy) exceeds the time, uses ftol(length + 0.5) instead. player+8 = time;
// returns time / 1000. guess: builds a spline move for a scripted mover. The 10 sample points are uninitialised
// stack in the original when the track has no keys (+8 == 0; zero here).
uint32_t fn_00493d00(const float* pos, uint8_t* player, const float* dir, const float* target, const float* from,
                     const float* alt)
{
    fn_00493f70(player);
    uint8_t* a = Field<uint8_t*>(Field<uint8_t*>(player, 4), 0xc);
    F(a, 8) = dir[0] * K(0x005bcdd0);
    F(a, 0xc) = dir[1] * K(0x005bcdd0);
    F(a, 0x10) = dir[2] * K(0x005bcdd0);
    F(a, 0x14) = -F(a, 8);
    F(a, 0x18) = -F(a, 0xc);
    F(a, 0x1c) = -F(a, 0x10);
    const uint32_t* posBits = reinterpret_cast<const uint32_t*>(pos);
    D(a, 0x20) = posBits[0];
    D(a, 0x24) = posBits[1];
    D(a, 0x28) = posBits[2];
    D(a, 0) = 0;
    D(a, 4) = 0;
    uint8_t* b = Field<uint8_t*>(Field<uint8_t*>(player, 4), 0x10);
    if (from)
    {
        F(b, 8) = pos[0] - from[0];
        const float y = pos[1] - from[1];
        D(b, 0x10) = 0;
        F(b, 0xc) = y;
        orig::fn_004e82b0(u(b + 0x10), u(b + 0xc), u(b + 8));
        F(b, 8) = F(b, 8) * K(0x005bcd3c);
        F(b, 0xc) = F(b, 0xc) * K(0x005bcd3c);
        F(b, 0x10) = F(b, 0x10) * K(0x005bcd3c);
    }
    else
    {
        F(b, 8) = alt[0] * K(0x005bcdd0);
        F(b, 0xc) = alt[1] * K(0x005bcdd0);
        F(b, 0x10) = dir[2] * K(0x005bcdd0);
    }
    F(b, 0x14) = -F(b, 8);
    F(b, 0x18) = -F(b, 0xc);
    F(b, 0x1c) = -F(b, 0x10);
    const uint32_t* targetBits = reinterpret_cast<const uint32_t*>(target);
    D(b, 0x20) = targetBits[0];
    D(b, 0x24) = targetBits[1];
    D(b, 0x28) = targetBits[2];
    {
        const float dy = target[1] - pos[1];
        const float dx = target[0] - pos[0];
        const float d = std::sqrt(dx * dx + dy * dy);
        const int32_t t = Ftol(d + K(0x005bcb4c)) * 1000;
        Field<int32_t>(b, 0) = t;
        if (t < 1)
            Field<int32_t>(b, 0) = 1;
    }
    D(b, 4) = 0;
    float sum = 0.0f;
    float pts[30] = {};
    uint32_t rec[9] = {};
    for (uint32_t i = 0; i < 10; ++i)
    {
        const int32_t count = Field<int32_t>(b, 0);
        rec[0] = 0;
        rec[1] = static_cast<uint32_t>(count);
        rec[6] = static_cast<uint32_t>(
            Ftol(static_cast<float>(i) * K(0x005bcbe4) * static_cast<float>(count) + K(0x005bcb4c)));
        uint8_t* track = Field<uint8_t*>(player, 4);
        if (Field<uint32_t>(track, 8))
        {
            AnimTrack_FindKeys(track, reinterpret_cast<uint8_t*>(rec));
            CallSampler(Field<uint32_t>(track, 4), u(&pts[3 * i]), u(rec), u(track), i, u(b));
        }
        if (i != 0)
        {
            const float dy = pts[3 * i + 1] - pts[3 * i - 2];
            const float dx = pts[3 * i] - pts[3 * i - 3];
            sum = std::sqrt(dx * dx + dy * dy) + sum;
        }
    }
    if (static_cast<double>(Field<int32_t>(b, 0)) < static_cast<double>(sum))
        Field<int32_t>(b, 0) = Ftol(sum + K(0x005bcb4c));
    if (Field<int32_t>(b, 0) < 1)
        Field<int32_t>(b, 0) = 1;
    Field<int32_t>(player, 8) = Field<int32_t>(b, 0);
    return static_cast<uint32_t>(Field<int32_t>(b, 0) / 1000);
}

// 0x00493f70 (esi = player; no stack; plain ret; executed): when player+0 is null, allocates a 0x30-byte track
// header with the allocator hook (0x005cacc0, cdecl (0x30, 2)), zeroes it and fills a two-entry descriptor (+0 ->
// +0x10, +0x10 -> header, word +8 = 1, +0x18 = 1, +4 = +0x14 = 0x30, word +0xa = 2, +0x1c = 2, +0x20 = header), stores
// it in player+0, then player+4 = 0x004ef910(al = 0, ecx = 2, edx = header; 5, 0x30, 2) and sets its +8 (key count)
// to 2. The allocator's two pushed arguments stay on the stack as 0x004ef910's 2nd and 3rd arguments ("pushes that
// outlive a call"). The return value (EAX) is not used by the caller.
uint32_t fn_00493f70(uint8_t* player)
{
    if (Field<uint32_t>(player, 0))
        return 0;
    uint8_t* h = reinterpret_cast<uint8_t*>(GameGlobal<void*(__cdecl*)(uint32_t, uint32_t)>(0x005cacc0)(0x30, 2));
    std::memset(h, 0, 0x30);
    D(h, 0xc) = 0;
    D(h, 0) = u(h + 0x10);
    D(h, 0x10) = u(h);
    Field<uint16_t>(h, 8) = 1;
    D(h, 0x18) = 1;
    D(h, 4) = 0x30;
    Field<uint16_t>(h, 0xa) = 2;
    D(h, 0x14) = 0x30;
    D(h, 0x1c) = 2;
    D(h, 0x20) = u(h);
    D(player, 0) = u(h);
    const uint32_t track = orig::fn_004ef910(0, 2, u(h), 5, 0x30, 2);
    D(player, 4) = track;
    Field<uint32_t>(P(track), 8) = 2;
    return track;
}

// 0x00493fe0 (esi = player; plain ret; executed): frees the player's track header (0x004e1e50, ebx = header) and
// clears player+0. Returns 0x004e1e50's EAX (the caller's EAX when there is none; 0 here).
uint32_t fn_00493fe0(uint8_t* player)
{
    const uint32_t h = Field<uint32_t>(player, 0);
    if (!h)
        return 0;
    const uint32_t r = orig::fn_004e1e50(h);
    D(player, 0) = 0;
    return r;
}

// ================================================================================================================
// ANM files
// ================================================================================================================

namespace
{
// Shared body of 0x004e84e0 / 0x004eb140 (identical code apart from the message and the fix-up function): with a
// path, opens the file (0x004f1c90, esi = a local 0x108-byte file object, handle at +0), logs errFormat on failure;
// else reads the 12-byte header {size, a, b}, allocates the image (0x004e1e90(heap, size, 0, a)), stores the header
// and reads the remaining size - 12 bytes (ReadFile / CloseHandle through the import slots 0x0056b0c8 / 0x0056b084).
// With an image (loaded or passed in ECX): res = [0x004e1db0(eax = image, ecx = 0; heap) + 0x20], fixup(eax =
// {image, 1, 0, 0}; res, heap), res+8 = image, word res+6 = loaded. Returns res (0 without an image). The stack
// cookie is not reproduced.
uint32_t AnmFile_Load(uint8_t* image, uint32_t heap, uint32_t path, uint32_t errFormat, bool skin)
{
    alignas(8) uint8_t file[0x108];
    uint32_t header[3] = {};
    uint32_t got = 0;
    uint32_t loaded = 0;
    if (path)
    {
        if (!static_cast<uint8_t>(orig::fn_004f1c90(u(file), path)))
        {
            LogPrintf(errFormat, path);
        }
        else
        {
            const ReadFileFn readFile = reinterpret_cast<ReadFileFn>(GameGlobal<uint32_t>(0x0056b0c8));
            const CloseHandleFn closeHandle = reinterpret_cast<CloseHandleFn>(GameGlobal<uint32_t>(0x0056b084));
            readFile(Field<uint32_t>(file, 0), header, 0xc, &got, nullptr);
            image = B(orig::fn_004e1e90(heap, header[0], 0, header[1]));
            D(image, 0) = header[0];
            D(image, 4) = header[1];
            D(image, 8) = header[2];
            readFile(Field<uint32_t>(file, 0), image + 0xc, header[0] - 0xc, &got, nullptr);
            closeHandle(Field<uint32_t>(file, 0));
            loaded = 1;
        }
    }
    if (!image)
        return 0;
    uint8_t* res = Field<uint8_t*>(B(orig::fn_004e1db0(u(image), 0, heap)), 0x20);
    uint32_t desc[4] = {u(image), 1, 0, 0};
    if (skin)
        orig::fn_004eae90(u(desc), u(res), heap);
    else
        orig::fn_004ef620(u(desc), u(res), heap);
    D(res, 8) = u(image);
    Field<uint16_t>(res, 6) = static_cast<uint16_t>(loaded != 0);
    return u(res);
}
}  // namespace

// 0x004e84e0 (ecx = image or 0; stack: heap, path or 0; caller pops; executed): Anm_LoadAnim (name from the string
// "Error loading ANM_Anim \"%s\"\n" at 0x0058b02c). Loads (or takes) an ANM animation image and fixes it up with
// 0x004ef620; see AnmFile_Load.
uint32_t Anm_LoadAnim(uint8_t* image, uint32_t heap, uint32_t path)
{
    return AnmFile_Load(image, heap, path, 0x0058b02c, false);
}

// 0x004eb140 (ecx = image or 0; stack: heap, path or 0; caller pops; executed): Anm_LoadSkin (string "Error loading
// ANM_Skin \"%s\"\n" at 0x0058afdc). Same as Anm_LoadAnim with the fix-up 0x004eae90.
uint32_t Anm_LoadSkin(uint8_t* image, uint32_t heap, uint32_t path)
{
    return AnmFile_Load(image, heap, path, 0x0058afdc, true);
}

// ================================================================================================================
// Node / key lookups
// ================================================================================================================

// 0x004eb280 (ecx unused (push ecx is a local slot); stack: node; caller pops; executed): walks the two-level list of
// node+4 -> +0x1c -> +4 (outer items; each payload's +4 is an inner list) and returns payload-of-inner-item + 4 for the
// first inner item whose payload equals node, else 0 (both list counts read once per list). guess: the name/record
// that belongs to a node.
uint32_t fn_004eb280(uint32_t node)
{
    uint8_t* outer = Field<uint8_t*>(Field<uint8_t*>(Field<uint8_t*>(P(node), 4), 0x1c), 4);
    const uint32_t n = Field<uint32_t>(outer, 0x1c);
    for (uint32_t i = 0;;)
    {
        const uint32_t item = ListAtN(outer, i, n);
        ++i;
        if (!item)
            return 0;
        uint8_t* inner = Field<uint8_t*>(P(Payload(item)), 4);
        const uint32_t m = Field<uint32_t>(inner, 0x1c);
        for (uint32_t j = 0;;)
        {
            const uint32_t item2 = ListAtN(inner, j, m);
            ++j;
            if (!item2)
                break;
            const uint32_t off = Field<uint16_t>(P(item2), 2);
            if (Field<uint32_t>(P(item2 + off), 0) == node)
                return item2 + off + 4;
        }
    }
}

// 0x004eb310 (esi = node, edi = target object; stack: a0; plain ret): target virtual +0x17c(node+0x44, a0), then for
// each of the node's node+0x80 entries (0x2c bytes at node+0x84) virtual +0x180(entry+0x24, a0). Returns node+0x80.
// guess: registers a node's resources with the target.
uint32_t fn_004eb310(uint8_t* node, uint8_t* target, uint32_t a0)
{
    VCall<uint32_t>(target, 0x17c / 4, D(node, 0x44), a0);
    int32_t i = 0;
    uint32_t off = 0;
    while (i < Field<int32_t>(node, 0x80))
    {
        VCall<uint32_t>(target, 0x180 / 4, Field<uint32_t>(Field<uint8_t*>(node, 0x84) + off, 0x24), a0);
        ++i;
        off += 0x2c;
    }
    return D(node, 0x80);
}

// 0x004eb360 (eax = target object, ecx unused (local slot); stack: model, a1; caller pops; executed): for every outer
// item of model+0x1c -> +4 and every inner item of its payload's +4 list, 0x004eb310(esi = inner payload, edi =
// target; a1). Returns 0.
uint32_t fn_004eb360(uint8_t* target, uint8_t* model, uint32_t a1)
{
    for (uint32_t i = 0;;)
    {
        uint8_t* outer = Field<uint8_t*>(Field<uint8_t*>(model, 0x1c), 4);
        const uint32_t item = ListAtN(outer, i, Field<uint32_t>(outer, 0x1c));
        ++i;
        if (!item)
            return 0;
        const uint32_t x = Payload(item);
        for (uint32_t j = 0;;)
        {
            uint8_t* inner = Field<uint8_t*>(P(x), 4);
            const uint32_t item2 = ListAtN(inner, j, Field<uint32_t>(inner, 0x1c));
            ++j;
            if (!item2)
                break;
            fn_004eb310(B(Payload(item2)), target, a1);
        }
    }
}

// 0x004eb3f0 (stack: track (unused), key; caller pops): the default event callback of 0x004eb480 (not executed in
// recorded runs). Logs "notes at time %d:\n" (key+0) and "{\n", indents the log (0x0063a0f8 += 4), then for each of
// key+8 notes (0xc bytes from key+0x10: {node, text, ?}) logs "Node = \"%s\"\n%s" with 0x004eb670(node) and the text,
// unindents and logs "}\n". Returns the last Log_Printf's EAX.
uint32_t fn_004eb3f0(uint32_t /*a0*/, uint8_t* key)
{
    LogPrintf(0x0058afc8u, D(key, 0));
    LogPrintf(0x0058d57cu);
    GameGlobal<uint32_t>(0x0063a0f8) += 4;
    const uint32_t n = D(key, 8);
    if (n)
    {
        uint8_t* e = key + 0x10;
        for (uint32_t k = n; k != 0; --k)
        {
            const uint32_t text = D(e, 4);
            const uint32_t name = fn_004eb670(D(e, 0));
            LogPrintf(0x0058afb8u, name, text);
            e += 0xc;
        }
    }
    GameGlobal<uint32_t>(0x0063a0f8) -= 4;
    return LogPrintf(0x0058d578u);
}

// 0x004eb480 (esi = animation; stack: from, to, callback (0 = 0x004eb3f0), user; caller pops; executed):
// AnimTrack_FireEvents (guess from behaviour). With an event track (animation+0x24) finds the keys around `from` and
// `to` (0x004eb780 with a record whose +0/+4 are animation+0x14/+0x18) and calls callback(animation, key, user)
// (cdecl) for every key in the covered index range with from <= time < to (forward, ascending) or to < time <= from
// (backward, descending). Returns the last callback's result or key (EAX of the last iteration).
uint32_t AnimTrack_FireEvents(uint8_t* anim, int32_t from, int32_t to, uint32_t callback, uint32_t user)
{
    if (!anim || !Field<uint32_t>(anim, 0x24))
        return 0;
    uint32_t rec[9] = {};
    rec[0] = D(anim, 0x14);
    rec[1] = D(anim, 0x18);
    if (!callback)
        callback = 0x004eb3f0;
    typedef uint32_t(__cdecl * EventFn)(uint8_t*, uint32_t, uint32_t);
    const EventFn fn = reinterpret_cast<EventFn>(callback);
    uint8_t* r = reinterpret_cast<uint8_t*>(rec);
    rec[6] = static_cast<uint32_t>(from);
    uint32_t result = 0;
    if (to >= from)
    {
        AnimTrack_FindKeys(Field<uint8_t*>(anim, 0x24), r);
        int32_t lo = static_cast<int32_t>(rec[2]);
        if (lo > static_cast<int32_t>(rec[3]))
            lo = static_cast<int32_t>(rec[3]);
        rec[6] = static_cast<uint32_t>(to);
        AnimTrack_FindKeys(Field<uint8_t*>(anim, 0x24), r);
        int32_t hi = static_cast<int32_t>(rec[3]);
        if (hi < static_cast<int32_t>(rec[2]))
            hi = static_cast<int32_t>(rec[2]);
        if (lo > hi)
            return static_cast<uint32_t>(hi);
        uint32_t off = static_cast<uint32_t>(lo) * 4 + 0xc;
        for (int32_t n = hi - lo + 1; n != 0; --n, off += 4)
        {
            const uint32_t key = Field<uint32_t>(Field<uint8_t*>(anim, 0x24), off);
            result = key;
            const int32_t t = Field<int32_t>(P(key), 0);
            if (t >= from && t < to)
                result = fn(anim, key, user);
        }
        return result;
    }
    AnimTrack_FindKeys(Field<uint8_t*>(anim, 0x24), r);
    int32_t hi = static_cast<int32_t>(rec[3]);
    if (hi < static_cast<int32_t>(rec[2]))
        hi = static_cast<int32_t>(rec[2]);
    rec[6] = static_cast<uint32_t>(to);
    AnimTrack_FindKeys(Field<uint8_t*>(anim, 0x24), r);
    int32_t lo = static_cast<int32_t>(rec[2]);
    if (lo > static_cast<int32_t>(rec[3]))
        lo = static_cast<int32_t>(rec[3]);
    if (hi < lo)
        return static_cast<uint32_t>(lo);
    uint32_t off = static_cast<uint32_t>(hi) * 4 + 0xc;
    for (int32_t n = hi - lo + 1; n != 0; --n, off -= 4)
    {
        const uint32_t key = Field<uint32_t>(Field<uint8_t*>(anim, 0x24), off);
        result = key;
        const int32_t t = Field<int32_t>(P(key), 0);
        if (!(t > from) && t > to)
            result = fn(anim, key, user);
    }
    return result;
}

// 0x004eb5d0 (eax = skeleton, ecx = fallback node, edx = first key or 0; stack: key; caller pops; executed): with edx,
// looks edx up in skeleton+0x28 -> +4 (0x004e0f40) and takes the found item's payload as the node; without edx the
// node is skeleton+0x2c. When the first lookup misses, the node is the value pushed from ECX (push ecx is the
// function's local slot, read back at 0x004eb619): callers that pass ECX = 0 (ai_06, anim_01: "ECX is not read")
// would fault there. Then looks key up in node+4 and returns the found item's payload, or 0.
uint32_t fn_004eb5d0(uint8_t* skel, uint32_t fallback, uint32_t first, uint32_t key)
{
    uint32_t node;
    if (first)
    {
        const uint32_t found = ListFind(Field<uint8_t*>(Field<uint8_t*>(skel, 0x28), 4), first);
        node = found ? Payload(found) : fallback;
    }
    else
    {
        node = D(skel, 0x2c);
    }
    const uint32_t found = ListFind(Field<uint8_t*>(P(node), 4), key);
    return found ? Payload(found) : 0;
}

// 0x004eb670 (ecx unused (local slot); stack: node; caller pops; executed): same walk as 0x004eb280 over
// node+0xc -> +0x28 -> +4: returns payload-of-inner-item + 4 for the inner item whose payload is node, else 0 (also
// for node 0). Used by 0x004eb3f0 as the node's name ("Node = \"%s\"").
uint32_t fn_004eb670(uint32_t node)
{
    if (!node)
        return 0;
    uint8_t* outer = Field<uint8_t*>(Field<uint8_t*>(Field<uint8_t*>(P(node), 0xc), 0x28), 4);
    const uint32_t n = Field<uint32_t>(outer, 0x1c);
    for (uint32_t i = 0;;)
    {
        const uint32_t item = ListAtN(outer, i, n);
        ++i;
        if (!item)
            return 0;
        uint8_t* inner = Field<uint8_t*>(P(Payload(item)), 4);
        const uint32_t m = Field<uint32_t>(inner, 0x1c);
        for (uint32_t j = 0;;)
        {
            const uint32_t item2 = ListAtN(inner, j, m);
            ++j;
            if (!item2)
                break;
            const uint32_t off = Field<uint16_t>(P(item2), 2);
            if (Field<uint32_t>(P(item2 + off), 0) == node)
                return item2 + off + 4;
        }
    }
}

// 0x004eb710 (eax = object; stack: time; caller pops; returns st0; executed): samples the object's scalar track
// (+0xa8): none -> +0x9c; no keys (+8 == 0) -> track+0xc; else record {+0 = [+0xc]+0x14, +4 = [+0xc]+0x18, time},
// 0x004eb780 and the track's sampler (out = a local initialised to +0x9c). Caller 0x0040a4d0 consumes st0.
float fn_004eb710(uint8_t* obj, uint32_t time)
{
    uint8_t* hdr = Field<uint8_t*>(obj, 0xc);
    uint8_t* track = Field<uint8_t*>(obj, 0xa8);
    uint32_t result = D(obj, 0x9c);
    if (!track)
        return bf(result);
    if (!D(track, 8))
        return F(track, 0xc);
    uint32_t rec[9] = {};
    rec[0] = D(hdr, 0x14);
    rec[1] = D(hdr, 0x18);
    rec[6] = time;
    AnimTrack_FindKeys(track, reinterpret_cast<uint8_t*>(rec));
    CallSampler(D(track, 4), u(&result), u(rec), 0, u(track), 0);
    return bf(result);
}

// 0x004eb780 (eax = track; stack: record; caller pops; executed): AnimTrack_FindKeys (name from behaviour). time =
// record+0x18; unless track byte +3 bit 2 or a zero range (last - first key time), wraps the time: before the first
// key by the pre mode (byte +1: 2/3 loop = last - (last - t) % range; 4 ping-pong, mirrored on odd periods), after the
// last key by the post mode (byte +2: 2/3 = (t - first) % range + first; 4 ping-pong). Binary search over the key
// times (hi = count, lo = 0); writes +8 index, +0x10 key, +0xc/+0x14 the next key and +0x1c/+0x20 the fraction and
// 1 - fraction: before the found key (time < its time): +0xc = index, +0x14 = key, +8 = count - 1, +0x10 = last key,
// fraction 1.0 (returns the last key); at the last key: next = key 0, fraction 0 (returns 0); else fraction = (t -
// t0) / (t1 - t0) (returns t0). The original also uses its argument slot as a temporary.
uint32_t AnimTrack_FindKeys(uint8_t* track, uint8_t* rec)
{
    int32_t t = Field<int32_t>(rec, 0x18);
    const uint32_t count = D(track, 8);
    const uint32_t* keys = reinterpret_cast<const uint32_t*>(track + 0xc);
    const int32_t first = Field<int32_t>(P(keys[0]), 0);
    const int32_t last = Field<int32_t>(P(keys[count - 1]), 0);
    const int32_t range = last - first;
    if (!(Field<uint8_t>(track, 3) & 2) && range != 0)
    {
        if (t < first)
        {
            const uint8_t mode = Field<uint8_t>(track, 1);
            if (mode == 2 || mode == 3)
            {
                t = last - (last - t) % range;
            }
            else if (mode == 4)
            {
                const int32_t q = (last - t) / range;
                t = last - (last - t) % range;
                if (q & 1)
                    t = last - t + first;
            }
        }
        if (t > last)
        {
            const uint8_t mode = Field<uint8_t>(track, 2);
            if (mode == 2 || mode == 3)
            {
                t = (t - first) % range + first;
            }
            else if (mode == 4)
            {
                const int32_t q = (t - first) / range;
                t = (t - first) % range + first;
                if (q & 1)
                    t = last - t + first;
            }
        }
    }
    uint32_t hi = count;
    uint32_t lo = 0;
    uint32_t idx = count;
    uint32_t mid = hi >> 1;
    if (hi != mid)
    {
        do
        {
            idx = mid;
            const int32_t d = t - Field<int32_t>(P(keys[mid]), 0);
            if (d == 0)
                break;
            if (d < 0)
                hi = mid;
            else
                lo = mid;
            mid = ((hi - lo) >> 1) + lo;
        } while (idx != mid);
    }
    D(rec, 8) = idx;
    const uint32_t key = keys[idx];
    D(rec, 0x10) = key;
    if (Field<int32_t>(P(key), 0) > t)
    {
        D(rec, 0xc) = idx;
        D(rec, 0x14) = key;
        D(rec, 8) = count - 1;
        D(rec, 0x1c) = 0x3f800000;
        F(rec, 0x20) = K(0x005bcb2c) - F(rec, 0x1c);
        const uint32_t lastKey = keys[D(track, 8) - 1];
        D(rec, 0x10) = lastKey;
        return lastKey;
    }
    if (idx == count - 1)
    {
        D(rec, 0xc) = 0;
        D(rec, 0x1c) = 0;
        F(rec, 0x20) = K(0x005bcb2c) - F(rec, 0x1c);
        D(rec, 0x14) = keys[0];
        return 0;
    }
    D(rec, 0xc) = idx + 1;
    const uint32_t next = keys[idx + 1];
    D(rec, 0x14) = next;
    const int32_t t0 = Field<int32_t>(P(D(rec, 0x10)), 0);
    const int32_t num = t - t0;
    const int32_t den = Field<int32_t>(P(next), 0) - t0;
    F(rec, 0x1c) = static_cast<float>(static_cast<double>(num) / static_cast<double>(den));
    F(rec, 0x20) = K(0x005bcb2c) - F(rec, 0x1c);
    return static_cast<uint32_t>(t0);
}

// ================================================================================================================
// Track samplers: cdecl (out, record), stored in track +4 (no direct callers; not executed in recorded runs)
// ================================================================================================================

// 0x004ebb50 (stack: out, record): scalar Hermite curve: s = 0x004eba50(fraction, key+0x14, next+0x10), basis
// h00 = 1 + (2s^3 - 3s^2), h01 = -(2s^3 - 3s^2), h10 = (s - 2s^2) + s^3, h11 = s^3 - s^2; *out = ((h11 * next+0x1c +
// h10 * key+0x18) + h01 * next+0x20) + h00 * key+0x20. Returns 0 (the original returns FPU status bits in EAX).
uint32_t fn_004ebb50(float* out, uint8_t* rec)
{
    uint8_t* a = Field<uint8_t*>(rec, 0x10);
    uint8_t* b = Field<uint8_t*>(rec, 0x14);
    const float s = CurveParam(F(rec, 0x1c), D(a, 0x14), D(b, 0x10));
    const float s2 = s * s;
    const float s3 = s2 * s;
    const float A = (s3 + s3) - s2 * K(0x005bcb7c);
    const float h00 = K(0x005bcb2c) + A;
    const float h01 = -A;
    const float h10 = (s - (s2 + s2)) + s3;
    const float h11 = s3 - s2;
    *out = ((h11 * F(b, 0x1c) + h10 * F(a, 0x18)) + h01 * F(b, 0x20)) + h00 * F(a, 0x20);
    return 0;
}

// 0x004ebbe0 (stack: out, record): evaluates a 1-component curve through 0x004eb930 with the block {record, key+0x10,
// key+0xc, next+8, next+0x10, out, 1}. Returns the block address (0x004eb930 returns EAX unchanged).
uint32_t fn_004ebbe0(uint32_t out, uint8_t* rec)
{
    uint8_t* a = Field<uint8_t*>(rec, 0x10);
    uint8_t* b = Field<uint8_t*>(rec, 0x14);
    uint32_t block[7] = {u(rec), u(a + 0x10), u(a + 0xc), u(b + 8), u(b + 0x10), out, 1};
    return CurveEval(u(block));
}

// 0x004ebc30 (stack: out, record): linear scalar: *out = key+4 * (1 - fraction) + next+4 * fraction. Returns out.
uint32_t fn_004ebc30(float* out, uint8_t* rec)
{
    uint8_t* a = Field<uint8_t*>(rec, 0x10);
    uint8_t* b = Field<uint8_t*>(rec, 0x14);
    *out = F(a, 4) * F(rec, 0x20) + F(b, 4) * F(rec, 0x1c);
    return u(out);
}

// 0x004ebc50 (stack: out, record): vec3 Hermite curve, same basis as 0x004ebb50: out[i] = ((h11 * next+0x24+4i +
// h01 * next+0x30+4i) + h10 * key+0x18+4i) + h00 * key+0x30+4i. Returns 0 (FPU status bits in the original).
uint32_t fn_004ebc50(float* out, uint8_t* rec)
{
    uint8_t* a = Field<uint8_t*>(rec, 0x10);
    uint8_t* b = Field<uint8_t*>(rec, 0x14);
    const float s = CurveParam(F(rec, 0x1c), D(a, 0x14), D(b, 0x10));
    const float s2 = s * s;
    const float s3 = s2 * s;
    const float A = (s3 + s3) - s2 * K(0x005bcb7c);
    const float h00 = K(0x005bcb2c) + A;
    const float h01 = -A;
    const float h10 = (s - (s2 + s2)) + s3;
    const float h11 = s3 - s2;
    for (uint32_t i = 0; i < 3; ++i)
        out[i] = ((h11 * F(b, 0x24 + 4 * i) + h01 * F(b, 0x30 + 4 * i)) + h10 * F(a, 0x18 + 4 * i)) +
                 h00 * F(a, 0x30 + 4 * i);
    return 0;
}

// 0x004ebd20 (stack: out, record): 3-component curve through 0x004eb930 with {record, key+0x20, key+0x14, next+8,
// next+0x20, out, 3}. Returns the block address.
uint32_t fn_004ebd20(uint32_t out, uint8_t* rec)
{
    uint8_t* a = Field<uint8_t*>(rec, 0x10);
    uint8_t* b = Field<uint8_t*>(rec, 0x14);
    uint32_t block[7] = {u(rec), u(a + 0x20), u(a + 0x14), u(b + 8), u(b + 0x20), out, 3};
    return CurveEval(u(block));
}

// 0x004ebd70 (stack: out, record): linear vec3: out[i] = next+4+4i * fraction + key+4+4i * (1 - fraction). Returns
// the record (EAX).
uint32_t fn_004ebd70(float* out, uint8_t* rec)
{
    uint8_t* a = Field<uint8_t*>(rec, 0x10);
    uint8_t* b = Field<uint8_t*>(rec, 0x14);
    for (uint32_t i = 0; i < 3; ++i)
        out[i] = F(b, 4 + 4 * i) * F(rec, 0x1c) + F(a, 4 + 4 * i) * F(rec, 0x20);
    return u(rec);
}

// 0x004ebdc0 (stack: out, record): quaternion curve with time warp: s = 0x004eba50(fraction, key+0x14, next+0x10),
// then 0x004f16b0(ecx = next+0x38, edx = key+0x38; out, key+0x18, next+0x28, s). Returns 0x004f16b0's EAX.
uint32_t fn_004ebdc0(uint32_t out, uint8_t* rec)
{
    uint8_t* a = Field<uint8_t*>(rec, 0x10);
    uint8_t* b = Field<uint8_t*>(rec, 0x14);
    const float s = CurveParam(F(rec, 0x1c), D(a, 0x14), D(b, 0x10));
    return QuatSquad(u(b + 0x38), u(a + 0x38), out, u(a + 0x18), u(b + 0x28), fb(s));
}

// 0x004ebe00 (stack: out, record): 0x004f16b0(ecx = next+0x18, edx = key+0x18; out, key+8, next+8, fraction).
uint32_t fn_004ebe00(uint32_t out, uint8_t* rec)
{
    uint8_t* a = Field<uint8_t*>(rec, 0x10);
    uint8_t* b = Field<uint8_t*>(rec, 0x14);
    return QuatSquad(u(b + 0x18), u(a + 0x18), out, u(a + 8), u(b + 8), D(rec, 0x1c));
}

// 0x004ebe30 (stack: out, record): 0x004f15a0(ecx = next+4, edx = key+4, esi = out; fraction).
uint32_t fn_004ebe30(uint32_t out, uint8_t* rec)
{
    return QuatSlerp(D(rec, 0x14) + 4, D(rec, 0x10) + 4, out, D(rec, 0x1c));
}

// 0x004ebe60 (stack: out, record): packed quaternion curve: unpacks (0x004f1920) next+0x20, next+0x1c, key+0x18 and
// key+0x20 into four locals (in that order), then 0x004f16b0(ecx = next+0x20's, edx = key+0x20's; out, key+0x18's,
// next+0x1c's, fraction).
uint32_t fn_004ebe60(uint32_t out, uint8_t* rec)
{
    uint8_t* a = Field<uint8_t*>(rec, 0x10);
    uint8_t* b = Field<uint8_t*>(rec, 0x14);
    const uint32_t t = D(rec, 0x1c);
    float q[4][4];
    const uint32_t q1 = QuatUnpack(u(q[3]), u(b + 0x20));
    const uint32_t q2 = QuatUnpack(u(q[2]), u(b + 0x1c));
    const uint32_t q3 = QuatUnpack(u(q[1]), u(a + 0x18));
    const uint32_t q4 = QuatUnpack(u(q[0]), u(a + 0x20));
    return QuatSquad(q1, q4, out, q3, q2, t);
}

// 0x004ebed0 (stack: out, record): as 0x004ebe60 with next+0xc, next+8, key+8, key+0xc.
uint32_t fn_004ebed0(uint32_t out, uint8_t* rec)
{
    uint8_t* a = Field<uint8_t*>(rec, 0x10);
    uint8_t* b = Field<uint8_t*>(rec, 0x14);
    const uint32_t t = D(rec, 0x1c);
    float q[4][4];
    const uint32_t q1 = QuatUnpack(u(q[3]), u(b + 0xc));
    const uint32_t q2 = QuatUnpack(u(q[2]), u(b + 8));
    const uint32_t q3 = QuatUnpack(u(q[1]), u(a + 8));
    const uint32_t q4 = QuatUnpack(u(q[0]), u(a + 0xc));
    return QuatSquad(q1, q4, out, q3, q2, t);
}

// 0x004ebf40 (stack: out, record): unpacks next+4 and key+4 (0x004f1920) and slerps them: 0x004f15a0(ecx = next's,
// edx = key's, esi = out; fraction).
uint32_t fn_004ebf40(uint32_t out, uint8_t* rec)
{
    uint8_t* a = Field<uint8_t*>(rec, 0x10);
    uint8_t* b = Field<uint8_t*>(rec, 0x14);
    const uint32_t t = D(rec, 0x1c);
    float q[2][4];
    const uint32_t q1 = QuatUnpack(u(q[1]), u(b + 4));
    const uint32_t q2 = QuatUnpack(u(q[0]), u(a + 4));
    return QuatSlerp(q1, q2, out, t);
}
