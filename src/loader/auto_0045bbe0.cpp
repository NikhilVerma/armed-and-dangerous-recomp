// Recovered: loader batch starting at 0x0045bbe0 (model slots, particle pool, pathing-grid rasteriser, texture
// bank loader, checksummed/compressed file stream). S2, Approach B.
//   0x0045bbe0 ModelSlots_Resolve
//   0x0046d780 ParticlePool_Create
//   0x0046e940 PathGrid_MarkCell
//   0x0046ea00 PathGrid_RasterizeTriangle   (uses PathGrid_ClipPolygon, the body of 0x0046e830, not hooked)
//   0x00488400 RenderSystem_LoadTextures
//   0x0048a700 StreamChecksum_Reset
//   0x0048a720 StreamChecksum_Add
//   0x0048a7a0 StreamChecksum_Write
//   0x0048a7c0 StreamChecksum_Read
//   0x0048a7e0 StreamChecksum_Equal
//   0x0048a8d0 FileStream_SetCompression
//   0x0048a9c0 FileStream_FlushBuffer
//   0x0048aac0 FileStream_Open
#include <cstring>

#include "game.h"
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"

#include <windows.h>

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}

uint8_t* GamePtr()
{
    return GameGlobal<uint8_t*>(0x005fe490);
}

constexpr uint32_t kHeap = 0x0063a210;  // allocator object passed in ECX to 0x004e07f0

uint32_t HeapAlloc_(uint32_t tag, uint32_t size, uint32_t align, uint32_t name)
{
    return orig::fn_004e07f0(kHeap, tag, size, align, name);
}

// Imports called through the game's IAT slots, so import hooks still apply.
BOOL IatReadFile(HANDLE h, void* buf, DWORD n, DWORD* got)
{
    using Fn = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
    return (*reinterpret_cast<Fn*>(0x0056b0c8))(h, buf, n, got, nullptr);
}
BOOL IatWriteFile(HANDLE h, const void* buf, DWORD n, DWORD* put)
{
    using Fn = BOOL(WINAPI*)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
    return (*reinterpret_cast<Fn*>(0x0056b0cc))(h, buf, n, put, nullptr);
}
HANDLE IatCreateFileA(const char* name, DWORD access, DWORD share, DWORD disposition, DWORD flags)
{
    using Fn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
    return (*reinterpret_cast<Fn*>(0x0056b0c0))(name, access, share, nullptr, disposition, flags, nullptr);
}
DWORD IatSetFilePointer(HANDLE h, LONG distance, LONG* high, DWORD method)
{
    using Fn = DWORD(WINAPI*)(HANDLE, LONG, PLONG, DWORD);
    return (*reinterpret_cast<Fn*>(0x0056b0d0))(h, distance, high, method);
}

// The inline "read chunk" sequence of the level loaders: stream (+0x28 reader, +0 error callback). Unless the
// reader's +0x10 bit 6 (failed) is set, call reader vtable slot 1 (size, dest); if that set the failed bit and
// an error callback exists, call it with -1.
void Stream_Read(uint8_t* stream, uint32_t size, void* dest)
{
    void* reader = Field<void*>(stream, 0x28);
    if (reader == nullptr || ((Field<uint32_t>(reader, 0x10) >> 6) & 1))
        return;
    VCall<uint32_t>(reader, 1, size, dest);
    if (!((Field<uint32_t>(Field<void*>(stream, 0x28), 0x10) >> 6) & 1))
        return;
    auto onError = Field<void(__cdecl*)(int32_t)>(stream, 0);
    if (onError)
        onError(-1);
}

// _ftol (0x0056a7c8): truncating float to 64-bit integer; callers here use the low dword.
uint32_t Ftol(float f)
{
    return static_cast<uint32_t>(static_cast<int64_t>(f));
}
}  // namespace

uint32_t PathGrid_MarkCell(uint32_t x, uint8_t* grid, uint32_t y, uint8_t* blocked, uint32_t clear, uint32_t force);
uint32_t StreamChecksum_Add(uint8_t* state, const uint8_t* data, uint32_t length);

