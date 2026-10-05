// Recovered: game-side rendering batch r3 (0x00423360-0x0047dbd0): fx update helpers, the fx draw (particles),
// UI text/mesh/primitive drawing, the particle pool and emitters, and the post-process effect system.
// Static recovery 2026-10-05, compile-checked only (the game was not run); the oracle (tools/compare.py) is to do.
// Conventions: Game+0x38 renderer, Game+0x40 render-state setter (virtual +0x14 set state block, +0xc, +0x10),
// Game+0x44 vertex batch drawer (virtual +0x14 begin primitive -> builder, builder virtual +4 (n) -> n vertices,
// +0x18 begin batch, +0x1c end). Screen vertices are 6 dwords: x, y, z, colour, u, v.
#include <cmath>
#include <cstdint>
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
uint32_t f2u(float f)
{
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}
float u2f(uint32_t v)
{
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
template <class T = void>
T* P(uint32_t v)
{
    return reinterpret_cast<T*>(v);
}

// Game constants (read from the image): 0.0, 1.0, 0.5, 255.0, 1/640, 1/480.
float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
constexpr uint32_t kZero = 0x0056c05c, kOne = 0x005bcb2c, kHalf = 0x005bcb4c, k255 = 0x005bcb34;
constexpr uint32_t kInv640 = 0x005bcbf8, kInv480 = 0x005bcbf4;

void* Renderer()
{
    return Field<void*>(g_game, 0x38);
}
void* States()
{
    return Field<void*>(g_game, 0x40);
}
void* Batch()
{
    return Field<void*>(g_game, 0x44);
}

// _ftol (0x0056a7c8): truncating float-to-int, low 32 bits of the 64-bit result.
int32_t Ftol(float f)
{
    return static_cast<int32_t>(static_cast<int64_t>(f));
}
// The x87 transcendental instructions the original uses (not the CRT's sinf/cosf).
float X87Sin(float x)
{
    float r;
    __asm {
        fld x
        fsin
        fstp r
    }
    return r;
}
float X87Cos(float x)
{
    float r;
    __asm {
        fld x
        fcos
        fstp r
    }
    return r;
}
// The game's _CIpow (0x00566790): st1 = x, st0 = y -> x^y.
float GamePow(float x, float y)
{
    float r;
    __asm {
        fld x
        fld y
        mov eax, 0x00566790
        call eax
        fstp r
    }
    return r;
}
int GameRand()
{
    return GameFunction<int(__cdecl*)()>(0x005391cd)();
}

// Colour darkening used by the disabled-widget path (+0x474 bit 0x40): r, g, b >> 2, alpha kept.
uint32_t Darken(uint32_t c)
{
    return (c & 0xff000000u) | (((c >> 16) & 0xff) >> 2) << 16 | (((c >> 8) & 0xff) >> 2) << 8 | ((c & 0xff) >> 2);
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// fx system
// ---------------------------------------------------------------------------------------------------------------

// 0x00423360 (ecx = fx system; stack: fx, callee pops 4): one fx update step. Sets fx +0x14 bit 0x200 (busy) and
// dispatches on the state fx +0x10 (jump table 0x00423418): 0 run, 1 wait (fx +8 -= frame dt [0x00604a90]; when
// it reaches <= 0 the state becomes 0 and it runs), 3/4 idle (clear 0x200), 2 and > 4 nothing. Run = the fx script
// interpreter 0x00420770 (edx = the flags just written; stack: system, fx); then 0x200 is cleared. A zero result
// subtracts fx +0xc from the timer fx +8 and clears +0xc; otherwise flag 0x10 without 0x100 sets state 3, else
// 0x0041d030(system, &fx) (guess: removes the finished fx). Returns EAX as the original where cheap; the wait path
// that keeps waiting returned the FPU status mixed into EAX (here: the state).
uint32_t Fx_Step(void* system, void* fx)
{
    const float dt = GameGlobal<float>(0x00604a90);
    const uint32_t flags = Field<uint32_t>(fx, 0x14) | 0x200;
    Field<uint32_t>(fx, 0x14) = flags;
    const uint32_t state = Field<uint32_t>(fx, 0x10);
    switch (state)
    {
    case 1:
    {
        const float t = Field<float>(fx, 8) - dt;
        Field<float>(fx, 8) = t;
        if (!(t <= K(kZero)))
        {
            Field<uint32_t>(fx, 0x14) &= ~0x200u;
            return state;
        }
        Field<uint32_t>(fx, 0x10) = 0;
    }
        [[fallthrough]];
    case 0:
    {
        const uint32_t ran = orig::fn_00420770(flags, u(system), u(fx)) & 0xff;
        const uint32_t now = Field<uint32_t>(fx, 0x14) & ~0x200u;
        Field<uint32_t>(fx, 0x14) = now;
        if (ran == 0)
        {
            Field<float>(fx, 8) = Field<float>(fx, 8) - Field<float>(fx, 0xc);
            Field<uint32_t>(fx, 0xc) = 0;
            return 0;
        }
        if ((now & 0x10) && !(now & 0x100))
        {
            Field<uint32_t>(fx, 0x10) = 3;
            return now;
        }
        void* slot = fx;  // the original passes the address of its own stack argument
        return orig::fn_0041d030(u(system), u(&slot));
    }
    case 3:
    case 4:
        Field<uint32_t>(fx, 0x14) &= ~0x200u;
        return state;
    default:
        return state;
    }
}

// 0x00423430 (esi = &tail, edi = value; stack: unused, &head; callee pops 8): pushes a node {next = 0, value}
// allocated from the pool 0x0063a210 (0x004e07f0, 0x12, 8, 2, "fx stack" 0x005b7e70) onto the singly linked list
// (tail, head). Does nothing for a null value (EAX was left as on entry: 0 here).
uint32_t FxStack_Push(uint32_t* tail, uint32_t value, uint32_t* head)
{
    if (!value)
        return 0;
    uint32_t* node = P<uint32_t>(orig::fn_004e07f0(0x0063a210u, 0x12, 8, 2, 0x005b7e70u));
    node[0] = 0;
    node[1] = value;
    if (*tail == 0)
    {
        *tail = u(node);
        *head = u(node);
        return u(node);
    }
    *P<uint32_t>(*tail) = u(node);
    *tail = u(node);
    return u(node);
}

// 0x00423470 (ebx = &head, edi = &tail; stack: unused, callee pops 4): pops the tail node of that list and frees it
// to the pool 0x0063a210 (0x004e08d0); the predecessor becomes the tail (both cleared when it was the only node).
uint32_t FxStack_Pop(uint32_t* head, uint32_t* tail)
{
    uint32_t last = *tail;
    uint32_t* node = P<uint32_t>(*head);
    if (last == u(node))
    {
        const uint32_t r = orig::fn_004e08d0(0x0063a210u, last);
        *tail = 0;
        *head = 0;
        return r;
    }
    uint32_t next = node[0];
    if (next != last)
    {
        last = *tail;
        do
        {
            node = P<uint32_t>(next);
            next = node[0];
        } while (next != last);
    }
    node[0] = 0;
    const uint32_t r = orig::fn_004e08d0(0x0063a210u, *tail);
    *tail = u(node);
    return r;
}

// 0x004237b0 (stdcall, ret 4: fx system; caller 0x00402e80, the game tick): steps every fx of the list +4 with
// Fx_Step unless Game+0x2684 is set (guess: paused; read once). List iterator 0x004e7340 / 0x004e73c0; the
// iterator's node (second dword) loses a reference (+0x18) at the end and is the return value.
uint32_t Fx_UpdateAll(void* system)
{
    const bool paused = Field<uint32_t>(g_game, 0x2684) != 0;
    uint32_t iterator[2] = {0, 0};
    for (uint32_t obj = orig::fn_004e7340(Field<uint32_t>(system, 4), u(iterator)); obj;
         obj = orig::fn_004e73c0(u(iterator)))
    {
        if (!paused)
            Fx_Step(system, P(obj));
    }
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return iterator[1];
}

uint32_t Particles_Draw(const float* camera, void* system, float dt, uint32_t stateFlags);

// 0x00423820 (edi = fx manager Game+0xa90; stack: dt, callee pops 4; caller: the scene pass, flag 0x20): the
// camera matrix (0x0040fa50, eax = 0x44-byte local, stack: Game+0xa68) and three particle draws (0x0046d9f0):
// pool +0x1c with state flags 0x100, pools +0x20 and +0x24 with 0x310. dt is forced to 0 while Game+0x2684 is set.
uint32_t Fx_DrawParticles(void* manager, float dt)
{
    if (Field<uint32_t>(g_game, 0x2684) != 0)
        dt = 0.0f;
    alignas(8) uint32_t camera[17];
    orig::fn_0040fa50(u(camera), Field<uint32_t>(g_game, 0xa68));
    const float* cam = reinterpret_cast<const float*>(camera);
    Particles_Draw(cam, Field<void*>(manager, 0x1c), dt, 0x100);
    Particles_Draw(cam, Field<void*>(manager, 0x20), dt, 0x310);
    return Particles_Draw(cam, Field<void*>(manager, 0x24), dt, 0x310);
}

// 0x0042eb60 (ecx = character; stack: value, callee pops 4): 0x00453580 (ecx = character; value), 0x004a0da0
// (esi = character +0x61c; value) and, when character +0xb08 is set, its virtual +0x100 (value). Classified
// render-fx ("set alpha", docs/subsystems/decals.md); guess.
uint32_t Character_SetFade(void* character, uint32_t value)
{
    orig::fn_00453580(u(character), value);
    uint32_t r = orig::fn_004a0da0(u(character) + 0x61c, value);
    if (void* p = Field<void*>(character, 0xb08))
        r = VCall<uint32_t>(p, 0x100 / 4, value);
    return r;
}

// ---------------------------------------------------------------------------------------------------------------
// UI drawing
// ---------------------------------------------------------------------------------------------------------------

// 0x00464750 (ecx = UI object, ebx = text (null: object +0x4c0), edi = unused (saved only); stack: ui, x, y,
// scale, callee pops 16; docs/subsystems/ui.md): draws a UI text with 0x00495e80 (eax = colour, ecx = Game+0xac0;
// stack: Game+0xac0, text, size, x + obj +0x4e8, y + obj +0x4ec, x-align, y-align, flags). Colour: 0xff404040 when
// +0x474 bit 0x40 (disabled), 0xff7f0000 for bit 0x800, white for bit 0x80, else +0x4e4. Alignment per axis: 0
// when the offset is > 0, 1.0 when < 0, else +0x4f0 / +0x4f4. Flags: 2 for +0x474 bit 0x200 (shadow), | 8 for bit
// 0x8000. Size = ((ui +0x94 - 1) * obj +0x4e0 + 1) * obj +0x4c8 * scale.
uint32_t UiText_Draw(void* obj, uint32_t text, void* ui, float x, float y, float scale)
{
    const uint32_t flags = Field<uint32_t>(obj, 0x474);
    uint32_t colour;
    if (flags & 0x40)
        colour = 0xff404040u;
    else if (flags & 0x800)
        colour = 0xff7f0000u;
    else if (flags & 0x80)
        colour = 0xffffffffu;
    else
        colour = Field<uint32_t>(obj, 0x4e4);
    uint32_t alignX, alignY;
    if (x > K(kZero))
        alignX = 0;
    else if (x < K(kZero))
        alignX = 0x3f800000u;
    else
        alignX = Field<uint32_t>(obj, 0x4f0);
    if (y > K(kZero))
        alignY = 0;
    else if (y < K(kZero))
        alignY = 0x3f800000u;
    else
        alignY = Field<uint32_t>(obj, 0x4f4);
    uint32_t drawFlags = 0;
    if (flags & 0x200)
        drawFlags = 2;
    if (flags & 0x8000)
        drawFlags |= 8;
    const float size = ((Field<float>(ui, 0x94) - K(kOne)) * Field<float>(obj, 0x4e0) + K(kOne)) *
                       Field<float>(obj, 0x4c8);
    const uint32_t str = text ? text : Field<uint32_t>(obj, 0x4c0);
    const float py = y + Field<float>(obj, 0x4ec);
    const float px = x + Field<float>(obj, 0x4e8);
    const float s = size * scale;
    const uint32_t font = Field<uint32_t>(g_game, 0xac0);
    return orig::fn_00495e80(colour, font, font, str, f2u(s), f2u(px), f2u(py), alignX, alignY, drawFlags);
}

// 0x00464e20 (ecx = UI object; virtual, no direct callers; docs/subsystems/ui.md): draws a UI mesh unless the
// object's virtual +0x150 has bit 2. Layout (+0x474 bit 0x2000: virtual +0x144 first), 0x004543f0 (eax 0, ecx obj,
// edx 0; 0, 1, 0), the model (0x00408800 eax 4 -> 0x00453120 eax obj, edx obj +0x374), the object matrix (virtual
// +0x70). World = A * B or B * A (+0x474 bit 8) where A = rotation part of the object matrix (translation column
// cleared, flag 0x10 cleared) times an X aspect scale ((renderer +0x88 * 4/3) / (viewport w / h), 1 with +0x474
// bit 0x10) and the object matrix with an identity 3x3, and B = scale(+0x37c / 640, +0x380 / 480, +0x384 / 480).
// Then every mesh part (model +0x18[0 .. +0x14], part +4: +0x84[0 .. +0x80] stride 0x2c) is drawn by 0x0048a290
// (ebx = batch; Game+0xa78, part, sub-mesh, obj +0x410, obj, -1) inside the batch drawer's begin/end.
uint32_t UiMesh_Draw(void* obj)
{
    uint32_t r = VCall<uint32_t>(obj, 0x150 / 4);
    if (r & 2)
        return r;
    if (Field<uint32_t>(obj, 0x474) & 0x2000)
        VCall<void>(obj, 0x144 / 4);
    orig::fn_004543f0(0, u(obj), 0, 0, 1, 0);
    const uint32_t* pv = P<uint32_t>(orig::fn_00408800(4, u(obj)));
    void* model = P(orig::fn_00453120(u(obj), Field<uint32_t>(obj, 0x374), *pv));
    alignas(8) uint32_t m0[17], a[17], b[17], s[17], c[17], d[17];
    VCall<void>(obj, 0x70 / 4, static_cast<void*>(m0));
    std::memcpy(a, m0, sizeof(a));
    std::memcpy(b, m0, sizeof(b));
    b[16] &= 0x10;
    a[16] &= ~0x10u;
    a[11] = 0, a[7] = 0, a[3] = 0;
    b[10] = 0x3f800000u, b[5] = 0x3f800000u, b[0] = 0x3f800000u;
    b[9] = 0, b[8] = 0, b[6] = 0, b[4] = 0, b[2] = 0, b[1] = 0;
    const float* vp = VCall<const float*>(Renderer(), 0x7c / 4);
    const float aspect = VCall<float>(Renderer(), 0x88 / 4);
    float scale[3];
    scale[1] = 1.0f;
    scale[2] = 1.0f;
    scale[0] = (aspect * K(0x005bcd90)) / (vp[0] / vp[1]);
    if (Field<uint8_t>(obj, 0x474) & 0x10)
        scale[0] = 1.0f;
    orig::fn_004e2d90(u(s), u(scale), 0, 0, 0);
    orig::fn_004e2910(u(a), u(c), u(s));
    orig::fn_004e2910(u(c), u(a), u(b));
    scale[0] = Field<float>(obj, 0x37c) * K(kInv640);
    scale[1] = Field<float>(obj, 0x380) * K(kInv480);
    scale[2] = Field<float>(obj, 0x384) * K(kInv480);
    orig::fn_004e2d90(u(b), u(scale), 0, 0, 0);
    if (Field<uint8_t>(obj, 0x474) & 8)
        orig::fn_004e2910(u(a), u(d), u(b));
    else
        orig::fn_004e2910(u(b), u(d), u(a));
    VCall<void>(Renderer(), 0x54 / 4, static_cast<void*>(d));
    const uint32_t batch = VCall<uint32_t>(Batch(), 0x18 / 4);
    for (int32_t i = 0; i < Field<int32_t>(model, 0x14); ++i)
    {
        const uint32_t entry = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(model) + 0x18)[i];
        void* mesh = Field<void*>(P(entry), 4);
        for (int32_t j = 0; j < Field<int32_t>(mesh, 0x80); ++j)
            orig::fn_0048a290(batch, Field<uint32_t>(g_game, 0xa78), entry, Field<uint32_t>(mesh, 0x84) + j * 0x2c,
                              Field<uint32_t>(obj, 0x410), u(obj), 0xffffffffu);
    }
    return VCall<uint32_t>(Batch(), 0x1c / 4);
}

uint32_t UiText_DrawBackground(void* obj);

// 0x00465360 (ecx = UI text object, edi = passed through to UiText_Draw (unused there)): fades the text in or out
// (+0x4e0 += [0x005bcd2c] up to 1 while +0x474 bit 0x80 is set and bit 0x400 is not, else -= down to 0), draws the
// background box (0x00465430) for +0x474 bit 0x4000 and, when it has a text (+0x4c0), draws it between renderer
// virtual +0x190 (1) and +0x194 (the original tail-jumps to +0x194; same stack, so a plain call). +0x474 bit 0x2000:
// virtual +0x144 (layout) first.
uint32_t UiText_Render(void* obj, uint32_t edi)
{
    (void)edi;
    if (Field<uint32_t>(obj, 0x474) & 0x2000)
        VCall<void>(obj, 0x144 / 4);
    const uint32_t flags = Field<uint32_t>(obj, 0x474);
    float f;
    if (!(flags & 0x400) && (flags & 0x80))
    {
        f = Field<float>(obj, 0x4e0) + K(0x005bcd2c);
        if (!(f < K(kOne)))
            f = K(kOne);
    }
    else
    {
        f = Field<float>(obj, 0x4e0) - K(0x005bcd2c);
        if (!(f > K(kZero)))
            f = K(kZero);
    }
    Field<float>(obj, 0x4e0) = f;
    if (flags & 0x4000)
        UiText_DrawBackground(obj);
    const uint32_t text = Field<uint32_t>(obj, 0x4c0);
    if (!text)
        return 0;
    VCall<void>(Renderer(), 0x190 / 4, 1u);
    UiText_Draw(obj, 0, Field<void*>(g_game, 0xa70), 0.0f, 0.0f, 1.0f);
    return VCall<uint32_t>(Renderer(), 0x194 / 4);
}

uint32_t Ui_DrawRect(void* obj, uint32_t texture, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t fill,
                     uint32_t outline, uint32_t keepColours);

// 0x00465430 (eax = UI object): the text's background box: Ui_DrawRect (no texture) over +0x490, +0x498, +0x494,
// +0x49c with fill 0xc04080c0, outline 0xff4080c0, colours kept as given (1).
uint32_t UiText_DrawBackground(void* obj)
{
    return Ui_DrawRect(obj, 0, Field<uint32_t>(obj, 0x490), Field<uint32_t>(obj, 0x498), Field<uint32_t>(obj, 0x494),
                       Field<uint32_t>(obj, 0x49c), 0xc04080c0u, 0xff4080c0u, 1);
}

uint32_t Ui_DrawLine(void* obj, uint32_t colour, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                     uint32_t keepColour);

// 0x0046b260 (eax = UI object, ecx = y0, edx = x0 (ints, 640x480 units); stack: x1, y1, colour, keep; callee pops
// 16): converts both points with 0x0046b0a0 (eax = obj; ecx, edx = outputs; stack: x, y; int * 1/640 + obj +0x4e8,
// int * 1/480 + obj +0x4ec) and draws a line (0x0046baf0).
uint32_t Ui_DrawLineInt(void* obj, uint32_t y0, uint32_t x0, uint32_t x1, uint32_t y1, uint32_t colour, uint32_t keep)
{
    uint32_t py0 = 0, px0 = 0;
    orig::fn_0046b0a0(u(obj), u(&px0), u(&py0), x0, y0);
    orig::fn_0046b0a0(u(obj), u(&x1), u(&y1), x1, y1);
    return Ui_DrawLine(obj, colour, px0, py0, x1, y1, keep);
}

// 0x0046b2c0 (eax = UI object, esi = texture or 0; stack: x0, y0, x1, y1, fill, outline, keep (byte); callee pops
// 28): a screen rectangle. Disabled objects (+0x474 bit 0x40) darken both colours unless keep. State block {0x103e0
// (0x133ec textured), fill, texture, 0, 0, 0} to Game+0x40 virtual +0x14, then +0xc (0). Textured: renderer
// virtual +0xa4 (0, 0, 0, 2, 0) and +0x134 (texture, &u, &v) for the UV extent (default 1, 1). Fill != 0: a
// 4-vertex strip (begin (0, 2, 1, 0)); outline != 0: a 5-vertex line strip (begin (0, 1, 1, 0)), the state block's
// flags masked with ~0x300c and its texture cleared first. Textured: virtual +0xa4 (0, 0, 0, 0, 0) at the end.
uint32_t Ui_DrawRect(void* obj, uint32_t texture, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t fill,
                     uint32_t outline, uint32_t keepColours)
{
    uint32_t uvU = 0x3f800000u, uvV = 0x3f800000u;
    if ((Field<uint8_t>(obj, 0x474) & 0x40) && static_cast<uint8_t>(keepColours) == 0)
    {
        fill = Darken(fill);
        outline = Darken(outline);
    }
    uint32_t state[6] = {0x103e0u, fill, 0, 0, 0, 0};
    if (texture)
    {
        state[2] = texture;
        state[0] = 0x133ecu;
        VCall<void>(Renderer(), 0xa4 / 4, 0u, 0u, 0u, 2u, 0u);
        VCall<void>(Renderer(), 0x134 / 4, texture, static_cast<void*>(&uvU), static_cast<void*>(&uvV));
    }
    VCall<void>(States(), 0x14 / 4, static_cast<void*>(state));
    uint32_t r = VCall<uint32_t>(States(), 0xc / 4, 0u);
    if (fill)
    {
        void* builder = VCall<void*>(Batch(), 0x14 / 4, 0u, 2u, 1u, 0u);
        if (builder)
        {
            uint32_t* v = VCall<uint32_t*>(builder, 4 / 4, 4u);
            v[1] = y0, v[6] = x1, v[0] = x0, v[7] = y0, v[2] = 0, v[3] = fill, v[4] = 0, v[5] = 0;
            v[8] = 0, v[9] = fill, v[10] = uvU, v[12] = x0, v[13] = y1, v[11] = 0, v[14] = 0, v[15] = fill;
            v[16] = 0, v[17] = uvV, v[18] = x1, v[19] = y1, v[20] = 0, v[21] = fill, v[22] = uvU, v[23] = uvV;
        }
        r = VCall<uint32_t>(Batch(), 0x1c / 4);
    }
    if (outline)
    {
        state[2] = 0;
        state[0] &= 0xffffcff3u;
        void* builder = VCall<void*>(Batch(), 0x14 / 4, 0u, 1u, 1u, 0u);
        if (builder)
        {
            uint32_t* v = VCall<uint32_t*>(builder, 4 / 4, 5u);
            v[1] = y0, v[6] = x1, v[7] = y0, v[12] = x1, v[13] = y1, v[18] = x0, v[19] = y1, v[0] = x0;
            v[24] = x0, v[25] = y0, v[2] = 0, v[3] = outline, v[8] = 0, v[9] = outline, v[14] = 0;
            v[15] = outline, v[20] = 0, v[21] = outline, v[26] = 0, v[27] = outline;
        }
        r = VCall<uint32_t>(Batch(), 0x1c / 4);
    }
    if (texture)
        r = VCall<uint32_t>(Renderer(), 0xa4 / 4, 0u, 0u, 0u, 0u, 0u);
    return r;
}

// 0x0046b570 (eax = UI object, esi = centre (x, y), edi = inner radii (x, y); stack: outer radii (x, y)*, exponent,
// inner colour, outer colour, texture, keep (byte); callee pops 24): a ring and its filled centre, 24 segments.
// Unit directions (sin a, cos a) for a = 0, step [0x005bccc0], each shaped by |c|^exponent with the sign kept
// (_CIpow 0x00566790), the 25th = the first. Ring: 50-vertex strip, outer vertex (outer colour) then inner (inner
// colour); centre: 26-vertex fan (centre then the inner points, inner colour). UVs map the outer box onto the
// texture extent (renderer +0x134, default 1, 1): u = x * su + ou, v = y * sv + ov with su = W/2 / rx,
// sv = H / ry * [0x005bcbc8], ou = W/2 - su * cx, ov = H/2 - sv * cy. Same state/darken handling as Ui_DrawRect.
uint32_t Ui_DrawRing(void* obj, const float* centre, const float* inner, const float* outer, float exponent,
                     uint32_t innerColour, uint32_t outerColour, uint32_t texture, uint32_t keep)
{
    float extentV = 1.0f, extentU = 1.0f;
    if ((Field<uint8_t>(obj, 0x474) & 0x40) && static_cast<uint8_t>(keep) == 0)
    {
        innerColour = Darken(innerColour);
        outerColour = Darken(outerColour);
    }
    uint32_t state[6] = {0x103e0u, innerColour, 0, 0, 0, 0};
    if (texture)
    {
        state[2] = texture;
        state[0] = 0x133ecu;
        VCall<void>(Renderer(), 0xa4 / 4, 0u, 0u, 0u, 2u, 0u);
        VCall<void>(Renderer(), 0x134 / 4, texture, static_cast<void*>(&extentU), static_cast<void*>(&extentV));
    }
    const float halfU = extentU * K(kHalf);
    const float su = halfU / outer[0];
    const float sv = extentV / outer[1] * K(0x005bcbc8);
    const float ou = halfU - su * centre[0];
    const float ov = extentV * K(kHalf) - sv * centre[1];
    VCall<void>(States(), 0x14 / 4, static_cast<void*>(state));
    VCall<void>(States(), 0xc / 4, 0u);
    float dir[25][2];
    float angle = 0.0f;
    for (uint32_t i = 0; i < 0x18; ++i)
    {
        const float s = X87Sin(angle);
        const float c = X87Cos(angle);
        dir[i][0] = s;
        dir[i][1] = c;
        dir[i][0] = (s >= K(kZero) || s != s) ? GamePow(s, exponent) : -GamePow(-s, exponent);
        dir[i][1] = (c >= K(kZero) || c != c) ? GamePow(c, exponent) : -GamePow(-c, exponent);
        angle = angle + K(0x005bccc0);
    }
    dir[24][0] = dir[0][0];
    dir[24][1] = dir[0][1];
    void* builder = VCall<void*>(Batch(), 0x14 / 4, 0u, 2u, 1u, 0u);
    if (builder)
    {
        float* v = VCall<float*>(builder, 4 / 4, 0x32u);
        for (uint32_t i = 0; i <= 0x18; ++i, v += 12)
        {
            const float x = dir[i][0] * outer[0] + centre[0];
            const float y = dir[i][1] * outer[1] + centre[1];
            v[0] = x;
            reinterpret_cast<uint32_t*>(v)[3] = outerColour;
            v[1] = y;
            reinterpret_cast<uint32_t*>(v)[2] = 0;
            v[4] = x * su + ou;
            v[5] = y * sv + ov;
            const float xi = inner[0] * dir[i][0] + centre[0];
            const float yi = inner[1] * dir[i][1] + centre[1];
            reinterpret_cast<uint32_t*>(v)[8] = 0;
            v[6] = xi;
            reinterpret_cast<uint32_t*>(v)[9] = innerColour;
            v[7] = yi;
            v[10] = xi * su + ou;
            v[11] = yi * sv + ov;
        }
    }
    VCall<void>(Batch(), 0x1c / 4);
    builder = VCall<void*>(Batch(), 0x14 / 4, 0u, 2u, 2u, 0u);
    if (builder)
    {
        float* v = VCall<float*>(builder, 4 / 4, 0x1au);
        const float cx = centre[0];
        v[0] = cx;
        const float cy = centre[1];
        reinterpret_cast<uint32_t*>(v)[2] = 0;
        v[1] = cy;
        reinterpret_cast<uint32_t*>(v)[3] = innerColour;
        v[4] = cx * su + ou;
        v[5] = cy * sv + ov;
        v += 6;
        for (uint32_t i = 0; i <= 0x18; ++i, v += 6)
        {
            const float x = dir[i][0] * inner[0] + centre[0];
            const float y = dir[i][1] * inner[1] + centre[1];
            v[0] = x;
            reinterpret_cast<uint32_t*>(v)[2] = 0;
            reinterpret_cast<uint32_t*>(v)[3] = innerColour;
            v[1] = y;
            v[4] = x * su + ou;
            v[5] = y * sv + ov;
        }
    }
    uint32_t r = VCall<uint32_t>(Batch(), 0x1c / 4);
    if (texture)
        r = VCall<uint32_t>(Renderer(), 0xa4 / 4, 0u, 0u, 0u, 0u, 0u);
    return r;
}

// 0x0046b960 (eax = UI object, ecx = colour 0; stack: x0, y0, x1, y1, x2, y2, colour 1, colour 2, keep (byte);
// callee pops 36): a Gouraud triangle (3-vertex strip, begin (0, 2, 1, 0)), untextured state block {0x103e0,
// colour 0}. Darkened when disabled unless keep.
uint32_t Ui_DrawTriangle(void* obj, uint32_t c0, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t x2,
                         uint32_t y2, uint32_t c1, uint32_t c2, uint32_t keep)
{
    if ((Field<uint8_t>(obj, 0x474) & 0x40) && static_cast<uint8_t>(keep) == 0)
    {
        c0 = Darken(c0);
        c1 = Darken(c1);
        c2 = Darken(c2);
    }
    uint32_t state[6] = {0x103e0u, c0, 0, 0, 0, 0};
    VCall<void>(States(), 0x14 / 4, static_cast<void*>(state));
    VCall<void>(States(), 0xc / 4, 0u);
    void* builder = VCall<void*>(Batch(), 0x14 / 4, 0u, 2u, 1u, 0u);
    if (builder)
    {
        uint32_t* v = VCall<uint32_t*>(builder, 4 / 4, 3u);
        v[0] = x0, v[1] = y0, v[6] = x1, v[7] = y1, v[9] = c1, v[12] = x2, v[2] = 0, v[3] = c0, v[8] = 0;
        v[13] = y2, v[14] = 0, v[15] = c2;
    }
    return VCall<uint32_t>(Batch(), 0x1c / 4);
}

// 0x0046baf0 (eax = UI object, ecx = colour; stack: x0, y0, x1, y1, keep (byte); callee pops 20): a screen line
// (2-vertex line list, begin (0, 1, 1, 0)), untextured; darkened when disabled unless keep.
uint32_t Ui_DrawLine(void* obj, uint32_t colour, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                     uint32_t keepColour)
{
    if ((Field<uint8_t>(obj, 0x474) & 0x40) && static_cast<uint8_t>(keepColour) == 0)
        colour = Darken(colour);
    uint32_t state[6] = {0x103e0u, colour, 0, 0, 0, 0};
    VCall<void>(States(), 0x14 / 4, static_cast<void*>(state));
    VCall<void>(States(), 0xc / 4, 0u);
    void* builder = VCall<void*>(Batch(), 0x14 / 4, 0u, 1u, 1u, 0u);
    if (builder)
    {
        uint32_t* v = VCall<uint32_t*>(builder, 4 / 4, 2u);
        v[0] = x0, v[1] = y0, v[2] = 0, v[3] = colour, v[6] = x1, v[7] = y1, v[8] = 0, v[9] = colour;
    }
    return VCall<uint32_t>(Batch(), 0x1c / 4);
}

// ---------------------------------------------------------------------------------------------------------------
// Particle pools (docs/subsystems/render-pipeline.md step 7). Pool: +4 capacity, +0xc free list sentinel (next,
// prev), +0x14 live list sentinel, +0x1c index buffer, +0x20 texture. Particle (0x6c bytes): +0 next, +4 prev,
// +8 life, +0xc time carried over, +0x10 position, +0x1c previous position, +0x28 velocity, +0x34 stretch,
// +0x38 gravity, +0x3c drag, +0x40 size, +0x44 growth, +0x48 colour, +0x4c alpha / ramp position, +0x50 fade rate,
// +0x54 colour ramp (refcounted, +4), +0x5c..+0x68 UVs (u0, u1, v0, v1). Names of the fields are guesses from use.
// ---------------------------------------------------------------------------------------------------------------

// 0x0046d810 (ebx = pool; stack: create (byte), caller pops): releases the pool's index buffer (its virtual 0) and,
// when create is set, makes a new one (renderer virtual +0x188 (n * 6), n = capacity capped at 256), locks it
// (virtual +0xc (n * 6)) and fills it with two triangles per quad (0 1 2, 0 2 3, base += 4), then unlocks it (virtual
// +0x14; the original tail-jumps there with the same stack). EAX on the early exits was left over (here: the
// create byte, or 0).
uint32_t ParticlePool_ResetIndexBuffer(void* pool, uint32_t create)
{
    if (!pool)
        return 0;
    if (void* ib = Field<void*>(pool, 0x1c))
    {
        VCall<void>(ib, 0);
        Field<uint32_t>(pool, 0x1c) = 0;
    }
    if (static_cast<uint8_t>(create) == 0)
        return create & 0xff;
    int32_t n = Field<int32_t>(pool, 4);
    if (n > 0x100)
        n = 0x100;
    const uint32_t size = static_cast<uint32_t>(n) * 6;
    void* ib = VCall<void*>(Renderer(), 0x188 / 4, size);
    Field<void*>(pool, 0x1c) = ib;
    if (!ib)
        return 0;
    uint16_t* idx = VCall<uint16_t*>(ib, 0xc / 4, size);
    if (n > 0)
    {
        uint32_t base = 0;
        for (int32_t q = n; q != 0; --q, base += 4, idx += 6)
        {
            idx[0] = static_cast<uint16_t>(base);
            idx[1] = static_cast<uint16_t>(base + 1);
            idx[2] = static_cast<uint16_t>(base + 2);
            idx[3] = static_cast<uint16_t>(base);
            idx[4] = static_cast<uint16_t>(base + 2);
            idx[5] = static_cast<uint16_t>(base + 3);
        }
    }
    return VCall<uint32_t>(Field<void*>(pool, 0x1c), 0x14 / 4);
}

uint32_t Particle_Release(void* particle, void* pool);

// 0x0046d8c0 (eax = pool; caller 0x0041c9d0): destroys a pool: every live particle released (0x0046d9a0), the index
// buffer released (0x0046d810, create 0), the pool freed to 0x0063a210 (0x004e08d0).
uint32_t ParticlePool_Destroy(void* pool)
{
    if (!pool)
        return 0;
    const uint32_t sentinel = u(pool) + 0x14;
    while (Field<uint32_t>(pool, 0x14) != sentinel)
        Particle_Release(P(Field<uint32_t>(pool, 0x14)), pool);
    ParticlePool_ResetIndexBuffer(pool, 0);
    return orig::fn_004e08d0(0x0063a210u, u(pool));
}

// 0x0046d900 (eax = texture, ecx = pool; caller 0x0041c7e0): pool +0x20 = texture (the draw passes it in the
// state block).
void ParticlePool_SetTexture(uint32_t texture, void* pool)
{
    Field<uint32_t>(pool, 0x20) = texture;
}

// 0x0046d910 (edx = pool): takes the first free particle (list +0xc) and appends it to the live list (+0x14).
// Returns it, or 0 when none is free.
uint32_t Particle_Alloc(void* pool)
{
    uint32_t* p = Field<uint32_t*>(pool, 0xc);
    if (u(p) == u(pool) + 0xc)
        return 0;
    if (p)
    {
        uint32_t* sentinel = reinterpret_cast<uint32_t*>(u(pool) + 0x14);
        P<uint32_t>(p[0])[1] = p[1];
        P<uint32_t>(p[1])[0] = p[0];
        p[1] = sentinel[1];
        p[0] = u(sentinel);
        P<uint32_t>(sentinel[1])[0] = u(p);
        sentinel[1] = u(p);
    }
    return u(p);
}

// 0x0046d950 (ecx = pool; caller 0x00446460): as Particle_Alloc but the particle (0x1b dwords) is zeroed before it
// is linked.
uint32_t Particle_AllocZeroed(void* pool)
{
    uint32_t* p = Field<uint32_t*>(pool, 0xc);
    if (u(p) == u(pool) + 0xc)
        return 0;
    if (p)
    {
        uint32_t* sentinel = reinterpret_cast<uint32_t*>(u(pool) + 0x14);
        P<uint32_t>(p[0])[1] = p[1];
        P<uint32_t>(p[1])[0] = p[0];
        std::memset(p, 0, 0x1b * 4);
        p[0] = u(sentinel);
        p[1] = sentinel[1];
        P<uint32_t>(sentinel[1])[0] = u(p);
        sentinel[1] = u(p);
    }
    return u(p);
}

// 0x0046d9a0 (esi = particle; stack: pool, caller pops): drops the particle's colour ramp reference (+0x54, count
// at +4, freed to 0x0063a210 at zero) and moves the particle to the pool's free list (+0xc, appended). Returns the
// free list sentinel (EAX was left over for a null particle: 0 here).
uint32_t Particle_Release(void* particle, void* pool)
{
    if (!particle)
        return 0;
    if (void* ramp = Field<void*>(particle, 0x54))
    {
        --Field<int32_t>(ramp, 4);
        void* r = Field<void*>(particle, 0x54);
        if (Field<int32_t>(r, 4) == 0)
            orig::fn_004e08d0(0x0063a210u, u(r));
    }
    uint32_t* p = static_cast<uint32_t*>(particle);
    P<uint32_t>(p[0])[1] = p[1];
    P<uint32_t>(p[1])[0] = p[0];
    uint32_t* sentinel = reinterpret_cast<uint32_t*>(u(pool) + 0xc);
    p[1] = Field<uint32_t>(pool, 0x10);
    p[0] = u(sentinel);
    P<uint32_t>(sentinel[1])[0] = u(p);
    sentinel[1] = u(p);
    return u(sentinel);
}

// 0x0046d9f0 (eax = camera matrix; stack: pool, dt, state flags, caller pops; caller Fx_DrawParticles. abi.csv
// lists 9 stack arguments: the caller pushes 3). Updates and draws every live particle as a camera-facing quad.
// State block {flags = 0x530a0 | flags | (8 if the texture's table entry [0x005caba0 + tex +0x1c * 4] else 4), -1,
// texture, 0, 0, 0} (Game+0x40 virtual +0x14, +0xc (2), +0x10 (1)), identity world (renderer +0x54), then the batch
// (Game+0x44 +0x18) is locked 0x400 vertices at a time (virtual +8 (0, 0x400)) and every 256 quads drawn (virtual
// +0x10 (0), +0x20 (2, 3, 0x600, 0, index buffer)); at the end +0xc ((256 - n) * 4), +0x10 (0), the rest drawn,
// Game+0x44 +0x1c.
// Update (dt != 0): t = dt + carried time (+0xc, cleared); life -= t, at <= 0 the particle is released. size +=
// t * growth, alpha -= t * fade; velocity -= velocity * drag * min(t, 1 / drag), z += t * gravity; the position moves
// by (v + v) * t/2 (the velocity just written is used twice; see the report). Quad: centre = mid-point of previous and
// current position, corners +-right +-up of the camera scaled by size; with stretch > 0 the corners are pushed along
// the motion projected on the view plane (scaled by stretch) and fade by (1 - c * -0.7071) when behind. Colour from
// the ramp (+0x54, entries 16 bytes, linear between floor and floor + 1 of +0x4c: +8 r, +0xc g, +0x10 b, +0x14 a)
// or +0x48 with alpha +0x4c.
uint32_t Particles_Draw(const float* camera, void* pool, float dt, uint32_t stateFlags)
{
    if (!pool)
        return u(camera);
    const uint32_t sentinel = u(pool) + 0x14;
    if (Field<uint32_t>(pool, 0x14) == sentinel)
        return sentinel;
    alignas(8) uint32_t world[17] = {};
    void* texture = Field<void*>(pool, 0x20);
    uint32_t state[6] = {0, 0xffffffffu, u(texture), 0, 0, 0};
    world[0] = world[5] = world[10] = world[15] = 0x3f800000u;
    const uint32_t mapped = GameGlobal<uint32_t>(0x005caba0 + Field<uint32_t>(texture, 0x1c) * 4);
    state[0] = (mapped != 0 ? 8u : 4u) | stateFlags | 0x530a0u;
    VCall<void>(States(), 0x14 / 4, static_cast<void*>(state));
    VCall<void>(States(), 0xc / 4, 2u);
    VCall<void>(States(), 0x10 / 4, 1u);
    VCall<void>(Renderer(), 0x54 / 4, static_cast<void*>(world));
    void* batch = VCall<void*>(Batch(), 0x18 / 4);
    float* v = VCall<float*>(batch, 8 / 4, 0u, 0x400u);
    const float rx = camera[0], ry = camera[1], rz = camera[2];
    const float ux = camera[4], uy = camera[5], uz = camera[6];
    const float fx = camera[8], fy = camera[9], fz = camera[10];
    uint32_t count = 0;
    uint32_t next;
    for (uint32_t node = Field<uint32_t>(pool, 0x14); node != sentinel; node = next)
    {
        uint8_t* p = P<uint8_t>(node);
        next = Field<uint32_t>(p, 0);
        if (dt != K(kZero))
        {
            const float t = dt + Field<float>(p, 0xc);
            Field<uint32_t>(p, 0xc) = 0;
            const float life = Field<float>(p, 8) - t;
            Field<float>(p, 8) = life;
            if (life <= K(kZero))
            {
                Particle_Release(p, pool);
                continue;
            }
            Field<float>(p, 0x40) = t * Field<float>(p, 0x44) + Field<float>(p, 0x40);
            Field<float>(p, 0x4c) = Field<float>(p, 0x4c) - t * Field<float>(p, 0x50);
            const float limit = K(kOne) / Field<float>(p, 0x3c);
            const float k = (t > limit) ? limit : t;
            const float vx = Field<float>(p, 0x28) - Field<float>(p, 0x28) * Field<float>(p, 0x3c) * k;
            Field<uint32_t>(p, 0x1c) = Field<uint32_t>(p, 0x10);
            Field<uint32_t>(p, 0x20) = Field<uint32_t>(p, 0x14);
            Field<uint32_t>(p, 0x24) = Field<uint32_t>(p, 0x18);
            const float vy = Field<float>(p, 0x2c) - Field<float>(p, 0x3c) * Field<float>(p, 0x2c) * k;
            const float vz0 = Field<float>(p, 0x30) - Field<float>(p, 0x30) * Field<float>(p, 0x3c) * k;
            Field<float>(p, 0x28) = vx;
            Field<float>(p, 0x2c) = vy;
            const float vz = vz0 + t * Field<float>(p, 0x38);
            Field<float>(p, 0x30) = vz;
            const float h = t * K(kHalf);
            Field<float>(p, 0x10) = (vx + Field<float>(p, 0x28)) * h + Field<float>(p, 0x10);
            Field<float>(p, 0x14) = (vy + Field<float>(p, 0x2c)) * h + Field<float>(p, 0x14);
            Field<float>(p, 0x18) = (vz + Field<float>(p, 0x30)) * h + Field<float>(p, 0x18);
        }
        const float dx = Field<float>(p, 0x10) - Field<float>(p, 0x1c);
        const float dy = Field<float>(p, 0x14) - Field<float>(p, 0x20);
        const float dz = Field<float>(p, 0x18) - Field<float>(p, 0x24);
        const float mx = dx * K(kHalf) + Field<float>(p, 0x10);
        const float my = dy * K(kHalf) + Field<float>(p, 0x14);
        const float mz = dz * K(kHalf) + Field<float>(p, 0x18);
        const uint32_t ramp = Field<uint32_t>(p, 0x54);
        float alpha = Field<float>(p, 0x4c);
        uint32_t colour;
        if (ramp)
        {
            const int32_t i = Ftol(alpha);
            const float f = Field<float>(p, 0x4c) - static_cast<float>(i);
            const uint8_t* e0 = P<uint8_t>(ramp + static_cast<uint32_t>(i) * 16);
            const uint8_t* e1 = P<uint8_t>(ramp + static_cast<uint32_t>(i + 1) * 16);
            const float g = K(kOne) - f;
            alpha = f * Field<float>(e1, 0x14) + g * Field<float>(e0, 0x14);
            const float r = f * Field<float>(e1, 8) + g * Field<float>(e0, 8);
            colour = static_cast<uint32_t>(Ftol(r * K(k255))) & 0xff;
            colour |= (static_cast<uint32_t>(Ftol(K(k255) * alpha)) & 0xff) << 8;
            const float gr = f * Field<float>(e1, 0xc) + g * Field<float>(e0, 0xc);
            colour <<= 8;
            colour |= static_cast<uint32_t>(Ftol(gr * K(k255))) & 0xff;
            colour <<= 8;
            const float b = f * Field<float>(e1, 0x10) + g * Field<float>(e0, 0x10);
            colour |= static_cast<uint32_t>(Ftol(b * K(k255))) & 0xff;
        }
        else
        {
            colour = (static_cast<uint32_t>(Ftol(K(k255) * alpha)) << 24) | (Field<uint32_t>(p, 0x48) & 0xffffff);
        }
        float c0 = 0.0f, c1 = 0.0f, c2 = 0.0f, c3 = 0.0f;
        const float dot = (dz * fz + dy * fy) + dx * fx;
        float px = dx - dot * fx;
        float py = dy - dot * fy;
        float pz = dz - dot * fz;
        const float size = Field<float>(p, 0x40);
        const float rxs = rx * size, rys = ry * size, rzs = rz * size;
        const float uxs = ux * size, uys = uy * size, uzs = uz * size;
        const float stretch = Field<float>(p, 0x34);
        if (stretch > K(kZero))
        {
            const float inv = K(kOne) / sqrtf((((pz * pz) + py * py) + px * px) * size * size);
            c0 = (((-rzs - uzs) * pz + (-rys - uys) * py) + (-rxs - uxs) * px) * inv;
            c1 = (((uzs - rzs) * pz + (uys - rys) * py) + (uxs - rxs) * px) * inv;
            c2 = (((uzs + rzs) * pz + (uys + rys) * py) + (uxs + rxs) * px) * inv;
            c3 = (((rzs - uzs) * pz + (rys - uys) * py) + (rxs - uxs) * px) * inv;
            px = px * stretch;
            py = py * stretch;
            pz = pz * stretch;
        }
        auto fade = [&](float c) -> uint32_t {
            if (c < K(kZero))
                return (static_cast<uint32_t>(Ftol((K(kOne) - c * K(0x005bcd18)) * alpha * K(k255))) << 24) |
                       (colour & 0xffffff);
            return colour;
        };
        uint32_t* vd = reinterpret_cast<uint32_t*>(v);
        const float mxr = mx - rxs, myr = my - rys, mzr = mz - rzs;
        v[0] = (mxr - uxs) + c0 * px;
        v[1] = (myr - uys) + c0 * py;
        v[2] = (mzr - uzs) + c0 * pz;
        vd[3] = fade(c0);
        vd[4] = Field<uint32_t>(p, 0x5c);
        vd[5] = Field<uint32_t>(p, 0x64);
        v[6] = (c1 * px + mxr) + uxs;
        v[7] = (c1 * py + myr) + uys;
        v[8] = (c1 * pz + mzr) + uzs;
        vd[9] = fade(c1);
        vd[10] = Field<uint32_t>(p, 0x5c);
        vd[11] = Field<uint32_t>(p, 0x68);
        v[12] = ((c2 * px + uxs) + rxs) + mx;
        v[13] = ((c2 * py + uys) + rys) + my;
        v[14] = ((c2 * pz + uzs) + rzs) + mz;
        vd[15] = fade(c2);
        vd[16] = Field<uint32_t>(p, 0x60);
        vd[17] = Field<uint32_t>(p, 0x68);
        v[18] = ((rxs + mx) - uxs) + c3 * px;
        v[19] = ((rys + my) - uys) + c3 * py;
        v[20] = ((rzs + mz) - uzs) + c3 * pz;
        vd[21] = fade(c3);
        vd[22] = Field<uint32_t>(p, 0x60);
        vd[23] = Field<uint32_t>(p, 0x64);
        v += 24;
        if (++count == 0x100)
        {
            VCall<void>(batch, 0x10 / 4, 0u);
            VCall<void>(batch, 0x20 / 4, 2u, 3u, 0x600u, 0u, Field<uint32_t>(pool, 0x1c));
            count = 0;
            v = VCall<float*>(batch, 8 / 4, 0u, 0x400u);
        }
    }
    VCall<void>(batch, 0xc / 4, (0x100 - count) << 2);
    VCall<void>(batch, 0x10 / 4, 0u);
    if (count)
        VCall<void>(batch, 0x20 / 4, 2u, 3u, count * 6, 0u, Field<uint32_t>(pool, 0x1c));
    return VCall<uint32_t>(Batch(), 0x1c / 4);
}

// 0x0046e320 (ecx = unused (0x0046e2c0 only uses it as stack space), esi = axis, edi = out; stack: cone angle,
// minimum speed, maximum speed, caller pops): a random velocity in a cone around the axis. A perpendicular = axis
// x reference, the reference being whichever of [0x005fb9f4] / [0x005fba00] is less parallel; rotation by a random
// angle in [0, cone] about it (quaternion (sin(a/2) * n, cos(a/2)) -> matrix 0x004e3260) times a rotation by a
// random angle in [0, 2 pi) about the axis (0x004e2910); the axis transformed by that (0x004e36a0, edx = 1) and
// scaled by a random speed (0x0046e2c0 = rand-based uniform). EAX was left from rand(): 0 here.
uint32_t Particle_RandomConeVelocity(uint32_t ecx, const float* axis, float* out, uint32_t cone, uint32_t minSpeed,
                                     uint32_t maxSpeed)
{
    const float* b = &GameGlobal<float>(0x005fba00);
    const float* a = &GameGlobal<float>(0x005fb9f4);
    const float db = fabsf((b[0] * axis[0] + b[1] * axis[1]) + b[2] * axis[2]);
    const float da = fabsf((a[0] * axis[0] + a[1] * axis[1]) + a[2] * axis[2]);
    const float* ref = (da > db) ? b : a;
    float vec[3];
    vec[0] = ref[2] * axis[1] - ref[1] * axis[2];
    vec[1] = axis[2] * ref[0] - ref[2] * axis[0];
    vec[2] = ref[1] * axis[0] - axis[1] * ref[0];
    float quat[4];
    alignas(8) uint32_t m1[17], m2[17], m3[17];
    float half = orig::fn_0046e2c0(ecx, 0, cone) * K(kHalf);
    float s = X87Sin(half);
    quat[0] = vec[0] * s;
    quat[1] = vec[1] * s;
    quat[2] = s * vec[2];
    quat[3] = X87Cos(half);
    orig::fn_004e3260(u(m1), u(quat));
    half = orig::fn_0046e2c0(0, 0, 0x40c90fdbu) * K(kHalf);
    vec[0] = axis[0];
    vec[1] = axis[1];
    vec[2] = axis[2];
    s = X87Sin(half);
    quat[0] = vec[0] * s;
    quat[1] = vec[1] * s;
    quat[2] = vec[2] * s;
    quat[3] = X87Cos(half);
    orig::fn_004e3260(u(m2), u(quat));
    orig::fn_004e2910(u(m1), u(m3), u(m2));
    orig::fn_004e36a0(u(axis), u(m3), 1, u(vec));
    const float speed = orig::fn_0046e2c0(0, minSpeed, maxSpeed);
    out[0] = vec[0] * speed;
    out[1] = vec[1] * speed;
    out[2] = vec[2] * speed;
    return 0;
}

// 0x0046e4c0 (ebx = velocity at the start, edi = velocity at the end; stack: pool, box min, box max, &timer,
// interval, dt, callback, user; caller pops; caller 0x0041f470): spawns particles over a frame: timer -= dt and,
// while it is negative, a particle (0x0046d910) at a random point of the box (rand 0x005391cd * (max - min) *
// [0x005bcbc0] + min, per axis), velocity lerped by t (t starts at timer / dt and grows by interval / dt), carried
// time -timer - dt, then callback(pool, particle, user); timer += interval. Returns &timer (or the FPU status left in
// EAX when nothing spawns: &timer here).
uint32_t ParticleEmitter_SpawnInBox(const float* vel0, const float* vel1, void* pool, const float* boxMin,
                                    const float* boxMax, float* timer, float interval, float dt, uint32_t callback,
                                    uint32_t user)
{
    const float inv = K(kOne) / dt;
    const float step = interval * inv;
    float t = inv * *timer;
    const float first = *timer - dt;
    *timer = first;
    if (!(first < K(kZero)))
        return u(timer);
    do
    {
        const float w = K(kOne) - t;
        uint8_t* p = P<uint8_t>(Particle_Alloc(pool));
        if (p)
        {
            const float maxX = boxMax[0], minX = boxMin[0];
            Field<float>(p, 0x10) = static_cast<float>(GameRand()) * (maxX - minX) * K(0x005bcbc0) + minX;
            const float maxY = boxMax[1], minY = boxMin[1];
            Field<float>(p, 0x14) = static_cast<float>(GameRand()) * (maxY - minY) * K(0x005bcbc0) + minY;
            const float maxZ = boxMax[2], minZ = boxMin[2];
            Field<float>(p, 0x18) = static_cast<float>(GameRand()) * (maxZ - minZ) * K(0x005bcbc0) + minZ;
            Field<float>(p, 0x28) = w * vel0[0] + t * vel1[0];
            Field<float>(p, 0x2c) = w * vel0[1] + t * vel1[1];
            Field<float>(p, 0x30) = w * vel0[2] + t * vel1[2];
            Field<float>(p, 0xc) = -*timer - dt;
            reinterpret_cast<void(__cdecl*)(void*, void*, uint32_t)>(callback)(pool, p, user);
        }
        t = t + step;
        *timer = interval + *timer;
    } while (*timer < K(kZero));
    return u(timer);
}

// 0x0046e650 (ebx = position at the end, esi = velocity at the end, edi = velocity at the start; stack: pool,
// position at the start, &timer, interval, dt, callback, user; caller pops; caller 0x0041f4b0): as
// ParticleEmitter_SpawnInBox, but the position is lerped between the start and end positions (a trail).
uint32_t ParticleEmitter_SpawnAlong(const float* pos1, const float* vel1, const float* vel0, void* pool,
                                    const float* pos0, float* timer, float interval, float dt, uint32_t callback,
                                    uint32_t user)
{
    const float inv = K(kOne) / dt;
    const float step = interval * inv;
    float t = inv * *timer;
    const float first = *timer - dt;
    *timer = first;
    if (!(first < K(kZero)))
        return u(timer);
    do
    {
        const float w = K(kOne) - t;
        uint8_t* p = P<uint8_t>(Particle_Alloc(pool));
        if (p)
        {
            Field<float>(p, 0x10) = w * pos0[0] + t * pos1[0];
            Field<float>(p, 0x14) = w * pos0[1] + t * pos1[1];
            Field<float>(p, 0x18) = w * pos0[2] + t * pos1[2];
            Field<float>(p, 0x28) = w * vel0[0] + t * vel1[0];
            Field<float>(p, 0x2c) = w * vel0[1] + t * vel1[1];
            Field<float>(p, 0x30) = w * vel0[2] + t * vel1[2];
            Field<float>(p, 0xc) = -*timer - dt;
            reinterpret_cast<void(__cdecl*)(void*, void*, uint32_t)>(callback)(pool, p, user);
        }
        t = t + step;
        *timer = interval + *timer;
    } while (*timer < K(kZero));
    return u(timer);
}

// ---------------------------------------------------------------------------------------------------------------
// Post-process effects (docs/subsystems/render-pipeline.md step 8). The effect manager (Game+0xa9c, vtable
// 0x0059e52c): +4 live list, +8 free list, 16 nodes of 0x64 bytes from +0xc, camera matrix +0x64c, per-frame
// accumulators +0x690..+0x6ef. Node: +0 next, +4 prev, +8 type, +0xc id, +0x10 flags (1 distance fade, 2 frustum
// edge fade, 4 runs while paused), +0x14 phase (0 in, 1 hold, 2 out), +0x18 phase time, +0x1c/+0x20/+0x24 phase
// durations (defaults [0x00588da8..b0] = 1, -1 (hold forever), 1), +0x28 position, +0x34/+0x38 distance fade scale
// and offset, +0x3c/+0x40 edge fade scale and offset, +0x44.. parameters. Types by the accumulator they feed: 0
// colour (+0x690..+0x69c), 1 blur amount (+0x6a0), 2 blur rings (+0x6a4, 8 floats), 3 screen texture amount (+0x6c4),
// 4 screen texture rings (+0x6c8), 5 fade (colour +0x6e8, alpha +0x6ec). The draw object is Game+0xa98 ("post"):
// saved matrices +0/+0x44/+0x88, viewport +0xcc, target count +0xdc, targets +0x22c.., state blocks (6 dwords) at
// +0x26c, +0x284, +0x29c, +0x2b4, +0x2cc, +0x2e4. Names of the effect types are guesses from their data flow.
// ---------------------------------------------------------------------------------------------------------------
namespace
{
float Clamp01(float v)
{
    if (v > K(kZero))
    {
        if (!(v < K(kOne)))
            return K(kOne);
        return v;
    }
    return K(kZero);
}
uint32_t ScreenColour()
{
    return GameGlobal<uint32_t>(0x0064a2fc);
}
uint32_t Target(void* post, int32_t i)
{
    return Field<uint32_t>(post, 0x22c + i * 4);
}
// The common tail of the screen effect draws: render state virtual +0x154 (4, post +0xe2) and the saved world,
// view, projection (virtuals +0x54, +0x50, +0x4c) and viewport (+0x74) restored; post +0x228 cleared.
uint32_t Post_EndDraw(void* r, void* post)
{
    VCall<void>(r, 0x154 / 4, 4u, static_cast<uint32_t>(Field<uint8_t>(post, 0xe2)));
    VCall<void>(r, 0x54 / 4, post);
    VCall<void>(r, 0x50 / 4, static_cast<void*>(reinterpret_cast<uint8_t*>(post) + 0x44));
    VCall<void>(r, 0x4c / 4, static_cast<void*>(reinterpret_cast<uint8_t*>(post) + 0x88));
    const uint32_t res = VCall<uint32_t>(r, 0x74 / 4, static_cast<void*>(reinterpret_cast<uint8_t*>(post) + 0xcc));
    Field<uint32_t>(post, 0x228) = 0;
    return res;
}
}  // namespace

// 0x0047bca0 (esi = &index; stack: amount, levels, &fraction, caller pops): amount clamped to [0, 1] (NaN -> 0),
// scaled by levels; index = truncation capped at levels - 1, fraction = scaled - index. Returns &fraction.
uint32_t Post_SplitLevel(int32_t* index, float amount, int32_t levels, float* fraction)
{
    float t = amount;
    if (t > K(kZero))
    {
        if (!(t < K(kOne)))
            t = K(kOne);
    }
    else
        t = K(kZero);
    const float scaled = t * static_cast<float>(levels);
    int32_t i = Ftol(scaled);
    *index = i;
    if (!(i < levels - 1))
        i = levels - 1;
    *index = i;
    *fraction = scaled - static_cast<float>(i);
    return u(fraction);
}

// 0x0047c030 (eax = node, edx = list head; stack: unused, callee pops 4): unlinks the node (+0 next, +4 prev, the
// head itself acting as a prev node) and pushes it at the front of the list.
void PostEffect_MoveToList(uint32_t* node, uint32_t* head)
{
    if (node[0])
        P<uint32_t>(node[0])[1] = node[1];
    if (node[1])
        P<uint32_t>(node[1])[0] = node[0];
    node[1] = u(head);
    node[0] = *head;
    if (*head)
        P<uint32_t>(*head)[1] = u(node);
    *head = u(node);
}

// 0x0047bd10 (edx = manager; caller 0x004011e0): constructor: vtable 0x0059e52c, both lists empty, accumulators
// (+0x690, 0x18 dwords) and the 16 nodes zeroed, the nodes chained into the free list (+8).
uint32_t PostEffects_Construct(void* mgr)
{
    uint8_t* m = static_cast<uint8_t*>(mgr);
    Field<uint32_t>(m, 0) = 0x0059e52cu;
    Field<uint32_t>(m, 8) = 0;
    Field<uint32_t>(m, 4) = 0;
    std::memset(m + 0x690, 0, 0x18 * 4);
    std::memset(m + 0xc, 0, 0x190 * 4);
    uint8_t* node = m + 0xc;
    for (int k = 15; k != 0; --k)
    {
        uint8_t* next = node + 0x64;
        Field<uint8_t*>(node, 0) = next;
        Field<uint8_t*>(node, 0x68) = node;
        node = next;
    }
    Field<uint8_t*>(m, 8) = m + 0xc;
    Field<uint8_t*>(m, 0x10) = m + 8;
    return u(m);
}

// 0x0047bd80 (esi = manager): moves every live effect back to the free list. Returns 0.
uint32_t PostEffects_Clear(void* mgr)
{
    uint32_t* head = reinterpret_cast<uint32_t*>(u(mgr) + 8);
    while (uint32_t n = Field<uint32_t>(mgr, 4))
        PostEffect_MoveToList(P<uint32_t>(n), head);
    return 0;
}

// 0x0047bd70 (eax = manager; caller 0x00401710): destructor body: vtable 0x0059e52c, then PostEffects_Clear.
uint32_t PostEffects_Destruct(void* mgr)
{
    Field<uint32_t>(mgr, 0) = 0x0059e52cu;
    return PostEffects_Clear(mgr);
}

namespace
{
// The common part of the effect starters: a free node moved to the live list, type and id set, flags, phase and
// time cleared, the default phase durations [0x00588da8..b0].
uint32_t* PostEffects_Start(void* mgr, uint32_t type, uint32_t id)
{
    uint32_t* node = Field<uint32_t*>(mgr, 8);
    if (!node)
        return nullptr;
    PostEffect_MoveToList(node, reinterpret_cast<uint32_t*>(u(mgr) + 4));
    node[3] = id;
    node[2] = type;
    node[4] = 0;
    node[5] = 0;
    node[6] = 0;
    node[7] = GameGlobal<uint32_t>(0x00588da8);
    node[8] = GameGlobal<uint32_t>(0x00588dac);
    node[9] = GameGlobal<uint32_t>(0x00588db0);
    return node;
}
}  // namespace

// 0x0047bda0 (ecx = manager, esi = (colour, alpha); stack: id, callee pops 4; caller: the fx interpreter
// 0x00420770): starts a type 5 (fade) effect. Returns the node (0 when none is free).
uint32_t PostEffects_AddFade(void* mgr, const uint32_t* params, uint32_t id)
{
    uint32_t* node = PostEffects_Start(mgr, 5, id);
    if (!node)
        return 0;
    node[0x11] = params[0];
    node[0x12] = params[1];
    return u(node);
}

// 0x0047be00 (ecx = manager, esi = 4 floats; stack: id, callee pops 4): starts a type 0 (colour) effect.
uint32_t PostEffects_AddColour(void* mgr, const uint32_t* params, uint32_t id)
{
    uint32_t* node = PostEffects_Start(mgr, 0, id);
    if (!node)
        return 0;
    node[0x11] = params[0];
    node[0x12] = params[1];
    node[0x13] = params[2];
    node[0x14] = params[3];
    return u(node);
}

// 0x0047be60 (ecx = manager; stack: id, &amount, callee pops 8): starts a type 1 (blur) effect.
uint32_t PostEffects_AddBlur(void* mgr, uint32_t id, const uint32_t* amount)
{
    uint32_t* node = PostEffects_Start(mgr, 1, id);
    if (!node)
        return 0;
    node[0x11] = *amount;
    return u(node);
}

// 0x0047bec0 (ecx = manager; stack: id, &amount, callee pops 8): starts a type 3 (screen texture) effect.
uint32_t PostEffects_AddType3(void* mgr, uint32_t id, const uint32_t* amount)
{
    uint32_t* node = PostEffects_Start(mgr, 3, id);
    if (!node)
        return 0;
    node[0x11] = *amount;
    return u(node);
}

// 0x0047bf20 (ecx = manager; stack: id, 8 floats, callee pops 8): starts a type 2 (blur rings) effect.
uint32_t PostEffects_AddBlurRings(void* mgr, uint32_t id, const uint32_t* params)
{
    uint32_t* node = PostEffects_Start(mgr, 2, id);
    if (!node)
        return 0;
    std::memcpy(node + 0x11, params, 8 * 4);
    return u(node);
}

// 0x0047bf80 (ecx = manager; stack: id, 8 floats, callee pops 8): starts a type 4 (screen texture rings) effect.
uint32_t PostEffects_AddType4(void* mgr, uint32_t id, const uint32_t* params)
{
    uint32_t* node = PostEffects_Start(mgr, 4, id);
    if (!node)
        return 0;
    std::memcpy(node + 0x11, params, 8 * 4);
    return u(node);
}

// 0x0047bfe0 (edi = manager; stack: id, now, callee pops 8): stops every effect with that id: freed at once when
// now is set, else switched to phase 2 (fade out) from time 0 unless already there. Returns 0.
uint32_t PostEffects_Stop(void* mgr, uint32_t id, uint32_t now)
{
    uint32_t* node = Field<uint32_t*>(mgr, 4);
    while (node)
    {
        uint32_t* next = P<uint32_t>(node[0]);
        if (node[3] == id)
        {
            if (now)
                PostEffect_MoveToList(node, reinterpret_cast<uint32_t*>(u(mgr) + 8));
            else if (node[5] != 2)
            {
                node[5] = 2;
                node[6] = 0;
            }
        }
        node = next;
    }
    return 0;
}

// 0x0047c060 (eax = manager, ecx = first only, edx = position, esi = id; caller 0x0041bd30): sets the position
// (+0x28) of the effects with that id (the first only when ecx). Returns that node, or 0.
uint32_t PostEffects_SetPosition(void* mgr, uint32_t firstOnly, const uint32_t* pos, uint32_t id)
{
    for (uint32_t* node = Field<uint32_t*>(mgr, 4); node; node = P<uint32_t>(node[0]))
    {
        if (node[3] != id)
            continue;
        node[10] = pos[0];
        node[11] = pos[1];
        node[12] = pos[2];
        if (firstOnly)
            return u(node);
    }
    return 0;
}

// 0x0047c0a0 (eax = manager, ebx = first only; stack: id, near, far, callee pops 12): distance fade of the effects
// with that id: a negative distance disables it (flag 1 off, scale 0, offset 1); else far is nudged by
// [0x005bcc64] when equal, scale = 1 / (near - far), offset = -scale * far, flag 1 on. EAX was left over (the
// manager when nothing matched, else flags or FPU status: the flags or the manager here).
uint32_t PostEffects_SetDistanceFade(void* mgr, uint32_t firstOnly, uint32_t id, float nearD, float farD)
{
    uint32_t r = u(mgr);
    for (uint32_t* node = Field<uint32_t*>(mgr, 4); node; node = P<uint32_t>(node[0]))
    {
        if (node[3] != id)
            continue;
        if (nearD < K(kZero) || farD < K(kZero))
        {
            node[4] &= ~1u;
            node[0xd] = 0;
            node[0xe] = 0x3f800000u;
        }
        else
        {
            if (nearD == farD)
                farD = nearD + K(0x005bcc64);
            const float inv = K(kOne) / (nearD - farD);
            Field<float>(node, 0x34) = inv;
            Field<float>(node, 0x38) = -(inv * farD);
            node[4] |= 1;
            r = node[4];
        }
        if (firstOnly)
            break;
    }
    return r;
}

// 0x0047c140 (eax = manager, edx = first only, edi = id; stack: near, far, callee pops 8): frustum edge fade of
// the effects with that id: scale = 1 / (near - far) (far nudged by [0x005bcc64] when equal) at +0x3c, offset
// -scale * far at +0x40, flag 2 on. Returns the flags (the manager when nothing matched).
uint32_t PostEffects_SetEdgeFade(void* mgr, uint32_t firstOnly, uint32_t id, float nearD, float farD)
{
    uint32_t r = u(mgr);
    for (uint32_t* node = Field<uint32_t*>(mgr, 4); node; node = P<uint32_t>(node[0]))
    {
        if (node[3] != id)
            continue;
        if (nearD == farD)
            farD = nearD + K(0x005bcc64);
        const float inv = K(kOne) / (nearD - farD);
        Field<float>(node, 0x3c) = inv;
        Field<float>(node, 0x40) = -(inv * farD);
        r = node[4] | 2;
        node[4] = r;
        if (firstOnly)
            break;
    }
    return r;
}

// 0x0047c1a0 (eax = manager, ecx = first only, edx = id; stack: in, hold, out, callee pops 12): phase durations of
// the effects with that id. Returns the node (first only) or 0.
uint32_t PostEffects_SetTimes(void* mgr, uint32_t firstOnly, uint32_t id, uint32_t in, uint32_t hold, uint32_t out)
{
    for (uint32_t* node = Field<uint32_t*>(mgr, 4); node; node = P<uint32_t>(node[0]))
    {
        if (node[3] != id)
            continue;
        node[7] = in;
        node[8] = hold;
        node[9] = out;
        if (firstOnly)
            return u(node);
    }
    return 0;
}

// 0x0047c1d0 (eax = manager, ecx = first only, edx = on, esi = id; caller 0x00403150): flag 4 (keeps running while
// Game+0x2684 is set) of the effects with that id. Returns the node (first only) or 0.
uint32_t PostEffects_SetIgnorePause(void* mgr, uint32_t firstOnly, uint32_t on, uint32_t id)
{
    for (uint32_t* node = Field<uint32_t*>(mgr, 4); node; node = P<uint32_t>(node[0]))
    {
        if (node[3] != id)
            continue;
        node[4] = on ? (node[4] | 4) : (node[4] & ~4u);
        if (firstOnly)
            return u(node);
    }
    return 0;
}

// 0x0047c200 (eax = node, ecx = manager; stack: dt, callee pops 4; caller PostProcess_Pass): advances one effect
// and adds it to the frame's accumulators. Time += dt unless paused (Game+0x2684) without flag 4; at the end of a
// phase (duration < 0 holds) the next starts from 0, after phase 2 the node is freed. Weight w = t / in, 1, or
// 1 - t / out; flag 1 multiplies it by clamp(|camera-space position| * +0x34 + +0x38), flag 2 by
// clamp(+0x40 - +0x3c * min distance to the scene pass frustum planes (0x00488990 on Game+0xa78), start 1e7).
// Type 0: colour factors (p - 1) * w + 1 (skipped when the first is 1) folded with min into +0x690/+0x694 and
// p * w with max into +0x698/+0x69c; 1/3: p * w maxed into +0x6a0/+0x6c4; 2/4: the 8 floats with both amounts
// (+3, +7) weighted copied to +0x6a4/+0x6c8 unless both are 0; 5: alpha * w, if larger than +0x6ec, replaces it
// and its colour (+0x6e8). EAX was left over (FPU status); 0 here, the node when freed.
uint32_t PostEffect_Apply(void* nodeP, void* mgr, float dt)
{
    uint8_t* node = static_cast<uint8_t*>(nodeP);
    uint8_t* m = static_cast<uint8_t*>(mgr);
    const uint32_t flags = Field<uint32_t>(node, 0x10);
    if ((flags & 4) || Field<uint32_t>(g_game, 0x2684) == 0)
        Field<float>(node, 0x18) = dt + Field<float>(node, 0x18);
    auto duration = [&](uint32_t phase) { return Field<float>(node, 0x1c + phase * 4); };
    if (Field<float>(node, 0x18) >= duration(Field<uint32_t>(node, 0x14)))
    {
        for (;;)
        {
            const uint32_t phase = Field<uint32_t>(node, 0x14);
            if (!(duration(phase) >= K(kZero)))
                break;
            const uint32_t next = phase + 1;
            Field<uint32_t>(node, 0x18) = 0;
            Field<uint32_t>(node, 0x14) = next;
            if (next == 3)
            {
                PostEffect_MoveToList(reinterpret_cast<uint32_t*>(node), reinterpret_cast<uint32_t*>(m + 8));
                return u(node);
            }
            if (!(Field<float>(node, 0x18) >= duration(next)))
                break;
        }
    }
    float w;
    const uint32_t phase = Field<uint32_t>(node, 0x14);
    if (phase == 0)
        w = Field<float>(node, 0x18) / Field<float>(node, 0x1c);
    else if (phase == 2)
        w = K(kOne) - Field<float>(node, 0x18) / Field<float>(node, 0x24);
    else
        w = 1.0f;
    if (flags & 3)
    {
        float f = K(kOne);
        if (flags & 1)
        {
            float p[8];
            orig::fn_004e36a0(u(node) + 0x28, u(m) + 0x64c, 1, u(p));
            const float d = sqrtf((p[2] * p[2] + p[1] * p[1]) + p[0] * p[0]) * Field<float>(node, 0x34) +
                            Field<float>(node, 0x38);
            f = Clamp01(d);
        }
        if (flags & 2)
        {
            uint32_t planes = 0;
            uint32_t n = orig::fn_00488990(Field<uint32_t>(g_game, 0xa78), u(&planes));
            float best = K(0x005bcd08);
            const float* pl = P<float>(planes);
            const float* pos = &Field<float>(node, 0x28);
            for (; n != 0; --n, pl += 4)
            {
                const float dp = ((pl[2] * pos[2] + pl[1] * pos[1]) + pl[0] * pos[0]) + pl[3];
                if (dp < best)
                    best = dp;
            }
            f = f * Clamp01(Field<float>(node, 0x40) - best * Field<float>(node, 0x3c));
        }
        w = f * w;
    }
    switch (Field<uint32_t>(node, 8))
    {
    case 0:
    {
        const float e = Field<float>(node, 0x50) * w;
        const float a = Field<float>(node, 0x4c) * w;
        const float b = (Field<float>(node, 0x48) - K(kOne)) * w + K(kOne);
        const float c = (Field<float>(node, 0x44) - K(kOne)) * w + K(kOne);
        if (c == K(kOne))
            break;
        Field<float>(m, 0x69c) = (e > Field<float>(m, 0x69c)) ? e : Field<float>(m, 0x69c);
        Field<float>(m, 0x698) = (a > Field<float>(m, 0x698)) ? a : Field<float>(m, 0x698);
        Field<float>(m, 0x694) = (b < Field<float>(m, 0x694)) ? b : Field<float>(m, 0x694);
        Field<float>(m, 0x690) = (c < Field<float>(m, 0x690)) ? c : Field<float>(m, 0x690);
        break;
    }
    case 1:
    case 3:
    {
        float& acc = Field<float>(m, Field<uint32_t>(node, 8) == 1 ? 0x6a0 : 0x6c4);
        const float v = Field<float>(node, 0x44) * w;
        acc = (v < acc) ? acc : v;
        break;
    }
    case 2:
    case 4:
    {
        uint8_t* dst = m + (Field<uint32_t>(node, 8) == 2 ? 0x6a4 : 0x6c8);
        float buf[8];
        std::memcpy(buf, node + 0x44, sizeof(buf));
        buf[3] = buf[3] * w;
        buf[7] = buf[7] * w;
        if (buf[3] != K(kZero) || buf[7] != K(kZero))
            std::memcpy(dst, buf, sizeof(buf));
        break;
    }
    case 5:
    {
        const float v = w * Field<float>(node, 0x48);
        if (v > Field<float>(m, 0x6ec))
        {
            Field<float>(m, 0x6ec) = v;
            Field<uint32_t>(m, 0x6e8) = Field<uint32_t>(node, 0x44);
        }
        break;
    }
    default:
        break;
    }
    return 0;
}

uint32_t Post_DrawColour(void* post, float brightness, float a1, float a2, float a3);
uint32_t Post_DrawBlur(void* post, float amount);
uint32_t Post_DrawBlurRings(const float* a, void* post, const float* b);
uint32_t Post_DrawType3(void* post, float amount);
uint32_t Post_DrawType3Rings(void* post, const float* a, const float* b);
uint32_t Post_DrawFade(void* post, uint32_t colour, float alpha);

// 0x0047dbd0 (eax = post; caller PostProcess_Pass when no colour effect is active): clears post +0xe0 (set by
// Post_DrawColour when post +0xe1; guess: "previous frame's colour target valid").
void Post_ClearE0(void* post)
{
    Field<uint8_t>(post, 0xe0) = 0;
}

// 0x0047c600 (esi = effect manager Game+0xa9c; stack: post Game+0xa98, dt, callee pops 8; caller: the scene pass,
// flag 0x40): clears the accumulators (+0x690 and +0x694 = 1), the camera matrix (0x0040fa50) into +0x64c, applies
// every live effect (PostEffect_Apply), then draws: colour (when +0x690 != 1.0 bitwise; else Post_ClearE0), blur
// (+0x6a0 alone, or the ring pair +0x6a4/+0x6b4 when either amount +0x6b0/+0x6c0 is set, raised to +0x6a0), the
// type 3 overlay likewise (+0x6c4, rings +0x6c8/+0x6d8), and the fade (+0x6e8, +0x6ec) when its alpha is set.
uint32_t PostProcess_Pass(void* mgr, void* post, float dt)
{
    uint8_t* m = static_cast<uint8_t*>(mgr);
    std::memset(m + 0x690, 0, 0x18 * 4);
    Field<uint32_t>(m, 0x694) = 0x3f800000u;
    Field<uint32_t>(m, 0x690) = 0x3f800000u;
    orig::fn_0040fa50(u(m) + 0x64c, Field<uint32_t>(g_game, 0xa68));
    for (uint32_t node = Field<uint32_t>(m, 4); node;)
    {
        const uint32_t next = Field<uint32_t>(P(node), 0);
        PostEffect_Apply(P(node), m, dt);
        node = next;
    }
    if (Field<uint32_t>(m, 0x690) != 0x3f800000u)
        Post_DrawColour(post, Field<float>(m, 0x690), Field<float>(m, 0x694), Field<float>(m, 0x698),
                        Field<float>(m, 0x69c));
    else
        Post_ClearE0(post);
    if (!(Field<float>(m, 0x6b0) != K(kZero)) && !(Field<float>(m, 0x6c0) != K(kZero)))
    {
        if (Field<float>(m, 0x6a0) != K(kZero))
            Post_DrawBlur(post, Field<float>(m, 0x6a0));
    }
    else
    {
        if (Field<float>(m, 0x6a0) != K(kZero))
        {
            Field<float>(m, 0x6b0) =
                (Field<float>(m, 0x6a0) > Field<float>(m, 0x6b0)) ? Field<float>(m, 0x6a0) : Field<float>(m, 0x6b0);
            Field<float>(m, 0x6c0) =
                (Field<float>(m, 0x6a0) > Field<float>(m, 0x6c0)) ? Field<float>(m, 0x6a0) : Field<float>(m, 0x6c0);
        }
        Post_DrawBlurRings(&Field<float>(m, 0x6a4), post, &Field<float>(m, 0x6b4));
    }
    if (!(Field<float>(m, 0x6d4) != K(kZero)) && !(Field<float>(m, 0x6e4) != K(kZero)))
    {
        if (Field<float>(m, 0x6c4) != K(kZero))
            Post_DrawType3(post, Field<float>(m, 0x6c4));
    }
    else
    {
        if (Field<float>(m, 0x6c4) != K(kZero))
        {
            Field<float>(m, 0x6d4) =
                (Field<float>(m, 0x6c4) > Field<float>(m, 0x6d4)) ? Field<float>(m, 0x6c4) : Field<float>(m, 0x6d4);
            Field<float>(m, 0x6e4) =
                (Field<float>(m, 0x6c4) > Field<float>(m, 0x6e4)) ? Field<float>(m, 0x6c4) : Field<float>(m, 0x6e4);
        }
        Post_DrawType3Rings(post, &Field<float>(m, 0x6c8), &Field<float>(m, 0x6d8));
    }
    if (Field<float>(m, 0x6ec) != K(kZero))
        Post_DrawFade(post, Field<uint32_t>(m, 0x6e8), Field<float>(m, 0x6ec));
    return 0;
}

// 0x0047c850 (edx = post; caller 0x004011e0): initialises the draw object's target tables and state blocks:
// +0xdc, +0x228 and the tables +0xe4 (0x51 dwords), +0x22c, +0x24c (8 each) and the blocks +0x26c (0x24 dwords)
// cleared; block flags 0x130e4, 0x1e0e4, 0x1e6e4, 0x131e4, 0x151e4, 0x133e4; colours [0x0064a2fc] for the blocks
// at +0x26c, +0x284, +0x2b4, +0x2e4, 0xffffff for +0x29c, 0x40404040 for +0x2cc. Returns post.
uint32_t PostTargets_Init(void* post)
{
    uint8_t* p = static_cast<uint8_t*>(post);
    Field<uint32_t>(p, 0xdc) = 0;
    Field<uint32_t>(p, 0x228) = 0;
    std::memset(p + 0xe4, 0, 0x51 * 4);
    std::memset(p + 0x22c, 0, 8 * 4);
    std::memset(p + 0x24c, 0, 8 * 4);
    std::memset(p + 0x26c, 0, 0x24 * 4);
    const uint32_t c = ScreenColour();
    Field<uint32_t>(p, 0x26c) = 0x130e4u;
    Field<uint32_t>(p, 0x270) = c;
    Field<uint32_t>(p, 0x288) = c;
    Field<uint32_t>(p, 0x2b8) = c;
    Field<uint32_t>(p, 0x2e8) = c;
    Field<uint32_t>(p, 0x284) = 0x1e0e4u;
    Field<uint32_t>(p, 0x29c) = 0x1e6e4u;
    Field<uint32_t>(p, 0x2a0) = 0xffffffu;
    Field<uint32_t>(p, 0x2b4) = 0x131e4u;
    Field<uint32_t>(p, 0x2cc) = 0x151e4u;
    Field<uint32_t>(p, 0x2d0) = 0x40404040u;
    Field<uint32_t>(p, 0x2e4) = 0x133e4u;
    return u(p);
}

// 0x0047caf0 (ebx = UV scale out, edi = source texture or 0; stack: unused, target (0 = back buffer), pixel offset
// out, UV offset out; callee pops 16): the size of the target (renderer virtual +0x138 (target, &w, &h), written
// into the original's own argument slots) and of the texture (+0x134 UV extent into the UV scale, +0x138 size;
// none: scale 1, the target's size). Pixel offset = ([0x00588db4] / w, [0x00588db8] / h) (-0.5, 0.5); UV offset =
// ((1 - tw / w) * [0x00588dbc] / tw, (1 - th / h) * [0x00588dc0] / th). Returns the pixel offset pointer.
uint32_t Post_GetQuadMapping(float* uvScale, uint32_t texture, uint32_t /*unused*/, uint32_t target, float* pixel,
                             float* uvOffset)
{
    void* r = Renderer();
    int32_t w = 0, h = 0, tw = 0, th = 0;
    VCall<void>(r, 0x138 / 4, target, static_cast<void*>(&w), static_cast<void*>(&h));
    if (texture)
    {
        VCall<void>(r, 0x134 / 4, texture, static_cast<void*>(uvScale), static_cast<void*>(uvScale + 1));
        VCall<void>(r, 0x138 / 4, texture, static_cast<void*>(&tw), static_cast<void*>(&th));
    }
    else
    {
        uvScale[1] = 1.0f;
        uvScale[0] = 1.0f;
        tw = w;
        th = h;
    }
    const float fw = static_cast<float>(w);
    pixel[0] = K(0x00588db4) / fw;
    const float fh = static_cast<float>(h);
    pixel[1] = K(0x00588db8) / fh;
    const float ftw = static_cast<float>(tw);
    const float du = K(kOne) - ftw / fw;
    uvOffset[0] = du;
    const float fth = static_cast<float>(th);
    const float dv = K(kOne) - fth / fh;
    uvOffset[1] = dv;
    uvOffset[0] = du * K(0x00588dbc) / ftw;
    uvOffset[1] = dv * K(0x00588dc0) / fth;
    return u(pixel);
}

// 0x0047cbe0 (eax = state block, esi = outer circle (x, y, r, a); stack: post (passed on, unused), inner circle,
// inner colour, outer colour, segments; callee pops 20): a ring between two circles as a strip of 2 * segments
// vertices, angles 0 .. 2 pi ([0x005bcb74] / (segments - 1)), in the target's [0, 1] space with y flipped
// ((pixel.y + 1) - y) and UVs (offset + p) * scale from Post_GetQuadMapping (target 0).
uint32_t Post_DrawRing(void* block, const float* outer, uint32_t post, const float* inner, uint32_t innerColour,
                       uint32_t outerColour, int32_t segments)
{
    VCall<void>(States(), 0x14 / 4, block);
    VCall<void>(States(), 0xc / 4, 0u);
    float uvOffset[2], uvScale[2], pixel[2];
    Post_GetQuadMapping(uvScale, Field<uint32_t>(block, 8), post, 0, pixel, uvOffset);
    void* builder = VCall<void*>(Batch(), 0x14 / 4, 0u, 2u, 1u, 0u);
    if (builder)
    {
        float* v = VCall<float*>(builder, 4 / 4, static_cast<uint32_t>(segments + segments));
        const float step = K(0x005bcb74) / (static_cast<float>(segments) - K(kOne));
        float angle = K(kZero);
        if (segments > 0)
        {
            const float q = pixel[1] + K(kOne);
            for (int32_t i = segments; i != 0; --i, v += 12)
            {
                uint32_t* vd = reinterpret_cast<uint32_t*>(v);
                const float s = X87Sin(angle);
                const float c = X87Cos(angle);
                const float x = s * inner[2] + inner[0];
                const float y = c * inner[2] + inner[1];
                vd[3] = innerColour;
                vd[2] = 0;
                v[0] = pixel[0] + x;
                v[1] = q - y;
                v[4] = (uvOffset[0] + x) * uvScale[0];
                v[5] = (uvOffset[1] + y) * uvScale[1];
                const float x2 = s * outer[2] + outer[0];
                const float y2 = c * outer[2] + outer[1];
                vd[8] = 0;
                vd[9] = outerColour;
                v[6] = pixel[0] + x2;
                v[7] = q - y2;
                v[10] = (uvOffset[0] + x2) * uvScale[0];
                v[11] = (uvOffset[1] + y2) * uvScale[1];
                angle = angle + step;
            }
        }
    }
    return VCall<uint32_t>(Batch(), 0x1c / 4);
}

// 0x0047cd50 (eax = texture 1, ecx = outer circle, edx = texture 2, edi = post; stack: inner circle, callee pops
// 4): draws the ring between the circles with texture 1 (block +0x26c, both colours [0x0064a2fc], 32 segments)
// and with texture 2 (block +0x2e4, colours [0x0064a2fc] with each circle's alpha (+0xc) * 255).
uint32_t Post_DrawRingPair(uint32_t tex1, const float* outer, uint32_t tex2, void* post, const float* inner)
{
    uint32_t r = tex1;
    if (tex1)
    {
        Field<uint32_t>(post, 0x274) = tex1;
        const uint32_t c = ScreenColour();
        r = Post_DrawRing(reinterpret_cast<uint8_t*>(post) + 0x26c, outer, u(post), inner, c, c, 0x20);
    }
    if (tex2)
    {
        Field<uint32_t>(post, 0x2ec) = tex2;
        const uint32_t rgb = ScreenColour() & 0xffffff;
        const uint32_t outerColour = (static_cast<uint32_t>(Ftol(outer[3] * K(k255))) << 24) | rgb;
        const uint32_t innerColour = (static_cast<uint32_t>(Ftol(inner[3] * K(k255))) << 24) | rgb;
        r = Post_DrawRing(reinterpret_cast<uint8_t*>(post) + 0x2e4, outer, u(post), inner, innerColour, outerColour,
                          0x20);
    }
    return r;
}

// 0x0047cdd0 (eax = source corners or 0 ([0x00588d68]: (0,0) (1,0) (0,1) (1,1)), ecx = render target (0 = back
// buffer); stack: post (passed on), state block, destination corners or 0 ([0x00588d88]), corner colours or 0
// (block colour); callee pops 16; docs/subsystems/ui.md): sets the target (renderer virtual +0xcc (target, 0, 0,
// 0); fails -> returns its AL = 0), the state block, and draws a 4-vertex strip with positions pixel offset +
// destination corners and UVs (UV offset + source corner) * UV scale (Post_GetQuadMapping). Returns AL = 1.
uint32_t Post_DrawScreenQuad(const float* src, uint32_t target, uint32_t post, void* block, const float* dst,
                             const uint32_t* colours)
{
    void* r = Renderer();
    if (!src)
        src = &GameGlobal<float>(0x00588d68);
    if (!dst)
        dst = &GameGlobal<float>(0x00588d88);
    const uint32_t ok = VCall<uint32_t>(r, 0xcc / 4, target, 0u, 0u, 0u);
    if ((ok & 0xff) == 0)
        return ok;
    VCall<void>(States(), 0x14 / 4, block);
    VCall<void>(States(), 0xc / 4, 0u);
    float pixel[2] = {0.0f, 0.0f}, uvOffset[2], uvScale[2];
    Post_GetQuadMapping(uvScale, Field<uint32_t>(block, 8), post, target, pixel, uvOffset);
    void* builder = VCall<void*>(Batch(), 0x14 / 4, 0u, 2u, 1u, 0u);
    if (builder)
    {
        float* v = VCall<float*>(builder, 4 / 4, 4u);
        for (int i = 0; i < 4; ++i, v += 6)
        {
            uint32_t* vd = reinterpret_cast<uint32_t*>(v);
            v[0] = pixel[0] + dst[i * 2];
            v[1] = pixel[1] + dst[i * 2 + 1];
            vd[2] = 0;
            vd[3] = colours ? colours[i] : Field<uint32_t>(block, 4);
            v[4] = (uvOffset[0] + src[i * 2]) * uvScale[0];
            v[5] = (uvOffset[1] + src[i * 2 + 1]) * uvScale[1];
        }
    }
    return (VCall<uint32_t>(Batch(), 0x1c / 4) & 0xffffff00u) | 1;
}

// 0x0047cf10 (eax = first state block or 0, edx = source texture; stack: post, steps, block or 0 (post +0x26c,
// its colour set to [0x0064a2fc]), targets or 0 (post +0x22c); callee pops 16): copies the source through
// min(steps, post +0xdc) targets, each step drawing the previous one full-screen into the next (Post_DrawScreenQuad;
// the first step with the given block), stopping at a null target or a failed draw; then the back buffer is set
// again (renderer +0xcc (0, 0, 0, 0)). Returns the last draw's success (0 for no steps or no source).
uint32_t Post_BlitChain(void* block, uint32_t texture, void* post, int32_t steps, void* defaultBlock,
                        const uint32_t* targets)
{
    void* r = Renderer();
    uint8_t ok = 0;
    const int32_t available = Field<int32_t>(post, 0xdc);
    if (!(steps < available))
        steps = available;
    if (steps == 0)
        return 0;
    if (!texture)
        return 0;
    if (!targets)
        targets = &Field<uint32_t>(post, 0x22c);
    if (!defaultBlock)
    {
        defaultBlock = reinterpret_cast<uint8_t*>(post) + 0x26c;
        Field<uint32_t>(post, 0x270) = ScreenColour();
    }
    if (!block)
        block = defaultBlock;
    for (int32_t i = 0; i < steps; ++i)
    {
        const uint32_t t = targets[i];
        if (!t)
            break;
        Field<uint32_t>(block, 8) = texture;
        ok = static_cast<uint8_t>(Post_DrawScreenQuad(nullptr, t, u(post), block, nullptr, nullptr));
        if (!ok)
            break;
        block = defaultBlock;
        texture = t;
    }
    VCall<void>(r, 0xcc / 4, 0u, 0u, 0u, 0u);
    return ok;
}

// 0x0047cff0 (ebx = post; stack: colour, alpha, callee pops 8; docs/subsystems/ui.md "full-screen fade"): begin 2D
// (0x00415dc0, esi = renderer, edi = post; 0, 1), a full-screen quad with state block {0x103a0, alpha (clamped,
// * 255) << 24 | colour rgb}, then the saved state restored (Post_EndDraw).
uint32_t Post_DrawFade(void* post, uint32_t colour, float alpha)
{
    void* r = Renderer();
    uint32_t block[6] = {};
    const float a = Clamp01(alpha);
    block[1] = (static_cast<uint32_t>(Ftol(a * K(k255))) << 24) | (colour & 0xffffff);
    block[0] = 0x103a0u;
    orig::fn_00415dc0(u(r), u(post), 0, 1);
    Post_DrawScreenQuad(nullptr, 0, u(post), block, nullptr, nullptr);
    return Post_EndDraw(r, post);
}

// 0x0047d0f0 (eax = post, ecx = unused (the original stores it into a local that is overwritten); stack: amount,
// callee pops 4): screen blur, when Game+0x4bc (guess: post effects enabled): the back buffer copy (0x00415dc0
// (1, 0) returns it) is chained down index + 1 targets (Post_SplitLevel over post +0xdc levels); target index - 1
// is drawn opaque (block +0x26c) when index > 0, and target index over it with alpha fraction (block +0x2e4).
uint32_t Post_DrawBlur(void* post, float amount)
{
    if (!Field<uint8_t>(g_game, 0x4bc))
        return u(g_game);
    void* r = Renderer();
    const uint32_t copy = orig::fn_00415dc0(u(r), u(post), 1, 0);
    if (copy)
    {
        int32_t index = 0;
        float fraction = 0.0f;
        Post_SplitLevel(&index, amount, Field<int32_t>(post, 0xdc), &fraction);
        if (Post_BlitChain(nullptr, copy, post, index + 1, nullptr, nullptr))
        {
            uint8_t* p = static_cast<uint8_t*>(post);
            if (index)
            {
                Field<uint32_t>(p, 0x274) = Field<uint32_t>(p, 0x228 + index * 4);
                Field<uint32_t>(p, 0x270) = ScreenColour();
                Post_DrawScreenQuad(nullptr, 0, u(p), p + 0x26c, nullptr, nullptr);
            }
            Field<uint32_t>(p, 0x2ec) = Target(p, index);
            Field<uint32_t>(p, 0x2e8) =
                (static_cast<uint32_t>(Ftol(fraction * K(k255))) << 24) | (ScreenColour() & 0xffffff);
            Post_DrawScreenQuad(nullptr, 0, u(p), p + 0x2e4, nullptr, nullptr);
        }
    }
    return Post_EndDraw(r, post);
}

// 0x0047d230 (eax = ring A (x, y, r, amount), ecx = post; stack: ring B, callee pops 4): blur that varies across
// the screen between two circles (guess: radial blur). The rings are ordered by amount, both split into level and
// fraction, the back buffer copy chained down to B's level + 1. The disc inside A (radius 0, or 0.7071 when A is
// not smaller than B) is drawn with A's level (Post_DrawRingPair) when A's fraction is set and 0 < A.r < 0.7071;
// when the radii differ, rings interpolated from A to B (t over the level span, B's fraction at its own level, else
// alpha 1; the previous ring's alpha set to 0) are drawn level by level; and the area outside B likewise with B's
// level. Then the saved state restored.
uint32_t Post_DrawBlurRings(const float* a, void* post, const float* b)
{
    void* r = Renderer();
    if (!Field<uint8_t>(g_game, 0x4bc))
        return u(g_game);
    const uint32_t copy = orig::fn_00415dc0(u(r), u(post), 1, 0);
    if (copy)
    {
        if (a[3] > b[3])
        {
            const float* t = a;
            a = b;
            b = t;
        }
        int32_t indexA = 0, indexB = 0;
        float fracA = 0.0f, fracB = 0.0f;
        Post_SplitLevel(&indexA, a[3], Field<int32_t>(post, 0xdc), &fracA);
        Post_SplitLevel(&indexB, b[3], Field<int32_t>(post, 0xdc), &fracB);
        if (Post_BlitChain(nullptr, copy, post, indexB + 1, nullptr, nullptr))
        {
            uint8_t* p = static_cast<uint8_t*>(post);
            auto below = [&](int32_t i) -> uint32_t { return i ? Field<uint32_t>(p, 0x228 + i * 4) : 0u; };
            float c28[4], c38[4], c48[4];
            c38[0] = a[0], c38[1] = a[1], c38[2] = a[2], c38[3] = fracA;
            c48[0] = b[0], c48[1] = b[1], c48[2] = b[2], c48[3] = fracB;
            const float k = K(0x005bce2c);
            if (fracA != K(kZero) && c38[2] > K(kZero) && c38[2] < k)
            {
                c28[0] = a[0], c28[1] = a[1];
                c28[2] = (a[2] < b[2]) ? 0.0f : u2f(0x3f3504f3u);
                c28[3] = fracA;
                Post_DrawRingPair(below(indexA), c38, Target(p, indexA), p, c28);
            }
            if (!(c38[2] == c48[2]))
            {
                const float span = (static_cast<float>(indexB - indexA) + fracB) - fracA;
                if (!(indexA > indexB))
                {
                    for (int32_t i = indexA; i <= indexB; ++i)
                    {
                        float t;
                        if (i == indexB)
                        {
                            c28[3] = fracB;
                            t = K(kOne);
                        }
                        else
                        {
                            c28[3] = 1.0f;
                            t = ((static_cast<float>(i - indexA) + K(kOne)) - fracA) / span;
                        }
                        c28[0] = (b[0] - a[0]) * t + a[0];
                        c28[1] = (b[1] - a[1]) * t + a[1];
                        c28[2] = (b[2] - a[2]) * t + a[2];
                        Post_DrawRingPair(below(i), c28, Target(p, i), p, c38);
                        c38[0] = c28[0];
                        c38[1] = c28[1];
                        c38[3] = c28[3];
                        c38[2] = c28[2];
                        c38[3] = 0.0f;
                    }
                }
            }
            if (fracB != K(kZero) && c48[2] > K(kZero) && c48[2] < k)
            {
                c28[0] = c48[0];
                c28[3] = c48[3];
                c28[1] = c48[1];
                c28[2] = (a[2] > b[2]) ? 0.0f : u2f(0x3f3504f3u);
                Post_DrawRingPair(below(indexB), c28, Target(p, indexB), p, c48);
            }
        }
    }
    return Post_EndDraw(r, post);
}

// 0x0047d5c0 (eax = post; stack: amount, callee pops 4): the type 3 overlay: the renderer's texture from virtual
// +0xe0 (post +0x2ec; none -> return 0) drawn full screen with alpha clamp(amount) (block +0x2e4) between begin 2D
// (0x00415dc0 (0, 0)) and Post_EndDraw. Needs Game+0x4bc.
uint32_t Post_DrawType3(void* post, float amount)
{
    if (!Field<uint8_t>(g_game, 0x4bc))
        return u(g_game);
    void* r = Renderer();
    const uint32_t tex = VCall<uint32_t>(r, 0xe0 / 4);
    Field<uint32_t>(post, 0x2ec) = tex;
    if (!tex)
        return 0;
    orig::fn_00415dc0(u(r), u(post), 0, 0);
    const float a = Clamp01(amount);
    Field<uint32_t>(post, 0x2e8) = (static_cast<uint32_t>(Ftol(a * K(k255))) << 24) | (ScreenColour() & 0xffffff);
    Post_DrawScreenQuad(nullptr, 0, u(post), reinterpret_cast<uint8_t*>(post) + 0x2e4, nullptr, nullptr);
    return Post_EndDraw(r, post);
}

// 0x0047d6d0 (eax = post; stack: ring A, ring B, callee pops 8): the type 3 overlay between two circles (ordered
// by radius): the inner disc (when its radius and alpha are set), the ring between them (alphas from each), and
// the outside of the larger one up to radius 0.7071 (when it is smaller than that and its alpha is set); block
// +0x2e4, 32 segments.
uint32_t Post_DrawType3Rings(void* post, const float* a, const float* b)
{
    if (!Field<uint8_t>(g_game, 0x4bc))
        return u(g_game);
    void* r = Renderer();
    const uint32_t tex = VCall<uint32_t>(r, 0xe0 / 4);
    Field<uint32_t>(post, 0x2ec) = tex;
    if (tex)
    {
        orig::fn_00415dc0(u(r), u(post), 0, 0);
        const float* s = a;
        const float* l = b;
        if (s[2] > l[2])
        {
            s = b;
            l = a;
        }
        uint8_t* block = reinterpret_cast<uint8_t*>(post) + 0x2e4;
        const uint32_t rgb = ScreenColour() & 0xffffff;
        const uint32_t cs = (static_cast<uint32_t>(Ftol(s[3] * K(k255))) << 24) | rgb;
        const uint32_t cl = (static_cast<uint32_t>(Ftol(l[3] * K(k255))) << 24) | rgb;
        if (s[2] != K(kZero) && s[3] != K(kZero))
        {
            float centre[4] = {s[0], s[1], 0.0f, s[3]};
            Post_DrawRing(block, s, u(post), centre, cs, cs, 0x20);
        }
        Post_DrawRing(block, l, u(post), s, cs, cl, 0x20);
        if (l[2] < K(0x005bce2c) && l[3] != K(kZero))
        {
            float edge[4] = {l[0], l[1], u2f(0x3f3504f3u), l[3]};
            Post_DrawRing(block, edge, u(post), l, cl, cl, 0x20);
        }
    }
    return Post_EndDraw(r, post);
}

// 0x0047d8a0 (eax = post, ecx = unused (stored into a local that is overwritten); stack: +0x690, +0x694, +0x698,
// +0x69c accumulators, callee pops 16): the colour effect, when Game+0x4bc and renderer virtual +0x160 (5) >= 0x20
// (guess: a capability). The back buffer copy (0x00415dc0 (1, 0)) is chained through all targets with the first
// block +0x284 (or +0x29c when post +0xe0 and the alpha byte of clamp(a3) is not 0) coloured (alpha a3 or 0, grey
// clamp(a0)) and the rest +0x26c coloured 0xff grey (1 - clamp(a1)), targets +0x24c; then targets n-2 .. 0 are drawn
// back up (block +0x2b4 with texture post +0xe4 + i * 0x24, colour -1) and the first target with block +0x2cc
// (0xff grey clamp(a2 * [0x005bcbe4])). post +0xe0 = 1 when post +0xe1.
uint32_t Post_DrawColour(void* post, float a0, float a1, float a2, float a3)
{
    if (!Field<uint8_t>(g_game, 0x4bc))
        return u(g_game);
    void* r = Renderer();
    const int32_t caps = VCall<int32_t>(r, 0x160 / 4, 5u);
    if (caps < 0x20)
        return static_cast<uint32_t>(caps);
    const uint32_t copy = orig::fn_00415dc0(u(r), u(post), 1, 0);
    if (copy)
    {
        uint8_t* p = static_cast<uint8_t*>(post);
        const float g = a2 * K(0x005bcbe4);
        a0 = Clamp01(a0);
        a1 = K(kOne) - Clamp01(a1);
        const float gc = Clamp01(g);
        a3 = Clamp01(a3);
        const uint32_t grey0 = static_cast<uint8_t>(Ftol(a0 * K(k255)));
        const uint32_t grey1 = static_cast<uint8_t>(Ftol(a1 * K(k255)));
        const uint32_t grey2 = static_cast<uint8_t>(Ftol(gc * K(k255)));
        uint32_t alpha;
        uint8_t* block;
        if (Field<uint8_t>(p, 0xe0))
        {
            alpha = static_cast<uint8_t>(Ftol(a3 * K(k255)));
            block = p + (alpha ? 0x29c : 0x284);
        }
        else
        {
            alpha = 0;
            block = p + 0x284;
        }
        Field<uint32_t>(block, 4) = (alpha << 24) | (grey0 << 16) | (grey0 << 8) | grey0;
        Field<uint32_t>(p, 0x270) = 0xff000000u | (grey1 << 16) | (grey1 << 8) | grey1;
        if (Post_BlitChain(block, copy, p, Field<int32_t>(p, 0xdc), p + 0x26c,
                           reinterpret_cast<const uint32_t*>(p + 0x24c)))
        {
            for (int32_t i = Field<int32_t>(p, 0xdc) - 1; i > 0; --i)
            {
                Field<uint32_t>(p, 0x2bc) = u(p) + static_cast<uint32_t>(i) * 0x24 + 0xe4;
                Field<uint32_t>(p, 0x2b8) = 0xffffffffu;
                Post_DrawScreenQuad(nullptr, i ? Field<uint32_t>(p, 0x248 + i * 4) : 0u, u(p), p + 0x2b4, nullptr,
                                    nullptr);
            }
            Field<uint32_t>(p, 0x2d4) = Field<uint32_t>(p, 0x24c);
            Field<uint32_t>(p, 0x2d0) = 0xff000000u | (grey2 << 16) | (grey2 << 8) | grey2;
            Post_DrawScreenQuad(nullptr, 0, u(p), p + 0x2cc, nullptr, nullptr);
            if (Field<uint8_t>(p, 0xe1))
                Field<uint8_t>(p, 0xe0) = 1;
        }
    }
    return Post_EndDraw(r, post);
}

// ---------------------------------------------------------------------------------------------------------------
// ABI adapters: register inputs in the order eax ecx edx ebx esi edi, then stack arguments.
// ---------------------------------------------------------------------------------------------------------------
namespace
{
uint32_t Fx_Step_Abi(uint32_t ecx, uint32_t a0)
{
    return Fx_Step(P(ecx), P(a0));
}
uint32_t FxStack_Push_Abi(uint32_t esi, uint32_t edi, uint32_t /*a0*/, uint32_t a1)
{
    return FxStack_Push(P<uint32_t>(esi), edi, P<uint32_t>(a1));
}
uint32_t FxStack_Pop_Abi(uint32_t ebx, uint32_t edi, uint32_t /*a0*/)
{
    return FxStack_Pop(P<uint32_t>(ebx), P<uint32_t>(edi));
}
uint32_t Fx_UpdateAll_Abi(uint32_t a0)
{
    return Fx_UpdateAll(P(a0));
}
uint32_t Fx_DrawParticles_Abi(uint32_t edi, uint32_t a0)
{
    return Fx_DrawParticles(P(edi), u2f(a0));
}
uint32_t Character_SetFade_Abi(uint32_t ecx, uint32_t a0)
{
    return Character_SetFade(P(ecx), a0);
}
uint32_t UiText_Draw_Abi(uint32_t ecx, uint32_t ebx, uint32_t /*edi*/, uint32_t a0, uint32_t a1, uint32_t a2,
                         uint32_t a3)
{
    return UiText_Draw(P(ecx), ebx, P(a0), u2f(a1), u2f(a2), u2f(a3));
}
uint32_t UiMesh_Draw_Abi(uint32_t ecx)
{
    return UiMesh_Draw(P(ecx));
}
uint32_t UiText_Render_Abi(uint32_t ecx, uint32_t edi)
{
    return UiText_Render(P(ecx), edi);
}
uint32_t UiText_DrawBackground_Abi(uint32_t eax)
{
    return UiText_DrawBackground(P(eax));
}
uint32_t Ui_DrawLineInt_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2,
                            uint32_t a3)
{
    return Ui_DrawLineInt(P(eax), ecx, edx, a0, a1, a2, a3);
}
uint32_t Ui_DrawRect_Abi(uint32_t eax, uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                         uint32_t a5, uint32_t a6)
{
    return Ui_DrawRect(P(eax), esi, a0, a1, a2, a3, a4, a5, a6);
}
uint32_t Ui_DrawRing_Abi(uint32_t eax, uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                         uint32_t a4, uint32_t a5)
{
    return Ui_DrawRing(P(eax), P<const float>(esi), P<const float>(edi), P<const float>(a0), u2f(a1), a2, a3, a4, a5);
}
uint32_t Ui_DrawTriangle_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                             uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8)
{
    return Ui_DrawTriangle(P(eax), ecx, a0, a1, a2, a3, a4, a5, a6, a7, a8);
}
uint32_t Ui_DrawLine_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return Ui_DrawLine(P(eax), ecx, a0, a1, a2, a3, a4);
}
uint32_t ParticlePool_ResetIndexBuffer_Abi(uint32_t ebx, uint32_t a0)
{
    return ParticlePool_ResetIndexBuffer(P(ebx), a0);
}
uint32_t ParticlePool_Destroy_Abi(uint32_t eax)
{
    return ParticlePool_Destroy(P(eax));
}
void ParticlePool_SetTexture_Abi(uint32_t eax, uint32_t ecx)
{
    ParticlePool_SetTexture(eax, P(ecx));
}
uint32_t Particle_Alloc_Abi(uint32_t edx)
{
    return Particle_Alloc(P(edx));
}
uint32_t Particle_AllocZeroed_Abi(uint32_t ecx)
{
    return Particle_AllocZeroed(P(ecx));
}
uint32_t Particle_Release_Abi(uint32_t esi, uint32_t a0)
{
    return Particle_Release(P(esi), P(a0));
}
// symbols/abi.csv lists 9 stack arguments; the only caller pushes 3 (caller pops), the rest are not read.
uint32_t Particles_Draw_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t /*a3*/, uint32_t /*a4*/,
                            uint32_t /*a5*/, uint32_t /*a6*/, uint32_t /*a7*/, uint32_t /*a8*/)
{
    return Particles_Draw(P<const float>(eax), P(a0), u2f(a1), a2);
}
uint32_t Particle_RandomConeVelocity_Abi(uint32_t ecx, uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1,
                                         uint32_t a2)
{
    return Particle_RandomConeVelocity(ecx, P<const float>(esi), P<float>(edi), a0, a1, a2);
}
uint32_t ParticleEmitter_SpawnInBox_Abi(uint32_t ebx, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                        uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    return ParticleEmitter_SpawnInBox(P<const float>(ebx), P<const float>(edi), P(a0), P<const float>(a1),
                                      P<const float>(a2), P<float>(a3), u2f(a4), u2f(a5), a6, a7);
}
uint32_t ParticleEmitter_SpawnAlong_Abi(uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1,
                                        uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    return ParticleEmitter_SpawnAlong(P<const float>(ebx), P<const float>(esi), P<const float>(edi), P(a0),
                                      P<const float>(a1), P<float>(a2), u2f(a3), u2f(a4), a5, a6);
}
uint32_t Post_SplitLevel_Abi(uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Post_SplitLevel(P<int32_t>(esi), u2f(a0), static_cast<int32_t>(a1), P<float>(a2));
}
uint32_t PostEffects_Construct_Abi(uint32_t edx)
{
    return PostEffects_Construct(P(edx));
}
uint32_t PostEffects_Destruct_Abi(uint32_t eax)
{
    return PostEffects_Destruct(P(eax));
}
uint32_t PostEffects_Clear_Abi(uint32_t esi)
{
    return PostEffects_Clear(P(esi));
}
uint32_t PostEffects_AddFade_Abi(uint32_t ecx, uint32_t esi, uint32_t a0)
{
    return PostEffects_AddFade(P(ecx), P<const uint32_t>(esi), a0);
}
uint32_t PostEffects_AddColour_Abi(uint32_t ecx, uint32_t esi, uint32_t a0)
{
    return PostEffects_AddColour(P(ecx), P<const uint32_t>(esi), a0);
}
uint32_t PostEffects_AddBlur_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return PostEffects_AddBlur(P(ecx), a0, P<const uint32_t>(a1));
}
uint32_t PostEffects_AddType3_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return PostEffects_AddType3(P(ecx), a0, P<const uint32_t>(a1));
}
uint32_t PostEffects_AddBlurRings_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return PostEffects_AddBlurRings(P(ecx), a0, P<const uint32_t>(a1));
}
uint32_t PostEffects_AddType4_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return PostEffects_AddType4(P(ecx), a0, P<const uint32_t>(a1));
}
uint32_t PostEffects_Stop_Abi(uint32_t edi, uint32_t a0, uint32_t a1)
{
    return PostEffects_Stop(P(edi), a0, a1);
}
void PostEffect_MoveToList_Abi(uint32_t eax, uint32_t edx, uint32_t /*a0*/)
{
    PostEffect_MoveToList(P<uint32_t>(eax), P<uint32_t>(edx));
}
// symbols/abi.csv lists 3 caller-popped stack arguments; none is read.
uint32_t PostEffects_SetPosition_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t esi, uint32_t /*a0*/,
                                     uint32_t /*a1*/, uint32_t /*a2*/)
{
    return PostEffects_SetPosition(P(eax), ecx, P<const uint32_t>(edx), esi);
}
uint32_t PostEffects_SetDistanceFade_Abi(uint32_t eax, uint32_t ebx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return PostEffects_SetDistanceFade(P(eax), ebx, a0, u2f(a1), u2f(a2));
}
uint32_t PostEffects_SetEdgeFade_Abi(uint32_t eax, uint32_t edx, uint32_t edi, uint32_t a0, uint32_t a1)
{
    return PostEffects_SetEdgeFade(P(eax), edx, edi, u2f(a0), u2f(a1));
}
uint32_t PostEffects_SetTimes_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return PostEffects_SetTimes(P(eax), ecx, edx, a0, a1, a2);
}
uint32_t PostEffects_SetIgnorePause_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t esi)
{
    return PostEffects_SetIgnorePause(P(eax), ecx, edx, esi);
}
uint32_t PostEffect_Apply_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return PostEffect_Apply(P(eax), P(ecx), u2f(a0));
}
uint32_t PostProcess_Pass_Abi(uint32_t esi, uint32_t a0, uint32_t a1)
{
    return PostProcess_Pass(P(esi), P(a0), u2f(a1));
}
uint32_t PostTargets_Init_Abi(uint32_t edx)
{
    return PostTargets_Init(P(edx));
}
uint32_t Post_GetQuadMapping_Abi(uint32_t ebx, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Post_GetQuadMapping(P<float>(ebx), edi, a0, a1, P<float>(a2), P<float>(a3));
}
uint32_t Post_DrawRing_Abi(uint32_t eax, uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return Post_DrawRing(P(eax), P<const float>(esi), a0, P<const float>(a1), a2, a3, static_cast<int32_t>(a4));
}
uint32_t Post_DrawRingPair_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t edi, uint32_t a0)
{
    return Post_DrawRingPair(eax, P<const float>(ecx), edx, P(edi), P<const float>(a0));
}
uint32_t Post_DrawScreenQuad_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Post_DrawScreenQuad(P<const float>(eax), ecx, a0, P(a1), P<const float>(a2), P<const uint32_t>(a3));
}
uint32_t Post_BlitChain_Abi(uint32_t eax, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Post_BlitChain(P(eax), edx, P(a0), static_cast<int32_t>(a1), P(a2), P<const uint32_t>(a3));
}
uint32_t Post_DrawFade_Abi(uint32_t ebx, uint32_t a0, uint32_t a1)
{
    return Post_DrawFade(P(ebx), a0, u2f(a1));
}
uint32_t Post_DrawBlur_Abi(uint32_t eax, uint32_t /*ecx*/, uint32_t a0)
{
    return Post_DrawBlur(P(eax), u2f(a0));
}
uint32_t Post_DrawBlurRings_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return Post_DrawBlurRings(P<const float>(eax), P(ecx), P<const float>(a0));
}
uint32_t Post_DrawType3_Abi(uint32_t eax, uint32_t a0)
{
    return Post_DrawType3(P(eax), u2f(a0));
}
uint32_t Post_DrawType3Rings_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return Post_DrawType3Rings(P(eax), P<const float>(a0), P<const float>(a1));
}
uint32_t Post_DrawColour_Abi(uint32_t eax, uint32_t /*ecx*/, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Post_DrawColour(P(eax), u2f(a0), u2f(a1), u2f(a2), u2f(a3));
}
void Post_ClearE0_Abi(uint32_t eax)
{
    Post_ClearE0(P(eax));
}
}  // namespace

