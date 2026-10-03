// Recovered: string parsing helpers. S2, Approach B.
//   0x0050cb40 ParseHex
#include "game.h"
#include "gen/entry_stubs.h"
#include "hooks.h"

// 0x0050cb40 (edx = text): value of the leading hexadecimal digits (either case); stops at the first other
// character. No overflow check (wraps).
uint32_t ParseHex(const char* text)
{
    uint32_t value = 0;
    for (char c = *text; c != '\0'; c = *text)
    {
        text++;
        if (c >= '0' && c <= '9')
            value = value * 16 + (c - '0');
        else if (c >= 'a' && c <= 'f')
            value = value * 16 + (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            value = value * 16 + (c - 'A' + 10);
        else
            break;
    }
    return value;
}

static uint32_t ParseHex_Abi(uint32_t edx)
{
    return ParseHex(reinterpret_cast<const char*>(edx));
}
ABI_ENTRY_0050cb40(ParseHex_Entry, ParseHex_Abi)
HOOK(0x0050cb40, "ParseHex", ParseHex_Entry, nullptr);
