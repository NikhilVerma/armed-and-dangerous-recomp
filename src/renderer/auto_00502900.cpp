// Recovered: statically linked zlib 1.1.x (trees.c bit output, adler32.c, infblock.c), placed in the renderer's
// address range. S2, Approach B. Structure layouts follow zlib 1.1 (deflate_state, inflate_blocks_state,
// z_stream); evidence: the zlib messages at 0x589840..0x5898a0 and the border table at 0x57dab0.
//   0x00502900 Zlib_SetDataType
//   0x00502990 Zlib_BiFlush
//   0x00502a10 Zlib_BiWindup
//   0x00502a80 Zlib_CopyBlock
//   0x00502b10 Zlib_Adler32
//   0x00502c40 Zlib_InflateBlocksReset
//   0x00502d40 Zlib_InflateBlocks
// Skipped: 0x00502cb0 (zlib inflate_blocks_new) reads a caller-cleaned stack argument ([esp+0xc] after two
// pushes, the check function) that symbols/abi.csv does not list (stack 0), so its generated entry stub cannot
// forward it.
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "renderer.h"

#include <string.h>

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}

typedef uint32_t(__cdecl* ZAllocFn)(uint32_t opaque, uint32_t items, uint32_t size);
typedef void(__cdecl* ZFreeFn)(uint32_t opaque, uint32_t address);
typedef uint32_t(__cdecl* CheckFn)(uint32_t, uint32_t, uint32_t);

const char* const kStrInvalidBlockType = reinterpret_cast<const char*>(0x005898a0);         // "invalid block type"
const char* const kStrInvalidStoredLengths = reinterpret_cast<const char*>(0x00589880);     // "invalid stored block lengths"
const char* const kStrTooManySymbols = reinterpret_cast<const char*>(0x0058985c);           // "too many length or distance symbols"
const char* const kStrInvalidRepeat = reinterpret_cast<const char*>(0x00589840);            // "invalid bit length repeat"

constexpr uint32_t kBorder = 0x0057dab0;       // uInt border[19]: order of the bit length code lengths
constexpr uint32_t kInflateMask = 0x005c94b0;  // uInt inflate_mask[17]: (1 << n) - 1
constexpr uint32_t kFixedBl = 0x005c94f4;      // fixed literal/length tree bits
constexpr uint32_t kFixedBd = 0x005c94f8;      // fixed distance tree bits
constexpr uint32_t kFixedTl = 0x005c9500;      // fixed literal/length tree
constexpr uint32_t kFixedTd = 0x005ca500;      // fixed distance tree

// deflate_state: pending_buf +0x08, pending +0x14, bi_buf (ush) +0x16b0, bi_valid +0x16b4.
void PutByte(uint8_t* s, uint8_t c)
{
    Field<uint8_t*>(s, 8)[Field<uint32_t>(s, 0x14)] = c;
    Field<uint32_t>(s, 0x14) += 1;
}
}  // namespace

void Zlib_BiWindup(uint8_t* s);

// 0x00502900 (ecx = deflate_state): zlib 1.1 set_data_type. Sums the literal frequencies of dyn_ltree (+0x8c,
// 4-byte entries, freq word first): codes 0-6 and 128-255 as binary, 7-127 as text; data_type (+0x1c) = 0
// (Z_BINARY) when bin > ascii / 4, else 1 (Z_ASCII). Returns EAX as left: the binary sum with AL = data type.
// (not executed in the comparison scenarios)
uint32_t Zlib_SetDataType(uint8_t* s)
{
    uint32_t bin = 0;
    for (uint32_t n = 0; n < 7; n++)
        bin += Field<uint16_t>(s, 0x8c + n * 4);
    uint32_t ascii = 0;
    for (uint32_t n = 7; n < 128; n++)
        ascii += Field<uint16_t>(s, 0x8c + n * 4);
    for (uint32_t n = 128; n < 256; n++)
        bin += Field<uint16_t>(s, 0x8c + n * 4);
    uint8_t type = (ascii >> 2) < bin ? 0 : 1;
    Field<uint8_t>(s, 0x1c) = type;
    return (bin & 0xffffff00u) | type;
}

