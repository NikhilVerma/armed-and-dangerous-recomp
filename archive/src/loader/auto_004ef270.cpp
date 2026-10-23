// Recovered: resource relocation / byte-swap helpers, file I/O wrappers and zlib 1.1.4 deflate/inflate pieces
// (loader area). S2, Approach B.
//   0x004ef270 fn_004ef270
//   0x004ef590 fn_004ef590
//   0x004ef9e0 fn_004ef9e0
//   0x004efec0 VertexStream_ReadVec2
//   0x004f0aa0 Bitmap_ComputeSteps
//   0x004f0cd0 NameDictionary_Add
//   0x004f1a20 File_OpenForWrite
//   0x004f1a60 File_OpenForAppend
//   0x004f1ed0 File_Read
//   0x004f1f00 File_GetSize
//   0x004f6630 Zlib_Deflate
//   0x004f68b0 Zlib_DeflateEnd
//   0x004f69d0 Zlib_LmInit
//   0x004f7590 Zlib_InflateReset
//   0x004f75d0 Zlib_InflateEnd
//   0x004fbae0 fn_004fbae0
//   0x004fbc30 fn_004fbc30
// Skipped:
//   0x004ef620 calls 0x004f0e60, which takes the relocation context in EDI although symbols/abi.csv lists no
//              register inputs, so orig::fn_004f0e60 cannot pass it.
//   0x004f0e60 reads the relocation context from EDI (pushed as an argument at 0x004f0e72) but its ABI lists
//              no register inputs; the generated entry stub would not forward it.
//   0x004f6310 (zlib deflateInit2_) tail-jumps into deflateReset (0x004f6520, not a known function), which calls
//              _tr_init at 0x00501160; that has no orig:: wrapper (no ret found).
//   0x004f7620 (zlib inflateInit2_) passes a stack argument (check function) to 0x00502cb0, whose ABI lists
//              stack 0, so orig::fn_00502cb0 would not pass it.
#include <windows.h>

#include <cstdlib>
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

uint16_t Word(uint32_t address)
{
    return *reinterpret_cast<uint16_t*>(address);
}

uint32_t Dword(uint32_t address)
{
    return *reinterpret_cast<uint32_t*>(address);
}

// Element i of the game's indexed container (count +0x1c, capacity/used +0x20, iterator +0x24 whose +0x10 is
// the element array), or 0 when out of range. Inlined in many loader functions.
uint32_t Container_At(uint32_t container, uint32_t i)
{
    if (i < Dword(container + 0x1c) && Dword(container + 0x20) != 0)
        return Dword(Dword(Dword(container + 0x24) + 0x10) + i * 4);
    return 0;
}

// The relocation context (EAX/EDI in the originals): +0 owner whose +8 is the block lookup table, +4 non-zero
// when the pass must skip byte swapping (returned as is), +8 byte-swap flag.
// Finds the block containing `address`: the table's iterator (+0x24) gets the root (+0x1c) and a pointer to the
// key, then 0x004e0f40 searches; +0xc non-zero means not found, else the block is ((+0x10)[+8]).
uint32_t Reloc_FindBlock(uint32_t ctx, uint32_t address)
{
    uint32_t key = address;
    uint32_t table = Dword(Dword(ctx) + 8);
    uint32_t root = Dword(table + 0x1c);
    if (root == 0 || Dword(table + 0x20) == 0)
        return 0;
    uint32_t it = Dword(table + 0x24);
    Field<uint32_t>(reinterpret_cast<void*>(it), 0) = root;
    Field<uint32_t>(reinterpret_cast<void*>(it), 0x18) = u(&key);
    orig::fn_004e0f40(it);
    if (Dword(it + 0xc) != 0)
        return 0;
    return Dword(Dword(it + 0x10) + Dword(it + 8) * 4);
}

// The address of `address` in the block's other image: block header at block + word(block + 2), whose
// +4 minus +0 is the delta. A missing block (0) faults exactly like the original.
uint32_t Reloc_Translate(uint32_t block, uint32_t address)
{
    uint32_t header = block + Word(block + 2);
    return Dword(header + 4) - Dword(header) + address;
}

// The inline "byte-swap the dword at address" sequence that calls 0x004f60b0. Returns what EAX held afterwards:
// ctx[+4] when set, 0 when swapping is off, else 0x004f60b0's result.
uint32_t Reloc_SwapDword(uint32_t ctx, uint32_t address)
{
    uint32_t skip = Dword(ctx + 4);
    if (skip != 0)
        return skip;
    uint32_t p = Reloc_Translate(Reloc_FindBlock(ctx, address), address);
    if (Dword(ctx + 8) == 0)
        return 0;
    return orig::fn_004f60b0(p, 0, 0, 0);
}

// The same with the swap inlined (bswap): returns ctx[+4] when set, else the translated address.
uint32_t Reloc_SwapDwordInline(uint32_t ctx, uint32_t address)
{
    uint32_t skip = Dword(ctx + 4);
    if (skip != 0)
        return skip;
    uint32_t p = Reloc_Translate(Reloc_FindBlock(ctx, address), address);
    if (Dword(ctx + 8) != 0)
        *reinterpret_cast<uint32_t*>(p) = _byteswap_ulong(Dword(p));
    return p;
}

