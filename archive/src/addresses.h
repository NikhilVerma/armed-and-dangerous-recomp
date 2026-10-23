#pragma once
// Original functions and globals used by recovered code, by address in game.unpacked.exe.
// Keep in sync with symbols/functions.csv and symbols/globals.csv. Names follow the tracker.

#include "game.h"

namespace orig
{
// --- functions (not yet recovered) --------------------------------------------------------------------------
inline void Timer_Init() { GameFunction<void (*)()>(0x004e4560)(); }        // QPF/QPC baseline, 1/60 step
inline double Timer_Seconds() { return GameFunction<double (*)()>(0x004e4520)(); }  // result in ST0
inline void* Singleton_004e43f0() { return GameFunction<void* (*)()>(0x004e43f0)(); }  // create-or-addref
inline void* Singleton_004fbab0() { return GameFunction<void* (*)()>(0x004fbab0)(); }  // create-or-addref
inline void srand(unsigned seed) { GameFunction<void(__cdecl*)(unsigned)>(0x005391c0)(seed); }  // game's CRT

// --- imports --------------------------------------------------------------------------------------------------
inline unsigned long GetTickCount() { return (*reinterpret_cast<unsigned long(__stdcall**)()>(0x0056b088))(); }

// --- globals --------------------------------------------------------------------------------------------------
inline float& g_timerStartSeconds = GameGlobal<float>(0x00604aa8);
inline int& g_timerInitCount = GameGlobal<int>(0x00604aac);
inline unsigned long& g_startTickCount = GameGlobal<unsigned long>(0x00604af8);
}  // namespace orig
