// Recovered: compressed file close, grid/text/rumble loaders, relocation fixups, async file queue (loader area).
// S2, Approach B.
//   0x0048afb0 CompressedFile_FinishDeflate
//   0x0048aff0 CompressedFile_Close
//   0x0048e750 fn_0048e750
//   0x0048e870 Grid_Init
//   0x00496060 TextData_Load
//   0x004e0fc0 Arena_NewBlock
//   0x004e1320 Reloc_FixupIndexedTable
//   0x004e18a0 MultiIndex_FindOrInsert
//   0x004e2040 Reloc_FixupPointer
//   0x004e2160 Reloc_SwapField
//   0x004e4d90 Rumble_Load
//   0x004e52b0 Rumble_GenerateWave
//   0x004e5cf0 fn_004e5cf0
//   0x004e6190 AsyncQueue_Enter
//   0x004e6250 AsyncQueue_Leave
//   0x004e62c0 AsyncFile_Close
//   0x004e6380 AsyncFile_Open
// Skipped: 0x004e1ad0 reads EDI (the relocation context) which symbols/abi.csv does not list, so its entry stub
// cannot forward it (recovered here only as the internal helper Reloc_FixupList_004e1ad0 that 0x004e1320 calls).
// Skipped: 0x004e1db0 reads a caller-cleaned stack argument that symbols/abi.csv does not list (stack 0).
#include <windows.h>

#include <cstring>

#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "game.h"

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}

uint32_t& D(uint32_t address)
{
    return *reinterpret_cast<uint32_t*>(address);
}

uint16_t& W(uint32_t address)
{
    return *reinterpret_cast<uint16_t*>(address);
}

uint8_t& B(uint32_t address)
{
    return *reinterpret_cast<uint8_t*>(address);
}

// A Win32 import called through the game's IAT slot (so import hooks apply as for the original).
template <class F>
F Import(uintptr_t slot)
{
    return *reinterpret_cast<F*>(slot);
}

const uintptr_t kIatCloseHandle = 0x0056b084;
const uintptr_t kIatQueryPerformanceCounter = 0x0056b08c;
const uintptr_t kIatWaitForSingleObject = 0x0056b0a8;
const uintptr_t kIatEnterCriticalSection = 0x0056b0b0;
const uintptr_t kIatLeaveCriticalSection = 0x0056b0b4;
const uintptr_t kIatSetEvent = 0x0056b0bc;
const uintptr_t kIatCreateFileA = 0x0056b0c0;
const uintptr_t kIatWriteFile = 0x0056b0cc;
const uintptr_t kIatSetFilePointer = 0x0056b0d0;
const uintptr_t kIatFindClose = 0x0056b0dc;

void EnterCs(LPCRITICAL_SECTION cs)
{
    Import<decltype(&::EnterCriticalSection)>(kIatEnterCriticalSection)(cs);
}

void LeaveCs(LPCRITICAL_SECTION cs)
{
    Import<decltype(&::LeaveCriticalSection)>(kIatLeaveCriticalSection)(cs);
}

BOOL SetEventIat(HANDLE h)
{
    return Import<decltype(&::SetEvent)>(kIatSetEvent)(h);
}

BOOL CloseHandleIat(HANDLE h)
{
    return Import<decltype(&::CloseHandle)>(kIatCloseHandle)(h);
}

const uint32_t kHeap = 0x0063a210;                // allocator object passed in ECX to 0x004e07f0
const uint32_t kStrNew = 0x0058b5e8;              // "new"
const uint32_t kStrTemplateAllocate = 0x00590390; // "Template allocate"
const uint32_t kStrTextData = 0x0058f8c8;         // "text data"
const uint32_t kStrHashEntries = 0x0058b26c;      // "input hashtable entries"
const uint32_t kStrRumbleSamples = 0x0058b29c;    // "Rumble Samples"
const uint32_t kStrListData = 0x0058b0e0;         // "PMS_List::m_listData"
const char* const kStrFailedToOpen = reinterpret_cast<const char*>(0x0058b1b4);  // "Failed to open %s\n"

LPCRITICAL_SECTION const kQueueLock = reinterpret_cast<LPCRITICAL_SECTION>(0x00604ae0);
const uint32_t kQueueHead = 0x0063a0dc;

uint32_t Bswap32(uint32_t v)
{
    return (v << 24) | ((v & 0xff00) << 8) | ((v >> 8) & 0xff00) | (v >> 24);
}

void Swap16At(uint32_t address)
{
    uint8_t lo = B(address);
    uint8_t hi = B(address + 1);
    W(address) = static_cast<uint16_t>(hi | (lo << 8));
}

// The key-table binary search of 0x004e0f40, which the loader also inlines: search struct {count, compare,
// index, result, entries, ?, key*}; compare(s) is cdecl and returns <0 / 0 / >0.
void SearchInline(uint32_t s)
{
    uint32_t hi = D(s);
    uint32_t mid = hi >> 1;
    uint32_t lo = 0;
    D(s + 8) = hi;
    if (hi == mid)
        return;
    auto compare = reinterpret_cast<int32_t(__cdecl*)(uint32_t)>(D(s + 4));
    do
    {
        D(s + 8) = mid;
        int32_t r = compare(s);
        D(s + 0xc) = static_cast<uint32_t>(r);
        if (r == 0)
            break;
        if (r < 0)
            hi = D(s + 8);
        else
            lo = D(s + 8);
        mid = ((hi - lo) >> 1) + lo;
    } while (D(s + 8) != mid);
}

// Relocation context {base, relocate-only flag, byte-swap flag}: find the type-table entry for *key in the
// search struct of ctx->base->+8 (+0x1c count, +0x20 must be non-zero, +0x24 search struct); 0 if none.
// useOrig: the original calls 0x004e0f40 here (otherwise it inlines the same search).
uint32_t FindEntry(uint32_t ctx, uint32_t* key, bool useOrig)
{
    uint32_t table = D(D(ctx) + 8);
    uint32_t count = D(table + 0x1c);
    if (count == 0 || D(table + 0x20) == 0)
        return 0;
    uint32_t s = D(table + 0x24);
    D(s) = count;
    D(s + 0x18) = u(key);
    if (useOrig)
        orig::fn_004e0f40(s);
    else
        SearchInline(s);
    if (D(s + 0xc) != 0)
        return 0;
    return D(D(s + 0x10) + D(s + 8) * 4);
}

