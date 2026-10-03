// Recovered: level-chunk loaders (fx, landscape, object system skins/anims/properties) and helpers.
//   0x0041c7e0 Fx_LoadChunk
//   0x00447590 Language_GetCode
//   0x00447920 fn_00447920
//   0x00447a00 ChunkFile_Load
//   0x00448470 Landscape_Load
//   0x00448790 LandscapeRegions_FindOrAdd
//   0x00448870 Landscape_BuildRegions
//   0x00449480 Mesh_GetVertex
//   0x0045ad10 ObjectSystem_FindSkin
//   0x0045af30 ObjectSystem_LoadSkins
//   0x0045b430 ObjectSystem_LoadAnims
//   0x0045b6c0 ObjectSystem_FindProperty
//   0x0045b7d0 ObjectSystem_LoadProperties
// Not recovered here: 0x00410ec0 and 0x00449e80 read stack arguments that symbols/abi.csv does not list.
#include <cstring>

#include "game.h"
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
template <class T>
T* p32(uint32_t v)
{
    return reinterpret_cast<T*>(v);
}

uint8_t* LoaderGame()
{
    return GameGlobal<uint8_t*>(0x005fe490);
}

// Imports, called through the game's import table so import hooks still apply.
using CreateFileAFn = void*(__stdcall*)(const char*, uint32_t, uint32_t, void*, uint32_t, uint32_t, void*);
using ReadFileFn = int(__stdcall*)(void*, void*, uint32_t, uint32_t*, void*);
using CloseHandleFn = int(__stdcall*)(void*);
void* Loader_CreateFileA(const char* path, uint32_t access, uint32_t share, void* sa, uint32_t disp, uint32_t flags,
                         void* tmpl)
{
    return (*reinterpret_cast<CreateFileAFn*>(0x0056b0c0))(path, access, share, sa, disp, flags, tmpl);
}
int Loader_ReadFile(void* h, void* buffer, uint32_t size, uint32_t* read, void* overlapped)
{
    return (*reinterpret_cast<ReadFileFn*>(0x0056b0c8))(h, buffer, size, read, overlapped);
}
int Loader_CloseHandle(void* h)
{
    return (*reinterpret_cast<CloseHandleFn*>(0x0056b084))(h);
}

// Callees whose generated wrapper is missing (the function ends in a tail jump to __security_check_cookie, so
// abi.csv notes "no ret found") or wrong (0x004e1db0 reads one stack argument that abi.csv does not list).
// Same shape as the generated wrappers: callee-saved registers and EBP are preserved, caller pops the stack.

// 0x004e1db0: inputs eax ecx, one stack argument (read at [esp+4] on entry), caller pops.
uint32_t Loader_Call_004e1db0(uint32_t eax_, uint32_t ecx_, uint32_t a0)
{
    static const uint32_t target = 0x004e1db0;
    uint32_t result;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        push a0
        mov eax, eax_
        mov ecx, ecx_
        call dword ptr [target]
        add esp, 4
        pop ebp
        pop edi
        pop esi
        pop ebx
        mov result, eax
    }
    return result;
}

// 0x004f1c90: input esi, one stack argument, caller pops; returns a bool in al.
uint32_t Loader_Call_004f1c90(uint32_t esi_, uint32_t a0)
{
    static const uint32_t target = 0x004f1c90;
    uint32_t result;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        push a0
        mov esi, esi_
        call dword ptr [target]
        add esp, 4
        pop ebp
        pop edi
        pop esi
        pop ebx
        mov result, eax
    }
    return result;
}

// 0x004eb140: input ecx, two stack arguments, caller pops.
uint32_t Loader_Call_004eb140(uint32_t ecx_, uint32_t a0, uint32_t a1)
{
    static const uint32_t target = 0x004eb140;
    uint32_t result;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        push a1
        push a0
        mov ecx, ecx_
        call dword ptr [target]
        add esp, 8
        pop ebp
        pop edi
        pop esi
        pop ebx
        mov result, eax
    }
    return result;
}

// 0x004495b0: no register inputs, one stack argument, caller pops.
uint32_t Loader_Call_004495b0(uint32_t a0)
{
    static const uint32_t target = 0x004495b0;
    uint32_t result;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        push a0
        call dword ptr [target]
        add esp, 4
        pop ebp
        pop edi
        pop esi
        pop ebx
        mov result, eax
    }
    return result;
}

// The game's tagged allocator (0x004e07f0, ecx = allocator 0x0063a210; [category, size, alignment, name]).
uint32_t Loader_Alloc(uint32_t category, uint32_t size, uint32_t alignment, uint32_t name)
{
    return orig::fn_004e07f0(0x0063a210, category, size, alignment, name);
}

// Inlined everywhere in the loaders: allocate a 0x20-byte hash table (category 0x10, name 0x0058b5e8) and set
// its compare callback (+0x10) and context (+0x14). Null if the allocation failed.
uint32_t Loader_NewHashTable(uint32_t compare, uint32_t context)
{
    auto* t = p32<uint32_t>(Loader_Alloc(0x10, 0x20, 2, 0x0058b5e8));
    if (!t)
        return 0;
    t[0] = 0;
    t[1] = 0;
    t[2] = 0;
    t[4] = compare;
    t[5] = context;
    t[7] = 0;
    return u(t);
}

// Inlined everywhere in the loaders: read `size` bytes from the chunk stream (reader object +0x28, virtual 1)
// unless the reader is missing or its +0x10 bit 6 (error) is set; when the read sets the error bit, call the
// stream's error callback (+0x00) with -1. Returns the reader's result, 0 if nothing was read.
uint32_t Loader_StreamRead(uint8_t* stream, uint32_t size, uint32_t destination)
{
    void* reader = Field<void*>(stream, 0x28);
    if (!reader || ((Field<uint32_t>(reader, 0x10) >> 6) & 1))
        return 0;
    uint32_t result = VCall<uint32_t>(reader, 1, size, destination);
    if ((Field<uint32_t>(Field<void*>(stream, 0x28), 0x10) >> 6) & 1)
    {
        auto onError = Field<void(__cdecl*)(int)>(stream, 0);
        if (onError)
            onError(-1);
    }
    return result;
}
}  // namespace