// 0x00502990 (eax = deflate_state): zlib 1.1 bi_flush. With 16 bits in bi_buf (+0x16b0) write both bytes to
// pending_buf (+0x08 at pending +0x14) and clear; with 8 or more (signed jl) write the low byte and keep the high.
// (not executed in the comparison scenarios)
void Zlib_BiFlush(uint8_t* s)
{
    int32_t valid = Field<int32_t>(s, 0x16b4);
    if (valid == 16)
    {
        PutByte(s, Field<uint8_t>(s, 0x16b0));
        PutByte(s, Field<uint8_t>(s, 0x16b1));
        Field<uint16_t>(s, 0x16b0) = 0;
        Field<int32_t>(s, 0x16b4) = 0;
        return;
    }
    if (valid >= 8)
    {
        Field<uint8_t*>(s, 8)[Field<uint32_t>(s, 0x14)] = Field<uint8_t>(s, 0x16b0);
        Field<uint16_t>(s, 0x16b0) = Field<uint8_t>(s, 0x16b1);
        Field<uint32_t>(s, 0x14) += 1;
        Field<int32_t>(s, 0x16b4) = Field<int32_t>(s, 0x16b4) - 8;
    }
}

// 0x00502a10 (eax = deflate_state): zlib 1.1 bi_windup. Writes the remaining bits of bi_buf (+0x16b0): two bytes
// when bi_valid (+0x16b4) > 8, one when > 0 (signed compares), then clears bi_buf and bi_valid.
void Zlib_BiWindup(uint8_t* s)
{
    int32_t valid = Field<int32_t>(s, 0x16b4);
    if (valid > 8)
    {
        PutByte(s, Field<uint8_t>(s, 0x16b0));
        PutByte(s, Field<uint8_t>(s, 0x16b1));
    }
    else if (valid > 0)
    {
        PutByte(s, Field<uint8_t>(s, 0x16b0));
    }
    Field<uint16_t>(s, 0x16b0) = 0;
    Field<int32_t>(s, 0x16b4) = 0;
}

// 0x00502a80 (eax = deflate_state, ecx = length, edx = data; [header]): zlib 1.1 copy_block. bi_windup
// (0x00502a10), last_eob_len (+0x16ac) = 8, with header write len and ~len as little-endian shorts, then copy the
// stored bytes to pending_buf. Returns the state (EAX is preserved by bi_windup).
// (not executed in the comparison scenarios)
uint8_t* Zlib_CopyBlock(uint8_t* s, uint32_t len, const uint8_t* buf, uint32_t header)
{
    Zlib_BiWindup(s);
    Field<uint32_t>(s, 0x16ac) = 8;
    if (header)
    {
        PutByte(s, static_cast<uint8_t>(len));
        PutByte(s, static_cast<uint8_t>(len >> 8));
        PutByte(s, static_cast<uint8_t>(~len));
        PutByte(s, static_cast<uint8_t>(~len >> 8));
    }
    for (; len != 0; len--)
        PutByte(s, *buf++);
    return s;
}

// 0x00502b10 (cdecl [adler, buf, len]): zlib adler32. Returns 1 for a null buffer; otherwise sums in chunks of
// NMAX = 5552 (0x15b0), 16 bytes unrolled (signed chunk compare), each chunk reduced modulo BASE = 65521 (0xfff1).
uint32_t Zlib_Adler32(uint32_t adler, const uint8_t* buf, uint32_t len)
{
    uint32_t s1 = adler & 0xffff;
    uint32_t s2 = adler >> 16;
    if (!buf)
        return 1;
    while (len > 0)
    {
        int32_t k = len < 0x15b0 ? static_cast<int32_t>(len) : 0x15b0;
        len -= static_cast<uint32_t>(k);
        if (k >= 16)
        {
            uint32_t blocks = static_cast<uint32_t>(k) >> 4;
            k -= static_cast<int32_t>(blocks << 4);
            do
            {
                for (int i = 0; i < 16; i++)
                {
                    s1 += buf[i];
                    s2 += s1;
                }
                buf += 16;
            } while (--blocks != 0);
        }
        while (k != 0)
        {
            s1 += *buf++;
            s2 += s1;
            k--;
        }
        s1 %= 0xfff1;
        s2 %= 0xfff1;
    }
    return (s2 << 16) | s1;
}

