// Recovered: 0x00500d60-0x00501880. Despite the address range (between renderer code), these are a padded
// stream read, a RIFF/WAVE file reader and zlib 1.1.4 trees.c (deflate Huffman trees). S2, Approach B.
//   0x00500d60 fn_00500d60
//   0x00500e10 RiffChunk_Find
//   0x00500ed0 RiffChunk_Read
//   0x00500f10 WaveFile_Init
//   0x00500ff0 WaveFile_Open
//   0x00501100 WaveFile_ReadData
//   0x00501160 Zlib_TrInit
//   0x005011c0 Zlib_InitBlock
//   0x00501230 Zlib_PqDownHeap
//   0x00501300 Zlib_GenBitlen
//   0x005015a0 Zlib_BuildTree
// Implemented but not hooked:
//   0x00501510 Zlib_GenCodes: reads the tree in EDI, which symbols/abi.csv does not list (it lists EBP, which is
//              only saved; the `mov edi, edi` padding at 0x0050152e hides the read), so the generated entry stub
//              cannot hand it over. Zlib_BuildTree calls the C++ version directly.
// Skipped:
//   0x005017a0 (zlib scan_tree) and 0x00501880 (zlib send_tree) read a caller-cleaned stack argument (the deflate
//              state at [esp+4] in scan_tree, max_code in send_tree) that symbols/abi.csv lists as "stack 0", so the
//              generated entry stub does not pass it.
//
// Deflate state offsets (zlib 1.1.4 deflate_state, confirmed by HEAP_SIZE 0x23d = 573, L_CODES 0x11e = 286,
// D_CODES 0x1e, BL_CODES 0x13, dyn_ltree[256].Freq = 1 at +0x48c): +0x08 pending_buf, +0x14 pending,
// +0x8c dyn_ltree, +0x980 dyn_dtree, +0xa74 bl_tree, +0xb10/+0xb1c/+0xb28 l_desc/d_desc/bl_desc, +0xb34 bl_count,
// +0xb54 heap, +0x1448 heap_len, +0x144c heap_max, +0x1450 depth, +0x1698 last_lit, +0x16a0 opt_len,
// +0x16a4 static_len, +0x16a8 matches, +0x16ac last_eob_len, +0x16b0 bi_buf, +0x16b4 bi_valid.
#include <windows.h>

#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "renderer.h"

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}

using SetFilePointerFn = decltype(&::SetFilePointer);
using ReadFileFn = decltype(&::ReadFile);
using CloseHandleFn = decltype(&::CloseHandle);
SetFilePointerFn IatSetFilePointer()
{
    return GameGlobal<SetFilePointerFn>(0x0056b0d0);
}
ReadFileFn IatReadFile()
{
    return GameGlobal<ReadFileFn>(0x0056b0c8);
}
CloseHandleFn IatCloseHandle()
{
    return GameGlobal<CloseHandleFn>(0x0056b084);
}

constexpr uint32_t kFourccRiff = 0x46464952;  // 'RIFF'
constexpr uint32_t kFourccFmt = 0x20746d66;   // 'fmt '
constexpr uint32_t kFourccData = 0x61746164;  // 'data'
constexpr uint32_t kFourccList = 0x5453494c;  // 'LIST'
constexpr uint32_t kFourccSmpl = 0x6c706d73;  // 'smpl'
constexpr uint32_t kFourccCue = 0x20657563;   // 'cue '
constexpr uint32_t kFourccAdtl = 0x6c746461;  // 'adtl'
constexpr uint32_t kFourccWave = 0x45564157;  // 'WAVE'

// zlib ct_data: Freq/Code at +0, Dad/Len at +2.
struct ZlibCtData
{
    uint16_t freq;
    uint16_t len;
};

constexpr int32_t kHeapSize = 0x23d;  // 2 * L_CODES + 1

int32_t* ZHeap(uint8_t* s)
{
    return &Field<int32_t>(s, 0xb54);
}
uint8_t* ZDepth(uint8_t* s)
{
    return s + 0x1450;
}
uint16_t* ZBlCount(uint8_t* s)
{
    return &Field<uint16_t>(s, 0xb34);
}
int32_t& ZHeapLen(uint8_t* s)
{
    return Field<int32_t>(s, 0x1448);
}
int32_t& ZHeapMax(uint8_t* s)
{
    return Field<int32_t>(s, 0x144c);
}
uint32_t& ZOptLen(uint8_t* s)
{
    return Field<uint32_t>(s, 0x16a0);
}
uint32_t& ZStaticLen(uint8_t* s)
{
    return Field<uint32_t>(s, 0x16a4);
}
}  // namespace

