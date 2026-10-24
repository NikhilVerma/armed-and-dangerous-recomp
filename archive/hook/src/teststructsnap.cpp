// Struct snapshots of the running original for the struct atlas (tools/struct_atlas.py, docs/structs/README.md): every
// N play frames, the raw memory of the live game objects, so that offsets the recovered C++ uses can be typed and their
// meaning tested (timers count down by the frame time, positions match the object's position, ...).
//
// [test] struct_snap=N            every N play frames from the oracle trace's first frame (testoracle.cpp; 0: off)
// [test] struct_snap_per_class=K  at most K objects per vtable (default 4; the first seen are kept while they live)
// [test] struct_snap_max=BYTES    bytes per object at most (default 0x1000; else the class size from its descriptor)
// [test] struct_snap_classes=A,B  only these classes (descriptor names, e.g. GenericAIObject; default: every class)
// [test] struct_snap_ptr_every=M  every M snapshots, the first dword of what each pointer-like dword points at (the
//                                 target's vtable: tells which class a pointer field refers to; default 8)
// Output and-structsnap.bin in the game folder (little endian):
//   "SNP1"
//   frame:  "FRM1", int32 play frame, float game clock, float frame dt (0x00604a90), uint32 objects
//   object: "OBJ1", uint32 address, uint32 vtable, uint16 level id (+0x396), uint16 0, float pos[3] (vt+0x54),
//           uint32 bytes, bytes
//   ptrs:   "PTR1", uint32 object address, uint32 n, n x {uint32 offset, uint32 value, uint32 target's first dword}
// The objects are World (Game+0xa84) list +0xe70 (every object: the trigger listeners, mission.md) plus the player;
// the Game singleton (0x4c40 bytes, "vtable" 1) and the World (0x1000 bytes, "vtable" 2) come first in each frame.
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#include "config.h"
#include "game/game_object.h"
#include "gen/orig_calls.h"
#include "log.h"