// Offset of the swappable field described by a type-table entry: entry[off + 4] - entry[off], off = word +2.
uint32_t EntryDelta(uint32_t entry)
{
    uint32_t off = W(entry + 2);
    return D(entry + off + 4) - D(entry + off);
}

enum class SwapKind
{
    Word,       // inline 16-bit swap
    Dword,      // inline 32-bit swap
    OrigDword,  // 32-bit swap through 0x004f60b0
};

// The inlined "swap one field" block: unless relocate-only, locate the field's entry (key = address) and, when
// the context swaps bytes, swap it. Returns true when 0x004f60b0 was called (its result in *result).
bool SwapFieldAt(uint32_t ctx, uint32_t address, SwapKind kind, bool useOrigSearch, uint32_t* result)
{
    if (D(ctx + 4) != 0)
        return false;
    uint32_t key = address;
    uint32_t entry = FindEntry(ctx, &key, useOrigSearch);
    uint32_t target = address + EntryDelta(entry);
    if (D(ctx + 8) == 0)
        return false;
    if (kind == SwapKind::Word)
        Swap16At(target);
    else if (kind == SwapKind::Dword)
        D(target) = Bswap32(D(target));
    else
    {
        *result = orig::fn_004f60b0(target, 0, 0, 0);
        return true;
    }
    return false;
}

// 0x004f1f40 (ecx = find state, esi = path; [out]) has no generated wrapper: same calling shape as orig::.
uint32_t Call_004f1f40(uint32_t ecx_, uint32_t esi_, uint32_t a0)
{
    static const uint32_t target = 0x004f1f40;
    uint32_t lo;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        push a0
        mov ecx, ecx_
        mov esi, esi_
        call dword ptr [target]
        add esp, 4
        pop ebp
        pop edi
        pop esi
        pop ebx
        mov lo, eax
    }
    return lo;
}

// Stream read used by the loaders: stream +0x28 is a reader; unless its flags (+0x10) bit 6 (failed) is set,
// call reader vtable slot 1 (size, buffer); if that sets the failed bit, call the stream's error callback (+0)
// with -1.
void Stream_ReadChecked(uint8_t* stream, uint32_t size, uint32_t buffer)
{
    void* reader = Field<void*>(stream, 0x28);
    if (reader == nullptr || ((Field<uint32_t>(reader, 0x10) >> 6) & 1))
        return;
    VCall<uint32_t>(reader, 1, size, buffer);
    if ((Field<uint32_t>(Field<void*>(stream, 0x28), 0x10) >> 6) & 1)
    {
        auto onError = Field<void(__cdecl*)(int32_t)>(stream, 0);
        if (onError)
            onError(-1);
    }
}

// The sine sample of 0x004e52b0 exactly as the original x87 sequence (fsin is not affected by precision control).
float SineSample(float phase, float amplitude)
{
    float result;
    __asm {
        fld dword ptr phase
        mov eax, 0x005bcba0
        fmul dword ptr [eax]
        fsin
        mov eax, 0x005bcb40
        fadd qword ptr [eax]
        mov eax, 0x005bcc70
        fmul qword ptr [eax]
        fmul dword ptr amplitude
        fstp dword ptr result
    }
    return result;
}
}  // namespace

uint32_t Reloc_FixupPointer(uint32_t* field, uint32_t ctx);
uint32_t Arena_NewBlock(uint8_t* arena);
uint32_t Rumble_GenerateWave(float* out, const uint8_t* desc);
uint32_t AsyncQueue_Enter(uint8_t* task, float delay);
uint32_t AsyncQueue_Leave(uint8_t* task);
uint32_t AsyncFile_Close(uint8_t* file);

// 0x0048afb0 (eax = compressed file; ecx unused): when open for writing (flags bits 0 and 1), run deflate
// (0x004f6630 on the z_stream at +0x210) with Z_FINISH (4) and write the output (0x0048a9c0) until it reports
// the end, then write once more. Returns the last write's result (else the flag byte in AL).
uint32_t CompressedFile_FinishDeflate(uint8_t* f, uint32_t ecx)
{
    uint8_t flags = f[0];
    if (!(flags & 1) || !(flags & 2))
        return (u(f) & 0xffffff00u) | flags;
    uint32_t stream = u(f + 0x210);
    if (orig::fn_004f6630(ecx, stream, 4) == 0)
    {
        do
        {
            orig::fn_0048a9c0(ecx, u(f));
        } while (orig::fn_004f6630(ecx, stream, 4) == 0);
    }
    return orig::fn_0048a9c0(ecx, u(f));
}

// 0x0048aff0 (eax = compressed file; ecx = scratch): close if open (bit 1). Writing (bit 0): finish deflate,
// seek to 0, with bit 3 finalise the checksum (+0x124c, 0x0048a770/0x0048a7a0), write the 4-byte header at
// +0x1248. Then CloseHandle(+0x108), end the stream (0x0048a8d0 with AL = 0) and clear bit 1.
uint32_t CompressedFile_Close(uint8_t* f, uint32_t ecx)
{
    uint8_t flags = f[0];
    if (!(flags & 2))
        return (u(f) & 0xffffff00u) | flags;
    if (flags & 1)
    {
        CompressedFile_FinishDeflate(f, ecx);
        Import<decltype(&::SetFilePointer)>(kIatSetFilePointer)(Field<HANDLE>(f, 0x108), 0, nullptr, 0);
        if (f[0] & 8)
        {
            orig::fn_0048a770(u(f + 0x124c));
            orig::fn_0048a7a0(u(f + 0x124c), u(f + 0x108));
        }
        DWORD written = ecx;  // the original reuses its saved-ECX slot as the byte count
        Import<decltype(&::WriteFile)>(kIatWriteFile)(Field<HANDLE>(f, 0x108), f + 0x1248, 4, &written, nullptr);
    }
    BOOL closed = CloseHandleIat(Field<HANDLE>(f, 0x108));
    uint32_t result = orig::fn_0048a8d0(static_cast<uint32_t>(closed) & 0xffffff00u, u(f));
    f[0] &= 0xfd;
    return result;
}

// 0x0048e750: increments the global counter at 0x0063a174.
void fn_0048e750()
{
    ++GameGlobal<uint32_t>(0x0063a174);
}