// 0x00500d60 (esi = stream; [unused, position, buffer, count, out bytes], callee pops): read up to `count` bytes
// from the stream's source object (+0x14): virtual 3 (+0xc) gives the bytes available, virtual 1 (+4) reads
// (count, buffer). Sets +0x18 = position + read and *out = read. When the source reports its end (virtual 13,
// +0x34, bool) and everything available was read, the rest of the buffer is zero-filled, counted into +0x18,
// *out and +0x1c, and flag 0x1000 is set in +0 once +0x1c reaches +0x10; returns 1. Otherwise returns whether
// the whole count was read. Count 0 returns 1 at once (the original leaves EAX's upper bytes as they were).
uint32_t fn_00500d60(uint8_t* stream, uint32_t /*unused*/, uint32_t position, uint8_t* buffer, uint32_t count,
                     uint32_t* outBytes)
{
    if (count == 0)
        return 1;
    void* source = Field<void*>(stream, 0x14);
    uint8_t atEnd = VCall<uint8_t>(source, 0x34 / 4);
    uint32_t available = VCall<uint32_t>(Field<void*>(stream, 0x14), 0xc / 4);
    uint32_t want = available < count ? available : count;
    uint32_t read = VCall<uint32_t>(Field<void*>(stream, 0x14), 4 / 4, want, buffer);
    bool pad = atEnd != 0 && available == read;
    uint32_t remaining = count - read;
    Field<uint32_t>(stream, 0x18) = position + read;
    *outBytes = read;
    if (!pad || remaining == 0)
        return remaining == 0 ? 1 : 0;
    memset(buffer + read, 0, remaining);
    Field<uint32_t>(stream, 0x18) += remaining;
    *outBytes += remaining;
    uint32_t produced = Field<uint32_t>(stream, 0x1c) + remaining;
    Field<uint32_t>(stream, 0x1c) = produced;
    if (produced >= Field<uint32_t>(stream, 0x10))
        Field<uint32_t>(stream, 0) |= 0x1000;
    return (produced & 0xffffff00u) | 1;
}

// 0x00500e10 (esi = chunk record; [list type or 0], callee pops) (not executed in the comparison scenarios):
// find a RIFF chunk. A record (0x18 bytes) is: +0 FOURCC, +4 parent record, +8 data offset, +0xc size,
// +0x10 flags, +0x14 file (HANDLE at +0). Scanning starts at the parent's data (+4 more past the form type when
// the parent is 'RIFF'), or offset 0 without a parent, and reads 8-byte headers (SetFilePointer, ReadFile) until
// the id matches; with a list type, the 4 bytes after the header must match it too. Found: stores data offset and
// size, sets flag 1, returns 1. A short header read returns 0.
uint32_t RiffChunk_Find(uint8_t* chunk, uint32_t listType)
{
    uint8_t* parent = Field<uint8_t*>(chunk, 4);
    LONG offset = 0;
    if (parent)
    {
        offset = Field<LONG>(parent, 8);
        if (Field<uint32_t>(parent, 0) == kFourccRiff)
            offset += 4;
    }
    uint32_t type = 0;  // the original's stack slot keeps the previous iteration's value
    for (;;)
    {
        IatSetFilePointer()(*Field<HANDLE*>(chunk, 0x14), offset, nullptr, FILE_BEGIN);
        uint32_t header[2];
        DWORD got;
        IatReadFile()(*Field<HANDLE*>(chunk, 0x14), header, 8, &got, nullptr);
        if (got != 8)
            return 0;
        if (Field<uint32_t>(chunk, 0) == header[0])
        {
            Field<LONG>(chunk, 8) = offset + 8;
            Field<uint32_t>(chunk, 0xc) = header[1];
            if (listType == 0)
                break;
            DWORD gotType;
            IatReadFile()(*Field<HANDLE*>(chunk, 0x14), &type, 4, &gotType, nullptr);
            if (type == listType)
                break;
        }
        offset = offset + header[1] + 8;
    }
    Field<uint32_t>(chunk, 0x10) |= 1;
    return 1;
}