void Reloc_Swap16(uint32_t ctx, uint32_t address)
{
    orig::fn_004e2160(ctx, address, 2, 0, 0);
}

const char* const kStrReallocNameSpace = reinterpret_cast<const char*>(0x0058ad78);  // "Error : reallocating..."
const char* const kStrAddNewName = reinterpret_cast<const char*>(0x0058ada8);        // "Error : adding new name..."

using CreateFileFn = decltype(&::CreateFileA);
using SetFilePointerFn = decltype(&::SetFilePointer);
using ReadFileFn = decltype(&::ReadFile);
CreateFileFn IatCreateFileA()
{
    return GameGlobal<CreateFileFn>(0x0056b0c0);
}
SetFilePointerFn IatSetFilePointer()
{
    return GameGlobal<SetFilePointerFn>(0x0056b0d0);
}
ReadFileFn IatReadFile()
{
    return GameGlobal<ReadFileFn>(0x0056b0c8);
}

using ZFreeFn = void(__cdecl*)(uint32_t opaque, uint32_t address);
void ZFree(uint8_t* strm, uint32_t address)
{
    Field<ZFreeFn>(strm, 0x24)(Field<uint32_t>(strm, 0x28), address);
}
}  // namespace

uint32_t VertexStream_ReadVec2(const uint8_t* desc, uint8_t* streams, float* out, const float* offset, int32_t index,
                             const float* scale);

// 0x004ef270 (eax = relocation context, ecx = record): relocate and byte-swap one loaded record: pointer fields
// +0x0c/+0x10/+0x14 (0x004e2040), sub-objects by the type word +6 (+0x48..+0x50 via 0x004eeda0), the extra block
// when word +4 is 1, then the words +4..+0xa and dwords +0x18, +0x44. Returns the last swap's EAX.
uint32_t fn_004ef270(uint32_t ctx, uint8_t* rec)
{
    orig::fn_004e2040(u(rec + 0xc), ctx);
    orig::fn_004e2040(u(rec + 0x10), ctx);
    orig::fn_004e2040(u(rec + 0x14), ctx);
    int16_t kind = Field<int16_t>(rec, 6);
    if (kind == 2 || kind == 3)
    {
        orig::fn_004eeda0(ctx, u(rec + 0x48));
        orig::fn_004eeda0(ctx, u(rec + 0x4c));
        orig::fn_004eeda0(ctx, u(rec + 0x50));
    }
    else if (kind == 1)
    {
        orig::fn_004eeda0(ctx, u(rec + 0x4c));
    }
    if (Field<int16_t>(rec, 4) == 1)
    {
        orig::fn_004eeda0(ctx, u(rec + 0xa8));
        orig::fn_004eeda0(ctx, u(rec + 0xac));
        orig::fn_004eeda0(ctx, u(rec + 0xb0));
        Reloc_Swap16(ctx, u(rec + 0x98));
        Reloc_Swap16(ctx, u(rec + 0x9a));
        Reloc_SwapDword(ctx, u(rec + 0x9c));
        Reloc_SwapDword(ctx, u(rec + 0xa0));
        Reloc_SwapDword(ctx, u(rec + 0xa4));
        orig::fn_004eec50(u(rec + 0x54), ctx);
    }
    orig::fn_004ee830(u(rec));
    Reloc_Swap16(ctx, u(rec + 4));
    Reloc_Swap16(ctx, u(rec + 6));
    Reloc_Swap16(ctx, u(rec + 8));
    Reloc_Swap16(ctx, u(rec + 0xa));
    Reloc_SwapDword(ctx, u(rec + 0x18));
    orig::fn_004ee9c0(ctx, u(rec + 0x1c));
    orig::fn_004eea40(ctx, u(rec + 0x28), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    orig::fn_004ee9c0(ctx, u(rec + 0x38));
    return Reloc_SwapDword(ctx, u(rec + 0x44));
}

// 0x004ef590 ([object, callback, user]): for every group in the object's container (+0x28, its +4) and every
// entry of the group's container (+4), call callback(entry value, entry data + 4, user) (cdecl); stop and
// return 0 when it returns 0, else 1.
uint32_t fn_004ef590(uint8_t* object, uint32_t callback, uint32_t user)
{
    auto fn = reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t)>(callback);
    for (uint32_t i = 0;; ++i)
    {
        uint32_t group = Container_At(Dword(Field<uint32_t>(object, 0x28) + 4), i);
        if (group == 0)
            return 1;
        uint32_t list = Dword(group + Word(group + 2));
        for (uint32_t j = 0;; ++j)
        {
            uint32_t entry = Container_At(Dword(list + 4), j);
            if (entry == 0)
                break;
            uint32_t data = entry + Word(entry + 2);
            if (fn(Dword(data), data + 4, user) == 0)
                return 0;
        }
    }
}