uint32_t fn_00447920(uint32_t* desc, uint8_t* object);
uint8_t* LandscapeRegions_FindOrAdd(uint32_t x, uint8_t* landscape, uint32_t material, uint32_t y);
uint32_t Landscape_BuildRegions(uint8_t* landscape);
uint32_t Mesh_GetVertex(uint8_t* mesh, float* out, uint32_t vertex);

// 0x0041c7e0 (eax = chunk stream, ecx = initial count, edi = fx system; [chunk size]): fx chunk ("Fx Data"):
// reset (0x0041c9d0), read the table size, allocate size - 4 bytes, make the hash table (+0x14, compare
// 0x0041c7b0), read the data (+0x0c/+0x18, end of table +0x10), then the three particle texture banks
// (+0x1c/+0x20/+0x24) bound to n_fx_bank_1 / n_fx_bank_2 (0x004881c0 lookup in Game+0xa78).
uint32_t Fx_LoadChunk(uint8_t* stream, uint32_t initialCount, uint8_t* fx, uint32_t size)
{
    orig::fn_0041c9d0(u(fx));
    uint32_t dataSize = size - 4;
    Field<uint32_t>(fx, 0xc) = Loader_Alloc(0x11, dataSize, 2, 0x005b938c /* "Fx Data" */);
    uint32_t tableSize = initialCount;  // the stack slot that held ecx receives the read
    Loader_StreamRead(stream, 4, u(&tableSize));
    Field<uint32_t>(fx, 0x14) = Loader_NewHashTable(0x0041c7b0, u(fx));
    uint32_t data = Field<uint32_t>(fx, 0xc);
    Field<uint32_t>(fx, 0x18) = data;
    Field<uint32_t>(fx, 0x10) = data + tableSize;
    Loader_StreamRead(stream, dataSize, data);
    orig::fn_004f1220(Field<uint32_t>(fx, 0x18), Field<uint32_t>(fx, 0x14));

    Field<uint32_t>(fx, 0x1c) = orig::fn_0046d780(2000, 0);
    uint32_t texture = orig::fn_004881c0(0x005b9380 /* "n_fx_bank_1" */, Field<uint32_t>(LoaderGame(), 0xa78),
                                         0, 0, 0, 0, 0, 0, 0, 0);
    if (texture == 0)
        orig::fn_00407ff0(Field<uint32_t>(LoaderGame(), 0x34), 8, 0x005b934c, 0);
    orig::fn_0046d900(texture, Field<uint32_t>(fx, 0x1c));

    Field<uint32_t>(fx, 0x20) = orig::fn_0046d780(4000, 1);
    texture = orig::fn_004881c0(0x005b9340 /* "n_fx_bank_2" */, Field<uint32_t>(LoaderGame(), 0xa78), 0, 0, 0,
                                0, 0, 0, 0, 0);
    if (texture == 0)
        orig::fn_00407ff0(Field<uint32_t>(LoaderGame(), 0x34), 8, 0x005b930c, 0);
    orig::fn_0046d900(texture, Field<uint32_t>(fx, 0x20));

    uint32_t bank = orig::fn_0046d780(2000, 2);
    Field<uint32_t>(fx, 0x24) = bank;
    return orig::fn_0046d900(texture, bank);
}

// 0x00447590: the language code, cached in 0x0063a198: the first dword of lang.dat (opened with 0x004f1c90)
// when exactly 4 bytes could be read (it asks for 8), else 'dflt' (0x64666c74).
uint32_t Language_GetCode()
{
    uint32_t& cached = GameGlobal<uint32_t>(0x0063a198);
    if (cached != 0)
        return cached;
    uint32_t code = 0x64666c74;
    struct
    {
        void* handle;
        char path[0x104];
    } file;
    if (static_cast<uint8_t>(Loader_Call_004f1c90(u(&file), 0x005b26c0 /* "lang.dat" */)) != 0)
    {
        uint32_t buffer[2] = {};
        uint32_t bytesRead = 0;
        Loader_ReadFile(file.handle, buffer, 8, &bytesRead, nullptr);
        Loader_CloseHandle(file.handle);
        if (bytesRead == 4)
            code = buffer[0];
    }
    cached = code;
    return code;
}

// 0x00447920 (eax = descriptor {buffer, done, swap, 0}, ecx = object): unless descriptor[1] is set, find the
// object's entry with the binary search at buffer+8 (0x004e0f40) and, when descriptor[2] is set, byte-swap the
// dword at object + (entry end - entry start) (0x004f60b0); then fix up the object's +0x08/+0x0c/+0x10 links.
uint32_t fn_00447920(uint32_t* desc, uint8_t* object)
{
    if (desc[1] == 0)
    {
        uint32_t key = u(object);
        uint8_t* table = Field<uint8_t*>(p32<uint8_t>(desc[0]), 8);
        uint8_t* entry = nullptr;
        uint32_t first = Field<uint32_t>(table, 0x1c);
        if (first != 0 && Field<uint32_t>(table, 0x20) != 0)
        {
            uint8_t* search = Field<uint8_t*>(table, 0x24);
            Field<uint32_t>(search, 0) = first;
            Field<uint32_t>(search, 0x18) = u(&key);
            orig::fn_004e0f40(u(search));
            if (Field<uint32_t>(search, 0xc) == 0)
                entry = Field<uint8_t**>(search, 0x10)[Field<uint32_t>(search, 8)];
        }
        uint8_t* range = entry + Field<uint16_t>(entry, 2);
        uint32_t target = Field<uint32_t>(range, 4) - Field<uint32_t>(range, 0) + u(object);
        if (desc[2] != 0)
            orig::fn_004f60b0(target, 0, 0, 0);
    }
    orig::fn_004e2040(u(object + 8), u(desc));
    orig::fn_004e2040(u(object + 0xc), u(desc));
    uint32_t link = Field<uint32_t>(object, 0xc);
    orig::fn_004f0e60(u(desc), link, 0x004f0dd0, 0x00447810);  // edi = descriptor (pushed on to 0x004e2040)
    return orig::fn_004e2160(u(desc), u(object + 0x10), 1, link, 0x004f0dd0);
}