ABI_ENTRY_00423360(Fx_Step_Entry, Fx_Step_Abi)
ABI_ENTRY_00423430(FxStack_Push_Entry, FxStack_Push_Abi)
ABI_ENTRY_00423470(FxStack_Pop_Entry, FxStack_Pop_Abi)
ABI_ENTRY_004237b0(Fx_UpdateAll_Entry, Fx_UpdateAll_Abi)
ABI_ENTRY_00423820(Fx_DrawParticles_Entry, Fx_DrawParticles_Abi)
ABI_ENTRY_0042eb60(Character_SetFade_Entry, Character_SetFade_Abi)
ABI_ENTRY_00464750(UiText_Draw_Entry, UiText_Draw_Abi)
ABI_ENTRY_00464e20(UiMesh_Draw_Entry, UiMesh_Draw_Abi)
ABI_ENTRY_00465360(UiText_Render_Entry, UiText_Render_Abi)
ABI_ENTRY_00465430(UiText_DrawBackground_Entry, UiText_DrawBackground_Abi)
ABI_ENTRY_0046b260(Ui_DrawLineInt_Entry, Ui_DrawLineInt_Abi)
ABI_ENTRY_0046b2c0(Ui_DrawRect_Entry, Ui_DrawRect_Abi)
ABI_ENTRY_0046b570(Ui_DrawRing_Entry, Ui_DrawRing_Abi)
ABI_ENTRY_0046b960(Ui_DrawTriangle_Entry, Ui_DrawTriangle_Abi)
ABI_ENTRY_0046baf0(Ui_DrawLine_Entry, Ui_DrawLine_Abi)
ABI_ENTRY_0046d810(ParticlePool_ResetIndexBuffer_Entry, ParticlePool_ResetIndexBuffer_Abi)
ABI_ENTRY_0046d8c0(ParticlePool_Destroy_Entry, ParticlePool_Destroy_Abi)
ABI_ENTRY_0046d900(ParticlePool_SetTexture_Entry, ParticlePool_SetTexture_Abi)
ABI_ENTRY_0046d910(Particle_Alloc_Entry, Particle_Alloc_Abi)
ABI_ENTRY_0046d950(Particle_AllocZeroed_Entry, Particle_AllocZeroed_Abi)
ABI_ENTRY_0046d9a0(Particle_Release_Entry, Particle_Release_Abi)
ABI_ENTRY_0046d9f0(Particles_Draw_Entry, Particles_Draw_Abi)
ABI_ENTRY_0046e320(Particle_RandomConeVelocity_Entry, Particle_RandomConeVelocity_Abi)
ABI_ENTRY_0046e4c0(ParticleEmitter_SpawnInBox_Entry, ParticleEmitter_SpawnInBox_Abi)
ABI_ENTRY_0046e650(ParticleEmitter_SpawnAlong_Entry, ParticleEmitter_SpawnAlong_Abi)
ABI_ENTRY_0047bca0(Post_SplitLevel_Entry, Post_SplitLevel_Abi)
ABI_ENTRY_0047bd10(PostEffects_Construct_Entry, PostEffects_Construct_Abi)
ABI_ENTRY_0047bd70(PostEffects_Destruct_Entry, PostEffects_Destruct_Abi)
ABI_ENTRY_0047bd80(PostEffects_Clear_Entry, PostEffects_Clear_Abi)
ABI_ENTRY_0047bda0(PostEffects_AddFade_Entry, PostEffects_AddFade_Abi)
ABI_ENTRY_0047be00(PostEffects_AddColour_Entry, PostEffects_AddColour_Abi)
ABI_ENTRY_0047be60(PostEffects_AddBlur_Entry, PostEffects_AddBlur_Abi)
ABI_ENTRY_0047bec0(PostEffects_AddType3_Entry, PostEffects_AddType3_Abi)
ABI_ENTRY_0047bf20(PostEffects_AddBlurRings_Entry, PostEffects_AddBlurRings_Abi)
ABI_ENTRY_0047bf80(PostEffects_AddType4_Entry, PostEffects_AddType4_Abi)
ABI_ENTRY_0047bfe0(PostEffects_Stop_Entry, PostEffects_Stop_Abi)
ABI_ENTRY_0047c030(PostEffect_MoveToList_Entry, PostEffect_MoveToList_Abi)
ABI_ENTRY_0047c060(PostEffects_SetPosition_Entry, PostEffects_SetPosition_Abi)
ABI_ENTRY_0047c0a0(PostEffects_SetDistanceFade_Entry, PostEffects_SetDistanceFade_Abi)
ABI_ENTRY_0047c140(PostEffects_SetEdgeFade_Entry, PostEffects_SetEdgeFade_Abi)
ABI_ENTRY_0047c1a0(PostEffects_SetTimes_Entry, PostEffects_SetTimes_Abi)
ABI_ENTRY_0047c1d0(PostEffects_SetIgnorePause_Entry, PostEffects_SetIgnorePause_Abi)
ABI_ENTRY_0047c200(PostEffect_Apply_Entry, PostEffect_Apply_Abi)
ABI_ENTRY_0047c600(PostProcess_Pass_Entry, PostProcess_Pass_Abi)
ABI_ENTRY_0047c850(PostTargets_Init_Entry, PostTargets_Init_Abi)
ABI_ENTRY_0047caf0(Post_GetQuadMapping_Entry, Post_GetQuadMapping_Abi)
ABI_ENTRY_0047cbe0(Post_DrawRing_Entry, Post_DrawRing_Abi)
ABI_ENTRY_0047cd50(Post_DrawRingPair_Entry, Post_DrawRingPair_Abi)
ABI_ENTRY_0047cdd0(Post_DrawScreenQuad_Entry, Post_DrawScreenQuad_Abi)
ABI_ENTRY_0047cf10(Post_BlitChain_Entry, Post_BlitChain_Abi)
ABI_ENTRY_0047cff0(Post_DrawFade_Entry, Post_DrawFade_Abi)
ABI_ENTRY_0047d0f0(Post_DrawBlur_Entry, Post_DrawBlur_Abi)
ABI_ENTRY_0047d230(Post_DrawBlurRings_Entry, Post_DrawBlurRings_Abi)
ABI_ENTRY_0047d5c0(Post_DrawType3_Entry, Post_DrawType3_Abi)
ABI_ENTRY_0047d6d0(Post_DrawType3Rings_Entry, Post_DrawType3Rings_Abi)
ABI_ENTRY_0047d8a0(Post_DrawColour_Entry, Post_DrawColour_Abi)
ABI_ENTRY_0047dbd0(Post_ClearE0_Entry, Post_ClearE0_Abi)