// ---------------------------------------------------------------------------------------------------------------
// 0x0045bbe0 (edi unused; [ctx, slots, override, filter]): fills the model table of a mover-like object. With a
// selected column (slots+0 >= 0) it looks up that column's row index by name (0x004f1160). Then for every row
// (anim name at table+0x30, 0x20 each, optionally filtered by _stricmp against `filter`) it resolves the row
// animation (0x0045ace0), stores its rate (end-start)/(frames*fps) and pointer into slots+0xc records, and for
// each column resolves and instantiates the model (0x004ed410), using a "mover_" lookup (0x0045dec0) when no
// override is given and logging "needs_models" (0x0045df00) on failure. Brackets the work with 0x004e0bd0.
uint32_t ModelSlots_Resolve(uint32_t ctx, uint8_t* slots, uint32_t overrideModel, const char* filter)
{
    uint8_t& busy = GameGlobal<uint8_t>(0x0063a0fc);
    int32_t selectedRow = -1;
    if (busy != 1)
    {
        orig::fn_004e0bd0();
        busy = 1;
    }
    int32_t selectedColumn = Field<int32_t>(slots, 0);
    if (selectedColumn >= 0)
    {
        char name[64];
        name[0] = 0;
        uint8_t* table = Field<uint8_t*>(slots, 0x10);
        uint32_t column = orig::fn_0045ad10((static_cast<uint32_t>(selectedColumn) << 5) + Field<uint32_t>(table, 0x34), ctx);
        if (column != 0)
        {
            orig::fn_004588f0(column + 0x34, u(name));
            uint32_t found = orig::fn_004f1160(Field<uint32_t>(Field<uint8_t*>(slots, 0x10), 0), u(name));
            selectedRow = found ? *reinterpret_cast<int32_t*>(found) : -1;
        }
    }

    uint8_t* table = Field<uint8_t*>(slots, 0x10);
    memset(Field<void*>(table, 0x38), 0, static_cast<uint32_t>(Field<uint32_t>(table, 0x28) * Field<uint32_t>(table, 0x24) * 4));

    uint32_t flat = 0;
    if (Field<uint32_t>(table, 0x24) != 0)
    {
        uint32_t row = 0;
        uint32_t rowOffset = 0;   // 12-byte records at slots+0xc
        uint32_t nameOffset = 0;  // 0x20-byte names at table+0x30
        do
        {
            table = Field<uint8_t*>(slots, 0x10);
            uint32_t rowName = Field<uint32_t>(table, 0x30) + nameOffset;
            if (nameOffset != 0 && filter != nullptr &&
                orig::fn_005448ee(nameOffset, u(filter), rowName) != 0)
            {
                flat += Field<uint32_t>(Field<uint8_t*>(slots, 0x10), 0x28);
            }
            else
            {
                uint8_t* anim = reinterpret_cast<uint8_t*>(orig::fn_0045ace0(rowName, ctx));
                if (anim != nullptr)
                {
                    int32_t span = Field<int32_t>(anim, 0x18) - Field<int32_t>(anim, 0x14);
                    int32_t frames = Field<int32_t>(anim, 0x20) * Field<int32_t>(anim, 0x1c);
                    Field<float>(Field<uint8_t*>(slots, 0xc), rowOffset) = static_cast<float>(span) / static_cast<float>(frames);
                }
                Field<uint8_t*>(Field<uint8_t*>(slots, 0xc), rowOffset + 8) = anim;
                table = Field<uint8_t*>(slots, 0x10);
                if (Field<uint32_t>(table, 0x28) != 0)
                {
                    uint32_t column = 0;
                    uint32_t columnOffset = 0;
                    do
                    {
                        uint32_t model = orig::fn_0045ad10(Field<uint32_t>(table, 0x34) + columnOffset, ctx);
                        if (model == 0 || (row == static_cast<uint32_t>(selectedRow) && column != Field<uint32_t>(slots, 0)))
                        {
                            model = 0;
                        }
                        else
                        {
                            uint32_t chosen = overrideModel;
                            uint8_t lookup[100];  // original: 36 bytes followed by the 64-byte name buffer
                            if (anim != nullptr && overrideModel == 0)
                            {
                                char animName[64];
                                animName[0] = 0;
                                orig::fn_004588f0(u(anim) + 0x34, u(animName));
                                const char* prefix = GameGlobal<const char*>(0x005fba14);  // "mover_"
                                char want = prefix[0];
                                uint32_t matched = 0;
                                if (static_cast<int32_t>(orig::fn_005388fb(static_cast<uint32_t>(static_cast<int32_t>(animName[0])))) ==
                                    static_cast<int32_t>(want))
                                {
                                    while (want != 0)
                                    {
                                        int32_t c = animName[matched + 1];
                                        want = GameGlobal<const char*>(0x005fba14)[matched + 1];
                                        ++matched;
                                        if (static_cast<int32_t>(orig::fn_005388fb(static_cast<uint32_t>(c))) != static_cast<int32_t>(want))
                                            break;
                                    }
                                }
                                if (matched == static_cast<uint32_t>(strlen(GameGlobal<const char*>(0x005fba14))))
                                {
                                    uint32_t* names = Field<uint32_t*>(Field<uint8_t*>(slots, 0x10), 0x3c);
                                    uint32_t ok = orig::fn_0045dec0(names[0], u(animName), names[2],
                                                                    Field<uint32_t>(GamePtr(), 0xa84), u(lookup));
                                    if (static_cast<uint8_t>(ok) != 0)
                                        chosen = u(lookup);
                                }
                            }
                            model = orig::fn_004ed410(u(anim), chosen, model, 0);
                            if (model == 0)
                            {
                                uint32_t* names = Field<uint32_t*>(Field<uint8_t*>(slots, 0x10), 0x3c);
                                orig::fn_0045df00(names[0], 0x005b09ac /* "needs_models" */, names[2],
                                                  Field<uint32_t>(GamePtr(), 0xa84), u(&chosen));
                            }
                        }
                        Field<uint32_t*>(Field<uint8_t*>(slots, 0x10), 0x38)[flat] = model;
                        ++column;
                        columnOffset += 0x20;
                        ++flat;
                        table = Field<uint8_t*>(slots, 0x10);
                    } while (column < Field<uint32_t>(table, 0x28));
                }
            }
            ++row;
            nameOffset += 0x20;
            rowOffset += 0xc;
        } while (row < Field<uint32_t>(Field<uint8_t*>(slots, 0x10), 0x24));
    }
    if (busy != 0)
    {
        uint32_t r = orig::fn_004e0bd0();
        busy = 0;
        return r;
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------
// 0x0046d780 ([count, owner]): allocates ("particles", tag 1) and zeroes a pool of count 0x6c-byte particles
// after a 0x24-byte header, links them into a doubly linked free list between sentinels at +0xc and +0x14,
// stores owner (+0) and count (+4), then calls 0x0046d810(pool, 1). Returns the pool.
uint8_t* ParticlePool_Create(int32_t count, uint32_t owner)
{
    uint32_t bytes = static_cast<uint32_t>(count) * 0x6c;
    uint32_t size = bytes + 0x24;
    uint8_t* pool = reinterpret_cast<uint8_t*>(HeapAlloc_(1, size, 2, 0x005a03dc /* "particles" */));
    memset(pool, 0, size);
    Field<uint32_t>(pool, 0) = owner;
    uint8_t* tail = pool + 0x14;
    uint8_t* first = pool + 0x24;
    Field<uint8_t*>(pool, 8) = first;
    Field<uint8_t*>(pool, 0x18) = tail;
    Field<uint8_t*>(tail, 0) = tail;
    if (count > 1)
    {
        uint8_t* p = first;
        int32_t n = count - 1;
        do
        {
            --n;
            uint8_t* next = p + 0x6c;
            Field<uint8_t*>(p, 0) = next;
            Field<uint8_t*>(p, 0x70) = p;
            p = next;
        } while (n != 0);
    }
    uint8_t* head = pool + 0xc;
    Field<uint8_t*>(first, 4) = head;
    uint8_t* last = first + bytes - 0x6c;
    Field<uint8_t*>(last, 0) = head;
    Field<uint8_t*>(pool, 0x10) = last;
    Field<uint8_t*>(head, 0) = first;
    Field<int32_t>(pool, 4) = count;
    orig::fn_0046d810(u(pool), 1);
    return pool;
}

// ---------------------------------------------------------------------------------------------------------------
// Body of 0x0046e830 (eax = point count, edx = points, edi = plane {nx, ny, nz, d}; [unused, out]): clips a
// polygon of xyz points against a plane, keeping points with n.p + d >= 0 and inserting the edge crossings;
// returns the output point count in EAX. Not hooked: its generated entry stub restores EAX (the ABI table says
// it returns ST0), which would lose the count that 0x0046ea00 reads.
static uint32_t PathGrid_ClipPolygon(uint32_t count, const float* points, const float* plane, float* out)
{
    const float zero = GameGlobal<float>(0x0056c05c);
    const float one = GameGlobal<float>(0x005bcb2c);
    uint32_t written = 0;
    const float* prev = points + count * 3 - 3;
    float prevDist = prev[2] * plane[2] + prev[1] * plane[1] + prev[0] * plane[0] + plane[3];
    if (count == 0)
        return 0;
    const float* cur = points;
    uint32_t n = count;
    do
    {
        float dist = cur[2] * plane[2] + cur[1] * plane[1] + plane[0] * cur[0] + plane[3];
        bool prevBehind = prevDist < zero;
        bool curBehind = dist < zero;
        if (prevBehind != curBehind)
        {
            float t = prevDist / (prevDist - dist);
            float s = one - t;
            out[0] = s * prev[0] + t * cur[0];
            out[1] = s * prev[1] + t * cur[1];
            out[2] = s * prev[2] + t * cur[2];
            out += 3;
            ++written;
        }
        if (!(dist < zero) && dist == dist)
        {
            out[0] = cur[0];
            out[1] = cur[1];
            out[2] = cur[2];
            out += 3;
            ++written;
        }
        prevDist = dist;
        prev = cur;
        cur += 3;
    } while (--n != 0);
    return written;
}

// ---------------------------------------------------------------------------------------------------------------
// 0x0046e940 (ecx = x; [grid, y, blocked bits, clear, force]): 2-bit cell flags, cell index = grid+4 (width)
// * y + x. force: set bit 1 in `blocked` (clearing bit 0) and bit 0 in grid+0 bits. clear: unless blocked bit 1
// is set, set blocked bit 0 and clear the grid bit. Otherwise set the grid bit unless blocked bit 0 is set.
uint32_t PathGrid_MarkCell(uint32_t x, uint8_t* grid, uint32_t y, uint8_t* blocked, uint32_t clear, uint32_t force)
{
    uint32_t index = Field<uint32_t>(grid, 4) * y + x;
    uint32_t byteIndex = index >> 2;
    uint32_t shift = (index & 3) << 1;
    uint8_t bit0 = static_cast<uint8_t>(1u << shift);
    uint8_t bit1 = static_cast<uint8_t>(2u << shift);
    if (static_cast<uint8_t>(force) != 0)
    {
        blocked[byteIndex] = static_cast<uint8_t>((~bit0 & blocked[byteIndex]) | bit1);
        uint8_t* cell = Field<uint8_t*>(grid, 0) + byteIndex;
        *cell |= bit0;
        return u(cell);
    }
    if (static_cast<uint8_t>(clear) != 0)
    {
        uint8_t b = blocked[byteIndex];
        if (b & bit1)
            return b;
        blocked[byteIndex] = static_cast<uint8_t>(b | bit0);
        uint8_t* cell = Field<uint8_t*>(grid, 0) + byteIndex;
        *cell &= static_cast<uint8_t>(~bit0);
        return u(cell);
    }
    uint32_t mask = 1u << shift;
    if (blocked[byteIndex] & static_cast<uint8_t>(mask))
        return mask;
    uint8_t* cell = Field<uint8_t*>(grid, 0) + byteIndex;
    *cell |= bit0;
    return u(cell);
}

// ---------------------------------------------------------------------------------------------------------------
// 0x0046ea00 (ebx = grid {+0xc originX, +0x10 originY}; [triangle xyz*3, blocked bits, clear, force]): marks
// every 10-unit grid cell the triangle overlaps. Computes the cell bounding box ((p - origin) * 0.1, _ftol);
// a single cell is marked directly (0x0046e940). Otherwise, for each cell (that is free per 0x0046f2e0 unless
// clear/force), the triangle is clipped to the cell's four side planes (0x0046e830) and the cell is marked when
// more than two points remain. Returns the maximum cell x.
uint32_t PathGrid_RasterizeTriangle(uint8_t* grid, const float* tri, uint8_t* blocked, uint32_t clear, uint32_t force)
{
    const float scale = GameGlobal<float>(0x005bcbfc);   // 0.1
    const float twoTo32 = GameGlobal<float>(0x005bcb28);  // 4294967296
    const float cell = GameGlobal<float>(0x005bcb68);     // 10
    const float minusOne = GameGlobal<float>(0x005bcb5c);  // -1

    uint32_t minX = Ftol((tri[0] - Field<float>(grid, 0xc)) * scale);
    uint32_t maxX = minX;
    uint32_t minY = Ftol((tri[1] - Field<float>(grid, 0x10)) * scale);
    uint32_t maxY = minY;
    for (int i = 1; i < 3; ++i)
    {
        uint32_t x = Ftol((tri[i * 3] - Field<float>(grid, 0xc)) * scale);
        if (minX > x)
            minX = x;
        if (maxX < x)
            maxX = x;
        uint32_t y = Ftol((tri[i * 3 + 1] - Field<float>(grid, 0x10)) * scale);
        if (minY > y)
            minY = y;
        if (maxY < y)
            maxY = y;
    }
    if (minX == maxX && minY == maxY)
        return PathGrid_MarkCell(minX, grid, minY, blocked, clear, force);

    for (uint32_t x = minX; x <= maxX; ++x)
    {
        for (uint32_t y = minY; y <= maxY; ++y)
        {
            if (static_cast<uint8_t>(force) == 0 && static_cast<uint8_t>(clear) == 0 &&
                static_cast<uint8_t>(orig::fn_0046f2e0(u(grid), x, y)) == 0)
                continue;
            float poly[30];
            float clipped[30];
            float plane[4];
            memcpy(poly, tri, 36);

            float fx = static_cast<int32_t>(x) < 0
                           ? static_cast<float>(static_cast<double>(static_cast<int32_t>(x)) + static_cast<double>(twoTo32))
                           : static_cast<float>(static_cast<int32_t>(x));
            float vx = fx * cell;
            plane[0] = 1.0f;
            plane[1] = 0.0f;
            plane[2] = 0.0f;
            plane[3] = -(vx + Field<float>(grid, 0xc));
            uint32_t n = PathGrid_ClipPolygon(3, poly, plane, clipped);
            plane[0] = -1.0f;
            plane[1] = 0.0f;
            plane[2] = 0.0f;
            plane[3] = -((vx + Field<float>(grid, 0xc) + cell) * minusOne);
            n = PathGrid_ClipPolygon(n, clipped, plane, poly);

            float fy = static_cast<int32_t>(y) < 0
                           ? static_cast<float>(static_cast<double>(static_cast<int32_t>(y)) + static_cast<double>(twoTo32))
                           : static_cast<float>(static_cast<int32_t>(y));
            float vy = fy * cell;
            plane[0] = 0.0f;
            plane[1] = 1.0f;
            plane[2] = 0.0f;
            plane[3] = -(vy + Field<float>(grid, 0x10));
            n = PathGrid_ClipPolygon(n, poly, plane, clipped);
            plane[0] = 0.0f;
            plane[1] = -1.0f;
            plane[2] = 0.0f;
            plane[3] = -((vy + Field<float>(grid, 0x10) + cell) * minusOne);
            n = PathGrid_ClipPolygon(n, clipped, plane, poly);
            if (n > 2)
                PathGrid_MarkCell(x, grid, y, blocked, clear, force);
        }
    }
    return maxX;
}

// ---------------------------------------------------------------------------------------------------------------
// 0x00488400 (eax = stream, esi = render system; [chunk size]): "RenderSystem::LoadTextures". Reads the hash
// entry count and entries into a new texture hashtable (+0xc/+0x10, fixed up by 0x004f1220), the texture count
// (+0x20), allocates the 0x24-byte texture records (+0x18), pointers (+0x1c), render lists (+0x1c4, +0x1c8) and
// the texture data blob (+0x24, chunk size - entries - 8), then fills each record from the blob (name, data
// after header, flags 0x108, "n_fx_bank_" bit), registers it with renderer slot 73 and gives it a render list
// (vtable 0x0058b0d8). Two extra lists end the table, the last built by 0x004e7570. Returns that last list.
uint32_t RenderSystem_LoadTextures(uint8_t* stream, uint8_t* rs, uint32_t chunkSize)
{
    uint32_t entryBytes = chunkSize;
    Stream_Read(stream, 4, &entryBytes);

    uint32_t* hash = reinterpret_cast<uint32_t*>(HeapAlloc_(0x10, 0x20, 2, 0x0058b5e8));
    if (hash != nullptr)
    {
        hash[0] = 0;
        hash[1] = 0;
        hash[2] = 0;
        hash[4] = 0x00488150;
        hash[5] = u(rs);
        hash[7] = 0;
    }
    Field<uint32_t*>(rs, 0xc) = hash;
    uint32_t entries = HeapAlloc_(0xe, entryBytes, 2, 0x0059daac /* "texture hashtable entries" */);
    Field<uint32_t>(rs, 0x10) = entries;
    Stream_Read(stream, entryBytes, reinterpret_cast<void*>(entries));
    orig::fn_004f1220(Field<uint32_t>(rs, 0x10), Field<uint32_t>(rs, 0xc));
    Stream_Read(stream, 4, &Field<uint32_t>(rs, 0x20));

    Field<uint32_t>(rs, 0x18) = HeapAlloc_(0xe, Field<uint32_t>(rs, 0x20) * 0x24, 2, 0x0059da9c /* "texture list" */);
    Field<uint32_t>(rs, 0x1c) = HeapAlloc_(0xe, Field<uint32_t>(rs, 0x20) << 2, 2, 0x0059da88 /* "texture pointers" */);
    memset(Field<void*>(rs, 0x18), 0, Field<uint32_t>(rs, 0x20) * 0x24);
    memset(Field<void*>(rs, 0x1c), 0, Field<uint32_t>(rs, 0x20) << 2);

    uint32_t* pool = reinterpret_cast<uint32_t*>(HeapAlloc_(0x10, 0x10, 2, 0x0058b5e8));
    if (pool != nullptr)
    {
        pool[0] = 0x18;
        pool[1] = 100;
        pool[2] = 0;
        pool[3] = 0;
    }
    Field<uint32_t*>(rs, 0x1b8) = pool;
    Field<uint32_t>(rs, 0x1c4) = HeapAlloc_(0xe, Field<uint32_t>(rs, 0x20) * 4 + 8, 2, 0x0059da78 /* "render lists" */);
    Field<uint32_t>(rs, 0x1c8) = HeapAlloc_(3, Field<uint32_t>(rs, 0x20) << 4, 2, 0x0059da64 /* "render list info" */);
    memset(Field<void*>(rs, 0x1c4), 0, Field<uint32_t>(rs, 0x20) * 4 + 8);
    memset(Field<void*>(rs, 0x1c8), 0, Field<uint32_t>(rs, 0x20) << 4);

    uint32_t dataBytes = chunkSize - entryBytes - 8;
    uint32_t blob = HeapAlloc_(0xe, dataBytes, 7, 0x0059da48 /* "RenderSystem::LoadTextures" */);
    Field<uint32_t>(rs, 0x24) = blob;
    Stream_Read(stream, dataBytes, reinterpret_cast<void*>(blob));

    uint8_t* data = Field<uint8_t*>(rs, 0x24);
    uint32_t infoOffset = 0;
    for (uint32_t i = 0; i < Field<uint32_t>(rs, 0x20); ++i)
    {
        uint32_t recOffset = i * 0x24;
        Field<uint32_t*>(rs, 0x1c)[i] = Field<uint32_t>(rs, 0x18) + recOffset;
        Field<uint8_t*>(Field<uint8_t*>(rs, 0x18), recOffset) = data;
        Field<uint32_t>(Field<uint8_t*>(rs, 0x18), recOffset + 4) = Field<uint32_t>(data, 0x24) + u(data) + 0x28;
        Field<uint32_t>(Field<uint8_t*>(rs, 0x18), recOffset + 8) = 0;
        uint32_t size = Field<uint32_t>(data, 0x20);
        Field<uint16_t>(Field<uint8_t*>(rs, 0x18), recOffset + 0x22) |= 0x108;
        Field<uint32_t>(Field<uint8_t*>(rs, 0x18), recOffset + 0x1c) = 0xf;
        data += size;
        Field<uint32_t>(Field<uint8_t*>(rs, 0x18), recOffset + 0x18) = 0xf;
        uint32_t name = Field<uint32_t>(Field<uint8_t*>(rs, 0x18), recOffset);
        if (orig::fn_00544957(name, name, 0x0059da3c /* "n_fx_bank_" */, 10) == 0)
            Field<uint8_t>(Field<uint8_t*>(rs, 0x18), recOffset + 0x23) |= 2;
        VCall<uint32_t>(Field<void*>(GamePtr(), 0x38), 0x124 / 4, Field<uint32_t>(rs, 0x18) + recOffset);

        uint32_t* list = reinterpret_cast<uint32_t*>(HeapAlloc_(0x10, 0x14, 2, 0x0058b5e8));
        if (list != nullptr)
        {
            list[0] = 0x0058b0d8;
            list[1] = Field<uint32_t>(GamePtr(), 0xac4);
            list[4] = 0;
            list[2] = 0;
            list[3] = 0;
        }
        Field<uint32_t**>(rs, 0x1c4)[i] = list;
        Field<uint32_t*>(Field<uint8_t*>(rs, 0x1c8), infoOffset + 0xc) = Field<uint32_t**>(rs, 0x1c4)[i];
        infoOffset += 0x10;
    }

    uint32_t* list = reinterpret_cast<uint32_t*>(HeapAlloc_(0x10, 0x14, 2, 0x0058b5e8));
    if (list != nullptr)
    {
        list[0] = 0x0058b0d8;
        list[1] = Field<uint32_t>(GamePtr(), 0xac4);
        list[4] = 0;
        list[2] = 0;
        list[3] = 0;
    }
    Field<uint32_t**>(rs, 0x1c4)[Field<uint32_t>(rs, 0x20)] = list;
    uint32_t last = HeapAlloc_(0x10, 0x18, 2, 0x0058b5e8);
    if (last != 0)
        orig::fn_004e7570(last, Field<uint32_t>(GamePtr(), 0xac4), 0x00487e90);  // keeps EAX = object
    Field<uint32_t*>(rs, 0x1c4)[Field<uint32_t>(rs, 0x20) + 1] = last;
    return last;
}

// ---------------------------------------------------------------------------------------------------------------
// File stream checksum state: +0 marker, +4 position, +8 running value.

// 0x0048a700 (eax = checksum; [unused]): marker 1, position 0, value 0.
void StreamChecksum_Reset(uint8_t* state)
{
    Field<uint32_t>(state, 4) = 0;
    Field<uint32_t>(state, 0) = 1;
    Field<uint32_t>(state, 8) = 0;
}

// 0x0048a720 (esi = checksum; [data, length]): for each byte, value ^= rotl(sign-extended byte, position & 31),
// position++. Returns length.
uint32_t StreamChecksum_Add(uint8_t* state, const uint8_t* data, uint32_t length)
{
    for (uint32_t i = 0; i < length; ++i)
    {
        uint32_t position = Field<uint32_t>(state, 4);
        uint32_t c = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(data[i])));
        uint32_t s = position & 0x1f;
        uint32_t rotated = s == 0 ? c : ((c >> (32 - s)) | (c << s));
        Field<uint32_t>(state, 8) ^= rotated;
        Field<uint32_t>(state, 4) = position + 1;
    }
    return length;
}