// 0x00447a00 (eax, ecx = path parts for 0x004f19d0; [preloaded buffer, heap, 5 unused]): load a chunk file:
// read its 12-byte header {size, alignment?, ...}, allocate size bytes from the heap (0x004e1e90), read the rest
// (0x004f1ed0), close it; with no path, use the preloaded buffer. Unless a step failed, open the chunk
// (0x004e1db0) and, when its object type is 9, fix it up (0x00447920) and return it (+4 = buffer, +0x10 = we
// allocated). Otherwise frees what it allocated and returns null. Read errors call 0x004495b0(-1).
uint32_t ChunkFile_Load(uint32_t pathA, uint32_t pathB, uint32_t preloaded, uint32_t heap)
{
    uint32_t buffer = preloaded;
    bool allocated = false;
    bool failed = false;
    if (pathA != 0)
    {
        failed = true;
        char path[0x104];
        orig::fn_004f19d0(pathA, pathB, u(path));
        void* handle = Loader_CreateFileA(path, 0x80000000, 1, nullptr, 3, 0, nullptr);
        if (handle != reinterpret_cast<void*>(-1) && handle != nullptr)
        {
            uint32_t header[3];
            uint32_t bytesRead;
            uint8_t readFailed = Loader_ReadFile(handle, header, 0xc, &bytesRead, nullptr) == 0 ? 1 : 0;
            bool reportError = true;
            if (!readFailed)
            {
                if (bytesRead != 0xc)
                {
                    reportError = false;
                }
                else
                {
                    buffer = orig::fn_004e1e90(heap, header[0], 0, header[1]);
                    Field<uint32_t>(p32<uint8_t>(buffer), 0) = header[0];
                    Field<uint32_t>(p32<uint8_t>(buffer), 4) = header[1];
                    Field<uint32_t>(p32<uint8_t>(buffer), 8) = header[2];
                    uint32_t got = orig::fn_004f1ed0(header[0] - 0xc, buffer + 0xc, u(&readFailed), u(&handle),
                                                     heap, header[0], 0, header[1]);
                    uint8_t secondFailed = readFailed;
                    if (secondFailed == 0 && got == header[0] - 0xc)
                        failed = false;
                    Loader_CloseHandle(handle);
                    allocated = true;
                    reportError = secondFailed != 0;
                }
            }
            if (reportError)
                Loader_Call_004495b0(0xffffffff);
        }
    }
    if (buffer == 0)
        return 0;
    if (!failed)
    {
        uint8_t* chunk = p32<uint8_t>(Loader_Call_004e1db0(buffer, 0, heap));
        heap = u(chunk);  // the original stores the result in its heap argument slot and reuses it below
        uint8_t* object = Field<uint8_t*>(chunk, 0x20);
        if (Field<uint32_t>(object, 0) == 9)
        {
            uint32_t desc[4] = {buffer, 1, 0, 0};
            fn_00447920(desc, object);
            Field<uint32_t>(object, 4) = buffer;
            Field<uint8_t>(object, 0x10) = allocated ? 1 : 0;
            return u(object);
        }
    }
    if (allocated)
        orig::fn_004e1f70(heap, buffer);
    return 0;
}

