// Remaster settings registry (settings.h).
#include "settings.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "config.h"
#include "log.h"

std::vector<RemasterSetting>& RemasterSettings()
{
    static std::vector<RemasterSetting> all;
    return all;
}

float RemasterRegister(RemasterSetting s, float fallback)
{
    s.value = ConfigFloat(s.section, s.key, fallback);
    RemasterSettings().push_back(s);
    return s.value;
}

void RemasterSet(size_t index, float value)
{
    auto& all = RemasterSettings();
    if (index >= all.size())
        return;
    RemasterSetting& s = all[index];
    if (value < s.minimum)
        value = s.minimum;
    if (value > s.maximum)
        value = s.maximum;
    s.value = value;
    char ini[MAX_PATH], text[32];
    GetModuleFileNameA(nullptr, ini, MAX_PATH);
    strcpy(strrchr(ini, '\\') + 1, "and.ini");
    if (s.kind == RemasterSetting::Slider)
        snprintf(text, sizeof(text), "%.2f", value);
    else
        snprintf(text, sizeof(text), "%d", static_cast<int>(value + 0.5f));
    WritePrivateProfileStringA(s.section, s.key, text, ini);
    Log("remaster: [%s] %s=%s%s", s.section, s.key, text, s.restart ? " (after a restart)" : "");
    if (s.apply)
        s.apply(value);
}