// 0x004ef9e0 (eax = mesh, ecx = out (2 dwords), edx = vertex index; [channel]): a vertex's 2-component attribute.
// With a stream block (+0x44): decode it via 0x004efec0 (descriptor +0xd0 + channel*4, streams +0x70, scale at
// (channel+3)*16, offset +8). Else from the channel's index table (+0x60) into the pair array +0x34; else zeros.
uint32_t fn_004ef9e0(uint8_t* mesh, uint32_t* out, uint32_t index, int32_t channel)
{
    uint8_t* block = Field<uint8_t*>(mesh, 0x44);
    if (block)
    {
        uint8_t* scale = block + static_cast<uint32_t>(channel + 3) * 16;
        return VertexStream_ReadVec2(block + static_cast<uint32_t>(channel) * 4 + 0xd0, block + 0x70,
                                   reinterpret_cast<float*>(out), reinterpret_cast<const float*>(scale + 8), index,
                                   reinterpret_cast<const float*>(scale));
    }
    uint32_t table = Field<uint32_t>(mesh, 0x60 + static_cast<uint32_t>(channel) * 4);
    if (table)
    {
        uint32_t slot = Dword(table + index * 4);
        uint32_t pairs = Field<uint32_t>(mesh, 0x34);
        uint32_t first = Dword(pairs + slot * 8);
        out[0] = first;
        out[1] = Dword(pairs + slot * 8 + 4);
        return first;
    }
    out[1] = 0;
    out[0] = 0;
    return u(mesh);
}

// 0x004efec0 (ecx = descriptor {stream, byte offset, type}, edx = streams (20-byte {data, stride}), esi = out,
// edi = offset; [index, scale]): read a 2-component value: types 6-8 two shorts, 10-12 two shorts times
// 1/32767 (0x005bcb78), 14-16 two floats, else zero; then out = out * scale + offset when both are given.
// Returns EAX as left by the original (the element address, or the second dword for float types).
uint32_t VertexStream_ReadVec2(const uint8_t* desc, uint8_t* streams, float* out, const float* offset, int32_t index,
                             const float* scale)
{
    uint8_t* stream = streams + desc[0] * 20u;
    uint32_t p = Field<uint32_t>(stream, 4) * static_cast<uint32_t>(index) + desc[1] + Field<uint32_t>(stream, 0);
    uint32_t result = p;
    switch (desc[2])
    {
    case 14: case 15: case 16:
        Field<uint32_t>(out, 0) = Dword(p);
        result = Dword(p + 4);
        Field<uint32_t>(out, 4) = result;
        break;
    case 10: case 11: case 12:
    {
        const float k = GameGlobal<float>(0x005bcb78);
        out[0] = static_cast<float>(static_cast<int32_t>(*reinterpret_cast<int16_t*>(p))) * k;
        out[1] = static_cast<float>(static_cast<int32_t>(*reinterpret_cast<int16_t*>(p + 2))) * k;
        break;
    }
    case 6: case 7: case 8:
        out[0] = static_cast<float>(static_cast<int32_t>(*reinterpret_cast<int16_t*>(p)));
        out[1] = static_cast<float>(static_cast<int32_t>(*reinterpret_cast<int16_t*>(p + 2)));
        break;
    default:
        Field<uint32_t>(out, 4) = 0;
        Field<uint32_t>(out, 0) = 0;
        break;
    }
    if (scale && offset)
    {
        out[0] = out[0] * scale[0] + offset[0];
        out[1] = scale[1] * out[1] + offset[1];
    }
    return result;
}

// 0x004f0aa0 (eax = bitmap {width, height, x step, row step, start offset, format, flags}, ecx = row bytes or 0):
// x step = bytes per pixel of the format (table 0x005cab60), row step = given or width * that; flag 1 mirrors
// horizontally (negative x step, start at the last column), flag 2 vertically (negative row step, last row).
void Bitmap_ComputeSteps(uint32_t* bmp, uint32_t rowBytes)
{
    uint32_t pixel = GameGlobal<uint32_t>(0x005cab60 + bmp[5] * 4);
    if (rowBytes == 0)
        rowBytes = bmp[0] * pixel;
    uint32_t flags = bmp[6];
    bmp[4] = 0;
    if (flags & 1)
    {
        uint32_t step = 0u - pixel;
        bmp[2] = step;
        bmp[4] = 0u - (bmp[0] - 1) * step;
    }
    else
    {
        bmp[2] = pixel;
    }
    if (flags & 2)
    {
        uint32_t step = 0u - rowBytes;
        bmp[3] = step;
        bmp[4] -= (bmp[1] - 1) * step;
    }
    else
    {
        bmp[3] = rowBytes;
    }
}