// 0x00448470 (eax = chunk stream, esi = landscape; [chunk size]): landscape chunk ("LandscapeSystem::Load"):
// read the data (+0x04, also +0xf00), load its skin (0x004eb140, +0xefc), make the model (+0xf4c) and render
// object (+0x00), copy the 11-dword grid header (+0x08..+0x2c), point the per-grid cell and index arrays
// ("Grid Array" +0xef4, "Grid Indices Array" +0xef8) into the data, then build the regions (0x00448870).
uint32_t Landscape_Load(uint8_t* stream, uint8_t* land, uint32_t size)
{
    orig::fn_00448400(u(land));
    uint32_t dataSize = size - 4;
    Field<uint32_t>(land, 4) = Loader_Alloc(0xd, dataSize, 5, 0x005b25c4 /* "LandscapeSystem::Load" */);
    uint32_t headerOffset = 0;
    Loader_StreamRead(stream, 4, u(&headerOffset));
    Loader_StreamRead(stream, dataSize, Field<uint32_t>(land, 4));
    uint32_t data = Field<uint32_t>(land, 4);
    Field<uint32_t>(land, 0xf00) = data;
    uint32_t* header = p32<uint32_t>(data + headerOffset);
    uint32_t skin = Loader_Call_004eb140(Field<uint32_t>(land, 0xf00), 0, 0);
    Field<uint32_t>(land, 0xefc) = skin;
    if (skin == 0)
        orig::fn_00407ff0(Field<uint32_t>(LoaderGame(), 0x34), 0x005b25a4 /* "LandscapeLoad can not load skin" */,
                          0, 0);

    uint8_t& lock = GameGlobal<uint8_t>(0x0063a0fc);
    uint8_t wasSet;
    if (lock != 1)
    {
        orig::fn_004e0bd0();
        lock = 1;
        wasSet = 0;
    }
    else
    {
        wasSet = 1;
    }
    orig::fn_0045a860(Field<uint32_t>(LoaderGame(), 0xa84), Field<uint32_t>(land, 0xefc), 0x42480000 /* 50.0f */, 0);
    Field<uint32_t>(land, 0xf4c) = orig::fn_004ed410(0, 0, Field<uint32_t>(land, 0xefc), 0);
    if (wasSet != lock)
    {
        orig::fn_004e0bd0();
        lock = wasSet;
    }

    Field<uint32_t>(land, 0x08) = header[1];
    Field<uint32_t>(land, 0x0c) = header[2];
    Field<uint32_t>(land, 0x10) = header[3];
    Field<uint32_t>(land, 0x14) = header[4];
    Field<uint32_t>(land, 0x1c) = header[5];
    Field<uint32_t>(land, 0x24) = header[6];
    Field<uint32_t>(land, 0x18) = header[7];
    Field<uint32_t>(land, 0x20) = header[8];
    Field<uint32_t>(land, 0x28) = header[9];
    Field<uint32_t>(land, 0x2c) = header[10];
    uint32_t* cursor = header + 11;

    Field<uint32_t>(land, 0xef4) =
        Loader_Alloc(0xd, Field<uint32_t>(Field<uint8_t*>(land, 0xefc), 0x28) << 2, 2, 0x005b2598 /* "Grid Array" */);
    Field<uint32_t>(land, 0xef8) = Loader_Alloc(0xd, Field<uint32_t>(Field<uint8_t*>(land, 0xefc), 0x28) << 2, 2,
                                                0x005b2584 /* "Grid Indices Array" */);
    for (int32_t g = 0; g < Field<int32_t>(Field<uint8_t*>(land, 0xefc), 0x28); ++g)
    {
        Field<uint32_t**>(land, 0xef4)[g] = cursor;
        uint32_t cells = Field<uint32_t>(land, 0xc) * Field<uint32_t>(land, 0x10) * Field<uint32_t>(land, 8);
        uint32_t* indices = cursor + cells * 2;
        Field<uint32_t**>(land, 0xef8)[g] = indices;
        cells = Field<uint32_t>(land, 0xc) * Field<uint32_t>(land, 0x10) * Field<uint32_t>(land, 8);
        uint32_t* lastCellEnd = Field<uint32_t**>(land, 0xef4)[g] + cells * 2;
        cursor = indices + (lastCellEnd[-2] + lastCellEnd[-1]);
    }

    alignas(16) uint8_t config[0xd0] = {};
    orig::fn_00449510(u(config));
    orig::fn_00456c40(Field<uint32_t>(LoaderGame(), 0xa8c), 0, Field<uint32_t>(land, 0xf4c), 0, 0, u(config));
    config[0xe] = 1;
    config[0xd] = 1;
    Field<uint32_t>(config, 0) = 2;
    Field<uint32_t>(land, 0) = orig::fn_00457030(Field<uint32_t>(LoaderGame(), 0xa8c), 0, u(config));
    orig::fn_0046ed30(Field<uint32_t>(LoaderGame(), 0xaa4));
    uint32_t bounds[6] = {Field<uint32_t>(land, 0x14), Field<uint32_t>(land, 0x1c), Field<uint32_t>(land, 0x24),
                          Field<uint32_t>(land, 0x18), Field<uint32_t>(land, 0x20), Field<uint32_t>(land, 0x28)};
    orig::fn_0048e870(u(bounds), Field<uint32_t>(LoaderGame(), 0xaa0), Field<uint32_t>(land, 0x2c));
    return Landscape_BuildRegions(land);
}

// 0x00448790 (ebx = cell x, esi = landscape; [material, cell y]): the landscape region (+0x34, 0x1d8-byte
// entries, count +0x30, at most 8) for a material whose +0x08 object has (+0x18 & 0xf000) == 0xd000: grows an
// existing region's cell bounds (+0x08/+0x0c min, +0x10/+0x14 max) or adds a zeroed one whose extreme vertices
// start at the landscape's bounds (+0x14..+0x20) widened by the float at 0x005bcb2c. Null otherwise.
uint8_t* LandscapeRegions_FindOrAdd(uint32_t x, uint8_t* land, uint32_t material, uint32_t y)
{
    if ((Field<uint32_t>(Field<uint8_t*>(p32<uint8_t>(material), 8), 0x18) & 0xf000) != 0xd000)
        return nullptr;
    uint32_t count = Field<uint32_t>(land, 0x30);
    uint8_t* region = land + 0x34;
    for (uint32_t left = count; left != 0; --left, region += 0x1d8)
    {
        if (Field<uint32_t>(region, 0) == material)
        {
            if (x < Field<uint32_t>(region, 8))
                Field<uint32_t>(region, 8) = x;
            else if (x > Field<uint32_t>(region, 0x10))
                Field<uint32_t>(region, 0x10) = x;
            if (y < Field<uint32_t>(region, 0xc))
                Field<uint32_t>(region, 0xc) = y;
            else if (y > Field<uint32_t>(region, 0x14))
                Field<uint32_t>(region, 0x14) = y;
            return region;
        }
    }
    if (count == 8)
        return nullptr;
    Field<uint32_t>(land, 0x30) = count + 1;
    memset(region, 0, 0x76 * 4);
    Field<uint32_t>(region, 0) = material;
    Field<uint32_t>(region, 0x14) = y;
    Field<uint32_t>(region, 0xc) = y;
    Field<uint32_t>(region, 0x10) = x;
    Field<uint32_t>(region, 8) = x;
    const float margin = GameGlobal<float>(0x005bcb2c);
    Field<float>(region, 0x18) = Field<float>(land, 0x18) + margin;
    Field<float>(region, 0x44) = Field<float>(land, 0x14) - margin;
    Field<float>(region, 0x74) = Field<float>(land, 0x20) + margin;
    Field<float>(region, 0xa0) = Field<float>(land, 0x1c) - margin;
    return region;
}