// 0x00502c40 (eax = adler out or null, esi = inflate_blocks_state, edi = z_stream): zlib 1.1
// inflate_blocks_reset. *c = check (+0x3c); zfree (z+0x24, opaque z+0x28) the bit-length table (+0x0c) in
// BTREE/DTREE (4/5) or the codes (+0x04) in CODES (6); mode = TYPE, bitk = bitb = 0, read = write = window
// (+0x28); with a check function (+0x38) check = z->adler (+0x30) = checkfn(0, 0, 0). Returns that value, or the
// null check function (0).
uint32_t Zlib_InflateBlocksReset(uint32_t* c, uint8_t* s, uint8_t* z)
{
    if (c)
        *c = Field<uint32_t>(s, 0x3c);
    uint32_t mode = Field<uint32_t>(s, 0);
    if (mode == 4 || mode == 5)
        Field<ZFreeFn>(z, 0x24)(Field<uint32_t>(z, 0x28), Field<uint32_t>(s, 0xc));
    if (Field<uint32_t>(s, 0) == 6)
        Field<ZFreeFn>(z, 0x24)(Field<uint32_t>(z, 0x28), Field<uint32_t>(s, 4));
    uint32_t window = Field<uint32_t>(s, 0x28);
    Field<uint32_t>(s, 0x34) = window;
    Field<uint32_t>(s, 0x30) = window;
    CheckFn check = Field<CheckFn>(s, 0x38);
    Field<uint32_t>(s, 0) = 0;
    Field<uint32_t>(s, 0x1c) = 0;
    Field<uint32_t>(s, 0x20) = 0;
    if (!check)
        return 0;
    uint32_t value = check(0, 0, 0);
    Field<uint32_t>(s, 0x3c) = value;
    Field<uint32_t>(z, 0x30) = value;
    return value;
}