// 0x004f0cd0 ([dictionary, name]): find or add a name (key {length with terminator, name, 2, &count +0x10}) in
// the dictionary's table (+4) with 0x004e18a0; when it fails, grow the name space (0x004e1100) and retry,
// logging "reallocating database name space" / "adding new name to dictionary". Counts references when flag +8
// bit 0 is set. Returns the entry or 0.
uint32_t NameDictionary_Add(uint8_t* dict, const char* name)
{
    struct Key
    {
        uint32_t length;
        const char* name;
        uint32_t two;
        uint8_t* counter;
    } key;
    uint32_t length = 0;
    while (name[length++] != 0)
    {
    }
    key.length = length;
    key.name = name;
    key.two = 2;
    key.counter = dict + 0x10;
    uint32_t entry = u(dict);  // the original passes its own argument slot as the out-parameter
    uint32_t* table = reinterpret_cast<uint32_t*>(dict + 4);
    uint32_t found = orig::fn_004e18a0(u(&entry), *table, u(&key));
    if (found == 0)
    {
        if (entry != 0)
            goto counted;
        if (orig::fn_004e1100(0x100, 0, u(table), Field<uint32_t>(dict, 0), 1, 0x400) == 0)
        {
            GameLog(kStrReallocNameSpace, name);
            return entry;
        }
        found = orig::fn_004e18a0(u(&entry), *table, u(&key));
        if (found == 0)
        {
            if (entry != 0)
                goto counted;
            GameLog(kStrAddNewName, name);
            return entry;
        }
    }
    ++Field<uint16_t>(dict, 0x10);
    if (entry == 0)
        return 0;
counted:
    if (Field<uint8_t>(dict, 8) & 1)
        ++*reinterpret_cast<uint16_t*>(entry + Word(entry + 4));
    return entry;
}

// 0x004f1a20 (eax, ecx = name parts for 0x004f19d0, esi = file {handle, path +4}; [unused]): build the path,
// CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, NORMAL). Handle 0 on failure. AL = opened.
uint32_t File_OpenForWrite(uint32_t eax, uint32_t ecx, uint8_t* file, uint32_t /*unused*/)
{
    orig::fn_004f19d0(eax, ecx, u(file + 4));
    HANDLE h = IatCreateFileA()(reinterpret_cast<LPCSTR>(file + 4), 0x40000000, 0, nullptr, 2, 0x80, nullptr);
    Field<uint32_t>(file, 0) = u(h);
    if (h == INVALID_HANDLE_VALUE)
        Field<uint32_t>(file, 0) = 0;
    return (u(h) & 0xffffff00) | (Field<uint32_t>(file, 0) != 0 ? 1u : 0u);
}

// 0x004f1a60 (eax, ecx = name parts for 0x004f19d0, esi = file {handle, path +4}): build the path, open it for
// writing (OPEN_ALWAYS) and move to its end (SetFilePointer dance). Handle 0 on failure. AL = opened.
uint32_t File_OpenForAppend(uint32_t eax, uint32_t ecx, uint8_t* file)
{
    orig::fn_004f19d0(eax, ecx, u(file + 4));
    HANDLE h = IatCreateFileA()(reinterpret_cast<LPCSTR>(file + 4), 0x40000000, 0, nullptr, 4, 0x80, nullptr);
    Field<uint32_t>(file, 0) = u(h);
    if (h == INVALID_HANDLE_VALUE)
    {
        Field<uint32_t>(file, 0) = 0;
        return 0xffffff00;
    }
    if (h == nullptr)
        return 0;
    SetFilePointerFn seek = IatSetFilePointer();
    DWORD current = seek(h, 0, nullptr, FILE_CURRENT);
    seek(Field<HANDLE>(file, 0), 0, nullptr, FILE_END);
    DWORD size = seek(Field<HANDLE>(file, 0), 0, nullptr, FILE_CURRENT);
    seek(Field<HANDLE>(file, 0), static_cast<LONG>(current), nullptr, FILE_BEGIN);
    DWORD last = seek(Field<HANDLE>(file, 0), static_cast<LONG>(size), nullptr, FILE_BEGIN);
    return (last & 0xffffff00) | 1;
}

// 0x004f1ed0 (ecx = byte count, edx = buffer, esi = error flag byte or null; [file, ...]): ReadFile from the
// file's handle (+0); sets the flag when ReadFile fails. Returns the bytes read.
uint32_t File_Read(uint32_t count, void* buffer, uint8_t* failed, uint8_t* file)
{
    DWORD read = count;
    BOOL ok = IatReadFile()(Field<HANDLE>(file, 0), buffer, count, &read, nullptr);
    if (failed)
        *failed = ok == 0 ? 1 : 0;
    return read;
}

// 0x004f1f00 (esi = file): the file's size (seek to the end and back with SetFilePointer).
uint32_t File_GetSize(uint8_t* file)
{
    SetFilePointerFn seek = IatSetFilePointer();
    DWORD current = seek(Field<HANDLE>(file, 0), 0, nullptr, FILE_CURRENT);
    seek(Field<HANDLE>(file, 0), 0, nullptr, FILE_END);
    DWORD size = seek(Field<HANDLE>(file, 0), 0, nullptr, FILE_CURRENT);
    seek(Field<HANDLE>(file, 0), static_cast<LONG>(current), nullptr, FILE_BEGIN);
    return size;
}