// 0x00448870 ([landscape]): rebuild the regions: for every grid (+0xefc +0x28) and cell, find or add the region
// of each primitive's material (0x00448790) and keep, per region, the vertex with the smallest/largest x
// (+0x18/+0x44) and y (+0x70/+0x9c) (0x00449480). Then for each region solve the texture mapping from three of
// those vertices (0x004e2b40, 0x004e3950) into four 0x44-byte records at +0xc8. Returns the region count.
uint32_t Landscape_BuildRegions(uint8_t* land)
{
    Field<uint32_t>(land, 0x30) = 0;
    for (int32_t g = 0; g < Field<int32_t>(Field<uint8_t*>(land, 0xefc), 0x28); ++g)
    {
        uint8_t* mesh = Field<uint8_t*>(Field<uint8_t*>(Field<uint8_t*>(land, 0xf4c), 0x18 + g * 4), 4);
        for (uint32_t z = 0; z < Field<uint32_t>(land, 0x10); ++z)
        {
            for (uint32_t y = 0; y < Field<uint32_t>(land, 0xc); ++y)
            {
                for (uint32_t x = 0; x < Field<uint32_t>(land, 8); ++x)
                {
                    uint32_t* cell = Field<uint32_t**>(land, 0xef4)[g] +
                                     ((Field<uint32_t>(land, 0xc) * z + y) * Field<uint32_t>(land, 8) + x) * 2;
                    uint32_t* index = Field<uint32_t**>(land, 0xef8)[g] + cell[0];
                    for (uint32_t k = 0; k < cell[1]; ++k)
                    {
                        uint32_t primIndex = *index++;
                        uint8_t* prim = Field<uint8_t*>(mesh, 0x84) + primIndex * 0x2c;
                        uint32_t material = Field<uint32_t>(prim, 0x10) * 100 + Field<uint32_t>(mesh, 0x7c);
                        uint8_t* region = LandscapeRegions_FindOrAdd(x, land, material, y);
                        if (!region)
                            continue;
                        uint32_t vertex = Field<uint32_t>(prim, 0x14);
                        for (uint32_t left = Field<uint32_t>(prim, 0x18); left != 0; --left, ++vertex)
                        {
                            float pos[3];
                            uint8_t* skin = Field<uint8_t*>(mesh, 0x44);
                            if (skin)
                            {
                                orig::fn_004effa0(u(pos), u(skin + 0xac), u(skin + 0x20), u(skin + 0x70), vertex,
                                                  u(skin + 0x10));
                            }
                            else
                            {
                                uint32_t v = Field<uint32_t*>(mesh, 0x48)[vertex];
                                memcpy(pos, Field<uint8_t*>(mesh, 0x28) + v * 12, 12);
                            }
                            if (pos[0] < Field<float>(region, 0x18))
                                Mesh_GetVertex(mesh, &Field<float>(region, 0x18), vertex);
                            else if (pos[0] > Field<float>(region, 0x44))
                                Mesh_GetVertex(mesh, &Field<float>(region, 0x44), vertex);
                            else if (pos[1] < Field<float>(region, 0x74))
                                Mesh_GetVertex(mesh, &Field<float>(region, 0x70), vertex);
                            else if (pos[1] > Field<float>(region, 0xa0))
                                Mesh_GetVertex(mesh, &Field<float>(region, 0x9c), vertex);
                        }
                    }
                }
            }
        }
    }

    uint32_t r = 0;
    if (Field<uint32_t>(land, 0x30) == 0)
        return 0;
    uint32_t uv[8];  // two 4-vectors: u and v of the three vertices, w = 0
    uv[3] = 0;
    uv[7] = 0;
    uint8_t* region = land + 0x34;
    do
    {
        uint32_t m[17];  // positions of the min-x, max-x and max-y vertices as rows (x, y, 1, 0), flags 0x3f
        m[0] = Field<uint32_t>(region, 0x18);
        m[1] = Field<uint32_t>(region, 0x1c);
        m[2] = 0x3f800000;
        m[3] = 0;
        m[4] = Field<uint32_t>(region, 0x44);
        m[5] = Field<uint32_t>(region, 0x48);
        m[6] = 0x3f800000;
        m[7] = 0;
        m[8] = Field<uint32_t>(region, 0x9c);
        m[9] = Field<uint32_t>(region, 0xa0);
        m[10] = 0x3f800000;
        m[11] = 0;
        m[12] = 0;
        m[13] = 0;
        m[14] = 0;
        m[15] = 0x3f800000;
        m[16] = 0x3f;
        uint32_t inverse[17];
        orig::fn_004e2b40(u(m), u(inverse));
        for (uint32_t set = 0; set < 4; ++set)
        {
            uint32_t pair = set * 8;  // uv pair `set` (+0x0c) of the min-x, max-x and max-y vertices
            uv[0] = Field<uint32_t>(region, 0x24 + pair);
            uv[1] = Field<uint32_t>(region, 0x50 + pair);
            uv[2] = Field<uint32_t>(region, 0xa8 + pair);
            uv[4] = Field<uint32_t>(region, 0x28 + pair);
            uv[5] = Field<uint32_t>(region, 0x54 + pair);
            uv[6] = Field<uint32_t>(region, 0xac + pair);
            uint32_t res[8];
            orig::fn_004e3950(u(inverse), u(res), 2, u(uv));
            uint32_t* out = reinterpret_cast<uint32_t*>(region + 0xc8 + set * 0x44);
            out[0] = res[0];
            out[1] = res[1];
            out[3] = res[2];
            out[4] = res[4];
            out[2] = 0;
            out[6] = 0;
            out[5] = res[5];
            out[7] = res[6];
            out[15] = 0x3f800000;
            out[16] = 0x1f;
        }
        ++r;
        region += 0x1d8;
    } while (r < Field<uint32_t>(land, 0x30));
    return r;
}