// 0x0048e870 (eax = bounds {min xyz, max xyz}; [grid, cell size]): reset the grid (0x0048e970), bump 0x0063a174,
// create its 0x18-byte helper (0x004e7010 with Game+0xac4) at +0x3c, copy the bounds, store the cell size and
// 1/size, cell counts x = (max.x-min.x)/size+1 (+0x20), y likewise (+0x24), cells (+0x30), cells+1 (+0x2c),
// and a zeroed array of cells+1 8-byte slots ("Template allocate") at +0x38. Returns 0.
uint32_t Grid_Init(const uint32_t* bounds, uint8_t* grid, uint32_t cellSizeBits)
{
    orig::fn_0048e970(u(grid));
    fn_0048e750();
    uint32_t object = orig::fn_004e07f0(kHeap, 0x10, 0x18, 2, kStrNew);
    uint32_t helper = 0;
    if (object != 0)
        helper = orig::fn_004e7010(Field<uint32_t>(GameGlobal<void*>(0x005fe490), 0xac4), 0, object);
    float cellSize;
    memcpy(&cellSize, &cellSizeBits, 4);
    float inverse = GameGlobal<float>(0x005bcb2c) / cellSize;
    Field<uint32_t>(grid, 0x3c) = helper;
    for (uint32_t i = 0; i < 6; ++i)
        Field<uint32_t>(grid, i * 4) = bounds[i];
    Field<uint32_t>(grid, 0x18) = cellSizeBits;
    Field<float>(grid, 0x1c) = inverse;
    int32_t nx = static_cast<int32_t>(
                     static_cast<int64_t>((Field<float>(grid, 0xc) - Field<float>(grid, 0)) * inverse)) + 1;
    Field<int32_t>(grid, 0x20) = nx;
    int32_t ny = static_cast<int32_t>(
                     static_cast<int64_t>((Field<float>(grid, 0x10) - Field<float>(grid, 4)) * inverse)) + 1;
    Field<int32_t>(grid, 0x24) = ny;
    uint32_t cells = static_cast<uint32_t>(ny) * static_cast<uint32_t>(nx);
    Field<uint32_t>(grid, 0x30) = cells;
    uint32_t slots = cells + 1;
    uint32_t bytes = slots * 8;
    Field<uint32_t>(grid, 0x28) = 1;
    Field<uint32_t>(grid, 0x2c) = slots;
    uint32_t array = orig::fn_004e07f0(kHeap, 3, bytes, 2, kStrTemplateAllocate);
    Field<uint32_t>(grid, 0x38) = array;
    memset(reinterpret_cast<void*>(array), 0, bytes);
    return 0;
}

// 0x00496060 (eax = stream, edi = text bank; [size]): allocate "text data" (+0xa0) and read the blob. Layout:
// dword length, string block (+0xa4) hashed by 0x004f1220 into a new 0x20-byte table (+0xa8, callback
// 0x00495ee0), then count (+0xac) and an offset table (+0xb0) turned into pointers in place. Returns the count.
uint32_t TextData_Load(uint8_t* stream, uint8_t* text, uint32_t size)
{
    uint32_t blob = orig::fn_004e07f0(kHeap, 6, size, 2, kStrTextData);
    Field<uint32_t>(text, 0xa0) = blob;
    Stream_ReadChecked(stream, size, blob);
    blob = Field<uint32_t>(text, 0xa0);
    uint32_t length = D(blob);
    uint32_t p = blob + 4;
    Field<uint32_t>(text, 0xa4) = p;
    p += length;
    uint32_t table = orig::fn_004e07f0(kHeap, 0x10, 0x20, 2, kStrNew);
    if (table != 0)
    {
        D(table) = 0;
        D(table + 4) = 0;
        D(table + 8) = 0;
        D(table + 0x10) = 0x00495ee0;
        D(table + 0x14) = 0;
        D(table + 0x1c) = 0;
    }
    Field<uint32_t>(text, 0xa8) = table;
    orig::fn_004f1220(Field<uint32_t>(text, 0xa4), table);
    uint32_t count = D(p);
    p += 8;
    Field<uint32_t>(text, 0xac) = count;
    Field<uint32_t>(text, 0xb0) = p;
    uint32_t i = 0;
    if (count != 0)
    {
        do
        {
            uint32_t offset = D(p);
            uint32_t offsets = Field<uint32_t>(text, 0xb0);
            D(offsets + i * 4) = offset + Field<uint32_t>(text, 0xac) * 4 + offsets;
            p += 4;
            ++i;
        } while (i < Field<uint32_t>(text, 0xac));
    }
    return i;
}

// 0x004e0fc0 (esi = arena {allocator, block size, blocks, ?, remaining, cursor}): start a new block: remaining =
// block size, cursor = 0x004e1e90(allocator, size, 1, 0); remaining 0 when that fails. Returns the cursor.
uint32_t Arena_NewBlock(uint8_t* arena)
{
    uint32_t size = Field<uint32_t>(arena, 4);
    Field<uint32_t>(arena, 0xc) = size;
    Field<uint32_t>(arena, 0x10) = size;
    if (size != 0)
        Field<uint32_t>(arena, 0x14) = orig::fn_004e1e90(Field<uint32_t>(arena, 0), size, 1, 0);
    if (Field<uint32_t>(arena, 0x14) == 0)
        Field<uint32_t>(arena, 0x10) = 0;
    return Field<uint32_t>(arena, 0x14);
}

// 0x004e2040 ([field, ctx]): relocate one pointer field of loaded data. Relocate-only context (+4): add the
// base (ctx+0) to a non-null pointer. Otherwise look up the field's and the pointee's type entries and store the
// pointee's adjusted address in the field's slot (byte-swapped when ctx+8); 0 and returns 0 if not found.
uint32_t Reloc_FixupPointer(uint32_t* field, uint32_t ctx)
{
    if (D(ctx + 4) != 0)
    {
        if (*field != 0)
            *field = D(ctx) + *field;
        return 1;
    }
    uint32_t key = u(field);
    uint32_t entry = FindEntry(ctx, &key, true);
    uint32_t delta = EntryDelta(entry);
    key = *field;
    uint32_t target = u(field) + delta;
    entry = FindEntry(ctx, &key, true);
    if (entry != 0)
    {
        uint32_t off = W(entry + 2);
        uint32_t value = D(entry + off + 4) - D(D(ctx)) - D(entry + off) + *field;
        D(target) = value;
        if (D(ctx + 8) != 0)
            D(target) = Bswap32(value);
        return 1;
    }
    D(target) = 0;
    return 0;
}