// 0x0048a7a0 (ecx = checksum, edx = &file handle): WriteFile of the 4-byte checksum value (+8).
uint32_t StreamChecksum_Write(uint8_t* state, HANDLE* file)
{
    DWORD written = 0;
    return static_cast<uint32_t>(IatWriteFile(*file, state + 8, 4, &written));
}

// 0x0048a7c0 (ecx = checksum, edx = &file handle): ReadFile of 4 bytes into the checksum value (+8).
uint32_t StreamChecksum_Read(uint8_t* state, HANDLE* file)
{
    DWORD read = 0;
    return static_cast<uint32_t>(IatReadFile(*file, state + 8, 4, &read));
}

// 0x0048a7e0 (eax, edx = checksums): 1 when their values (+8) are equal, else 0.
uint32_t StreamChecksum_Equal(const uint8_t* a, const uint8_t* b)
{
    return Field<uint32_t>(b, 8) == Field<uint32_t>(a, 8) ? 1u : 0u;
}

// ---------------------------------------------------------------------------------------------------------------
// File stream: +0 flags (1 write, 2 open, 4 compressed, 8 checksummed, 0x10 checksum seed), +1 name,
// +0x108 handle, +0x10c path, +0x210 zlib stream (version "1.1.4"), +0x248 4 KB buffer, +0x1248 trailer,
// +0x124c checksum.