// 0x00500ed0 (eax = chunk record, edx = offset in the chunk, edi = byte count; [buffer], callee pops)
// (not executed in the comparison scenarios): SetFilePointer to the chunk's data offset (+8) + offset, ReadFile
// count bytes. Returns whether all of them were read.
uint32_t RiffChunk_Read(uint8_t* chunk, uint32_t offset, uint32_t count, void* buffer)
{
    IatSetFilePointer()(*Field<HANDLE*>(chunk, 0x14), static_cast<LONG>(Field<uint32_t>(chunk, 8) + offset), nullptr,
                        FILE_BEGIN);
    DWORD got;
    IatReadFile()(*Field<HANDLE*>(chunk, 0x14), buffer, count, &got, nullptr);
    return got == count ? 1 : 0;
}

// 0x00500f10 (edx = wave file) (not executed in the comparison scenarios): zero the wave file object: seven chunk
// records (+0x00-+0xa4), the file (+0xa8, 0x42 dwords) and +0x1b0-+0x1b8. Returns the object.
uint8_t* WaveFile_Init(uint8_t* wave)
{
    for (uint32_t off = 0; off < 0xa8; off += 4)
        Field<uint32_t>(wave, off) = 0;
    for (uint32_t i = 0; i < 0x42; ++i)
        Field<uint32_t>(wave, 0xa8 + i * 4) = 0;
    Field<uint32_t>(wave, 0x1b0) = 0;
    Field<uint32_t>(wave, 0x1b4) = 0;
    Field<uint32_t>(wave, 0x1b8) = 0;
    return wave;
}

// 0x00500ff0 (ebx = wave file; [path], callee pops) (not executed in the comparison scenarios): CloseHandle the
// file (+0xa8), open the path with 0x004f1c90 (esi = file), then set up the chunk records: 'RIFF' (+0x00, no
// parent), 'fmt ' (+0x18), 'data' (+0x30), 'LIST' (+0x60), 'smpl' (+0x78), 'cue ' (+0x90), all children of the
// RIFF record and reading the same file. RIFF, fmt and data must be found; LIST (type 'adtl'), smpl and cue are
// optional. Returns whether the RIFF form type is 'WAVE' (the original leaves the form type's upper bytes in EAX).
uint32_t WaveFile_Open(uint8_t* wave, uint32_t path)
{
    uint8_t* file = wave + 0xa8;
    IatCloseHandle()(Field<HANDLE>(wave, 0xa8));
    uint32_t r = orig::fn_004f1c90(u(file), path);
    if (static_cast<uint8_t>(r) == 0)
        return r & 0xffffff00u;
    Field<uint8_t*>(wave, 0x14) = file;
    Field<uint32_t>(wave, 0x00) = kFourccRiff;
    Field<uint32_t>(wave, 0x04) = 0;
    Field<uint8_t*>(wave, 0x2c) = file;
    Field<uint32_t>(wave, 0x18) = kFourccFmt;
    Field<uint8_t*>(wave, 0x1c) = wave;
    Field<uint8_t*>(wave, 0x44) = file;
    Field<uint32_t>(wave, 0x30) = kFourccData;
    Field<uint8_t*>(wave, 0x34) = wave;
    Field<uint8_t*>(wave, 0x74) = file;
    Field<uint32_t>(wave, 0x60) = kFourccList;
    Field<uint8_t*>(wave, 0x64) = wave;
    Field<uint8_t*>(wave, 0x8c) = file;
    Field<uint32_t>(wave, 0x78) = kFourccSmpl;
    Field<uint8_t*>(wave, 0x7c) = wave;
    Field<uint8_t*>(wave, 0xa4) = file;
    Field<uint32_t>(wave, 0x90) = kFourccCue;
    Field<uint8_t*>(wave, 0x94) = wave;
    r = RiffChunk_Find(wave, 0);
    if (!r)
        return 0;
    r = RiffChunk_Find(wave + 0x18, 0);
    if (!r)
        return 0;
    r = RiffChunk_Find(wave + 0x30, 0);
    if (!r)
        return 0;
    RiffChunk_Find(wave + 0x60, kFourccAdtl);
    RiffChunk_Find(wave + 0x78, 0);
    RiffChunk_Find(wave + 0x90, 0);
    uint32_t formType = path;  // the original reads into its argument slot; a failed read leaves the path there
    RiffChunk_Read(wave, 0, 4, &formType);
    return (formType & 0xffffff00u) | (formType == kFourccWave ? 1u : 0u);
}