// 0x004e2160 (edi = ctx; [object, size, unused, unused]): unless relocate-only, locate the object's swappable
// field and, when the context swaps bytes, swap it as 2 or 4 bytes (4 via 0x004f60b0). Returns 1 (0 if
// relocate-only).
uint32_t Reloc_SwapField(uint32_t ctx, uint32_t object, uint32_t size)
{
    if (D(ctx + 4) != 0)
        return 0;
    uint32_t key = object;
    uint32_t entry = FindEntry(ctx, &key, true);
    uint32_t target = object + EntryDelta(entry);
    if (D(ctx + 8) != 0)
    {
        if (size == 2)
            Swap16At(target);
        else if (size == 4)
        {
            orig::fn_004f60b0(target, 0, 0, 0);
            return 1;
        }
    }
    return 1;
}

// 0x004e1ad0 ([list base], edi = ctx) as an internal helper (see the skip note at the top): relocate the ring
// at +0x10, swap each node's dword +4 and words +8/+0xa, relocate +0x20 and +0xc, swap dwords +0x14/+0x18/+0x1c.
static void Reloc_FixupList_004e1ad0(uint32_t base, uint32_t ctx)
{
    uint32_t head = base + 0x10;
    uint32_t node = head;
    do
    {
        Reloc_FixupPointer(reinterpret_cast<uint32_t*>(node), ctx);
        node = D(node);
    } while (node != head);
    uint32_t unused = 0;
    for (node = D(head); node != base + 0x10; node = D(node))
    {
        SwapFieldAt(ctx, node + 4, SwapKind::Dword, true, &unused);
        SwapFieldAt(ctx, node + 8, SwapKind::Word, true, &unused);
        SwapFieldAt(ctx, node + 0xa, SwapKind::Word, true, &unused);
    }
    Reloc_FixupPointer(reinterpret_cast<uint32_t*>(base + 0x20), ctx);
    Reloc_FixupPointer(reinterpret_cast<uint32_t*>(base + 0xc), ctx);
    SwapFieldAt(ctx, base + 0x14, SwapKind::OrigDword, true, &unused);
    SwapFieldAt(ctx, base + 0x18, SwapKind::OrigDword, true, &unused);
    SwapFieldAt(ctx, base + 0x1c, SwapKind::OrigDword, true, &unused);
}

// 0x004e1320 ([table, arg, ctx]): relocate a loaded indexed table: pointers +0, +0x14, +0x24, the list at
// *(+0), then for each of +0x20 rows (+0x24, 0x1c bytes, pointer at +0x10) each of +0x1c record pointers and the
// record's word field (the first processed row also swaps +0x20 words after the record). Relocate-only: call
// 0x004e0f80(table, arg). Finally swap dwords +4..+0x10, +0x18, +0x1c, +0x20. Returns ctx or the last swap.
uint32_t Reloc_FixupIndexedTable(uint32_t table, uint32_t arg, uint32_t ctx)
{
    Reloc_FixupPointer(reinterpret_cast<uint32_t*>(table), ctx);
    Reloc_FixupPointer(reinterpret_cast<uint32_t*>(table + 0x14), ctx);
    Reloc_FixupPointer(reinterpret_cast<uint32_t*>(table + 0x24), ctx);
    Reloc_FixupList_004e1ad0(D(table), ctx);
    uint32_t rows = D(table + 0x24);
    uint32_t remaining = D(table + 0x20);
    if (remaining != 0)
    {
        uint32_t row = rows + 0x10;
        do
        {
            --remaining;
            Reloc_FixupPointer(reinterpret_cast<uint32_t*>(row), ctx);
            uint32_t columns = D(table + 0x1c);
            uint32_t p = D(row);
            if (columns != 0)
            {
                do
                {
                    Reloc_FixupPointer(reinterpret_cast<uint32_t*>(p), ctx);
                    uint32_t record = D(p);
                    if (D(ctx + 4) == 0)
                    {
                        uint32_t key = record;
                        uint32_t entry = FindEntry(ctx, &key, false);
                        uint32_t target = record + EntryDelta(entry);
                        if (D(ctx + 8) != 0)
                            Swap16At(target);
                    }
                    uint32_t n = D(table + 0x20);
                    if (remaining == n - 1)
                    {
                        uint32_t q = D(p) + 2;
                        for (uint32_t j = n; j != 0; --j)
                        {
                            if (D(ctx + 4) == 0)
                            {
                                uint32_t key = q;
                                uint32_t entry = FindEntry(ctx, &key, false);
                                uint32_t target = q + EntryDelta(entry);
                                if (D(ctx + 8) != 0)
                                    Swap16At(target);
                            }
                            q += 2;
                        }
                    }
                    p += 4;
                } while (--columns != 0);
            }
            row += 0x1c;
        } while (remaining != 0);
    }
    if (D(ctx + 4) != 0)
        orig::fn_004e0f80(table, arg);
    static const uint32_t kOffsets[] = {4, 8, 0xc, 0x10, 0x18, 0x1c, 0x20};
    uint32_t result = ctx;
    for (uint32_t offset : kOffsets)
    {
        uint32_t swapped = 0;
        bool called = SwapFieldAt(ctx, table + offset, SwapKind::OrigDword, true, &swapped);
        result = called ? swapped : ctx;
    }
    return result;
}