namespace
{
int g_every, g_perClass = 4, g_ptrEvery = 8;
unsigned g_max = 0x1000;
FILE* g_snap;
int g_snaps;
char g_classes[1024];

struct Chosen
{
    uint32_t vtable;
    uint32_t objects[16];
};
Chosen g_chosen[512];
int g_nChosen;

struct ClassInfo
{
    uint32_t vtable;
    uint32_t size;  // 0: excluded
};
ClassInfo g_info[512];
int g_nInfo;

bool Mapped(uint32_t p, unsigned n)
{
    MEMORY_BASIC_INFORMATION m;
    if (p < 0x10000 || p >= 0x7fff0000 || !VirtualQuery(reinterpret_cast<const void*>(p), &m, sizeof(m)) ||
        m.State != MEM_COMMIT)
        return false;
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                           PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (m.Protect & readable) && !(m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
           p + n <= reinterpret_cast<uint32_t>(m.BaseAddress) + m.RegionSize;
}

// The class of a vtable: slot 1 is `mov eax, descriptor; ret` (B8 imm32 C3); descriptor +8 size, +0x14 "CLASS(Name)"
uint32_t ClassSize(uint32_t vtable)
{
    for (int i = 0; i < g_nInfo; ++i)
        if (g_info[i].vtable == vtable)
            return g_info[i].size;
    uint32_t size = 0x460;  // the Object base when the class has no descriptor
    const char* name = "";
    const uint32_t slot1 = Mapped(vtable + 4, 4) ? *reinterpret_cast<const uint32_t*>(vtable + 4) : 0;
    if (slot1 >= 0x00401000 && slot1 < 0x0056b000 && *reinterpret_cast<const uint8_t*>(slot1) == 0xb8 &&
        *reinterpret_cast<const uint8_t*>(slot1 + 5) == 0xc3)
    {
        const uint32_t desc = *reinterpret_cast<const uint32_t*>(slot1 + 1);
        if (Mapped(desc, 0x18))
        {
            size = *reinterpret_cast<const uint32_t*>(desc + 8);
            const uint32_t s = *reinterpret_cast<const uint32_t*>(desc + 0x14);
            if (Mapped(s, 8))
                name = reinterpret_cast<const char*>(s);
        }
    }
    if (size > g_max)
        size = g_max;
    if (size < 0x10)
        size = 0x10;
    if (g_classes[0])
    {
        // "CLASS(Name)" / "CLASSV(Name)": the name between the parentheses must be listed
        const char* open = strchr(name, '(');
        bool listed = false;
        if (open)
        {
            const char* n = open + 1;
            const size_t len = strcspn(n, ")");
            for (const char* p = g_classes; *p;)
            {
                const size_t l = strcspn(p, ",");
                if (l == len && !strncmp(p, n, l))
                    listed = true;
                p += l + (p[l] == ',');
            }
        }
        if (!listed)
            size = 0;
    }
    if (g_nInfo < 512)
        g_info[g_nInfo++] = {vtable, size};
    return size;
}

// Kept while alive: the first K objects seen per vtable; a dead one's slot goes to the next new object.
bool Take(uint32_t vtable, uint32_t obj, const uint32_t* live, int nLive)
{
    Chosen* c = nullptr;
    for (int i = 0; i < g_nChosen && !c; ++i)
        if (g_chosen[i].vtable == vtable)
            c = &g_chosen[i];
    if (!c)
    {
        if (g_nChosen >= 512)
            return false;
        c = &g_chosen[g_nChosen++];
        memset(c, 0, sizeof(*c));
        c->vtable = vtable;
    }
    const int k = g_perClass < 16 ? g_perClass : 16;
    for (int i = 0; i < k; ++i)
        if (c->objects[i] == obj)
            return true;
    for (int i = 0; i < k; ++i)
    {
        bool alive = false;
        for (int j = 0; j < nLive && c->objects[i] && !alive; ++j)
            alive = live[j] == c->objects[i];
        if (!alive)
        {
            c->objects[i] = obj;
            return true;
        }
    }
    return false;
}

void Put(const void* p, size_t n)
{
    fwrite(p, 1, n, g_snap);
}

uint8_t g_buf[0x1000 + 64 + 12 * 1024 + 16];
size_t g_len;

void Add(const void* p, size_t n)
{
    memcpy(g_buf + g_len, p, n);
    g_len += n;
}

// One object into g_buf, inside SEH (a dying object's virtual can fault): position, the bytes, the pointer targets.
// Returns false (nothing usable in g_buf) on a fault.
bool SnapOne(uint32_t obj, uint32_t vtable, uint32_t size, bool ptrs)
{
    g_len = 0;
    __try
    {
        // the position (vt+0x54); Body_getPosition 0x00457930 reads the Havok entity +0x1c4 without a null test: an
        // object without a body (dying projectiles) gets NaN instead of a fault (the crash logger dumps each one)
        const float nan = std::numeric_limits<float>::quiet_NaN();
        float pos[4] = {nan, nan, nan, 0};
        void** vt = *reinterpret_cast<void***>(obj);
        if (vt[0x54 / 4] != reinterpret_cast<void*>(0x00457930) || *reinterpret_cast<const uint32_t*>(obj + 0x1c4))
        {
            using Fn = void(__thiscall*)(void*, float*);
            reinterpret_cast<Fn>(vt[0x54 / 4])(reinterpret_cast<void*>(obj), pos);
        }
        const uint32_t tag = 0x314a424f;  // "OBJ1"
        const uint16_t id = *reinterpret_cast<const uint16_t*>(obj + 0x396), zero = 0;
        Add(&tag, 4), Add(&obj, 4), Add(&vtable, 4), Add(&id, 2), Add(&zero, 2), Add(pos, 12), Add(&size, 4);
        Add(reinterpret_cast<const void*>(obj), size);
        if (ptrs)
        {
            const uint32_t ptag = 0x31525450;  // "PTR1"
            const size_t at = g_len;
            uint32_t n = 0;
            Add(&ptag, 4), Add(&obj, 4), Add(&n, 4);
            for (uint32_t off = 4; off + 4 <= size && n < 1024; off += 4)
            {
                const uint32_t v = *reinterpret_cast<const uint32_t*>(obj + off);
                if (v < 0x00010000 || (v >= 0x00400000 && v < 0x0064d000) || (v & 3) || !Mapped(v, 4))
                    continue;
                const uint32_t rec[3] = {off, v, *reinterpret_cast<const uint32_t*>(v)};
                Add(rec, 12);
                ++n;
            }
            memcpy(g_buf + at + 8, &n, 4);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
}  // namespace

void TestStructSnapInit()
{
    g_every = ConfigInt("test", "struct_snap", 0);
    if (!g_every)
        return;
    g_perClass = ConfigInt("test", "struct_snap_per_class", 4);
    g_max = static_cast<unsigned>(ConfigInt("test", "struct_snap_max", 0x1000));
    g_ptrEvery = ConfigInt("test", "struct_snap_ptr_every", 8);
    ConfigString("test", "struct_snap_classes", "", g_classes, sizeof(g_classes));
    Log("test: struct snapshots every %d play frames, %d per class, max 0x%x bytes, classes '%s'", g_every, g_perClass,
        g_max, g_classes);
}

// Called by TestOracleOnFrame on each traced play frame (f: the play frame, clock: the game clock).
void TestStructSnapFrame(int f, float clock)
{
    if (!g_every || !g_game || (f % g_every) != 0)
        return;
    const uint32_t game = reinterpret_cast<uint32_t>(static_cast<void*>(g_game));
    const uint32_t world = *reinterpret_cast<const uint32_t*>(game + 0xa84);
    if (!world)
        return;
    if (!g_snap)
    {
        g_snap = fopen("and-structsnap.bin", "wb");
        if (!g_snap)
            return;
        Put("SNP1", 4);
    }
    static uint32_t live[4096];
    int nLive = 0;
    uint32_t iter[2] = {0, 0};
    for (uint32_t a = orig::fn_004e7340(*reinterpret_cast<const uint32_t*>(world + 0xe70), reinterpret_cast<uint32_t>(iter));
         a && nLive < 4095; a = orig::fn_004e73c0(reinterpret_cast<uint32_t>(iter)))
        live[nLive++] = a;
    const uint32_t player = *reinterpret_cast<const uint32_t*>(game + 0x748);
    bool hasPlayer = false;
    for (int i = 0; i < nLive && !hasPlayer; ++i)
        hasPlayer = live[i] == player;
    if (player && !hasPlayer)
        live[nLive++] = player;
    static uint32_t picked[4096], sizes[4096], vts[4096];
    uint32_t n = 0;
    for (int i = 0; i < nLive; ++i)
    {
        const uint32_t obj = live[i];
        if (!Mapped(obj, 0x400))
            continue;
        const uint32_t vt = *reinterpret_cast<const uint32_t*>(obj);
        if (vt < 0x0056b000 || vt >= 0x005bf000)
            continue;
        const uint32_t size = ClassSize(vt);
        if (!size || !Mapped(obj, size) || !Take(vt, obj, live, nLive))
            continue;
        picked[n] = obj, sizes[n] = size, vts[n] = vt, ++n;
    }
    // the Game singleton (0x005fe490) and the World (Game+0xa84) as pseudo objects: "vtable" 1 / 2, no position
    const uint32_t singles[2][3] = {{game, 1, 0x4c40}, {world, 2, 0x1000}};
    uint32_t nSingles = 0;
    for (const auto& s : singles)
        nSingles += Mapped(s[0], s[2]) ? 1 : 0;
    const uint32_t total = n + nSingles;
    const uint32_t tag = 0x314d5246;  // "FRM1"
    const float dt = *reinterpret_cast<const float*>(0x00604a90);
    Put(&tag, 4), Put(&f, 4), Put(&clock, 4), Put(&dt, 4), Put(&total, 4);
    for (const auto& s : singles)
        if (Mapped(s[0], s[2]))
        {
            const uint32_t otag = 0x314a424f, z = 0;
            const float zp[3] = {};
            Put(&otag, 4), Put(&s[0], 4), Put(&s[1], 4), Put(&z, 4), Put(zp, 12), Put(&s[2], 4);
            Put(reinterpret_cast<const void*>(s[0]), s[2]);
        }
    const bool ptrs = g_ptrEvery > 0 && (g_snaps % g_ptrEvery) == 0;
    for (uint32_t i = 0; i < n; ++i)
        if (SnapOne(picked[i], vts[i], sizes[i], ptrs))
            Put(g_buf, g_len);
        else
        {
            // keep the record count right: an empty object record
            const uint32_t otag = 0x314a424f, z = 0;
            const float zp[3] = {};
            Put(&otag, 4), Put(&picked[i], 4), Put(&vts[i], 4), Put(&z, 4), Put(zp, 12), Put(&z, 4);
        }
    fflush(g_snap);
    ++g_snaps;
}
