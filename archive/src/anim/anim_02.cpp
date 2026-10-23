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
// 0x004e7880 (ecx = vector, edx = out): normalises and returns |v| in st0 (popped here; see anim_01).
void Normalize3(uint32_t src, uint32_t dst)
{
    float r;
    __asm {
        mov ecx, src
        mov edx, dst
        mov eax, 0x004e7880
        call eax
        fstp r
    }
    (void)r;
}
// Bone controller callbacks (skeleton +0x14 -> [bone index * 8]): cdecl (out transform, in transform, bone, parent
// bone, skeleton, mode, layer) - the leg IK controllers of anim_01.
inline void CallBoneController(uint32_t fn, uint32_t out, uint32_t in, uint32_t bone, uint32_t parent, uint32_t skel,
                               uint32_t mode, uint32_t layer)
{
    reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(fn)(
        out, in, bone, parent, skel, mode, layer);
}
// The controller of a bone, or 0: skeleton +0x14 (table) -> [bone index * 8].
inline uint32_t BoneController(uint8_t* skel, uint32_t bone)
{
    uint8_t* table = Field<uint8_t*>(skel, 0x14);
    if (!table)
        return 0;
    return Field<uint32_t>(table, Field<uint16_t>(P(bone), 0xa) * 8u);
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
uint32_t fn_004ebf90(uint8_t* skel, uint8_t* bone, uint32_t stamp);
uint32_t fn_004ec2f0(uint32_t xf, uint8_t* parent, uint8_t* bone, uint8_t* skel, uint32_t stamp, uint32_t weight,
                     uint32_t flags);
uint32_t fn_004ec410(uint8_t* bone, uint32_t animStamp, uint8_t* skel, uint32_t stamp, uint32_t weight,
                     uint32_t flags);
uint32_t fn_004ec530(uint32_t count, uint8_t** cursor, uint8_t* recs, uint8_t* skel, uint32_t stamp, uint32_t mode,
                     uint32_t flags);
uint32_t fn_004ec600(uint8_t* skel, uint8_t* recs, uint32_t count, uint32_t stamp, uint32_t weight, uint32_t flags);
uint32_t fn_004ec800(uint8_t* recs, uint32_t count, uint8_t* skel, uint32_t stamp, uint32_t weight, uint32_t flags);
uint32_t fn_004eccc0(uint32_t count, uint8_t* skel, uint8_t* recs, uint32_t stamp, uint32_t weight, uint32_t flags);
uint32_t fn_004ed260(uint8_t* pose);

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
// function's local slot, read back at 0x004eb619), i.e. the caller's leftover ECX. All five original call sites
// (0x004426e5, 0x00453de6, 0x00485cf6, 0x0049b4f7, 0x0049efab) pass edx = 0, so that path is not reached from them;
// recovered callers still pass the site's ECX where it is known. Then looks key up in node+4 and returns the found
// item's payload, or 0.
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

// ================================================================================================================
// Pose evaluation: bones, local transforms, blending, world matrices
// ================================================================================================================
//
// Bone (from the accesses below; guess): +6 word type (1 stepped track, 2 sampled tracks, 3 aim/look-at), +0xa word
// index, +0xc header (+0x14/+0x18 copied into sample records), +0x10 parent bone, +0x14 aim target bone (type 3),
// +0x18 stamp of the cached local transform, +0x1c local transform (0x2c bytes: position, quaternion at +0x28,
// scale at +0x38, the flags word at +0x44 lies after it), +0x48/+0x4c/+0x50 position/rotation/scale tracks.
// Skeleton: +0x14 bone controller table, +0x18 world matrices (0x48 bytes per bone: 0x44 matrix + stamp at +0x44),
// +0x20 optional transform cache (0x30 bytes per bone: 0x2c transform + stamp at +0x2c).

namespace
{
inline void Negate3(uint8_t* p, uint32_t o)
{
    F(p, o) = -F(p, o);
    F(p, o + 4) = -F(p, o + 4);
    F(p, o + 8) = -F(p, o + 8);
}
// Find the keys of `track` at the record's time and run the track's sampler into `out` (the 0x004eb780 + [track+4]
// pair used by 0x004ebf90).
inline void SampleTrack(uint8_t* track, uint8_t* rec, uint32_t out, uint8_t* bone, uint8_t* skel)
{
    AnimTrack_FindKeys(track, rec);
    CallSampler(D(track, 4), out, u(rec), u(bone), 0, u(skel));
}
}  // namespace

// 0x004ebf90 (eax = skeleton; stack: bone, stamp; caller pops; executed): the bone's local transform at `stamp`
// (cached: returns bone+0x1c at once when bone+0x18 == stamp). Record {+0/+4 = header +0x14/+0x18, time = stamp}.
// Type 1: copies the 11 dwords after the current key of track +0x4c (stepped, no sampler) to +0x1c and negates the
// quaternion xyz (+0x28..+0x30). Type 2: samples +0x48 into the position (+0x44 |= 0x10) and +0x4c into the
// quaternion (negated xyz, +0x44 |= 1). Type 3 (aim): local position of the bone's +0x14 target (world matrix via
// 0x004ec410 with stamp code 0x004ed260) in the parent's frame (Matrix_Inverse of the parent's 0x004ec410 matrix,
// 0x004e36a0) or as is without a parent (Matrix_Identity), optional position track +0x48; builds a basis from
// (position - target) normalised, the matrix's z column and two cross products (0x004e7830, 0x004e7880), converts it to
// a quaternion (0x004e35a0) and, with a roll track +0x4c (sampled into the global 0x005fb8a8, or its constant +0xc
// without keys), multiplies it by the axis-angle quaternion of 0x005fb89c (0x004f13d0, 0x004f1710) into +0x28. Types
// 2 and 3: scale track +0x50 into +0x38 (negated when +0x44 bit 8; +0x44 |= 2). Stores the stamp; returns bone+0x1c.
// The registers the samplers receive are not reproduced exactly (they read only their stack arguments).
uint32_t fn_004ebf90(uint8_t* skel, uint8_t* bone, uint32_t stamp)
{
    if (D(bone, 0x18) == stamp)
        return u(bone + 0x1c);
    uint8_t* hdr = Field<uint8_t*>(bone, 0xc);
    uint32_t R[9] = {};
    R[0] = D(hdr, 0x14);
    R[1] = D(hdr, 0x18);
    R[6] = stamp;
    uint8_t* rec = reinterpret_cast<uint8_t*>(R);
    const uint16_t type = Field<uint16_t>(bone, 6);
    if (type == 1)
    {
        AnimTrack_FindKeys(Field<uint8_t*>(bone, 0x4c), rec);
        std::memcpy(bone + 0x1c, B(D(rec, 0x10)) + 4, 0x2c);
        Negate3(bone, 0x28);
        D(bone, 0x18) = stamp;
        return u(bone + 0x1c);
    }
    if (type == 2)
    {
        if (uint8_t* t = Field<uint8_t*>(bone, 0x48))
        {
            SampleTrack(t, rec, u(bone + 0x1c), bone, skel);
            D(bone, 0x44) |= 0x10;
        }
        if (uint8_t* t = Field<uint8_t*>(bone, 0x4c))
        {
            SampleTrack(t, rec, u(bone + 0x28), bone, skel);
            Negate3(bone, 0x28);
            D(bone, 0x44) |= 1;
        }
    }
    else if (type == 3)
    {
        const uint32_t code = fn_004ed260(skel);
        uint8_t* target = B(fn_004ec410(Field<uint8_t*>(bone, 0x14), stamp, skel, code, 0, 0));
        float v10[3];
        float q1c[4];
        float q2c[4];
        float m[17];
        uint32_t* q1cBits = reinterpret_cast<uint32_t*>(q1c);
        q1cBits[0] = D(target, 0xc);
        q1cBits[1] = D(target, 0x1c);
        q1cBits[2] = D(target, 0x2c);
        if (uint8_t* parent = Field<uint8_t*>(bone, 0x10))
        {
            const uint32_t pm = fn_004ec410(parent, stamp, skel, code, 0, 0);
            orig::fn_004e2b40(pm, u(m));
            orig::fn_004e36a0(u(q1c), u(m), 1, u(v10));
        }
        else
        {
            orig::fn_004e28f0(u(m));
            std::memcpy(v10, q1c, 0xc);
        }
        if (uint8_t* t = Field<uint8_t*>(bone, 0x48))
        {
            SampleTrack(t, rec, u(bone + 0x1c), bone, skel);
            D(bone, 0x44) |= 0x10;
        }
        v10[0] = F(bone, 0x1c) - v10[0];
        v10[1] = F(bone, 0x20) - v10[1];
        v10[2] = F(bone, 0x24) - v10[2];
        Normalize3(u(v10), u(v10));
        std::memcpy(&q1c[0], &m[2], 4);
        std::memcpy(&q1c[1], &m[6], 4);
        std::memcpy(&q1c[2], &m[10], 4);
        orig::fn_004e7830(u(v10), u(q1c), u(q2c));
        Normalize3(u(q2c), u(q2c));
        orig::fn_004e7830(u(q2c), u(v10), u(q1c));
        std::memcpy(&m[0], &q2c[0], 4);
        std::memcpy(&m[4], &q2c[1], 4);
        std::memcpy(&m[8], &q2c[2], 4);
        std::memcpy(&m[1], &q1c[0], 4);
        std::memcpy(&m[5], &q1c[1], 4);
        std::memcpy(&m[2], &v10[0], 4);
        std::memcpy(&m[9], &q1c[2], 4);
        std::memcpy(&m[6], &v10[1], 4);
        std::memcpy(&m[10], &v10[2], 4);
        reinterpret_cast<uint32_t*>(m)[16] = 1;
        orig::fn_004e35a0(u(m), u(q1c));
        if (uint8_t* t = Field<uint8_t*>(bone, 0x4c))
        {
            if (D(t, 8))
                SampleTrack(t, rec, 0x005fb8a8u, bone, skel);
            else
                GameGlobal<uint32_t>(0x005fb8a8) = D(t, 0xc);
            orig::fn_004f13d0(0x005fb89cu, u(q2c));
            orig::fn_004f1710(u(q2c), u(q1c), u(bone + 0x28));
        }
    }
    else
    {
        D(bone, 0x18) = stamp;
        return u(bone + 0x1c);
    }
    if (uint8_t* t = Field<uint8_t*>(bone, 0x50))
    {
        SampleTrack(t, rec, u(bone + 0x38), bone, skel);
        const uint32_t f = D(bone, 0x44);
        if (f & 8)
            Negate3(bone, 0x38);
        D(bone, 0x44) = f | 2;
    }
    D(bone, 0x18) = stamp;
    return u(bone + 0x1c);
}

// 0x004ec2f0 (edx = local transform; stack: parent bone, bone, skeleton, stamp, weight (float), flags; caller pops;
// executed): with a transform cache (skeleton+0x20): stamps the bone's entry, blends it towards the new transform when
// weight > 0 (0x004e3510(eax = cache entry; local, transform, weight) into a local 0x2c-byte transform) and with flags
// bit 4 copies the (blended) transform into the cache. Unless flags bit 1: stamps the world matrix (+0x18, 0x48 per
// bone, stamp at +0x44) and builds it - with a parent: 0x004e3370 into a local matrix, then Matrix_Multiply(local;
// world, parent's world); without: 0x004e3370 straight into the world matrix. Returns ~flags when bit 1 is set
// (the original's EAX), 0x004e3370's EAX without a parent, else 0.
uint32_t fn_004ec2f0(uint32_t xf, uint8_t* parent, uint8_t* bone, uint8_t* skel, uint32_t stamp, uint32_t weight,
                     uint32_t flags)
{
    alignas(4) uint8_t local[0x2c];
    float m[17];
    const uint32_t idx = Field<uint16_t>(bone, 0xa);
    uint8_t* cache = Field<uint8_t*>(skel, 0x20);
    if (cache)
    {
        D(cache + idx * 0x30, 0x2c) = stamp;
        if (bf(weight) > K(0x0056c05c))
        {
            orig::fn_004e3510(u(Field<uint8_t*>(skel, 0x20) + idx * 0x30), u(local), u(local), xf, weight);
            xf = u(local);
        }
        if (flags & 4)
            std::memcpy(Field<uint8_t*>(skel, 0x20) + idx * 0x30, P(xf), 0x2c);
    }
    if (flags & 1)
        return ~flags;
    D(Field<uint8_t*>(skel, 0x18) + idx * 0x48, 0x44) = stamp;
    if (parent)
    {
        orig::fn_004e3370(u(m), xf);
        uint8_t* mats = Field<uint8_t*>(skel, 0x18);
        orig::fn_004e2910(u(m), u(mats + Field<uint16_t>(bone, 0xa) * 0x48),
                          u(mats + Field<uint16_t>(parent, 0xa) * 0x48));
        return 0;
    }
    return orig::fn_004e3370(u(Field<uint8_t*>(skel, 0x18) + idx * 0x48), xf);
}

// 0x004ec410 (stack: bone, anim stamp, skeleton, stamp, weight, flags; caller pops; executed): the bone's world
// matrix (or cache entry with flags bit 1) at `stamp`: walks up the parents (+0x10) while their entry's stamp
// (world +0x44, or cache +0x2c with flags bit 1; no cache counts as stale) differs, then from the topmost stale bone
// down: local transform 0x004ebf90(bone, anim stamp), the bone controller (skeleton+0x14 table; cdecl (local out,
// transform, bone, parent, skeleton, 0, 0)) and 0x004ec2f0. Returns the bone's world matrix (skeleton+0x18 + index *
// 0x48), or 0 with flags bit 1 / no bone. The original keeps up to 32 pending bones in its frame.
uint32_t fn_004ec410(uint8_t* bone, uint32_t animStamp, uint8_t* skel, uint32_t stamp, uint32_t weight, uint32_t flags)
{
    if (!bone)
        return 0;
    uint32_t pending[32];
    uint32_t n = 0;
    alignas(4) uint8_t local[0x2c];
    const uint32_t useWorld = (~flags) & 1;
    for (;;)
    {
        const uint32_t idx = Field<uint16_t>(bone, 0xa);
        bool current;
        if (useWorld)
        {
            current = D(Field<uint8_t*>(skel, 0x18) + idx * 0x48, 0x44) == stamp;
        }
        else
        {
            uint8_t* cache = Field<uint8_t*>(skel, 0x20);
            current = cache && D(cache + idx * 0x30, 0x2c) == stamp;
        }
        if (current)
            break;
        pending[n++] = u(bone);
        bone = Field<uint8_t*>(bone, 0x10);
        if (!bone)
            break;
    }
    while (n != 0)
    {
        uint8_t* parent = bone;
        bone = B(pending[--n]);
        uint32_t xf = fn_004ebf90(skel, bone, animStamp);
        if (const uint32_t cb = BoneController(skel, u(bone)))
        {
            CallBoneController(cb, u(local), xf, u(bone), u(parent), u(skel), 0, 0);
            xf = u(local);
        }
        fn_004ec2f0(xf, parent, bone, skel, stamp, weight, flags);
    }
    if (!bone || !useWorld)
        return 0;
    return u(Field<uint8_t*>(skel, 0x18) + Field<uint16_t>(bone, 0xa) * 0x48);
}

// 0x004ec530 (eax = record count, ecx unused (local slot), edi = cursor (pointer to the current 0xc-byte chain entry);
// stack: records (0x1c bytes from +0xc: {bone, time, mode, weight, ...}), skeleton, stamp, mode, flags; caller pops;
// executed): builds one layer's bone chain for the blend: repeatedly clears the entry at the cursor and fills it from
// the first record of this mode whose current bone is set (taking a later one only while the entry is empty or the
// bone's byte +8 & 0x3f is nonzero, which ends the pass), advancing every matching record to its bone's parent; stops
// when the entry stays empty or its bone's world matrix (cache entry with flags bit 1; no cache = stale) is current,
// else advances the cursor by 0xc. Returns the number of entries added. guess: per-layer bone chain gathering.
uint32_t fn_004ec530(uint32_t count, uint8_t** cursor, uint8_t* recs, uint8_t* skel, uint32_t stamp, uint32_t mode,
                     uint32_t flags)
{
    uint32_t added = 0;
    for (;;)
    {
        D(*cursor, 0) = 0;
        uint32_t stop = 0;
        if (count)
        {
            uint8_t* r = recs + 0xc;
            uint32_t left = count;
            do
            {
                --left;
                if (stop)
                    break;
                if (D(r, 8) == mode)
                {
                    uint32_t b = D(r, 0);
                    if (b)
                    {
                        uint8_t* e = *cursor;
                        if (D(e, 0) == 0 || (Field<uint8_t>(P(b), 8) & 0x3f))
                        {
                            D(e, 0) = b;
                            D(*cursor, 4) = D(r, 4);
                            D(*cursor, 8) = D(r, 0xc);
                            b = D(r, 0);
                            stop = Field<uint8_t>(P(b), 8) & 0x3f;
                        }
                        D(r, 0) = Field<uint32_t>(P(b), 0x10);
                    }
                }
                r += 0x1c;
            } while (left != 0);
        }
        uint8_t* e = *cursor;
        const uint32_t b = D(e, 0);
        if (!b)
            return added;
        const uint32_t idx = Field<uint16_t>(P(b), 0xa);
        if (!(flags & 1))
        {
            if (D(Field<uint8_t*>(skel, 0x18) + idx * 0x48, 0x44) == stamp)
                return added;
        }
        else
        {
            uint8_t* cache = Field<uint8_t*>(skel, 0x20);
            if (cache && D(cache + idx * 0x30, 0x2c) == stamp)
                return added;
        }
        ++added;
        *cursor = e + 0xc;
    }
}

// 0x004ec600 (esi = skeleton; stack: records, count, stamp, weight, flags; caller pops; executed): two-layer blend
// (record modes 0 and 1): gathers both chains (0x004ec530 into two 32-entry frame arrays), aligns layer 1 to layer 0's
// depth (zero-filled entries, or the cursor moved back), then from the top of layer 0 down: local transform
// (0x004ebf90 + bone controller with mode/layer 0, 0), and when layer 1 has a bone and the layer-0 entry's weight is
// nonzero, layer 1's transform (controller with 1, 1) blended in (0x004e3510(eax = layer 1; out, layer 0, weight));
// 0x004ec2f0(parent = the previous level's bone). Returns layer 0's bottom bone's world matrix (0 with flags bit 1).
uint32_t fn_004ec600(uint8_t* skel, uint8_t* recs, uint32_t count, uint32_t stamp, uint32_t weight, uint32_t flags)
{
    alignas(4) uint8_t arr0[0x180];
    alignas(4) uint8_t arr1[0x180];
    alignas(4) uint8_t t0buf[0x2c];
    alignas(4) uint8_t t1buf[0x2c];
    alignas(4) uint8_t blend[0x2c];
    uint8_t* cur0 = arr0;
    uint8_t* cur1 = arr1;
    const int32_t r0 = static_cast<int32_t>(fn_004ec530(count, &cur0, recs, skel, stamp, 0, flags));
    const int32_t r1 = static_cast<int32_t>(fn_004ec530(count, &cur1, recs, skel, stamp, 1, flags));
    int32_t diff = r0 - r1;
    uint8_t* p1 = cur1;
    if (diff > 0)
    {
        for (; diff != 0; --diff)
        {
            D(p1, 0) = 0;
            p1 += 0xc;
        }
    }
    else if (diff < 0)
    {
        p1 = cur1 + diff * 0xc;
    }
    uint8_t* e = cur0;
    while (e != arr0)
    {
        const uint32_t parent = D(e, 0);
        e -= 0xc;
        p1 -= 0xc;
        uint32_t xf = fn_004ebf90(skel, B(D(e, 0)), D(e, 4));
        if (const uint32_t cb = BoneController(skel, D(e, 0)))
        {
            CallBoneController(cb, u(t0buf), xf, D(e, 0), parent, u(skel), 0, 0);
            xf = u(t0buf);
        }
        if (D(p1, 0) && !(F(e, 8) == K(0x0056c05c)))
        {
            uint32_t xf1 = fn_004ebf90(skel, B(D(p1, 0)), D(p1, 4));
            if (const uint32_t cb = BoneController(skel, D(p1, 0)))
            {
                CallBoneController(cb, u(t1buf), xf1, D(p1, 0), parent, u(skel), 1, 1);
                xf1 = u(t1buf);
            }
            orig::fn_004e3510(xf1, 0, u(blend), xf, D(e, 8));
            xf = u(blend);
        }
        fn_004ec2f0(xf, B(parent), B(D(e, 0)), skel, stamp, weight, flags);
    }
    const uint32_t leaf = D(arr0, 0);
    if (!leaf || (flags & 1))
        return 0;
    return u(Field<uint8_t*>(skel, 0x18) + Field<uint16_t>(P(leaf), 0xa) * 0x48);
}

// 0x004ec800 (stack: records, count, skeleton, stamp, weight, flags; caller pops; executed): four-layer blend for
// record modes with bits 0x10..0x80 (anim_01 0x00408ee0 builds them: 0x10/0x11/0x40/0x41 and 0x20/0x21/0x80/0x81).
// m = OR of all record modes; layers A = m & 0x50, B = A | 1 (or A itself when A is 0), C = (m & 0xa0) | 1, D =
// m & 0xa0 (or C when 0), gathered with 0x004ec530 into four 32-entry arrays; two layer pairs are used (A/B always,
// D/C when both D and C have entries); B, C and D are aligned to the deepest chain. For each of A's levels, from the
// top: per pair (D/C first) the primary bone (or the secondary's copied into the primary entry; neither: an identity
// transform) is evaluated (0x004ebf90, controller (mode, pair)), the secondary blended in by the primary entry's
// weight unless it is the same bone and time (0x004e3510); with two pairs the results are blended by B's current
// entry weight; then 0x004ec2f0. Returns A's bottom bone's world matrix (0 with flags bit 1).
uint32_t fn_004ec800(uint8_t* recs, uint32_t count, uint8_t* skel, uint32_t stamp, uint32_t weight, uint32_t flags)
{
    alignas(4) uint8_t arrA[0x180];
    alignas(4) uint8_t arrD[0x180];
    alignas(4) uint8_t arrB[0x180];
    alignas(4) uint8_t arrC[0x180];
    alignas(4) uint8_t ta[2][0x2c];
    alignas(4) uint8_t tb[2][0x2c];
    alignas(4) uint8_t tc[2][0x2c];
    alignas(4) uint8_t td[0x2c];
    uint8_t* prim[2] = {arrA, arrD};
    uint8_t* sec[2] = {arrB, arrC};
    uint32_t mode[4];  // the frame's mode slots: A, D, B, C
    uint32_t x[2] = {};
    uint32_t m = 0;
    for (uint32_t k = count; k != 0; --k)
        m |= D(recs + 0x14 + (k - 1) * 0x1c, 0);
    const uint32_t mA = m & 0x50;
    mode[0] = mA;
    const int32_t r0 = static_cast<int32_t>(fn_004ec530(count, &prim[0], recs, skel, stamp, mA, flags));
    int32_t r1;
    if (mA)
    {
        mode[2] = mA | 1;
        r1 = static_cast<int32_t>(fn_004ec530(count, &sec[0], recs, skel, stamp, mA | 1, flags));
    }
    else
    {
        sec[0] = prim[0];
        mode[2] = mA;
        r1 = r0;
    }
    const uint32_t mC = m & 0xa0;
    mode[3] = mC | 1;
    const int32_t r2 = static_cast<int32_t>(fn_004ec530(count, &sec[1], recs, skel, stamp, mC | 1, flags));
    int32_t r3;
    if (mC)
    {
        mode[1] = mC;
        r3 = static_cast<int32_t>(fn_004ec530(count, &prim[1], recs, skel, stamp, mC, flags));
    }
    else
    {
        prim[1] = sec[1];
        r3 = r2;
        mode[1] = mC | 1;
    }
    const int32_t pairs = (r3 != 0 && r2 != 0) ? 2 : 1;
    int32_t deepest = r0 < r3 ? r3 : r0;
    if (deepest < r1)
        deepest = r1;
    if (deepest < r2)
        deepest = r2;
    auto align = [](uint8_t*& p, int32_t n) {
        if (n > 0)
        {
            for (; n != 0; --n)
            {
                D(p, 0) = 0;
                p += 0xc;
            }
        }
        else if (n < 0)
        {
            p = p + n * 0xc;
        }
    };
    align(prim[1], deepest - r3);
    align(sec[0], deepest - r1);
    align(sec[1], deepest - r2);
    for (int32_t level = r0; level != 0; --level)
    {
        const uint32_t parent = D(prim[0], 0);
        for (int32_t k = pairs - 1; k >= 0; --k)
        {
            uint8_t* e = prim[k] - 0xc;
            uint8_t* s = sec[k] - 0xc;
            prim[k] = e;
            sec[k] = s;
            uint32_t b = D(e, 0);
            if (!b)
            {
                b = D(s, 0);
                D(e, 0) = b;
                if (!b)
                {
                    uint8_t* t = ta[k];
                    x[k] = u(t);
                    D(t, 0x14) = 0;
                    D(t, 0x10) = 0;
                    D(t, 0xc) = 0;
                    D(t, 0x18) = 0x3f800000;
                    D(t, 0x24) = 0x3f800000;
                    D(t, 0x20) = 0x3f800000;
                    D(t, 0x1c) = 0x3f800000;
                    D(t, 8) = 0;
                    D(t, 4) = 0;
                    D(t, 0) = 0;
                    continue;
                }
            }
            x[k] = fn_004ebf90(skel, B(b), D(e, 4));
            if (const uint32_t cb = BoneController(skel, D(e, 0)))
            {
                CallBoneController(cb, u(ta[k]), x[k], D(e, 0), parent, u(skel), mode[k], static_cast<uint32_t>(k));
                x[k] = u(ta[k]);
            }
            const uint32_t b2 = D(s, 0);
            if (!b2)
                continue;
            if (b2 == D(e, 0) && D(s, 4) == D(e, 4))
                continue;
            if (!(F(e, 8) > K(0x0056c05c)))
                continue;
            uint32_t y = fn_004ebf90(skel, B(b2), D(s, 4));
            if (const uint32_t cb = BoneController(skel, D(s, 0)))
            {
                CallBoneController(cb, u(tb[k]), y, D(s, 0), parent, u(skel), mode[2 + k], static_cast<uint32_t>(k));
                y = u(tb[k]);
            }
            orig::fn_004e3510(y, 0, u(tc[k]), x[k], D(e, 8));
            x[k] = u(tc[k]);
        }
        uint32_t xf = x[0];
        if (pairs > 1 && F(sec[0], 8) > K(0x0056c05c))
        {
            orig::fn_004e3510(x[1], 0, u(td), x[0], D(sec[0], 8));
            xf = u(td);
            x[0] = xf;
        }
        uint32_t b = D(prim[0], 0);
        if (!b)
            b = D(sec[0], 0);
        fn_004ec2f0(xf, B(parent), B(b), skel, stamp, weight, flags);
    }
    const uint32_t leaf = D(prim[0], 0);
    if (!leaf || (flags & 1))
        return 0;
    return u(Field<uint8_t*>(skel, 0x18) + Field<uint16_t>(P(leaf), 0xa) * 0x48);
}

// 0x004eccc0 (eax = record count, ecx = skeleton, edi = records; stack: stamp, weight, flags; caller pops; executed):
// pose dispatcher: one record -> 0x004ec410(record bone, record time, skeleton, stamp, weight, flags); any record mode
// >= 2 (signed, scanned from the last) -> 0x004ec800; else 0x004ec600 (esi = skeleton). Returns their result.
uint32_t fn_004eccc0(uint32_t count, uint8_t* skel, uint8_t* recs, uint32_t stamp, uint32_t weight, uint32_t flags)
{
    if (count == 1)
        return fn_004ec410(Field<uint8_t*>(recs, 0xc), D(recs, 0x10), skel, stamp, weight, flags);
    for (uint32_t k = count; k != 0; --k)
    {
        if (Field<int32_t>(recs + 0x14 + (k - 1) * 0x1c, 0) >= 2)
            return fn_004ec800(recs, count, skel, stamp, weight, flags);
    }
    return fn_004ec600(skel, recs, count, stamp, weight, flags);
}

namespace
{
// The iterator's compare callback (iterator +4, cdecl (iterator); EAX = the probed index, ESI = iterator in the
// original, reproduced here).
int32_t CallCompare(uint32_t fn, uint32_t it, uint32_t mid)
{
    int32_t r;
    __asm {
        push esi
        mov esi, it
        mov eax, mid
        push esi
        call fn
        add esp, 4
        mov r, eax
        pop esi
    }
    return r;
}
// The binary search 0x004ecd40 inlines (twice) instead of calling 0x004e0f40: iterator {+0 count, +8 index, +0xc last
// compare result, +0x10 items, +0x18 key}. Returns the found item, or 0 (empty list, no match, null item).
uint32_t IterFind(uint8_t* list, uint32_t key)
{
    const uint32_t count = D(list, 0x1c);
    if (!count || !D(list, 0x20))
        return 0;
    uint8_t* it = Field<uint8_t*>(list, 0x24);
    uint32_t hi = count;
    uint32_t lo = 0;
    D(it, 0x18) = key;
    uint32_t mid = hi >> 1;
    D(it, 0) = count;
    D(it, 8) = hi;
    if (hi != mid)
    {
        do
        {
            D(it, 8) = mid;
            const int32_t r = CallCompare(D(it, 4), u(it), mid);
            Field<int32_t>(it, 0xc) = r;
            if (!r)
                break;
            if (r < 0)
                hi = D(it, 8);
            else
                lo = D(it, 8);
            mid = ((hi - lo) >> 1) + lo;
        } while (D(it, 8) != mid);
    }
    if (D(it, 0xc))
        return 0;
    return Field<uint32_t*>(it, 0x10)[D(it, 8)];
}
}  // namespace

// 0x004ecd40 (stack: records, count, out, passes, skeleton, stamp, weight, flags; caller pops; executed): aligns a
// set of models on a common bone (guess: attachment/hand-off between animated objects; callers 0x00453b90,
// 0x00453c50). Records are 0x1c bytes from +4: {model, ?, bone, time, mode, weight, ?}. Repeated `passes` times
// (the original decrements its argument slot): when out+0x88 (chosen bone name) is 0, tries every bone of record 0's
// model (model+4 -> +4 -> +4 list; record 0's bone = the item's payload, name = payload + 4), resolves the same name
// in every other record's model (inline binary search through the list iterator's compare callback), poses them
// (0x004eccc0(count, skeleton, records; stamp, weight, flags)) and keeps the bone whose relative matrix (out *
// inverse(pose), Matrix_Inverse / Matrix_Multiply) has the smallest squared translation: out+0x88 = name, out+0x44 =
// that matrix. Then resolves every record's bone by out+0x88 (cached per record in out+0x8c[]; reused while
// bone+0xc == model+8), returning 0 at the first record it cannot resolve, poses again and sets out = out+0x44 * pose.
// Returns 1. The best distance is uninitialised in the original until the first candidate is stored (not read before).
uint32_t fn_004ecd40(uint8_t* recs, uint32_t count, uint8_t* out, int32_t passes, uint32_t skel, uint32_t stamp,
                     uint32_t weight, uint32_t flags)
{
    if (!passes)
        return 1;
    float best = 0.0f;
    do
    {
        --passes;
        if (!D(out, 0x88))
        {
            for (uint32_t i = 0;;)
            {
                uint8_t* list = Field<uint8_t*>(Field<uint8_t*>(Field<uint8_t*>(recs, 4), 4), 4);
                const uint32_t item = ListAtN(list, i, Field<uint32_t>(list, 0x1c));
                ++i;
                if (!item)
                    break;
                const uint32_t p = item + Field<uint16_t>(P(item), 2);
                const uint32_t name = p + 4;
                D(recs, 0xc) = Field<uint32_t>(P(p), 0);
                bool resolved = true;
                uint8_t* r = recs + 0x28;
                for (uint32_t k = count - 1; k != 0; --k)
                {
                    uint8_t* l2 = Field<uint8_t*>(Field<uint8_t*>(Field<uint8_t*>(r, static_cast<uint32_t>(-8)), 4), 4);
                    const uint32_t found = IterFind(l2, name);
                    if (!found)
                    {
                        resolved = false;
                        break;
                    }
                    D(r, 0) = Payload(found);
                    r += 0x1c;
                }
                if (!resolved)
                    continue;
                const uint32_t pose = fn_004eccc0(count, B(skel), recs, stamp, weight, flags);
                if (!pose)
                    continue;
                float inv[17];
                float rel[17];
                orig::fn_004e2b40(pose, u(inv));
                orig::fn_004e2910(u(out), u(rel), u(inv));
                const float d = (rel[3] * rel[3] + rel[7] * rel[7]) + rel[11] * rel[11];
                if (D(out, 0x88) && !(best > d))
                    continue;
                best = d;
                D(out, 0x88) = name;
                std::memcpy(out + 0x44, rel, 0x44);
            }
        }
        uint32_t* cached = Field<uint32_t*>(out, 0x8c);
        uint8_t* r = recs + 4;
        for (uint32_t k = count; k != 0;)
        {
            --k;
            uint32_t b = *cached;
            if (!b || Field<uint32_t>(P(b), 0xc) != Field<uint32_t>(Field<uint8_t*>(r, 0), 8))
            {
                uint32_t found = 0;
                if (uint8_t* model = Field<uint8_t*>(r, 0))
                    found = IterFind(Field<uint8_t*>(Field<uint8_t*>(model, 4), 4), D(out, 0x88));
                if (!found)
                    return 0;
                *cached = Payload(found);
            }
            D(r, 8) = *cached;
            r += 0x1c;
            ++cached;
        }
        const uint32_t pose = fn_004eccc0(count, B(skel), recs, stamp, weight, flags);
        if (pose)
            orig::fn_004e2910(u(out + 0x44), u(out), pose);
    } while (passes != 0);
    return 1;
}

// 0x004ed0d0 (edi = bone count; stack: heap, flags; caller pops; executed): allocates a 0x24-byte pose buffer set
// (0x004e1e90(heap, size, 1, 2)) and, by flags, its arrays: 1 -> +0x10 (4 per bone), 2 -> +0x14 (8), 4 -> +0x18 and
// 8 -> +0x1c (0x48: world matrices), 0x10 -> +0x20 (0x30: transform cache, each initialised to position 0, quaternion
// w = 1.0, flags +0x28 = 0). +4 = heap, +0xc = count. Returns the set (0 for no bones or a failed allocation).
uint32_t fn_004ed0d0(int32_t count, uint32_t heap, uint32_t flags)
{
    if (!count)
        return 0;
    uint8_t* s = B(orig::fn_004e1e90(heap, 0x24, 1, 2));
    if (!s)
        return 0;
    if (flags & 1)
        D(s, 0x10) = orig::fn_004e1e90(heap, static_cast<uint32_t>(count) * 4, 1, 2);
    if (flags & 2)
        D(s, 0x14) = orig::fn_004e1e90(heap, static_cast<uint32_t>(count) * 8, 1, 2);
    if (flags & 4)
        D(s, 0x18) = orig::fn_004e1e90(heap, static_cast<uint32_t>(count) * 0x48, 1, 2);
    if (flags & 8)
        D(s, 0x1c) = orig::fn_004e1e90(heap, static_cast<uint32_t>(count) * 0x48, 1, 2);
    if (flags & 0x10)
    {
        const uint32_t cache = orig::fn_004e1e90(heap, static_cast<uint32_t>(count) * 0x30, 1, 2);
        D(s, 0x20) = cache;
        if (cache && count > 0)
        {
            uint32_t o = 0;
            for (int32_t k = count; k != 0; --k, o += 0x30)
            {
                D(Field<uint8_t*>(s, 0x20) + o, 0x14) = 0;
                D(Field<uint8_t*>(s, 0x20) + o, 0x10) = 0;
                D(Field<uint8_t*>(s, 0x20) + o, 0xc) = 0;
                D(Field<uint8_t*>(s, 0x20) + o, 0x18) = 0x3f800000;
                D(Field<uint8_t*>(s, 0x20) + o, 0x28) = 0;
            }
        }
    }
    D(s, 4) = heap;
    D(s, 0xc) = static_cast<uint32_t>(count);
    return u(s);
}

// 0x004ed1e0 (esi = pose buffer set; plain ret; executed): frees the arrays +0x20, +0x1c, +0x18, +0x14, +0x10 (when
// set) and the set itself with 0x004e1f70(heap = +4, block). Returns the last call's EAX (0 without a set).
uint32_t fn_004ed1e0(uint8_t* s)
{
    if (!s)
        return 0;
    for (uint32_t o = 0x20; o >= 0x10; o -= 4)
    {
        if (D(s, o))
            orig::fn_004e1f70(D(s, 4), D(s, o));
    }
    return orig::fn_004e1f70(D(s, 4), u(s));
}

// 0x004ed260 (eax = pose buffer set; plain ret; executed): next stamp code: ++set+8 (skipping 0) and returns it;
// without a set logs "Error: Can't generate stamp code, no cache\n" (0x0058af8c) and returns 0.
uint32_t fn_004ed260(uint8_t* pose)
{
    if (!pose)
    {
        LogPrintf(0x0058af8cu);
        return 0;
    }
    if (++D(pose, 8) == 0)
        D(pose, 8) = 1;
    return D(pose, 8);
}

// 0x004ed590 (ebx = block; plain ret; executed): frees the +0x14 pointers at +0x18.. (when set) and the block through
// the allocator's free hook (0x005cacc4, cdecl). Returns the last free's EAX (0 without a block).
uint32_t fn_004ed590(uint8_t* blk)
{
    if (!blk)
        return 0;
    typedef uint32_t(__cdecl * FreeFn)(uint32_t);
    const uint32_t n = D(blk, 0x14);
    for (uint32_t k = 0; k < n; ++k)
    {
        const uint32_t p = D(blk, 0x18 + k * 4);
        if (p)
            GameGlobal<FreeFn>(0x005cacc4)(p);
    }
    return GameGlobal<FreeFn>(0x005cacc4)(u(blk));
}

// 0x004ed5d0 (eax = entry index, ecx = instance; stack: record count, skeleton, stamp, weight, flags; caller pops;
// executed): for entry `index` of the instance's definition (instance+8 = set, set+4 = definition; entries of 0x2c
// bytes at +0x84, count +0x80) with a set (+8), definition +0x24, a skeleton and a group (entry+0x1c >= 0; groups of
// 0x18 bytes at +0x94: +0x10 count, +0x14 int16 bone indices): per bone index, fills every record's bone (instance+0xc
// + k*0x1c) from its table (instance+8 + k*0x1c -> [index*4 + 0xc]), poses (0x004eccc0(count, skeleton, instance;
// stamp, weight, flags)) and, unless flags bit 2, when the bone's second matrix (skeleton+0x1c, 0x48 each, stamp at
// +0x44) is stale, stamps it and Matrix_Multiply(eax = definition+0x8c + index * 0x58 + 0xc; it, pose). Returns 0
// after the loop; the early exits return what the original leaves in EAX (index, the entry address, entry+0x1c or
// the group address).
uint32_t fn_004ed5d0(int32_t index, uint8_t* inst, uint32_t count, uint8_t* skel, uint32_t stamp, uint32_t weight,
                     uint32_t flags)
{
    if (!inst)
        return static_cast<uint32_t>(index);
    uint8_t* set = Field<uint8_t*>(inst, 8);
    uint8_t* def = Field<uint8_t*>(set, 4);
    if (index >= Field<int32_t>(def, 0x80))
        return static_cast<uint32_t>(index);
    uint8_t* entry = Field<uint8_t*>(def, 0x84) + index * 0x2c;
    if (!D(set, 8) || !D(def, 0x24) || !skel)
        return u(entry);
    const int32_t group = Field<int32_t>(entry, 0x1c);
    if (group < 0)
        return static_cast<uint32_t>(group);
    uint8_t* g = Field<uint8_t*>(def, 0x94) + group * 0x18;
    const int16_t* bones = Field<int16_t*>(g, 0x14);
    uint32_t n = D(g, 0x10);
    if (!n)
        return u(g);
    do
    {
        if (count)
        {
            uint8_t* r = inst + 0xc;
            for (uint32_t k = count; k != 0; --k)
            {
                D(r, 0) = Field<uint32_t>(Field<uint8_t*>(r, static_cast<uint32_t>(-4)), *bones * 4 + 0xc);
                r += 0x1c;
            }
        }
        const uint32_t pose = fn_004eccc0(count, skel, inst, stamp, weight, flags);
        if (pose && !(flags & 2))
        {
            const uint32_t bone = Field<uint32_t>(set, *bones * 4 + 0xc);
            uint8_t* m = Field<uint8_t*>(skel, 0x1c) + Field<uint16_t>(P(bone), 0xa) * 0x48;
            if (D(m, 0x44) != stamp)
            {
                D(m, 0x44) = stamp;
                orig::fn_004e2910(u(Field<uint8_t*>(def, 0x8c) + *bones * 0x58 + 0xc), u(m), pose);
            }
        }
        ++bones;
    } while (--n != 0);
    return 0;
}

// 0x004ed710 (edi = instance; stack: record count, skeleton, stamp, weight, flags; caller pops; executed): runs
// 0x004ed5d0 for every entry of the instance's definition (instance+8 -> +4, count +0x80, re-read each time).
// Returns the entry count (the caller's EAX without an instance; 0 here).
uint32_t fn_004ed710(uint8_t* inst, uint32_t count, uint32_t skel, uint32_t stamp, uint32_t weight, uint32_t flags)
{
    if (!inst)
        return 0;
    uint8_t* def = Field<uint8_t*>(Field<uint8_t*>(inst, 8), 4);
    int32_t n = Field<int32_t>(def, 0x80);
    if (n <= 0)
        return static_cast<uint32_t>(n);
    int32_t i = 0;
    do
    {
        fn_004ed5d0(i, inst, count, B(skel), stamp, weight, flags);
        n = Field<int32_t>(def, 0x80);
        ++i;
    } while (i < n);
    return static_cast<uint32_t>(n);
}

// 0x004ed760 (eax = instance; stack: record count, skeleton, stamp, weight, flags; caller pops; executed): for each of
// the n = (instance+4)+0x14 variants j, points every record's set (+4 + k*0x1c -> +4) at its object's j-th entry
// (object +0x18 + 4j), last record first, then 0x004ed710. Returns the last 0x004ed710 result (n when n <= 0).
uint32_t fn_004ed760(uint8_t* inst, uint32_t count, uint32_t skel, uint32_t stamp, uint32_t weight, uint32_t flags)
{
    const int32_t n = Field<int32_t>(Field<uint8_t*>(inst, 4), 0x14);
    if (n <= 0)
        return static_cast<uint32_t>(n);
    uint32_t r = 0;
    uint32_t off = 0x18;
    for (int32_t j = n; j != 0; --j, off += 4)
    {
        if (count)
        {
            for (uint32_t k = count; k != 0; --k)
            {
                uint8_t* rec = inst + 4 + (k - 1) * 0x1c;
                D(rec, 4) = Field<uint32_t>(Field<uint8_t*>(rec, 0), off);
            }
        }
        r = fn_004ed710(inst, count, skel, stamp, weight, flags);
    }
    return r;
}

// 0x004ed7d0 (eax = 3x4 matrix, ecx = point, edx = accumulator; stack: weight; caller pops; no return value; not
// executed in recorded runs): acc += (M * point + translation) * weight, row by row (row 0 summed as (m1 p1 + m2 p2) +
// p0 m0, the others in column order). guess: weighted skinning of a position.
void fn_004ed7d0(const float* m, const float* p, float* acc, float w)
{
    acc[0] = ((m[1] * p[1] + m[2] * p[2]) + p[0] * m[0] + m[3]) * w + acc[0];
    acc[1] = ((m[4] * p[0] + m[5] * p[1]) + m[6] * p[2] + m[7]) * w + acc[1];
    acc[2] = ((m[8] * p[0] + m[9] * p[1]) + m[10] * p[2] + m[11]) * w + acc[2];
}

// 0x004ed840 (eax = 3x4 matrix, ecx = vector, edx = accumulator; stack: weight; caller pops; no return value; not
// executed in recorded runs): as 0x004ed7d0 without the translation (guess: weighted skinning of a normal).
void fn_004ed840(const float* m, const float* p, float* acc, float w)
{
    acc[0] = ((m[1] * p[1] + m[2] * p[2]) + p[0] * m[0]) * w + acc[0];
    acc[1] = ((m[4] * p[0] + m[5] * p[1]) + m[6] * p[2]) * w + acc[1];
    acc[2] = ((m[8] * p[0] + m[9] * p[1]) + m[10] * p[2]) * w + acc[2];
}

// ================================================================================================================
// ABI adapters, entry stubs and hooks
// ================================================================================================================

static uint32_t fn_00493d00_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_00493d00(reinterpret_cast<const float*>(eax), B(a0), reinterpret_cast<const float*>(a1),
                       reinterpret_cast<const float*>(a2), reinterpret_cast<const float*>(a3),
                       reinterpret_cast<const float*>(a4));
}
static uint32_t fn_00493f70_Abi(uint32_t esi)
{
    return fn_00493f70(B(esi));
}
static uint32_t fn_00493fe0_Abi(uint32_t esi)
{
    return fn_00493fe0(B(esi));
}
static uint32_t Anm_LoadAnim_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return Anm_LoadAnim(B(ecx), a0, a1);
}
static uint32_t Anm_LoadSkin_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return Anm_LoadSkin(B(ecx), a0, a1);
}
static uint32_t fn_004eb280_Abi(uint32_t /*ecx*/, uint32_t a0)
{
    return fn_004eb280(a0);
}
static uint32_t fn_004eb310_Abi(uint32_t esi, uint32_t edi, uint32_t a0)
{
    return fn_004eb310(B(esi), B(edi), a0);
}
static uint32_t fn_004eb360_Abi(uint32_t eax, uint32_t /*ecx*/, uint32_t a0, uint32_t a1)
{
    return fn_004eb360(B(eax), B(a0), a1);
}
static uint32_t fn_004eb3f0_Abi(uint32_t a0, uint32_t a1)
{
    return fn_004eb3f0(a0, B(a1));
}
static uint32_t AnimTrack_FireEvents_Abi(uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return AnimTrack_FireEvents(B(esi), static_cast<int32_t>(a0), static_cast<int32_t>(a1), a2, a3);
}
static uint32_t fn_004eb5d0_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0)
{
    return fn_004eb5d0(B(eax), ecx, edx, a0);
}
static uint32_t fn_004eb670_Abi(uint32_t /*ecx*/, uint32_t a0)
{
    return fn_004eb670(a0);
}
static float fn_004eb710_Abi(uint32_t eax, uint32_t a0)
{
    return fn_004eb710(B(eax), a0);
}
static uint32_t AnimTrack_FindKeys_Abi(uint32_t eax, uint32_t a0)
{
    return AnimTrack_FindKeys(B(eax), B(a0));
}
static uint32_t fn_004ebb50_Abi(uint32_t a0, uint32_t a1)
{
    return fn_004ebb50(reinterpret_cast<float*>(a0), B(a1));
}
static uint32_t fn_004ebbe0_Abi(uint32_t a0, uint32_t a1)
{
    return fn_004ebbe0(a0, B(a1));
}
static uint32_t fn_004ebc30_Abi(uint32_t a0, uint32_t a1)
{
    return fn_004ebc30(reinterpret_cast<float*>(a0), B(a1));
}
static uint32_t fn_004ebc50_Abi(uint32_t a0, uint32_t a1)
{
    return fn_004ebc50(reinterpret_cast<float*>(a0), B(a1));
}
static uint32_t fn_004ebd20_Abi(uint32_t a0, uint32_t a1)
{
    return fn_004ebd20(a0, B(a1));
}
static uint32_t fn_004ebd70_Abi(uint32_t a0, uint32_t a1)
{
    return fn_004ebd70(reinterpret_cast<float*>(a0), B(a1));
}
static uint32_t fn_004ebdc0_Abi(uint32_t a0, uint32_t a1)
{
    return fn_004ebdc0(a0, B(a1));
}
static uint32_t fn_004ebe00_Abi(uint32_t a0, uint32_t a1)
{
    return fn_004ebe00(a0, B(a1));
}
static uint32_t fn_004ebe30_Abi(uint32_t a0, uint32_t a1)
{
    return fn_004ebe30(a0, B(a1));
}
static uint32_t fn_004ebe60_Abi(uint32_t a0, uint32_t a1)
{
    return fn_004ebe60(a0, B(a1));
}
static uint32_t fn_004ebed0_Abi(uint32_t a0, uint32_t a1)
{
    return fn_004ebed0(a0, B(a1));
}
static uint32_t fn_004ebf40_Abi(uint32_t a0, uint32_t a1)
{
    return fn_004ebf40(a0, B(a1));
}
static uint32_t fn_004ebf90_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return fn_004ebf90(B(eax), B(a0), a1);
}
static uint32_t fn_004ec2f0_Abi(uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                uint32_t a5)
{
    return fn_004ec2f0(edx, B(a0), B(a1), B(a2), a3, a4, a5);
}
static uint32_t fn_004ec410_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    return fn_004ec410(B(a0), a1, B(a2), a3, a4, a5);
}
static uint32_t fn_004ec530_Abi(uint32_t eax, uint32_t /*ecx*/, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2,
                                uint32_t a3, uint32_t a4)
{
    return fn_004ec530(eax, reinterpret_cast<uint8_t**>(edi), B(a0), B(a1), a2, a3, a4);
}
static uint32_t fn_004ec600_Abi(uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_004ec600(B(esi), B(a0), a1, a2, a3, a4);
}
static uint32_t fn_004ec800_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    return fn_004ec800(B(a0), a1, B(a2), a3, a4, a5);
}
static uint32_t fn_004eccc0_Abi(uint32_t eax, uint32_t ecx, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004eccc0(eax, B(ecx), B(edi), a0, a1, a2);
}
static uint32_t fn_004ecd40_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                                uint32_t a6, uint32_t a7)
{
    return fn_004ecd40(B(a0), a1, B(a2), static_cast<int32_t>(a3), a4, a5, a6, a7);
}
static uint32_t fn_004ed0d0_Abi(uint32_t edi, uint32_t a0, uint32_t a1)
{
    return fn_004ed0d0(static_cast<int32_t>(edi), a0, a1);
}
static uint32_t fn_004ed1e0_Abi(uint32_t esi)
{
    return fn_004ed1e0(B(esi));
}
static uint32_t fn_004ed260_Abi(uint32_t eax)
{
    return fn_004ed260(B(eax));
}
static uint32_t fn_004ed590_Abi(uint32_t ebx)
{
    return fn_004ed590(B(ebx));
}
static uint32_t fn_004ed5d0_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                uint32_t a4)
{
    return fn_004ed5d0(static_cast<int32_t>(eax), B(ecx), a0, B(a1), a2, a3, a4);
}
static uint32_t fn_004ed710_Abi(uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_004ed710(B(edi), a0, a1, a2, a3, a4);
}
static uint32_t fn_004ed760_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_004ed760(B(eax), a0, a1, a2, a3, a4);
}
static void fn_004ed7d0_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0)
{
    fn_004ed7d0(reinterpret_cast<const float*>(eax), reinterpret_cast<const float*>(ecx),
                reinterpret_cast<float*>(edx), bf(a0));
}
static void fn_004ed840_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0)
{
    fn_004ed840(reinterpret_cast<const float*>(eax), reinterpret_cast<const float*>(ecx),
                reinterpret_cast<float*>(edx), bf(a0));
}