// 0x004e18a0 ([out, index, keys {size, data}[]]): multi-key index (+0x20 keys, search structs at +0x24, 0x1c
// each). With records (+0x1c), binary-search each key; a hit stores that record and returns 0. Otherwise, if
// under capacity (+0x18), carve a record (word size, word offsets, the key bytes, 4-aligned) from the arena
// (new block 0x004e0fc0 when short, +8 counts blocks), insert it into every key's sorted array, count it and
// return 1 with the record; 0 and null when full or out of memory.
uint32_t MultiIndex_FindOrInsert(uint32_t* out, uint32_t index, uint32_t keys)
{
    uint32_t n = D(index + 0x20);
    if (D(index + 0x1c) != 0)
    {
        uint32_t s = D(index + 0x24);
        if (n != 0)
        {
            uint32_t keyData = keys + 4;
            do
            {
                D(s) = D(index + 0x1c);
                D(s + 0x18) = D(keyData);
                --n;
                SearchInline(s);
                if (D(s + 0xc) == 0)
                {
                    *out = D(D(s + 0x10) + D(s + 8) * 4);
                    return 0;
                }
                keyData += 8;
                s += 0x1c;
            } while (n != 0);
        }
    }
    else if (n != 0)
    {
        uint32_t s = D(index + 0x24) + 0xc;
        do
        {
            D(s - 4) = 0;
            D(s) = 0;
            s += 0x1c;
        } while (--n != 0);
    }

    if (D(index + 0x1c) >= D(index + 0x18))
    {
        *out = 0;
        return 0;
    }
    uint32_t keyCount = D(index + 0x20);
    uint32_t header = (keyCount * 2 + 5) & 0xfffffffcu;
    uint32_t total = header;
    if (keyCount != 0)
    {
        uint32_t s = D(index + 0x24) + 0x14;
        uint32_t k = keys;
        for (uint32_t i = keyCount; i != 0; --i)
        {
            total += (D(k) + D(s) + 3) & 0xfffffffcu;
            s += 0x1c;
            k += 8;
        }
    }
    if (total > D(index + 0x10))
    {
        Arena_NewBlock(reinterpret_cast<uint8_t*>(index));
        ++D(index + 8);
    }
    uint32_t remaining = D(index + 0x10);
    if (total > remaining)
    {
        *out = 0;
        return 0;
    }
    uint32_t record = D(index + 0x14);
    D(index + 0x14) = record + total;
    D(index + 0x10) = remaining - total;
    W(record) = static_cast<uint16_t>(total);
    uint32_t offset = header;
    uint32_t slot = record + 2;
    keyCount = D(index + 0x20);
    if (keyCount != 0)
    {
        uint32_t s = D(index + 0x24) + 8;
        uint32_t k = keys;
        do
        {
            W(slot) = static_cast<uint16_t>(offset);
            uint32_t dst = D(s + 0xc) + record + offset;
            uint32_t src = D(k + 4);
            for (uint32_t i = D(k); i != 0; --i)
                B(dst++) = B(src++);
            offset += (D(k) + D(s + 0xc) + 3) & 0xfffffffcu;
            if (static_cast<int32_t>(D(s + 4)) > 0)
                ++D(s);
            uint32_t count = D(index + 0x1c);
            uint32_t move = count - D(s);
            uint32_t p = D(s + 8) + count * 4;
            for (; move != 0; --move)
            {
                D(p) = D(p - 4);
                p -= 4;
            }
            D(p) = record;
            slot += 2;
            s += 0x1c;
            k += 8;
        } while (--keyCount != 0);
    }
    ++D(index + 0x1c);
    *out = record;
    return 1;
}

// 0x004e4d90 (ecx = stream, edi = rumble bank; [unused]): read the sample count (+0x1c) and hash size, build the
// hash table (+0x20, callback 0x004e4d60) over "input hashtable entries" (+0x24, 0x004f1220), read "Rumble
// Samples" (+8, 0x840 bytes each), create a 0x10-byte pool (+0xc) and a PMS_List (+4, vtable 0x0058b098,
// count/10+1 nodes of 0x1c via 0x004e06f0), then generate both waves of each sample (+0x20 -> +0x48,
// +0x430 -> +0x458). The original's SEH frame only covers an exception in the PMS_List constructor.
uint32_t Rumble_Load(uint8_t* stream, uint8_t* bank)
{
    uint32_t hashSize;
    Stream_ReadChecked(stream, 4, u(bank + 0x1c));
    Stream_ReadChecked(stream, 4, u(&hashSize));
    uint32_t hash = orig::fn_004e07f0(kHeap, 0x10, 0x20, 2, kStrNew);
    if (hash != 0)
    {
        D(hash) = 0;
        D(hash + 4) = 0;
        D(hash + 8) = 0;
        D(hash + 0x10) = 0x004e4d60;
        D(hash + 0x14) = u(bank);
        D(hash + 0x1c) = 0;
    }
    Field<uint32_t>(bank, 0x20) = hash;
    uint32_t entries = orig::fn_004e07f0(kHeap, 2, hashSize, 2, kStrHashEntries);
    Field<uint32_t>(bank, 0x24) = entries;
    Stream_ReadChecked(stream, hashSize, entries);
    orig::fn_004f1220(Field<uint32_t>(bank, 0x24), Field<uint32_t>(bank, 0x20));
    uint32_t samples = orig::fn_004e07f0(kHeap, 0, Field<uint32_t>(bank, 0x1c) * 0x840, 2, kStrRumbleSamples);
    Field<uint32_t>(bank, 8) = samples;
    Stream_ReadChecked(stream, Field<uint32_t>(bank, 0x1c) * 0x840, samples);

    uint32_t pool = orig::fn_004e07f0(kHeap, 0x10, 0x10, 2, kStrNew);
    if (pool != 0)
    {
        D(pool) = 0x24;
        D(pool + 4) = Field<uint32_t>(bank, 0x1c) / 10 + 1;
        D(pool + 8) = 0;
        D(pool + 0xc) = 0;
    }
    Field<uint32_t>(bank, 0xc) = pool;

    uint32_t list = orig::fn_004e07f0(kHeap, 0x10, 0x18, 2, kStrNew);
    uint32_t result = list;
    if (list != 0)
    {
        uint32_t nodes = Field<uint32_t>(bank, 0x1c) / 10 + 1;
        D(list) = 0x0058b098;
        uint32_t data = orig::fn_004e06f0(nodes, kHeap, kStrListData, 0x1c, 2, 5);
        D(list + 8) = data;
        B(list + 4) = 1;
        D(list + 0xc) = 0;
        D(list + 0x10) = 0;
        D(list + 0x14) = 0;
        result = data;
    }
    Field<uint32_t>(bank, 4) = list;

    if (Field<uint32_t>(bank, 0x1c) != 0)
    {
        uint32_t i = 0;
        uint32_t offset = 0;
        do
        {
            uint8_t* sample = Field<uint8_t*>(bank, 8) + offset;
            Rumble_GenerateWave(reinterpret_cast<float*>(sample + 0x48), sample + 0x20);
            Rumble_GenerateWave(reinterpret_cast<float*>(sample + 0x458), sample + 0x430);
            ++i;
            offset += 0x840;
        } while (i < Field<uint32_t>(bank, 0x1c));
        result = i;
    }
    return result;
}