// 0x004f6630 (ebx = z_stream; [flush]): zlib 1.1.4 deflate(): checks ("stream error" 0x005caa24, "buffer
// error" 0x005caa30), zlib header, flush_pending (0x004f65c0), the level's compress function (table 0x0057e740),
// _tr_align / _tr_stored_block (0x00502110 / 0x00502070), adler trailer on Z_FINISH.
int32_t Zlib_Deflate(uint8_t* strm, int32_t flush)
{
    if (!strm)
        return -2;
    uint8_t* s = Field<uint8_t*>(strm, 0x1c);
    if (!s || flush > 4 || flush < 0)
        return -2;
    if (Field<uint32_t>(strm, 0xc) == 0 || (Field<uint32_t>(strm, 0) == 0 && Field<uint32_t>(strm, 4) != 0) ||
        (Field<uint32_t>(s, 4) == 0x29a && flush != 4))
    {
        Field<uint32_t>(strm, 0x18) = GameGlobal<uint32_t>(0x005caa24);
        return -2;
    }
    if (Field<uint32_t>(strm, 0x10) == 0)
    {
        Field<uint32_t>(strm, 0x18) = GameGlobal<uint32_t>(0x005caa30);
        return -5;
    }
    int32_t oldFlush = Field<int32_t>(s, 0x20);
    Field<uint8_t*>(s, 0) = strm;
    Field<int32_t>(s, 0x20) = flush;
    if (Field<uint32_t>(s, 4) == 0x2a)
    {
        uint32_t header = ((Field<uint32_t>(s, 0x28) - 8) << 12) + 0x800;
        uint32_t levelFlags = static_cast<uint32_t>((Field<int32_t>(s, 0x7c) - 1) >> 1);
        if (levelFlags > 3)
            levelFlags = 3;
        header |= levelFlags << 6;
        if (Field<uint32_t>(s, 0x64) != 0)
            header |= 0x20;
        Field<uint32_t>(s, 4) = 0x71;
        orig::fn_004f6590(u(s), header - header % 31 + 31);
        if (Field<uint32_t>(s, 0x64) != 0)
        {
            orig::fn_004f6590(u(s), Field<uint16_t>(strm, 0x32));
            orig::fn_004f6590(u(s), Field<uint32_t>(strm, 0x30) & 0xffff);
        }
        Field<uint32_t>(strm, 0x30) = 1;
    }
    if (Field<uint32_t>(s, 0x14) != 0)
    {
        orig::fn_004f65c0(u(strm));
        if (Field<uint32_t>(strm, 0x10) == 0)
        {
            Field<int32_t>(s, 0x20) = -1;
            return 0;
        }
    }
    else if (Field<uint32_t>(strm, 4) == 0 && flush <= oldFlush && flush != 4)
    {
        Field<uint32_t>(strm, 0x18) = GameGlobal<uint32_t>(0x005caa30);
        return -5;
    }
    uint32_t status = Field<uint32_t>(s, 4);
    if (status == 0x29a && Field<uint32_t>(strm, 4) != 0)
    {
        Field<uint32_t>(strm, 0x18) = GameGlobal<uint32_t>(0x005caa30);
        return -5;
    }
    if (Field<uint32_t>(strm, 4) != 0 || Field<uint32_t>(s, 0x6c) != 0 || (flush != 0 && status != 0x29a))
    {
        auto compress = GameGlobal<int32_t(__cdecl*)(uint8_t*, int32_t)>(0x0057e740 + Field<uint32_t>(s, 0x7c) * 12);
        int32_t bstate = compress(s, flush);
        if (bstate == 2 || bstate == 3)
            Field<uint32_t>(s, 4) = 0x29a;
        if (bstate == 0 || bstate == 2)
        {
            if (Field<uint32_t>(strm, 0x10) == 0)
                Field<int32_t>(s, 0x20) = -1;
            return 0;
        }
        if (bstate == 1)
        {
            if (flush == 1)
            {
                orig::fn_00502110(u(s));
            }
            else
            {
                orig::fn_00502070(u(s), 0, 0, 0);
                if (flush == 3)
                {
                    Field<uint16_t*>(s, 0x3c)[Field<uint32_t>(s, 0x44) - 1] = 0;
                    std::memset(Field<uint16_t*>(s, 0x3c), 0, Field<uint32_t>(s, 0x44) * 2 - 2);
                }
            }
            orig::fn_004f65c0(u(strm));
            if (Field<uint32_t>(strm, 0x10) == 0)
            {
                Field<int32_t>(s, 0x20) = -1;
                return 0;
            }
        }
    }
    if (flush != 4)
        return 0;
    if (Field<uint32_t>(s, 0x18) != 0)
        return 1;
    orig::fn_004f6590(u(s), Field<uint16_t>(strm, 0x32));
    orig::fn_004f6590(u(s), Field<uint32_t>(strm, 0x30) & 0xffff);
    orig::fn_004f65c0(u(strm));
    Field<int32_t>(s, 0x18) = -1;
    return Field<uint32_t>(s, 0x14) == 0 ? 1 : 0;
}