// 0x00501100 (eax = byte count, ecx = offset, edx = wave file, ebx = out count or null; [buffer], callee pops)
// (not executed in the comparison scenarios): read from the 'data' chunk (offset +0x38, size +0x3c, file +0x44),
// clamping the count to the chunk's end. Stores the clamped count in *ebx. Returns whether it was all read
// (1 for a zero count, with the original's upper EAX bytes from the size).
uint32_t WaveFile_ReadData(uint32_t count, uint32_t offset, uint8_t* wave, uint32_t* outCount, void* buffer)
{
    uint32_t size = Field<uint32_t>(wave, 0x3c);
    uint32_t eax = size;
    if (offset + count > size)
    {
        eax = size - offset;
        count = eax;
    }
    uint32_t result = (eax & 0xffffff00u) | 1;
    if (count != 0)
    {
        IatSetFilePointer()(*Field<HANDLE*>(wave, 0x44), static_cast<LONG>(Field<uint32_t>(wave, 0x38) + offset),
                            nullptr, FILE_BEGIN);
        DWORD got;
        IatReadFile()(*Field<HANDLE*>(wave, 0x44), buffer, count, &got, nullptr);
        result = got == count ? 1 : 0;
    }
    if (outCount)
        *outCount = count;
    return result;
}

// 0x005011c0 (edx = deflate state): zlib 1.1.4 init_block(): clear the Freq of dyn_ltree (286), dyn_dtree (30) and
// bl_tree (19), set dyn_ltree[END_BLOCK].Freq = 1 (+0x48c), clear opt_len, static_len, last_lit, matches.
// Returns the address after bl_tree (what the loop leaves in EAX).
uint32_t Zlib_InitBlock(uint8_t* s)
{
    for (int i = 0; i < 0x11e; ++i)
        Field<uint16_t>(s, 0x8c + i * 4) = 0;
    for (int i = 0; i < 0x1e; ++i)
        Field<uint16_t>(s, 0x980 + i * 4) = 0;
    for (int i = 0; i < 0x13; ++i)
        Field<uint16_t>(s, 0xa74 + i * 4) = 0;
    Field<uint32_t>(s, 0x16a4) = 0;
    Field<uint32_t>(s, 0x16a0) = 0;
    Field<uint32_t>(s, 0x16a8) = 0;
    Field<uint32_t>(s, 0x1698) = 0;
    Field<uint16_t>(s, 0x48c) = 1;
    return u(s + 0xa74 + 0x13 * 4);
}

// 0x00501160 (edx = deflate state): zlib 1.1.4 _tr_init(): l_desc/d_desc/bl_desc dyn_tree = dyn_ltree, dyn_dtree,
// bl_tree with stat_desc static_l_desc 0x005ca7a4, static_d_desc 0x005ca7b8, static_bl_desc 0x005ca7cc;
// bi_buf = bi_valid = 0, last_eob_len = 8, then init_block (tail jump to 0x005011c0).
uint32_t Zlib_TrInit(uint8_t* s)
{
    Field<uint8_t*>(s, 0xb10) = s + 0x8c;
    Field<uint8_t*>(s, 0xb28) = s + 0xa74;
    Field<uint32_t>(s, 0xb18) = 0x005ca7a4;
    Field<uint8_t*>(s, 0xb1c) = s + 0x980;
    Field<uint32_t>(s, 0xb24) = 0x005ca7b8;
    Field<uint32_t>(s, 0xb30) = 0x005ca7cc;
    Field<uint16_t>(s, 0x16b0) = 0;
    Field<uint32_t>(s, 0x16b4) = 0;
    Field<uint32_t>(s, 0x16ac) = 8;
    return Zlib_InitBlock(s);
}