// 0x0048a8d0 (al = want compression, edi = stream): when flag 4 differs from `want`, starts or ends zlib
// deflate (0x004f6310 / 0x004f68b0, write mode) or inflate (0x004f7620 / 0x004f75d0, read mode) on +0x210 and
// resets its buffer pointers. On success stores flag 4 and clears +0x218/+0x224. AL = 1 on success, 0 on error.
uint32_t FileStream_SetCompression(uint32_t want, uint8_t* stream)
{
    uint8_t wantByte = static_cast<uint8_t>(want);
    uint8_t flags = stream[0];
    if (static_cast<uint8_t>((flags >> 2) & 1) == wantByte)
        return (want & 0xffffff00u) | 1;
    uint8_t* z = stream + 0x210;
    uint32_t r;
    if (flags & 1)
    {
        if (wantByte != 0)
        {
            r = orig::fn_004f6310(0x0058aa08 /* "1.1.4" */, 0xf, u(z), 9, 8, 8, 0, 0x38);
            Field<uint32_t>(z, 0) = 0;
            Field<uint32_t>(stream, 0x214) = 0;
            Field<uint8_t*>(stream, 0x21c) = stream + 0x248;
            Field<uint32_t>(stream, 0x220) = 0x1000;
        }
        else
        {
            r = orig::fn_004f68b0(u(z));
        }
    }
    else if (wantByte != 0)
    {
        r = orig::fn_004f7620(0x0058aa08 /* "1.1.4" */, 0xf, u(z), 0x38);
        Field<uint8_t*>(z, 0) = stream + 0x248;
        Field<uint32_t>(stream, 0x214) = 0;
        Field<uint32_t>(stream, 0x21c) = 0;
        Field<uint32_t>(stream, 0x220) = 0;
    }
    else
    {
        r = orig::fn_004f75d0(u(z));
    }
    if (r != 0)
        return r & 0xffffff00u;
    uint8_t current = stream[0];
    stream[0] = static_cast<uint8_t>(current ^ ((static_cast<uint8_t>(wantByte << 2) ^ current) & 4));
    Field<uint32_t>(stream, 0x224) = 0;
    Field<uint32_t>(stream, 0x218) = 0;
    return 1;
}