// 0x004e52b0 (ebx = 250 output floats, esi = wave {+0x1c rate, +0x20 amplitude, +0x24 shape}; edi and the
// stack argument unused): fill 250 samples. Shapes: 0 ramp up and 4 ramp down (phase step rate*0.004, wraps at
// 1), 1 sine (degrees from 270, step rate*1.44, wraps at 360, (sin+1)/2*amp), 2 square (toggles between amp
// and 0 each time the accumulator rate*0.008 reaches 1), 3 noise (rand()*amp/32768).
uint32_t Rumble_GenerateWave(float* out, const uint8_t* desc)
{
    const float& kOne = GameGlobal<float>(0x005bcb2c);
    const float& k360 = GameGlobal<float>(0x005bcb88);
    const float& kZero = GameGlobal<float>(0x0056c05c);
    const float& kRampStep = GameGlobal<float>(0x005bcc78);
    const float& kSineStep = GameGlobal<float>(0x005bcc7c);
    const float& kSquareStep = GameGlobal<float>(0x005bcc6c);
    const float& kRandScale = GameGlobal<float>(0x005bcbc0);
    float phase = 0.0f;
    float step = 0.0f;
    switch (Field<uint32_t>(desc, 0x24))
    {
    case 0:
    case 4:
        phase = 0.0f;
        step = Field<float>(desc, 0x1c) * kRampStep;
        break;
    case 1:
        phase = 270.0f;
        step = Field<float>(desc, 0x1c) * kSineStep;
        break;
    case 2:
        phase = Field<float>(desc, 0x20);
        step = 0.0f;
        break;
    default:
        break;
    }
    for (int32_t i = 0; i < 250; ++i)
    {
        switch (Field<uint32_t>(desc, 0x24))
        {
        case 0:
        case 4:
            if (Field<uint32_t>(desc, 0x24) == 0)
                out[i] = phase * Field<float>(desc, 0x20);
            else
                out[i] = (kOne - phase) * Field<float>(desc, 0x20);
            phase = step + phase;
            if (phase > kOne)
                phase = phase - kOne;
            break;
        case 1:
            out[i] = SineSample(phase, Field<float>(desc, 0x20));
            phase = step + phase;
            if (phase > k360)
                phase = phase - k360;
            break;
        case 2:
            out[i] = phase;
            step = Field<float>(desc, 0x1c) * kSquareStep + step;
            if (step >= kOne)
            {
                step = 0.0f;
                if (phase == kZero)
                    phase = Field<float>(desc, 0x20);
                else
                    phase = 0.0f;
            }
            break;
        case 3:
        {
            float amplitude = Field<float>(desc, 0x20);
            int32_t r = static_cast<int32_t>(static_cast<uint32_t>(orig::fn_005391cd()));
            out[i] = static_cast<float>(r) * amplitude * kRandScale;
            break;
        }
        default:
            break;
        }
    }
    return 0;
}

// 0x004e5cf0 (ecx = buffer queue; [out start, byte limit]): if the current slot (+0xd, 12-byte slots at
// +0x20) is complete (+4 == +8), store its start and return the bytes (slot size +8 each) of the run of
// following complete slots contiguous in memory, up to the slot count (+0xc) and the limit (0 = none); else 0.
uint32_t fn_004e5cf0(uint8_t* queue, uint32_t* outStart, uint32_t limit)
{
    uint32_t current = queue[0xd];
    uint32_t slot = u(queue) + 0x20 + current * 12;
    if (D(slot + 4) != D(slot + 8))
        return 0;
    *outStart = D(slot);
    uint32_t i = queue[0xd] + 1u;
    uint32_t stride = Field<uint32_t>(queue, 8);
    uint32_t count = queue[0xc];
    slot += 0xc;
    uint32_t length = stride;
    while (i < count)
    {
        if (limit != 0 && length >= limit)
            break;
        if (D(slot + 4) != D(slot + 8))
            break;
        if (D(slot) != *outStart + length)
            break;
        length += stride;
        ++i;
        slot += 0xc;
    }
    return length;
}

// 0x004e6190 (esi = file task; [delay]): due time (+0x554) = (QPC - start 0x00604ad8) / frequency 0x00604ac8 +
// delay; under lock 0x00604ae0 insert into the queue at 0x0063a0dc (link +0x558) after the head, ordered by due
// time; if it is not the head, wait on its event (+0x540).
uint32_t AsyncQueue_Enter(uint8_t* task, float delay)
{
    LARGE_INTEGER now;
    Import<decltype(&::QueryPerformanceCounter)>(kIatQueryPerformanceCounter)(&now);
    int64_t elapsed = now.QuadPart - GameGlobal<int64_t>(0x00604ad8);
    Field<float>(task, 0x554) =
        static_cast<float>(static_cast<double>(elapsed) / static_cast<double>(GameGlobal<int64_t>(0x00604ac8)) +
                           delay);
    EnterCs(kQueueLock);
    uint32_t* link = &GameGlobal<uint32_t>(kQueueHead);
    uint32_t head = *link;
    if (head != 0)
    {
        uint32_t current = head;
        do
        {
            if (current != head && Field<float>(task, 0x554) < Field<float>(reinterpret_cast<void*>(current), 0x554))
                break;
            link = &D(current + 0x558);
            current = *link;
        } while (current != 0);
    }
    Field<uint32_t>(task, 0x558) = *link;
    *link = u(task);
    bool wait = GameGlobal<uint32_t>(kQueueHead) != u(task);
    LeaveCs(kQueueLock);
    if (wait)
        return Import<decltype(&::WaitForSingleObject)>(kIatWaitForSingleObject)(Field<HANDLE>(task, 0x540),
                                                                                 INFINITE);
    return 0;
}

// 0x004e6250 (esi = file task): under lock 0x00604ae0 unlink the task from the queue at 0x0063a0dc (link
// +0x558) and signal the new head's event (+0x540).
uint32_t AsyncQueue_Leave(uint8_t* task)
{
    EnterCs(kQueueLock);
    uint32_t* link = &GameGlobal<uint32_t>(kQueueHead);
    uint32_t current = *link;
    while (current != 0 && current != u(task))
    {
        link = &D(current + 0x558);
        current = *link;
    }
    *link = Field<uint32_t>(task, 0x558);
    Field<uint32_t>(task, 0x558) = 0;
    uint32_t head = GameGlobal<uint32_t>(kQueueHead);
    if (head != 0)
        SetEventIat(Field<HANDLE>(reinterpret_cast<void*>(head), 0x540));
    LeaveCs(kQueueLock);
    return 0;
}