// 0x004f68b0 (esi = z_stream): zlib deflateEnd(): unless the state's status (+4) is not INIT/BUSY/FINISH (0x2a,
// 0x71, 0x29a), free pending_buf, head, prev, window and the state through zfree. Z_DATA_ERROR if still busy.
int32_t Zlib_DeflateEnd(uint8_t* strm)
{
    if (!strm)
        return -2;
    uint8_t* s = Field<uint8_t*>(strm, 0x1c);
    if (!s)
        return -2;
    uint32_t status = Field<uint32_t>(s, 4);
    if (status != 0x2a && status != 0x71 && status != 0x29a)
        return -2;
    if (Field<uint32_t>(s, 8) != 0)
        ZFree(strm, Field<uint32_t>(s, 8));
    if (Field<uint32_t>(Field<uint8_t*>(strm, 0x1c), 0x3c) != 0)
        ZFree(strm, Field<uint32_t>(Field<uint8_t*>(strm, 0x1c), 0x3c));
    if (Field<uint32_t>(Field<uint8_t*>(strm, 0x1c), 0x38) != 0)
        ZFree(strm, Field<uint32_t>(Field<uint8_t*>(strm, 0x1c), 0x38));
    if (Field<uint32_t>(Field<uint8_t*>(strm, 0x1c), 0x30) != 0)
        ZFree(strm, Field<uint32_t>(Field<uint8_t*>(strm, 0x1c), 0x30));
    ZFree(strm, Field<uint32_t>(strm, 0x1c));
    Field<uint32_t>(strm, 0x1c) = 0;
    return status == 0x71 ? -3 : 0;
}

// 0x004f69d0 (edx = deflate state): zlib lm_init(): window_size = 2 * w_size, clear the hash heads, copy the
// level's parameters from configuration_table (0x0057e738, 12-byte entries), reset the match state. Returns 2.
uint32_t Zlib_LmInit(uint8_t* s)
{
    Field<uint32_t>(s, 0x34) = Field<uint32_t>(s, 0x24) << 1;
    Field<uint16_t*>(s, 0x3c)[Field<uint32_t>(s, 0x44) - 1] = 0;
    std::memset(Field<uint16_t*>(s, 0x3c), 0, Field<uint32_t>(s, 0x44) * 2 - 2);
    uint32_t config = 0x0057e738 + Field<uint32_t>(s, 0x7c) * 12;
    Field<uint32_t>(s, 0x78) = Word(config + 2);  // max_lazy
    Field<uint32_t>(s, 0x84) = Word(config + 0);  // good_length
    Field<uint32_t>(s, 0x88) = Word(config + 4);  // nice_length
    Field<uint32_t>(s, 0x74) = Word(config + 6);  // max_chain
    Field<uint32_t>(s, 0x64) = 0;
    Field<uint32_t>(s, 0x54) = 0;
    Field<uint32_t>(s, 0x6c) = 0;
    Field<uint32_t>(s, 0x60) = 0;
    Field<uint32_t>(s, 0x40) = 0;
    Field<uint32_t>(s, 0x70) = 2;
    Field<uint32_t>(s, 0x58) = 2;
    return 2;
}

// 0x004f7590 (eax = z_stream): zlib inflateReset(): clear totals and msg, mode = nowrap ? BLOCKS : METHOD,
// inflate_blocks_reset (0x00502c40) of the state's blocks (+0x14).
int32_t Zlib_InflateReset(uint8_t* z)
{
    if (!z)
        return -2;
    uint32_t* state = Field<uint32_t*>(z, 0x1c);
    if (!state)
        return -2;
    Field<uint32_t>(z, 0x14) = 0;
    Field<uint32_t>(z, 8) = 0;
    Field<uint32_t>(z, 0x18) = 0;
    state[0] = state[3] != 0 ? 7 : 0;
    orig::fn_00502c40(0, Field<uint32_t>(Field<uint8_t*>(z, 0x1c), 0x14), u(z));
    return 0;
}

// 0x004f75d0 (eax = z_stream): zlib inflateEnd(): inflate_blocks_free (0x00503880) of the state's blocks, zfree
// the state.
int32_t Zlib_InflateEnd(uint8_t* z)
{
    if (!z || !Field<uint8_t*>(z, 0x1c) || !Field<uint32_t>(z, 0x24))
        return -2;
    uint32_t blocks = Field<uint32_t>(Field<uint8_t*>(z, 0x1c), 0x14);
    if (blocks)
        orig::fn_00503880(blocks, u(z));
    ZFree(z, Field<uint32_t>(z, 0x1c));
    Field<uint32_t>(z, 0x1c) = 0;
    return 0;
}

// 0x004fbae0 (eax = mesh, edi = relocation context): relocate the stream block pointer (+0x44) and, when
// present, its pointers +4, +0x70, +0x84, then byte-swap its dwords +0 and +8. Returns the last swap's EAX.
uint32_t fn_004fbae0(uint8_t* mesh, uint32_t ctx)
{
    uint32_t r = orig::fn_004e2040(u(mesh + 0x44), ctx);
    uint32_t block = Field<uint32_t>(mesh, 0x44);
    if (block == 0)
        return r;
    orig::fn_004e2040(block + 4, ctx);
    orig::fn_004e2040(block + 0x70, ctx);
    orig::fn_004e2040(block + 0x84, ctx);
    Reloc_SwapDwordInline(ctx, block);
    return Reloc_SwapDwordInline(ctx, block + 8);
}