ABI_ENTRY_00493d00(fn_00493d00_Entry, fn_00493d00_Abi)
ABI_ENTRY_00493f70(fn_00493f70_Entry, fn_00493f70_Abi)
ABI_ENTRY_00493fe0(fn_00493fe0_Entry, fn_00493fe0_Abi)
ABI_ENTRY_004e84e0(Anm_LoadAnim_Entry, Anm_LoadAnim_Abi)
ABI_ENTRY_004eb140(Anm_LoadSkin_Entry, Anm_LoadSkin_Abi)
ABI_ENTRY_004eb280(fn_004eb280_Entry, fn_004eb280_Abi)
ABI_ENTRY_004eb310(fn_004eb310_Entry, fn_004eb310_Abi)
ABI_ENTRY_004eb360(fn_004eb360_Entry, fn_004eb360_Abi)
ABI_ENTRY_004eb3f0(fn_004eb3f0_Entry, fn_004eb3f0_Abi)
ABI_ENTRY_004eb480(AnimTrack_FireEvents_Entry, AnimTrack_FireEvents_Abi)
ABI_ENTRY_004eb5d0(fn_004eb5d0_Entry, fn_004eb5d0_Abi)
ABI_ENTRY_004eb670(fn_004eb670_Entry, fn_004eb670_Abi)
ABI_ENTRY_004eb710(fn_004eb710_Entry, fn_004eb710_Abi)
ABI_ENTRY_004eb780(AnimTrack_FindKeys_Entry, AnimTrack_FindKeys_Abi)
ABI_ENTRY_004ebb50(fn_004ebb50_Entry, fn_004ebb50_Abi)
ABI_ENTRY_004ebbe0(fn_004ebbe0_Entry, fn_004ebbe0_Abi)
ABI_ENTRY_004ebc30(fn_004ebc30_Entry, fn_004ebc30_Abi)
ABI_ENTRY_004ebc50(fn_004ebc50_Entry, fn_004ebc50_Abi)
ABI_ENTRY_004ebd20(fn_004ebd20_Entry, fn_004ebd20_Abi)
ABI_ENTRY_004ebd70(fn_004ebd70_Entry, fn_004ebd70_Abi)
ABI_ENTRY_004ebdc0(fn_004ebdc0_Entry, fn_004ebdc0_Abi)
ABI_ENTRY_004ebe00(fn_004ebe00_Entry, fn_004ebe00_Abi)
ABI_ENTRY_004ebe30(fn_004ebe30_Entry, fn_004ebe30_Abi)
ABI_ENTRY_004ebe60(fn_004ebe60_Entry, fn_004ebe60_Abi)
ABI_ENTRY_004ebed0(fn_004ebed0_Entry, fn_004ebed0_Abi)
ABI_ENTRY_004ebf40(fn_004ebf40_Entry, fn_004ebf40_Abi)
ABI_ENTRY_004ebf90(fn_004ebf90_Entry, fn_004ebf90_Abi)
ABI_ENTRY_004ec2f0(fn_004ec2f0_Entry, fn_004ec2f0_Abi)
ABI_ENTRY_004ec410(fn_004ec410_Entry, fn_004ec410_Abi)
ABI_ENTRY_004ec530(fn_004ec530_Entry, fn_004ec530_Abi)
ABI_ENTRY_004ec600(fn_004ec600_Entry, fn_004ec600_Abi)
ABI_ENTRY_004ec800(fn_004ec800_Entry, fn_004ec800_Abi)
ABI_ENTRY_004eccc0(fn_004eccc0_Entry, fn_004eccc0_Abi)
ABI_ENTRY_004ecd40(fn_004ecd40_Entry, fn_004ecd40_Abi)
ABI_ENTRY_004ed0d0(fn_004ed0d0_Entry, fn_004ed0d0_Abi)
ABI_ENTRY_004ed1e0(fn_004ed1e0_Entry, fn_004ed1e0_Abi)
ABI_ENTRY_004ed260(fn_004ed260_Entry, fn_004ed260_Abi)
ABI_ENTRY_004ed590(fn_004ed590_Entry, fn_004ed590_Abi)
ABI_ENTRY_004ed5d0(fn_004ed5d0_Entry, fn_004ed5d0_Abi)
ABI_ENTRY_004ed710(fn_004ed710_Entry, fn_004ed710_Abi)
ABI_ENTRY_004ed760(fn_004ed760_Entry, fn_004ed760_Abi)
ABI_ENTRY_004ed7d0(fn_004ed7d0_Entry, fn_004ed7d0_Abi)
ABI_ENTRY_004ed840(fn_004ed840_Entry, fn_004ed840_Abi)