// 0x00502d40 (cdecl [s, z, r]): zlib 1.1 inflate_blocks, the block-level state machine (modes TYPE 0, LENS 1,
// STORED 2, TABLE 3, BTREE 4, DTREE 5, CODES 6, DRY 7, DONE 8, BAD 9; jump table 0x0050383c). State layout:
// mode +0, left/table/codes +4, index +8, blens +0xc, bb +0x10, tb +0x14, last +0x18, bitk +0x1c, bitb +0x20,
// hufts +0x24, window +0x28, end +0x2c, read +0x30, write +0x34. z_stream: next_in +0, avail_in +4, total_in +8,
// msg +0x18, zalloc +0x20, zfree +0x24, opaque +0x28. Callees: inflate_flush 0x0050d470, inflate_codes_new
// 0x0050d5b0 (fixed tables from inflate_trees_fixed's globals 0x5c94f4..0x5ca500), inflate_trees_bits 0x0050d250,
// inflate_trees_dynamic 0x0050d2f0, inflate_codes 0x0050d5f0. The dynamic-block inflate_codes_new is inlined
// (zalloc 1 x 0x1c). Error messages are the game's strings 0x589840..0x5898a0.
int32_t Zlib_InflateBlocks(uint8_t* s, uint8_t* z, int32_t r)
{
    uint8_t* p = Field<uint8_t*>(z, 0);
    uint32_t n = Field<uint32_t>(z, 4);
    uint32_t b = Field<uint32_t>(s, 0x20);
    uint32_t k = Field<uint32_t>(s, 0x1c);
    uint8_t* q = Field<uint8_t*>(s, 0x34);
    auto read = [&]() { return Field<uint8_t*>(s, 0x30); };
    auto end = [&]() { return Field<uint8_t*>(s, 0x2c); };
    auto window = [&]() { return Field<uint8_t*>(s, 0x28); };
    auto windowAvail = [&]() {
        return q < read() ? static_cast<uint32_t>(read() - q - 1) : static_cast<uint32_t>(end() - q);
    };
    uint32_t m = windowAvail();

    auto zalloc = [&](uint32_t items, uint32_t size) {
        return Field<ZAllocFn>(z, 0x20)(Field<uint32_t>(z, 0x28), items, size);
    };
    auto zfree = [&](uint32_t address) { Field<ZFreeFn>(z, 0x24)(Field<uint32_t>(z, 0x28), address); };
    auto flush = [&](int32_t result) {
        return static_cast<int32_t>(orig::fn_0050d470(u(s), u(z), static_cast<uint32_t>(result)));
    };
    auto update = [&]() {
        Field<uint32_t>(s, 0x20) = b;
        Field<uint32_t>(s, 0x1c) = k;
        Field<uint32_t>(z, 4) = n;
        Field<uint32_t>(z, 8) += static_cast<uint32_t>(p - Field<uint8_t*>(z, 0));
        Field<uint8_t*>(z, 0) = p;
        Field<uint8_t*>(s, 0x34) = q;
    };
    auto leave = [&](int32_t result) {
        update();
        return flush(result);
    };
    auto needBits = [&](uint32_t j) {
        while (k < j)
        {
            if (n == 0)
                return false;
            n--;
            r = 0;
            b |= static_cast<uint32_t>(*p++) << k;
            k += 8;
        }
        return true;
    };
    auto dumpBits = [&](uint32_t j) {
        b >>= j;
        k -= j;
    };
    auto mask = [](uint32_t bits) { return GameGlobal<uint32_t>(kInflateMask + bits * 4); };
    auto setBad = [&](const char* msg) {
        Field<uint32_t>(s, 0) = 9;
        Field<const char*>(z, 0x18) = msg;
    };

    for (;;)
    {
        switch (Field<uint32_t>(s, 0))
        {
        case 0:  // TYPE
        {
            if (!needBits(3))
                return leave(r);
            uint32_t t = b & 7;
            Field<uint32_t>(s, 0x18) = t & 1;
            switch (t >> 1)
            {
            case 0:  // stored
                dumpBits(3);
                dumpBits(k & 7);
                Field<uint32_t>(s, 0) = 1;
                break;
            case 1:  // fixed
            {
                uint32_t codes = orig::fn_0050d5b0(u(z), GameGlobal<uint32_t>(kFixedBl),
                                                   GameGlobal<uint32_t>(kFixedBd), kFixedTl, kFixedTd);
                Field<uint32_t>(s, 4) = codes;
                if (!codes)
                    return leave(-4);
                dumpBits(3);
                Field<uint32_t>(s, 0) = 6;
                break;
            }
            case 2:  // dynamic
                dumpBits(3);
                Field<uint32_t>(s, 0) = 3;
                break;
            case 3:  // illegal
                dumpBits(3);
                setBad(kStrInvalidBlockType);
                return leave(-3);
            }
            break;
        }
        case 1:  // LENS
            if (!needBits(32))
                return leave(r);
            if ((~b >> 16) != (b & 0xffff))
            {
                setBad(kStrInvalidStoredLengths);
                return leave(-3);
            }
            Field<uint32_t>(s, 4) = b & 0xffff;
            k = 0;
            b = 0;
            if (Field<uint32_t>(s, 4) != 0)
                Field<uint32_t>(s, 0) = 2;
            else
                Field<uint32_t>(s, 0) = Field<uint32_t>(s, 0x18) != 0 ? 7 : 0;
            break;
        case 2:  // STORED
        {
            if (n == 0)
                return leave(r);
            if (m == 0)
            {
                if (q == end() && read() != window())
                {
                    q = window();
                    m = windowAvail();
                }
                if (m == 0)
                {
                    Field<uint8_t*>(s, 0x34) = q;
                    r = flush(r);
                    q = Field<uint8_t*>(s, 0x34);
                    m = windowAvail();
                    if (q == end() && read() != window())
                    {
                        q = window();
                        m = windowAvail();
                    }
                    if (m == 0)
                        return leave(r);
                }
            }
            r = 0;
            uint32_t t = Field<uint32_t>(s, 4);
            if (t > n)
                t = n;
            if (t > m)
                t = m;
            memcpy(q, p, t);
            p += t;
            n -= t;
            q += t;
            m -= t;
            Field<uint32_t>(s, 4) -= t;
            if (Field<uint32_t>(s, 4) != 0)
                break;
            Field<uint32_t>(s, 0) = Field<uint32_t>(s, 0x18) != 0 ? 7 : 0;
            break;
        }
        case 3:  // TABLE
        {
            if (!needBits(14))
                return leave(r);
            uint32_t t = b & 0x3fff;
            Field<uint32_t>(s, 4) = t;
            if ((t & 0x1f) > 29 || ((t >> 5) & 0x1f) > 29)
            {
                setBad(kStrTooManySymbols);
                return leave(-3);
            }
            uint32_t blens = zalloc(258 + (t & 0x1f) + ((t >> 5) & 0x1f), 4);
            Field<uint32_t>(s, 0xc) = blens;
            if (!blens)
                return leave(-4);
            dumpBits(14);
            Field<uint32_t>(s, 8) = 0;
            Field<uint32_t>(s, 0) = 4;
        }
            [[fallthrough]];
        case 4:  // BTREE
        {
            while (Field<uint32_t>(s, 8) < 4 + (Field<uint32_t>(s, 4) >> 10))
            {
                if (!needBits(3))
                    return leave(r);
                uint32_t index = Field<uint32_t>(s, 8);
                Field<uint32_t*>(s, 0xc)[GameGlobal<uint32_t>(kBorder + index * 4)] = b & 7;
                Field<uint32_t>(s, 8) = index + 1;
                dumpBits(3);
            }
            while (Field<uint32_t>(s, 8) < 19)
            {
                uint32_t index = Field<uint32_t>(s, 8);
                Field<uint32_t*>(s, 0xc)[GameGlobal<uint32_t>(kBorder + index * 4)] = 0;
                Field<uint32_t>(s, 8) = index + 1;
            }
            Field<uint32_t>(s, 0x10) = 7;
            int32_t t = static_cast<int32_t>(orig::fn_0050d250(u(s + 0x14), u(z), Field<uint32_t>(s, 0xc),
                                                                u(s + 0x10), u(s + 0x14), Field<uint32_t>(s, 0x24)));
            if (t != 0)
            {
                if (t == -3)
                {
                    zfree(Field<uint32_t>(s, 0xc));
                    Field<uint32_t>(s, 0) = 9;
                }
                return leave(t);
            }
            Field<uint32_t>(s, 8) = 0;
            Field<uint32_t>(s, 0) = 5;
        }
            [[fallthrough]];
        case 5:  // DTREE
        {
            auto total = [&]() {
                uint32_t t = Field<uint32_t>(s, 4);
                return 258 + (t & 0x1f) + ((t >> 5) & 0x1f);
            };
            while (Field<uint32_t>(s, 8) < total())
            {
                uint32_t t = Field<uint32_t>(s, 0x10);
                if (!needBits(t))
                    return leave(r);
                const uint8_t* h = Field<uint8_t*>(s, 0x14) + (b & mask(t)) * 8;
                t = h[1];
                uint32_t c = *reinterpret_cast<const uint32_t*>(h + 4);
                if (c < 16)
                {
                    dumpBits(t);
                    uint32_t index = Field<uint32_t>(s, 8);
                    Field<uint32_t*>(s, 0xc)[index] = c;
                    Field<uint32_t>(s, 8) = index + 1;
                    continue;
                }
                uint32_t i = c == 18 ? 7 : c - 14;
                uint32_t j = c == 18 ? 11 : 3;
                if (!needBits(t + i))
                    return leave(r);
                dumpBits(t);
                j += b & mask(i);
                dumpBits(i);
                i = Field<uint32_t>(s, 8);
                if (i + j > total() || (c == 16 && i < 1))
                {
                    zfree(Field<uint32_t>(s, 0xc));
                    setBad(kStrInvalidRepeat);
                    return leave(-3);
                }
                uint32_t* blens = Field<uint32_t*>(s, 0xc);
                uint32_t value = c == 16 ? blens[i - 1] : 0;
                do
                {
                    blens[i++] = value;
                } while (--j != 0);
                Field<uint32_t>(s, 8) = i;
            }
            Field<uint32_t>(s, 0x14) = 0;
            uint32_t bl = 9;
            uint32_t bd = 6;
            uint32_t tl = 0;
            uint32_t td = 0;
            uint32_t table = Field<uint32_t>(s, 4);
            uint32_t nd = ((table >> 5) & 0x1f) + 1;
            int32_t t = static_cast<int32_t>(orig::fn_0050d2f0(nd, u(z), (table & 0x1f) + 0x101, nd,
                                                                Field<uint32_t>(s, 0xc), u(&bl), u(&bd), u(&tl),
                                                                u(&td), Field<uint32_t>(s, 0x24)));
            if (t != 0)
            {
                if (t == -3)
                {
                    zfree(Field<uint32_t>(s, 0xc));
                    Field<uint32_t>(s, 0) = 9;
                }
                return leave(t);
            }
            uint8_t* codes = reinterpret_cast<uint8_t*>(zalloc(1, 0x1c));
            if (!codes)
                return leave(-4);
            Field<uint8_t>(codes, 0x10) = static_cast<uint8_t>(bl);
            Field<uint8_t>(codes, 0x11) = static_cast<uint8_t>(bd);
            Field<uint32_t>(codes, 0) = 0;  // START
            Field<uint32_t>(codes, 0x14) = tl;
            Field<uint32_t>(codes, 0x18) = td;
            Field<uint8_t*>(s, 4) = codes;
            zfree(Field<uint32_t>(s, 0xc));
            Field<uint32_t>(s, 0) = 6;
        }
            [[fallthrough]];
        case 6:  // CODES
            update();
            r = static_cast<int32_t>(orig::fn_0050d5f0(u(s), u(z), static_cast<uint32_t>(r)));
            if (r != 1)
                return flush(r);
            r = 0;
            zfree(Field<uint32_t>(s, 4));
            p = Field<uint8_t*>(z, 0);
            n = Field<uint32_t>(z, 4);
            b = Field<uint32_t>(s, 0x20);
            k = Field<uint32_t>(s, 0x1c);
            q = Field<uint8_t*>(s, 0x34);
            m = windowAvail();
            if (Field<uint32_t>(s, 0x18) == 0)
            {
                Field<uint32_t>(s, 0) = 0;
                break;
            }
            Field<uint32_t>(s, 0) = 7;
            [[fallthrough]];
        case 7:  // DRY
            Field<uint8_t*>(s, 0x34) = q;
            r = flush(r);
            q = Field<uint8_t*>(s, 0x34);
            if (read() != q)
                return leave(r);
            Field<uint32_t>(s, 0) = 8;
            [[fallthrough]];
        case 8:  // DONE
            return leave(1);
        case 9:  // BAD
            return leave(-3);
        default:
            return leave(-2);
        }
    }
}