// 0x0048a9c0 (ecx unused, edi = stream): for an open stream, write mode writes the filled part of the 4 KB
// buffer (0x1000 - avail_out at +0x220, checksummed first with flag 8) and resets the deflate output; read mode
// with an empty inflate input (+0x214) reads the next 4 KB (checksummed when flag 8 and checksum armed) and
// points +0x210 at it. AL = 0 when not open or at end of file, else 1.
uint32_t FileStream_FlushBuffer(uint8_t* stream)
{
    uint8_t flags = stream[0];
    if (!(flags & 2))
        return 0;
    if (flags & 1)
    {
        uint32_t used = 0x1000 - Field<uint32_t>(stream, 0x220);
        if (used == 0)
            return 1;
        if (flags & 8)
            StreamChecksum_Add(stream + 0x124c, stream + 0x248, used);
        DWORD written = 0;
        IatWriteFile(Field<HANDLE>(stream, 0x108), stream + 0x248, used, &written);
        uint32_t trailer = Field<uint32_t>(stream, 0x218);
        Field<uint8_t*>(stream, 0x21c) = stream + 0x248;
        Field<uint32_t>(stream, 0x1248) = trailer;
        Field<uint32_t>(stream, 0x220) = 0x1000;
        return (trailer & 0xffffff00u) | 1;
    }
    if (Field<uint32_t>(stream, 0x214) != 0)
        return 1;
    DWORD got = 0;
    IatReadFile(Field<HANDLE>(stream, 0x108), stream + 0x248, 0x1000, &got);
    if (got == 0)
        return 0;
    if ((stream[0] & 8) && Field<int32_t>(stream, 0x124c) != -1)
        StreamChecksum_Add(stream + 0x124c, stream + 0x248, got);
    Field<uint32_t>(stream, 0x214) = got;
    Field<uint8_t*>(stream, 0x210) = stream + 0x248;
    return 1;
}

