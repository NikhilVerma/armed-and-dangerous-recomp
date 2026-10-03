#pragma once
// Access to the original game image (game.unpacked.exe, SHA-256 f6a690a4...) from recovered code.
//
// Recovered code calls not-yet-recovered functions and touches original globals by address (handover s8).
// Every address used anywhere in src/ is declared in one place: src/addresses.h.

#include <cstddef>
#include <cstdint>

#define GAME_STATIC_ASSERT_SIZE(type, size) static_assert(sizeof(type) == (size), #type " size")
#define GAME_STATIC_ASSERT_OFFSET(type, member, offset) \
    static_assert(offsetof(type, member) == (offset), #type "::" #member " offset")

template <class T>
inline T& GameGlobal(uintptr_t address)
{
    return *reinterpret_cast<T*>(address);
}

template <class Fn>
inline Fn GameFunction(uintptr_t address)
{
    return reinterpret_cast<Fn>(address);
}