// 0x00501230 (eax = deflate state, edi = tree; [k], caller pops; ECX is only saved): zlib pqdownheap(): sift
// heap[k] down; a node is smaller with a lower Freq, ties broken by depth (+0x1450) <=.
void Zlib_PqDownHeap(uint8_t* s, ZlibCtData* tree, int32_t k)
{
    int32_t* heap = ZHeap(s);
    uint8_t* depth = ZDepth(s);
    auto smaller = [&](int32_t n, int32_t m) {
        return tree[n].freq < tree[m].freq || (tree[n].freq == tree[m].freq && depth[n] <= depth[m]);
    };
    int32_t v = heap[k];
    int32_t j = k << 1;
    while (j <= ZHeapLen(s))
    {
        if (j < ZHeapLen(s) && smaller(heap[j + 1], heap[j]))
            ++j;
        if (smaller(v, heap[j]))
            break;
        heap[k] = heap[j];
        k = j;
        j <<= 1;
    }
    heap[k] = v;
}

// 0x00501300 (eax = tree_desc, edx = deflate state): zlib gen_bitlen(): compute the optimal bit lengths of the
// desc's tree (+0 dyn_tree, +4 max_code, +8 stat_desc: +0 static_tree, +4 extra_bits, +8 extra_base,
// +0x10 max_length) from the heap built by build_tree, update bl_count (+0xb34), opt_len and static_len, and fix
// lengths that overflow max_length.
uint32_t Zlib_GenBitlen(uint8_t* desc, uint8_t* s)
{
    auto* tree = Field<ZlibCtData*>(desc, 0);
    int32_t maxCode = Field<int32_t>(desc, 4);
    uint8_t* stat = Field<uint8_t*>(desc, 8);
    auto* stree = Field<ZlibCtData*>(stat, 0);
    auto* extra = Field<const int32_t*>(stat, 4);
    int32_t base = Field<int32_t>(stat, 8);
    int32_t maxLength = Field<int32_t>(stat, 0x10);
    uint16_t* blCount = ZBlCount(s);
    int32_t* heap = ZHeap(s);
    for (int bits = 0; bits <= 15; ++bits)
        blCount[bits] = 0;

    tree[heap[ZHeapMax(s)]].len = 0;
    int32_t overflow = 0;
    int32_t h;
    for (h = ZHeapMax(s) + 1; h < kHeapSize; ++h)
    {
        int32_t n = heap[h];
        int32_t bits = tree[tree[n].len].len + 1;
        if (bits > maxLength)
        {
            bits = maxLength;
            ++overflow;
        }
        tree[n].len = static_cast<uint16_t>(bits);
        if (n > maxCode)
            continue;
        blCount[bits]++;
        int32_t xbits = 0;
        if (n >= base)
            xbits = extra[n - base];
        uint32_t f = tree[n].freq;
        ZOptLen(s) += static_cast<uint32_t>(bits + xbits) * f;
        if (stree)
            ZStaticLen(s) += static_cast<uint32_t>(stree[n].len + xbits) * f;
    }
    if (overflow == 0)
        return 0;
    do
    {
        int32_t bits = maxLength - 1;
        while (blCount[bits] == 0)
            --bits;
        blCount[bits]--;
        blCount[bits + 1] += 2;
        blCount[maxLength]--;
        overflow -= 2;
    } while (overflow > 0);
    for (int32_t bits = maxLength; bits != 0; --bits)
    {
        uint32_t n = blCount[bits];
        while (n != 0)
        {
            int32_t m = heap[--h];
            if (m > maxCode)
                continue;
            if (tree[m].len != static_cast<uint32_t>(bits))
            {
                ZOptLen(s) += static_cast<uint32_t>(bits - tree[m].len) * tree[m].freq;
                tree[m].len = static_cast<uint16_t>(bits);
            }
            --n;
        }
    }
    return 0;
}

