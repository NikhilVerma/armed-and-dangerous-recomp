#include "config.h"

#include <windows.h>
#include <cstdlib>

static const char* IniPath()
{
    static char path[MAX_PATH];
    if (!path[0])
    {
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        char* slash = strrchr(path, '\\');
        strcpy(slash ? slash + 1 : path, "and.ini");
    }
    return path;
}

int ConfigInt(const char* section, const char* key, int fallback)
{
    return GetPrivateProfileIntA(section, key, fallback, IniPath());
}

float ConfigFloat(const char* section, const char* key, float fallback)
{
    char buf[64];
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), IniPath());
    return buf[0] ? static_cast<float>(atof(buf)) : fallback;
}

void ConfigString(const char* section, const char* key, const char* fallback, char* out, unsigned size)
{
    GetPrivateProfileStringA(section, key, fallback, out, size, IniPath());
}

bool ConfigOriginalDefaults()
{
    static const bool original = ConfigInt("remaster", "original_defaults", 0) != 0;
    return original;
}

int ConfigIntDefault(const char* section, const char* key, int original, int remaster)
{
    return ConfigInt(section, key, ConfigOriginalDefaults() ? original : remaster);
}

float ConfigFloatDefault(const char* section, const char* key, float original, float remaster)
{
    return ConfigFloat(section, key, ConfigOriginalDefaults() ? original : remaster);
}