// 0x00449480 (esi = mesh; [destination, vertex index]): fetch a vertex: position from the skin (+0x44,
// 0x004effa0) or the mesh's position table (+0x28 indexed through +0x48), then its four uv sets (0x004ef9e0)
// at destination +0x0c/+0x14/+0x1c/+0x24.
uint32_t Mesh_GetVertex(uint8_t* mesh, float* out, uint32_t vertex)
{
    uint8_t* skin = Field<uint8_t*>(mesh, 0x44);
    if (skin)
    {
        orig::fn_004effa0(u(out), u(skin + 0xac), u(skin + 0x20), u(skin + 0x70), vertex, u(skin + 0x10));
    }
    else
    {
        const uint32_t* src =
            p32<uint32_t>(Field<uint32_t>(mesh, 0x28) + Field<uint32_t*>(mesh, 0x48)[vertex] * 12);
        uint32_t* dst = reinterpret_cast<uint32_t*>(out);
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
    }
    uint8_t* bytes = reinterpret_cast<uint8_t*>(out);
    orig::fn_004ef9e0(u(mesh), u(bytes + 0xc), vertex, 0);
    orig::fn_004ef9e0(u(mesh), u(bytes + 0x14), vertex, 1);
    orig::fn_004ef9e0(u(mesh), u(bytes + 0x1c), vertex, 2);
    return orig::fn_004ef9e0(u(mesh), u(bytes + 0x24), vertex, 3);
}

// 0x0045ad10 (ecx = name, esi = object system): the skin with that name: lookup in the skin hash table
// (+0xe10, 0x004f1160), index into the skin list (+0xe18). Null if absent.
uint32_t ObjectSystem_FindSkin(uint32_t name, uint8_t* system)
{
    int32_t* found = p32<int32_t>(orig::fn_004f1160(Field<uint32_t>(system, 0xe10), name));
    if (found && *found >= 0)
        return Field<uint32_t*>(system, 0xe18)[*found];
    return 0;
}

// 0x0045af30 (eax = chunk stream, esi = object system; [chunk size]): skins chunk: hash table (+0xe10, compare
// 0x0045ad40) over "skin hashtable entries" (+0xe14), the skin count (+0xe0c), the rest of the chunk
// ("ObjectSystem::LoadSkins", +0xe1c) and the "skin list" (+0xe18): each skin is opened in place
// (0x004e1db0/0x004eae90) and registered (0x0045a860, 1.0f); skins are 32-byte aligned. Returns the count.
uint32_t ObjectSystem_LoadSkins(uint8_t* stream, uint8_t* system, uint32_t size)
{
    uint32_t tableSize = 0;
    uint32_t consumed = Loader_StreamRead(stream, 4, u(&tableSize));
    Field<uint32_t>(system, 0xe10) = Loader_NewHashTable(0x0045ad40, u(system));
    uint32_t table = Loader_Alloc(10, tableSize, 2, 0x005b0a38 /* "skin hashtable entries" */);
    Field<uint32_t>(system, 0xe14) = table;
    consumed += Loader_StreamRead(stream, tableSize, table);
    orig::fn_004f1220(Field<uint32_t>(system, 0xe14), Field<uint32_t>(system, 0xe10));
    consumed += Loader_StreamRead(stream, 4, u(system + 0xe0c));
    uint32_t restSize = size - consumed;
    uint32_t rest = Loader_Alloc(10, restSize, 5, 0x005b0a20 /* "ObjectSystem::LoadSkins" */);
    Field<uint32_t>(system, 0xe1c) = rest;
    Loader_StreamRead(stream, restSize, rest);
    Field<uint32_t>(system, 0xe18) =
        Loader_Alloc(10, Field<uint32_t>(system, 0xe0c) << 2, 2, 0x005b0a14 /* "skin list" */);
    uint32_t cursor = Field<uint32_t>(system, 0xe1c);
    for (uint32_t i = 0; i < Field<uint32_t>(system, 0xe0c); ++i)
    {
        uint32_t skin = 0;
        if (cursor != 0)
        {
            skin = Field<uint32_t>(p32<uint8_t>(Loader_Call_004e1db0(cursor, 0, 0)), 0x20);
            uint32_t desc[4] = {cursor, 1, 0, 0};
            orig::fn_004eae90(u(desc), skin, 0);
            Field<uint32_t>(p32<uint8_t>(skin), 8) = cursor;
            Field<uint16_t>(p32<uint8_t>(skin), 6) = 0;
        }
        Field<uint32_t*>(system, 0xe18)[i] = skin;
        orig::fn_0045a860(u(system), Field<uint32_t*>(system, 0xe18)[i], 0x3f800000 /* 1.0f */, 0);
        uint32_t skinSize = *Field<uint32_t*>(p32<uint8_t>(Field<uint32_t*>(system, 0xe18)[i]), 8);
        cursor = (skinSize - (skinSize & 0x1f)) + cursor + 0x20;
    }
    return Field<uint32_t>(system, 0xe0c);
}

