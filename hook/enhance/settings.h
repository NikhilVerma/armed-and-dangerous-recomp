#pragma once
// Remaster settings registry (docs/goals/remaster-basics.md: "each item has an and.ini switch and an entry on a
// remaster options page"). Each enhancement registers the and.ini values it reads, with a label, a range and an
// optional live-apply callback; the in-game Remaster page (hook/enhance/remaster_menu.cpp) lists them, and changes
// are written back to and.ini.

#include <vector>

struct RemasterSetting
{
    enum Kind { Toggle, Slider, List };
    const char* section;  // and.ini section and key
    const char* key;
    const wchar_t* label;  // shown on the Remaster page
    Kind kind;
    float minimum, maximum, step;      // Slider: value range and step; Toggle: 0 / 1
    std::vector<const wchar_t*> names;  // List: one label per value 0..n-1
    float value;                        // current value (read from and.ini at registration)
    bool restart;                       // takes effect after a restart (shown on the page)
    void (*apply)(float value);         // live apply (null: only written to and.ini)
};

// Registers a setting and returns its current value (and.ini, or `fallback`). Call from the enhancements' Init.
float RemasterRegister(RemasterSetting setting, float fallback);

// All registered settings, in registration order.
std::vector<RemasterSetting>& RemasterSettings();

// Changes a setting: stores it, writes and.ini and calls its apply callback.
void RemasterSet(size_t index, float value);
