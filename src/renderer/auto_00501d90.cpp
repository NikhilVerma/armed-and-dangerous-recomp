// Recovered: zlib deflate trees (trees.c), statically linked next to the renderer. S2, Approach B.
// The layout matches zlib 1.2.x deflate_state: pending_buf +0x08, pending +0x14, dyn_ltree +0x8c, dyn_dtree +0x980,
// l_buf +0x1690, last_lit +0x1698, d_buf +0x169c, last_eob_len +0x16ac, bi_buf (16 bits) +0x16b0, bi_valid +0x16b4.
// Tables: extra_lbits 0x0057db00, extra_dbits 0x0057db78, bl_order 0x0057dc3c, static_ltree 0x0057dc50,
// static_dtree 0x0057e0d0, _dist_code 0x0057e148, _length_code 0x0057e348, base_length 0x0057e448, base_dist 0x0057e4c0.
// Callers: Zlib_Deflate (0x004f6630) calls 0x00502070 and 0x00502110.
//   0x00502070 Zlib_TrStoredBlock
//   0x00502110 Zlib_TrAlign
//   0x005024e0 Zlib_CompressBlock
// Skipped:
//   0x00501d90 (build_bl_tree) calls 0x005017a0 (scan_tree), which reads its caller-pushed stack argument (the
//     state, [esp+0x18] after its pushes) although symbols/abi.csv lists it with stack 0, so orig::fn_005017a0
//     cannot pass it.
//   0x00501e00 (send_all_trees) reads three caller-pushed stack arguments (lcodes, dcodes, blcodes at
//     [esp+0x14]/[esp+0xc]/[esp+0x14] after its pushes) that symbols/abi.csv does not list (stack 0), so its entry
//     stub cannot forward them; its callee 0x00501880 has the same problem.
//   0x00502300 (_tr_flush_block) reads a caller-pushed stack argument (buf, [esp+0x10] after three pushes) that
//     symbols/abi.csv does not list (stack 0), so its entry stub cannot forward it.
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

const uint8_t* const kLengthCode = reinterpret_cast<const uint8_t*>(0x0057e348);
const uint8_t* const kDistCode = reinterpret_cast<const uint8_t*>(0x0057e148);
const int32_t* const kExtraLBits = reinterpret_cast<const int32_t*>(0x0057db00);
const int32_t* const kExtraDBits = reinterpret_cast<const int32_t*>(0x0057db78);
const int32_t* const kBaseLength = reinterpret_cast<const int32_t*>(0x0057e448);
const int32_t* const kBaseDist = reinterpret_cast<const int32_t*>(0x0057e4c0);

// zlib send_bits (inlined everywhere in the original): append `length` bits of `value` to bi_buf (+0x16b0);
// when bi_valid (+0x16b4) exceeds 16 - length, flush the 16-bit buffer to pending_buf (+0x08) at pending (+0x14),
// low byte first, and keep the bits that did not fit. The comparison is signed (jle).
void SendBits(uint8_t* s, uint32_t value, int32_t length)
{
    int32_t valid = Field<int32_t>(s, 0x16b4);
    uint16_t& buf = Field<uint16_t>(s, 0x16b0);
    buf = static_cast<uint16_t>(buf | static_cast<uint16_t>(value << valid));
    if (valid > 16 - length)
    {
        uint32_t& pending = Field<uint32_t>(s, 0x14);
        Field<uint8_t*>(s, 0x08)[pending] = static_cast<uint8_t>(buf);
        pending++;
        Field<uint8_t*>(s, 0x08)[pending] = static_cast<uint8_t>(buf >> 8);
        pending++;
        buf = static_cast<uint16_t>(static_cast<uint16_t>(value) >> (16 - valid));
        Field<int32_t>(s, 0x16b4) = valid + length - 16;
    }
    else
    {
        Field<int32_t>(s, 0x16b4) = valid + length;
    }
}

// zlib send_code: a Huffman code from a ct_data tree (4-byte entries: code word +0, length word +2).
void SendCode(uint8_t* s, uint32_t symbol, const uint16_t* tree)
{
    SendBits(s, tree[symbol * 2], tree[symbol * 2 + 1]);
}
}  // namespace

// 0x00502070 (eax = deflate state, ecx = eof; [buf, stored_len]): zlib _tr_stored_block. Sends the 3-bit block
// header (STORED_BLOCK << 1) + eof, then copy_block (0x00502a80: eax = state, ecx = length, edx = buf,
// [header = 1]) writes the length, its complement and the data. Returns copy_block's EAX.
// (not executed in the comparison scenarios)
uint32_t Zlib_TrStoredBlock(uint8_t* s, uint32_t eof, const uint8_t* buf, uint32_t storedLen)
{
    SendBits(s, eof, 3);
    return orig::fn_00502a80(u(s), storedLen, u(buf), 1);
}