// 0x0048aac0 (eax = stream): opens the stream unless already open (flag 2). Enables compression
// (0x0048a8d0), then write mode creates the file (0x004f1a20), writes the checksum placeholder (flag 8) and a
// zero trailer and arms the checksum; read mode normalises the path (0x004f19d0), opens it with CreateFileA,
// and with flag 8 verifies the stored checksum by reading the whole file through the buffer (on mismatch it
// closes via 0x0048aff0 and fails), rewinds, then reads the trailer. AL = 1 on success.
uint32_t FileStream_Open(uint8_t* stream)
{
    if (stream[0] & 2)
        return (u(stream) & 0xffffff00u) | 1;
    uint32_t r = FileStream_SetCompression((u(stream) & 0xffffff00u) | 1, stream);
    if (static_cast<uint8_t>(r) == 0)
        return r & 0xffffff00u;
    HANDLE* file = &Field<HANDLE>(stream, 0x108);
    DWORD transferred = 0;
    if (stream[0] & 1)
    {
        r = orig::fn_004f1a20(u(stream + 1), 0, u(file), 0);
        if (static_cast<uint8_t>(r) == 0)
            return r & 0xffffff00u;
        uint8_t flags = static_cast<uint8_t>(stream[0] | 2);
        stream[0] = flags;
        if (flags & 8)
            StreamChecksum_Write(stream + 0x124c, file);
        Field<uint32_t>(stream, 0x1248) = 0;
        IatWriteFile(*file, stream + 0x1248, 4, &transferred);
        if (!(stream[0] & 8))
            return 1;
        StreamChecksum_Reset(stream + 0x124c);
        return ((u(stream) + 0x124c) & 0xffffff00u) | 1;
    }

    uint8_t stored[12];
    orig::fn_0048a6e0(u(stored));
    orig::fn_004f19d0(u(stream + 1), 0, u(stream + 0x10c));
    HANDLE h = IatCreateFileA(reinterpret_cast<const char*>(stream + 0x10c), 0x80000000, 1, 3, 0);
    *file = h;
    if (h == INVALID_HANDLE_VALUE)
    {
        *file = nullptr;
        return 0xffffff00u;
    }
    if (h == nullptr)
        return 0;
    uint8_t flags = static_cast<uint8_t>(stream[0] | 2);
    stream[0] = flags;
    if (flags & 8)
    {
        StreamChecksum_Read(stored, file);
        IatReadFile(*file, stream + 0x1248, 4, &transferred);
        uint8_t* checksum = stream + 0x124c;
        StreamChecksum_Reset(checksum);
        if (static_cast<uint8_t>(FileStream_FlushBuffer(stream)) != 0)
        {
            do
            {
                Field<uint32_t>(stream, 0x214) = 0;
            } while (static_cast<uint8_t>(FileStream_FlushBuffer(stream)) != 0);
        }
        orig::fn_0048a770(u(checksum));
        if (static_cast<uint8_t>(StreamChecksum_Equal(checksum, stored)) == 0)
        {
            r = orig::fn_0048aff0(u(stream), 0);
            return r & 0xffffff00u;
        }
        IatSetFilePointer(*file, 0, nullptr, 0);
        StreamChecksum_Read(stored, file);
    }
    IatReadFile(*file, stream + 0x1248, 4, &transferred);
    return 1;
}

