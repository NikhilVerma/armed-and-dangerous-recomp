#pragma once

// and.ini next to the game executable. Every hook and every enhancement has a switch here.
// Remaster enhancements default to on; [remaster] original_defaults=1 turns every default back to the original game
// (the comparison harness sets it so tests run the faithful game; docs/decision.md).
//
// [hooks]      one key per replaced original function, by name: 1 = replace, 0 = original, 2 = differential
// [display]    enhancement switches (hook/enhance/display.cpp)
// [render]     rendering enhancements (hook/enhance/render.cpp): wireframe; rhi=1 renderer interface,
//              backend=d3d9|vulkan (src/rhi)

const char* ConfigIniPath();  // and.ini next to the game executable
int ConfigInt(const char* section, const char* key, int fallback);
float ConfigFloat(const char* section, const char* key, float fallback);
void ConfigString(const char* section, const char* key, const char* fallback, char* out, unsigned size);

// Remaster defaults (docs/decision.md 2026-10-04): a setting the and.ini does not name takes its remaster default,
// unless [remaster] original_defaults=1, which gives every setting the original game's value (the comparison
// harness sets it, so the tests run the faithful game).
bool ConfigOriginalDefaults();
// Drops the cached values (after writing and.ini).
void ConfigForget();
int ConfigIntDefault(const char* section, const char* key, int original, int remaster);
float ConfigFloatDefault(const char* section, const char* key, float original, float remaster);