// 0x0045b430 (ecx = unused, ebx = chunk stream, esi = object system; [chunk size]): animations chunk: hash
// table (+0xe24, compare 0x0045b200) over "anim hashtable entries" (+0xe28), the rest as "anim image data"
// (+0xe30), the anim count (+0xe20) and the "anim list" (+0xe2c) of anims read in place by 0x004e8430.
// Returns the count.
uint32_t ObjectSystem_LoadAnims(uint32_t /*ecx*/, uint8_t* stream, uint8_t* system, uint32_t size)
{
    uint32_t tableSize = size;  // read into the size argument's stack slot, which keeps size if nothing is read
    uint32_t first = Loader_StreamRead(stream, 4, u(&tableSize));
    Field<uint32_t>(system, 0xe24) = Loader_NewHashTable(0x0045b200, u(system));
    uint32_t table = Loader_Alloc(9, tableSize, 2, 0x005b09fc /* "anim hashtable entries" */);
    Field<uint32_t>(system, 0xe28) = table;
    uint32_t second = Loader_StreamRead(stream, tableSize, table);
    orig::fn_004f1220(Field<uint32_t>(system, 0xe28), Field<uint32_t>(system, 0xe24));
    Field<uint32_t>(system, 0xe30) = Loader_Alloc(9, size - second - first, 2, 0x005b09ec /* "anim image data" */);
    Loader_StreamRead(stream, 4, u(system + 0xe20));
    Field<uint32_t>(system, 0xe2c) =
        Loader_Alloc(9, Field<uint32_t>(system, 0xe20) << 2, 2, 0x005b09e0 /* "anim list" */);
    uint32_t cursor = Field<uint32_t>(system, 0xe30);
    for (uint32_t i = 0; i < Field<uint32_t>(system, 0xe20); ++i)
    {
        Field<uint32_t*>(system, 0xe2c)[i] = orig::fn_004e8430(u(stream), cursor);
        cursor += *Field<uint32_t*>(p32<uint8_t>(Field<uint32_t*>(system, 0xe2c)[i]), 8);
    }
    return Field<uint32_t>(system, 0xe20);
}

// 0x0045b6c0 (eax = name, esi = object system): the property list entry (+0xe58, 12-byte entries) whose name
// the property hash table (+0xe4c, 0x004f1160) maps, through the entry's +0x20 index. Null for an empty name.
uint32_t ObjectSystem_FindProperty(const char* name, uint8_t* system)
{
    if (!name || *name == 0)
        return 0;
    uint32_t found = orig::fn_004f1160(Field<uint32_t>(system, 0xe4c), u(name));
    if (!found)
        return 0;
    return Field<uint32_t>(system, 0xe58) + Field<uint32_t>(p32<uint8_t>(found), 0x20) * 0xc;
}

// 0x0045b7d0 (eax = chunk stream, edi = object system; [chunk size]): properties chunk ("prop data", +0xe54):
// a hash table (+0xe4c, compare 0x00495ee0) over the first block (+0xe50), then the "property list" (+0xe58,
// count +0xe48) of {hash table, its entries, value pointers}, value offsets made absolute. Returns the count
// (the list pointer when it is empty, as the original leaves it in eax).
uint32_t ObjectSystem_LoadProperties(uint8_t* stream, uint8_t* system, uint32_t size)
{
    uint32_t data = Loader_Alloc(0xc, size, 2, 0x005b09d4 /* "prop data" */);
    Field<uint32_t>(system, 0xe54) = data;
    Loader_StreamRead(stream, size, data);
    uint8_t* cursor = Field<uint8_t*>(system, 0xe54);
    uint32_t blockSize = Field<uint32_t>(cursor, 0);
    cursor += 4;
    Field<uint8_t*>(system, 0xe50) = cursor;
    cursor += blockSize;
    uint32_t table = Loader_NewHashTable(0x00495ee0, 0);
    Field<uint32_t>(system, 0xe4c) = table;
    orig::fn_004f1220(Field<uint32_t>(system, 0xe50), table);
    uint32_t count = Field<uint32_t>(cursor, 0);
    Field<uint32_t>(system, 0xe48) = count;
    cursor += 4;
    uint32_t list = Loader_Alloc(0xc, count * 0xc, 2, 0x005b09c4 /* "property list" */);
    Field<uint32_t>(system, 0xe58) = list;
    if (Field<uint32_t>(system, 0xe48) == 0)
        return list;
    uint32_t i = 0;
    uint32_t offset = 0;
    do
    {
        Field<uint8_t*>(Field<uint8_t*>(system, 0xe58), offset) = cursor;
        uint32_t entriesSize = Field<uint32_t>(cursor, 0);
        cursor += 4;
        uint32_t entryTable = Loader_NewHashTable(0x00495ee0, 0);
        Field<uint32_t>(Field<uint8_t*>(system, 0xe58), offset) = entryTable;
        Field<uint8_t*>(Field<uint8_t*>(system, 0xe58), offset + 4) = cursor;
        uint8_t* entry = Field<uint8_t*>(system, 0xe58) + offset;
        orig::fn_004f1220(Field<uint32_t>(entry, 4), Field<uint32_t>(entry, 0));
        cursor += entriesSize;
        uint32_t values = Field<uint32_t>(cursor, 0);
        uint32_t skip = Field<uint32_t>(cursor, 4);
        cursor += 8;
        Field<uint8_t*>(Field<uint8_t*>(system, 0xe58), offset + 8) = cursor;
        uint32_t base = u(cursor) + values * 4;
        for (uint32_t j = 0; j < values; ++j)
        {
            uint32_t value = Field<uint32_t>(cursor, 0) + base;
            cursor += 4;
            Field<uint32_t*>(Field<uint8_t*>(system, 0xe58), offset + 8)[j] = value;
        }
        cursor += skip;
        ++i;
        offset += 0xc;
    } while (i < Field<uint32_t>(system, 0xe48));
    return i;
}

// ---- ABI adapters ---------------------------------------------------------------------------------------------