// ---------------------------------------------------------------------------------------------------------------
// ABI adapters and hooks.
static uint32_t ModelSlots_Resolve_Abi(uint32_t /*edi*/, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return ModelSlots_Resolve(a0, reinterpret_cast<uint8_t*>(a1), a2, reinterpret_cast<const char*>(a3));
}
static uint32_t ParticlePool_Create_Abi(uint32_t a0, uint32_t a1)
{
    return u(ParticlePool_Create(static_cast<int32_t>(a0), a1));
}
static uint32_t PathGrid_MarkCell_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return PathGrid_MarkCell(ecx, reinterpret_cast<uint8_t*>(a0), a1, reinterpret_cast<uint8_t*>(a2), a3, a4);
}
static uint32_t PathGrid_RasterizeTriangle_Abi(uint32_t ebx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return PathGrid_RasterizeTriangle(reinterpret_cast<uint8_t*>(ebx), reinterpret_cast<const float*>(a0),
                                      reinterpret_cast<uint8_t*>(a1), a2, a3);
}
static uint32_t RenderSystem_LoadTextures_Abi(uint32_t eax, uint32_t /*ecx*/, uint32_t esi, uint32_t a0)
{
    return RenderSystem_LoadTextures(reinterpret_cast<uint8_t*>(eax), reinterpret_cast<uint8_t*>(esi), a0);
}
static uint32_t StreamChecksum_Reset_Abi(uint32_t eax, uint32_t /*a0*/)
{
    StreamChecksum_Reset(reinterpret_cast<uint8_t*>(eax));
    return eax;
}
static uint32_t StreamChecksum_Add_Abi(uint32_t esi, uint32_t a0, uint32_t a1)
{
    return StreamChecksum_Add(reinterpret_cast<uint8_t*>(esi), reinterpret_cast<const uint8_t*>(a0), a1);
}
static uint32_t StreamChecksum_Write_Abi(uint32_t ecx, uint32_t edx)
{
    return StreamChecksum_Write(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<HANDLE*>(edx));
}
static uint32_t StreamChecksum_Read_Abi(uint32_t ecx, uint32_t edx)
{
    return StreamChecksum_Read(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<HANDLE*>(edx));
}
static uint32_t StreamChecksum_Equal_Abi(uint32_t eax, uint32_t edx)
{
    return StreamChecksum_Equal(reinterpret_cast<const uint8_t*>(eax), reinterpret_cast<const uint8_t*>(edx));
}
static uint32_t FileStream_SetCompression_Abi(uint32_t eax, uint32_t edi)
{
    return FileStream_SetCompression(eax, reinterpret_cast<uint8_t*>(edi));
}
static uint32_t FileStream_FlushBuffer_Abi(uint32_t /*ecx*/, uint32_t edi)
{
    return FileStream_FlushBuffer(reinterpret_cast<uint8_t*>(edi));
}
static uint32_t FileStream_Open_Abi(uint32_t eax)
{
    return FileStream_Open(reinterpret_cast<uint8_t*>(eax));
}