HOOK(0x00423360, "Fx_Step", Fx_Step_Entry, nullptr);
HOOK(0x00423430, "FxStack_Push", FxStack_Push_Entry, nullptr);
HOOK(0x00423470, "FxStack_Pop", FxStack_Pop_Entry, nullptr);
HOOK(0x004237b0, "Fx_UpdateAll", Fx_UpdateAll_Entry, nullptr);
HOOK(0x00423820, "Fx_DrawParticles", Fx_DrawParticles_Entry, nullptr);
HOOK(0x0042eb60, "Character_SetFade", Character_SetFade_Entry, nullptr);
HOOK(0x00464750, "UiText_Draw", UiText_Draw_Entry, nullptr);
HOOK(0x00464e20, "UiMesh_Draw", UiMesh_Draw_Entry, nullptr);
HOOK(0x00465360, "UiText_Render", UiText_Render_Entry, nullptr);
HOOK(0x00465430, "UiText_DrawBackground", UiText_DrawBackground_Entry, nullptr);
HOOK(0x0046b260, "Ui_DrawLineInt", Ui_DrawLineInt_Entry, nullptr);
HOOK(0x0046b2c0, "Ui_DrawRect", Ui_DrawRect_Entry, nullptr);
HOOK(0x0046b570, "Ui_DrawRing", Ui_DrawRing_Entry, nullptr);
HOOK(0x0046b960, "Ui_DrawTriangle", Ui_DrawTriangle_Entry, nullptr);
HOOK(0x0046baf0, "Ui_DrawLine", Ui_DrawLine_Entry, nullptr);
HOOK(0x0046d810, "ParticlePool_ResetIndexBuffer", ParticlePool_ResetIndexBuffer_Entry, nullptr);
HOOK(0x0046d8c0, "ParticlePool_Destroy", ParticlePool_Destroy_Entry, nullptr);
HOOK(0x0046d900, "ParticlePool_SetTexture", ParticlePool_SetTexture_Entry, nullptr);
HOOK(0x0046d910, "Particle_Alloc", Particle_Alloc_Entry, nullptr);
HOOK(0x0046d950, "Particle_AllocZeroed", Particle_AllocZeroed_Entry, nullptr);
HOOK(0x0046d9a0, "Particle_Release", Particle_Release_Entry, nullptr);
HOOK(0x0046d9f0, "Particles_Draw", Particles_Draw_Entry, nullptr);
HOOK(0x0046e320, "Particle_RandomConeVelocity", Particle_RandomConeVelocity_Entry, nullptr);
HOOK(0x0046e4c0, "ParticleEmitter_SpawnInBox", ParticleEmitter_SpawnInBox_Entry, nullptr);
HOOK(0x0046e650, "ParticleEmitter_SpawnAlong", ParticleEmitter_SpawnAlong_Entry, nullptr);
HOOK(0x0047bca0, "Post_SplitLevel", Post_SplitLevel_Entry, nullptr);
HOOK(0x0047bd10, "PostEffects_Construct", PostEffects_Construct_Entry, nullptr);
HOOK(0x0047bd70, "PostEffects_Destruct", PostEffects_Destruct_Entry, nullptr);
HOOK(0x0047bd80, "PostEffects_Clear", PostEffects_Clear_Entry, nullptr);
HOOK(0x0047bda0, "PostEffects_AddFade", PostEffects_AddFade_Entry, nullptr);
HOOK(0x0047be00, "PostEffects_AddColour", PostEffects_AddColour_Entry, nullptr);
HOOK(0x0047be60, "PostEffects_AddBlur", PostEffects_AddBlur_Entry, nullptr);
HOOK(0x0047bec0, "PostEffects_AddType3", PostEffects_AddType3_Entry, nullptr);
HOOK(0x0047bf20, "PostEffects_AddBlurRings", PostEffects_AddBlurRings_Entry, nullptr);
HOOK(0x0047bf80, "PostEffects_AddType4", PostEffects_AddType4_Entry, nullptr);
HOOK(0x0047bfe0, "PostEffects_Stop", PostEffects_Stop_Entry, nullptr);
HOOK(0x0047c030, "PostEffect_MoveToList", PostEffect_MoveToList_Entry, nullptr);
HOOK(0x0047c060, "PostEffects_SetPosition", PostEffects_SetPosition_Entry, nullptr);
HOOK(0x0047c0a0, "PostEffects_SetDistanceFade", PostEffects_SetDistanceFade_Entry, nullptr);
HOOK(0x0047c140, "PostEffects_SetEdgeFade", PostEffects_SetEdgeFade_Entry, nullptr);
HOOK(0x0047c1a0, "PostEffects_SetTimes", PostEffects_SetTimes_Entry, nullptr);
HOOK(0x0047c1d0, "PostEffects_SetIgnorePause", PostEffects_SetIgnorePause_Entry, nullptr);
HOOK(0x0047c200, "PostEffect_Apply", PostEffect_Apply_Entry, nullptr);
HOOK(0x0047c600, "PostProcess_Pass", PostProcess_Pass_Entry, nullptr);
HOOK(0x0047c850, "PostTargets_Init", PostTargets_Init_Entry, nullptr);
HOOK(0x0047caf0, "Post_GetQuadMapping", Post_GetQuadMapping_Entry, nullptr);
HOOK(0x0047cbe0, "Post_DrawRing", Post_DrawRing_Entry, nullptr);
HOOK(0x0047cd50, "Post_DrawRingPair", Post_DrawRingPair_Entry, nullptr);
HOOK(0x0047cdd0, "Post_DrawScreenQuad", Post_DrawScreenQuad_Entry, nullptr);
HOOK(0x0047cf10, "Post_BlitChain", Post_BlitChain_Entry, nullptr);
HOOK(0x0047cff0, "Post_DrawFade", Post_DrawFade_Entry, nullptr);
HOOK(0x0047d0f0, "Post_DrawBlur", Post_DrawBlur_Entry, nullptr);
HOOK(0x0047d230, "Post_DrawBlurRings", Post_DrawBlurRings_Entry, nullptr);
HOOK(0x0047d5c0, "Post_DrawType3", Post_DrawType3_Entry, nullptr);
HOOK(0x0047d6d0, "Post_DrawType3Rings", Post_DrawType3Rings_Entry, nullptr);
HOOK(0x0047d8a0, "Post_DrawColour", Post_DrawColour_Entry, nullptr);
HOOK(0x0047dbd0, "Post_ClearE0", Post_ClearE0_Entry, nullptr);