static uint32_t Fx_LoadChunk_Abi(uint32_t eax, uint32_t ecx, uint32_t edi, uint32_t size)
{
    return Fx_LoadChunk(p32<uint8_t>(eax), ecx, p32<uint8_t>(edi), size);
}
static uint32_t Language_GetCode_Abi()
{
    return Language_GetCode();
}
static uint32_t fn_00447920_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_00447920(p32<uint32_t>(eax), p32<uint8_t>(ecx));
}
static uint32_t ChunkFile_Load_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t /*a2*/,
                                   uint32_t /*a3*/, uint32_t /*a4*/, uint32_t /*a5*/, uint32_t /*a6*/)
{
    return ChunkFile_Load(eax, ecx, a0, a1);
}
static uint32_t Landscape_Load_Abi(uint32_t eax, uint32_t esi, uint32_t size)
{
    return Landscape_Load(p32<uint8_t>(eax), p32<uint8_t>(esi), size);
}
static uint32_t LandscapeRegions_FindOrAdd_Abi(uint32_t ebx, uint32_t esi, uint32_t material, uint32_t y)
{
    return u(LandscapeRegions_FindOrAdd(ebx, p32<uint8_t>(esi), material, y));
}
static uint32_t Landscape_BuildRegions_Abi(uint32_t land)
{
    return Landscape_BuildRegions(p32<uint8_t>(land));
}
static uint32_t Mesh_GetVertex_Abi(uint32_t esi, uint32_t out, uint32_t vertex)
{
    return Mesh_GetVertex(p32<uint8_t>(esi), p32<float>(out), vertex);
}
static uint32_t ObjectSystem_FindSkin_Abi(uint32_t ecx, uint32_t esi)
{
    return ObjectSystem_FindSkin(ecx, p32<uint8_t>(esi));
}
static uint32_t ObjectSystem_LoadSkins_Abi(uint32_t eax, uint32_t esi, uint32_t size)
{
    return ObjectSystem_LoadSkins(p32<uint8_t>(eax), p32<uint8_t>(esi), size);
}
static uint32_t ObjectSystem_LoadAnims_Abi(uint32_t ecx, uint32_t ebx, uint32_t esi, uint32_t size)
{
    return ObjectSystem_LoadAnims(ecx, p32<uint8_t>(ebx), p32<uint8_t>(esi), size);
}
static uint32_t ObjectSystem_FindProperty_Abi(uint32_t eax, uint32_t esi)
{
    return ObjectSystem_FindProperty(p32<const char>(eax), p32<uint8_t>(esi));
}
static uint32_t ObjectSystem_LoadProperties_Abi(uint32_t eax, uint32_t edi, uint32_t size)
{
    return ObjectSystem_LoadProperties(p32<uint8_t>(eax), p32<uint8_t>(edi), size);
}

ABI_ENTRY_0041c7e0(Fx_LoadChunk_Entry, Fx_LoadChunk_Abi)
ABI_ENTRY_00447590(Language_GetCode_Entry, Language_GetCode_Abi)
ABI_ENTRY_00447920(fn_00447920_Entry, fn_00447920_Abi)
ABI_ENTRY_00447a00(ChunkFile_Load_Entry, ChunkFile_Load_Abi)
ABI_ENTRY_00448470(Landscape_Load_Entry, Landscape_Load_Abi)
ABI_ENTRY_00448790(LandscapeRegions_FindOrAdd_Entry, LandscapeRegions_FindOrAdd_Abi)
ABI_ENTRY_00448870(Landscape_BuildRegions_Entry, Landscape_BuildRegions_Abi)
ABI_ENTRY_00449480(Mesh_GetVertex_Entry, Mesh_GetVertex_Abi)
ABI_ENTRY_0045ad10(ObjectSystem_FindSkin_Entry, ObjectSystem_FindSkin_Abi)
ABI_ENTRY_0045af30(ObjectSystem_LoadSkins_Entry, ObjectSystem_LoadSkins_Abi)
ABI_ENTRY_0045b430(ObjectSystem_LoadAnims_Entry, ObjectSystem_LoadAnims_Abi)
ABI_ENTRY_0045b6c0(ObjectSystem_FindProperty_Entry, ObjectSystem_FindProperty_Abi)
ABI_ENTRY_0045b7d0(ObjectSystem_LoadProperties_Entry, ObjectSystem_LoadProperties_Abi)

HOOK(0x0041c7e0, "Fx_LoadChunk", Fx_LoadChunk_Entry, nullptr);
HOOK(0x00447590, "Language_GetCode", Language_GetCode_Entry, nullptr);
HOOK(0x00447920, "fn_00447920", fn_00447920_Entry, nullptr);
HOOK(0x00447a00, "ChunkFile_Load", ChunkFile_Load_Entry, nullptr);
HOOK(0x00448470, "Landscape_Load", Landscape_Load_Entry, nullptr);
HOOK(0x00448790, "LandscapeRegions_FindOrAdd", LandscapeRegions_FindOrAdd_Entry, nullptr);
HOOK(0x00448870, "Landscape_BuildRegions", Landscape_BuildRegions_Entry, nullptr);
HOOK(0x00449480, "Mesh_GetVertex", Mesh_GetVertex_Entry, nullptr);
HOOK(0x0045ad10, "ObjectSystem_FindSkin", ObjectSystem_FindSkin_Entry, nullptr);
HOOK(0x0045af30, "ObjectSystem_LoadSkins", ObjectSystem_LoadSkins_Entry, nullptr);
HOOK(0x0045b430, "ObjectSystem_LoadAnims", ObjectSystem_LoadAnims_Entry, nullptr);
HOOK(0x0045b6c0, "ObjectSystem_FindProperty", ObjectSystem_FindProperty_Entry, nullptr);
HOOK(0x0045b7d0, "ObjectSystem_LoadProperties", ObjectSystem_LoadProperties_Entry, nullptr);
