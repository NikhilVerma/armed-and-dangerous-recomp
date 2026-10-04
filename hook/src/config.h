#pragma once

// and.ini next to the game executable. Every hook and every enhancement has a switch here.
// Enhancements default to off so that tests run the faithful game (docs/decision.md).
//
// [hooks]      one key per replaced original function, by name: 1 = replace, 0 = original, 2 = differential
// [display]    enhancement switches (hook/enhance/display.cpp)
// [render]     rendering enhancements (hook/enhance/render.cpp): wireframe; rhi=1 renderer interface,
//              backend=d3d9|vulkan (src/rhi)

int ConfigInt(const char* section, const char* key, int fallback);
float ConfigFloat(const char* section, const char* key, float fallback);
void ConfigString(const char* section, const char* key, const char* fallback, char* out, unsigned size);