// 0x004e62c0 (eax = async file): set the stop flag (+0x10 bit 0, lock +0x520) and signal +0x53c. If open
// (bit 8): close the handle (+0xf0); in copy mode 2 close the destination (+0x304) and, when the copied size
// (+0x18) differs from the source size (+0x2fc), delete it (0x004f1ae0 on +0x40c) inside the queue; then clear
// size, bits 7 and 8, and signal again. Returns the last SetEvent result.
uint32_t AsyncFile_Close(uint8_t* file)
{
    LPCRITICAL_SECTION lock = reinterpret_cast<LPCRITICAL_SECTION>(file + 0x520);
    EnterCs(lock);
    Field<uint32_t>(file, 0x10) |= 1;
    LeaveCs(lock);
    BOOL result = SetEventIat(Field<HANDLE>(file, 0x53c));
    if ((Field<uint32_t>(file, 0x10) >> 8) & 1)
    {
        CloseHandleIat(Field<HANDLE>(file, 0xf0));
        if (Field<uint32_t>(file, 0x300) == 2)
        {
            CloseHandleIat(Field<HANDLE>(file, 0x304));
            if (Field<uint32_t>(file, 0x18) != Field<uint32_t>(file, 0x2fc))
            {
                AsyncQueue_Enter(file, Field<float>(file, 0x548));
                GameFunction<uint32_t(__cdecl*)(uint32_t)>(0x004f1ae0)(u(file + 0x40c));
                AsyncQueue_Leave(file);
            }
            Field<uint32_t>(file, 0x300) = 0;
        }
        Field<uint32_t>(file, 0x18) = 0;
        EnterCs(lock);
        Field<uint32_t>(file, 0x10) &= 0xfffffe7fu;
        LeaveCs(lock);
        result = SetEventIat(Field<HANDLE>(file, 0x53c));
    }
    return static_cast<uint32_t>(result);
}

// 0x004e6380 ([async file]): close, then inside the queue: find the source (+0x1f8, 0x004f1f40) to record its
// size (+0x2fc), open it for overlapped reading (+0xf0, name +0xf4). Failure: log "Failed to open %s", set bit 6,
// clear bits 0, 2, 7, 8 and call vtable slot 11 (0). Success: clear bits 0, 2, 6, 7, set bit 8; copy mode 2
// creates the destination (+0x304, name +0x308 from +0x40c). Then +0x548 = +0x54c. Returns 1 if opened.
uint32_t AsyncFile_Open(uint8_t* file)
{
    AsyncFile_Close(file);
    AsyncQueue_Enter(file, Field<float>(file, 0x548));
    alignas(4) uint8_t find[0x248];
    Field<uint32_t>(file, 0x300) = 0;
    if (static_cast<uint8_t>(Call_004f1f40(u(find), u(file + 0x1f8), 0)) != 0)
    {
        Field<uint32_t>(file, 0x2fc) = Field<uint32_t>(find, 0x24);
        Import<decltype(&::FindClose)>(kIatFindClose)(Field<HANDLE>(find, 0));
    }
    uint8_t* source = Field<uint32_t>(file, 0x300) == 1 ? file + 0x40c : file + 0x1f8;
    orig::fn_004f19d0(u(source), 0, u(file + 0xf4));
    auto createFile = Import<decltype(&::CreateFileA)>(kIatCreateFileA);
    HANDLE h = createFile(reinterpret_cast<LPCSTR>(file + 0xf4), 0x80000000u, 1, nullptr, 3, 0x28000000u, nullptr);
    Field<HANDLE>(file, 0xf0) = h;
    if (h == INVALID_HANDLE_VALUE)
        Field<uint32_t>(file, 0xf0) = 0;
    uint32_t opened;
    LPCRITICAL_SECTION lock = reinterpret_cast<LPCRITICAL_SECTION>(file + 0x520);
    if (h == INVALID_HANDLE_VALUE || h == nullptr)
    {
        EnterCs(lock);
        GameLog(kStrFailedToOpen, reinterpret_cast<const char*>(source));
        EnterCs(lock);
        Field<uint32_t>(file, 0x10) |= 0x40;
        LeaveCs(lock);
        EnterCs(lock);
        Field<uint32_t>(file, 0x10) &= 0xfffffe7bu;
        LeaveCs(lock);
        VCall<uint32_t>(file, 11, 0u);
        LeaveCs(lock);
        opened = 0;
    }
    else
    {
        EnterCs(lock);
        Field<uint32_t>(file, 0x10) &= 0xffffff3au;
        LeaveCs(lock);
        EnterCs(lock);
        Field<uint32_t>(file, 0x10) |= 0x100;
        LeaveCs(lock);
        if (Field<uint32_t>(file, 0x300) == 2)
        {
            orig::fn_004f19d0(u(file + 0x40c), 0, u(file + 0x308));
            HANDLE out = createFile(reinterpret_cast<LPCSTR>(file + 0x308), 0x40000000u, 0, nullptr, 2, 0x80, nullptr);
            Field<HANDLE>(file, 0x304) = out;
            if (out == INVALID_HANDLE_VALUE)
            {
                Field<uint32_t>(file, 0x304) = 0;
                Field<uint32_t>(file, 0x300) = 0;
            }
            else if (out == nullptr)
            {
                Field<uint32_t>(file, 0x300) = 0;
            }
        }
        opened = 1;
    }
    AsyncQueue_Leave(file);
    Field<uint32_t>(file, 0x548) = Field<uint32_t>(file, 0x54c);
    return opened;
}

// ---- ABI adapters and hooks ----

static uint32_t CompressedFile_FinishDeflate_Abi(uint32_t eax, uint32_t ecx)
{
    return CompressedFile_FinishDeflate(reinterpret_cast<uint8_t*>(eax), ecx);
}
ABI_ENTRY_0048afb0(CompressedFile_FinishDeflate_Entry, CompressedFile_FinishDeflate_Abi)
HOOK(0x0048afb0, "CompressedFile_FinishDeflate", CompressedFile_FinishDeflate_Entry, nullptr);