static uint32_t Zlib_SetDataType_Abi(uint32_t ecx)
{
    return Zlib_SetDataType(reinterpret_cast<uint8_t*>(ecx));
}
static uint32_t Zlib_BiFlush_Abi(uint32_t eax)
{
    Zlib_BiFlush(reinterpret_cast<uint8_t*>(eax));
    return 0;
}
static uint32_t Zlib_BiWindup_Abi(uint32_t eax)
{
    Zlib_BiWindup(reinterpret_cast<uint8_t*>(eax));
    return 0;
}
static uint32_t Zlib_CopyBlock_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t header)
{
    return u(Zlib_CopyBlock(reinterpret_cast<uint8_t*>(eax), ecx, reinterpret_cast<const uint8_t*>(edx), header));
}
static uint32_t Zlib_Adler32_Abi(uint32_t adler, uint32_t buf, uint32_t len)
{
    return Zlib_Adler32(adler, reinterpret_cast<const uint8_t*>(buf), len);
}
static uint32_t Zlib_InflateBlocksReset_Abi(uint32_t eax, uint32_t esi, uint32_t edi)
{
    return Zlib_InflateBlocksReset(reinterpret_cast<uint32_t*>(eax), reinterpret_cast<uint8_t*>(esi),
                                   reinterpret_cast<uint8_t*>(edi));
}
static uint32_t Zlib_InflateBlocks_Abi(uint32_t s, uint32_t z, uint32_t r)
{
    return static_cast<uint32_t>(Zlib_InflateBlocks(reinterpret_cast<uint8_t*>(s), reinterpret_cast<uint8_t*>(z),
                                                    static_cast<int32_t>(r)));
}
ABI_ENTRY_00502900(Zlib_SetDataType_Entry, Zlib_SetDataType_Abi)
ABI_ENTRY_00502990(Zlib_BiFlush_Entry, Zlib_BiFlush_Abi)
ABI_ENTRY_00502a10(Zlib_BiWindup_Entry, Zlib_BiWindup_Abi)
ABI_ENTRY_00502a80(Zlib_CopyBlock_Entry, Zlib_CopyBlock_Abi)
ABI_ENTRY_00502b10(Zlib_Adler32_Entry, Zlib_Adler32_Abi)
ABI_ENTRY_00502c40(Zlib_InflateBlocksReset_Entry, Zlib_InflateBlocksReset_Abi)
ABI_ENTRY_00502d40(Zlib_InflateBlocks_Entry, Zlib_InflateBlocks_Abi)
HOOK(0x00502900, "Zlib_SetDataType", Zlib_SetDataType_Entry, nullptr);
HOOK(0x00502990, "Zlib_BiFlush", Zlib_BiFlush_Entry, nullptr);
HOOK(0x00502a10, "Zlib_BiWindup", Zlib_BiWindup_Entry, nullptr);
HOOK(0x00502a80, "Zlib_CopyBlock", Zlib_CopyBlock_Entry, nullptr);
HOOK(0x00502b10, "Zlib_Adler32", Zlib_Adler32_Entry, nullptr);
HOOK(0x00502c40, "Zlib_InflateBlocksReset", Zlib_InflateBlocksReset_Entry, nullptr);
HOOK(0x00502d40, "Zlib_InflateBlocks", Zlib_InflateBlocks_Entry, nullptr);