// 0x00501510 (edx = bl_count, ebx = max_code, edi = tree; /GS cookie): zlib gen_codes(): next_code per bit length
// from bl_count, then each used code (Len +2) gets its bit-reversed next code in Code (+0). Not hooked (see top).
void Zlib_GenCodes(ZlibCtData* tree, int32_t maxCode, const uint16_t* blCount)
{
    uint16_t nextCode[16];
    uint16_t code = 0;
    for (int bits = 1; bits <= 15; ++bits)
    {
        code = static_cast<uint16_t>((code + blCount[bits - 1]) << 1);
        nextCode[bits] = code;
    }
    for (int32_t n = 0; n <= maxCode; ++n)
    {
        uint32_t len = tree[n].len;
        if (len == 0)
            continue;
        uint32_t c = nextCode[len]++;
        uint32_t res = 0;
        do
        {
            res |= c & 1;
            c >>= 1;
            res <<= 1;
        } while (--len > 0);
        tree[n].freq = static_cast<uint16_t>(res >> 1);
    }
}

// 0x005015a0 (esi = deflate state; [tree_desc], caller pops; EBP is only saved): zlib build_tree(): put the used
// symbols of desc->dyn_tree (stat_desc +0xc elems) in the heap (forcing at least two), store max_code (desc +4),
// combine the two least frequent nodes until one is left (pqdownheap 0x00501230), then gen_bitlen (0x00501300) and
// gen_codes (0x00501510).
uint32_t Zlib_BuildTree(uint8_t* s, uint8_t* desc)
{
    auto* tree = Field<ZlibCtData*>(desc, 0);
    uint8_t* stat = Field<uint8_t*>(desc, 8);
    auto* stree = Field<ZlibCtData*>(stat, 0);
    int32_t elems = Field<int32_t>(stat, 0xc);
    int32_t* heap = ZHeap(s);
    uint8_t* depth = ZDepth(s);
    int32_t maxCode = -1;
    ZHeapLen(s) = 0;
    ZHeapMax(s) = kHeapSize;
    for (int32_t n = 0; n < elems; ++n)
    {
        if (tree[n].freq != 0)
        {
            heap[++ZHeapLen(s)] = n;
            maxCode = n;
            depth[n] = 0;
        }
        else
        {
            tree[n].len = 0;
        }
    }
    while (ZHeapLen(s) < 2)
    {
        int32_t node = maxCode < 2 ? ++maxCode : 0;
        heap[++ZHeapLen(s)] = node;
        tree[node].freq = 1;
        depth[node] = 0;
        ZOptLen(s)--;
        if (stree)
            ZStaticLen(s) -= stree[node].len;
    }
    Field<int32_t>(desc, 4) = maxCode;
    for (int32_t n = ZHeapLen(s) / 2; n >= 1; --n)
        Zlib_PqDownHeap(s, tree, n);
    int32_t node = elems;
    do
    {
        int32_t n = heap[1];
        heap[1] = heap[ZHeapLen(s)--];
        Zlib_PqDownHeap(s, tree, 1);
        int32_t m = heap[1];
        heap[--ZHeapMax(s)] = n;
        heap[--ZHeapMax(s)] = m;
        tree[node].freq = static_cast<uint16_t>(tree[m].freq + tree[n].freq);
        uint8_t dm = depth[m], dn = depth[n];
        depth[node] = static_cast<uint8_t>((dn >= dm ? dn : dm) + 1);
        tree[m].len = static_cast<uint16_t>(node);
        tree[n].len = static_cast<uint16_t>(node);
        heap[1] = node++;
        Zlib_PqDownHeap(s, tree, 1);
    } while (ZHeapLen(s) >= 2);
    heap[--ZHeapMax(s)] = heap[1];
    Zlib_GenBitlen(desc, s);
    Zlib_GenCodes(tree, maxCode, ZBlCount(s));
    return 0;
}