ABI_ENTRY_0045bbe0(ModelSlots_Resolve_Entry, ModelSlots_Resolve_Abi)
ABI_ENTRY_0046d780(ParticlePool_Create_Entry, ParticlePool_Create_Abi)
ABI_ENTRY_0046e940(PathGrid_MarkCell_Entry, PathGrid_MarkCell_Abi)
ABI_ENTRY_0046ea00(PathGrid_RasterizeTriangle_Entry, PathGrid_RasterizeTriangle_Abi)
ABI_ENTRY_00488400(RenderSystem_LoadTextures_Entry, RenderSystem_LoadTextures_Abi)
ABI_ENTRY_0048a700(StreamChecksum_Reset_Entry, StreamChecksum_Reset_Abi)
ABI_ENTRY_0048a720(StreamChecksum_Add_Entry, StreamChecksum_Add_Abi)
ABI_ENTRY_0048a7a0(StreamChecksum_Write_Entry, StreamChecksum_Write_Abi)
ABI_ENTRY_0048a7c0(StreamChecksum_Read_Entry, StreamChecksum_Read_Abi)
ABI_ENTRY_0048a7e0(StreamChecksum_Equal_Entry, StreamChecksum_Equal_Abi)
ABI_ENTRY_0048a8d0(FileStream_SetCompression_Entry, FileStream_SetCompression_Abi)
ABI_ENTRY_0048a9c0(FileStream_FlushBuffer_Entry, FileStream_FlushBuffer_Abi)
ABI_ENTRY_0048aac0(FileStream_Open_Entry, FileStream_Open_Abi)

HOOK(0x0045bbe0, "ModelSlots_Resolve", ModelSlots_Resolve_Entry, nullptr);
HOOK(0x0046d780, "ParticlePool_Create", ParticlePool_Create_Entry, nullptr);
HOOK(0x0046e940, "PathGrid_MarkCell", PathGrid_MarkCell_Entry, nullptr);
HOOK(0x0046ea00, "PathGrid_RasterizeTriangle", PathGrid_RasterizeTriangle_Entry, nullptr);
HOOK(0x00488400, "RenderSystem_LoadTextures", RenderSystem_LoadTextures_Entry, nullptr);
HOOK(0x0048a700, "StreamChecksum_Reset", StreamChecksum_Reset_Entry, nullptr);
HOOK(0x0048a720, "StreamChecksum_Add", StreamChecksum_Add_Entry, nullptr);
HOOK(0x0048a7a0, "StreamChecksum_Write", StreamChecksum_Write_Entry, nullptr);
HOOK(0x0048a7c0, "StreamChecksum_Read", StreamChecksum_Read_Entry, nullptr);
HOOK(0x0048a7e0, "StreamChecksum_Equal", StreamChecksum_Equal_Entry, nullptr);
HOOK(0x0048a8d0, "FileStream_SetCompression", FileStream_SetCompression_Entry, nullptr);
HOOK(0x0048a9c0, "FileStream_FlushBuffer", FileStream_FlushBuffer_Entry, nullptr);
HOOK(0x0048aac0, "FileStream_Open", FileStream_Open_Entry, nullptr);