// 0x004fbc30 (esi = record): over 16 four-byte entries at +0x14 build the used mask (+0xc, byte 2 non-zero) and
// the max of bytes 1 + 3 (+4); set +0x10 from table 0x005ca850 by byte +0x4a and +0x11 from the highest used of
// entries 9-12; then +0 = rotating checksum of the 0x154 bytes. Global 0x0063a0b0 picks the follow-up:
// 0 -> 0x004fbe00, 1 -> expand the entries to 16-byte slots at +0x54 (inlined 0x004fbda0), else returns it - 1.
uint32_t fn_004fbc30(uint32_t /*ecx*/, uint8_t* rec)
{
    Field<uint32_t>(rec, 0xc) = 0;
    Field<uint32_t>(rec, 4) = 0;
    for (uint32_t i = 0; i < 16; ++i)
    {
        uint8_t* e = rec + 0x14 + i * 4;
        if (e[2] != 0)
        {
            Field<uint32_t>(rec, 0xc) |= 1u << i;
            uint32_t sum = static_cast<uint32_t>(e[1]) + e[3];
            uint32_t current = Field<uint32_t>(rec, 4);
            if (current > sum)
                sum = current;
            Field<uint32_t>(rec, 4) = sum;
        }
    }
    rec[0x10] = GameGlobal<uint8_t>(0x005ca850 + rec[0x4a] * 4u);
    if (rec[0x46] != 0)
        rec[0x11] = 4;
    else if (rec[0x42] != 0)
        rec[0x11] = 3;
    else if (rec[0x3e] != 0)
        rec[0x11] = 2;
    else
        rec[0x11] = rec[0x3a] != 0 ? 1 : 0;
    Field<uint32_t>(rec, 0) = 0;
    uint32_t hash = 0;
    uint32_t shift = 0;
    for (uint32_t k = 0; k < 0x154; ++k)
    {
        hash = _rotl(hash, static_cast<int>(shift & 0x1f)) + static_cast<uint32_t>(static_cast<int32_t>(
                                                                 static_cast<int8_t>(rec[k])));
        shift = (shift + 7) % 0x18;
    }
    Field<uint32_t>(rec, 0) = hash;
    uint32_t mode = GameGlobal<uint32_t>(0x0063a0b0);
    if (mode == 0)
        return orig::fn_004fbe00(u(rec));
    if (mode != 1)
        return mode - 1;
    uint8_t* src = rec + 0x15;
    uint8_t* dst = rec + 0x58;
    for (int n = 0; n < 16; ++n)
    {
        Field<uint32_t>(dst, static_cast<uint32_t>(-4)) = src[-1];
        Field<uint32_t>(dst, 0) = src[0];
        Field<uint32_t>(dst, 4) = GameGlobal<uint32_t>(0x0057e538 + src[1] * 4u);
        dst[8] = 0;
        dst[9] = 0;
        src += 4;
        dst += 0x10;
    }
    return u(dst);
}

static uint32_t fn_004ef270_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_004ef270(eax, reinterpret_cast<uint8_t*>(ecx));
}
static uint32_t fn_004ef590_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004ef590(reinterpret_cast<uint8_t*>(a0), a1, a2);
}
static uint32_t fn_004ef9e0_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0)
{
    return fn_004ef9e0(reinterpret_cast<uint8_t*>(eax), reinterpret_cast<uint32_t*>(ecx), edx,
                       static_cast<int32_t>(a0));
}
static uint32_t VertexStream_ReadVec2_Abi(uint32_t ecx, uint32_t edx, uint32_t esi, uint32_t edi, uint32_t a0,
                                        uint32_t a1)
{
    return VertexStream_ReadVec2(reinterpret_cast<const uint8_t*>(ecx), reinterpret_cast<uint8_t*>(edx),
                               reinterpret_cast<float*>(esi), reinterpret_cast<const float*>(edi),
                               static_cast<int32_t>(a0), reinterpret_cast<const float*>(a1));
}
static void Bitmap_ComputeSteps_Abi(uint32_t eax, uint32_t ecx)
{
    Bitmap_ComputeSteps(reinterpret_cast<uint32_t*>(eax), ecx);
}
static uint32_t NameDictionary_Add_Abi(uint32_t a0, uint32_t a1)
{
    return NameDictionary_Add(reinterpret_cast<uint8_t*>(a0), reinterpret_cast<const char*>(a1));
}
ABI_ENTRY_004ef270(fn_004ef270_Entry, fn_004ef270_Abi)
ABI_ENTRY_004ef590(fn_004ef590_Entry, fn_004ef590_Abi)
ABI_ENTRY_004ef9e0(fn_004ef9e0_Entry, fn_004ef9e0_Abi)
ABI_ENTRY_004efec0(VertexStream_ReadVec2_Entry, VertexStream_ReadVec2_Abi)
ABI_ENTRY_004f0aa0(Bitmap_ComputeSteps_Entry, Bitmap_ComputeSteps_Abi)
ABI_ENTRY_004f0cd0(NameDictionary_Add_Entry, NameDictionary_Add_Abi)
HOOK(0x004ef270, "fn_004ef270", fn_004ef270_Entry, nullptr);
HOOK(0x004ef590, "fn_004ef590", fn_004ef590_Entry, nullptr);
HOOK(0x004ef9e0, "fn_004ef9e0", fn_004ef9e0_Entry, nullptr);
HOOK(0x004efec0, "VertexStream_ReadVec2", VertexStream_ReadVec2_Entry, nullptr);
HOOK(0x004f0aa0, "Bitmap_ComputeSteps", Bitmap_ComputeSteps_Entry, nullptr);
HOOK(0x004f0cd0, "NameDictionary_Add", NameDictionary_Add_Entry, nullptr);

