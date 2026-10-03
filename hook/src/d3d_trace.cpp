// Direct3D 9 call-stream trace for comparison tests: the renderer's observable output.
//
// [test] d3d_trace=1   every IDirect3DDevice9 method call is folded into a per-frame hash (method, value
//                      arguments, contents of matrices/viewports/lights/materials/shader constants/user-pointer
//                      vertex data, resources as stable ids in order of first appearance). One line per frame is
//                      written to and-d3d.txt: "frame calls hash".
//
// Implementation: the game's device object gets its own copy of the vtable whose entries are thunks
// (push index; jmp common). The common thunk records the call and returns into the real method with the
// stack untouched.
#include <windows.h>
#include <d3d9.h>

#include <cstdio>
#include <cstring>
#include <unordered_map>

#include "config.h"
#include "log.h"

namespace
{
// Argument kinds per IDirect3DDevice9 method, in vtable order (d3d9.h). One char per stack argument after `this`:
//   v value   r resource (stable id)   o output pointer (ignored)   M matrix*   V viewport*   A material*
//   L light*   4 float4*   G gamma ramp*   R rect* (nullable)   F/I/B shader constants (start, data, count)
//   U DrawPrimitiveUP data   X DrawIndexedPrimitiveUP data   - query method (counted, args ignored)
const char* const kMethods[119] = {
    "--", "", "", "", "", "", "-", "-", "--", "-",                                    // 0-9
    "vvr", "vvv", "v", "-o", "-o", "", "o", "oovo", "vvvo", "vo",                      // 10-19
    "v", "vvG", "vo", "vvvvvvoo", "vvvvvvvoo", "vvvvvoo", "vvvvoo", "vvvvoo",            // 20-27
    "vvvvvvoo", "vvvvvvoo", "rRro", "rr", "rr", "vr", "rRrRv", "rRv", "vvvvoo", "vr",  // 28-37
    "vo", "r", "o", "", "", "vRvvvv", "vM", "vo", "vM", "V",                           // 38-47
    "o", "A", "o", "vL", "vo", "vv", "vo", "v4", "vo", "vv",                           // 48-57
    "vo", "vo", "", "o", "o", "o", "vo", "vr", "vvo", "vvv",                           // 58-67
    "vvo", "vvv", "o", "vo", "vo", "v", "o", "R", "o", "v",                            // 68-77
    "", "v", "", "vvv", "vvvvvv", "U", "X", "vvvrrv", "oo", "r",                        // 78-87
    "o", "v", "o", "oo", "r", "o", "F", "-", "I", "-",                                  // 88-97
    "B", "-", "vrvv", "vooo", "vv", "vo", "r", "o", "oo", "r",                          // 98-107
    "o", "F", "-", "I", "-", "B", "-", "-", "-", "v",                              // 108-117
    "vo",                                                                               // 118
};

void** g_realVtbl;
// The runtime makes internal virtual calls beyond the 119 public methods, so the copy keeps a long tail of the
// real vtable (entries past 119 are copied unchanged).
constexpr int kVtblCopy = 1024;
void* g_ourVtbl[kVtblCopy];
FILE* g_out;
unsigned long long g_hash;
unsigned g_calls, g_frame;
std::unordered_map<const void*, unsigned>* g_ids;
CRITICAL_SECTION g_lock;

inline void Mix(unsigned long long v)
{
    for (int i = 0; i < 8; i++)
    {
        g_hash = (g_hash ^ (v & 0xff)) * 1099511628211ull;
        v >>= 8;
    }
}

void MixBytes(const void* p, size_t n)
{
    auto b = static_cast<const unsigned char*>(p);
    for (size_t i = 0; i < n; i++)
        g_hash = (g_hash ^ b[i]) * 1099511628211ull;
}

unsigned IdOf(const void* p)
{
    if (!p)
        return 0;
    auto it = g_ids->find(p);
    if (it != g_ids->end())
        return it->second;
    unsigned id = static_cast<unsigned>(g_ids->size()) + 1;
    (*g_ids)[p] = id;
    return id;
}

unsigned VerticesFor(DWORD type, DWORD prims)
{
    switch (type)
    {
    case D3DPT_POINTLIST: return prims;
    case D3DPT_LINELIST: return prims * 2;
    case D3DPT_LINESTRIP: return prims + 1;
    case D3DPT_TRIANGLELIST: return prims * 3;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN: return prims + 2;
    default: return 0;
    }
}

// Called from the common thunk with the method index and a pointer to the first argument after `this`.
// Returns the real method's address.
int g_suppress;  // >0 while the test harness itself calls the device

extern "C" void* __cdecl TraceCall(unsigned index, const DWORD* args)
{
    if (g_suppress)
        return g_realVtbl[index];
    EnterCriticalSection(&g_lock);
    const char* kinds = kMethods[index];
    g_calls++;
    Mix(index);
    if (kinds[0] != '-')
    {
        for (int k = 0; kinds[k]; k++)
        {
            DWORD a = args[k];
            const void* p = reinterpret_cast<const void*>(a);
            switch (kinds[k])
            {
            case 'v': Mix(a); break;
            case 'r': Mix(IdOf(p)); break;
            case 'M': if (p) MixBytes(p, 64); break;
            case 'V': if (p) MixBytes(p, sizeof(D3DVIEWPORT9)); break;
            case 'A': if (p) MixBytes(p, sizeof(D3DMATERIAL9)); break;
            case 'L': if (p) MixBytes(p, sizeof(D3DLIGHT9)); break;
            case '4': if (p) MixBytes(p, 16); break;
            case 'G': if (p) MixBytes(p, sizeof(D3DGAMMARAMP)); break;
            case 'R': Mix(p != nullptr); if (p) MixBytes(p, sizeof(RECT)); break;
            case 'F': case 'I': case 'B':
            {
                // (start, data, count)
                Mix(args[0]);
                Mix(args[2]);
                if (args[1])
                    MixBytes(reinterpret_cast<const void*>(args[1]), args[2] * (kinds[k] == 'B' ? 4 : 16));
                k = 2;
                break;
            }
            case 'U':  // DrawPrimitiveUP(type, count, data, stride)
                Mix(args[0]); Mix(args[1]); Mix(args[3]);
                if (args[2])
                    MixBytes(reinterpret_cast<const void*>(args[2]), VerticesFor(args[0], args[1]) * args[3]);
                k = 3;
                break;
            case 'X':  // DrawIndexedPrimitiveUP(type, minIndex, numVerts, prims, indices, fmt, verts, stride)
                for (int j = 0; j < 4; j++) Mix(args[j]);
                Mix(args[5]); Mix(args[7]);
                if (args[4])
                    MixBytes(reinterpret_cast<const void*>(args[4]),
                             VerticesFor(args[0], args[3]) * (args[5] == D3DFMT_INDEX32 ? 4 : 2));
                if (args[6])
                    MixBytes(reinterpret_cast<const void*>(args[6]), (args[1] + args[2]) * args[7]);
                k = 7;
                break;
            default: break;  // 'o' output pointers
            }
        }
    }
    if (index == 17)  // Present: close the frame
    {
        if (g_out)
            fprintf(g_out, "%u %u %016llx\n", g_frame, g_calls, g_hash);
        g_frame++;
        g_calls = 0;
        g_hash = 1469598103934665603ull;
    }
    void* real = g_realVtbl[index];
    LeaveCriticalSection(&g_lock);
    return real;
}

// Stack on entry to Common: [index][return address][this][args...]
__declspec(naked) void Common()
{
    __asm {
        pushad                      // 32 bytes
        lea eax, [esp + 32 + 12]    // first argument after `this`
        push eax
        push dword ptr [esp + 36]   // index (at esp+32, plus the push above)
        call TraceCall
        add esp, 8
        mov [esp + 32], eax         // replace the index slot with the real method
        popad
        ret                         // jumps to the real method; stack is [return][this][args...]
    }
}

#define THUNK(i) \
    __declspec(naked) void Thunk##i() { __asm push i __asm jmp Common }
#define T10(n) THUNK(n##0) THUNK(n##1) THUNK(n##2) THUNK(n##3) THUNK(n##4) THUNK(n##5) THUNK(n##6) THUNK(n##7) \
    THUNK(n##8) THUNK(n##9)
THUNK(0) THUNK(1) THUNK(2) THUNK(3) THUNK(4) THUNK(5) THUNK(6) THUNK(7) THUNK(8) THUNK(9)
T10(1) T10(2) T10(3) T10(4) T10(5) T10(6) T10(7) T10(8) T10(9) T10(10)
THUNK(110) THUNK(111) THUNK(112) THUNK(113) THUNK(114) THUNK(115) THUNK(116) THUNK(117) THUNK(118)

#define ADDR10(n) Thunk##n##0, Thunk##n##1, Thunk##n##2, Thunk##n##3, Thunk##n##4, Thunk##n##5, Thunk##n##6, \
    Thunk##n##7, Thunk##n##8, Thunk##n##9
void (*const kThunks[119])() = {
    Thunk0, Thunk1, Thunk2, Thunk3, Thunk4, Thunk5, Thunk6, Thunk7, Thunk8, Thunk9,
    ADDR10(1), ADDR10(2), ADDR10(3), ADDR10(4), ADDR10(5), ADDR10(6), ADDR10(7), ADDR10(8), ADDR10(9), ADDR10(10),
    Thunk110, Thunk111, Thunk112, Thunk113, Thunk114, Thunk115, Thunk116, Thunk117, Thunk118,
};
}  // namespace

// Called after the game's device is created. Must run before hooks that patch shared vtable entries, because
// it snapshots the current entries (those hooks then still run, being in the real vtable we forward to).
void D3DTraceAttach(IDirect3DDevice9* dev)
{
    if (!ConfigInt("test", "d3d_trace", 0) || g_realVtbl)
        return;
    InitializeCriticalSection(&g_lock);
    g_ids = new std::unordered_map<const void*, unsigned>();
    g_hash = 1469598103934665603ull;
    g_out = fopen("and-d3d.txt", "w");
    g_realVtbl = *reinterpret_cast<void***>(dev);
    MEMORY_BASIC_INFORMATION mbi;
    VirtualQuery(g_realVtbl, &mbi, sizeof(mbi));
    size_t avail = (reinterpret_cast<BYTE*>(mbi.BaseAddress) + mbi.RegionSize - reinterpret_cast<BYTE*>(g_realVtbl)) /
                   sizeof(void*);
    memcpy(g_ourVtbl, g_realVtbl, (avail < kVtblCopy ? avail : kVtblCopy) * sizeof(void*));
    for (int i = 0; i < 119; i++)
        g_ourVtbl[i] = reinterpret_cast<void*>(kThunks[i]);
    *reinterpret_cast<void***>(dev) = g_ourVtbl;
    Log("test: Direct3D call trace on device %p -> and-d3d.txt", dev);
}

// The harness's own device calls (back-buffer hashes and screenshots) are not part of the game's stream.
void D3DTraceSuppress(bool on)
{
    g_suppress += on ? 1 : -1;
}

void D3DTraceFlush()
{
    if (g_out)
        fflush(g_out);
}