// 0x00502110 (eax = deflate state): zlib _tr_align. Sends an empty static block (STATIC_TREES << 1 in 3 bits, then
// END_BLOCK from static_ltree: code 0, 7 bits) and flushes with bi_flush (0x00502990). If the last EOB plus this
// one leave fewer than 9 bits of lookahead (1 + last_eob_len +0x16ac + 10 - bi_valid < 9, signed), sends a second
// empty static block and flushes again. Sets last_eob_len to 7. (not executed in the comparison scenarios)
void Zlib_TrAlign(uint8_t* s)
{
    SendBits(s, 2, 3);
    SendBits(s, 0, 7);
    orig::fn_00502990(u(s));
    if (Field<int32_t>(s, 0x16ac) - Field<int32_t>(s, 0x16b4) + 11 < 9)
    {
        SendBits(s, 2, 3);
        SendBits(s, 0, 7);
        orig::fn_00502990(u(s));
    }
    Field<int32_t>(s, 0x16ac) = 7;
}

// 0x005024e0 (eax = deflate state; [ltree, dtree]): zlib compress_block. For each of last_lit (+0x1698, unsigned
// loop) symbols: distance d_buf[i] (+0x169c, words) and literal/length l_buf[i] (+0x1690, bytes). Distance 0 sends
// the literal with ltree; otherwise sends the length code (ltree entry 257 + _length_code[lc]) plus its extra bits
// (extra_lbits, base_length), then dist - 1's code (_dist_code[dist] below 256, else _dist_code[256 + (dist >> 7)])
// with dtree plus its extra bits (extra_dbits, base_dist). Ends with END_BLOCK (ltree[256]) and stores its length
// in last_eob_len (+0x16ac).
void Zlib_CompressBlock(uint8_t* s, const uint16_t* ltree, const uint16_t* dtree)
{
    uint32_t lx = 0;
    if (Field<uint32_t>(s, 0x1698) != 0)
    {
        do
        {
            uint32_t dist = Field<const uint16_t*>(s, 0x169c)[lx];
            uint32_t lc = Field<const uint8_t*>(s, 0x1690)[lx];
            lx++;
            if (dist == 0)
            {
                SendCode(s, lc, ltree);
            }
            else
            {
                uint32_t code = kLengthCode[lc];
                SendCode(s, code + 257, ltree);
                int32_t extra = kExtraLBits[code];
                if (extra != 0)
                {
                    lc -= kBaseLength[code];
                    SendBits(s, lc, extra);
                }
                dist--;
                code = dist < 256 ? kDistCode[dist] : kDistCode[256 + (dist >> 7)];
                SendCode(s, code, dtree);
                extra = kExtraDBits[code];
                if (extra != 0)
                {
                    dist -= kBaseDist[code];
                    SendBits(s, dist, extra);
                }
            }
        } while (lx < Field<uint32_t>(s, 0x1698));
    }
    SendCode(s, 256, ltree);
    Field<int32_t>(s, 0x16ac) = ltree[256 * 2 + 1];
}

static uint32_t Zlib_TrStoredBlock_Abi(uint32_t eax, uint32_t ecx, uint32_t buf, uint32_t storedLen)
{
    return Zlib_TrStoredBlock(reinterpret_cast<uint8_t*>(eax), ecx, reinterpret_cast<const uint8_t*>(buf), storedLen);
}
static uint32_t Zlib_TrAlign_Abi(uint32_t eax)
{
    Zlib_TrAlign(reinterpret_cast<uint8_t*>(eax));
    return 0;
}
static uint32_t Zlib_CompressBlock_Abi(uint32_t eax, uint32_t ltree, uint32_t dtree)
{
    Zlib_CompressBlock(reinterpret_cast<uint8_t*>(eax), reinterpret_cast<const uint16_t*>(ltree),
                       reinterpret_cast<const uint16_t*>(dtree));
    return 0;
}
ABI_ENTRY_00502070(Zlib_TrStoredBlock_Entry, Zlib_TrStoredBlock_Abi)
ABI_ENTRY_00502110(Zlib_TrAlign_Entry, Zlib_TrAlign_Abi)
ABI_ENTRY_005024e0(Zlib_CompressBlock_Entry, Zlib_CompressBlock_Abi)
HOOK(0x00502070, "Zlib_TrStoredBlock", Zlib_TrStoredBlock_Entry, nullptr);
HOOK(0x00502110, "Zlib_TrAlign", Zlib_TrAlign_Entry, nullptr);
HOOK(0x005024e0, "Zlib_CompressBlock", Zlib_CompressBlock_Entry, nullptr);