static uint32_t fn_00500d60_Abi(uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_00500d60(reinterpret_cast<uint8_t*>(esi), a0, a1, reinterpret_cast<uint8_t*>(a2), a3,
                       reinterpret_cast<uint32_t*>(a4));
}
static uint32_t RiffChunk_Find_Abi(uint32_t esi, uint32_t listType)
{
    return RiffChunk_Find(reinterpret_cast<uint8_t*>(esi), listType);
}
static uint32_t RiffChunk_Read_Abi(uint32_t eax, uint32_t edx, uint32_t edi, uint32_t buffer)
{
    return RiffChunk_Read(reinterpret_cast<uint8_t*>(eax), edx, edi, reinterpret_cast<void*>(buffer));
}
static uint32_t WaveFile_Init_Abi(uint32_t edx)
{
    return u(WaveFile_Init(reinterpret_cast<uint8_t*>(edx)));
}
static uint32_t WaveFile_Open_Abi(uint32_t ebx, uint32_t path)
{
    return WaveFile_Open(reinterpret_cast<uint8_t*>(ebx), path);
}
static uint32_t WaveFile_ReadData_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t ebx, uint32_t buffer)
{
    return WaveFile_ReadData(eax, ecx, reinterpret_cast<uint8_t*>(edx), reinterpret_cast<uint32_t*>(ebx),
                             reinterpret_cast<void*>(buffer));
}
static uint32_t Zlib_TrInit_Abi(uint32_t edx)
{
    return Zlib_TrInit(reinterpret_cast<uint8_t*>(edx));
}
static uint32_t Zlib_InitBlock_Abi(uint32_t edx)
{
    return Zlib_InitBlock(reinterpret_cast<uint8_t*>(edx));
}
static void Zlib_PqDownHeap_Abi(uint32_t eax, uint32_t /*ecx*/, uint32_t edi, uint32_t k)
{
    Zlib_PqDownHeap(reinterpret_cast<uint8_t*>(eax), reinterpret_cast<ZlibCtData*>(edi), static_cast<int32_t>(k));
}
static uint32_t Zlib_GenBitlen_Abi(uint32_t eax, uint32_t edx)
{
    return Zlib_GenBitlen(reinterpret_cast<uint8_t*>(eax), reinterpret_cast<uint8_t*>(edx));
}
static uint32_t Zlib_BuildTree_Abi(uint32_t esi, uint32_t /*ebp*/, uint32_t desc, uint32_t /*a1*/, uint32_t /*a2*/)
{
    return Zlib_BuildTree(reinterpret_cast<uint8_t*>(esi), reinterpret_cast<uint8_t*>(desc));
}
ABI_ENTRY_00500d60(fn_00500d60_Entry, fn_00500d60_Abi)
ABI_ENTRY_00500e10(RiffChunk_Find_Entry, RiffChunk_Find_Abi)
ABI_ENTRY_00500ed0(RiffChunk_Read_Entry, RiffChunk_Read_Abi)
ABI_ENTRY_00500f10(WaveFile_Init_Entry, WaveFile_Init_Abi)
ABI_ENTRY_00500ff0(WaveFile_Open_Entry, WaveFile_Open_Abi)
ABI_ENTRY_00501100(WaveFile_ReadData_Entry, WaveFile_ReadData_Abi)
ABI_ENTRY_00501160(Zlib_TrInit_Entry, Zlib_TrInit_Abi)
ABI_ENTRY_005011c0(Zlib_InitBlock_Entry, Zlib_InitBlock_Abi)
ABI_ENTRY_00501230(Zlib_PqDownHeap_Entry, Zlib_PqDownHeap_Abi)
ABI_ENTRY_00501300(Zlib_GenBitlen_Entry, Zlib_GenBitlen_Abi)
ABI_ENTRY_005015a0(Zlib_BuildTree_Entry, Zlib_BuildTree_Abi)
HOOK(0x00500d60, "fn_00500d60", fn_00500d60_Entry, nullptr);
HOOK(0x00500e10, "RiffChunk_Find", RiffChunk_Find_Entry, nullptr);
HOOK(0x00500ed0, "RiffChunk_Read", RiffChunk_Read_Entry, nullptr);
HOOK(0x00500f10, "WaveFile_Init", WaveFile_Init_Entry, nullptr);
HOOK(0x00500ff0, "WaveFile_Open", WaveFile_Open_Entry, nullptr);
HOOK(0x00501100, "WaveFile_ReadData", WaveFile_ReadData_Entry, nullptr);
HOOK(0x00501160, "Zlib_TrInit", Zlib_TrInit_Entry, nullptr);
HOOK(0x005011c0, "Zlib_InitBlock", Zlib_InitBlock_Entry, nullptr);
HOOK(0x00501230, "Zlib_PqDownHeap", Zlib_PqDownHeap_Entry, nullptr);
HOOK(0x00501300, "Zlib_GenBitlen", Zlib_GenBitlen_Entry, nullptr);
HOOK(0x005015a0, "Zlib_BuildTree", Zlib_BuildTree_Entry, nullptr);