static uint32_t CompressedFile_Close_Abi(uint32_t eax, uint32_t ecx)
{
    return CompressedFile_Close(reinterpret_cast<uint8_t*>(eax), ecx);
}
ABI_ENTRY_0048aff0(CompressedFile_Close_Entry, CompressedFile_Close_Abi)
HOOK(0x0048aff0, "CompressedFile_Close", CompressedFile_Close_Entry, nullptr);

static void fn_0048e750_Abi()
{
    fn_0048e750();
}
ABI_ENTRY_0048e750(fn_0048e750_Entry, fn_0048e750_Abi)
HOOK(0x0048e750, "fn_0048e750", fn_0048e750_Entry, nullptr);

static uint32_t Grid_Init_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return Grid_Init(reinterpret_cast<const uint32_t*>(eax), reinterpret_cast<uint8_t*>(a0), a1);
}
ABI_ENTRY_0048e870(Grid_Init_Entry, Grid_Init_Abi)
HOOK(0x0048e870, "Grid_Init", Grid_Init_Entry, nullptr);

static uint32_t TextData_Load_Abi(uint32_t eax, uint32_t edi, uint32_t a0)
{
    return TextData_Load(reinterpret_cast<uint8_t*>(eax), reinterpret_cast<uint8_t*>(edi), a0);
}
ABI_ENTRY_00496060(TextData_Load_Entry, TextData_Load_Abi)
HOOK(0x00496060, "TextData_Load", TextData_Load_Entry, nullptr);

static uint32_t Arena_NewBlock_Abi(uint32_t esi)
{
    return Arena_NewBlock(reinterpret_cast<uint8_t*>(esi));
}
ABI_ENTRY_004e0fc0(Arena_NewBlock_Entry, Arena_NewBlock_Abi)
HOOK(0x004e0fc0, "Arena_NewBlock", Arena_NewBlock_Entry, nullptr);

static uint32_t Reloc_FixupIndexedTable_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Reloc_FixupIndexedTable(a0, a1, a2);
}
ABI_ENTRY_004e1320(Reloc_FixupIndexedTable_Entry, Reloc_FixupIndexedTable_Abi)
HOOK(0x004e1320, "Reloc_FixupIndexedTable", Reloc_FixupIndexedTable_Entry, nullptr);

static uint32_t MultiIndex_FindOrInsert_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return MultiIndex_FindOrInsert(reinterpret_cast<uint32_t*>(a0), a1, a2);
}
ABI_ENTRY_004e18a0(MultiIndex_FindOrInsert_Entry, MultiIndex_FindOrInsert_Abi)
HOOK(0x004e18a0, "MultiIndex_FindOrInsert", MultiIndex_FindOrInsert_Entry, nullptr);

static uint32_t Reloc_FixupPointer_Abi(uint32_t a0, uint32_t a1)
{
    return Reloc_FixupPointer(reinterpret_cast<uint32_t*>(a0), a1);
}
ABI_ENTRY_004e2040(Reloc_FixupPointer_Entry, Reloc_FixupPointer_Abi)
HOOK(0x004e2040, "Reloc_FixupPointer", Reloc_FixupPointer_Entry, nullptr);

static uint32_t Reloc_SwapField_Abi(uint32_t edi, uint32_t a0, uint32_t a1, uint32_t /*a2*/, uint32_t /*a3*/)
{
    return Reloc_SwapField(edi, a0, a1);
}
ABI_ENTRY_004e2160(Reloc_SwapField_Entry, Reloc_SwapField_Abi)
HOOK(0x004e2160, "Reloc_SwapField", Reloc_SwapField_Entry, nullptr);

static uint32_t Rumble_Load_Abi(uint32_t ecx, uint32_t edi, uint32_t /*a0*/)
{
    return Rumble_Load(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<uint8_t*>(edi));
}
ABI_ENTRY_004e4d90(Rumble_Load_Entry, Rumble_Load_Abi)
HOOK(0x004e4d90, "Rumble_Load", Rumble_Load_Entry, nullptr);

static uint32_t Rumble_GenerateWave_Abi(uint32_t ebx, uint32_t esi, uint32_t /*edi*/, uint32_t /*a0*/)
{
    return Rumble_GenerateWave(reinterpret_cast<float*>(ebx), reinterpret_cast<const uint8_t*>(esi));
}
ABI_ENTRY_004e52b0(Rumble_GenerateWave_Entry, Rumble_GenerateWave_Abi)
HOOK(0x004e52b0, "Rumble_GenerateWave", Rumble_GenerateWave_Entry, nullptr);

static uint32_t fn_004e5cf0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_004e5cf0(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<uint32_t*>(a0), a1);
}
ABI_ENTRY_004e5cf0(fn_004e5cf0_Entry, fn_004e5cf0_Abi)
HOOK(0x004e5cf0, "fn_004e5cf0", fn_004e5cf0_Entry, nullptr);

static uint32_t AsyncQueue_Enter_Abi(uint32_t esi, uint32_t a0)
{
    float delay;
    memcpy(&delay, &a0, 4);
    return AsyncQueue_Enter(reinterpret_cast<uint8_t*>(esi), delay);
}
ABI_ENTRY_004e6190(AsyncQueue_Enter_Entry, AsyncQueue_Enter_Abi)
HOOK(0x004e6190, "AsyncQueue_Enter", AsyncQueue_Enter_Entry, nullptr);

static uint32_t AsyncQueue_Leave_Abi(uint32_t esi)
{
    return AsyncQueue_Leave(reinterpret_cast<uint8_t*>(esi));
}
ABI_ENTRY_004e6250(AsyncQueue_Leave_Entry, AsyncQueue_Leave_Abi)
HOOK(0x004e6250, "AsyncQueue_Leave", AsyncQueue_Leave_Entry, nullptr);

static uint32_t AsyncFile_Close_Abi(uint32_t eax)
{
    return AsyncFile_Close(reinterpret_cast<uint8_t*>(eax));
}
ABI_ENTRY_004e62c0(AsyncFile_Close_Entry, AsyncFile_Close_Abi)
HOOK(0x004e62c0, "AsyncFile_Close", AsyncFile_Close_Entry, nullptr);

static uint32_t AsyncFile_Open_Abi(uint32_t a0)
{
    return AsyncFile_Open(reinterpret_cast<uint8_t*>(a0));
}
ABI_ENTRY_004e6380(AsyncFile_Open_Entry, AsyncFile_Open_Abi)
HOOK(0x004e6380, "AsyncFile_Open", AsyncFile_Open_Entry, nullptr);