static uint32_t File_OpenForWrite_Abi(uint32_t eax, uint32_t ecx, uint32_t esi, uint32_t a0)
{
    return File_OpenForWrite(eax, ecx, reinterpret_cast<uint8_t*>(esi), a0);
}
static uint32_t File_OpenForAppend_Abi(uint32_t eax, uint32_t ecx, uint32_t esi)
{
    return File_OpenForAppend(eax, ecx, reinterpret_cast<uint8_t*>(esi));
}
static uint32_t File_Read_Abi(uint32_t ecx, uint32_t edx, uint32_t esi, uint32_t a0, uint32_t /*a1*/,
                              uint32_t /*a2*/, uint32_t /*a3*/, uint32_t /*a4*/)
{
    return File_Read(ecx, reinterpret_cast<void*>(edx), reinterpret_cast<uint8_t*>(esi),
                     reinterpret_cast<uint8_t*>(a0));
}
static uint32_t File_GetSize_Abi(uint32_t esi)
{
    return File_GetSize(reinterpret_cast<uint8_t*>(esi));
}
ABI_ENTRY_004f1a20(File_OpenForWrite_Entry, File_OpenForWrite_Abi)
ABI_ENTRY_004f1a60(File_OpenForAppend_Entry, File_OpenForAppend_Abi)
ABI_ENTRY_004f1ed0(File_Read_Entry, File_Read_Abi)
ABI_ENTRY_004f1f00(File_GetSize_Entry, File_GetSize_Abi)
HOOK(0x004f1a20, "File_OpenForWrite", File_OpenForWrite_Entry, nullptr);
HOOK(0x004f1a60, "File_OpenForAppend", File_OpenForAppend_Entry, nullptr);
HOOK(0x004f1ed0, "File_Read", File_Read_Entry, nullptr);
HOOK(0x004f1f00, "File_GetSize", File_GetSize_Entry, nullptr);

static uint32_t Zlib_Deflate_Abi(uint32_t /*ecx*/, uint32_t ebx, uint32_t flush)
{
    return static_cast<uint32_t>(Zlib_Deflate(reinterpret_cast<uint8_t*>(ebx), static_cast<int32_t>(flush)));
}
static uint32_t Zlib_DeflateEnd_Abi(uint32_t esi)
{
    return static_cast<uint32_t>(Zlib_DeflateEnd(reinterpret_cast<uint8_t*>(esi)));
}
static uint32_t Zlib_LmInit_Abi(uint32_t edx)
{
    return Zlib_LmInit(reinterpret_cast<uint8_t*>(edx));
}
static uint32_t Zlib_InflateReset_Abi(uint32_t eax)
{
    return static_cast<uint32_t>(Zlib_InflateReset(reinterpret_cast<uint8_t*>(eax)));
}
static uint32_t Zlib_InflateEnd_Abi(uint32_t eax)
{
    return static_cast<uint32_t>(Zlib_InflateEnd(reinterpret_cast<uint8_t*>(eax)));
}
ABI_ENTRY_004f6630(Zlib_Deflate_Entry, Zlib_Deflate_Abi)
ABI_ENTRY_004f68b0(Zlib_DeflateEnd_Entry, Zlib_DeflateEnd_Abi)
ABI_ENTRY_004f69d0(Zlib_LmInit_Entry, Zlib_LmInit_Abi)
ABI_ENTRY_004f7590(Zlib_InflateReset_Entry, Zlib_InflateReset_Abi)
ABI_ENTRY_004f75d0(Zlib_InflateEnd_Entry, Zlib_InflateEnd_Abi)
HOOK(0x004f6630, "Zlib_Deflate", Zlib_Deflate_Entry, nullptr);
HOOK(0x004f68b0, "Zlib_DeflateEnd", Zlib_DeflateEnd_Entry, nullptr);
HOOK(0x004f69d0, "Zlib_LmInit", Zlib_LmInit_Entry, nullptr);
HOOK(0x004f7590, "Zlib_InflateReset", Zlib_InflateReset_Entry, nullptr);
HOOK(0x004f75d0, "Zlib_InflateEnd", Zlib_InflateEnd_Entry, nullptr);

static uint32_t fn_004fbae0_Abi(uint32_t eax, uint32_t edi)
{
    return fn_004fbae0(reinterpret_cast<uint8_t*>(eax), edi);
}
static uint32_t fn_004fbc30_Abi(uint32_t ecx, uint32_t esi)
{
    return fn_004fbc30(ecx, reinterpret_cast<uint8_t*>(esi));
}
ABI_ENTRY_004fbae0(fn_004fbae0_Entry, fn_004fbae0_Abi)
ABI_ENTRY_004fbc30(fn_004fbc30_Entry, fn_004fbc30_Abi)
HOOK(0x004fbae0, "fn_004fbae0", fn_004fbae0_Entry, nullptr);
HOOK(0x004fbc30, "fn_004fbc30", fn_004fbc30_Entry, nullptr);
