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

// A member not yet named in a struct (S2 code): Field<uint32_t>(obj, 0x3d0). Name it in the struct at S3.
template <class T>
inline T& Field(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}

// Call virtual method `slot` of a game object (thiscall): VCall<int>(obj, 8, a, b).
template <class R, class... A>
inline R VCall(void* object, int slot, A... args)
{
    auto fn = reinterpret_cast<R(__fastcall*)(void*, void*, A...)>((*reinterpret_cast<void***>(object))[slot]);
    return fn(object, nullptr, args...);
}

// The game's own printf-style debug log (0x004e0b30, cdecl, variadic).
template <class... A>
inline void GameLog(const char* format, A... args)
{
    reinterpret_cast<void(__cdecl*)(const char*, ...)>(0x004e0b30)(format, args...);
}