HOOK(0x00493d00, "fn_00493d00", fn_00493d00_Entry, nullptr);
HOOK(0x00493f70, "fn_00493f70", fn_00493f70_Entry, nullptr);
HOOK(0x00493fe0, "fn_00493fe0", fn_00493fe0_Entry, nullptr);
HOOK(0x004e84e0, "Anm_LoadAnim", Anm_LoadAnim_Entry, nullptr);
HOOK(0x004eb140, "Anm_LoadSkin", Anm_LoadSkin_Entry, nullptr);
HOOK(0x004eb280, "fn_004eb280", fn_004eb280_Entry, nullptr);
HOOK(0x004eb310, "fn_004eb310", fn_004eb310_Entry, nullptr);
HOOK(0x004eb360, "fn_004eb360", fn_004eb360_Entry, nullptr);
HOOK(0x004eb3f0, "fn_004eb3f0", fn_004eb3f0_Entry, nullptr);
HOOK(0x004eb480, "AnimTrack_FireEvents", AnimTrack_FireEvents_Entry, nullptr);
HOOK(0x004eb5d0, "fn_004eb5d0", fn_004eb5d0_Entry, nullptr);
HOOK(0x004eb670, "fn_004eb670", fn_004eb670_Entry, nullptr);
HOOK(0x004eb710, "fn_004eb710", fn_004eb710_Entry, nullptr);
HOOK(0x004eb780, "AnimTrack_FindKeys", AnimTrack_FindKeys_Entry, nullptr);
HOOK(0x004ebb50, "fn_004ebb50", fn_004ebb50_Entry, nullptr);
HOOK(0x004ebbe0, "fn_004ebbe0", fn_004ebbe0_Entry, nullptr);
HOOK(0x004ebc30, "fn_004ebc30", fn_004ebc30_Entry, nullptr);
HOOK(0x004ebc50, "fn_004ebc50", fn_004ebc50_Entry, nullptr);
HOOK(0x004ebd20, "fn_004ebd20", fn_004ebd20_Entry, nullptr);
HOOK(0x004ebd70, "fn_004ebd70", fn_004ebd70_Entry, nullptr);
HOOK(0x004ebdc0, "fn_004ebdc0", fn_004ebdc0_Entry, nullptr);
HOOK(0x004ebe00, "fn_004ebe00", fn_004ebe00_Entry, nullptr);
HOOK(0x004ebe30, "fn_004ebe30", fn_004ebe30_Entry, nullptr);
HOOK(0x004ebe60, "fn_004ebe60", fn_004ebe60_Entry, nullptr);
HOOK(0x004ebed0, "fn_004ebed0", fn_004ebed0_Entry, nullptr);
HOOK(0x004ebf40, "fn_004ebf40", fn_004ebf40_Entry, nullptr);
HOOK(0x004ebf90, "fn_004ebf90", fn_004ebf90_Entry, nullptr);
HOOK(0x004ec2f0, "fn_004ec2f0", fn_004ec2f0_Entry, nullptr);
HOOK(0x004ec410, "fn_004ec410", fn_004ec410_Entry, nullptr);
HOOK(0x004ec530, "fn_004ec530", fn_004ec530_Entry, nullptr);
HOOK(0x004ec600, "fn_004ec600", fn_004ec600_Entry, nullptr);
HOOK(0x004ec800, "fn_004ec800", fn_004ec800_Entry, nullptr);
HOOK(0x004eccc0, "fn_004eccc0", fn_004eccc0_Entry, nullptr);
HOOK(0x004ecd40, "fn_004ecd40", fn_004ecd40_Entry, nullptr);
HOOK(0x004ed0d0, "fn_004ed0d0", fn_004ed0d0_Entry, nullptr);
HOOK(0x004ed1e0, "fn_004ed1e0", fn_004ed1e0_Entry, nullptr);
HOOK(0x004ed260, "fn_004ed260", fn_004ed260_Entry, nullptr);
HOOK(0x004ed590, "fn_004ed590", fn_004ed590_Entry, nullptr);
HOOK(0x004ed5d0, "fn_004ed5d0", fn_004ed5d0_Entry, nullptr);
HOOK(0x004ed710, "fn_004ed710", fn_004ed710_Entry, nullptr);
HOOK(0x004ed760, "fn_004ed760", fn_004ed760_Entry, nullptr);
HOOK(0x004ed7d0, "fn_004ed7d0", fn_004ed7d0_Entry, nullptr);
HOOK(0x004ed840, "fn_004ed840", fn_004ed840_Entry, nullptr);
